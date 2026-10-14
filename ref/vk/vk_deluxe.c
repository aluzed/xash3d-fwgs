/*
vk_deluxe.c - light directions of the lightmaps, for normal mapping
Copyright (C) 2026 aluzed

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
GoldSrc lightmaps say how much light a surface gets, not where it comes
from, and a normal map needs the direction. For every lightmap sample of
the world, this finds the direction the light mostly comes from, from what
the map compiler lit it with:

  light, light_spot      point lights of the entity lump ("_light" r g b i)
  light_environment      the sun, seen when a ray towards it reaches the sky
  textures with a ~      texture lights (lights.rad), one light per surface,
                         at its centre, for its area

Each light seen from the sample (a ray through the world BSP) weighs by
its brightness, distance and angle like the compiler's direct light. The
result is stored in a page parallel to each lightmap page, RGBA8: the
direction in model space as rgb * 2 - 1, and in alpha how much of the
light comes from it (1 for a single light, 0 for light from everywhere).
Only the direct light counts, bounced light has no direction here.

It runs once at map load, a few hundred milliseconds on big maps.
*/

#include "vk_local.h"

#define VK_DELUXE_MAX_LIGHTS 4096
#define VK_DELUXE_REF_DIST2  ( 128.0f * 128.0f ) // a light of intensity i gives i at 128 units
#define VK_DELUXE_TEXLIGHT   300.0f  // texture light brightness, lights.rad isn't in the map
#define VK_DELUXE_TEXAREA    4096.0f // a 64x64 texture light gives one such light

typedef enum
{
	VK_DLS_POINT,
	VK_DLS_SPOT,
	VK_DLS_SURFACE,
	VK_DLS_SUN,
} vk_deluxe_light_type_t;

typedef struct
{
	vk_deluxe_light_type_t type;
	vec3_t origin;
	vec3_t dir;      // spot and surface: where it shines; sun: towards the sun
	float intensity;
	float cone_inner, cone_outer; // spot cosines
} vk_deluxe_light_t;

static struct
{
	vk_deluxe_light_t lights[VK_DELUXE_MAX_LIGHTS];
	int num_lights;
	qboolean have_sun;
} vk_dl;

/*
=================
Light sources
=================
*/
// "_light": r g b brightness, or a brightness alone
static float VK_ParseLightValue( const char *value )
{
	float v[4] = { 0 };
	int n = sscanf( value, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3] );

	if( n == 4 )
		return ( 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2] ) / 255.0f * v[3];
	if( n == 3 )
		return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
	if( n == 1 )
		return v[0];

	return 300.0f; // the compiler's default
}

// the direction the compiler gives an entity from angles, angle and pitch
static void VK_LightAngles( const float *angles, float yaw, float pitch, qboolean have_pitch, vec3_t out )
{
	if( !have_pitch )
		pitch = angles[0];

	if( yaw == -1.0f ) // up
		VectorSet( out, 0.0f, 0.0f, 1.0f );
	else if( yaw == -2.0f ) // down
		VectorSet( out, 0.0f, 0.0f, -1.0f );
	else
	{
		const float a = DEG2RAD( yaw ), p = DEG2RAD( pitch );
		VectorSet( out, cosf( a ) * cosf( p ), sinf( a ) * cosf( p ), sinf( p ));
	}
}

static void VK_AddEntityLight( const char *classname, const vec3_t origin, float intensity, const vec3_t angles,
	float yaw, float pitch, qboolean have_pitch, float cone, float cone2 )
{
	vk_deluxe_light_t *l;

	if( vk_dl.num_lights >= VK_DELUXE_MAX_LIGHTS || intensity <= 0.0f )
		return;

	if( !Q_strcmp( classname, "light_environment" ))
	{
		// maps repeat it for every sky area, one sun is enough
		if( vk_dl.have_sun )
			return;
		vk_dl.have_sun = true;
	}

	l = &vk_dl.lights[vk_dl.num_lights++];
	memset( l, 0, sizeof( *l ));
	VectorCopy( origin, l->origin );
	l->intensity = intensity;

	if( !Q_strcmp( classname, "light_environment" ))
	{
		l->type = VK_DLS_SUN;
		VK_LightAngles( angles, yaw, pitch, have_pitch, l->dir );
		VectorNegate( l->dir, l->dir ); // towards the sun
	}
	else if( !Q_strcmp( classname, "light_spot" ))
	{
		l->type = VK_DLS_SPOT;
		VK_LightAngles( angles, yaw, pitch, have_pitch, l->dir );
		l->cone_inner = cosf( DEG2RAD( cone ));
		l->cone_outer = cosf( DEG2RAD( Q_max( cone2, cone + 1.0f )));
	}
	else l->type = VK_DLS_POINT;
}

static void VK_ParseEntityLights( const char *entities )
{
	char key[256], value[1024];
	char *p = (char *)entities;

	while(( p = COM_ParseFile( p, key, sizeof( key ))) != NULL )
	{
		char classname[64] = "";
		vec3_t origin = { 0 }, angles = { 0 };
		float intensity = 300.0f, yaw = 0.0f, pitch = 0.0f, cone = 10.0f, cone2 = 20.0f;
		qboolean have_pitch = false;

		if( key[0] != '{' )
			break;

		while(( p = COM_ParseFile( p, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if( !( p = COM_ParseFile( p, value, sizeof( value ))))
				break;

			if( !Q_strcmp( key, "classname" ))
				Q_strncpy( classname, value, sizeof( classname ));
			else if( !Q_strcmp( key, "origin" ))
				sscanf( value, "%f %f %f", &origin[0], &origin[1], &origin[2] );
			else if( !Q_strcmp( key, "angles" ))
			{
				sscanf( value, "%f %f %f", &angles[0], &angles[1], &angles[2] );
				yaw = angles[1];
			}
			else if( !Q_strcmp( key, "angle" ))
				yaw = Q_atof( value );
			else if( !Q_strcmp( key, "pitch" ))
			{
				pitch = Q_atof( value );
				have_pitch = true;
			}
			else if( !Q_strcmp( key, "_light" ))
				intensity = VK_ParseLightValue( value );
			else if( !Q_strcmp( key, "_cone" ))
				cone = Q_atof( value );
			else if( !Q_strcmp( key, "_cone2" ))
				cone2 = Q_atof( value );
		}

		if( !Q_strncmp( classname, "light", 5 ))
			VK_AddEntityLight( classname, origin, intensity, angles, yaw, pitch, have_pitch, cone, cone2 );

		if( !p )
			break;
	}
}

// texture lights: a light per surface at its centre, shining along its normal
static void VK_AddTextureLights( const model_t *mod )
{
	for( int i = 0; i < mod->numsurfaces && vk_dl.num_lights < VK_DELUXE_MAX_LIGHTS; i++ )
	{
		const msurface_t *surf = &mod->surfaces[i];
		vec3_t centre = { 0 }, normal;
		float area = 0.0f;
		int count = 0;

		if( !surf->texinfo || !surf->texinfo->texture || !Q_strchr( surf->texinfo->texture->name, '~' )
			|| FBitSet( surf->flags, SURF_DRAWSKY|SURF_DRAWTURB ))
			continue;

		for( const glpoly2_t *poly = surf->polys; poly; poly = poly->next )
		{
			for( int j = 0; j < poly->numverts; j++ )
			{
				VectorAdd( centre, poly->verts[j], centre );
				count++;

				// fan triangles
				if( j >= 2 )
				{
					vec3_t a, b, c;

					VectorSubtract( poly->verts[j - 1], poly->verts[0], a );
					VectorSubtract( poly->verts[j], poly->verts[0], b );
					CrossProduct( a, b, c );
					area += VectorLength( c ) * 0.5f;
				}
			}
		}

		if( !count || area <= 0.0f )
			continue;

		if( FBitSet( surf->flags, SURF_PLANEBACK ))
			VectorNegate( surf->plane->normal, normal );
		else VectorCopy( surf->plane->normal, normal );

		vk_deluxe_light_t *l = &vk_dl.lights[vk_dl.num_lights++];
		memset( l, 0, sizeof( *l ));
		l->type = VK_DLS_SURFACE;
		VectorScale( centre, 1.0f / count, l->origin );
		VectorMA( l->origin, 2.0f, normal, l->origin ); // off the surface for the visibility rays
		VectorCopy( normal, l->dir );
		l->intensity = VK_DELUXE_TEXLIGHT * area / VK_DELUXE_TEXAREA;
	}
}

/*
=================
Visibility
=================
*/
static int VK_LeafContents( const mnode_t *node )
{
	const int contents = ((const mleaf_t *)node )->contents;

	return contents == CONTENTS_SOLID || contents == CONTENTS_SKY ? contents : CONTENTS_EMPTY;
}

// the first solid or sky leaf from p1 to p2, CONTENTS_EMPTY when there is none
static int VK_TraceWorld( mnode_t *node, const vec3_t p1, const vec3_t p2 )
{
	while( node->contents >= 0 )
	{
		const float d1 = PlaneDiff( p1, node->plane );
		const float d2 = PlaneDiff( p2, node->plane );

		if( d1 >= 0.0f && d2 >= 0.0f )
			node = node_child( node, 0, WORLDMODEL );
		else if( d1 < 0.0f && d2 < 0.0f )
			node = node_child( node, 1, WORLDMODEL );
		else
		{
			const int side = d1 < 0.0f;
			vec3_t mid;
			int hit;

			VectorLerp( p1, d1 / ( d1 - d2 ), p2, mid );

			if(( hit = VK_TraceWorld( node_child( node, side, WORLDMODEL ), p1, mid )) != CONTENTS_EMPTY )
				return hit;

			return VK_TraceWorld( node_child( node, !side, WORLDMODEL ), mid, p2 );
		}
	}

	return VK_LeafContents( node );
}

static int VK_PointContents( const vec3_t p )
{
	mnode_t *node = WORLDMODEL->nodes;

	while( node->contents >= 0 )
		node = node_child( node, PlaneDiff( p, node->plane ) < 0.0f, WORLDMODEL );

	return VK_LeafContents( node );
}

/*
=================
VK_DeluxeSample

the light direction at a point of a surface, alpha its share of the light
=================
*/
static void VK_DeluxeSample( const vec3_t p, const vec3_t normal, vec4_t out )
{
	vec3_t sum = { 0 };
	float total = 0.0f;

	for( int i = 0; i < vk_dl.num_lights; i++ )
	{
		const vk_deluxe_light_t *l = &vk_dl.lights[i];
		vec3_t dir, end;
		float w, ndl;

		if( l->type == VK_DLS_SUN )
		{
			VectorCopy( l->dir, dir );
			if(( ndl = DotProduct( normal, dir )) <= 0.0f )
				continue;

			VectorMA( p, 16384.0f, dir, end );
			if( VK_TraceWorld( WORLDMODEL->nodes, p, end ) != CONTENTS_SKY )
				continue;

			w = l->intensity;
		}
		else
		{
			float dist2;

			VectorSubtract( l->origin, p, dir );
			dist2 = DotProduct( dir, dir );
			VectorScale( dir, 1.0f / sqrtf( Q_max( dist2, 1.0f )), dir );

			if(( ndl = DotProduct( normal, dir )) <= 0.0f )
				continue;

			w = l->intensity * VK_DELUXE_REF_DIST2 / Q_max( dist2, 256.0f );

			if( l->type == VK_DLS_SPOT )
			{
				const float c = -DotProduct( dir, l->dir );

				if( c <= l->cone_outer )
					continue;
				if( c < l->cone_inner )
					w *= ( c - l->cone_outer ) / ( l->cone_inner - l->cone_outer );
			}
			else if( l->type == VK_DLS_SURFACE )
				w *= Q_max( -DotProduct( dir, l->dir ), 0.0f );

			// too faint to steer anything
			if( w < 0.5f )
				continue;

			if( VK_TraceWorld( WORLDMODEL->nodes, p, l->origin ) != CONTENTS_EMPTY )
				continue;
		}

		w *= ndl;
		VectorMA( sum, w, dir, sum );
		total += w;
	}

	if( total <= 0.0f )
	{
		VectorCopy( normal, out );
		out[3] = 0.0f;
		return;
	}

	const float len = VectorLength( sum );

	VectorScale( sum, 1.0f / Q_max( len, 1e-6f ), out );
	out[3] = Q_min( len / total, 1.0f );
}

// the point of the surface plane at lightmap coordinates s, t
static qboolean VK_LightmapPoint( const msurface_t *surf, float s, float t, vec3_t out )
{
	const mextrasurf_t *info = surf->info;
	const float *a = info->lmvecs[0], *b = info->lmvecs[1], *n = surf->plane->normal;
	const float rhs[3] = { s - a[3], t - b[3], surf->plane->dist };
	const float det = a[0] * ( b[1] * n[2] - b[2] * n[1] ) - a[1] * ( b[0] * n[2] - b[2] * n[0] ) + a[2] * ( b[0] * n[1] - b[1] * n[0] );

	if( fabsf( det ) < 1e-8f )
		return false;

	// Cramer's rule, rows a, b, n
	for( int i = 0; i < 3; i++ )
	{
		float m[3][3];

		for( int r = 0; r < 3; r++ )
		{
			const float *row = r == 0 ? a : r == 1 ? b : n;

			for( int c = 0; c < 3; c++ )
				m[r][c] = c == i ? rhs[r] : row[c];
		}

		out[i] = ( m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) - m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] )
			+ m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] )) / det;
	}

	return true;
}

/*
=================
VK_BuildDeluxemaps

fills pages[page] (BLOCK_SIZE squared, RGBA8, allocated by the caller)
for every lightmapped surface of the world model
=================
*/
void VK_BuildDeluxemaps( byte **pages, int block_size )
{
	const model_t *mod = WORLDMODEL;
	const double start = gEngfuncs.pfnTime();
	size_t samples = 0;

	memset( &vk_dl, 0, sizeof( vk_dl ));

	if( mod->entities )
		VK_ParseEntityLights( mod->entities );
	VK_AddTextureLights( mod );

	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		const msurface_t *surf = &mod->surfaces[i];
		const mextrasurf_t *info = surf->info;
		vec3_t normal, centre = { 0 };
		int count = 0;

		if( !pages[surf->lightmaptexturenum] || !surf->samples || FBitSet( surf->flags, SURF_DRAWTILED ) || !surf->plane )
			continue;

		const int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
		const int smax = ( info->lightextents[0] / sample_size ) + 1;
		const int tmax = ( info->lightextents[1] / sample_size ) + 1;
		byte *page = pages[surf->lightmaptexturenum];

		if( FBitSet( surf->flags, SURF_PLANEBACK ))
			VectorNegate( surf->plane->normal, normal );
		else VectorCopy( surf->plane->normal, normal );

		// samples at the edges may fall in the walls: pulled towards the middle
		for( const glpoly2_t *poly = surf->polys; poly; poly = poly->next )
		{
			for( int j = 0; j < poly->numverts; j++, count++ )
				VectorAdd( centre, poly->verts[j], centre );
		}
		if( count )
			VectorScale( centre, 1.0f / count, centre );

		for( int t = 0; t < tmax; t++ )
		{
			for( int s = 0; s < smax; s++ )
			{
				byte *dst = page + (( surf->light_t + t ) * block_size + surf->light_s + s ) * 4;
				vec3_t p;
				vec4_t d;

				if( !VK_LightmapPoint( surf, info->lightmapmins[0] + s * sample_size, info->lightmapmins[1] + t * sample_size, p ))
					continue;

				VectorMA( p, 1.0f, normal, p ); // the plane side of the surface

				for( int tries = 0; tries < 3 && count && VK_PointContents( p ) == CONTENTS_SOLID; tries++ )
				{
					VectorLerp( p, 0.5f, centre, p );
					VectorMA( p, 1.0f, normal, p );
				}

				VK_DeluxeSample( p, normal, d );
				dst[0] = (byte)bound( 0, Q_rint(( d[0] * 0.5f + 0.5f ) * 255.0f ), 255 );
				dst[1] = (byte)bound( 0, Q_rint(( d[1] * 0.5f + 0.5f ) * 255.0f ), 255 );
				dst[2] = (byte)bound( 0, Q_rint(( d[2] * 0.5f + 0.5f ) * 255.0f ), 255 );
				dst[3] = (byte)bound( 0, Q_rint( d[3] * 255.0f ), 255 );
				samples++;
			}
		}
	}

	gEngfuncs.Con_Printf( "ref_vk: light directions of %u lightmap samples from %d lights in %.0f ms\n",
		(uint)samples, vk_dl.num_lights, ( gEngfuncs.pfnTime() - start ) * 1000.0 );
}
