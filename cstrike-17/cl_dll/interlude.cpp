/*
interlude.cpp - a match's interludes: the teams shown by a camera (story 058)
Copyright (C) 2026 aluzed

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.
*/

// At the start of a match, at half time, at the start of overtime and at its
// switch the server holds the players still in their spawns for a few
// seconds and sends Interlude: the players shown, where they stand and
// where they face. Every client then looks at them from a camera of its own
// instead of its player's eyes, without HUD nor view model, each shown
// player's name under their feet in their team's colour. At the end of the
// match only the winners are shown (both teams on a draw).
//
// The camera: a group of players (a team, or both teams when their spawns
// are close enough to be framed together) is looked at from in front of it,
// above, starting close and travelling slowly back until all of it is in
// view. In front is where the players face, or across the line between the
// two teams; when a wall is there, the direction with the most room. Every
// position is traced against the world so that the camera never goes
// through a wall. Two teams too far apart are shown one after the other,
// half of the time each.

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "triangleapi.h"
#include "event_api.h"
#include "pm_defs.h"
#include "ref_params.h"
#include "parsemsg.h"
#include "draw_util.h"
#include "interlude.h"

#include <vector>
#include <math.h>

void VectorAngles( const float *forward, float *angles );

namespace
{

enum
{
	SHOWN_T = 1,
	SHOWN_CT = 2,
	MAX_SHOWN = 32,
};

// two teams farther apart than this are shown one after the other
const float FRAME_TOGETHER = 1400.0f;
// the eyes above a player's origin, the feet below
const float FEET = 36.0f;
const float DEG2RAD = 3.14159265f / 180.0f;

struct Shown
{
	int index;
	int team;
	Vector pos;
	float yaw;
	float screenX, screenY;
	bool onScreen;
};

struct Shot
{
	Vector center;  // looked at
	Vector dir;     // from the center to the camera, flat
	float distStart, distEnd;
	float lift;     // the height for a distance
	float t0, t1;   // client time
};

Shown s_shown[MAX_SHOWN];
int s_numShown;
std::vector<Shot> s_shots;
float s_start, s_end;

float Clamp01( float x )
{
	return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x );
}

// the free distance from a point along a flat direction, up to max
float Room( const Vector &from, const Vector &dir, float max )
{
	const Vector to = from + dir * max;
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)&from, (float *)&to, PM_WORLD_ONLY, -1, &tr );
	if( tr.startsolid || tr.allsolid )
		return 0.0f;
	return tr.fraction * max;
}

Vector Flat( float yaw )
{
	const float r = yaw * DEG2RAD;
	return Vector( cosf( r ), sinf( r ), 0.0f );
}

// the camera at a distance and height, pulled in by the walls
Vector CameraAt( const Vector &eye, const Vector &dir, float dist, float lift )
{
	const Vector want = eye + dir * dist + Vector( 0.0f, 0.0f, lift );
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)&eye, (float *)&want, PM_WORLD_ONLY, -1, &tr );
	if( tr.fraction >= 1.0f )
		return want;
	const float len = ( want - eye ).Length( );
	return eye + ( want - eye ) * fmaxf( tr.fraction - 16.0f / fmaxf( len, 1.0f ), 0.0f );
}

bool Sees( const Vector &from, const Vector &to )
{
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)&from, (float *)&to, PM_WORLD_ONLY, -1, &tr );
	return tr.fraction >= 1.0f;
}

// a shot of a group: its center, its size, the side and height to look from
void AddShot( const std::vector<const Shown *> &group, Vector prefer, float t0, float t1 )
{
	if( group.empty( ))
		return;

	Shot shot;
	shot.center = Vector( 0.0f, 0.0f, 0.0f );
	for( const Shown *s : group )
		shot.center = shot.center + s->pos;
	shot.center = shot.center * ( 1.0f / group.size( ));

	float radius = 0.0f;
	for( const Shown *s : group )
	{
		Vector d = s->pos - shot.center;
		d.z = 0.0f;
		radius = fmaxf( radius, d.Length( ));
	}
	radius += 32.0f;

	// far enough for the whole group in a 90 degree view
	const float need = radius * 1.2f + 110.0f;
	const Vector eye = shot.center + Vector( 0.0f, 0.0f, 24.0f );

	prefer.z = 0.0f;
	const bool hasPrefer = prefer.Length( ) > 0.3f;
	const float baseYaw = hasPrefer ? atan2f( prefer.y, prefer.x ) / DEG2RAD : 0.0f;

	// eight sides, at the usual height or looking down from above: the most
	// players in view from where the camera ends, then the least pulled in,
	// in front of the group and at the usual height when it's as good
	float bestScore = -1.0f;
	shot.dir = Flat( baseYaw );
	shot.lift = 0.3f;
	shot.distEnd = need;
	for( int high = 0; high < 2; high++ )
	{
		const float lift = high ? 0.9f : 0.3f;
		for( int i = 0; i < 8; i++ )
		{
			const Vector dir = Flat( baseYaw + i * 45.0f );
			const Vector cam = CameraAt( eye, dir, need, 16.0f + need * lift );
			const float reach = ( cam - eye ).Length( ) / ( Vector( dir * need ) + Vector( 0.0f, 0.0f, 16.0f + need * lift )).Length( );

			int seen = 0;
			for( const Shown *s : group )
				seen += Sees( cam, s->pos ) ? 1 : 0;

			const float score = seen * 10.0f + reach * 5.0f + ( i == 0 && hasPrefer ? 2.0f : 0.0f ) + ( high ? 0.0f : 1.0f );
			if( score > bestScore )
			{
				bestScore = score;
				shot.dir = dir;
				shot.lift = lift;
				shot.distEnd = need * fmaxf( reach, 0.2f );
			}
		}
	}

	shot.distStart = fminf( fmaxf( radius * 0.5f + 70.0f, 90.0f ), shot.distEnd );
	shot.t0 = t0;
	shot.t1 = t1;
	s_shots.push_back( shot );
}

void PlanShots( int kind )
{
	s_shots.clear( );

	std::vector<const Shown *> teams[3];
	Vector facing[3] = { Vector( 0, 0, 0 ), Vector( 0, 0, 0 ), Vector( 0, 0, 0 ) };
	for( int i = 0; i < s_numShown; i++ )
	{
		const Shown &s = s_shown[i];
		const int t = ( s.team == SHOWN_T || s.team == SHOWN_CT ) ? s.team : 0;
		teams[t].push_back( &s );
		facing[t] = facing[t] + Flat( s.yaw );
	}

	std::vector<const Shown *> all;
	for( int i = 0; i < s_numShown; i++ )
		all.push_back( &s_shown[i] );

	const float mid = ( s_start + s_end ) * 0.5f;

	if( teams[SHOWN_T].empty( ) || teams[SHOWN_CT].empty( ))
	{
		// one team (the winners), or players without a team
		AddShot( all, facing[0] + facing[SHOWN_T] + facing[SHOWN_CT], s_start, s_end );
		return;
	}

	Vector cT( 0, 0, 0 ), cCT( 0, 0, 0 );
	for( const Shown *s : teams[SHOWN_T] )
		cT = cT + s->pos;
	for( const Shown *s : teams[SHOWN_CT] )
		cCT = cCT + s->pos;
	cT = cT * ( 1.0f / teams[SHOWN_T].size( ));
	cCT = cCT * ( 1.0f / teams[SHOWN_CT].size( ));

	Vector across = cCT - cT;
	across.z = 0.0f;

	if( across.Length( ) <= FRAME_TOGETHER )
	{
		// both teams side by side: looked at across the line between them
		Vector side = Vector( -across.y, across.x, 0.0f );
		const Vector center = ( cT + cCT ) * 0.5f + Vector( 0, 0, 24 );
		if( side.Length( ) < 1.0f )
			side = Vector( 1, 0, 0 );
		side = side.Normalize( );
		if( Room( center, side * -1.0f, 2048.0f ) > Room( center, side, 2048.0f ))
			side = side * -1.0f;
		AddShot( all, side, s_start, s_end );
		return;
	}

	// too far apart: the terrorists, then the counter-terrorists
	AddShot( teams[SHOWN_T], facing[SHOWN_T], s_start, mid );
	AddShot( teams[SHOWN_CT], facing[SHOWN_CT], mid, s_end );
}

int MsgFunc_Interlude( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	const int kind = reader.ReadByte( );
	const float duration = reader.ReadByte( ) / 10.0f;
	const int count = reader.ReadByte( );

	s_numShown = 0;
	for( int i = 0; i < count; i++ )
	{
		Shown s;
		s.index = reader.ReadByte( );
		s.team = reader.ReadByte( );
		s.pos.x = reader.ReadCoord( );
		s.pos.y = reader.ReadCoord( );
		s.pos.z = reader.ReadCoord( );
		s.yaw = reader.ReadAngle( );
		s.onScreen = false;
		if( s_numShown < MAX_SHOWN )
			s_shown[s_numShown++] = s;
	}

	s_start = gEngfuncs.GetClientTime( );
	s_end = s_start + duration;

	if( s_numShown )
		PlanShots( kind );
	else
		s_end = s_start;

	return 1;
}

} // namespace

void Interlude_Init( void )
{
	gEngfuncs.pfnHookUserMsg( "Interlude", MsgFunc_Interlude );
}

void Interlude_Reset( void )
{
	s_numShown = 0;
	s_shots.clear( );
	s_start = s_end = 0.0f;
}

bool Interlude_Active( void )
{
	const float now = gEngfuncs.GetClientTime( );
	return !s_shots.empty( ) && now >= s_start && now < s_end;
}

void Interlude_CalcRefdef( struct ref_params_s *pparams )
{
	if( !Interlude_Active( ))
		return;

	const float now = gEngfuncs.GetClientTime( );
	const Shot *shot = &s_shots.back( );
	for( const Shot &s : s_shots )
	{
		if( now >= s.t0 && now < s.t1 )
		{
			shot = &s;
			break;
		}
	}

	// travels back fast at first, then slowly
	const float u = Clamp01(( now - shot->t0 ) / fmaxf( shot->t1 - shot->t0, 0.1f ));
	const float ease = 1.0f - ( 1.0f - u ) * ( 1.0f - u );
	const float dist = shot->distStart + ( shot->distEnd - shot->distStart ) * ease;

	// never through a wall, a ceiling or the sky
	const Vector eye = shot->center + Vector( 0.0f, 0.0f, 24.0f );
	const Vector cam = CameraAt( eye, shot->dir, dist, 16.0f + dist * shot->lift );

	Vector forward = shot->center - cam;
	Vector angles( 0, 0, 0 );
	VectorAngles( forward, angles );
	float pitch = angles.x > 180.0f ? angles.x - 360.0f : angles.x;

	VectorCopy( cam, pparams->vieworg );
	pparams->viewangles[0] = -pitch;
	pparams->viewangles[1] = angles.y;
	pparams->viewangles[2] = 0.0f;
}

// in the 3D pass, where the view's matrices are set: the names' places
void Interlude_Project( void )
{
	if( !Interlude_Active( ))
		return;

	for( int i = 0; i < s_numShown; i++ )
	{
		Shown &s = s_shown[i];
		Vector feet = s.pos - Vector( 0.0f, 0.0f, FEET + 4.0f );

		// the player as the client sees them, when it does
		cl_entity_t *ent = gEngfuncs.GetEntityByIndex( s.index );
		if( ent && ent->player && ent->curstate.messagenum == gEngfuncs.GetLocalPlayer( )->curstate.messagenum )
			feet = Vector( ent->origin[0], ent->origin[1], ent->origin[2] - FEET - 4.0f );

		float screen[3];
		s.onScreen = !gEngfuncs.pTriAPI->WorldToScreen( feet, screen );
		s.screenX = XPROJECT( screen[0] );
		s.screenY = YPROJECT( screen[1] );
	}
}

void Interlude_DrawNames( void )
{
	if( !Interlude_Active( ))
		return;

	for( int i = 0; i < s_numShown; i++ )
	{
		const Shown &s = s_shown[i];
		if( !s.onScreen )
			continue;

		hud_player_info_t info = { };
		gEngfuncs.pfnGetPlayerInfo( s.index, &info );
		if( !info.name || !info.name[0] )
			continue;

		int width, height;
		DrawUtils::ConsoleStringSize( info.name, &width, &height );
		if( s.team == SHOWN_T )
			DrawUtils::SetConsoleTextColor( (unsigned char)255, 63, 63 );
		else
			DrawUtils::SetConsoleTextColor( (unsigned char)153, 204, 255 );
		DrawUtils::DrawConsoleString( (int)s.screenX - width / 2, (int)s.screenY, info.name );
	}
}
