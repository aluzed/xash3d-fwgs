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
	VK_InitMaterials();
	VK_InitStream();
	VK_InitImm();
	VK_InitWorld();
	R_StudioInit();
	R_SpriteInit();
	R_DecalInit();
	VK_RegisterFogCvars();
	VK_RegisterProfileCvars();
	VK_RegisterPostCvars();
	VK_RegisterStreamCvars();
	VK_RegisterRTCvars();
	R_ClearDecals();

	// the engine falls back to the next renderer when this fails
	if( !gEngfuncs.R_Init_Video( REF_VULKAN ))
	{
		VK_ShutdownTextures();
		Mem_FreePool( &r_temppool );
		return false;
	}

	if( !VK_InitCore( ) || !VK_InitMemory( ) || !VK_InitFrames( ) || !VK_InitPipelines( ) || !VK_InitFog( ) || !VK_InitPost( ))
	{
		VK_ShutdownTextures();
		VK_ShutdownPost();
		VK_ShutdownFog();
		VK_ShutdownPipelines();
		VK_ShutdownFrames();
		VK_ShutdownMemory();
		VK_ShutdownCore();
		gEngfuncs.R_Free_Video();
		Mem_FreePool( &r_temppool );
		return false;
	}

	// DDS and KTX2 files keep their BC blocks, uploaded as they are
	if( vk.caps.texture_compression_bc )
		gEngfuncs.Image_AddCmdFlags( IL_DDS_HARDWARE );

	VK_InitProfile();
	VK_InitRT();
	VK_CreateInitialSwapchain();

	return true;
}

static void R_Shutdown( void )
{
	gEngfuncs.Job_WaitIdle(); // jobs fill renderer data
	VK_ShutdownProfile();
	VK_ShutdownWorld();
	VK_ShutdownMaterials();
	VK_ShutdownStream();
	VK_ShutdownPost();
	VK_ShutdownRT();
	VK_ShutdownGraph();
	VK_ShutdownTextures();
	VK_ShutdownFog();
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

static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	return VK_ScreenShot( filename, shot_type );
}

static qboolean VID_CubemapShot( const char *base, uint size, const float *vieworg, qboolean skyshot )
{
	return VK_CubemapShot( base, size, vieworg, skyshot );
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

/*
=================
r_speeds (ref_gl/gl_backend.c)
=================
*/
ref_speeds_t r_stats;
static char r_speeds_msg[MAX_SYSPATH];

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	if( gEngfuncs.drawFuncs->R_SpeedsMessage != NULL )
	{
		if( gEngfuncs.drawFuncs->R_SpeedsMessage( out, size ))
			return true;
		// otherwise pass to default handler
	}

	if( r_speeds->value <= 0 ) return false;
	if( !out || !size ) return false;

	Q_strncpy( out, r_speeds_msg, size );

	return true;
}

static void GL_BackendStartFrame( void )
{
	r_speeds_msg[0] = '\0';
}

static void GL_BackendEndFrame( void )
{
	if( r_speeds->value <= 0 || !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || !WORLDMODEL )
		return;

	mleaf_t *curleaf = RI.viewleaf ? RI.viewleaf : WORLDMODEL->leafs;

	switch( (int)r_speeds->value )
	{
	case 1:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i wpoly, %3i apoly\n%3i epoly, %3i spoly",
			r_stats.c_world_polys, r_stats.c_alias_polys, r_stats.c_studio_polys, r_stats.c_sprite_polys );
		break;
	case 2:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ),
			"Renderer: ^1Vulkan^7\n\n"
			"visible leafs:\n%3i leafs\ncurrent leaf %3i\n"
			"ReciusiveWorldNode: %3lf secs\nDrawTextureChains %lf",
			r_stats.c_world_leafs, (int)( curleaf - WORLDMODEL->leafs ), r_stats.t_world_node, r_stats.t_world_draw );
		break;
	case 3:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i alias models drawn\n%3i studio models drawn\n%3i sprites drawn",
			r_stats.c_alias_models_drawn, r_stats.c_studio_models_drawn, r_stats.c_sprite_models_drawn );
		break;
	case 4:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i static entities\n%3i normal entities\n%3i server entities",
			r_stats.c_client_ents, tr.draw_list->num_solid_entities + tr.draw_list->num_trans_entities - r_stats.c_client_ents,
			(int)ENGINE_GET_PARM( PARM_NUMENTITIES ));
		break;
	case 5:
		Q_snprintf( r_speeds_msg, sizeof( r_speeds_msg ), "%3i tempents\n%3i viewbeams\n%3i particles",
			r_stats.c_active_tents_count, r_stats.c_view_beams_count, r_stats.c_particle_count );
		break;
	}

	memset( &r_stats, 0, sizeof( r_stats ));
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
	VK_TextureFogParams( texture, red, green, blue, alpha );
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
	api->DrawSingleDecal         = DrawSingleDecal;
	api->R_DecalSetupVerts       = R_DecalSetupVerts;
	api->R_EntityRemoveDecals    = R_EntityRemoveDecals;
	api->GL_SelectTexture        = RAPI_GLStubInt;
	api->GL_LoadTextureMatrix    = RAPI_GLStubMatrix;
	api->GL_TexMatrixIdentity    = R_SimpleStub;
	api->GL_CleanUpTextureUnits  = RAPI_GLStubInt;
	api->GL_TexGen               = RAPI_GLStubUInt2;
	api->GL_TextureTarget        = RAPI_GLStubUInt;
	api->GL_TexCoordArrayMode    = RAPI_GLStubUInt;
	api->GL_UpdateTexSize        = RAPI_UpdateTexSize;
	api->GL_DrawParticles        = CL_DrawParticlesExternal;
	api->LightVec                = R_LightVec; // ref_common
	api->StudioGetTexture        = RAPI_StudioGetTexture;
	api->GL_GetProcAddress       = RAPI_GetProcAddress;
}

static void TriScreenToWorld( const float *screen, float *world )
{
	R_ScreenToWorld( screen, world );
}

// glGetFloatv of the scene matrices, the ones the client sees from its
// triangle callbacks
static void TriGetMatrix( const int pname, float *matrix )
{
	switch( pname )
	{
	case 0x0BA6: // GL_MODELVIEW_MATRIX
		Matrix4x4_ToArrayFloatGL( RI.worldviewMatrix, matrix );
		break;
	case 0x0BA7: // GL_PROJECTION_MATRIX
		Matrix4x4_ToArrayFloatGL( RI.projectionMatrix, matrix );
		break;
	default:
		VK_STUB();
		memset( matrix, 0, 16 * sizeof( *matrix ));
		matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
		break;
	}
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
	.R_PushScene          = R_PushScene,
	.R_PopScene           = R_PopScene,
	.GL_BackendStartFrame = GL_BackendStartFrame,
	.GL_BackendEndFrame   = GL_BackendEndFrame,

	.R_ClearScreen    = R_ClearScreen,
	.R_AllowFog       = R_AllowFog,
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
	.WorldToScreen    = R_WorldToScreen,

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
