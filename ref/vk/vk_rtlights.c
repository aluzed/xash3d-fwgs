/*
vk_rtlights.c - the lights the map was compiled with, for ray traced lighting
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
GoldSrc maps keep what their lightmaps were baked from: the light entities
of the entity lump and the texture lights. The client reads them again,
no server involved:

  light, light_spot  point lights, "_light" r g b brightness; a "style"
                     key (the compiler writes the one it gave a named,
                     switchable light, from 32 up; without it, named
                     lights get them in entity order): their lightstyle
                     scales them every frame
  ~ textures         texture lights (not the +a... frames of switchable
                     ones, off as the map starts): one light per surface, at its
                     centre, shining along its normal with its area, the
                     colour of the texture (its last mip, on the GPU) and
                     a brightness lights.rad would give (not shipped with
                     the games: 300, or what maps/<map>.rtlights says)
  emissive maps      surfaces whose material (022) has one, the same way
  light_environment  the sun: movevars skycolor and skyvec, as in 025

Their colour is in lightmap units at 128 units of distance (a light of
brightness b gives b at 128 units), the units R_BuildLightMap adds styles
in. A grid of 256 unit cells over the world lists, for each cell, the
lights that matter there: those without a style first (sampled and
accumulated over the frames by rt_gi.comp), then those with one (traced
every frame, so lightstyles animate).

maps/<map>.rtlights, optional, one setting a line, // comments:

  scale <f>                         every light of the map
  texlight <texture> <r> <g> <b> <brightness>
  light <x> <y> <z> <r> <g> <b> <brightness> [style]
*/

#include "vk_local.h"

#define VK_RTL_MAX_LIGHTS    8192
#define VK_RTL_MAX_TEXLIGHTS 256
#define VK_RTL_CELL          256.0f
#define VK_RTL_CELL_LIGHTS   16     // static lights listed per cell
#define VK_RTL_CELL_STYLED   8      // styled ones
#define VK_RTL_MIN_LIGHT     0.5f   // lightmap units under which a light doesn't count in a cell
#define VK_RTL_TEXLIGHT      300.0f // texture light brightness without lights.rad
#define VK_RTL_TEXAREA       4096.0f
#define VK_RTL_REF_DIST2     ( 128.0f * 128.0f )
#define VK_RTL_SIZE2         ( 48.0f * 48.0f ) // fixture size of the falloff, rtfx_common.glsl

#define RT_LIGHT_POINT   0
#define RT_LIGHT_SPOT    1
#define RT_LIGHT_SURFACE 2

// mirrors rt_common.glsl
typedef struct
{
	float origin[3];
	int type;
	float color[3];    // lightmap units at 128 units
	int style;         // lightstyle, 0 for none
	float dir[3];      // spot, surface: where it shines
	float cone_outer;  // spot cosines
	float cone_inner;
	int texture;       // surface: its colour is the texture's average
	float pad[2];
} vk_rt_glight_t;

typedef struct
{
	float mins[4];     // w: cell size
	int dims[4];       // cells along x, y, z; lights
	uint32_t offsets[4]; // bytes from the start: cells (uvec2 each), indices, lights
} vk_rt_lightgrid_t;

typedef struct
{
	char name[16];
	float color[3];
	float brightness;
} vk_rtl_texlight_t;

static struct
{
	vk_rt_glight_t lights[VK_RTL_MAX_LIGHTS];
	char names[VK_RTL_MAX_LIGHTS][20]; // where each comes from, for r_vk_rtlights
	int num_lights;
	vk_rtl_texlight_t texlights[VK_RTL_MAX_TEXLIGHTS];
	int num_texlights;
	float scale;
	char targetnames[MAX_LIGHTSTYLES - 32][64]; // styles 32 and up, in order
	int num_targetnames;

	vk_buffer_t buffer;
	VkDeviceAddress address;
	int num_static, num_styled;
} rtl;

/*
=================
Parsing
=================
*/
static vk_rt_glight_t *VK_RTLNew( const char *name )
{
	vk_rt_glight_t *l;

	if( rtl.num_lights >= VK_RTL_MAX_LIGHTS )
		return NULL;

	Q_strncpy( rtl.names[rtl.num_lights], name, sizeof( rtl.names[0] ));
	l = &rtl.lights[rtl.num_lights++];
	memset( l, 0, sizeof( *l ));
	return l;
}

// "_light": r g b brightness, r g b, or a brightness alone (white)
static void VK_RTLParseColor( const char *value, vec3_t out )
{
	float v[4] = { 0 };
	int n = sscanf( value, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3] );

	if( n == 4 )
		VectorScale( v, v[3] / 255.0f, out );
	else if( n == 3 )
		VectorCopy( v, out );
	else if( n == 1 )
		VectorSet( out, v[0], v[0], v[0] );
	else VectorSet( out, 300.0f, 300.0f, 300.0f ); // the compiler's default
}

// the compiler's style of a named light: 32 and up, one per targetname
static int VK_RTLStyleForTargetname( const char *name )
{
	for( int i = 0; i < rtl.num_targetnames; i++ )
	{
		if( !Q_stricmp( rtl.targetnames[i], name ))
			return 32 + i;
	}

	if( 32 + rtl.num_targetnames >= MAX_LIGHTSTYLES )
		return 0;

	Q_strncpy( rtl.targetnames[rtl.num_targetnames], name, sizeof( rtl.targetnames[0] ));
	return 32 + rtl.num_targetnames++;
}

static void VK_RTLAngles( const vec3_t angles, float yaw, float pitch, qboolean have_pitch, vec3_t out )
{
	if( !have_pitch )
		pitch = angles[0];

	if( yaw == -1.0f )
		VectorSet( out, 0.0f, 0.0f, 1.0f );
	else if( yaw == -2.0f )
		VectorSet( out, 0.0f, 0.0f, -1.0f );
	else
	{
		const float a = DEG2RAD( yaw ), p = DEG2RAD( pitch );
		VectorSet( out, cosf( a ) * cosf( p ), sinf( a ) * cosf( p ), sinf( p ));
	}
}

static void VK_RTLParseEntities( const char *entities )
{
	char key[256], value[1024];
	char *p = (char *)entities;

	while(( p = COM_ParseFile( p, key, sizeof( key ))) != NULL && key[0] == '{' )
	{
		char classname[64] = "", targetname[64] = "";
		vec3_t origin = { 0 }, angles = { 0 }, color;
		float yaw = 0.0f, pitch = 0.0f, cone = 10.0f, cone2 = 20.0f;
		qboolean have_pitch = false;
		int style = 0;

		VectorSet( color, 300.0f, 300.0f, 300.0f );

		while(( p = COM_ParseFile( p, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if( !( p = COM_ParseFile( p, value, sizeof( value ))))
				break;

			if( !Q_strcmp( key, "classname" ))
				Q_strncpy( classname, value, sizeof( classname ));
			else if( !Q_strcmp( key, "targetname" ))
				Q_strncpy( targetname, value, sizeof( targetname ));
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
				VK_RTLParseColor( value, color );
			else if( !Q_strcmp( key, "_cone" ))
				cone = Q_atof( value );
			else if( !Q_strcmp( key, "_cone2" ))
				cone2 = Q_atof( value );
			else if( !Q_strcmp( key, "style" ))
				style = Q_atoi( value );
		}

		if( !p )
			break;

		// the sun is movevars' (025)
		if( Q_strcmp( classname, "light" ) && Q_strcmp( classname, "light_spot" ))
			continue;

		// the compiler writes the style it gave a named light; older maps may not
		if( targetname[0] && !style )
			style = VK_RTLStyleForTargetname( targetname );

		vk_rt_glight_t *l = VK_RTLNew( targetname[0] ? targetname : classname );
		if( !l )
			break;

		VectorCopy( origin, l->origin );
		VectorCopy( color, l->color );
		l->style = bound( 0, style, MAX_LIGHTSTYLES - 1 );

		if( !Q_strcmp( classname, "light_spot" ))
		{
			l->type = RT_LIGHT_SPOT;
			VK_RTLAngles( angles, yaw, pitch, have_pitch, l->dir );
			l->cone_inner = cosf( DEG2RAD( cone ));
			l->cone_outer = cosf( DEG2RAD( Q_max( cone2, cone + 1.0f )));
		}
		else l->type = RT_LIGHT_POINT;
	}
}

static const vk_rtl_texlight_t *VK_RTLTexlight( const char *name )
{
	for( int i = 0; i < rtl.num_texlights; i++ )
	{
		if( !Q_stricmp( rtl.texlights[i].name, name ))
			return &rtl.texlights[i];
	}

	return NULL;
}

// maps/<map>.rtlights
static void VK_RTLParseOverrides( const char *mapname )
{
	char path[MAX_QPATH], token[256];
	char *data, *p;

	COM_StripExtension( (char *)mapname );
	Q_snprintf( path, sizeof( path ), "%s.rtlights", mapname );

	if( !( data = (char *)gEngfuncs.fsapi->LoadFile( path, NULL, false )))
		return;

	gEngfuncs.Con_Reportf( "ref_vk: lights of %s\n", path );
	p = data;

	while(( p = COM_ParseFile( p, token, sizeof( token ))) != NULL )
	{
		float v[8];
		int n;

		if( !Q_stricmp( token, "scale" ))
		{
			p = COM_ParseFile( p, token, sizeof( token ));
			rtl.scale = Q_atof( token );
		}
		else if( !Q_stricmp( token, "texlight" ) && rtl.num_texlights < VK_RTL_MAX_TEXLIGHTS )
		{
			vk_rtl_texlight_t *t = &rtl.texlights[rtl.num_texlights];

			p = COM_ParseFile( p, token, sizeof( token ));
			Q_strncpy( t->name, token, sizeof( t->name ));
			for( n = 0; n < 4 && p; n++ )
			{
				p = COM_ParseFile( p, token, sizeof( token ));
				v[n] = Q_atof( token );
			}
			VectorScale( v, 1.0f / 255.0f, t->color );
			t->brightness = v[3];
			rtl.num_texlights++;
		}
		else if( !Q_stricmp( token, "light" ))
		{
			vk_rt_glight_t *l;

			for( n = 0; n < 8 && p; n++ )
			{
				char *next = COM_ParseFile( p, token, sizeof( token ));

				// the style is optional: a word starts the next setting
				if( n == 7 && ( !next || !Q_isdigit( token )))
					break;
				p = next;
				v[n] = Q_atof( token );
			}

			if( n >= 7 && ( l = VK_RTLNew( "rtlights" )))
			{
				VectorCopy( v, l->origin );
				VectorScale( &v[3], v[6] / 255.0f, l->color );
				l->style = n == 8 ? bound( 0, (int)v[7], MAX_LIGHTSTYLES - 1 ) : 0;
				l->type = RT_LIGHT_POINT;
			}
		}
	}

	Mem_Free( data );
}

// texture lights and emissive materials: a light per surface
static void VK_RTLAddSurfaceLights( const model_t *mod )
{
	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		const msurface_t *surf = &mod->surfaces[i];
		const texture_t *tex = surf->texinfo ? surf->texinfo->texture : NULL;
		const vk_rtl_texlight_t *t;
		vec3_t centre = { 0 }, normal, color;
		float area = 0.0f, strength;
		int count = 0, texnum, emissive;

		if( !tex || FBitSet( surf->flags, SURF_DRAWSKY|SURF_DRAWTURB ))
			continue;

		texnum = tex->gl_texturenum;
		emissive = VK_MaterialEmissive( texnum, &strength );

		if( !Q_strchr( tex->name, '~' ) && !emissive )
			continue;

		// +a ... +j: the other frames of a switchable light, off by default
		if( tex->name[0] == '+' && Q_tolower( tex->name[1] ) >= 'a' && Q_tolower( tex->name[1] ) <= 'j' )
			continue;

		for( const glpoly2_t *poly = surf->polys; poly; poly = poly->next )
		{
			for( int j = 0; j < poly->numverts; j++ )
			{
				VectorAdd( centre, poly->verts[j], centre );
				count++;

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

		// the texture's colour times a brightness, from the overrides when listed
		if(( t = VK_RTLTexlight( tex->name )) != NULL )
			VectorScale( t->color, t->brightness, color );
		else
		{
			const float b = emissive && !Q_strchr( tex->name, '~' ) ? VK_RTL_TEXLIGHT * strength : VK_RTL_TEXLIGHT;
			VectorSet( color, b, b, b );
		}

		vk_rt_glight_t *l = VK_RTLNew( tex->name );
		if( !l )
			return;

		l->type = RT_LIGHT_SURFACE;
		VectorScale( centre, 1.0f / count, l->origin );
		VectorMA( l->origin, 2.0f, normal, l->origin ); // off the surface for the shadow rays
		VectorCopy( normal, l->dir );
		VectorScale( color, area / VK_RTL_TEXAREA, l->color );
		l->texture = t ? 0 : ( emissive && !Q_strchr( tex->name, '~' ) ? emissive : texnum );
	}
}

/*
=================
The grid
=================
*/
// the most a light gives in a box: at its nearest point
static float VK_RTLReach( const vk_rt_glight_t *l, const vec3_t mins, const vec3_t maxs )
{
	vec3_t nearest, d;
	float d2;

	for( int i = 0; i < 3; i++ )
		nearest[i] = bound( mins[i], l->origin[i], maxs[i] );

	VectorSubtract( nearest, l->origin, d );
	d2 = DotProduct( d, d );

	return Q_max( l->color[0], Q_max( l->color[1], l->color[2] )) * ( VK_RTL_REF_DIST2 + VK_RTL_SIZE2 ) / ( d2 + VK_RTL_SIZE2 );
}

typedef struct
{
	int light;
	float reach;
} vk_rtl_pick_t;

static int VK_RTLPickCompare( const void *a, const void *b )
{
	const float d = ((const vk_rtl_pick_t *)b )->reach - ((const vk_rtl_pick_t *)a )->reach;
	return d > 0.0f ? 1 : d < 0.0f ? -1 : 0;
}

static qboolean VK_RTLBuildGrid( const model_t *mod )
{
	vk_rt_lightgrid_t header = { 0 };
	vk_rtl_pick_t *picks = Mem_Malloc( r_temppool, Q_max( rtl.num_lights, 1 ) * sizeof( *picks ));
	uint32_t *cells, *indices;
	int num_cells, num_indices = 0, max_indices;
	vk_buffer_t *buf = &rtl.buffer;
	qboolean ok;

	for( int i = 0; i < 3; i++ )
	{
		header.mins[i] = mod->mins[i] - 1.0f;
		header.dims[i] = bound( 1, (int)ceilf(( mod->maxs[i] - mod->mins[i] + 2.0f ) / VK_RTL_CELL ), 256 );
	}
	header.mins[3] = VK_RTL_CELL;
	header.dims[3] = rtl.num_lights;
	num_cells = header.dims[0] * header.dims[1] * header.dims[2];
	max_indices = num_cells * ( VK_RTL_CELL_LIGHTS + VK_RTL_CELL_STYLED );

	cells = Mem_Calloc( r_temppool, num_cells * 2 * sizeof( uint32_t ));
	indices = Mem_Malloc( r_temppool, Q_max( max_indices, 1 ) * sizeof( uint32_t ));

	for( int z = 0; z < header.dims[2]; z++ )
	for( int y = 0; y < header.dims[1]; y++ )
	for( int x = 0; x < header.dims[0]; x++ )
	{
		const int cell = ( z * header.dims[1] + y ) * header.dims[0] + x;
		vec3_t mins = { header.mins[0] + x * VK_RTL_CELL, header.mins[1] + y * VK_RTL_CELL, header.mins[2] + z * VK_RTL_CELL };
		vec3_t maxs = { mins[0] + VK_RTL_CELL, mins[1] + VK_RTL_CELL, mins[2] + VK_RTL_CELL };
		int count[2] = { 0, 0 };

		cells[cell * 2] = num_indices;

		// static lights, then styled ones, the strongest of each
		for( int styled = 0; styled < 2; styled++ )
		{
			int n = 0;

			for( int i = 0; i < rtl.num_lights; i++ )
			{
				const vk_rt_glight_t *l = &rtl.lights[i];
				const float reach = VK_RTLReach( l, mins, maxs );

				if(( l->style != 0 ) != styled || reach < VK_RTL_MIN_LIGHT )
					continue;

				picks[n++] = (vk_rtl_pick_t){ i, reach };
			}

			qsort( picks, n, sizeof( *picks ), VK_RTLPickCompare );
			count[styled] = Q_min( n, styled ? VK_RTL_CELL_STYLED : VK_RTL_CELL_LIGHTS );

			for( int i = 0; i < count[styled]; i++ )
				indices[num_indices++] = picks[i].light;
		}

		cells[cell * 2 + 1] = count[0] | ( count[1] << 16 );
	}

	header.offsets[0] = sizeof( header );
	header.offsets[1] = header.offsets[0] + num_cells * 2 * sizeof( uint32_t );
	header.offsets[2] = ( header.offsets[1] + num_indices * sizeof( uint32_t ) + 15 ) & ~15;

	const size_t size = header.offsets[2] + Q_max( rtl.num_lights, 1 ) * sizeof( vk_rt_glight_t );
	byte *data = Mem_Calloc( r_temppool, size );

	memcpy( data, &header, sizeof( header ));
	memcpy( data + header.offsets[0], cells, num_cells * 2 * sizeof( uint32_t ));
	memcpy( data + header.offsets[1], indices, num_indices * sizeof( uint32_t ));
	memcpy( data + header.offsets[2], rtl.lights, rtl.num_lights * sizeof( vk_rt_glight_t ));

	ok = VK_CreateBuffer( buf, "rt lights", size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		| VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	if( ok )
	{
		VkBuffer staging;
		VkDeviceSize offset;
		VkCommandBuffer cmd;
		void *dst = VK_UploadStaging( size, &staging, &offset, &cmd );

		if(( ok = dst != NULL ))
		{
			memcpy( dst, data, size );
			VK_UploadBufferRegion( cmd, staging, offset, buf->buffer, 0, size );
			rtl.address = VK_BufferAddress( buf->buffer );
		}
	}

	gEngfuncs.Con_Reportf( "ref_vk: %d lights for ray tracing (%d styled), grid %dx%dx%d, %d entries, %.1f KB\n",
		rtl.num_lights, rtl.num_styled, header.dims[0], header.dims[1], header.dims[2], num_indices, size / 1024.0 );

	Mem_Free( data );
	Mem_Free( cells );
	Mem_Free( indices );
	Mem_Free( picks );
	return ok;
}

/*
=================
VK_RTLightsBuild

the lights of the world model, with the acceleration structures of the map
=================
*/
void VK_RTLightsBuild( void )
{
	const model_t *mod = WORLDMODEL;
	char mapname[MAX_QPATH];

	VK_RTLightsFree();

	if( !mod )
		return;

	rtl.scale = 1.0f;
	Q_strncpy( mapname, mod->name, sizeof( mapname ));
	VK_RTLParseOverrides( mapname );

	if( mod->entities )
		VK_RTLParseEntities( mod->entities );
	VK_RTLAddSurfaceLights( mod );

	for( int i = 0; i < rtl.num_lights; i++ )
	{
		VectorScale( rtl.lights[i].color, rtl.scale, rtl.lights[i].color );
		if( rtl.lights[i].style )
			rtl.num_styled++;
		else rtl.num_static++;
	}

	if( !VK_RTLBuildGrid( mod ))
	{
		gEngfuncs.Con_Printf( S_ERROR "ref_vk: can't upload the lights of the map\n" );
		VK_RTLightsFree();
	}
}

void VK_RTLightsFree( void )
{
	VK_DestroyBuffer( &rtl.buffer );
	memset( &rtl, 0, sizeof( rtl ));
}

typedef struct
{
	int light;
	float dist;
} vk_rtl_near_t;

static int VK_RTLNearCompare( const void *a, const void *b )
{
	const float d = ((const vk_rtl_near_t *)a )->dist - ((const vk_rtl_near_t *)b )->dist;
	return d > 0.0f ? 1 : d < 0.0f ? -1 : 0;
}

// r_vk_rtlights [count]: the lights nearest the view, to tune maps/<map>.rtlights
static void VK_RTLights_f( void )
{
	static const char *types[] = { "point", "spot", "surface" };
	const int count = gEngfuncs.Cmd_Argc() > 1 ? Q_atoi( gEngfuncs.Cmd_Argv( 1 )) : 16;
	vk_rtl_near_t *nearest;
	int n = 0;

	if( !rtl.num_lights )
	{
		gEngfuncs.Con_Printf( "no lights: r_vk_quality rt or r_vk_rt 1 extracts them with the map\n" );
		return;
	}

	nearest = Mem_Malloc( r_temppool, rtl.num_lights * sizeof( *nearest ));
	for( int i = 0; i < rtl.num_lights; i++ )
	{
		vec3_t d;

		VectorSubtract( rtl.lights[i].origin, RI.rvp.vieworigin, d );
		nearest[n++] = (vk_rtl_near_t){ i, VectorLength( d ) };
	}

	qsort( nearest, n, sizeof( *nearest ), VK_RTLNearCompare );
	gEngfuncs.Con_Printf( "%d lights (%d styled), nearest the view:\n", rtl.num_lights, rtl.num_styled );

	for( int i = 0; i < n && i < count; i++ )
	{
		const vk_rt_glight_t *l = &rtl.lights[nearest[i].light];

		gEngfuncs.Con_Printf( "%5.0f  %-7s %-20s at %.0f %.0f %.0f  colour %.0f %.0f %.0f  style %d\n", nearest[i].dist, types[l->type],
			rtl.names[nearest[i].light], l->origin[0], l->origin[1], l->origin[2], l->color[0], l->color[1], l->color[2], l->style );
	}

	Mem_Free( nearest );
}

void VK_RTLightsInit( void )
{
	gEngfuncs.Cmd_AddCommand( "r_vk_rtlights", VK_RTLights_f, "the lights of the map nearest the view, for ray traced lighting" );
}

void VK_RTLightsShutdown( void )
{
	VK_RTLightsFree();
	gEngfuncs.Cmd_RemoveCommand( "r_vk_rtlights" );
}

// the grid and lights rt_common.glsl reads, 0 when none
VkDeviceAddress VK_RTLightsAddress( void )
{
	return rtl.address;
}

int VK_RTLightsCount( void )
{
	return rtl.num_lights;
}
