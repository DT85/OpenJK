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

#include "cm_local.h"
#include "cm_patch.h"
#include <cmath>

void MatrixInverse(matrix3_t inMatrix, matrix3_t outMatrix) {
	float det = inMatrix[0][0] * (inMatrix[2][2] * inMatrix[1][1] - inMatrix[2][1] * inMatrix[1][2]) -
		inMatrix[0][1] * (inMatrix[2][2] * inMatrix[1][0] - inMatrix[2][0] * inMatrix[1][2]) +
		inMatrix[0][2] * (inMatrix[2][1] * inMatrix[1][0] - inMatrix[2][0] * inMatrix[1][1]);

	outMatrix[0][0] = (inMatrix[2][2] * inMatrix[1][1] - inMatrix[2][1] * inMatrix[1][2]) / det;
	outMatrix[0][1] = (inMatrix[2][0] * inMatrix[1][2] - inMatrix[2][2] * inMatrix[1][0]) / det;
	outMatrix[0][2] = (inMatrix[2][1] * inMatrix[1][0] - inMatrix[2][0] * inMatrix[1][1]) / det;

	outMatrix[1][0] = (inMatrix[0][2] * inMatrix[2][1] - inMatrix[0][1] * inMatrix[2][2]) / det;
	outMatrix[1][1] = (inMatrix[0][0] * inMatrix[2][2] - inMatrix[0][2] * inMatrix[2][0]) / det;
	outMatrix[1][2] = (inMatrix[0][1] * inMatrix[2][0] - inMatrix[0][0] * inMatrix[2][1]) / det;

	outMatrix[2][0] = (inMatrix[1][2] * inMatrix[0][1] - inMatrix[1][1] * inMatrix[0][2]) / det;
	outMatrix[2][1] = (inMatrix[1][0] * inMatrix[0][2] - inMatrix[1][2] * inMatrix[0][0]) / det;
	outMatrix[2][2] = (inMatrix[1][1] * inMatrix[0][0] - inMatrix[1][0] * inMatrix[0][1]) / det;
}

void MatrixVectorMultiply(matrix3_t matrix, vec3_t vector, vec3_t out) {
	out[0] = matrix[0][0] * vector[0] + matrix[0][1] * vector[1] + matrix[0][2] * vector[2];
	out[1] = matrix[1][0] * vector[0] + matrix[1][1] * vector[1] + matrix[1][2] * vector[2];
	out[2] = matrix[2][0] * vector[0] + matrix[2][1] * vector[1] + matrix[2][2] * vector[2];
}

static void CM_GetIntersectingPoint(cplane_t* a, cplane_t* b, cplane_t* c, vec3_t out) {
	matrix3_t sysmat, sysmatInverse;
	vec3_t dists;

	sysmat[0][0] = a->normal[0];
	sysmat[0][1] = b->normal[0];
	sysmat[0][2] = c->normal[0];
	sysmat[1][0] = a->normal[1];
	sysmat[1][1] = b->normal[1];
	sysmat[1][2] = c->normal[1];
	sysmat[2][0] = a->normal[2];
	sysmat[2][1] = b->normal[2];
	sysmat[2][2] = c->normal[2];


	MatrixInverse(sysmat, sysmatInverse);
	VectorSet(dists, a->dist, b->dist, c->dist);
	MatrixVectorMultiply(sysmatInverse, dists, out);
}

void CM_NumData(int* brushes, int* surfaces) {
	*brushes = cmg.numBrushes;
	*surfaces = cmg.numSurfaces;
}

void CM_FWBRecurse(cNode_t* node, int* brushes, int* patches, int& brushes_num, int& patches_num) {
	if (node->children[0] < 0) {
		cLeaf_t* leaf = &cmg.leafs[-1 - node->children[0]];

		for (int j = 0; j < leaf->numLeafBrushes; j++) {
			bool good = true;
			
			for (int k = 0; k < brushes_num && good; k++) {
				if (brushes[k] == cmg.leafbrushes[leaf->firstLeafBrush + j]) 
					good = false;
			}

			if (good) {
				brushes[brushes_num] = cmg.leafbrushes[leaf->firstLeafBrush + j];
				brushes_num++;
			}
		}

		for (int j = 0; j < leaf->numLeafSurfaces; j++) {
			bool good = true;

			for (int k = 0; k < patches_num && good; k++) {
				if (patches[k] == cmg.leafsurfaces[leaf->firstLeafSurface + j]) 
					good = false;
			}

			if (good) {
				patches[patches_num] = cmg.leafsurfaces[leaf->firstLeafSurface + j];
				patches_num++;
			}
		}
	}
	else {
		CM_FWBRecurse(&cmg.nodes[node->children[0]], brushes, patches, brushes_num, patches_num);
	}

	if (node->children[1] < 0) {
		cLeaf_t* leaf = &cmg.leafs[-1 - node->children[1]];

		for (int j = 0; j < leaf->numLeafBrushes; j++) {
			bool good = true;

			for (int k = 0; k < brushes_num && good; k++) {
				if (brushes[k] == cmg.leafbrushes[leaf->firstLeafBrush + j]) 
					good = false;
			}
			
			if (good) {
				brushes[brushes_num] = cmg.leafbrushes[leaf->firstLeafBrush + j];
				brushes_num++;
			}
		}

		for (int j = 0; j < leaf->numLeafSurfaces; j++) {
			bool good = true;

			for (int k = 0; k < patches_num && good; k++) {
				if (patches[k] == cmg.leafsurfaces[leaf->firstLeafSurface + j]) 
					good = false;
			}

			if (good) {
				patches[patches_num] = cmg.leafsurfaces[leaf->firstLeafSurface + j];
				patches_num++;
			}
		}
	}
	else {
		CM_FWBRecurse(&cmg.nodes[node->children[1]], brushes, patches, brushes_num, patches_num);
	}
}

void CM_SubmodelIndicies(int submodel, int* brushes, int* patches, int* brushes_num_ptr, int* patches_num_ptr) {
	cmodel_t* wmod = &cmg.cmodels[submodel];
	int brushes_num = 0;
	int patches_num = 0;

	if (wmod->firstNode < 0) {
		for (int j = 0; j < wmod->leaf.numLeafBrushes; j++, brushes_num++) {
			brushes[brushes_num] = cmg.leafbrushes[wmod->leaf.firstLeafBrush + j];
		}

		for (int j = 0; j < wmod->leaf.numLeafSurfaces; j++, patches_num++) {
			patches[patches_num] = cmg.leafsurfaces[wmod->leaf.firstLeafSurface + j];
		}
	}
	else {
		cNode_t* node = &cmg.nodes[wmod->firstNode];
		CM_FWBRecurse(node, brushes, patches, brushes_num, patches_num);
	}
	*brushes_num_ptr = brushes_num;
	*patches_num_ptr = patches_num;
}

int CM_BrushContentFlags(int brushnum) {
	if (brushnum > cmg.numBrushes) 
		return 0;

	return cmg.brushes[brushnum].contents;
}

int CM_PatchContentFlags(int patchnum) {
	if (patchnum > cmg.numSurfaces) 
		return 0;

	cPatch_t* patch = cmg.surfaces[patchnum];

	if (!patch) 
		return 0;

	return patch->contents;
}

int CM_BrushOverallFlags(int brushnum) {
	if (brushnum > cmg.numBrushes)
		return 0;

	int flags = 0;

	for (short i = 0; i < cmg.brushes[brushnum].numsides; i++) {
		flags |= cmg.shaders[cmg.brushes[brushnum].sides[i].shaderNum].surfaceFlags;
	}

	return flags;
}

int CM_PatchSurfaceFlags(int patchnum) {
	if (patchnum > cmg.numSurfaces) 
		return 0;

	cPatch_t* patch = cmg.surfaces[patchnum];

	if (!patch) 
		return 0;

	return patch->surfaceFlags;
}

int CM_CalculateHull(int brushnum, vec3_t* points, int points_size) {
	if (brushnum >= cmg.numBrushes) 
		return -1;

	cbrush_t* brush = cmg.brushes + brushnum;
	int i, j, k, size, pi = 0;
	size = 0;

	for (i = 0; i < brush->numsides; i++) {
		for (j = 0; j < i; j++) {
			for (k = 0; k < j; k++) {
				if (i == j || j == k || i == k) 
					continue;

				if (size == points_size) 
					break;

				bool legal = true;
				vec3_t vec, vecn;
				CM_GetIntersectingPoint((brush->sides + i)->plane, (brush->sides + j)->plane, (brush->sides + k)->plane, vec);
				
				if (std::isnan(vec[0]) || std::isnan(vec[1]) || std::isnan(vec[2]) || std::isinf(vec[0]) || std::isinf(vec[1]) || std::isinf(vec[2])) 
					legal = false;
				
				if (legal) 
					for (int l = 0; l < brush->numsides; l++) {
						if (l == i || l == j || l == k) 
							continue;
						VectorSet(vecn, brush->sides[l].plane->normal[0], brush->sides[l].plane->normal[1], brush->sides[l].plane->normal[2]);

					if (DotProduct(vecn, vec) > brush->sides[l].plane->dist + 0.01f) {
						legal = false;
						break;
					}
				}

				if (legal) {
					VectorCopy(vec, points[pi++]);
					size++;
				}
			}
		}
	}
	return pi;
}

void CM_PatchMeshPoints(int patchnum, vec3_t* points, int points_size, int* width, int* height) {
	cPatch_t* patch = cmg.surfaces[patchnum];
	
	if (!patch) {
		*width = 0;
		*height = 0;

		return;
	}

	CM_QuickSubdividePatch(patch, points, points_size, width, height);
}
