/*
vk_fog.c - GL fog emulation: the GL_FOG state and ref_gl's fog logic
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

#include "vk_local.h"

/*
 * ref_gl fogs with the fixed function GL_FOG state, which its code turns on
 * and off and recolours between passes (gl_rmain.c, gl_rsurf.c, gl_warp.c,
 * gl_triapi.c). That state machine is kept as it is: the glFog calls set
 * the state below, and every draw takes a snapshot of it, an index into a
 * per-frame table the fragment shaders read (shaders/fog.glsl).
 */

CVAR_DEFINE_AUTO( gl_fog, "1", FCVAR_GLCONFIG, "allow for rendering fog using built-in OpenGL fog implementation" );
// detail textures aren't drawn yet, but ref_gl's fog colours count their pass
CVAR_DEFINE_AUTO( r_detailtextures, "1", FCVAR_GLCONFIG, "enable detail textures support" );

#define IsLiquidContents( cnt ) ( cnt == CONTENTS_WATER || cnt == CONTENTS_SLIME || cnt == CONTENTS_LAVA )

typedef struct
{
	float color[4];
	float params[4]; // VK_FOG_*, density, start, end
} vk_fog_entry_t;

static struct
{
	// GL_FOG and glFog
	qboolean enabled;
	int mode;
	vec4_t color;
	float density;
	float start, end;

	qboolean isFogEnabled; // glState.isFogEnabled

	// the table of the frames in flight
	vk_buffer_t buffer;
	int frame;
	int count;    // entries written this frame, 0 is "no fog"
	int current;  // entry of the current state
	qboolean dirty;
} fog;

/*
=================
GL state
=================
*/
static void GL_FogEnable( qboolean enable )
{
	fog.enabled = enable;
}

static void GL_FogMode( int mode )
{
	fog.mode = mode;
	fog.dirty = true;
}

static void GL_FogColor( const float *color )
{
	VectorCopy( color, fog.color );
	fog.color[3] = 1.0f;
	fog.dirty = true;
}

static void GL_FogDensity( float density )
{
	fog.density = density;
	fog.dirty = true;
}

static void GL_FogStartEnd( float start, float end )
{
	fog.start = start;
	fog.end = end;
	fog.dirty = true;
}

/*
=================
VK_FogIndex

the table entry of the current GL fog state, 0 when GL_FOG is off
=================
*/
int VK_FogIndex( void )
{
	if( !fog.enabled || !fog.buffer.alloc.mapped )
		return 0;

	if( fog.dirty || !fog.current )
	{
		// a full table keeps the last state, the frame won't have that many
		if( fog.count < VK_MAX_FOG_STATES )
		{
			vk_fog_entry_t *entry = (vk_fog_entry_t *)fog.buffer.alloc.mapped + fog.frame * VK_MAX_FOG_STATES + fog.count;

			Vector4Copy( fog.color, entry->color );
			entry->params[0] = fog.mode;
			entry->params[1] = fog.density;
			entry->params[2] = fog.start;
			entry->params[3] = fog.end;
			fog.current = fog.count++;
		}

		fog.dirty = false;
	}

	return fog.current;
}

void VK_FogBeginFrame( int frame )
{
	fog.frame = frame;
	fog.count = 1;
	fog.current = 0;
	fog.dirty = true;
}

/*
=================
R_Set2DMode hooks: 2D drawing is never fogged
=================
*/
void VK_FogSet2D( qboolean enable )
{
	if( enable )
		GL_FogEnable( false );
	else if( gl_fog.value && ( RI.fogEnabled || RI.fogCustom ))
		GL_FogEnable( true ); // restore the scene fog that was disabled in 2D mode
}

// glState.isFogEnabled
qboolean VK_FogActive( void )
{
	return fog.isFogEnabled;
}

// R_SetupFrame
void VK_FogSetupFrame( void )
{
	fog.isFogEnabled = fog.enabled;
}

// R_NewMap
void VK_FogNewMap( void )
{
	fog.isFogEnabled = false;
	GL_FogEnable( false );
}

/*
=================
gl_rmain.c
=================
*/
void R_AllowFog( qboolean allowed )
{
	if( allowed )
	{
		if( fog.isFogEnabled && gl_fog.value )
			GL_FogEnable( true );
	}
	else
	{
		if( fog.isFogEnabled )
			GL_FogEnable( false );
	}
}

void R_DrawFog( void )
{
	if( !RI.fogEnabled || !gl_fog.value )
		return;

	GL_FogEnable( true );
	GL_FogMode( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ) ? VK_FOG_EXP2 : VK_FOG_EXP );
	GL_FogDensity( RI.fogDensity );
	GL_FogColor( RI.fogColor );
}

/*
=============
R_RecursiveFindWaterTexture

using to find source waterleaf with
watertexture to grab fog values from it
=============
*/
static int R_RecursiveFindWaterTexture( const mnode_t *node, const mnode_t *ignore, qboolean down )
{
	int tex = 0;

	// ignore solid nodes
	if( node->contents == CONTENTS_SOLID )
		return 0;

	if( node->contents < 0 )
	{
		// ignore non-liquid leaves
		if( node->contents != CONTENTS_WATER && node->contents != CONTENTS_LAVA && node->contents != CONTENTS_SLIME )
			return 0;

		// find texture
		mleaf_t *pleaf = (mleaf_t *)node;
		msurface_t **mark = pleaf->firstmarksurface;
		int c = pleaf->nummarksurfaces;

		for( int i = 0; i < c; i++, mark++ )
		{
			if( (*mark)->flags & SURF_DRAWTURB && (*mark)->texinfo && (*mark)->texinfo->texture )
				return (*mark)->texinfo->texture->gl_texturenum;
		}

		// texture not found
		return 0;
	}

	// this is a regular node
	// traverse children
	mnode_t *child = node_child( node, 0, WORLDMODEL );

	if( child && ( child != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( child, node, true );
		if( tex ) return tex;
	}

	child = node_child( node, 1, WORLDMODEL );

	if( child && ( child != ignore ))
	{
		tex = R_RecursiveFindWaterTexture( child, node, true );
		if( tex ) return tex;
	}

	// for down recursion, return immediately
	if( down ) return 0;

	// texture not found, step up if any
	if( node->parent )
		return R_RecursiveFindWaterTexture( node->parent, node, false );

	// top-level node, bail out
	return 0;
}

/*
=============
R_CheckFog

check for underwater fog
Using backward recursion to find waterline leaf
from underwater leaf (idea: XaeroX)
=============
*/
void R_CheckFog( void )
{
	// quake global fog
	if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
	{
		if( !gp_movevars->fog_settings )
		{
			GL_FogEnable( false );
			RI.fogEnabled = false;
			return;
		}

		// quake-style global fog
		RI.fogColor[0] = ((gp_movevars->fog_settings & 0xFF000000) >> 24) / 255.0f;
		RI.fogColor[1] = ((gp_movevars->fog_settings & 0xFF0000) >> 16) / 255.0f;
		RI.fogColor[2] = ((gp_movevars->fog_settings & 0xFF00) >> 8) / 255.0f;
		RI.fogDensity = ((gp_movevars->fog_settings & 0xFF) / 255.0f) * 0.01f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
		return;
	}

	RI.fogEnabled = false;

	if( FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ) || ENGINE_GET_PARM( PARM_WATER_LEVEL ) < 3 || !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || !RI.viewleaf )
	{
		if( RI.cached_waterlevel == 3 )
		{
			// in some cases waterlevel jumps from 3 to 1. Catch it
			RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );
			RI.cached_contents = CONTENTS_EMPTY;
			if( !RI.fogCustom )
			{
				fog.isFogEnabled = false;
				GL_FogEnable( false );
			}
		}
		return;
	}

	cl_entity_t *ent = gEngfuncs.CL_GetWaterEntity( RI.rvp.vieworigin );
	int cnt;
	if( ent && ent->model && ent->model->type == mod_brush && ent->curstate.skin < 0 )
		cnt = ent->curstate.skin;
	else cnt = RI.viewleaf->contents;

	RI.cached_waterlevel = ENGINE_GET_PARM( PARM_WATER_LEVEL );

	if( !IsLiquidContents( RI.cached_contents ) && IsLiquidContents( cnt ))
	{
		int tex = 0;

		// check for water texture
		if( ent && ent->model && ent->model->type == mod_brush )
		{
			int count = ent->model->nummodelsurfaces;

			msurface_t *surf = &ent->model->surfaces[ent->model->firstmodelsurface];
			for( int i = 0; i < count; i++, surf++ )
			{
				if( surf->flags & SURF_DRAWTURB && surf->texinfo && surf->texinfo->texture )
				{
					tex = surf->texinfo->texture->gl_texturenum;
					RI.cached_contents = ent->curstate.skin;
					break;
				}
			}
		}
		else
		{
			tex = R_RecursiveFindWaterTexture( RI.viewleaf->parent, NULL, false );
			if( tex ) RI.cached_contents = RI.viewleaf->contents;
		}

		if( !tex ) return; // no valid fogs

		// copy fog params
		byte params[4];
		VK_TextureFogParams( tex, &params[0], &params[1], &params[2], &params[3] );
		RI.fogColor[0] = params[0] / 255.0f;
		RI.fogColor[1] = params[1] / 255.0f;
		RI.fogColor[2] = params[2] / 255.0f;
		RI.fogDensity = params[3] * 0.000025f;
		RI.fogStart = RI.fogEnd = 0.0f;
		RI.fogColor[3] = 1.0f;
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
	else
	{
		RI.fogCustom = false;
		RI.fogEnabled = true;
		RI.fogSkybox = true;
	}
}

/*
=================
gl_rsurf.c: the passes after the texture one blend over it, so they get
a fog colour that sums up to RI.fogColor
=================
*/
void GL_SetupFogColorForSurfacesEx( int passes, float density, qboolean blend_lightmaps )
{
	if( !fog.isFogEnabled )
		return;

	if(( passes < 2 ) || ( RI.currententity && RI.currententity->curstate.rendermode == kRenderTransTexture ))
	{
		GL_FogColor( RI.fogColor );
		return;
	}

	float div = passes - 1;
	float factor = passes;
	vec4_t fogColor;
	fogColor[0] = pow( RI.fogColor[0] / div, ( 1.0f / factor ));
	fogColor[1] = pow( RI.fogColor[1] / div, ( 1.0f / factor ));
	fogColor[2] = pow( RI.fogColor[2] / div, ( 1.0f / factor ));
	fogColor[3] = 1.0f; // ignored but GL_FOG_COLOR requires vec4_t

	// because of enabled blending in R_BlendLightmaps, need to scale down fog color
	// but only during lightmap blending & without VBO (it takes another route)
	if( blend_lightmaps && gl_overbright.value )
		VectorScale( fogColor, 0.5f, fogColor );

	GL_FogColor( fogColor );
	GL_FogDensity( RI.fogDensity * density );
}

void GL_SetupFogColorForSurfaces( void )
{
	GL_SetupFogColorForSurfacesEx( r_detailtextures.value ? 3 : 2, 1.0f, false );
}

void GL_ResetFogColor( void )
{
	// restore fog here
	if( fog.isFogEnabled )
		GL_FogColor( RI.fogColor );
}

// gl_warp.c R_DrawSkyBox: the sky gets half the density, or no fog
void VK_FogSkyBegin( void )
{
	// don't fogging skybox (this fix old Half-Life bug)
	if( !RI.fogSkybox ) R_AllowFog( false );

	if( RI.fogEnabled )
		GL_FogDensity( RI.fogDensity * 0.5f );
}

void VK_FogSkyEnd( void )
{
	if( !RI.fogSkybox )
		R_AllowFog( true );

	if( RI.fogEnabled )
		GL_FogDensity( RI.fogDensity );
}

/*
=================
gl_triapi.c
=================
*/
void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	// overrided by internal fog
	if( RI.fogEnabled || !gl_fog.value ) return;
	RI.fogCustom = bOn;

	// check for invalid parms
	if( flEnd <= flStart )
	{
		fog.isFogEnabled = RI.fogCustom = false;
		GL_FogEnable( false );
		return;
	}

	GL_FogEnable( RI.fogCustom );

	// copy fog params
	RI.fogColor[0] = flFogColor[0] / 255.0f;
	RI.fogColor[1] = flFogColor[1] / 255.0f;
	RI.fogColor[2] = flFogColor[2] / 255.0f;
	RI.fogColor[3] = 1.0f;

	RI.fogStart = flStart;
	RI.fogEnd = flEnd;

	if( RI.fogDensity > 0.0f )
	{
		GL_FogMode( VK_FOG_EXP2 );
		GL_FogDensity( RI.fogDensity );
	}
	else
	{
		GL_FogMode( VK_FOG_LINEAR );
		RI.fogSkybox = true;
	}

	GL_FogColor( RI.fogColor );
	GL_FogStartEnd( RI.fogStart, RI.fogEnd );
}

void TriFogParams( float flDensity, int iFogSkybox )
{
	RI.fogDensity = flDensity;
	RI.fogSkybox = iFogSkybox;
}

/*
=================
Lifetime
=================
*/
void VK_RegisterFogCvars( void )
{
	gEngfuncs.Cvar_RegisterVariable( &gl_fog );
	gEngfuncs.Cvar_RegisterVariable( &r_detailtextures );
}

qboolean VK_InitFog( void )
{
	memset( &fog.buffer, 0, sizeof( fog.buffer ));
	fog.mode = VK_FOG_EXP;

	if( !VK_CreateBuffer( &fog.buffer, "fog table", sizeof( vk_fog_entry_t ) * VK_MAX_FOG_STATES * VK_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ))
		return false;

	// entry 0 of every frame: no fog, never read
	memset( fog.buffer.alloc.mapped, 0, sizeof( vk_fog_entry_t ) * VK_MAX_FOG_STATES * VK_FRAMES_IN_FLIGHT );

	for( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ )
		VK_WriteFogDescriptor( i, fog.buffer.buffer, sizeof( vk_fog_entry_t ) * VK_MAX_FOG_STATES * i, sizeof( vk_fog_entry_t ) * VK_MAX_FOG_STATES );

	VK_FogBeginFrame( 0 );
	return true;
}

void VK_ShutdownFog( void )
{
	if( fog.buffer.buffer )
		VK_DestroyBuffer( &fog.buffer );
}
