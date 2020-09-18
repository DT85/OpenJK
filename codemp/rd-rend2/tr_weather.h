/*
===========================================================================
Copyright (C) 2016, OpenJK contributors

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
#pragma once

#ifdef VANILLA_WEATHER
void R_InitWorldEffects(void);
void R_ShutdownWorldEffects(void);
void RB_RenderWorldEffects(void);

void RE_WorldEffectCommand(const char *command);
void R_WorldEffect_f(void);

bool R_GetWindVector(vec3_t windVector);
bool R_GetWindSpeed(float &windSpeed);

bool R_IsRaining();
//bool R_IsSnowing();
bool R_IsPuffing();
void RE_AddWeatherZone(vec3_t mins, vec3_t maxs);
#else
#include "qcommon/qcommon.h"


struct weatherSystem_t;
struct srfWeather_t;

void R_InitWeatherSystem();
void R_InitWeatherForMap();
void R_AddWeatherSurfaces();
void R_ShutdownWeatherSystem();
void RB_SurfaceWeather( srfWeather_t *surfaceType );
#endif
