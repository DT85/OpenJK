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

#include "cg_local.h"

typedef struct clightstyle_s {
	int				length;
	color4ub_t		value;
	color4ub_t		map[MAX_QPATH];
} clightstyle_t;

static	clightstyle_t	cl_lightstyle[MAX_LIGHT_STYLES];
static	int				lastofs;

/*
================
CG_ClearLightStyles
================
*/
void CG_ClearLightStyles (void)
{
	int	i;

	memset( cl_lightstyle, 0, sizeof( cl_lightstyle ) );
	lastofs = -1;

	for ( i=0; i<MAX_LIGHT_STYLES*3; i++ )
		CG_SetLightstyle( i );
}

/*
================
CG_RunLightStyles
================
*/
void CG_RunLightStyles (void)
{
	int ofs, i;
	clightstyle_t *ls;

	ofs = cg.time / 50;
//	if ( ofs == lastofs )
//		return;
	lastofs = ofs;

	for ( i=0, ls=cl_lightstyle; i<MAX_LIGHT_STYLES; i++, ls++ ) {
		byteAlias_t *ba = (byteAlias_t *)&ls->value;

		ls->value[3] = 255;
		if ( !ls->length ) {
			ls->value[0] = ls->value[1] = ls->value[2] = 255;
		}
		else if ( ls->length == 1 ) {
			ls->value[0] = ls->map[0][0];
			ls->value[1] = ls->map[0][1];
			ls->value[2] = ls->map[0][2];
		//	ls->value[3] = ls->map[0][3];
		}
		else {
			ls->value[0] = ls->map[ofs%ls->length][0];
			ls->value[1] = ls->map[ofs%ls->length][1];
			ls->value[2] = ls->map[ofs%ls->length][2];
		//	ls->value[3] = ls->map[ofs%ls->length][3];
		}

		trap->R_SetLightStyle( i, ba->i );
	}
}

void CG_SetLightstyle (int i)
{
	const char	*s;
	int			j, k;

	s = CG_ConfigString( i+CS_LIGHT_STYLES );
	j = strlen (s);
	if (j >= MAX_QPATH)
	{
		Com_Error (ERR_DROP, "svc_lightstyle length=%i", j);
	}

	cl_lightstyle[(i/3)].length = j;
	for (k=0 ; k<j ; k++)
	{
		cl_lightstyle[(i/3)].map[k][(i%3)] = (float)(s[k]-'a')/(float)('z'-'a') * 255.0;
	}
}

/*
==============
CG_AddDLightstyle
==============
*/
#define LS_FRAMETIME 100 // (ms)  cycle through lightstyle characters at 10fps
void CG_AddDLightstyle(centity_t *cent) {
	float lightval;
	int cl;
	int r, g, b;
	int stringlength;
	float offset;
	int otime;
	int lastch, nextch;

	if (cent->dl_stylestring[0] == '\0') {
		return;
	}

	otime = cg.time - cent->dl_time;
	stringlength = strlen(cent->dl_stylestring);

	// it's been a long time since you were updated, lets assume a reset
	if (otime > 2 * LS_FRAMETIME) {
		otime = 0;
		cent->dl_frame = cent->dl_oldframe = 0;
		cent->dl_backlerp = 0;
	}

	cent->dl_time = cg.time;

	offset = ((float)otime) / LS_FRAMETIME;

	cent->dl_backlerp += offset;


	if (cent->dl_backlerp > 1) {                     // we're moving on to the next frame
		cent->dl_oldframe = cent->dl_oldframe + (int)cent->dl_backlerp;
		cent->dl_frame = cent->dl_oldframe + 1;
		if (cent->dl_oldframe >= stringlength) {
			cent->dl_oldframe = (cent->dl_oldframe) % stringlength;
			if (cent->dl_oldframe < 3 && cent->dl_sound) { // < 3 so if an alarm comes back into the pvs it will only start a sound if it's going to be closely synced with the light, otherwise wait till the next cycle
				trap->S_StartSound(NULL, cent->currentState.number, CHAN_AUTO, cgs.gameSounds[cent->dl_sound]);
			}
		}

		if (cent->dl_frame >= stringlength) {
			cent->dl_frame = (cent->dl_frame) % stringlength;
		}

		cent->dl_backlerp = cent->dl_backlerp - (int)cent->dl_backlerp;
	}


	lastch = cent->dl_stylestring[cent->dl_oldframe] - 'a';
	nextch = cent->dl_stylestring[cent->dl_frame] - 'a';

	lightval = (lastch * (1.0 - cent->dl_backlerp)) + (nextch * cent->dl_backlerp);

	lightval = (lightval * (1000.0f / 24.0f)) - 200.0f;  // they want 'm' as the "middle" value as 300

	lightval = max(0.0f, lightval);
	lightval = min(1000.0f, lightval);

	cl = cent->currentState.constantLight;
	r = cl & 255;
	g = (cl >> 8) & 255;
	b = (cl >> 16) & 255;

	trap->R_AddLightToScene(cent->lerpOrigin, lightval, (float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);
}

