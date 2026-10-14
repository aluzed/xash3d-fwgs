/*
vk_main.c - Vulkan renderer interface
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

CVAR_DEFINE_AUTO( r_vk_log_stubs, "0", 0, "log every unimplemented ref_vk call once" );
CVAR_DEFINE_AUTO( r_vk_validation, "0", FCVAR_GLCONFIG, "enable the Vulkan validation layer (restart the renderer), -vkvalidation does the same" );
CVAR_DEFINE_AUTO( r_vk_device, "-1", FCVAR_GLCONFIG, "physical device index to use, -1 picks the best one (restart the renderer)" );
CVAR_DEFINE_AUTO( r_vk_dynamic_mb, "8", FCVAR_GLCONFIG, "size of each per-frame dynamic vertex ring, grows on overflow" );

/*
=================
Lifetime
=================
*/
static qboolean R_Init( void )
{
	// first, like ref_gl: random tiling textures pick their variant from it
	GL_InitRandomTable();

	gEngfuncs.Cvar_RegisterVariable( &r_vk_log_stubs );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_validation );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_device );
	gEngfuncs.Cvar_RegisterVariable( &r_vk_dynamic_mb );

	r_temppool = Mem_AllocPool( "ref_vk temp" );

	// every cvar before R_Init_Video: it executes vulkan.cfg, which would
	// otherwise create them as plain user cvars and break the registration
	VK_InitTextures();
	VK_InitImm();
	VK_InitWorld();

	// the engine falls back to the next renderer when this fails
	if( !gEngfuncs.R_Init_Video( REF_VULKAN ))
	{
		VK_ShutdownTextures();
		Mem_FreePool( &r_temppool );
		return false;
	}

	if( !VK_InitCore( ) || !VK_InitMemory( ) || !VK_InitFrames( ) || !VK_InitPipelines( ))
	{
		VK_ShutdownTextures();
		VK_ShutdownPipelines();
		VK_ShutdownFrames();
		VK_ShutdownMemory();
		VK_ShutdownCore();
		gEngfuncs.R_Free_Video();
		Mem_FreePool( &r_temppool );
		return false;
	}

	return true;
}

static void R_Shutdown( void )
{
	VK_ShutdownWorld();
	VK_ShutdownTextures();
	VK_ShutdownPipelines();
	VK_ShutdownFrames();
	VK_ShutdownMemory();
	VK_ShutdownCore();
	gEngfuncs.R_Free_Video();
	Mem_FreePool( &r_temppool );
}

static const char *R_GetConfigName( void )
{
	return "vulkan"; // FCVAR_GLCONFIG cvars go to vulkan.cfg
}

static intptr_t RefGetParm( int parm, int arg )
{
	switch( parm )
	{
	case PARM_TEX_WIDTH:
	case PARM_TEX_HEIGHT:
	case PARM_TEX_SRC_WIDTH:
	case PARM_TEX_SRC_HEIGHT:
	case PARM_TEX_ENCODE:
	case PARM_TEX_MIPCOUNT:
	case PARM_TEX_DEPTH:
	case PARM_TEX_TEXNUM:
	case PARM_TEX_FLAGS:
	case PARM_TEX_MEMORY:
	case PARM_TEX_FILTERING:
		return VK_TextureParm( parm, arg );
	case PARM_LIGHTSTYLEVALUE:
		return g_lightstylevalue[bound( 0, arg, MAX_LIGHTSTYLES - 1 )];
	case PARM_MAX_IMAGE_UNITS:
		return 1;
	case PARM_GL_CONTEXT_TYPE:
		// no GL context: RenderAPI users must not issue GL calls
		return CONTEXT_TYPE_SOFTWARE;
	case PARM_TEX_SKYBOX:
	case PARM_TEX_SKYTEXNUM:
		return VK_SkyParm( parm, arg );
	case PARM_TEX_GLFORMAT:
	case PARM_TEX_TARGET:
	case PARM_TEX_LIGHTMAP:
	case PARM_ACTIVE_TMU:
	case PARM_REBUILD_GAMMA:
	case PARM_GLES_WRAPPER:
	case PARM_STENCIL_ACTIVE:
	case PARM_GET_STUDIO_HDR:
		VK_STUB();
		return 0;
	default:
		return ENGINE_GET_PARM_( parm, arg ); // engine side parameters
	}
}

/*
=================
Stubs, replaced story by story (see docs/stories)
=================
*/
static void R_SimpleStub( void )
{
	;
}

static void R_SimpleStubBool( qboolean unused )
{
	;
}

static qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int x, int y, float unused1_default_at_1, float unused2_default_at_1 )
{
	return true;
}

static void GL_SetupAttributes( int safegl )
{
	;
}

static void R_BeginFrame( qboolean clearScene )
{
	VK_WorldBeginFrame();
	VK_BeginFrame( clearScene );
}

static void R_RenderScene( void )
{
	VK_STUB();
}

static void R_EndFrame( void )
{
	VK_EndFrame();
}

static void R_ClearScreen( void )
{
	VK_ClearScreen();
}

static qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	VK_STUB();
	return true;
}

static int WorldToScreen( const vec3_t world, vec3_t screen )
{
	VK_STUB();
	VectorClear( screen );
	return 0;
}

static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	return VK_ScreenShot( filename, shot_type );
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	VK_STUB();
	return false;
}

static void R_DecalShoot( int textureIndex, int entityIndex, int modelIndex, vec3_t pos, int flags, float scale )
{
	VK_STUB();
}

static void R_DecalRemoveAll( int texture )
{
	VK_STUB();
}

static int R_CreateDecalList( struct decallist_s *pList )
{
	VK_STUB();
	return 0;
}

static void R_ClearAllDecals( qboolean include_permanent )
{
	VK_STUB();
}

static float R_StudioEstimateFrame( cl_entity_t *e, mstudioseqdesc_t *pseqdesc, double time )
{
	VK_STUB();
	return 0.0f;
}

static void R_StudioLerpMovement( cl_entity_t *e, double time, vec3_t origin, vec3_t angles )
{
	VK_STUB();
}

// no studio interface yet: the engine then skips the client DLL negotiation,
// so client DLLs replacing the studio renderer (CS does) never call into us
static qboolean R_StudioFillAPI( struct engine_studio_api_s *api, struct r_studio_interface_s *pDefaultDraw )
{
	VK_STUB();
	return false;
}

static void R_StudioSetDrawInterface( struct r_studio_interface_s *pDraw )
{
	VK_STUB();
}

/*
=================
Model lifetime, as ref_gl/gl_context.c: the engine loads brush textures
through GL_LoadTexture, the renderer frees them with the model
=================
*/
static qboolean Mod_LooksLikeWaterTexture( const char *name )
{
	if(( name[0] == '*' && Q_stricmp( name, REF_DEFAULT_TEXTURE )) || name[0] == '!' )
		return true;

	if( !FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
	{
		if( !Q_strnicmp( name, "water", 5 ) || !Q_strnicmp( name, "laser", 5 ))
			return true;
	}

	return false;
}

static void Mod_BrushUnloadTextures( model_t *mod )
{
	const int default_texture = GL_FindTexture( REF_DEFAULT_TEXTURE );

	for( int i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];

		if( !tx )
			continue; // free slot

		if( tx->gl_texturenum != default_texture )
			GL_FreeTexture( tx->gl_texturenum ); // main texture

		if( !Mod_LooksLikeWaterTexture( tx->name ))
		{
			GL_FreeTexture( tx->fb_texturenum ); // luma texture
			GL_FreeTexture( tx->dt_texturenum ); // detail texture
		}
	}
}

static void Mod_StudioUnloadTextures( void *data )
{
	const studiohdr_t *phdr = data;
	const int default_texture = GL_FindTexture( REF_DEFAULT_TEXTURE );
	const mstudiotexture_t *ptexture;

	if( !phdr )
		return;

	ptexture = (const mstudiotexture_t *)(((const byte *)phdr) + phdr->textureindex );

	for( int i = 0; i < phdr->numtextures; i++ )
	{
		if( ptexture[i].index != default_texture )
			GL_FreeTexture( ptexture[i].index );
	}
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf, size_t buffersize )
{
	if( !create )
	{
		if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
			gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, false, buf );

		switch( mod->type )
		{
		case mod_studio:
			Mod_StudioUnloadTextures( mod->cache.data );
			break;
		case mod_brush:
			Mod_BrushUnloadTextures( mod );
			break;
		default:
			break; // sprites keep nothing, alias models are Quake only
		}
		return true;
	}

	switch( mod->type )
	{
	case mod_studio:
	case mod_brush:
	case mod_sprite:
		break;
	case mod_alias:
		VK_STUB(); // Quake models, not part of the HL / CS scope
		return false;
	default:
		gEngfuncs.Host_Error( "%s: unsupported type %d\n", __func__, mod->type );
		return false;
	}

	if( gEngfuncs.drawFuncs->Mod_ProcessUserData )
		gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, true, buf );

	return true;
}

static void Mod_StudioLoadTextures( model_t *mod, void *data )
{
	VK_STUB();
}

static void CL_DrawParticles( double frametime, particle_t *particles, float partsize )
{
	VK_STUB();
}

static void CL_DrawTracers( double frametime, particle_t *tracers )
{
	VK_STUB();
}

static void CL_DrawBeams( int fTrans, BEAM *beams )
{
	VK_STUB();
}

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

static void R_ClearScene( void )
{
	VK_STUB();
}

/*
=================
RenderAPI and TriAPI entries owned by the renderer. Client DLLs call them
without checking for NULL (cs16-client does during HUD init), so every
entry gets a safe stub
=================
*/
static void RAPI_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *alpha )
{
	VK_STUB();
	if( red ) *red = 255;
	if( green ) *green = 255;
	if( blue ) *blue = 255;
	if( alpha ) *alpha = 255;
}

static float RAPI_GetFrameTime( void )
{
	return gp_cl->time - gp_cl->oldtime;
}

static void RAPI_SetCurrentEntity( struct cl_entity_s *ent )
{
	VK_STUB();
}

static void RAPI_SetCurrentModel( struct model_s *mod )
{
	VK_STUB();
}

static int RAPI_LoadTextureArray( const char **names, int flags )
{
	VK_STUB();
	return 0;
}

static int RAPI_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags )
{
	VK_STUB();
	return 0;
}

static void RAPI_DrawSingleDecal( struct decal_s *pDecal, struct msurface_s *fa )
{
	VK_STUB();
}

static float *RAPI_DecalSetupVerts( struct decal_s *pDecal, struct msurface_s *surf, int texture, int *outCount )
{
	VK_STUB();
	if( outCount ) *outCount = 0;
	return NULL;
}

static void RAPI_EntityRemoveDecals( struct model_s *mod )
{
	VK_STUB();
}

// fixed function GL state has no meaning here
static void RAPI_GLStubInt( int unused )
{
	;
}

static void RAPI_GLStubMatrix( const float *unused )
{
	;
}

static void RAPI_GLStubUInt( unsigned int unused )
{
	;
}

static void RAPI_GLStubUInt2( unsigned int unused1, unsigned int unused2 )
{
	;
}

static void RAPI_UpdateTexSize( int texnum, int width, int height, int depth )
{
	;
}

static void RAPI_DrawParticles( const struct ref_viewpass_s *rvp, qboolean trans_pass, float frametime )
{
	VK_STUB();
}

static struct mstudiotex_s *RAPI_StudioGetTexture( struct cl_entity_s *e )
{
	VK_STUB();
	return NULL;
}

static void *RAPI_GetProcAddress( const char *name )
{
	return NULL; // there are no GL entry points to give
}

static void R_FillRenderAPI( render_api_t *api )
{
	api->GetExtraParmsForTexture = RAPI_GetExtraParmsForTexture;
	api->GetFrameTime            = RAPI_GetFrameTime;
	api->R_SetCurrentEntity      = RAPI_SetCurrentEntity;
	api->R_SetCurrentModel       = RAPI_SetCurrentModel;
	api->GL_CreateTexture        = GL_CreateTexture;
	api->GL_LoadTextureArray     = RAPI_LoadTextureArray;
	api->GL_CreateTextureArray   = RAPI_CreateTextureArray;
	api->DrawSingleDecal         = RAPI_DrawSingleDecal;
	api->R_DecalSetupVerts       = RAPI_DecalSetupVerts;
	api->R_EntityRemoveDecals    = RAPI_EntityRemoveDecals;
	api->GL_SelectTexture        = RAPI_GLStubInt;
	api->GL_LoadTextureMatrix    = RAPI_GLStubMatrix;
	api->GL_TexMatrixIdentity    = R_SimpleStub;
	api->GL_CleanUpTextureUnits  = RAPI_GLStubInt;
	api->GL_TexGen               = RAPI_GLStubUInt2;
	api->GL_TextureTarget        = RAPI_GLStubUInt;
	api->GL_TexCoordArrayMode    = RAPI_GLStubUInt;
	api->GL_UpdateTexSize        = RAPI_UpdateTexSize;
	api->GL_DrawParticles        = RAPI_DrawParticles;
	api->LightVec                = R_LightVec; // ref_common
	api->StudioGetTexture        = RAPI_StudioGetTexture;
	api->GL_GetProcAddress       = RAPI_GetProcAddress;
}

static void TriFog( float flFogColor[3], float flStart, float flEnd, int bOn )
{
	VK_STUB();
}

static void TriScreenToWorld( const float *screen, float *world )
{
	VK_STUB();
	VectorClear( world );
}

static void TriGetMatrix( const int pname, float *matrix )
{
	VK_STUB();
	memset( matrix, 0, 16 * sizeof( *matrix ));
	matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void TriFogParams( float flDensity, int iFogSkybox )
{
	VK_STUB();
}

static void R_FillTriAPI( triangleapi_t *api )
{
	api->TexCoord2f    = TriTexCoord2f;
	api->Fog           = TriFog;
	api->ScreenToWorld = TriScreenToWorld;
	api->GetMatrix     = TriGetMatrix;
	api->FogParams     = TriFogParams;
}

const ref_interface_t gReffuncs =
{
	.R_Init                = R_Init,
	.R_Shutdown            = R_Shutdown,
	.R_GetConfigName       = R_GetConfigName,
	.R_SetDisplayTransform = R_SetDisplayTransform,

	.GL_SetupAttributes = GL_SetupAttributes,
	.GL_InitExtensions  = R_SimpleStub,
	.GL_ClearExtensions = R_SimpleStub,

	.R_GammaChanged       = R_GammaChanged,
	.R_BeginFrame         = R_BeginFrame,
	.R_RenderScene        = R_RenderScene,
	.R_EndFrame           = R_EndFrame,
	.R_PushScene          = R_SimpleStub,
	.R_PopScene           = R_SimpleStub,
	.GL_BackendStartFrame = R_SimpleStub,
	.GL_BackendEndFrame   = R_SimpleStub,

	.R_ClearScreen    = R_ClearScreen,
	.R_AllowFog       = R_SimpleStubBool,
	.GL_SetRenderMode = GL_SetRenderMode,

	.R_AddEntity      = R_AddEntity,
	.R_ProcessEntData = R_ProcessEntData,

	.R_ShowTextures = R_SimpleStub,

	.R_GetTextureOriginalBuffer = R_GetTextureOriginalBuffer,
	.GL_LoadTextureFromBuffer   = GL_LoadTextureFromBuffer,
	.GL_ProcessTexture          = GL_ProcessTexture,
	.R_SetupSky                 = R_SetupSky,

	.R_Set2DMode      = R_Set2DMode,
	.R_Set2DOffset    = R_Set2DOffset,
	.R_DrawStretchPic = R_DrawStretchPic,
	.FillRGBA         = FillRGBA,
	.WorldToScreen    = WorldToScreen,

	.VID_ScreenShot  = VID_ScreenShot,
	.VID_CubemapShot = VID_CubemapShot,

	.R_LightPoint = R_LightPoint, // ref_common

	.R_DecalShoot      = R_DecalShoot,
	.R_DecalRemoveAll  = R_DecalRemoveAll,
	.R_CreateDecalList = R_CreateDecalList,
	.R_ClearAllDecals  = R_ClearAllDecals,

	.R_StudioEstimateFrame    = R_StudioEstimateFrame,
	.R_StudioLerpMovement     = R_StudioLerpMovement,
	.R_StudioFillAPI          = R_StudioFillAPI,
	.R_StudioSetDrawInterface = R_StudioSetDrawInterface,

	.R_SetSkyCloudsTextures = R_SetSkyCloudsTextures,
	.GL_SubdivideSurface    = GL_SubdivideSurface,
	.CL_RunLightStyles      = CL_RunLightStyles, // ref_common

	.Mod_ProcessRenderData  = Mod_ProcessRenderData,
	.Mod_StudioLoadTextures = Mod_StudioLoadTextures,

	.CL_DrawParticles = CL_DrawParticles,
	.CL_DrawTracers   = CL_DrawTracers,
	.CL_DrawBeams     = CL_DrawBeams,

	.RefGetParm = RefGetParm,

	.R_GetDetailScaleForTexture = R_GetDetailScaleForTexture,
	.R_SetDetailScaleForTexture = R_SetDetailScaleForTexture,

	.GL_CreateTexture = GL_CreateTexture,
	.GL_FindTexture   = GL_FindTexture,
	.GL_TextureName   = GL_TextureName,
	.GL_TextureData   = GL_TextureData,
	.GL_LoadTexture   = GL_LoadTexture,
	.GL_FreeTexture   = GL_FreeTexture,
	.R_OverrideTextureSourceSize = R_OverrideTextureSourceSize,

	.GL_UpdateTexture = GL_UpdateTexture,

	.GL_Bind = GL_Bind,

	.GL_RenderFrame    = GL_RenderFrame,
	.GL_OrthoBounds    = GL_OrthoBounds,
	.R_SpeedsMessage   = R_SpeedsMessage,
	.Mod_GetCurrentVis = Mod_GetCurrentVis,
	.R_NewMap          = R_NewMap,
	.R_ClearScene      = R_ClearScene,

	.TriRenderMode = TriRenderMode,
	.Begin         = TriBegin,
	.End           = TriEnd,
	.Color4f       = TriColor4f,
	.Color4ub      = TriColor4ub,
	.Vertex3fv     = TriVertex3fv,
	.Vertex3f      = TriVertex3f,
	.CullFace      = TriCullFace,

	.R_FillRenderAPI = R_FillRenderAPI,
	.R_FillTriAPI    = R_FillTriAPI,

	.VGUI_SetupDrawing = VGUI_SetupDrawing,
};
