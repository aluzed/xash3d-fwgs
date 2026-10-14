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

/*
=================
Lifetime
=================
*/
static qboolean R_Init( void )
{
	gEngfuncs.Cvar_RegisterVariable( &r_vk_log_stubs );

	r_temppool = Mem_AllocPool( "ref_vk temp" );

	// the engine falls back to the next renderer when this fails
	if( !gEngfuncs.R_Init_Video( REF_VULKAN ))
	{
		Mem_FreePool( &r_temppool );
		return false;
	}

	VK_InitTextures();

	if( !VK_InitCore( ))
	{
		gEngfuncs.R_Free_Video();
		Mem_FreePool( &r_temppool );
		return false;
	}

	return true;
}

static void R_Shutdown( void )
{
	VK_ShutdownTextures();
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
	case PARM_GL_CONTEXT_TYPE:
		// no GL context: RenderAPI users must not issue GL calls
		return CONTEXT_TYPE_SOFTWARE;
	default:
		VK_STUB();
		return 0;
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

static void R_SimpleStubInt( int unused )
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
	VK_STUB();
}

static void R_RenderScene( void )
{
	VK_STUB();
}

static void R_EndFrame( void )
{
	VK_STUB();
}

static void R_ClearScreen( void )
{
	VK_STUB();
}

static void R_GammaChanged( qboolean do_reset_gamma )
{
	VK_STUB();
}

static qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	VK_STUB();
	return true;
}

static void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	;
}

static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	VK_STUB();
	return NULL;
}

static void GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	VK_STUB();
}

static void R_SetupSky( int *skytextures )
{
	VK_STUB();
}

static void R_Set2DMode( qboolean enable )
{
	VK_STUB();
}

static void R_Set2DOffset( float x, float y )
{
	VK_STUB();
}

static void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	VK_STUB();
}

static void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a )
{
	VK_STUB();
}

static int WorldToScreen( const vec3_t world, vec3_t screen )
{
	VK_STUB();
	VectorClear( screen );
	return 0;
}

static qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	VK_STUB();
	return false;
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

static void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	VK_STUB();
}

static void GL_SubdivideSurface( model_t *mod, msurface_t *fa )
{
	VK_STUB();
}

static qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buffer, size_t buffersize )
{
	VK_STUB();
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

static void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	if( xScale ) *xScale = 1.0f;
	if( yScale ) *yScale = 1.0f;
}

static void R_SetDetailScaleForTexture( int texture, float xScale, float yScale )
{
	VK_STUB();
}

static const byte *GL_TextureData( unsigned int texnum )
{
	VK_STUB();
	return NULL;
}

static void R_OverrideTextureSourceSize( unsigned int texnum, unsigned int srcWidth, unsigned int srcHeight )
{
	VK_STUB();
}

static void GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt )
{
	VK_STUB();
}

static void GL_Bind( int tmu, unsigned int texnum )
{
	VK_STUB();
}

static void GL_RenderFrame( const struct ref_viewpass_s *rvp )
{
	VK_STUB();
}

static void GL_OrthoBounds( const float *mins, const float *maxs )
{
	VK_STUB();
}

static qboolean R_SpeedsMessage( char *out, size_t size )
{
	return false;
}

static byte *Mod_GetCurrentVis( void )
{
	VK_STUB();
	return NULL;
}

static void R_NewMap( void )
{
	VK_STUB();
}

static void R_ClearScene( void )
{
	VK_STUB();
}

static void TriRenderMode( int mode )
{
	VK_STUB();
}

static void TriBegin( int primitive )
{
	VK_STUB();
}

static void TriEnd( void )
{
	VK_STUB();
}

static void Color4f( float r, float g, float b, float a )
{
	VK_STUB();
}

static void Color4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	VK_STUB();
}

static void Vertex3fv( const float *worldPnt )
{
	VK_STUB();
}

static void Vertex3f( float x, float y, float z )
{
	VK_STUB();
}

static void CullFace( TRICULLSTYLE mode )
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

static void TriTexCoord2f( float u, float v )
{
	VK_STUB();
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

static void VGUI_SetupDrawing( qboolean rect )
{
	VK_STUB();
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
	.GL_SetRenderMode = R_SimpleStubInt,

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
	.Color4f       = Color4f,
	.Color4ub      = Color4ub,
	.Vertex3fv     = Vertex3fv,
	.Vertex3f      = Vertex3f,
	.CullFace      = CullFace,

	.R_FillRenderAPI = R_FillRenderAPI,
	.R_FillTriAPI    = R_FillTriAPI,

	.VGUI_SetupDrawing = VGUI_SetupDrawing,
};
