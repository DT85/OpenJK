/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "bullet_public.h"
#include "qcommon/cm_public.h"
#include "qcommon/qcommon.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <btBulletDynamicsCommon.h>

cvar_t* phys_playerclip;

void Com_Phys_Init() {
	phys_playerclip = Cvar_Get("phys_playerclip", "1", CVAR_ARCHIVE | CVAR_SYSTEMINFO, "Should physics objects collide with CONTENTS_PLAYERCLIP?");
}

static constexpr float d2r_mult = 0.0174532925f;
static constexpr float r2d_mult = 57.2957795f;

static std::unordered_map<btDynamicsWorld const*, phys_world_t*> world_map;

static void bt2q3_vec3(btVector3 const& bt, float* q3) {
	VectorSet(q3, bt.x(), bt.y(), bt.z());
}

static btVector3 q32bt_vec3(float* q3) {
	return { q3[0], q3[1], q3[2] };
}

struct phys_object_s {

	phys_object_s(btDiscreteDynamicsWorld* parent) {
		this->parent = parent;
	}

	phys_properties_t properties{};

	btVector3 inertia{ 0, 0, 0 };
	btCollisionShape* shape = nullptr;
	btDefaultMotionState* motion_state = nullptr;
	btRigidBody* body = nullptr;

	bool is_compound = false;
	bool is_disabled = false;

	btDiscreteDynamicsWorld* parent = nullptr;

	void set_properties() {
		shape->calculateLocalInertia(properties.mass, inertia);
		body->setMassProps(properties.mass, inertia);
		body->setDamping(properties.dampening, properties.dampening);
		body->setFriction(properties.friction);
		body->setRestitution(properties.restitution);
		body->setSleepingThresholds(20.f, 20.f);

		if (properties.kinematic) {
			body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		}
		else {
			body->setCollisionFlags(body->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
		}

		if (properties.actor) {
			body->setAngularFactor({ 0, 0, 0 });

		}
		else {
			body->setAngularFactor({ 1, 1, 1 });
		}

		if ((properties.kinematic || properties.actor)) {
			body->setActivationState(DISABLE_DEACTIVATION);
		}
		else {
			body->setActivationState(ACTIVE_TAG);
		}

		if (properties.contents && is_disabled && !properties.disabled) {
			is_disabled = false;
			parent->addRigidBody(body);
		}
		else if ((properties.disabled || !properties.contents) && !is_disabled) {
			is_disabled = true;
			parent->removeRigidBody(body);

			body->setActivationState(DISABLE_SIMULATION);
		}
	}

	~phys_object_s() {
		if (body) delete body;
		if (motion_state) delete motion_state;
		if (shape) {
			if (is_compound) {
				btCompoundShape* cs = reinterpret_cast<btCompoundShape*>(shape);
				for (int i = 0; i < cs->getNumChildShapes(); i++) {
					delete cs->getChildShape(i);
				}
			}
			delete shape;
		}
	}
};

struct phys_world_s {
	btBroadphaseInterface* broadphase = nullptr;
	btDefaultCollisionConfiguration* config = nullptr;
	btCollisionDispatcher* dispatcher = nullptr;
	btSequentialImpulseConstraintSolver* solver = nullptr;
	btDiscreteDynamicsWorld* world = nullptr;

	phys_touch_callback touch_cb = nullptr;

	int step_resolution = 120;

	std::unordered_set<phys_object_t*> objects;
	std::unordered_map<btCollisionObject const*, phys_object_t*> object_map;

	phys_object_t* map_static = nullptr;
	phys_object_t* map_static_slick = nullptr;

	~phys_world_s() {

		if (map_static) delete map_static;
		if (map_static_slick) delete map_static_slick;

		for (phys_object_t* obj : objects) {
			delete obj;
		}

		objects.clear();

		if (world) delete world;
		if (solver) delete solver;
		if (dispatcher) delete dispatcher;
		if (config) delete config;
		if (broadphase) delete broadphase;
	}
};

static void bullet_world_substep_cb(btDynamicsWorld* world, btScalar timeStep) {

	phys_world_t* this_world = nullptr;
	{
		auto iter = world_map.find(world);
		if (iter == world_map.end()) return;
		this_world = iter->second;
	}

	if (!this_world->touch_cb) return;

	int numManifolds = world->getDispatcher()->getNumManifolds();
	for (int i = 0; i < numManifolds; i++) {
		btPersistentManifold* contactManifold = world->getDispatcher()->getManifoldByIndexInternal(i);
		int numContacts = contactManifold->getNumContacts();
		if (!numContacts) continue;

		btCollisionObject const* obA = static_cast<btCollisionObject const*>(contactManifold->getBody0());
		btCollisionObject const* obB = static_cast<btCollisionObject const*>(contactManifold->getBody1());

		phys_object_t* pA = nullptr;
		phys_object_t* pB = nullptr;

		{
			if (obA == this_world->map_static->body) {
				pA = this_world->map_static;
			}
			else if (obA == this_world->map_static_slick->body) {
				pA = this_world->map_static_slick;
			}
			else {
				auto iter = this_world->object_map.find(obA);
				if (iter == this_world->object_map.end()) continue;
				pA = iter->second;
			}
		}
		{
			if (obB == this_world->map_static->body) {
				pB = this_world->map_static;
			}
			else if (obB == this_world->map_static_slick->body) {
				pB = this_world->map_static_slick;
			}
			else {
				auto iter = this_world->object_map.find(obB);
				if (iter == this_world->object_map.end()) continue;
				pB = iter->second;
			}
		}

		if (pA->properties.disabled || pB->properties.disabled) continue;
		phys_collision_t col;
		col.A = pA;
		col.B = pB;
		for (int i = 0; i < numContacts; i++) {
			auto c = contactManifold->getContactPoint(i);
			col.impulse = c.getAppliedImpulse();
			bt2q3_vec3(c.getPositionWorldOnA(), col.posA);
			bt2q3_vec3(c.getPositionWorldOnB(), col.posB);
			bt2q3_vec3(c.m_normalWorldOnB, col.normal);
			this_world->touch_cb(this_world, &col);
		}
	}
}

phys_world_t* Phys_World_Create(phys_touch_callback touch_cb) {
	phys_world_t* nw = new phys_world_t;

	nw->broadphase = new btDbvtBroadphase;
	nw->config = new btDefaultCollisionConfiguration;
	nw->dispatcher = new btCollisionDispatcher{ nw->config };
	nw->solver = new btSequentialImpulseConstraintSolver;
	nw->world = new btDiscreteDynamicsWorld{ nw->dispatcher, nw->broadphase, nw->solver, nw->config };

	nw->world->setGravity({ 0, 0, -800 });
	nw->world->setInternalTickCallback(bullet_world_substep_cb);
	nw->world->setForceUpdateAllAabbs(false);

	nw->touch_cb = touch_cb;

	world_map[nw->world] = nw;

	return nw;
}

void Phys_World_Destroy(phys_world_t* w) {
	world_map.erase(w->world);
	delete w;
}

void Phys_World_Advance(phys_world_t* w, int time) {
	w->world->stepSimulation(time / 1000.0f, w->step_resolution, 1.0f / w->step_resolution);
}

void Phys_World_Set_Resolution(phys_world_t* world, unsigned int resolution) {
	world->step_resolution = resolution;
}

void Phys_World_Set_Gravity(phys_world_t* world, float gravity) {
	world->world->setGravity({ 0, 0, -gravity });
}

#define BP_POINTS_SIZE (2048*4)

struct gptp_brusurf_task_data {
	phys_world_t* world;
	int brushes_num, surfaces_num;
	int* brushes = nullptr;
	int* patches = nullptr;
	std::atomic_int brush_i, patch_i;
	std::mutex objmut;

	btCompoundShape* cs = nullptr;
	btCompoundShape* css = nullptr;

	~gptp_brusurf_task_data() {
		if (brushes) delete brushes;
		if (patches) delete patches;
	}
};

void* gptp_brush_task(void* arg) {

	gptp_brusurf_task_data* data = reinterpret_cast<gptp_brusurf_task_data*>(arg);
	vec3_t points[BP_POINTS_SIZE];
	int points_num;

	int content_mask = CONTENTS_SOLID;
	if (phys_playerclip->integer) content_mask |= CONTENTS_PLAYERCLIP;

	while (true) {
		int ib = data->brush_i--;
		if (ib < 0) return nullptr;
		int i = data->brushes[ib];
		if (CM_BrushContentFlags(i) & content_mask) {
			points_num = CM_CalculateHull(i, points, BP_POINTS_SIZE);
			if (points_num < 4) continue;


			btConvexHullShape* chs = new btConvexHullShape();
			for (int i = 0; i < points_num; i++) {
				btVector3 vec{ points[i][0], points[i][1], points[i][2] };
				chs->addPoint(vec, false);
			}
			chs->recalcLocalAabb();
			data->objmut.lock();
			if (CM_BrushOverallFlags(i) & SURF_SLICK) data->css->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
			else data->cs->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
			data->objmut.unlock();
		}
	}
}

void* gptp_surface_task(void* arg) {

	gptp_brusurf_task_data* data = reinterpret_cast<gptp_brusurf_task_data*>(arg);
	vec3_t points[BP_POINTS_SIZE];

	int content_mask = CONTENTS_SOLID;
	if (phys_playerclip->integer) content_mask |= CONTENTS_PLAYERCLIP;

	while (true) {
		int pi = data->patch_i--;
		if (pi < 0) return nullptr;
		int i = data->patches[pi];
		if (CM_PatchContentFlags(i) & content_mask) {
			int width, height;
			CM_PatchMeshPoints(i, points, BP_POINTS_SIZE, &width, &height);
			if (width * height < 4) continue;

			for (int x = 0; x < width - 1; x++) {
				for (int y = 0; y < height - 1; y++) {
					btConvexHullShape* chs = new btConvexHullShape();
					chs->addPoint(btVector3{ points[((y + 0) * width) + (x + 0)][0], points[((y + 0) * width) + (x + 0)][1], points[((y + 0) * width) + (x + 0)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 1) * width) + (x + 0)][0], points[((y + 1) * width) + (x + 0)][1], points[((y + 1) * width) + (x + 0)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 0) * width) + (x + 1)][0], points[((y + 0) * width) + (x + 1)][1], points[((y + 0) * width) + (x + 1)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 1) * width) + (x + 1)][0], points[((y + 1) * width) + (x + 1)][1], points[((y + 1) * width) + (x + 1)][2] }, false);
					chs->recalcLocalAabb();

					chs->recalcLocalAabb();
					data->objmut.lock();
					if (CM_PatchSurfaceFlags(i) & SURF_SLICK) data->css->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
					else data->cs->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
					data->objmut.unlock();
				}
			}
		}
	}
}

void Phys_World_Add_Current_Map(phys_world_t* world, void* world_token) {

	if (world->map_static) delete world->map_static;

	int brushes_max, surfaces_max;
	gptp_brusurf_task_data btd;

	btd.world = world;

	CM_NumData(&brushes_max, &surfaces_max);

	btd.brushes = new int[brushes_max];
	btd.patches = new int[surfaces_max];

	CM_SubmodelIndicies(0, btd.brushes, btd.patches, &btd.brushes_num, &btd.surfaces_num);

	btd.brush_i = btd.brushes_num - 1;
	btd.patch_i = btd.surfaces_num - 1;

	btd.cs = new btCompoundShape;
	btd.css = new btCompoundShape;

	std::vector<void*> tasks;
	for (unsigned int i = 0; i < GPTP_GetThreadCount(); i++) {
		tasks.push_back(GPTP_TaskBegin(gptp_brush_task, &btd));
	}
	for (void* task : tasks) {
		GPTP_TaskCollect(task);
	}
	tasks.clear();
	for (unsigned int i = 0; i < GPTP_GetThreadCount(); i++) {
		tasks.push_back(GPTP_TaskBegin(gptp_surface_task, &btd));
	}
	for (void* task : tasks) {
		GPTP_TaskCollect(task);
	}

	phys_properties_t world_props = {
		0,
		0,
		0,
		0,
		qfalse,
		qfalse,
		qfalse,
		CONTENTS_SOLID,
		world_token,
	};

	world->map_static = new phys_object_t{ world->world };
	world->map_static->properties = world_props;
	world->map_static->is_compound = true;
	btd.cs->recalculateLocalAabb();
	world->map_static->shape = btd.cs;
	world->map_static->motion_state = new btDefaultMotionState{ btTransform { btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} } };
	world->map_static->shape->calculateLocalInertia(0, world->map_static->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ 0, world->map_static->motion_state, world->map_static->shape, world->map_static->inertia };
	world->map_static->body = new btRigidBody{ CI };
	world->world->addRigidBody(world->map_static->body);
	world->object_map[world->map_static->body] = world->map_static;

	world->map_static_slick = new phys_object_t{ world->world };
	world->map_static->properties = world_props;
	world->map_static_slick->is_compound = true;
	btd.css->recalculateLocalAabb();
	world->map_static_slick->shape = btd.css;
	world->map_static_slick->motion_state = new btDefaultMotionState{ btTransform { btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} } };
	world->map_static_slick->shape->calculateLocalInertia(0, world->map_static_slick->inertia);
	btRigidBody::btRigidBodyConstructionInfo CIS{ 0, world->map_static_slick->motion_state, world->map_static_slick->shape, world->map_static_slick->inertia };
	world->map_static_slick->body = new btRigidBody{ CIS };
	world->map_static_slick->body->setFriction(0);
	world->world->addRigidBody(world->map_static_slick->body);
	world->object_map[world->map_static_slick->body] = world->map_static_slick;
}

void Phys_World_Remove_Object(phys_world_t* w, phys_object_t* obj) {
	w->world->removeRigidBody(obj->body);
	auto i = w->object_map.find(obj->body);
	if (i != w->object_map.end()) w->object_map.erase(i);
	w->objects.erase(obj);
	delete obj;
}

void Phys_World_Trace(phys_world_t* w, vec3_t start, vec3_t end, phys_trace_t* tr) {
	btVector3 bstart = q32bt_vec3(start), bend = q32bt_vec3(end);
	btCollisionWorld::ClosestRayResultCallback hit{ bstart, bend };
	w->world->rayTest(bstart, bend, hit);

	memset(tr, 0, sizeof(phys_trace_t));

	if (hit.hasHit()) {
		tr->hit_fraction = hit.m_closestHitFraction;
		bt2q3_vec3(hit.m_hitNormalWorld, tr->hit_normal);
		bt2q3_vec3(hit.m_hitPointWorld, tr->hit_position);
		tr->hit_object = w->object_map[hit.m_collisionObject];
	}
}

static void bullet_world_add_object(phys_world_t* w, phys_object_t* obj) {
	w->world->addRigidBody(obj->body);
	w->objects.insert(obj);
	w->object_map[obj->body] = obj;
}

phys_object_t* Phys_Object_Create_Box(phys_world_t* w, vec3_t mins, vec3_t maxs, phys_transform_t* initial_transform, phys_properties_t* properties) {
	phys_object_t* obj = new phys_object_t{ w->world };

	obj->properties = *properties;
	VectorCopy(mins, obj->properties.mins);
	VectorCopy(maxs, obj->properties.maxs);

	btConvexHullShape* chs = new btConvexHullShape;
	chs->addPoint({ mins[0], mins[1], mins[2] }, false);
	chs->addPoint({ mins[0], mins[1], maxs[2] }, false);
	chs->addPoint({ mins[0], maxs[1], mins[2] }, false);
	chs->addPoint({ mins[0], maxs[1], maxs[2] }, false);
	chs->addPoint({ maxs[0], mins[1], mins[2] }, false);
	chs->addPoint({ maxs[0], mins[1], maxs[2] }, false);
	chs->addPoint({ maxs[0], maxs[1], mins[2] }, false);
	chs->addPoint({ maxs[0], maxs[1], maxs[2] }, false);
	chs->recalcLocalAabb();

	obj->shape = chs;

	if (obj->properties.mass < 0) {
		obj->properties.mass = fabs(mins[0] - maxs[0]) * fabs(mins[1] - maxs[1]) * fabs(mins[2] - maxs[2]);
	}

	obj->motion_state = new btDefaultMotionState{ btTransform {
		btQuaternion { initial_transform->angles[0] * d2r_mult, initial_transform->angles[2] * d2r_mult, initial_transform->angles[1] * d2r_mult },
		btVector3 {initial_transform->origin[0], initial_transform->origin[1], initial_transform->origin[2]} } };
	obj->shape->calculateLocalInertia(obj->properties.mass, obj->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ obj->properties.mass, obj->motion_state, obj->shape, obj->inertia };
	obj->body = new btRigidBody{ CI };

	bullet_world_add_object(w, obj);
	obj->set_properties();

	return obj;
}

phys_object_t* Phys_Object_Create_Capsule(phys_world_t* w, float cylinder_height, float radius, float v_center_offs, phys_transform_t* initial_transform, phys_properties_t* properties) {
	phys_object_t* obj = new phys_object_t{ w->world };

	obj->properties = *properties;

	VectorSet(obj->properties.mins, -radius, -radius, -(cylinder_height / 2 + radius));
	VectorSet(obj->properties.mins, radius, radius, (cylinder_height / 2 + radius));

	if (v_center_offs) {
		btCompoundShape* cmp = new btCompoundShape;
		btCapsuleShapeZ* cap = new btCapsuleShapeZ{ radius, cylinder_height };
		cmp->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, v_center_offs} }, cap);
		obj->shape = cmp;
		obj->is_compound = true;
	}
	else {
		btCapsuleShapeZ* cap = new btCapsuleShapeZ{ radius, cylinder_height };
		obj->shape = cap;
	}

	if (obj->properties.mass < 0) {
		obj->properties.mass = -obj->properties.mass * ((M_PI * pow(radius, 2) * cylinder_height) + ((4.0f / 3.0f) * M_PI * pow(radius, 3)));
	}

	obj->motion_state = new btDefaultMotionState{ btTransform {
		btQuaternion { initial_transform->angles[0] * d2r_mult, initial_transform->angles[2] * d2r_mult, initial_transform->angles[1] * d2r_mult },
		btVector3 {initial_transform->origin[0], initial_transform->origin[1], initial_transform->origin[2]} } };

	obj->shape->calculateLocalInertia(obj->properties.mass, obj->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ obj->properties.mass, obj->motion_state, obj->shape, obj->inertia };
	obj->body = new btRigidBody{ CI };

	bullet_world_add_object(w, obj);
	obj->set_properties();

	return obj;
}

phys_object_t* Phys_Object_Create_Cylinder(phys_world_t* w, float cylinder_height, float radius, float v_center_offs, phys_transform_t* initial_transform, phys_properties_t* properties) {
	phys_object_t* obj = new phys_object_t{ w->world };

	obj->properties = *properties;

	VectorSet(obj->properties.mins, -radius, -radius, -(cylinder_height / 2 + radius));
	VectorSet(obj->properties.mins, radius, radius, (cylinder_height / 2 + radius));

	if (v_center_offs) {
		btCompoundShape* cmp = new btCompoundShape;
		btCylinderShapeZ* cyl = new btCylinderShapeZ{ btVector3 {radius, radius, cylinder_height / 2} };
		cmp->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, v_center_offs} }, cyl);
		obj->shape = cmp;
		obj->is_compound = true;
	}
	else {
		btCylinderShapeZ* cyl = new btCylinderShapeZ{ btVector3 {radius, radius, cylinder_height / 2} };
		obj->shape = cyl;
	}

	if (obj->properties.mass < 0) {
		obj->properties.mass = -obj->properties.mass * ((M_PI * pow(radius, 2) * cylinder_height) + ((4.0f / 3.0f) * M_PI * pow(radius, 3)));
	}

	obj->motion_state = new btDefaultMotionState{ btTransform {
		btQuaternion { initial_transform->angles[0] * d2r_mult, initial_transform->angles[2] * d2r_mult, initial_transform->angles[1] * d2r_mult },
		btVector3 {initial_transform->origin[0], initial_transform->origin[1], initial_transform->origin[2]} } };

	obj->shape->calculateLocalInertia(obj->properties.mass, obj->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ obj->properties.mass, obj->motion_state, obj->shape, obj->inertia };
	obj->body = new btRigidBody{ CI };

	bullet_world_add_object(w, obj);
	obj->set_properties();

	return obj;
}

phys_object_t* Phys_Object_Create_From_Obj(phys_world_t* world, char const* path, phys_transform_t* initial_transform, phys_properties_t* properties, float scale) {
	phys_object_t* no = new phys_object_t{ world->world };

	no->properties = *properties;

	bool calc_mass = false;
	float mass = properties->mass;
	if (mass < 0) {
		calc_mass = true;
		mass = 0;
	}

	objModel_t* mod = CM_LoadObj(path);
	if (!mod) return nullptr;

	VectorCopy(mod->mins, no->properties.mins);
	VectorCopy(mod->maxs, no->properties.maxs);

	if (mod->numSurfaces > 1) {

		btCompoundShape* cps = new btCompoundShape;
		no->is_compound = true;

		if (calc_mass) {

			for (int s = 0; s < mod->numSurfaces; s++) {

				btConvexHullShape* chs = new btConvexHullShape;
				btVector3 min, max;

				for (int i = 0; i < mod->surfaces[s].numFaces; i++) {
					btVector3 p1{ mod->surfaces[s].faces[i][0].vertex[0], mod->surfaces[s].faces[i][0].vertex[1], mod->surfaces[s].faces[i][0].vertex[2] };
					btVector3 p2{ mod->surfaces[s].faces[i][1].vertex[0], mod->surfaces[s].faces[i][1].vertex[1], mod->surfaces[s].faces[i][1].vertex[2] };
					btVector3 p3{ mod->surfaces[s].faces[i][2].vertex[0], mod->surfaces[s].faces[i][2].vertex[1], mod->surfaces[s].faces[i][2].vertex[2] };
					chs->addPoint(p1, false);
					chs->addPoint(p2, false);
					chs->addPoint(p2, false);
					if (i == 0) {
						min = p1;
						max = p1;
					}
					if (p1.x() < min.x()) min.setX(p1.x());
					if (p1.x() > max.x()) max.setX(p1.x());
					if (p1.y() < min.y()) min.setY(p1.y());
					if (p1.y() > max.y()) max.setY(p1.y());
					if (p1.z() < min.z()) min.setZ(p1.z());
					if (p1.z() > max.z()) max.setZ(p1.z());
					if (p2.x() < min.x()) min.setX(p2.x());
					if (p2.x() > max.x()) max.setX(p2.x());
					if (p2.y() < min.y()) min.setY(p2.y());
					if (p2.y() > max.y()) max.setY(p2.y());
					if (p2.z() < min.z()) min.setZ(p2.z());
					if (p2.z() > max.z()) max.setZ(p2.z());
					if (p3.x() < min.x()) min.setX(p3.x());
					if (p3.x() > max.x()) max.setX(p3.x());
					if (p3.y() < min.y()) min.setY(p3.y());
					if (p3.y() > max.y()) max.setY(p3.y());
					if (p3.z() < min.z()) min.setZ(p3.z());
					if (p3.z() > max.z()) max.setZ(p3.z());
				}

				chs->recalcLocalAabb();
				mass += fabs(min.x() - max.x()) * fabs(min.y() - max.y()) * fabs(min.z() - max.z());
				cps->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);

			}

		}
		else {

			for (int s = 0; s < mod->numSurfaces; s++) {

				btConvexHullShape* chs = new btConvexHullShape;
				for (int i = 0; i < mod->surfaces[s].numFaces; i++) {
					chs->addPoint({ mod->surfaces[s].faces[i][0].vertex[0], mod->surfaces[s].faces[i][0].vertex[1], mod->surfaces[s].faces[i][0].vertex[2] }, false);
					chs->addPoint({ mod->surfaces[s].faces[i][1].vertex[0], mod->surfaces[s].faces[i][1].vertex[1], mod->surfaces[s].faces[i][1].vertex[2] }, false);
					chs->addPoint({ mod->surfaces[s].faces[i][2].vertex[0], mod->surfaces[s].faces[i][2].vertex[1], mod->surfaces[s].faces[i][2].vertex[2] }, false);
				}

				chs->recalcLocalAabb();

				cps->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);

			}
		}
		cps->setLocalScaling({ scale, scale, scale });
		cps->recalculateLocalAabb();
		no->shape = cps;

	}
	else {

		if (calc_mass) {

			btConvexHullShape* chs = new btConvexHullShape;
			btVector3 min, max;

			for (int i = 0; i < mod->numVerts; i++) {
				btVector3 np{ mod->verts[i * 3], mod->verts[i * 3 + 1], mod->verts[i * 3 + 2] };
				if (i == 0) {
					min = np;
					max = np;
				}
				chs->addPoint(np, false);
				if (np.x() < min.x()) min.setX(np.x());
				if (np.x() > max.x()) max.setX(np.x());
				if (np.y() < min.y()) min.setY(np.y());
				if (np.y() > max.y()) max.setY(np.y());
				if (np.z() < min.z()) min.setZ(np.z());
				if (np.z() > max.z()) max.setZ(np.z());
			}
			chs->setLocalScaling({ scale, scale, scale });
			chs->recalcLocalAabb();
			no->shape = chs;
			mass += fabs(min.x() - max.x()) * fabs(min.y() - max.y()) * fabs(min.z() - max.z());

		}
		else {

			btConvexHullShape* chs = new btConvexHullShape;

			for (int i = 0; i < mod->numVerts; i++) {
				chs->addPoint({ mod->verts[i * 3], mod->verts[i * 3 + 1], mod->verts[i * 3 + 2] }, false);
			}
			chs->setLocalScaling({ scale, scale, scale });
			chs->recalcLocalAabb();
			no->shape = chs;

		}
	}

	if (calc_mass) {
		no->properties.mass = mass;
	}

	no->motion_state = new btDefaultMotionState{ btTransform {
		btQuaternion { initial_transform->angles[0] * d2r_mult, initial_transform->angles[2] * d2r_mult, initial_transform->angles[1] * d2r_mult },
		btVector3 {initial_transform->origin[0], initial_transform->origin[1], initial_transform->origin[2]} } };
	no->shape->calculateLocalInertia(no->properties.mass, no->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ no->properties.mass, no->motion_state, no->shape, no->inertia };
	no->body = new btRigidBody{ CI };

	bullet_world_add_object(world, no);
	no->set_properties();

	return no;
}

phys_object_t* Phys_Object_Create_From_BModel(phys_world_t* world, int modeli, phys_transform_t* initial_transform, phys_properties_t* properties) {
	vec3_t points[BP_POINTS_SIZE];
	int brushes_max, surfaces_max, brushes_num, surfaces_num;
	CM_NumData(&brushes_max, &surfaces_max);
	int* brushes = new int[brushes_max];
	int* patches = new int[surfaces_max];
	CM_SubmodelIndicies(modeli, brushes, patches, &brushes_num, &surfaces_num);

	phys_object_t* no = new phys_object_t{ world->world };
	no->properties = *properties;
	no->is_compound = true;
	btCompoundShape* cs = new btCompoundShape;

	for (int i = 0; i < brushes_num; i++) {
		if (!(CM_BrushContentFlags(brushes[i]) & CONTENTS_SOLID)) continue;
		int n = CM_CalculateHull(brushes[i], points, BP_POINTS_SIZE);

		btConvexHullShape* chs = new btConvexHullShape();
		for (int i = 0; i < n; i++) {
			btVector3 vec{ points[i][0], points[i][1], points[i][2] };
			chs->addPoint(vec, false);
		}

		chs->recalcLocalAabb();
		cs->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
	}

	for (int i = 0; i < surfaces_num; i++) {
		if (CM_PatchContentFlags(patches[i]) & CONTENTS_SOLID) {
			int width, height;
			CM_PatchMeshPoints(patches[i], points, BP_POINTS_SIZE, &width, &height);
			if (width * height < 4) continue;

			for (int x = 0; x < width - 1; x++) {
				for (int y = 0; y < height - 1; y++) {
					btConvexHullShape* chs = new btConvexHullShape();
					chs->addPoint(btVector3{ points[((y + 0) * width) + (x + 0)][0], points[((y + 0) * width) + (x + 0)][1], points[((y + 0) * width) + (x + 0)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 1) * width) + (x + 0)][0], points[((y + 1) * width) + (x + 0)][1], points[((y + 1) * width) + (x + 0)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 0) * width) + (x + 1)][0], points[((y + 0) * width) + (x + 1)][1], points[((y + 0) * width) + (x + 1)][2] }, false);
					chs->addPoint(btVector3{ points[((y + 1) * width) + (x + 1)][0], points[((y + 1) * width) + (x + 1)][1], points[((y + 1) * width) + (x + 1)][2] }, false);

					chs->recalcLocalAabb();

					cs->addChildShape(btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} }, chs);
				}
			}
		}
	}

	cs->recalculateLocalAabb();
	no->shape = cs;
	no->motion_state = new btDefaultMotionState{ btTransform {
		btQuaternion { initial_transform->angles[0] * d2r_mult, initial_transform->angles[2] * d2r_mult, initial_transform->angles[1] * d2r_mult },
		btVector3 {initial_transform->origin[0], initial_transform->origin[1], initial_transform->origin[2]} } };
	no->shape->calculateLocalInertia(no->properties.mass, no->inertia);
	btRigidBody::btRigidBodyConstructionInfo CI{ no->properties.mass, no->motion_state, no->shape, no->inertia };
	no->body = new btRigidBody{ CI };

	bullet_world_add_object(world, no);
	no->set_properties();

	return no;
}

void Phys_Object_Get_Origin(phys_object_t* obj, vec3_t origin) {
	if (obj->properties.kinematic) {
		btTransform trans;
		obj->motion_state->getWorldTransform(trans);
		btVector3 borig = trans.getOrigin();
		VectorSet(origin, borig.x(), borig.y(), borig.z());
	}
	else {
		btTransform& trans = obj->body->getWorldTransform();
		btVector3 borig = trans.getOrigin();
		VectorSet(origin, borig.x(), borig.y(), borig.z());
	}
}

void Phys_Object_Set_Origin(phys_object_t* obj, vec3_t origin) {
	if (obj->properties.kinematic) {
		btTransform trans;
		obj->motion_state->getWorldTransform(trans);
		trans.setOrigin({ origin[0], origin[1], origin[2] });
		obj->motion_state->setWorldTransform(trans);
	}
	else {
		obj->body->getWorldTransform().setOrigin({ origin[0], origin[1], origin[2] });
		obj->body->activate(true);
	}
}

void Phys_Object_Get_Rotation(phys_object_t* obj, vec3_t angles) {
	if (obj->properties.kinematic) {
		btTransform trans;
		obj->motion_state->getWorldTransform(trans);
		btMatrix3x3{ trans.getRotation() }.getEulerYPR(angles[1], angles[0], angles[2]);
	}
	else {
		btTransform& trans = obj->body->getWorldTransform();
		btMatrix3x3{ trans.getRotation() }.getEulerYPR(angles[1], angles[0], angles[2]);
	}
	VectorScale(angles, r2d_mult, angles);
}

void Phys_Object_Set_Rotation(phys_object_t* obj, vec3_t angles) {
	if (obj->properties.kinematic) {
		btTransform trans;
		obj->motion_state->getWorldTransform(trans);
		trans.setRotation(btQuaternion{ angles[0] * d2r_mult, angles[2] * d2r_mult, angles[1] * d2r_mult });
		obj->motion_state->setWorldTransform(trans);
	}
	else {
		obj->body->getWorldTransform().setRotation(btQuaternion{ angles[0] * d2r_mult, angles[2] * d2r_mult, angles[1] * d2r_mult });
	}
}

phys_properties_t* Phys_Object_Get_Properties(phys_object_t* obj) {
	return &obj->properties;
}

void Phys_Object_Set_Properties(phys_object_t* obj) {
	obj->set_properties();
}

void Phys_Object_Get_Transform(phys_object_t* obj, phys_transform_t* copyto) {
	btTransform trans;
	if (obj->properties.kinematic) obj->motion_state->getWorldTransform(trans);
	else trans = obj->body->getWorldTransform();

	btVector3& origin = trans.getOrigin();
	VectorCopy(origin, copyto->origin);
	btMatrix3x3{ trans.getRotation() }.getEulerYPR(copyto->angles[1], copyto->angles[0], copyto->angles[2]);
}

void Phys_Object_Set_Transform(phys_object_t* obj, phys_transform_t* copyfrom) {
	btTransform trans;
	if (obj->properties.kinematic) obj->motion_state->getWorldTransform(trans);
	else trans = obj->body->getWorldTransform();

	trans.setOrigin({ copyfrom->origin[0], copyfrom->origin[1], copyfrom->origin[2] });
	trans.setRotation(btQuaternion{ copyfrom->angles[0] * d2r_mult, copyfrom->angles[2] * d2r_mult, copyfrom->angles[1] * d2r_mult });

	if (obj->properties.kinematic) obj->motion_state->setWorldTransform(trans);
	else obj->body->setWorldTransform(trans);
}

void Phys_Object_Force(phys_object_t* obj, float* lin, float* ang) {
	if (lin) obj->body->applyCentralForce(q32bt_vec3(lin));
	if (ang) obj->body->applyTorque(q32bt_vec3(ang));
	obj->body->activate(true);
}

void Phys_Object_Impulse(phys_object_t* obj, float* lin, float* ang) {
	if (lin) obj->body->applyCentralImpulse(q32bt_vec3(lin));
	if (ang) obj->body->applyTorqueImpulse(q32bt_vec3(ang));
	obj->body->activate(true);
}

void Phys_Obj_Set_Linear_Velocity(phys_object_t* obj, vec3_t lin) {
	obj->body->setLinearVelocity(q32bt_vec3(lin));
	obj->body->activate(true);
}

void Phys_Obj_Get_Linear_Velocity(phys_object_t* obj, vec3_t lin) {
	bt2q3_vec3(obj->body->getLinearVelocity(), lin);
}

void Phys_Obj_Set_Angular_Velocity(phys_object_t* obj, vec3_t ang) {
	obj->body->setAngularVelocity(q32bt_vec3(ang));
	obj->body->activate(true);
}

void Phys_Obj_Get_Angular_Velocity(phys_object_t* obj, vec3_t ang) {
	bt2q3_vec3(obj->body->getAngularVelocity(), ang);
}

void Phys_Obj_Weld(phys_object_t* A, phys_object_t* B) {
	btTransform tA = btTransform{ btQuaternion {0, 0, 0, 1}, btVector3 {0, 0, 0} };
	btTransform tB = btTransform{ btQuaternion {0, 0, 0, 1}, A->body->getWorldTransform().getOrigin() - B->body->getWorldTransform().getOrigin() };
	btFixedConstraint* cons = new btFixedConstraint(*A->body, *B->body, tA, tB);
	A->parent->addConstraint(cons, true);
}
