/*
smoke_volume.cpp - the smoke grenade's cloud as a volume (story 061)
Copyright (C) 2026 aluzed

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.
*/

// As Counter-Strike 2's: a grid of cells filled from the grenade outwards
// over its first second, the walls stopping it (a trace between two
// neighbouring cells), so that it takes the shape of the place; a fixed
// number of cells, so a smoke blocked on one side flows further on the
// other. Drawn as one soft puff per cell, sorted back to front.
//
// An HE grenade (SmokeBlast: origin, radius, yaw) empties the cells in an
// oval around it, long along the throw so that one can see through the
// smoke, and throws their puffs outwards; after a moment the smoke flows back in,
// from the outside of the hole to its centre. The smoke's own end (it thins
// from 13 seconds, the outer layers first, and is gone at 17, or the round
// ends) never moves.
//
// Drawn with the TriAPI in the client's transparent pass: ref_vk gives it
// ref_gl's 3D state there, blends its alpha as GL does in gamma space, and
// keeps the rays of r_vk_quality rt off what it covers (vk_pipeline.c).
//
// Everything is computed from the map, the grenade's origin and the seed
// sent with the event: every client sees the same volume and the same
// hole.

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "triangleapi.h"
#include "event_api.h"
#include "pm_defs.h"
#include "com_model.h"
#include "parsemsg.h"
#include "smoke_volume.h"

#include <vector>
#include <algorithm>
#include <math.h>

extern vec3_t v_origin, v_angles;
extern float g_flRoundTime;

namespace
{

const float CELL = 24.0f;            // size of a cell
const int MAX_CELLS = 460;           // cells of a smoke: a dome of about 145 units (a light smoke half)
const float MAX_RISE = 120.0f;       // the cloud's top above the grenade
const float MAX_DROP = 64.0f;        // and how far it flows down
const float FILL_TIME = 1.0f;        // seconds to fill the volume
const float APPEAR_TIME = 0.35f;     // seconds for a cell to thicken
const float FADE_START = 13.0f;      // the smoke thins from the outside in
const float FADE_END = 17.0f;        // and is gone, as the server's
const float CELL_FADE = 1.5f;        // seconds for one cell to dissolve
const float BLAST_HOLD = 1.5f;       // the hole stays open, then
const float BLAST_REFILL = 2.5f;     // the smoke flows back in
const float BLAST_PUSH = 130.0f;     // how far a puff is thrown at most
const float BLAST_ALONG = 1.6f;      // the oval of a blast, in its radius: along the throw
const float BLAST_SIDE = 0.65f;      // across it
const float BLAST_UP = 0.9f;         // and up and down
const float PUFF_SIZE = 44.0f;       // half the size of a puff
const float PUFF_ALPHA = 0.9f;

struct Cell
{
	Vector pos;
	int neighbours[6];
	int layer;          // fill order: cells from the grenade
	float size, angle;  // of its puff
	int variant;        // one of the four puffs of smoke_puff.spr
	float shade;        // light at the cell
	float edge;         // 1 inside the cloud, less on its border
	float dissolve;     // age at which it starts to dissolve
	// the last blast that reached it
	float removed;      // part of the cell blown away (0..1)
	float blastTime;
	float refillStart;
	Vector push;        // where its puff was thrown
};

struct Smoke
{
	float created;
	int layers;
	std::vector<Cell> cells;
};

std::vector<Smoke> s_smokes;
cvar_t *cl_smoke_volume;
cvar_t *cl_smoke_debug;
HSPRITE s_hPuff; // sprites/smoke_puff.spr: four thick puffs in a 2x2 sheet

// a small generator of our own: the same numbers on every client
struct Random
{
	unsigned int state;
	explicit Random( int seed ) : state( (unsigned int)seed * 2654435761u + 1u ) { }
	float Float( float lo, float hi )
	{
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return lo + ( hi - lo ) * ( state & 0xffffff ) / float( 0xffffff );
	}
};

bool Passable( const Vector &from, const Vector &to )
{
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( (float *)&from, (float *)&to, PM_WORLD_ONLY, -1, &tr );
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

float Smoothstep( float x )
{
	x = x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x );
	return x * x * ( 3.0f - 2.0f * x );
}

void Build( Smoke &smoke, const Vector &origin, int seed, bool light )
{
	const int budget = light ? MAX_CELLS / 2 : MAX_CELLS;
	Random rnd( seed );

	// the first cell: just above the grenade, or a little higher when the
	// grenade lies against something
	Vector start = origin + Vector( 0.0f, 0.0f, 12.0f );
	if( gEngfuncs.PM_PointContents( (float *)&start, NULL ) == CONTENTS_SOLID )
		start = origin + Vector( 0.0f, 0.0f, CELL );

	struct Key { int x, y, z; };
	std::vector<Key> keys;
	std::vector<Cell> &cells = smoke.cells;
	cells.reserve( budget );
	keys.reserve( budget );

	auto find = [&]( int x, int y, int z ) -> int
	{
		for( size_t i = 0; i < keys.size(); i++ )
			if( keys[i].x == x && keys[i].y == y && keys[i].z == z )
				return (int)i;
		return -1;
	};

	auto add = [&]( int x, int y, int z, int layer )
	{
		Cell c;
		c.pos = start + Vector( x * CELL, y * CELL, z * CELL );
		for( int k = 0; k < 6; k++ )
			c.neighbours[k] = -1;
		c.layer = layer;
		c.size = PUFF_SIZE * rnd.Float( 0.85f, 1.2f );
		c.angle = rnd.Float( 0.0f, 6.2831853f );
		c.variant = (int)rnd.Float( 0.0f, 3.999f );
		c.edge = 1.0f;
		c.dissolve = rnd.Float( 0.0f, 1.0f ); // a random share, turned into an age once the layers are known
		c.removed = 0.0f;
		c.blastTime = c.refillStart = 0.0f;
		c.push = Vector( 0.0f, 0.0f, 0.0f );
		// a puff a little off the grid's centre
		c.pos = c.pos + Vector( rnd.Float( -5, 5 ), rnd.Float( -5, 5 ), rnd.Float( -4, 4 ));

		float lightv[3] = { 128, 128, 128 };
		gEngfuncs.pTriAPI->LightAtPoint( (float *)&c.pos, lightv );
		const float l = ( lightv[0] + lightv[1] + lightv[2] ) / ( 3.0f * 255.0f );
		c.shade = 0.55f + 0.45f * ( l > 1.0f ? 1.0f : l );
		// lit from above: the bottom of the cloud darker than its top, and
		// each puff a little different
		const float height = ( z * CELL + MAX_DROP ) / ( MAX_RISE + MAX_DROP );
		c.shade *= ( 0.78f + 0.3f * height ) * rnd.Float( 0.92f, 1.06f );
		if( c.shade > 1.0f ) c.shade = 1.0f;

		cells.push_back( c );
		keys.push_back( { x, y, z } );
	};

	static const int dirs[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetSolidPlayers( -1 );

	// the cells nearest to the grenade first (a priority queue on the
	// distance), reached only through open neighbours: a ball where nothing
	// stops it, flowing round the walls and further where it is stopped,
	// a fixed number of cells as CS2's smoke has a fixed volume
	struct Open { float dist; int x, y, z; };
	auto farther = []( const Open &a, const Open &b )
	{
		if( a.dist != b.dist ) return a.dist > b.dist;
		if( a.z != b.z ) return a.z > b.z; // a strict order: the same everywhere
		if( a.y != b.y ) return a.y > b.y;
		return a.x > b.x;
	};
	std::vector<Open> open;
	auto distOf = []( int x, int y, int z ) { return sqrtf( float( x * x + y * y + z * z )) * CELL; };

	add( 0, 0, 0, 0 );
	size_t visited = 0;
	while( visited < cells.size() && (int)cells.size() < budget )
	{
		// open the neighbours of every cell not opened yet
		for( ; visited < cells.size(); visited++ )
		{
			const Key k = keys[visited];
			for( int d = 0; d < 6; d++ )
			{
				const int nx = k.x + dirs[d][0], ny = k.y + dirs[d][1], nz = k.z + dirs[d][2];
				const float dz = nz * CELL;
				if( dz > MAX_RISE || dz < -MAX_DROP )
					continue;
				if( find( nx, ny, nz ) >= 0 )
					continue;

				bool queued = false;
				for( const Open &q : open )
					if( q.x == nx && q.y == ny && q.z == nz )
						queued = true;
				if( queued )
					continue;

				const Vector to = start + Vector( nx * CELL, ny * CELL, nz * CELL );
				if( !Passable( start + Vector( k.x * CELL, k.y * CELL, k.z * CELL ), to ))
					continue;

				open.push_back( { distOf( nx, ny, nz ), nx, ny, nz } );
				std::push_heap( open.begin(), open.end(), farther );
			}
		}

		if( open.empty() )
			break;

		// the nearest open cell joins the cloud
		std::pop_heap( open.begin(), open.end(), farther );
		const Open q = open.back();
		open.pop_back();
		add( q.x, q.y, q.z, int( q.dist / CELL ));
	}

	gEngfuncs.pEventAPI->EV_PopPMStates();

	// neighbours, and the border of the cloud thinner
	for( size_t i = 0; i < cells.size(); i++ )
	{
		int count = 0;
		for( int d = 0; d < 6; d++ )
		{
			const int n = find( keys[i].x + dirs[d][0], keys[i].y + dirs[d][1], keys[i].z + dirs[d][2] );
			cells[i].neighbours[d] = n;
			if( n >= 0 )
				count++;
		}
		cells[i].edge = 0.45f + 0.55f * ( count / 6.0f );
	}

	smoke.layers = 1;
	for( const Cell &c : cells )
		if( c.layer + 1 > smoke.layers )
			smoke.layers = c.layer + 1;

	// the end: the outer layers dissolve first, the core last, each cell a
	// little apart from its neighbours so that the cloud frays as it thins
	const float span = FADE_END - CELL_FADE - FADE_START;
	for( Cell &c : cells )
	{
		const float outer = 1.0f - float( c.layer ) / smoke.layers;
		c.dissolve = FADE_START + span * ( 0.75f * outer + 0.25f * c.dissolve );
	}

	if( cl_smoke_debug && cl_smoke_debug->value )
		gEngfuncs.Con_Printf( "smoke at %.0f %.0f %.0f: %d cells, %d layers, seed %d\n", origin.x, origin.y, origin.z, (int)cells.size(), smoke.layers, seed );
}

// the part of a cell blown away now
float Removed( const Cell &c, float now )
{
	if( c.removed <= 0.0f )
		return 0.0f;
	if( now < c.refillStart )
		return c.removed;
	return c.removed * ( 1.0f - Smoothstep(( now - c.refillStart ) / APPEAR_TIME ));
}

// how far a point is in the oval of a blast: 0 at its centre, 1 on its edge
float BlastDistance( const Vector &rel, float radius, float yaw )
{
	const float a = yaw * ( 3.14159265f / 180.0f );
	const float ca = cosf( a ), sa = sinf( a );
	const float along = ( rel.x * ca + rel.y * sa ) / ( radius * BLAST_ALONG );
	const float side = ( rel.y * ca - rel.x * sa ) / ( radius * BLAST_SIDE );
	const float up = rel.z / ( radius * BLAST_UP );
	return sqrtf( along * along + side * side + up * up );
}

void Blast( Smoke &smoke, const Vector &origin, float radius, float yaw, float now )
{
	bool reached = false;
	for( Cell &c : smoke.cells )
	{
		const Vector rel = c.pos - origin;
		const float d = rel.Length();
		const float k = BlastDistance( rel, radius, yaw );
		if( k >= 1.0f )
			continue;

		// blown away in the middle, thinned over the outer third of the
		// oval: the sides of a smoke around the blast keep a little of it
		const float depth = Smoothstep(( 1.0f - k ) / 0.35f );
		const float now_removed = Removed( c, now );
		if( depth <= now_removed )
			continue;

		c.removed = depth;
		c.blastTime = now;
		c.push = ( d > 1.0f ? rel * ( 1.0f / d ) : Vector( 0.0f, 0.0f, 1.0f )) * ( BLAST_PUSH * ( 1.0f - k ));
		reached = true;
	}

	if( cl_smoke_debug && cl_smoke_debug->value )
		gEngfuncs.Con_Printf( "smoke blast at %.0f %.0f %.0f radius %.0f yaw %.0f: %s\n", origin.x, origin.y, origin.z, radius, yaw, reached ? "opened" : "no smoke in reach" );

	if( !reached )
		return;

	// the smoke flows back from the outside in: after the hold, the cells
	// furthest from the blast first, the centre of the hole last
	for( Cell &c : smoke.cells )
	{
		if( c.removed <= 0.0f || c.blastTime != now )
			continue;
		const float k = BlastDistance( c.pos - origin, radius, yaw );
		const float inward = 1.0f - ( k < 1.0f ? k : 1.0f );
		c.refillStart = now + BLAST_HOLD + BLAST_REFILL * inward;
	}
}

int MsgFunc_SmokeBlast( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	Vector origin;
	origin.x = reader.ReadCoord();
	origin.y = reader.ReadCoord();
	origin.z = reader.ReadCoord();
	const float radius = reader.ReadShort();
	const float yaw = reader.ReadAngle();

	SmokeVolume_Blast( origin, radius, yaw );
	return 1;
}

struct Puff
{
	Vector pos;
	float size, angle, alpha, shade, dist;
	int variant;
};

int s_statPuffs;
float s_statNearest;

void SmokeStats_f( void )
{
	int removed = 0, cells = 0;
	const float now = gEngfuncs.GetClientTime();
	for( const Smoke &smoke : s_smokes )
		for( const Cell &c : smoke.cells )
		{
			cells++;
			if( Removed( c, now ) > 0.01f )
				removed++;
		}
	gEngfuncs.Con_Printf( "smoke_stats: %d smokes, %d cells, %d blown, %d puffs drawn, nearest %.0f, view %.0f %.0f %.0f\n",
		(int)s_smokes.size(), cells, removed, s_statPuffs, s_statNearest, v_origin[0], v_origin[1], v_origin[2] );
}

// cl_smoke_debug 1: smoke_test makes a smoke where the crosshair points,
// smoke_blast_test blows a hole in it, on this client only
Vector s_testPoint;

void SmokeTest_f( void )
{
	if( !cl_smoke_debug || !cl_smoke_debug->value )
		return;

	Vector forward, right, up;
	AngleVectors( v_angles, forward, right, up );
	const Vector end = Vector( v_origin ) + forward * 4096.0f;

	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetSolidPlayers( -1 );
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace( v_origin, (float *)&end, PM_WORLD_ONLY, -1, &tr );
	gEngfuncs.pEventAPI->EV_PopPMStates();

	s_testPoint = Vector( tr.endpos ) + Vector( tr.plane.normal ) * 4.0f;
	SmokeVolume_Create( s_testPoint, gEngfuncs.Cmd_Argc() > 1 ? atoi( gEngfuncs.Cmd_Argv( 1 )) : 1, false );
}

void SmokeBlastTest_f( void )
{
	if( !cl_smoke_debug || !cl_smoke_debug->value )
		return;

	SmokeVolume_Blast( s_testPoint + Vector( 0.0f, 0.0f, 24.0f ), 200.0f, v_angles[1] );
}

} // namespace

void SmokeVolume_Init( void )
{
	cl_smoke_volume = gEngfuncs.pfnRegisterVariable( "cl_smoke_volume", "1", FCVAR_ARCHIVE );
	cl_smoke_debug = gEngfuncs.pfnRegisterVariable( "cl_smoke_debug", "0", 0 );
	gEngfuncs.pfnHookUserMsg( "SmokeBlast", MsgFunc_SmokeBlast );
	gEngfuncs.pfnAddCommand( "smoke_test", SmokeTest_f );
	gEngfuncs.pfnAddCommand( "smoke_blast_test", SmokeBlastTest_f );
	gEngfuncs.pfnAddCommand( "smoke_stats", SmokeStats_f );
}

void SmokeVolume_Reset( void )
{
	s_smokes.clear();
	s_hPuff = SPR_Load( "sprites/smoke_puff.spr" );
}

bool SmokeVolume_Enabled( void )
{
	return cl_smoke_volume && cl_smoke_volume->value != 0.0f;
}

void SmokeVolume_Create( const Vector &origin, int seed, bool light )
{
	Smoke smoke;
	smoke.created = gEngfuncs.GetClientTime();
	Build( smoke, origin, seed, light );
	s_smokes.push_back( std::move( smoke ));
}

void SmokeVolume_Blast( const Vector &origin, float radius, float yaw )
{
	const float now = gEngfuncs.GetClientTime();
	for( Smoke &smoke : s_smokes )
		Blast( smoke, origin, radius, yaw, now );
}

void SmokeVolume_Draw( void )
{
	if( s_smokes.empty() )
		return;

	const float now = gEngfuncs.GetClientTime();
	const float life = FADE_END;

	// smokes of a past round or over
	s_smokes.erase( std::remove_if( s_smokes.begin(), s_smokes.end(), [&]( const Smoke &s )
	{
		return now - s.created > life || now < s.created || ( g_flRoundTime > 0.0f && s.created < g_flRoundTime );
	}), s_smokes.end() );

	// Valve's puff (one per sprite, faint) when the extras lack ours
	const model_t *pGas = s_hPuff ? gEngfuncs.GetSpritePointer( s_hPuff ) : NULL;
	const bool sheet = pGas != NULL;
	if( !pGas )
		pGas = gEngfuncs.GetSpritePointer( gHUD.m_hGasPuff );
	if( !pGas )
		return;

	static std::vector<Puff> puffs;
	puffs.clear();

	const Vector view = v_origin;
	for( const Smoke &smoke : s_smokes )
	{
		const float age = now - smoke.created;
		// the whole cloud lighter as it thins, on top of its cells leaving
		const float fade = 1.0f - 0.6f * Smoothstep(( age - FADE_START ) / ( FADE_END - FADE_START ));
		if( age >= FADE_END )
			continue;

		const float layerTime = FILL_TIME / smoke.layers;
		for( const Cell &c : smoke.cells )
		{
			const float appear = Smoothstep(( age - c.layer * layerTime ) / APPEAR_TIME );
			if( appear <= 0.0f )
				continue;

			const float dissolve = 1.0f - Smoothstep(( age - c.dissolve ) / CELL_FADE );
			if( dissolve <= 0.0f )
				continue;

			float alpha = PUFF_ALPHA * c.edge * appear * fade * dissolve * ( 1.0f - Removed( c, now ));
			if( alpha < 0.01f )
				continue;

			// thrown outwards, coming back with the refill
			Vector pos = c.pos;
			if( c.removed > 0.0f )
			{
				const float s = now - c.blastTime;
				float k = s < 0.2f ? Smoothstep( s / 0.2f ) : 1.0f - Smoothstep(( now - c.blastTime - 0.2f ) / ( c.refillStart + APPEAR_TIME - c.blastTime - 0.2f ));
				pos = pos + c.push * k;
			}

			Puff p;
			p.pos = pos;
			p.size = c.size;
			p.angle = c.angle;
			p.variant = c.variant;
			p.alpha = alpha;
			p.shade = c.shade;
			p.dist = ( pos - view ).Length();
			if( p.dist < 8.0f )
				continue; // the puff the eye is in
			puffs.push_back( p );
		}
	}

	s_statPuffs = (int)puffs.size();
	s_statNearest = 99999.0f;
	for( const Puff &p : puffs )
		if( p.dist < s_statNearest )
			s_statNearest = p.dist;

	if( puffs.empty() )
		return;

	std::sort( puffs.begin(), puffs.end(), []( const Puff &a, const Puff &b ) { return a.dist > b.dist; });

	Vector forward, right, up;
	AngleVectors( v_angles, forward, right, up );

	triangleapi_s *tri = gEngfuncs.pTriAPI;
	tri->SpriteTexture( (model_s *)pGas, 0 );
	tri->RenderMode( kRenderTransAlpha );
	tri->CullFace( TRI_NONE );

	tri->Begin( TRI_QUADS );
	for( const Puff &p : puffs )
	{
		// a quad facing the view, turned by the puff's own angle
		const float ca = cosf( p.angle ) * p.size, sa = sinf( p.angle ) * p.size;
		const Vector a = right * ca + up * sa;
		const Vector b = up * ca - right * sa;

		// lit from above: each puff lighter at its top, darker at its bottom,
		// so that the puffs over each other show the cloud's billows
		const float g = 0.66f * p.shade;
		auto colour = [&]( const Vector &v )
		{
			float k = g * ( 1.0f + 0.32f * ( v.z - p.pos.z ) / p.size );
			k = k < 0.0f ? 0.0f : ( k > 1.0f ? 1.0f : k );
			tri->Color4f( k, k, k, p.alpha );
		};

		const Vector v0 = p.pos - a + b, v1 = p.pos + a + b, v2 = p.pos + a - b, v3 = p.pos - a - b;
		float s0 = 0.0f, t0 = 0.0f, ds = 1.0f;
		if( sheet )
		{
			s0 = ( p.variant & 1 ) * 0.5f;
			t0 = ( p.variant >> 1 ) * 0.5f;
			ds = 0.5f;
		}
		colour( v0 ); tri->TexCoord2f( s0, t0 ); tri->Vertex3f( v0.x, v0.y, v0.z );
		colour( v1 ); tri->TexCoord2f( s0 + ds, t0 ); tri->Vertex3f( v1.x, v1.y, v1.z );
		colour( v2 ); tri->TexCoord2f( s0 + ds, t0 + ds ); tri->Vertex3f( v2.x, v2.y, v2.z );
		colour( v3 ); tri->TexCoord2f( s0, t0 + ds ); tri->Vertex3f( v3.x, v3.y, v3.z );
	}
	tri->End();

	tri->RenderMode( kRenderNormal );
	tri->CullFace( TRI_FRONT );
}
