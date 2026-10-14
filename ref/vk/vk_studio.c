/*
vk_studio.c - studio models: the engine_studio_api_t client DLLs draw through
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
#include "studio.h"
#include "r_studioint.h"

/*
 * Client DLLs (hlsdk-portable, cs16-client) bring their own studio model
 * renderer: it computes bones and animation, then calls back into these
 * functions to light and draw. Same semantics as ref_gl/gl_studio.c.
 */

typedef struct player_model_s
{
	char name[MAX_QPATH];
	char modelname[MAX_QPATH];
	model_t *model;
} player_model_t;

static struct
{
	studiohdr_t *header;
	mstudiobodyparts_t *bodypart;
	mstudiomodel_t *submodel;

	float rotationmatrix[3][4];
	float bonestransform[MAXSTUDIOBONES][3][4];
	float lighttransform[MAXSTUDIOBONES][3][4];

	int framecount;      // studio models drawn, GetModelCounters
	int models_drawn;
	int force_face_flags;

	player_model_t player_models[MAX_CLIENTS];
} g_studio;

static r_studio_interface_t *pStudioDraw;

/*
=================
Engine studio API entries owned by the renderer
=================
*/
static cl_entity_t *pfnGetCurrentEntity( void )
{
	return VK_CurrentEntity();
}

static player_info_t *pfnPlayerInfo( int index )
{
	if( !FBitSet( VK_ViewPass()->flags, RF_DRAW_WORLD ))
		index = -1;

	return gEngfuncs.pfnPlayerInfo( index );
}

static entity_state_t *R_StudioGetPlayerState( int index )
{
	cl_entity_t *e = VK_CurrentEntity();

	if( !FBitSet( VK_ViewPass()->flags, RF_DRAW_WORLD ) && e )
		return &e->curstate;

	return gEngfuncs.pfnGetPlayerState( index );
}

static void pfnGetEngineTimes( int *framecount, double *current, double *old )
{
	if( framecount ) *framecount = VK_RealFrameCount();
	if( current ) *current = gp_cl->time;
	if( old ) *old = gp_cl->oldtime;
}

static void pfnGetViewInfo( float *origin, float *upv, float *rightv, float *forwardv )
{
	vec3_t forward, right, up;

	VK_ViewVectors( forward, right, up );

	if( origin ) VectorCopy( VK_ViewPass()->vieworigin, origin );
	if( forwardv ) VectorCopy( forward, forwardv );
	if( rightv ) VectorCopy( right, rightv );
	if( upv ) VectorCopy( up, upv );
}

static void pfnGetModelCounters( int **s, int **a )
{
	*s = &g_studio.framecount;
	*a = &g_studio.models_drawn;
}

static float ****pfnStudioGetBoneTransform( void )
{
	return (float ****)g_studio.bonestransform;
}

static float ****pfnStudioGetLightTransform( void )
{
	return (float ****)g_studio.lighttransform;
}

static float ***pfnStudioGetRotationMatrix( void )
{
	return (float ***)g_studio.rotationmatrix;
}

static void R_StudioSetupModel( int bodypart, void **ppbodypart, void **ppsubmodel )
{
	cl_entity_t *e = VK_CurrentEntity();
	studiohdr_t *hdr = g_studio.header;

	if( !hdr || !e )
		return;

	if( bodypart > hdr->numbodyparts )
		bodypart = 0;

	g_studio.bodypart = (mstudiobodyparts_t *)((byte *)hdr + hdr->bodypartindex ) + bodypart;

	int index = e->curstate.body / g_studio.bodypart->base;
	index = index % g_studio.bodypart->nummodels;

	g_studio.submodel = (mstudiomodel_t *)((byte *)hdr + g_studio.bodypart->modelindex ) + index;

	if( ppbodypart ) *ppbodypart = g_studio.bodypart;
	if( ppsubmodel ) *ppsubmodel = g_studio.submodel;
}

static int R_StudioCheckBBox( void )
{
	VK_STUB();
	return true; // visible
}

static void pfnStudioDynamicLight( cl_entity_t *ent, alight_t *plight )
{
	VK_STUB();
}

static void R_StudioEntityLight( alight_t *plight )
{
	VK_STUB();
}

static void R_StudioSetupLighting( alight_t *plight )
{
	VK_STUB();
}

static void R_StudioDrawPoints( void )
{
	VK_STUB();
}

static void R_StudioDrawNothing( void )
{
	;
}

static void R_StudioSetupSkin( studiohdr_t *ptexturehdr, int index )
{
	VK_STUB();
}

static void R_StudioSetRemapColors( int top, int bottom )
{
	VK_STUB();
}

static model_t *R_StudioSetupPlayerModel( int index )
{
	player_info_t *info = gEngfuncs.pfnPlayerInfo( index );
	cl_entity_t *e = VK_CurrentEntity();
	player_model_t *state;

	if( index < 0 || index >= gp_cl->maxclients || !e )
		return NULL;

	state = &g_studio.player_models[index];

	// g-cont: force for "dev-mode", non-local games and menu preview
	if(( gpGlobals->developer || !ENGINE_GET_PARM( PARM_SINGLEPLAYER_GAME ) || !FBitSet( VK_ViewPass()->flags, RF_DRAW_WORLD )) && info->model[0] )
	{
		if( Q_strcmp( state->name, info->model ))
		{
			Q_strncpy( state->name, info->model, sizeof( state->name ));
			Q_snprintf( state->modelname, sizeof( state->modelname ), "models/player/%s/%s.mdl", info->model, info->model );

			if( gEngfuncs.fsapi->FileExists( state->modelname, false ))
				state->model = gEngfuncs.Mod_ForName( state->modelname, false, true );
			else
				state->model = NULL;

			if( !state->model )
				state->model = e->model;
		}
	}
	else
	{
		if( state->model != e->model )
			state->model = e->model;
		state->name[0] = 0;
	}

	return state->model;
}

void R_StudioResetPlayerModels( void )
{
	memset( g_studio.player_models, 0, sizeof( g_studio.player_models ));
}

static void R_StudioClientEvents( void )
{
	VK_STUB(); // animation events (muzzle flashes, shells): story 017
}

static int R_StudioGetForceFaceFlags( void )
{
	return g_studio.force_face_flags;
}

static void R_StudioSetForceFaceFlags( int flags )
{
	g_studio.force_face_flags = flags;
}

static void R_StudioSetHeader( studiohdr_t *header )
{
	g_studio.header = header;
}

static void R_StudioSetRenderModel( model_t *model )
{
	VK_SetCurrentModel( model );
}

static void R_StudioSetupRenderer( int rendermode )
{
	VK_STUB();
}

static void R_StudioRestoreRenderer( void )
{
	VK_STUB();
}

static void GL_StudioSetRenderMode( int mode )
{
	VK_STUB();
}

static void R_StudioSetRenderamt( int iRenderamt )
{
	cl_entity_t *e = VK_CurrentEntity();

	if( !e )
		return;

	e->curstate.renderamt = iRenderamt;
}

static void R_StudioSetCullState( int iCull )
{
	VK_STUB();
}

static void R_StudioRenderShadow( int iSprite, float *p1, float *p2, float *p3, float *p4 )
{
	VK_STUB();
}

// default renderer when the client DLL doesn't bring one
static int R_StudioDrawModel( int flags )
{
	VK_STUB();
	return 0;
}

static int R_StudioDrawPlayer( int flags, entity_state_t *pplayer )
{
	VK_STUB();
	return 0;
}

qboolean R_StudioFillAPI( engine_studio_api_t *api, r_studio_interface_t *pDefaultDraw )
{
	api->GetCurrentEntity        = pfnGetCurrentEntity;
	api->PlayerInfo              = pfnPlayerInfo;
	api->GetPlayerState          = R_StudioGetPlayerState;
	api->GetTimes                = pfnGetEngineTimes;
	api->GetViewInfo             = pfnGetViewInfo;
	api->GetModelCounters        = pfnGetModelCounters;
	api->StudioGetBoneTransform  = pfnStudioGetBoneTransform;
	api->StudioGetLightTransform = pfnStudioGetLightTransform;
	api->StudioGetRotationMatrix = pfnStudioGetRotationMatrix;
	api->StudioSetupModel        = R_StudioSetupModel;
	api->StudioCheckBBox         = R_StudioCheckBBox;
	api->StudioDynamicLight      = pfnStudioDynamicLight;
	api->StudioEntityLight       = R_StudioEntityLight;
	api->StudioSetupLighting     = R_StudioSetupLighting;
	api->StudioDrawPoints        = R_StudioDrawPoints;
	api->StudioDrawHulls         = R_StudioDrawNothing;
	api->StudioDrawAbsBBox       = R_StudioDrawNothing;
	api->StudioDrawBones         = R_StudioDrawNothing;
	api->StudioSetupSkin         = (void *)R_StudioSetupSkin;
	api->StudioSetRemapColors    = R_StudioSetRemapColors;
	api->SetupPlayerModel        = R_StudioSetupPlayerModel;
	api->StudioClientEvents      = R_StudioClientEvents;
	api->GetForceFaceFlags       = R_StudioGetForceFaceFlags;
	api->SetForceFaceFlags       = R_StudioSetForceFaceFlags;
	api->StudioSetHeader         = (void *)R_StudioSetHeader;
	api->SetRenderModel          = R_StudioSetRenderModel;
	api->SetupRenderer           = R_StudioSetupRenderer;
	api->RestoreRenderer         = R_StudioRestoreRenderer;
	api->SetChromeOrigin         = R_StudioDrawNothing;
	api->GL_StudioDrawShadow     = R_StudioDrawNothing;
	api->GL_SetRenderMode        = GL_StudioSetRenderMode;
	api->StudioSetRenderamt      = R_StudioSetRenderamt;
	api->StudioSetCullState      = R_StudioSetCullState;
	api->StudioRenderShadow      = R_StudioRenderShadow;

	pDefaultDraw->version          = STUDIO_INTERFACE_VERSION;
	pDefaultDraw->StudioDrawModel  = R_StudioDrawModel;
	pDefaultDraw->StudioDrawPlayer = R_StudioDrawPlayer;

	return true;
}

void R_StudioSetDrawInterface( r_studio_interface_t *pDraw )
{
	pStudioDraw = pDraw;
}

/*
=================
R_DrawStudioModel

the client renderer animates and draws, calling back into the API above
=================
*/
void R_DrawStudioModel( cl_entity_t *e )
{
	if( !pStudioDraw || FBitSet( VK_ViewPass()->flags, RF_DRAW_CUBEMAP ))
		return;

	g_studio.framecount++;

	if( e->player )
		pStudioDraw->StudioDrawPlayer( STUDIO_RENDER|STUDIO_EVENTS, R_StudioGetPlayerState( e->index - 1 ));
	else if( e->curstate.movetype != MOVETYPE_FOLLOW ) // attachments follow their parent, story 017
		pStudioDraw->StudioDrawModel( STUDIO_RENDER|STUDIO_EVENTS );
}
