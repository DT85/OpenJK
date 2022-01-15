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


/*
reads in ext_data\player.dat to playerExtData

example:

{
spinenumber	1

name	lower_lumbar	// DEFAULT = lower_lumbar
X		POSITIVE_X		// DEFAULT = POSITIVE_X
Y		NEGATIVE_Y		// DEFAULT = NEGATIVE_Y
Z		NEGATIVE_Z		// DEFAULT = NEGATIVE_Z
pitch	0.40			// DEFAULT = 0.40
roll	0.45			// DEFAULT = 0.45
yaw		0.45			// DEFAULT = 0.45
}

TODO:
- Add head & neck bones
*/

#include "g_local.h"
#include "ghoul2/G2.h"



struct playerExtParms_s
{
	int	spineNum; 	// Current spine number
} playerExtParms;

void PL_SpineNumber(const char** holdBuf);
void PL_SpineName(const char** holdBuf);
void PL_SpineXYZ(const char** holdBuf);
void PL_SpinePitch(const char** holdBuf);
void PL_SpineRoll(const char** holdBuf);
void PL_SpineYaw(const char** holdBuf);


typedef struct
{
	const char* parmName;
	void	(*func)(const char** holdBuf);
} playerExtParms_t;


#define PL_PARM_MAX 12


playerExtParms_t PlayerExtParms[PL_PARM_MAX] =
{
//use me
};

playerExtParms_t PlayerExtSpineParms[PL_PARM_MAX] =
{
	{ "spinenumber",	PL_SpineNumber },
	{ "name",			PL_SpineName },
	{ "X",				PL_SpineXYZ },
	{ "Y",				PL_SpineXYZ },
	{ "Z",				PL_SpineXYZ },
	{ "pitch",			PL_SpinePitch },
	{ "roll",			PL_SpineRoll },
	{ "yaw",			PL_SpineYaw },
};

static void PL_SpineNumber(const char** holdBuf)
{
	int spineNum;
	const char* tokenStr;

	if (COM_ParseString(holdBuf, &tokenStr))
		return;

	if (!Q_stricmp(tokenStr, "1"))
		spineNum = PL_SPINE1;
	else if (!Q_stricmp(tokenStr, "2"))
		spineNum = PL_SPINE2;
	else if (!Q_stricmp(tokenStr, "3"))
		spineNum = PL_SPINE3;
	else
	{
		spineNum = 0;
		gi.Printf("WARNING: bad spine number in external player data '%s'\n", tokenStr);
	}

	playerExtParms.spineNum = spineNum;
}

void PL_SpineName(const char** holdBuf)
{
	int len;
	const char* tokenStr;

	if (COM_ParseString(holdBuf, &tokenStr))
	{
		return;
	}

	len = strlen(tokenStr);
	len++;
	if (len > 32)
	{
		len = 32;
		gi.Printf("WARNING: name too long in external PLAYER.DAT '%s'\n", tokenStr);
	}

	Q_strncpyz(playerExtData[playerExtParms.spineNum].name, tokenStr, len);
}

void PL_SpineXYZ(const char** holdBuf)
{
	int orientation;
	const char* tokenStr;

	if (COM_ParseString(holdBuf, &tokenStr))
		return;

	if (!Q_stricmp(tokenStr, "POSITIVE_X"))
		orientation = POSITIVE_X;
	else if (!Q_stricmp(tokenStr, "POSITIVE_Y"))
		orientation = POSITIVE_Y;
	else if (!Q_stricmp(tokenStr, "POSITIVE_Z"))
		orientation = POSITIVE_Z;
	else if (!Q_stricmp(tokenStr, "NEGATIVE_X"))
		orientation = NEGATIVE_X;
	else if (!Q_stricmp(tokenStr, "NEGATIVE_Y"))
		orientation = NEGATIVE_Y;
	else if (!Q_stricmp(tokenStr, "NEGATIVE_Z"))
		orientation = NEGATIVE_Z;
	else
	{
		orientation = NULL;
		gi.Printf("WARNING: bad orientation in external player data '%s'\n", tokenStr);
	}

	if (!Q_stricmp((const char*)holdBuf, "spineX"))
		playerExtData[playerExtParms.spineNum].X = (Eorientations)orientation;
	else if (!Q_stricmp((const char*)holdBuf, "spineY"))
		playerExtData[playerExtParms.spineNum].Y = (Eorientations)orientation;
	else if (!Q_stricmp((const char*)holdBuf, "spineZ"))
		playerExtData[playerExtParms.spineNum].Z = (Eorientations)orientation;
}

void PL_SpinePitch(const char** holdBuf)
{
	float	tokenFlt;

	if (COM_ParseFloat(holdBuf, &tokenFlt))
	{
		SkipRestOfLine(holdBuf);
		return;
	}

	playerExtData[playerExtParms.spineNum].pitch = tokenFlt;
}

void PL_SpineRoll(const char** holdBuf)
{
	float	tokenFlt;

	if (COM_ParseFloat(holdBuf, &tokenFlt))
	{
		SkipRestOfLine(holdBuf);
		return;
	}

	playerExtData[playerExtParms.spineNum].roll = tokenFlt;
}

void PL_SpineYaw(const char** holdBuf)
{
	float	tokenFlt;

	if (COM_ParseFloat(holdBuf, &tokenFlt))
	{
		SkipRestOfLine(holdBuf);
		return;
	}

	playerExtData[playerExtParms.spineNum].yaw = tokenFlt;
}

static void PL_ParsePlayerParms(const char** holdBuf)
{
	const char* token;
	int		i;

	while (holdBuf)
	{
		token = COM_ParseExt(holdBuf, qtrue);

		if (!Q_stricmp(token, "}"))	// End of data for this
			break;

		// Loop through possible parameters
		for (i = 0; i < PL_PARM_MAX; ++i)
		{
			if (!Q_stricmp(token, PlayerExtParms[i].parmName))
			{
				PlayerExtParms[i].func(holdBuf);
				break;
			}
		}

		if (i < PL_PARM_MAX)	// Find parameter???
			continue;

		Com_Printf("^3WARNING: bad parameter in external player data '%s'\n", token);
		SkipRestOfLine(holdBuf);
	}
}

static void PL_ParsePlayerSpineParms(const char** holdBuf)
{
	const char* token;
	int		i;

	while (holdBuf)
	{
		token = COM_ParseExt(holdBuf, qtrue);

		if (!Q_stricmp(token, "}"))	// End of data for this
			break;

		// Loop through possible parameters
		for (i = 0; i < PL_PARM_MAX; ++i)
		{
			if (!Q_stricmp(token, PlayerExtSpineParms[i].parmName))
			{
				PlayerExtSpineParms[i].func(holdBuf);
				break;
			}
		}

		if (i < PL_PARM_MAX)	// Find parameter???
			continue;

		Com_Printf("^3WARNING: bad spine parameter in external player data '%s'\n", token);
		SkipRestOfLine(holdBuf);
	}
}

static void PL_ParseParms(const char *buffer)
{
	const char* holdBuf;
	const char* token;

	holdBuf = buffer;
	COM_BeginParseSession();

	while (holdBuf)
	{
		token = COM_ParseExt(&holdBuf, qtrue);

		if (!Q_stricmp(token, "{"))
		{
			PL_ParsePlayerSpineParms(&holdBuf);
		}

	}

	COM_EndParseSession();
}

void PL_LoadPlayerParms (void)
{
	char *buffer;
	int len;

	len = gi.FS_ReadFile("ext_data/player.dat", (void**)&buffer);

	if (len == -1)
		Com_Error(ERR_FATAL, "Cannot find ext_data/player.dat!\n");

	// initialise the data area
	memset(playerExtData, 0, sizeof(playerExtData));

	// put in the default values, because backwards compatibility is awesome!
	for (int i = 0; i < PL_NUM_SPINES; i++)
	{
		playerExtData[i].X = POSITIVE_X;
		playerExtData[i].Y = NEGATIVE_Y;
		playerExtData[i].Z = NEGATIVE_Z;
	}

	PL_ParseParms(buffer);

	gi.FS_FreeFile( buffer );	//let go of the buffer
}

