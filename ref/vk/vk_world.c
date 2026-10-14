/*
vk_world.c - BSP world: geometry, lightmaps, visibility and drawing
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
#include "entity_types.h"

/*
 * Ported from ref_gl (gl_rsurf.c, gl_rmain.c, gl_cull.c, gl_frustum.c) so
 * the same surfaces are visible with the same polygons, texture and
 * lightmap coordinates and lightmap texels. Only the drawing differs:
 * polygons live in one static GPU buffer, and the texture and lightmap
 * passes of ref_gl are a single shader pass (world.frag).
 */

CVAR_DEFINE_AUTO( gl_overbright, "1", FCVAR_GLCONFIG, "overbrights" );
CVAR_DEFINE_AUTO( gl_keeptjunctions, "1", FCVAR_GLCONFIG, "removing tjuncs causes blinking pixels" );
CVAR_DEFINE_AUTO( gl_litwater_force, "0", FCVAR_GLCONFIG, "force enable lightmapped water, even if support not declared in the map" );
CVAR_DEFINE_AUTO( r_novis, "0", 0, "ignore vis information (perfomance test)" );
CVAR_DEFINE_AUTO( r_nocull, "0", 0, "ignore frustrum culling (perfomance test)" );
CVAR_DEFINE_AUTO( r_lockpvs, "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
CVAR_DEFINE_AUTO( r_lockfrustum, "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
CVAR_DEFINE_AUTO( gl_polyoffset_bmodels, "2", FCVAR_GLCONFIG, "polygon offset for brush models" );
CVAR_DEFINE_AUTO( r_large_lightmaps, "0", FCVAR_GLCONFIG|FCVAR_LATCH, "enable larger lightmap atlas textures (might break custom renderer mods)" );

#define BLOCK_SIZE_DEFAULT 128 // for keep backward compatibility
#define SUBDIVIDE_SIZE     64
#define TURBSCALE          ( 256.0f / ( M_PI2 ))

// speed up sin calculations
static const float r_turbsin[] =
{
#include "warpsin.h"
};

#define FRUSTUM_LEFT    0
#define FRUSTUM_RIGHT   1
#define FRUSTUM_BOTTOM  2
#define FRUSTUM_TOP     3
#define FRUSTUM_FAR     4
#define FRUSTUM_NEAR    5

#define CULL_VISIBLE    0
#define CULL_BACKSIDE   1
#define CULL_FRUSTUM    2
#define CULL_VISFRAME   3
#define CULL_OTHER      4

#define R_StaticEntity( ent ) ( VectorIsNull( ent->origin ) && VectorIsNull( ent->angles ))

// RI and tr of ref_gl, declared in vk_local.h
vk_refinst_t RI;
vk_globals_t tr;

#define BLOCK_SIZE ( tr.block_size )

static uint r_blocklights[BLOCK_SIZE_MAX * BLOCK_SIZE_MAX * 3];

// GPU copy of the world polygons
static struct
{
	vk_buffer_t vertices;
	vk_buffer_t indices;
	uint32_t *first_index; // per world surface
	uint32_t *num_indices;
	byte *dlit;            // the page holds dynamic light for this surface
	int num_surfaces;
} vk_wgeo;


void R_ProcessEntData( qboolean allocate, cl_entity_t *entities, unsigned int max_entities )
{
	if( !allocate )
	{
		tr.max_entities = 0;
		tr.entities = NULL;
	}
	else
	{
		tr.max_entities = max_entities;
		tr.entities = entities;
	}

	if( gEngfuncs.drawFuncs->R_ProcessEntData )
		gEngfuncs.drawFuncs->R_ProcessEntData( allocate );
}

/*
=================
Frustum (gl_frustum.c)
=================
*/
static void VK_FrustumSetPlane( vk_frustum_t *out, int side, const vec3_t vecNormal, float flDist )
{
	out->planes[side].type = PlaneTypeForNormal( vecNormal );
	out->planes[side].signbits = SignbitsForPlane( vecNormal );
	VectorCopy( vecNormal, out->planes[side].normal );
	out->planes[side].dist = flDist;
	SetBits( out->clipFlags, BIT( side ));
}

static void VK_FrustumInitProj( vk_frustum_t *out, float flZNear, float flZFar, float flFovX, float flFovY )
{
	vec3_t normal, iforward, farpoint, nearpoint;
	float xs, xc;

	memset( out, 0, sizeof( *out ));

	SinCos( DEG2RAD( flFovX ) * 0.5f, &xs, &xc );

	VectorMAM( xs, RI.cull_vforward, -xc, RI.cull_vright, normal );
	VK_FrustumSetPlane( out, FRUSTUM_LEFT, normal, DotProduct( RI.cullorigin, normal ));

	VectorMAM( xs, RI.cull_vforward, xc, RI.cull_vright, normal );
	VK_FrustumSetPlane( out, FRUSTUM_RIGHT, normal, DotProduct( RI.cullorigin, normal ));

	SinCos( DEG2RAD( flFovY ) * 0.5f, &xs, &xc );
	VectorNegate( RI.cull_vforward, iforward );

	VectorMAM( xs, RI.cull_vforward, -xc, RI.cull_vup, normal );
	VK_FrustumSetPlane( out, FRUSTUM_BOTTOM, normal, DotProduct( RI.cullorigin, normal ));

	VectorMAM( xs, RI.cull_vforward, xc, RI.cull_vup, normal );
	VK_FrustumSetPlane( out, FRUSTUM_TOP, normal, DotProduct( RI.cullorigin, normal ));

	VectorMA( RI.cullorigin, flZFar, RI.cull_vforward, farpoint );
	VK_FrustumSetPlane( out, FRUSTUM_FAR, iforward, DotProduct( iforward, farpoint ));

	if( flZNear == 0.0f ) return;

	VectorMA( RI.cullorigin, flZNear, RI.cull_vforward, nearpoint );
	VK_FrustumSetPlane( out, FRUSTUM_NEAR, RI.cull_vforward, DotProduct( RI.cull_vforward, nearpoint ));
}

static void VK_FrustumInitOrtho( vk_frustum_t *out, float xLeft, float xRight, float yTop, float yBottom, float flZNear, float flZFar )
{
	vec3_t iforward, iright, iup;
	float orgOffset;

	memset( out, 0, sizeof( *out ));

	orgOffset = DotProduct( RI.cullorigin, RI.cull_vforward );
	VectorNegate( RI.cull_vforward, iforward );

	// because quake ortho is inverted and far and near should be swaped
	VK_FrustumSetPlane( out, FRUSTUM_FAR, iforward, -flZNear - orgOffset );
	VK_FrustumSetPlane( out, FRUSTUM_NEAR, RI.cull_vforward, flZFar + orgOffset );

	orgOffset = DotProduct( RI.cullorigin, RI.cull_vright );
	VectorNegate( RI.cull_vright, iright );
	VK_FrustumSetPlane( out, FRUSTUM_LEFT, RI.cull_vright, xLeft + orgOffset );
	VK_FrustumSetPlane( out, FRUSTUM_RIGHT, iright, -xRight - orgOffset );

	orgOffset = DotProduct( RI.cullorigin, RI.cull_vup );
	VectorNegate( RI.cull_vup, iup );
	VK_FrustumSetPlane( out, FRUSTUM_TOP, RI.cull_vup, yTop + orgOffset );
	VK_FrustumSetPlane( out, FRUSTUM_BOTTOM, iup, -yBottom - orgOffset );
}

static qboolean VK_FrustumCullBox( const vk_frustum_t *out, const vec3_t mins, const vec3_t maxs, int userClipFlags )
{
	int iClipFlags = userClipFlags != 0 ? userClipFlags : out->clipFlags;

	if( unlikely( r_nocull.value ) || !iClipFlags )
		return false;

	for( int i = FRUSTUM_PLANES, bit = 1; i > 0; i--, bit <<= 1 )
	{
		const mplane_t *p = &out->planes[FRUSTUM_PLANES - i];

		if( !FBitSet( iClipFlags, bit ))
			continue;

		switch( p->signbits )
		{
		case 0:
			if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 1:
			if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 2:
			if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 3:
			if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 4:
			if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 5:
			if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 6:
			if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 7:
			if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		default:
			return false;
		}
	}

	return false;
}

// gl_cull.c
qboolean R_CullBox( const vec3_t mins, const vec3_t maxs )
{
	return VK_FrustumCullBox( &RI.frustum, mins, maxs, 0 );
}

qboolean R_CullModel( const cl_entity_t *e, const vec3_t absmin, const vec3_t absmax )
{
	if( e == tr.viewent )
	{
		if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
			return true;

		if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ) && !ENGINE_GET_PARM( PARM_THIRDPERSON ) && CL_IsViewEntityLocalPlayer())
			return false;

		return true;
	}

	return VK_FrustumCullBox( &RI.frustum, absmin, absmax, 0 );
}

// gl_rmain.c: returns true when the point is behind the view
int R_WorldToScreen( const vec3_t point, vec3_t screen )
{
	if( !point || !screen )
		return true;

	matrix4x4 worldToScreen;
	Matrix4x4_Copy( worldToScreen, RI.worldviewProjectionMatrix );
	screen[0] = worldToScreen[0][0] * point[0] + worldToScreen[0][1] * point[1] + worldToScreen[0][2] * point[2] + worldToScreen[0][3];
	screen[1] = worldToScreen[1][0] * point[0] + worldToScreen[1][1] * point[1] + worldToScreen[1][2] * point[2] + worldToScreen[1][3];
	float w = worldToScreen[3][0] * point[0] + worldToScreen[3][1] * point[1] + worldToScreen[3][2] * point[2] + worldToScreen[3][3];
	screen[2] = 0.0f; // just so we have something valid here

	qboolean behind;
	if( w < 0.001f )
	{
		behind = true;
	}
	else
	{
		float invw = 1.0f / w;
		screen[0] *= invw;
		screen[1] *= invw;
		behind = false;
	}

	return behind;
}

void R_ScreenToWorld( const vec3_t screen, vec3_t point )
{
	if( !point || !screen )
		return;

	matrix4x4 screenToWorld;
	Matrix4x4_Invert_Full( screenToWorld, RI.worldviewProjectionMatrix );

	point[0] = screen[0] * screenToWorld[0][0] + screen[1] * screenToWorld[0][1] + screen[2] * screenToWorld[0][2] + screenToWorld[0][3];
	point[1] = screen[0] * screenToWorld[1][0] + screen[1] * screenToWorld[1][1] + screen[2] * screenToWorld[1][2] + screenToWorld[1][3];
	point[2] = screen[0] * screenToWorld[2][0] + screen[1] * screenToWorld[2][1] + screen[2] * screenToWorld[2][2] + screenToWorld[2][3];
	float w = screen[0] * screenToWorld[3][0] + screen[1] * screenToWorld[3][1] + screen[2] * screenToWorld[3][2] + screenToWorld[3][3];
	if( w != 0.0f ) VectorScale( point, ( 1.0f / w ), point );
}

// gl_triapi.c: viewport pixels
int TriWorldToScreen( const float *world, float *screen )
{
	int retval = R_WorldToScreen( world, screen );

	screen[0] =  0.5f * screen[0] * (float)RI.rvp.viewport[2];
	screen[1] = -0.5f * screen[1] * (float)RI.rvp.viewport[3];
	screen[0] += 0.5f * (float)RI.rvp.viewport[2];
	screen[1] += 0.5f * (float)RI.rvp.viewport[3];

	return retval;
}

// fog comes with story 015
void R_AllowFog( qboolean allowed )
{
}

/*
=================
Polygons (gl_rsurf.c)
=================
*/
void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords )
{
	const mextrasurf_t *info = surf->info;
	float s, t;

	s = DotProduct( v, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	s += surf->light_s * sample_size;
	s += sample_size * 0.5f;
	s /= BLOCK_SIZE * sample_size;

	t = DotProduct( v, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	t += surf->light_t * sample_size;
	t += sample_size * 0.5f;
	t /= BLOCK_SIZE * sample_size;

	Vector2Set( coords, s, t );
}

static void R_TextureCoord( const vec3_t v, const msurface_t *surf, vec2_t coords )
{
	const mtexinfo_t *info = surf->texinfo;
	float s = DotProduct( v, info->vecs[0] );
	float t = DotProduct( v, info->vecs[1] );

	if( !FBitSet( surf->flags, SURF_DRAWTURB ))
	{
		s = ( s + info->vecs[0][3] ) / info->texture->width;
		t = ( t + info->vecs[1][3] ) / info->texture->height;
	}

	Vector2Set( coords, s, t );
}

static void R_GetEdgePosition( const model_t *mod, const msurface_t *fa, int i, vec3_t vec )
{
	const int lindex = mod->surfedges[fa->firstedge + i];

	if( FBitSet( mod->flags, MODEL_QBSP2 ))
	{
		const medge32_t *pedges = mod->edges32;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
	else
	{
		const medge16_t *pedges = mod->edges16;

		if( lindex > 0 )
			VectorCopy( mod->vertexes[pedges[lindex].v[0]].position, vec );
		else
			VectorCopy( mod->vertexes[pedges[-lindex].v[1]].position, vec );
	}
}

static void BoundPoly( int numverts, float *verts, vec3_t mins, vec3_t maxs )
{
	float *v = verts;

	ClearBounds( mins, maxs );

	for( int i = 0; i < numverts; i++ )
	{
		for( int j = 0; j < 3; j++, v++ )
		{
			if( *v < mins[j] ) mins[j] = *v;
			if( *v > maxs[j] ) maxs[j] = *v;
		}
	}
}

static void SubdividePolygon_r( model_t *loadmodel, msurface_t *warpface, int numverts, float *verts )
{
	vec3_t front[SUBDIVIDE_SIZE], back[SUBDIVIDE_SIZE];
	float dist[SUBDIVIDE_SIZE];
	vec3_t mins, maxs;
	float *v;
	int j;

	if( numverts > ( SUBDIVIDE_SIZE - 4 ))
		gEngfuncs.Host_Error( "%s: too many vertexes on face ( %i )\n", __func__, numverts );

	BoundPoly( numverts, verts, mins, maxs );

	for( int i = 0; i < 3; i++ )
	{
		float m = ( mins[i] + maxs[i] ) * 0.5f;
		m = SUBDIVIDE_SIZE * floor( m / SUBDIVIDE_SIZE + 0.5f );
		if( maxs[i] - m < 8 ) continue;
		if( m - mins[i] < 8 ) continue;

		// cut it
		v = verts + i;
		for( j = 0; j < numverts; j++, v += 3 )
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v -= i;
		VectorCopy( verts, v );

		int f = 0, b = 0;
		v = verts;
		for( j = 0; j < numverts; j++, v += 3 )
		{
			if( dist[j] >= 0 )
			{
				VectorCopy( v, front[f] );
				f++;
			}

			if( dist[j] <= 0 )
			{
				VectorCopy( v, back[b] );
				b++;
			}

			if( dist[j] == 0 || dist[j+1] == 0 )
				continue;

			if(( dist[j] > 0 ) != ( dist[j+1] > 0 ))
			{
				// clip point
				float frac = dist[j] / ( dist[j] - dist[j+1] );
				for( int k = 0; k < 3; k++ )
					front[f][k] = back[b][k] = v[k] + frac * ( v[3+k] - v[k] );
				f++;
				b++;
			}
		}

		SubdividePolygon_r( loadmodel, warpface, f, front[0] );
		SubdividePolygon_r( loadmodel, warpface, b, back[0] );
		return;
	}

	if( numverts != 4 )
		ClearBits( warpface->flags, SURF_DRAWTURB_QUADS );

	glpoly2_t *poly = Mem_Calloc( loadmodel->mempool, sizeof( glpoly2_t ) + numverts * VERTEXSIZE * sizeof( float ));
	poly->next = warpface->polys;
	poly->flags = warpface->flags;
	warpface->polys = poly;
	poly->numverts = numverts;

	for( int i = 0; i < numverts; i++, verts += 3 )
	{
		VectorCopy( verts, poly->verts[i] );
		R_TextureCoord( verts, warpface, &poly->verts[i][3] );
	}
}

void GL_SubdivideSurface( model_t *loadmodel, msurface_t *fa )
{
	vec3_t verts[SUBDIVIDE_SIZE];

	for( int i = 0; i < fa->numedges; i++ )
		R_GetEdgePosition( loadmodel, fa, i, verts[i] );

	// ref_gl predicts quads only with a compatibility GL context
	SubdividePolygon_r( loadmodel, fa, fa->numedges, verts[0] );
}

static int GL_BuildPolygonFromSurface( model_t *mod, msurface_t *fa )
{
	int nColinElim = 0;

	if( !mod || !fa->texinfo || !fa->texinfo->texture )
		return nColinElim;

	if( FBitSet( fa->flags, SURF_CONVEYOR ) && fa->texinfo->texture->gl_texturenum != 0 )
	{
		texture_t *tex = fa->texinfo->texture;

		// update conveyor widths for keep properly speed of scrolling
		R_OverrideTextureSourceSize( tex->gl_texturenum, tex->width, tex->height );
	}

	float sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );
	int lnumverts = fa->numedges;

	glpoly2_t *poly = fa->polys;
	fa->polys = NULL;

	poly = Mem_Realloc( mod->mempool, poly, sizeof( glpoly2_t ) + lnumverts * VERTEXSIZE * sizeof( float ));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for( int i = 0; i < lnumverts; i++ )
	{
		R_GetEdgePosition( mod, fa, i, poly->verts[i] );
		R_TextureCoord( poly->verts[i], fa, &poly->verts[i][3] );
		R_LightmapCoord( poly->verts[i], fa, sample_size, &poly->verts[i][5] );
	}

	// remove co-linear points - Ed
	if( !gl_keeptjunctions.value && !FBitSet( fa->flags, SURF_UNDERWATER ))
	{
		for( int i = 0; i < lnumverts; i++ )
		{
			vec3_t v1, v2;
			float *prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			float *next = poly->verts[(i + 1) % lnumverts];
			float *this = poly->verts[i];

			VectorSubtract( this, prev, v1 );
			VectorNormalize( v1 );
			VectorSubtract( next, prev, v2 );
			VectorNormalize( v2 );

			if(( fabs( v1[0] - v2[0] ) <= 0.001f ) && ( fabs( v1[1] - v2[1] ) <= 0.001f ) && ( fabs( v1[2] - v2[2] ) <= 0.001f ))
			{
				for( int j = i + 1; j < lnumverts; j++ )
				{
					for( int k = 0; k < VERTEXSIZE; k++ )
						poly->verts[j-1][k] = poly->verts[j][k];
				}

				lnumverts--;
				nColinElim++;
				i--;
			}
		}
	}

	poly->numverts = lnumverts;
	return nColinElim;
}

/*
=================
Lightmaps (gl_rsurf.c)
=================
*/
static int Mod_LightmappedWaterMinlight( void )
{
	if( FBitSet( tr.world->flags, FWORLD_HAS_LITWATER ) && tr.world->litwater_minlight >= 0 )
		return tr.world->litwater_minlight;

	return 0;
}

static float Mod_LightmappedWaterScale( void )
{
	if( FBitSet( tr.world->flags, FWORLD_HAS_LITWATER ) && tr.world->litwater_scale >= 0.0f )
		return tr.world->litwater_scale;

	return 1.0f;
}

static void R_AddDynamicLights( const msurface_t *surf, float sample_size, int smax, int tmax )
{
	const mextrasurf_t *info = surf->info;
	int sample_frac = 1.0;

	if( !surf->dlightbits )
		return;

	mtexinfo_t *tex = surf->texinfo;

	if( FBitSet( tex->flags, TEX_WORLD_LUXELS ))
	{
		if( surf->texinfo->faceinfo )
			sample_frac = surf->texinfo->faceinfo->texture_step;
		else if( FBitSet( surf->texinfo->flags, TEX_EXTRA_LIGHTMAP ))
			sample_frac = LM_SAMPLE_EXTRASIZE;
		else sample_frac = LM_SAMPLE_SIZE;
	}

	for( int lnum = 0; lnum < MAX_DLIGHTS; lnum++ )
	{
		vec3_t impact, origin_l;

		if( !FBitSet( surf->dlightbits, BIT( lnum )))
			continue;

		dlight_t *dl = &gp_dlights[lnum];

		// transform light origin to local bmodel space
		if( !tr.modelviewIdentity )
			Matrix4x4_VectorITransform( RI.objectMatrix, dl->origin, origin_l );
		else VectorCopy( dl->origin, origin_l );

		float rad = dl->radius;
		float dist = PlaneDiff( origin_l, surf->plane );
		rad -= fabs( dist );

		float minlight = dl->minlight;
		if( rad < minlight )
			continue;

		minlight = rad - minlight;

		if( surf->plane->type < 3 )
		{
			VectorCopy( origin_l, impact );
			impact[surf->plane->type] -= dist;
		}
		else VectorMA( origin_l, -dist, surf->plane->normal, impact );

		float sl = DotProduct( impact, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
		float tl = DotProduct( impact, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];

		float half = ( minlight + 1.0f ) / ( sample_size * sample_frac );
		int s0 = Q_max( 0, (int)( sl / sample_size - half ));
		int s1 = Q_min( smax - 1, (int)( sl / sample_size + half ));
		int t0 = Q_max( 0, (int)( tl / sample_size - half ));
		int t1 = Q_min( tmax - 1, (int)( tl / sample_size + half ));

		for( int t = t0; t <= t1; t++ )
		{
			int td = ( tl - sample_size * t ) * sample_frac;

			if( td < 0 )
				td = -td;

			for( int s = s0; s <= s1; s++ )
			{
				int sd = ( sl - sample_size * s ) * sample_frac;
				float d;

				if( sd < 0 )
					sd = -sd;

				if( sd > td )
					d = sd + ( td >> 1 );
				else
					d = td + ( sd >> 1 );

				if( d < minlight )
				{
					uint *bl = &r_blocklights[(s + (t * smax)) * 3];

					bl[0] += ((int)(( rad - d ) * 256 ) * dl->color.r ) / 256;
					bl[1] += ((int)(( rad - d ) * 256 ) * dl->color.g ) / 256;
					bl[2] += ((int)(( rad - d ) * 256 ) * dl->color.b ) / 256;
				}
			}
		}
	}
}

static void R_BuildLightMap( const msurface_t *surf, byte *restrict dest, int stride, qboolean dynamic )
{
	const mextrasurf_t *info = surf->info;
	const qboolean turb = FBitSet( surf->flags, SURF_DRAWTURB );
	const qboolean linear_gamma = FBitSet( gp_host->features, ENGINE_LINEAR_GAMMA_SPACE );
	const uint16_t *restrict lightgammatable = (const uint16_t *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	const int litwater_minlight = Mod_LightmappedWaterMinlight();
	const float litwater_scale = Mod_LightmappedWaterScale();
	const int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	const int smax = ( info->lightextents[0] / sample_size ) + 1;
	const int tmax = ( info->lightextents[1] / sample_size ) + 1;
	const int size = smax * tmax;
	qboolean init = false;
	int lightscale, map;

	// 256 is ref_gl without VBO, the default path
	if( gl_overbright.value )
		lightscale = 256;
	else
		lightscale = ( pow( 2.0f, 1.0f / v_lightgamma->value ) * 256 ) + 0.5;

	for( map = 0; map < MAXLIGHTMAPS && surf->samples; map++ )
	{
		if( surf->styles[map] >= 255 )
			break;

		uint scale = g_lightstylevalue[surf->styles[map]];
		const color24 *lm = &surf->samples[map * size];
		for( int i = 0; i < size; i++ )
		{
			r_blocklights[i * 3 + 0] = lm[i].r * scale;
			r_blocklights[i * 3 + 1] = lm[i].g * scale;
			r_blocklights[i * 3 + 2] = lm[i].b * scale;
		}
		init = true;
		break;
	}

	if( init )
	{
		for( map++; map < MAXLIGHTMAPS; map++ )
		{
			if( surf->styles[map] >= 255 )
				break;

			uint scale = g_lightstylevalue[surf->styles[map]];
			const color24 *lm = &surf->samples[map * size];
			for( int i = 0; i < size; i++ )
			{
				r_blocklights[i * 3 + 0] += lm[i].r * scale;
				r_blocklights[i * 3 + 1] += lm[i].g * scale;
				r_blocklights[i * 3 + 2] += lm[i].b * scale;
			}
		}
	}
	else
		memset( r_blocklights, 0, sizeof( uint ) * size * 3 );

	if( surf->dlightframe == tr.framecount && dynamic )
		R_AddDynamicLights( surf, sample_size, smax, tmax );

	for( int t = 0; t < tmax; t++ )
	{
		for( int s = 0; s < smax; s++ )
		{
			const uint *restrict bl = &r_blocklights[(s + (t * smax)) * 3];
			byte *restrict dst = &dest[(t * stride) + (s * 4)];

			for( int i = 0; i < 3; i++ )
			{
				int l = bl[i] * lightscale >> 14;

				// amp up water lightmap to avoid too dark water
				if( turb )
				{
					float fl = l * litwater_scale;
					l = Q_max( Q_rint( fl ), litwater_minlight );
				}

				if( l > 1023 )
					l = 1023;

				if( linear_gamma )
					dst[i] = l >> 2;
				else
					dst[i] = lightgammatable[l] >> 2;
			}
			dst[3] = 255;
		}
	}
}

static void LM_InitBlock( void )
{
	Atlas_Init( &tr.atlas, BLOCK_SIZE );
}

static void LM_UploadBlock( void )
{
	rgbdata_t r_lightmap;
	char lmName[16];
	int i = tr.current_lightmap_texture;

	memset( &r_lightmap, 0, sizeof( r_lightmap ));
	Q_snprintf( lmName, sizeof( lmName ), "*lightmap%i", i );

	r_lightmap.width = BLOCK_SIZE;
	r_lightmap.height = BLOCK_SIZE;
	r_lightmap.type = PF_RGBA_32;
	r_lightmap.size = r_lightmap.width * r_lightmap.height * 4;
	r_lightmap.flags = IMAGE_HAS_COLOR;
	r_lightmap.buffer = tr.lightmap_buffer;
	tr.lightmapTextures[i] = GL_LoadTextureFromBuffer( lmName, &r_lightmap, TF_NOMIPMAP|TF_ATLAS_PAGE, false );

	if( ++tr.current_lightmap_texture == MAX_LIGHTMAPS )
		gEngfuncs.Host_Error( "%s: full\n", __func__ );
}

static void GL_CreateSurfaceLightmap( msurface_t *surf, model_t *loadmodel )
{
	mextrasurf_t *info = surf->info;

	if( !loadmodel->lightdata || FBitSet( surf->flags, SURF_DRAWTILED ))
		return;

	int sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	int smax = ( info->lightextents[0] / sample_size ) + 1;
	int tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( !Atlas_AllocBlock( &tr.atlas, smax, tmax, &surf->light_s, &surf->light_t ))
	{
		LM_UploadBlock();
		LM_InitBlock();

		if( !Atlas_AllocBlock( &tr.atlas, smax, tmax, &surf->light_s, &surf->light_t ))
			gEngfuncs.Host_Error( "%s: full\n", __func__ );
	}

	surf->lightmaptexturenum = tr.current_lightmap_texture;

	byte *base = tr.lightmap_buffer;
	base += ( surf->light_t * BLOCK_SIZE + surf->light_s ) * 4;

	R_UpdateSurfaceCachedLight( surf );
	R_BuildLightMap( surf, base, BLOCK_SIZE * 4, false );
}

static void VK_FreeLightmaps( void )
{
	for( int i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !tr.lightmapTextures[i] ) break;
		GL_FreeTexture( tr.lightmapTextures[i] );
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	tr.current_lightmap_texture = 0;
}

/*
=================
GPU geometry: every polygon of the world model, fanned into triangles
=================
*/
static void VK_FreeWorldGeometry( void )
{
	if( vk_wgeo.vertices.buffer || vk_wgeo.indices.buffer )
	{
		vkDeviceWaitIdle( vk.device ); // map change, frames may still read them
		VK_DestroyBuffer( &vk_wgeo.vertices );
		VK_DestroyBuffer( &vk_wgeo.indices );
	}

	if( vk_wgeo.first_index )
		Mem_Free( vk_wgeo.first_index );
	if( vk_wgeo.num_indices )
		Mem_Free( vk_wgeo.num_indices );
	if( vk_wgeo.dlit )
		Mem_Free( vk_wgeo.dlit );

	memset( &vk_wgeo, 0, sizeof( vk_wgeo ));
}

static void VK_UploadBuffer( vk_buffer_t *dst, const void *data, VkDeviceSize size )
{
	VkBuffer staging;
	VkDeviceSize offset;
	VkCommandBuffer cmd;
	void *ptr = VK_UploadStaging( size, &staging, &offset, &cmd );

	if( !ptr )
		return;

	memcpy( ptr, data, size );

	VkBufferCopy copy = { offset, 0, size };
	vkCmdCopyBuffer( cmd, staging, dst->buffer, 1, &copy );
}

static void VK_BuildWorldGeometry( void )
{
	const model_t *mod = WORLDMODEL;
	size_t num_verts = 0, num_indices = 0;
	float *verts;
	uint32_t *indices;

	VK_FreeWorldGeometry();

	vk_wgeo.num_surfaces = mod->numsurfaces;
	vk_wgeo.first_index = Mem_Calloc( r_temppool, sizeof( uint32_t ) * mod->numsurfaces );
	vk_wgeo.num_indices = Mem_Calloc( r_temppool, sizeof( uint32_t ) * mod->numsurfaces );
	vk_wgeo.dlit = Mem_Calloc( r_temppool, mod->numsurfaces );

	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		for( const glpoly2_t *p = mod->surfaces[i].polys; p; p = p->next )
		{
			num_verts += p->numverts;
			num_indices += ( p->numverts - 2 ) * 3;
		}
	}

	if( !num_verts )
		return;

	verts = Mem_Malloc( r_temppool, num_verts * VERTEXSIZE * sizeof( float ));
	indices = Mem_Malloc( r_temppool, num_indices * sizeof( uint32_t ));
	num_verts = num_indices = 0;

	for( int i = 0; i < mod->numsurfaces; i++ )
	{
		vk_wgeo.first_index[i] = num_indices;

		for( const glpoly2_t *p = mod->surfaces[i].polys; p; p = p->next )
		{
			// GL_POLYGON is a fan
			for( int j = 2; j < p->numverts; j++ )
			{
				indices[num_indices++] = num_verts;
				indices[num_indices++] = num_verts + j - 1;
				indices[num_indices++] = num_verts + j;
			}

			memcpy( &verts[num_verts * VERTEXSIZE], p->verts, p->numverts * VERTEXSIZE * sizeof( float ));
			num_verts += p->numverts;
		}

		vk_wgeo.num_indices[i] = num_indices - vk_wgeo.first_index[i];
	}

	if( VK_CreateBuffer( &vk_wgeo.vertices, "world vertices", num_verts * VERTEXSIZE * sizeof( float ),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
		&& VK_CreateBuffer( &vk_wgeo.indices, "world indices", num_indices * sizeof( uint32_t ),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ))
	{
		VK_UploadBuffer( &vk_wgeo.vertices, verts, num_verts * VERTEXSIZE * sizeof( float ));
		VK_UploadBuffer( &vk_wgeo.indices, indices, num_indices * sizeof( uint32_t ));
		VK_FlushUploads();
	}

	Mem_Free( verts );
	Mem_Free( indices );

	gEngfuncs.Con_Reportf( "ref_vk: world geometry %u vertices, %u indices\n", (uint)num_verts, (uint)num_indices );
}

/*
=================
GL_BuildLightmaps
=================
*/
static void VK_BuildLightmaps( void )
{
	VK_FreeLightmaps();

	if( FBitSet( gp_host->features, ENGINE_LARGE_LIGHTMAPS ) || tr.world->version == QBSP2_VERSION || r_large_lightmaps.value )
		tr.block_size = BLOCK_SIZE_MAX;
	else tr.block_size = BLOCK_SIZE_DEFAULT;

	tr.skychain = NULL;
	tr.framecount = tr.visframecount = 1; // no dlight cache
	tr.realframecount = 1;
	memset( &RI, 0, sizeof( RI ));

	CL_RunLightStyles((lightstyle_t *)ENGINE_GET_PARM( PARM_GET_LIGHTSTYLES_PTR ));

	LM_InitBlock();

	for( int i = 0; i < gp_cl->nummodels; i++ )
	{
		model_t *m = CL_ModelHandle( i + 1 );

		if( m == NULL || m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( int j = 0; j < m->numsurfaces; j++ )
		{
			m->surfaces[j].pdecals = NULL;
			m->surfaces[j].visframe = 0;

			GL_CreateSurfaceLightmap( m->surfaces + j, m );

			// warped surfaces keep the polygons GL_SubdivideSurface built
			if( m->surfaces[j].flags & SURF_DRAWTURB )
				continue;

			GL_BuildPolygonFromSurface( m, m->surfaces + j );
		}

		for( int j = 0; j < m->numleafs; j++ )
			m->leafs[j+1].visframe = 0;
		for( int j = 0; j < m->numnodes; j++ )
			m->nodes[j].visframe = 0;
	}

	LM_UploadBlock();

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
}

// GL_RebuildLightmaps: gamma or overbright changed
static void VK_RebuildLightmaps( void )
{
	if( !ENGINE_GET_PARM( PARM_CLIENT_ACTIVE ) || !WORLDMODEL )
		return;

	VK_FreeLightmaps();
	CL_RunLightStyles((lightstyle_t *)ENGINE_GET_PARM( PARM_GET_LIGHTSTYLES_PTR ));
	LM_InitBlock();

	for( int i = 0; i < gp_cl->nummodels; i++ )
	{
		model_t *m = CL_ModelHandle( i + 1 );

		if( m == NULL || m->name[0] == '*' || m->type != mod_brush )
			continue;

		for( int j = 0; j < m->numsurfaces; j++ )
			GL_CreateSurfaceLightmap( m->surfaces + j, m );
	}

	LM_UploadBlock();

	if( vk_wgeo.dlit )
		memset( vk_wgeo.dlit, 0, vk_wgeo.num_surfaces );

	if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
		gEngfuncs.drawFuncs->GL_BuildLightmaps( );
}

void R_GammaChanged( qboolean do_reset_gamma )
{
	if( do_reset_gamma )
	{
		if( gEngfuncs.drawFuncs->GL_BuildLightmaps )
			gEngfuncs.drawFuncs->GL_BuildLightmaps( );
	}
	else VK_RebuildLightmaps();
}

/*
=================
R_UpdateSurfaceLightmap

R_CheckLightMap of ref_gl: a light style changed, or a dynamic light
touches the surface or stopped touching it; rebuild its texels in place
=================
*/
static void R_UpdateSurfaceLightmap( msurface_t *fa, int surfnum )
{
	static byte texels[BLOCK_SIZE_MAX * BLOCK_SIZE_MAX * 4];
	const qboolean dlit = r_dynamic->value && fa->dlightframe == tr.framecount;
	qboolean changed = dlit || vk_wgeo.dlit[surfnum];

	if( !r_dynamic->value && !vk_wgeo.dlit[surfnum] )
		return;

	for( int maps = 0; !changed && maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++ )
	{
		if( g_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps] )
			changed = true;
	}

	if( !changed )
		return;

	mextrasurf_t *info = fa->info;
	int sample_size = gEngfuncs.Mod_SampleSizeForFace( fa );
	int smax = ( info->lightextents[0] / sample_size ) + 1;
	int tmax = ( info->lightextents[1] / sample_size ) + 1;

	if( smax > BLOCK_SIZE_MAX || tmax > BLOCK_SIZE_MAX )
		return;

	R_BuildLightMap( fa, texels, smax * 4, dlit );
	R_UpdateSurfaceCachedLight( fa );
	VK_UpdateTextureRegion( tr.lightmapTextures[fa->lightmaptexturenum], fa->light_s, fa->light_t, smax, tmax, texels );

	vk_wgeo.dlit[surfnum] = dlit;
}

/*
=================
Texture animation
=================
*/
texture_t *R_TextureAnimation( msurface_t *s )
{
	texture_t *base = s->texinfo->texture;
	int reletive;

	if( RI.currententity && RI.currententity->curstate.frame )
	{
		if( base->alternate_anims )
			base = base->alternate_anims;
	}

	if( !base->anim_total )
		return base;

	if( base->name[0] == '-' )
	{
		int tx = (int)(( s->texturemins[0] + ( base->width << 16 )) / base->width ) % MOD_FRAMES;
		int ty = (int)(( s->texturemins[1] + ( base->height << 16 )) / base->height ) % MOD_FRAMES;

		reletive = rtable[tx][ty] % base->anim_total;
	}
	else
	{
		// Quake1 textures uses 10 frames per second
		int speed = FBitSet( VK_TextureParm( PARM_TEX_FLAGS, base->gl_texturenum ), TF_QUAKEPAL ) ? 10 : 20;

		reletive = (int)( gp_cl->time * speed ) % base->anim_total;
	}

	int count = 0;

	while( base->anim_min > reletive || base->anim_max <= reletive )
	{
		base = base->anim_next;

		if( !base || ++count > MOD_FRAMES )
			return s->texinfo->texture;
	}

	return base;
}

/*
=================
Visibility (gl_rsurf.c, gl_cull.c)
=================
*/
static int R_CullSurface( const msurface_t *surf, const vk_frustum_t *frustum, uint clipflags )
{
	const cl_entity_t *e = RI.currententity;

	if( surf->visframe != tr.framecount && e == CL_GetEntityByIndex( 0 ))
		return CULL_VISFRAME;

	if( unlikely( !surf->texinfo || !surf->texinfo->texture ))
		return CULL_OTHER;

	if( unlikely( r_nocull.value ))
		return CULL_VISIBLE;

	if( !R_StaticEntity( e ))
		frustum = NULL;

	// world culls front faces (GL_Cull( GL_FRONT ))
	if( !VectorIsNull( surf->plane->normal ))
	{
		float dist;

		if( FBitSet( RI.rvp.flags, RF_DRAW_OVERVIEW ))
			dist = surf->plane->normal[2];
		else dist = PlaneDiff( tr.modelorg, surf->plane );

		if( FBitSet( surf->flags, SURF_PLANEBACK ))
			dist = -dist;

		if( dist <= BACKFACE_EPSILON )
			return CULL_BACKSIDE;
	}

	if( frustum && clipflags && VK_FrustumCullBox( frustum, surf->info->mins, surf->info->maxs, clipflags ))
		return CULL_FRUSTUM;

	return CULL_VISIBLE;
}

static void R_FindViewLeaf( void )
{
	RI.oldviewleaf = RI.viewleaf;
	RI.viewleaf = gEngfuncs.Mod_PointInLeaf( RI.rvp.vieworigin, WORLDMODEL->nodes, WORLDMODEL );
}

static void R_MarkLeaves( void )
{
	qboolean novis = false;
	qboolean force = false;
	mleaf_t *leaf = NULL;

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return;

	if( FBitSet( r_novis.flags, FCVAR_CHANGED ) || tr.fResetVis )
	{
		ClearBits( r_novis.flags, FCVAR_CHANGED );
		tr.fResetVis = false;
		RI.viewleaf = NULL;
	}

	vec3_t test = Vec3( RI.rvp.vieworigin );

	if( RI.viewleaf != NULL )
	{
		// merge two leafs that can be a crossed-line contents
		if( RI.viewleaf->contents == CONTENTS_EMPTY )
			VectorSet( test, RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2] - 16.0f );
		else
			VectorSet( test, RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2] + 16.0f );

		leaf = gEngfuncs.Mod_PointInLeaf( test, WORLDMODEL->nodes, WORLDMODEL );

		if(( leaf->contents != CONTENTS_SOLID ) && ( RI.viewleaf != leaf ))
			force = true;
	}

	if( RI.viewleaf == RI.oldviewleaf && RI.viewleaf != NULL && !force )
		return;

	if( r_lockpvs.value ) return;

	RI.oldviewleaf = RI.viewleaf;
	tr.visframecount++;

	if( r_novis.value || FBitSet( RI.rvp.flags, RF_DRAW_OVERVIEW ) || !RI.viewleaf || !WORLDMODEL->visdata )
		novis = true;

	gEngfuncs.R_FatPVS( RI.rvp.vieworigin, r_pvs_radius->value, RI.visbytes, false, novis );
	if( force && !novis )
		gEngfuncs.R_FatPVS( test, r_pvs_radius->value, RI.visbytes, true, novis );

	for( int i = 0; i < WORLDMODEL->numleafs; i++ )
	{
		if( CHECKVISBIT( RI.visbytes, i ))
		{
			mnode_t *node = (mnode_t *)&WORLDMODEL->leafs[i+1];
			do
			{
				if( node->visframe == tr.visframecount )
					break;
				node->visframe = tr.visframecount;
				node = node->parent;
			} while( node );
		}
	}
}

static void R_RecursiveWorldNode( mnode_t *node, uint clipflags )
{
loc0:
	if( node->contents == CONTENTS_SOLID )
		return;

	if( node->visframe != tr.visframecount )
		return;

	if( clipflags && !r_nocull.value )
	{
		for( int i = 0; i < 6; i++ )
		{
			const mplane_t *p = &RI.frustum.planes[i];

			if( !FBitSet( clipflags, BIT( i )))
				continue;

			int clipped = BOX_ON_PLANE_SIDE( node->minmaxs, node->minmaxs + 3, p );
			if( clipped == 2 ) return;
			if( clipped == 1 ) ClearBits( clipflags, BIT( i ));
		}
	}

	if( node->contents < 0 )
	{
		mleaf_t *pleaf = (mleaf_t *)node;
		msurface_t **mark = pleaf->firstmarksurface;

		for( int i = 0; i < pleaf->nummarksurfaces; i++ )
			mark[i]->visframe = tr.framecount;

		if( pleaf->efrags )
			gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );
		return;
	}

	float dot = PlaneDiff( tr.modelorg, node->plane );
	int side = ( dot >= 0.0f ) ? 0 : 1;

	R_RecursiveWorldNode( node_child( node, side, WORLDMODEL ), clipflags );

	int firstsurface = node_firstsurface( node, WORLDMODEL );
	int numsurfaces = node_numsurfaces( node, WORLDMODEL );

	for( int i = firstsurface; i < firstsurface + numsurfaces; i++ )
	{
		msurface_t *surf = &WORLDMODEL->surfaces[i];

		if( R_CullSurface( surf, &RI.frustum, clipflags ))
			continue;

		if( surf->flags & SURF_DRAWSKY )
		{
			surf->texturechain = tr.skychain;
			tr.skychain = surf;
		}
		else
		{
			surf->texturechain = surf->texinfo->texture->texturechain;
			surf->texinfo->texture->texturechain = surf;
		}
	}

	node = node_child( node, !side, WORLDMODEL );
	goto loc0;
}

byte *Mod_GetCurrentVis( void )
{
	return RI.visbytes;
}

void GL_OrthoBounds( const float *mins, const float *maxs )
{
	Vector2Copy( mins, tr.ortho_mins );
	Vector2Copy( maxs, tr.ortho_maxs );
}

/*
=================
View setup (gl_rmain.c)
=================
*/
static float R_GetFarClip( void )
{
	if( WORLDMODEL && FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return gp_movevars->zmax * 1.73f;
	return 2048.0f;
}

static void R_SetupFrustum( void )
{
	AngleVectors( RI.rvp.viewangles, RI.vforward, RI.vright, RI.vup );

	if( !r_lockfrustum.value )
	{
		VectorCopy( RI.rvp.vieworigin, RI.cullorigin );
		VectorCopy( RI.vforward, RI.cull_vforward );
		VectorCopy( RI.vright, RI.cull_vright );
		VectorCopy( RI.vup, RI.cull_vup );
	}

	if( FBitSet( RI.rvp.flags, RF_DRAW_OVERVIEW ))
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		VK_FrustumInitOrtho( &RI.frustum, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
	}
	else
	{
		VK_FrustumInitProj( &RI.frustum, 0.0f, R_GetFarClip(), RI.rvp.fov_x, RI.rvp.fov_y );
	}
}

static void R_SetupProjectionMatrix( matrix4x4 m )
{
	if( FBitSet( RI.rvp.flags, RF_DRAW_OVERVIEW ))
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}

	RI.farClip = R_GetFarClip();

	float zNear = 4.0f;
	float zFar = Q_max( 256.0f, RI.farClip );
	float yMax = zNear * tan( RI.rvp.fov_y * M_PI_F / 360.0f );
	float xMax = zNear * tan( RI.rvp.fov_x * M_PI_F / 360.0f );

	Matrix4x4_CreateProjection( m, xMax, -xMax, yMax, -yMax, zNear, zFar );
}

static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -RI.rvp.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -RI.rvp.vieworigin[0], -RI.rvp.vieworigin[1], -RI.rvp.vieworigin[2] );
}

// GL clip space has z in [-w, w], Vulkan in [0, w]
static void VK_GLToVulkanClip( matrix4x4 out, const matrix4x4 in )
{
	matrix4x4 clip;

	Matrix4x4_LoadIdentity( clip );
	clip[2][2] = 0.5f;
	clip[2][3] = 0.5f;
	Matrix4x4_Concat( out, clip, in );
}

void VK_ComputeMVP( float out[16], const matrix4x4 modelview )
{
	matrix4x4 mvp, vkmvp;

	Matrix4x4_Concat( mvp, RI.projectionMatrix, modelview );
	VK_GLToVulkanClip( vkmvp, mvp );
	Matrix4x4_ToArrayFloatGL( vkmvp, out );
}

/*
=================
Skybox (gl_warp.c): the sky surfaces only mark which parts of the box are
seen, the box is drawn after the world at half the far clip distance
=================
*/
#define MAX_CLIP_VERTS 128

static const vec3_t skyclip[SKYBOX_MAX_SIDES] =
{
{  1,  1,  0 },
{  1, -1,  0 },
{  0, -1,  1 },
{  0,  1,  1 },
{  1,  0,  1 },
{ -1,  0,  1 }
};

// 1 = s, 2 = t, 3 = 2048
static const int st_to_vec[SKYBOX_MAX_SIDES][3] =
{
{  3, -1,  2 },
{ -3,  1,  2 },
{  1,  3,  2 },
{ -1, -3,  2 },
{ -2, -1,  3 },  // 0 degrees yaw, look straight up
{  2, -1, -3 }   // look straight down
};

static void DrawSkyPolygon( int nump, vec3_t vecs )
{
	vec3_t v;
	float *vp = vecs;

	VectorClear( v );
	for( int i = 0; i < nump; i++, vp += 3 )
		VectorAdd( vp, v, v );

	const int axis = R_SkyboxAxisFromDir( v );

	for( int i = 0; i < nump; i++, vecs += 3 )
	{
		float s, t;

		if( !R_SkyboxProject( vecs, axis, &s, &t ))
			continue;

		if( s < RI.skyMins[0][axis] ) RI.skyMins[0][axis] = s;
		if( t < RI.skyMins[1][axis] ) RI.skyMins[1][axis] = t;
		if( s > RI.skyMaxs[0][axis] ) RI.skyMaxs[0][axis] = s;
		if( t > RI.skyMaxs[1][axis] ) RI.skyMaxs[1][axis] = t;
	}
}

static void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
{
	const float *norm;
	float *v, d, e;
	qboolean front, back;
	float dists[MAX_CLIP_VERTS + 1];
	int sides[MAX_CLIP_VERTS + 1];
	vec3_t newv[2][MAX_CLIP_VERTS + 1];
	int newc[2];
	int i, j;

	if( nump > MAX_CLIP_VERTS )
		gEngfuncs.Host_Error( "%s: MAX_CLIP_VERTS\n", __func__ );
loc1:
	if( stage == 6 )
	{
		DrawSkyPolygon( nump, vecs );
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		d = DotProduct( v, norm );
		if( d > ON_EPSILON )
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if( d < -ON_EPSILON )
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if( !front || !back )
	{
		stage++;
		goto loc1;
	}

	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy( vecs, ( vecs + ( i * 3 )));
	newc[0] = newc[1] = 0;

	for( i = 0, v = vecs; i < nump; i++, v += 3 )
	{
		switch( sides[i] )
		{
		case SIDE_FRONT:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy( v, newv[0][newc[0]] );
			newc[0]++;
			VectorCopy( v, newv[1][newc[1]] );
			newc[1]++;
			break;
		}

		if( sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i] )
			continue;

		d = dists[i] / ( dists[i] - dists[i+1] );
		for( j = 0; j < 3; j++ )
		{
			e = v[j] + d * ( v[j+3] - v[j] );
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	ClipSkyPolygon( newc[0], newv[0][0], stage + 1 );
	ClipSkyPolygon( newc[1], newv[1][0], stage + 1 );
}

static void MakeSkyVec( float s, float t, int axis, float *out )
{
	int farclip = RI.farClip;
	vec3_t b, v;

	b[0] = s * ( farclip >> 1 );
	b[1] = t * ( farclip >> 1 );
	b[2] = ( farclip >> 1 );

	for( int j = 0; j < 3; j++ )
	{
		int k = st_to_vec[axis][j];
		v[j] = ( k < 0 ) ? -b[-k-1] : b[k-1];
		v[j] += RI.cullorigin[j];
	}

	// avoid bilerp seam, clamp to edge is always there
	s = bound( 0.0f, ( s + 1.0f ) * 0.5f, 1.0f );
	t = bound( 0.0f, ( t + 1.0f ) * 0.5f, 1.0f );
	t = 1.0f - t;

	VectorCopy( v, out );
	out[3] = s;
	out[4] = t;
	out[5] = out[6] = 0.0f;
}

static void R_ClearSkyBox( void )
{
	for( int i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		RI.skyMins[0][i] = RI.skyMins[1][i] = 9999999.0f;
		RI.skyMaxs[0][i] = RI.skyMaxs[1][i] = -9999999.0f;
	}
}

static void R_AddSkyBoxSurface( msurface_t *fa )
{
	vec3_t verts[MAX_CLIP_VERTS];

	// the Quake sky sphere (FWORLD_SKYSPHERE) isn't part of the HL / CS scope
	for( glpoly2_t *p = fa->polys; p; p = p->next )
	{
		for( int i = 0; i < p->numverts; i++ )
			VectorSubtract( p->verts[i], RI.cullorigin, verts[i] );
		ClipSkyPolygon( p->numverts, verts[0], 0 );
	}
}

static void R_DrawSkyBox( VkCommandBuffer cmd )
{
	vk_world_push_t push = { 0 };
	float verts[SKYBOX_MAX_SIDES * 6][VERTEXSIZE];
	int first[SKYBOX_MAX_SIDES], num_verts = 0;
	VkBuffer buffer;
	VkDeviceSize offset;
	void *dst;

	for( int i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		float quad[4][VERTEXSIZE];

		first[i] = -1;

		if( RI.skyMins[0][i] >= RI.skyMaxs[0][i] || RI.skyMins[1][i] >= RI.skyMaxs[1][i] )
			continue;

		MakeSkyVec( RI.skyMins[0][i], RI.skyMins[1][i], i, quad[0] );
		MakeSkyVec( RI.skyMins[0][i], RI.skyMaxs[1][i], i, quad[1] );
		MakeSkyVec( RI.skyMaxs[0][i], RI.skyMaxs[1][i], i, quad[2] );
		MakeSkyVec( RI.skyMaxs[0][i], RI.skyMins[1][i], i, quad[3] );

		// GL_QUADS
		first[i] = num_verts;
		memcpy( verts[num_verts++], quad[0], sizeof( quad[0] ));
		memcpy( verts[num_verts++], quad[1], sizeof( quad[0] ));
		memcpy( verts[num_verts++], quad[2], sizeof( quad[0] ));
		memcpy( verts[num_verts++], quad[0], sizeof( quad[0] ));
		memcpy( verts[num_verts++], quad[2], sizeof( quad[0] ));
		memcpy( verts[num_verts++], quad[3], sizeof( quad[0] ));
	}

	if( !num_verts || !( dst = VK_DynamicAlloc( num_verts * sizeof( verts[0] ), 16, &buffer, &offset )))
		return;

	memcpy( dst, verts, num_verts * sizeof( verts[0] ));

	VK_BeginLabel( cmd, "skybox" );
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( VK_BLEND_NONE, true, false ));
	vkCmdBindVertexBuffers( cmd, 0, 1, &buffer, &offset );

	VK_ComputeMVP( push.mvp, RI.worldviewMatrix );
	push.mode = VK_WORLD_MODE_TEXTURE; // GL_REPLACE
	Vector4Set( push.color, 1.0f, 1.0f, 1.0f, 1.0f );

	for( int i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		int texnum;

		if( first[i] < 0 )
			continue;

		texnum = tr.skyboxTextures[r_skyTexOrder[i]];
		if( !texnum )
			texnum = GL_FindTexture( REF_GRAY_TEXTURE ); // stub

		push.texture = VK_TextureForDraw( texnum );
		vkCmdPushConstants( cmd, vk_pipe.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
		vkCmdDraw( cmd, 6, 1, first[i], 0 );
	}

	VK_EndLabel( cmd );
}

static void R_UnloadSkybox( void )
{
	for( int i = 0; i < SKYBOX_MAX_SIDES; i++ )
	{
		if( tr.skyboxTextures[i] )
			GL_FreeTexture( tr.skyboxTextures[i] );
	}

	memset( tr.skyboxTextures, 0, sizeof( tr.skyboxTextures ));

	if( tr.world )
		ClearBits( tr.world->flags, FWORLD_CUSTOM_SKYBOX );
}

void R_SetupSky( int *skyboxTextures )
{
	R_UnloadSkybox();

	if( !skyboxTextures )
		return;

	for( int i = 0; i < SKYBOX_MAX_SIDES; i++ )
		tr.skyboxTextures[i] = skyboxTextures[i];
}

void R_SetSkyCloudsTextures( int solidskyTexture, int alphaskyTexture )
{
	tr.solidskyTexture = solidskyTexture;
	tr.alphaskyTexture = alphaskyTexture;
}

intptr_t VK_SkyParm( int parm, int arg )
{
	if( parm == PARM_TEX_SKYBOX )
		return tr.skyboxTextures[bound( 0, arg, SKYBOX_MAX_SIDES - 1 )];

	return tr.skytexturenum; // PARM_TEX_SKYTEXNUM
}

/*
=================
Drawing
=================
*/
static msurface_t *lightmap_surfaces[MAX_LIGHTMAPS]; // gl_lms.lightmap_surfaces

// the GL state of a brush surface batch
typedef struct vk_brush_state_s
{
	vk_blend_t blend;
	qboolean depth_write;
	int mode;           // VK_WORLD_MODE_TEXTURE or VK_WORLD_MODE_COLOR
	vec4_t color;
	qboolean alpha_test;
	float alpha_ref;
	qboolean lightmaps; // R_HasLightmap
	vk_blend_t lightmap_blend;
	qboolean lightmap_equal;
} vk_brush_state_t;

static void VK_PushWorld( VkCommandBuffer cmd, vk_world_push_t *push )
{
	vkCmdPushConstants( cmd, vk_pipe.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof( *push ), push );
}

static void VK_DrawSurface( VkCommandBuffer cmd, int surfnum )
{
	if( vk_wgeo.num_indices[surfnum] )
		vkCmdDrawIndexed( cmd, vk_wgeo.num_indices[surfnum], 1, vk_wgeo.first_index[surfnum], 0, 0 );
}

// R_HasLightmap of ref_gl
static qboolean R_HasLightmap( void )
{
	if( r_fullbright->value || !WORLDMODEL->lightdata )
		return false;

	if( RI.currententity )
	{
		if( FBitSet( RI.currententity->curstate.effects, EF_FULLBRIGHT ))
			return false;

		switch( RI.currententity->curstate.rendermode )
		{
		case kRenderTransTexture:
			return FBitSet( RI.currentmodel->flags, MODEL_LIQUID ) ? true : false;
		case kRenderTransColor:
		case kRenderTransAdd:
		case kRenderGlow:
			return false;
		}
	}

	return true;
}

static void VK_BindBrushGeometry( VkCommandBuffer cmd )
{
	VkDeviceSize offset = 0;

	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.layout, 0, 1, &vk_pipe.texture_set, 0, NULL );
	vkCmdBindVertexBuffers( cmd, 0, 1, &vk_wgeo.vertices.buffer, &offset );
	vkCmdBindIndexBuffer( cmd, vk_wgeo.indices.buffer, 0, VK_INDEX_TYPE_UINT32 );
}

/*
=================
VK_DrawBrushSurfaces

R_RenderBrushPoly for a list of surfaces then R_BlendLightmaps, ref_gl's
two passes, with the GL state of the current entity
=================
*/
// DrawGLPoly: SURF_CONVEYOR texture scroll, speed in the entity colour
static void R_ConveyorOffset( int texnum, float *sOffset, float *tOffset )
{
	const cl_entity_t *e = RI.currententity;
	float flConveyorSpeed;
	float sy, cy;

	if( e == CL_GetEntityByIndex( 0 ) && FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
	{
		// same as doom speed
		flConveyorSpeed = -35.0f;
	}
	else
	{
		flConveyorSpeed = ( e->curstate.rendercolor.g << 8 | e->curstate.rendercolor.b ) / 16.0f;
		if( e->curstate.rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
	}

	float flRate = fabs( flConveyorSpeed ) / (float)VK_TextureParm( PARM_TEX_SRC_WIDTH, texnum );
	float flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;

	SinCos( flAngle * ( M_PI_F / 180.0f ), &sy, &cy );
	*sOffset = gp_cl->time * cy * flRate;
	*tOffset = gp_cl->time * sy * flRate;

	// make sure that we are positive
	if( *sOffset < 0.0f ) *sOffset += 1.0f + -(int)*sOffset;
	if( *tOffset < 0.0f ) *tOffset += 1.0f + -(int)*tOffset;

	// make sure that we are in a [0,1] range
	*sOffset = *sOffset - (int)*sOffset;
	*tOffset = *tOffset - (int)*tOffset;
}

static qboolean Mod_HaveLightmappedWater( void )
{
	// if this flag set, it's level designer's job to ensure water has been properly lit
	if( FBitSet( tr.world->flags, FWORLD_HAS_LITWATER ))
		return true;

	// otherwise, check user preference, as some maps have lightmapped water
	return gl_litwater_force.value ? true : false;
}

/*
=================
VK_DrawWaterSurfaces

EmitWaterPolys and EmitWaterLightPolys: the warp moves with time, so the
vertices are computed every frame like ref_gl does, into dynamic memory.
Only the SURF_DRAWTURB surfaces of the list are drawn. r_ripple isn't
supported (ref_gl's default is off).
=================
*/
static void VK_DrawWaterSurfaces( VkCommandBuffer cmd, msurface_t **surfs, int count, const vk_brush_state_t *st, const matrix4x4 modelview, qboolean check_backside )
{
	vk_world_push_t push = { 0 };
	const qboolean lit = st->lightmaps && Mod_HaveLightmappedWater( );
	qboolean any = false;

	VK_ComputeMVP( push.mvp, modelview );
	Vector4Copy( st->color, push.color );
	push.alpha_test = st->alpha_test;
	push.alpha_ref = st->alpha_ref;

	for( int i = 0; i < count; i++ )
	{
		msurface_t *warp = surfs[i];
		float waveHeight, *dst, *out;
		VkBuffer buffer;
		VkDeviceSize offset;
		int num_verts = 0;

		if( !FBitSet( warp->flags, SURF_DRAWTURB ) || !warp->polys )
			continue;

		// a backside surface of an entity is drawn reversed (R_RenderBrushPoly)
		const qboolean reverse = check_backside && R_CullSurface( warp, &RI.frustum, 0 ) == CULL_BACKSIDE;

		for( glpoly2_t *p = warp->polys; p; p = p->next )
			num_verts += ( p->numverts - 2 ) * 3;

		if( num_verts <= 0 || !( dst = VK_DynamicAlloc( num_verts * VERTEXSIZE * sizeof( float ), 16, &buffer, &offset )))
			continue;

		// set the current waveheight
		if( warp->polys->verts[0][2] >= RI.rvp.vieworigin[2] )
			waveHeight = -RI.currententity->curstate.scale;
		else waveHeight = RI.currententity->curstate.scale;

		out = dst;

		for( glpoly2_t *p = warp->polys; p; p = p->next )
		{
			float fan[3][VERTEXSIZE];

			for( int j = 0; j < p->numverts; j++ )
			{
				const float *v = reverse ? p->verts[p->numverts - 1 - j] : p->verts[j];
				float nv, s, t;

				if( waveHeight )
				{
					nv = r_turbsin[(int)( gp_cl->time * 160.0f + v[1] + v[0] ) & 255] + 8.0f;
					nv = ( r_turbsin[(int)( v[0] * 5.0f + gp_cl->time * 171.0f - v[1] ) & 255] + 8.0f ) * 0.8f + nv;
					nv = nv * waveHeight + v[2];
				}
				else nv = v[2];

				float os = v[3];
				float ot = v[4];

				s = os + r_turbsin[(int)(( ot * 0.125f + gp_cl->time ) * TURBSCALE ) & 255];
				t = ot + r_turbsin[(int)(( os * 0.125f + gp_cl->time ) * TURBSCALE ) & 255];

				s *= ( 1.0f / SUBDIVIDE_SIZE );
				t *= ( 1.0f / SUBDIVIDE_SIZE );

				float *w = fan[j < 2 ? j : 2];
				w[0] = v[0]; w[1] = v[1]; w[2] = nv;
				w[3] = s; w[4] = t;
				w[5] = v[5]; w[6] = v[6];

				// GL_POLYGON is a fan
				if( j >= 2 )
				{
					memcpy( out, fan[0], sizeof( fan[0] )); out += VERTEXSIZE;
					memcpy( out, fan[1], sizeof( fan[0] )); out += VERTEXSIZE;
					memcpy( out, fan[2], sizeof( fan[0] )); out += VERTEXSIZE;
					memcpy( fan[1], fan[2], sizeof( fan[0] ));
				}
			}
		}

		if( !any )
		{
			VK_BeginLabel( cmd, "water" );
			any = true;
		}

		vkCmdBindVertexBuffers( cmd, 0, 1, &buffer, &offset );

		// warp texture
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( st->blend, st->depth_write, false ));
		push.mode = st->mode;
		push.texture = VK_TextureForDraw( R_TextureAnimation( warp )->gl_texturenum );
		push.alpha_test = st->alpha_test;
		VK_PushWorld( cmd, &push );
		vkCmdDraw( cmd, num_verts, 1, 0, 0 );

		// add lightmaps if requested, over the same moved vertices
		if( lit && !FBitSet( warp->flags, SURF_DRAWTILED ))
		{
			R_UpdateSurfaceLightmap( warp, warp - WORLDMODEL->surfaces );

			if( r_lightmap->value )
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( VK_BLEND_NONE, false, st->lightmap_equal ));
			else
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( st->lightmap_blend, false, st->lightmap_equal ));

			push.mode = VK_WORLD_MODE_LIGHTMAP;
			push.scale = gl_overbright.value && st->lightmap_equal ? 128.0f / 192.0f : 1.0f;
			push.lightmap = VK_TextureForDraw( tr.lightmapTextures[warp->lightmaptexturenum] );
			push.alpha_test = false;
			VK_PushWorld( cmd, &push );
			vkCmdDraw( cmd, num_verts, 1, 0, 0 );
		}
	}

	if( any )
	{
		VK_BindBrushGeometry( cmd );
		VK_EndLabel( cmd );
	}
}

// glPolygonOffset of the brush draws, restored after the decals
static float vk_brush_offset_factor, vk_brush_offset_units;

static void VK_SetBrushOffset( VkCommandBuffer cmd, float factor, float units )
{
	vk_brush_offset_factor = factor;
	vk_brush_offset_units = units;
	vkCmdSetDepthBias( cmd, units, 0.0f, factor );
}

// the decals went through the immediate mode: rebind the brush state
static void VK_ResumeBrushDraws( VkCommandBuffer cmd, const vk_brush_state_t *st, vk_world_push_t *push )
{
	VK_ImmFlush();
	VK_BindBrushGeometry( cmd );
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( st->blend, st->depth_write, false ));
	vkCmdSetDepthBias( cmd, vk_brush_offset_units, 0.0f, vk_brush_offset_factor );
	push->texture = -1; // the push constants changed too

	if( st->mode == VK_WORLD_MODE_COLOR )
		VK_PushWorld( cmd, push );
}

static void VK_DrawBrushSurfaces( VkCommandBuffer cmd, msurface_t **surfs, int count, const vk_brush_state_t *st, const matrix4x4 modelview )
{
	vk_world_push_t push = { 0 };
	qboolean any_lightmap = false;

	memset( lightmap_surfaces, 0, sizeof( lightmap_surfaces ));

	VK_ComputeMVP( push.mvp, modelview );
	push.mode = st->mode;
	Vector4Copy( st->color, push.color );
	push.alpha_test = st->alpha_test;
	push.alpha_ref = st->alpha_ref;
	push.texture = -1;

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( st->blend, st->depth_write, false ));

	if( st->mode == VK_WORLD_MODE_COLOR )
		VK_PushWorld( cmd, &push );

	for( int i = 0; i < count; i++ )
	{
		msurface_t *s = surfs[i];
		const int surfnum = s - WORLDMODEL->surfaces;

		// water comes with its own pass
		if( FBitSet( s->flags, SURF_DRAWTURB|SURF_DRAWSKY ))
			continue;

		if( st->mode == VK_WORLD_MODE_TEXTURE )
		{
			const int gl_texnum = R_TextureAnimation( s )->gl_texturenum;
			const int texnum = VK_TextureForDraw( gl_texnum );
			float sOffset = 0.0f, tOffset = 0.0f;

			if( FBitSet( s->flags, SURF_CONVEYOR ))
				R_ConveyorOffset( gl_texnum, &sOffset, &tOffset );

			if( texnum != push.texture || sOffset != push.tcoffset[0] || tOffset != push.tcoffset[1] )
			{
				push.texture = texnum;
				push.tcoffset[0] = sOffset;
				push.tcoffset[1] = tOffset;
				VK_PushWorld( cmd, &push );
			}
		}

		VK_DrawSurface( cmd, surfnum );

		// R_RenderDecalsForSurface
		if( s->pdecals )
		{
			if( RI.currententity->curstate.rendermode == kRenderNormal )
			{
				// batch decals to draw later
				if( tr.num_draw_decals < MAX_DECAL_SURFS )
					tr.draw_decals[tr.num_draw_decals++] = s;
			}
			else
			{
				// if rendermode != kRenderNormal draw decals sequentially
				GL_Bind( XASH_TEXTURE0, R_TextureAnimation( s )->gl_texturenum );
				DrawSurfaceDecals( s, true, R_CullSurface( s, &RI.frustum, 0 ) == CULL_BACKSIDE );
				VK_ResumeBrushDraws( cmd, st, &push );
			}
		}

		// R_RenderLightmapForSurface
		if( st->lightmaps && s->polys && !FBitSet( s->flags, SURF_DRAWTILED ))
		{
			R_UpdateSurfaceLightmap( s, surfnum );
			s->info->lightmapchain = lightmap_surfaces[s->lightmaptexturenum];
			lightmap_surfaces[s->lightmaptexturenum] = s;
			any_lightmap = true;
		}
	}

	// before the lightmaps, which light them like the surface
	if( tr.num_draw_decals )
	{
		if( RI.currententity->curstate.rendermode == kRenderTransColor )
			VK_ImmTexture2D( true );

		DrawDecalsBatch();
		VK_ResumeBrushDraws( cmd, st, &push );
	}

	if( !any_lightmap )
		return;

	if( r_lightmap->value )
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( VK_BLEND_NONE, false, st->lightmap_equal ));
	else
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_WorldPipeline( st->lightmap_blend, false, st->lightmap_equal ));

	push.mode = VK_WORLD_MODE_LIGHTMAP;
	push.scale = gl_overbright.value && st->lightmap_equal ? 128.0f / 192.0f : 1.0f;
	push.alpha_test = false;
	push.tcoffset[0] = push.tcoffset[1] = 0.0f;

	for( int i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !lightmap_surfaces[i] )
			continue;

		push.lightmap = VK_TextureForDraw( tr.lightmapTextures[i] );
		VK_PushWorld( cmd, &push );

		for( msurface_t *surf = lightmap_surfaces[i]; surf != NULL; surf = surf->info->lightmapchain )
			VK_DrawSurface( cmd, surf - WORLDMODEL->surfaces );
	}
}

static void VK_SetSolidLightmapBlend( vk_brush_state_t *st )
{
	st->lightmap_blend = gl_overbright.value ? VK_BLEND_OVERBRIGHT : VK_BLEND_MODULATE;
	st->lightmap_equal = true;
}

/*
=================
World surfaces: R_DrawTextureChains then R_BlendLightmaps
=================
*/
static msurface_t **vk_draw_surfs;
static int vk_max_draw_surfs;
static msurface_t **vk_alpha_water; // draw_wateralpha
static int vk_num_alpha_water;

static void VK_DrawWorldSurfaces( VkCommandBuffer cmd )
{
	vk_brush_state_t st = { 0 };
	int count = 0;

	if( !vk_wgeo.vertices.buffer )
		return;

	if( vk_max_draw_surfs < WORLDMODEL->numsurfaces )
	{
		if( vk_draw_surfs )
			Mem_Free( vk_draw_surfs );
		vk_max_draw_surfs = WORLDMODEL->numsurfaces;
		vk_draw_surfs = Mem_Malloc( r_temppool, sizeof( *vk_draw_surfs ) * vk_max_draw_surfs );
		if( vk_alpha_water )
			Mem_Free( vk_alpha_water );
		vk_alpha_water = Mem_Malloc( r_temppool, sizeof( *vk_alpha_water ) * vk_max_draw_surfs );
	}

	vk_num_alpha_water = 0;

	// texture chains, in texture order
	for( int i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		texture_t *t = WORLDMODEL->textures[i];

		if( !t || !t->texturechain || i == tr.skytexturenum )
			continue;

		// translucent water is drawn after the entities (R_DrawWaterSurfaces)
		const qboolean alpha_water = FBitSet( t->texturechain->flags, SURF_DRAWTURB ) && gp_movevars->wateralpha < 1.0f;

		for( msurface_t *s = t->texturechain; s != NULL && count < vk_max_draw_surfs; s = s->texturechain )
		{
			if( alpha_water )
				vk_alpha_water[vk_num_alpha_water++] = s;
			else vk_draw_surfs[count++] = s;
		}

		t->texturechain = NULL;
	}

	st.blend = VK_BLEND_NONE;
	st.depth_write = true;
	st.mode = VK_WORLD_MODE_TEXTURE;
	Vector4Set( st.color, 1.0f, 1.0f, 1.0f, 1.0f );
	st.lightmaps = R_HasLightmap();
	VK_SetSolidLightmapBlend( &st );

	VK_BeginLabel( cmd, "world" );
	VK_BindBrushGeometry( cmd );
	VK_SetBrushOffset( cmd, 0.0f, 0.0f );
	VK_DrawBrushSurfaces( cmd, vk_draw_surfs, count, &st, RI.worldviewMatrix );
	VK_DrawWaterSurfaces( cmd, vk_draw_surfs, count, &st, RI.worldviewMatrix, false );
	VK_EndLabel( cmd );
}

/*
=================
R_DrawWaterSurfaces

the translucent world water, after the entities
=================
*/
static void R_DrawWaterSurfaces( VkCommandBuffer cmd )
{
	vk_brush_state_t st = { 0 };

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ) || !vk_num_alpha_water )
		return;

	VK_ImmFlush();

	// restore worldmodel
	RI.currententity = CL_GetEntityByIndex( 0 );
	RI.currentmodel = RI.currententity->model;

	// GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA without depth writes, GL_MODULATE by wateralpha
	st.blend = VK_BLEND_ALPHA;
	st.depth_write = false;
	st.mode = VK_WORLD_MODE_TEXTURE;
	Vector4Set( st.color, 1.0f, 1.0f, 1.0f, gp_movevars->wateralpha );
	st.lightmaps = R_HasLightmap();
	VK_SetSolidLightmapBlend( &st );

	VK_BindBrushGeometry( cmd );
	vkCmdSetDepthBias( cmd, 0.0f, 0.0f, 0.0f );
	VK_DrawWaterSurfaces( cmd, vk_alpha_water, vk_num_alpha_water, &st, RI.worldviewMatrix, false );
	vk_num_alpha_water = 0;
}

static void R_DrawWorld( VkCommandBuffer cmd )
{
	RI.currententity = CL_GetEntityByIndex( 0 );
	if( !RI.currententity )
		return;

	RI.currentmodel = RI.currententity->model;
	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ) || FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		return;

	VectorCopy( RI.cullorigin, tr.modelorg );
	Matrix4x4_LoadIdentity( RI.objectMatrix );
	tr.modelviewIdentity = true;
	tr.skychain = NULL;
	R_ClearSkyBox();

	R_RecursiveWorldNode( WORLDMODEL->nodes, RI.frustum.clipFlags );

	// R_DrawTextureChains clips the sky surfaces first
	for( msurface_t *s = tr.skychain; s != NULL; s = s->texturechain )
		R_AddSkyBoxSurface( s );

	VK_DrawWorldSurfaces( cmd );

	if( tr.skychain && !ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		R_DrawSkyBox( cmd );

	tr.skychain = NULL;
	gEngfuncs.R_DrawWorldHull();
}

/*
=================
Entity lists (gl_rmain.c)
=================
*/
#define MAX_DRAW_STACK 2 // normal view and menu view

static vk_draw_list_t vk_draw_stack[MAX_DRAW_STACK];
static int vk_draw_stack_pos;


int CL_FxBlend( cl_entity_t *e )
{
	int blend = 0;
	float offset = ((int)e->index ) * 363.0f; // Use ent index to de-sync these fx

	switch( e->curstate.renderfx )
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 2 + offset );
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin( gp_cl->time * 8 + offset );
		break;
	case kRenderFxFadeSlow:
		if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		{
			if( e->curstate.renderamt > 0 )
				e->curstate.renderamt -= 1;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		{
			if( e->curstate.renderamt > 3 )
				e->curstate.renderamt -= 4;
			else e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		{
			if( e->curstate.renderamt < 255 )
				e->curstate.renderamt += 1;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		{
			if( e->curstate.renderamt < 252 )
				e->curstate.renderamt += 4;
			else e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin( gp_cl->time * 4 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin( gp_cl->time * 16 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin( gp_cl->time * 36 + offset );
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * ( sin( gp_cl->time * 2 ) + sin( gp_cl->time * 17 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * ( sin( gp_cl->time * 16 ) + sin( gp_cl->time * 23 + offset ));
		if( blend < 0 ) blend = 0;
		else blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
	{
		vec3_t tmp = Vec3( e->origin );
		VectorSubtract( tmp, RI.rvp.vieworigin, tmp );
		float dist = DotProduct( tmp, RI.vforward );

		// turn off distance fade
		if( e->curstate.renderfx == kRenderFxDistort )
			dist = 1;

		if( dist <= 0 )
			blend = 0;
		else
		{
			e->curstate.renderamt = 180;
			if( dist <= 100 ) blend = e->curstate.renderamt;
			else blend = (int)(( 1.0f - ( dist - 100 ) * ( 1.0f / 400.0f )) * e->curstate.renderamt );
			blend += gEngfuncs.COM_RandomLong( -32, 31 );
		}
		break;
	}
	default:
		blend = e->curstate.renderamt;
		break;
	}

	return bound( 0, blend, 255 );
}


static qboolean R_OpaqueEntity( cl_entity_t *ent )
{
	if( R_GetEntityRenderMode( ent ) == kRenderNormal )
	{
		switch( ent->curstate.renderfx )
		{
		case kRenderFxNone:
		case kRenderFxDeadPlayer:
		case kRenderFxLightMultiplier:
		case kRenderFxExplode:
			return true;
		}
	}
	return false;
}

static int R_RankForRenderMode( int rendermode )
{
	switch( rendermode )
	{
	case kRenderTransTexture:
		return 1; // draw second
	case kRenderTransAdd:
		return 2; // draw third
	case kRenderGlow:
		return 3; // must be last!
	}
	return 0;
}

static int R_TransEntityCompare( const void *a, const void *b )
{
	cl_entity_t *ent1 = *(cl_entity_t **)a;
	cl_entity_t *ent2 = *(cl_entity_t **)b;
	int rendermode1 = R_GetEntityRenderMode( ent1 );
	int rendermode2 = R_GetEntityRenderMode( ent2 );
	vec3_t vecLen, org;
	float dist1, dist2;

	if( ent1->model->type != mod_brush || rendermode1 != kRenderTransAlpha )
	{
		VectorAverage( ent1->model->mins, ent1->model->maxs, org );
		VectorAdd( ent1->origin, org, org );
		VectorSubtract( RI.rvp.vieworigin, org, vecLen );
		dist1 = DotProduct( vecLen, vecLen );
	}
	else dist1 = 1000000000;

	if( ent2->model->type != mod_brush || rendermode2 != kRenderTransAlpha )
	{
		VectorAverage( ent2->model->mins, ent2->model->maxs, org );
		VectorAdd( ent2->origin, org, org );
		VectorSubtract( RI.rvp.vieworigin, org, vecLen );
		dist2 = DotProduct( vecLen, vecLen );
	}
	else dist2 = 1000000000;

	if( dist1 > dist2 )
		return -1;
	if( dist1 < dist2 )
		return 1;

	if( R_RankForRenderMode( rendermode1 ) > R_RankForRenderMode( rendermode2 ))
		return 1;
	if( R_RankForRenderMode( rendermode1 ) < R_RankForRenderMode( rendermode2 ))
		return -1;

	return 0;
}

void R_PushScene( void )
{
	if( ++vk_draw_stack_pos >= MAX_DRAW_STACK )
		gEngfuncs.Host_Error( "draw stack overflow\n" );

	tr.draw_list = &vk_draw_stack[vk_draw_stack_pos];
}

void R_PopScene( void )
{
	if( --vk_draw_stack_pos < 0 )
		gEngfuncs.Host_Error( "draw stack underflow\n" );

	tr.draw_list = &vk_draw_stack[vk_draw_stack_pos];
}

void R_ClearScene( void )
{
	tr.draw_list->num_solid_entities = 0;
	tr.draw_list->num_trans_entities = 0;
	tr.draw_list->num_beam_entities = 0;

	if( gEngfuncs.drawFuncs->R_ClearScene != NULL )
		gEngfuncs.drawFuncs->R_ClearScene();
}

qboolean R_AddEntity( struct cl_entity_s *clent, int type )
{
	if( !r_drawentities->value )
		return false;

	if( FBitSet( clent->curstate.effects, EF_NODRAW ))
		return false;

	if( !R_ModelOpaque( clent->curstate.rendermode ) && CL_FxBlend( clent ) <= 0 )
		return true; // invisible

	if( type == ET_BEAM )
	{
		if( tr.draw_list->num_beam_entities >= MAX_VISIBLE_PACKET )
		{
			gEngfuncs.Con_Printf( S_ERROR "Too many beams %d!\n", tr.draw_list->num_beam_entities );
			return false;
		}

		tr.draw_list->beam_entities[tr.draw_list->num_beam_entities++] = clent;
		return true;
	}

	if( R_OpaqueEntity( clent ))
	{
		if( tr.draw_list->num_solid_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->solid_entities[tr.draw_list->num_solid_entities++] = clent;
	}
	else
	{
		if( tr.draw_list->num_trans_entities >= MAX_VISIBLE_PACKET )
			return false;

		tr.draw_list->trans_entities[tr.draw_list->num_trans_entities++] = clent;
	}

	return true;
}

/*
=================
Brush entities (R_DrawBrushModel)
=================
*/
// R_SetRenderMode of gl_rsurf.c: the GL state the decals of the entity
// inherit, set on the immediate mode
static void R_SetRenderModeImm( cl_entity_t *e )
{
	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
		VK_ImmColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		break;
	case kRenderTransColor:
		VK_ImmBlendFunc( VK_BLEND_ALPHA );
		VK_ImmColor4ub( e->curstate.rendercolor.r, e->curstate.rendercolor.g, e->curstate.rendercolor.b, e->curstate.renderamt );
		VK_ImmTexReplace( false );
		VK_ImmTexture2D( false );
		VK_ImmBlendEnable( true );
		break;
	case kRenderTransAdd:
		VK_ImmTexReplace( false );
		VK_ImmColor4f( tr.blend, tr.blend, tr.blend, 1.0f );
		VK_ImmBlendFunc( VK_BLEND_ONE_ONE );
		VK_ImmDepthMask( false );
		VK_ImmBlendEnable( true );
		break;
	case kRenderTransAlpha:
		VK_ImmAlphaTestEnable( true );
		VK_ImmTexReplace( false );
		if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
		{
			VK_ImmBlendFunc( VK_BLEND_ALPHA );
			VK_ImmColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
			VK_ImmBlendEnable( true );
		}
		else
		{
			VK_ImmColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
			VK_ImmBlendEnable( false );
		}
		VK_ImmAlphaFunc( 0.25f );
		break;
	default:
		VK_ImmTexReplace( false );
		VK_ImmBlendFunc( VK_BLEND_ALPHA );
		VK_ImmColor4f( 1.0f, 1.0f, 1.0f, tr.blend );
		VK_ImmDepthMask( false );
		VK_ImmBlendEnable( true );
		break;
	}
}

static void R_SetBrushRenderMode( cl_entity_t *e, vk_brush_state_t *st )
{
	memset( st, 0, sizeof( *st ));
	st->mode = VK_WORLD_MODE_TEXTURE;
	st->depth_write = true;
	Vector4Set( st->color, 1.0f, 1.0f, 1.0f, 1.0f );
	st->lightmaps = R_HasLightmap();
	VK_SetSolidLightmapBlend( st );

	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
		break;
	case kRenderTransColor:
		st->blend = VK_BLEND_ALPHA;
		st->mode = VK_WORLD_MODE_COLOR; // GL_TEXTURE_2D disabled
		Vector4Set( st->color, e->curstate.rendercolor.r / 255.0f, e->curstate.rendercolor.g / 255.0f,
			e->curstate.rendercolor.b / 255.0f, e->curstate.renderamt / 255.0f );
		break;
	case kRenderTransAdd:
		st->blend = VK_BLEND_ONE_ONE;
		st->depth_write = false;
		Vector4Set( st->color, tr.blend, tr.blend, tr.blend, 1.0f );
		break;
	case kRenderTransAlpha:
		st->alpha_test = true;
		st->alpha_ref = 0.25f;
		if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
		{
			st->blend = VK_BLEND_ALPHA;
			st->color[3] = tr.blend;
		}
		break;
	default:
		st->blend = VK_BLEND_ALPHA;
		st->depth_write = false;
		st->color[3] = tr.blend;
		break;
	}

	// R_BlendLightmaps of a translucent liquid: no GL_EQUAL, no depth write
	if( e->curstate.rendermode == kRenderTransTexture )
	{
		st->lightmap_blend = VK_BLEND_DSTCOLOR_INVSRCALPHA;
		st->lightmap_equal = false;
	}
}

static int R_SurfaceCompare( const void *a, const void *b )
{
	const msurface_t *surf1 = *(const msurface_t **)a;
	const msurface_t *surf2 = *(const msurface_t **)b;
	vec3_t org1, org2;

	VectorAdd( RI.currententity->origin, surf1->info->origin, org1 );
	VectorAdd( RI.currententity->origin, surf2->info->origin, org2 );

	float len1 = DotProduct( org1, RI.vforward ) - RI.viewplanedist;
	float len2 = DotProduct( org2, RI.vforward ) - RI.viewplanedist;

	if( len1 > len2 )
		return -1;
	if( len1 < len2 )
		return 1;

	return 0;
}

static void R_DrawBrushModel( VkCommandBuffer cmd, cl_entity_t *e )
{
	vec3_t mins, maxs;
	qboolean rotated;
	matrix4x4 modelview;
	vk_brush_state_t st;
	int num_sorted = 0;
	model_t *clmodel = e->model;

	if( !FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		return;

	// studio models and sprites drawn before stay before, and leave their
	// own vertex buffer bound
	VK_ImmFlush();
	VK_BindBrushGeometry( cmd );

	// models from another bsp aren't in the world buffers
	if( clmodel->surfaces != WORLDMODEL->surfaces )
	{
		VK_STUB();
		return;
	}

	if( !VectorIsNull( e->angles ))
	{
		for( int i = 0; i < 3; i++ )
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
		rotated = true;
	}
	else
	{
		VectorAdd( e->origin, clmodel->mins, mins );
		VectorAdd( e->origin, clmodel->maxs, maxs );
		rotated = false;
	}

	if( VK_FrustumCullBox( &RI.frustum, mins, maxs, 0 ))
	{
		return;
	}

	int old_rendermode = e->curstate.rendermode;

	// R_RotateForEntity / R_TranslateForEntity
	if( rotated )
		Matrix4x4_CreateFromEntity( RI.objectMatrix, e->angles, e->origin, 1.0f );
	else
		Matrix4x4_CreateFromEntity( RI.objectMatrix, vec3_origin, e->origin, 1.0f );
	Matrix4x4_ConcatTransforms( modelview, RI.worldviewMatrix, RI.objectMatrix );
	Vector4Set( modelview[3], 0.0f, 0.0f, 0.0f, 1.0f ); // ConcatTransforms only writes the 3x4 part
	tr.modelviewIdentity = false;

	if( rotated )
		Matrix4x4_VectorITransform( RI.objectMatrix, RI.cullorigin, tr.modelorg );
	else
		VectorSubtract( RI.cullorigin, e->origin, tr.modelorg );

	if( FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ) && FBitSet( clmodel->flags, MODEL_TRANSPARENT ))
		e->curstate.rendermode = kRenderTransAlpha;

	e->visframe = tr.realframecount;

	R_PushDlightsForBmodel( clmodel, tr.dlightframecount, RI.objectMatrix );
	R_SetBrushRenderMode( e, &st );

	// R_SortBrushModelSurfaces
	for( int i = 0; i < clmodel->nummodelsurfaces; i++ )
	{
		msurface_t *psurf = &clmodel->surfaces[clmodel->firstmodelsurface + i];

		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !FBitSet( gp_host->features, ENGINE_QUAKE_COMPATIBLE ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( e->curstate.effects, EF_WATERSIDES ))
				continue;

			if( mins[2] + 1.0f >= psurf->plane->dist )
				continue;
		}

		int cull_type = R_CullSurface( psurf, &RI.frustum, RI.frustum.clipFlags );

		if( cull_type >= CULL_FRUSTUM )
			continue;

		if( cull_type == CULL_BACKSIDE && !FBitSet( psurf->flags, SURF_DRAWTURB )
			&& !( psurf->pdecals && e->curstate.rendermode == kRenderTransTexture ))
			continue;

		if( num_sorted < vk_max_draw_surfs )
			vk_draw_surfs[num_sorted++] = psurf;
	}

	if( !FBitSet( clmodel->flags, MODEL_LIQUID ) && e->curstate.rendermode == kRenderTransTexture )
		qsort( vk_draw_surfs, num_sorted, sizeof( *vk_draw_surfs ), R_SurfaceCompare );

	
	// draw bmodels with a polyoffset to avoid flickering when they are too
	// close to world; the decals push theirs on top
	if( gl_polyoffset_bmodels.value )
	{
		GL_PushPolygonOffset( -0.5f, -gl_polyoffset_bmodels.value );
		VK_SetBrushOffset( cmd, -0.5f, -gl_polyoffset_bmodels.value );
	}
	else VK_SetBrushOffset( cmd, 0.0f, 0.0f );

	R_SetRenderModeImm( e );
	VK_DrawBrushSurfaces( cmd, vk_draw_surfs, num_sorted, &st, modelview );
	VK_DrawWaterSurfaces( cmd, vk_draw_surfs, num_sorted, &st, modelview, true );

	if( gl_polyoffset_bmodels.value )
		GL_PopPolygonOffset();
	VK_SetBrushOffset( cmd, 0.0f, 0.0f );

	// the GL state R_DrawBrushModel leaves
	VK_ImmAlphaTestEnable( false );
	VK_ImmAlphaFunc( DEFAULT_ALPHATEST );
	VK_ImmBlendEnable( false );
	VK_ImmDepthMask( true );
	VK_ImmTexture2D( true );

	e->curstate.rendermode = old_rendermode;
	tr.modelviewIdentity = true;
	Matrix4x4_LoadIdentity( RI.objectMatrix );
}

/*
=================
R_DrawEntitiesOnList
=================
*/
static void R_DrawEntitiesOnList( VkCommandBuffer cmd )
{
	const double frametime = tr.frametime;

	tr.blend = 1.0f;
	VK_BindBrushGeometry( cmd );

	for( uint i = 0; i < tr.draw_list->num_solid_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( !RI.currentmodel )
			continue;

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( cmd, RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			VK_ImmFlush(); // keep the draw order with the brush entities
			VK_BindBrushGeometry( cmd );
			break;
		default:
			break;
		}
	}

	// draw sprites seperately, because of alpha blending
	for( uint i = 0; i < tr.draw_list->num_solid_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->solid_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( RI.currentmodel && RI.currentmodel->type == mod_sprite )
			R_DrawSpriteModel( RI.currententity );
	}

	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		gEngfuncs.CL_DrawEFX( frametime, false );

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.pfnDrawNormalTriangles();

	VK_BindBrushGeometry( cmd );

	for( uint i = 0; i < tr.draw_list->num_trans_entities && !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ); i++ )
	{
		RI.currententity = tr.draw_list->trans_entities[i];
		RI.currentmodel = RI.currententity->model;

		if( RI.currententity->curstate.rendermode != kRenderNormal )
			tr.blend = CL_FxBlend( RI.currententity ) / 255.0f;
		else tr.blend = 1.0f;

		if( tr.blend <= 0.0f || !RI.currentmodel )
			continue;

		switch( RI.currentmodel->type )
		{
		case mod_brush:
			R_DrawBrushModel( cmd, RI.currententity );
			break;
		case mod_studio:
			R_DrawStudioModel( RI.currententity );
			VK_ImmFlush(); // keep the draw order with the brush entities
			VK_BindBrushGeometry( cmd );
			break;
		case mod_sprite:
			R_DrawSpriteModel( RI.currententity );
			break;
		default:
			break;
		}
	}

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
	{
		VK_ImmTexReplace( false );
		gEngfuncs.pfnDrawTransparentTriangles();
	}

	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		gEngfuncs.CL_DrawEFX( frametime, true );

	VK_ImmBlendEnable( false ); // Trinity Render issues

	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		R_DrawViewModel();
	gEngfuncs.CL_ExtraUpdate();
}

static void R_RenderScene( void )
{
	VkCommandBuffer cmd;
	int x, y, w, h;

	if( !WORLDMODEL && FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	cmd = VK_BeginMainPass();
	if( !cmd )
		return;

	// frametime is valid only for normal pass
	if( !FBitSet( RI.rvp.flags, RF_DRAW_CUBEMAP ))
		tr.frametime = gp_cl->time - gp_cl->oldtime;
	else tr.frametime = 0.0;

	tr.framecount++;
	if( WORLDMODEL )
		tr.dlightframecount = R_PushDlights( WORLDMODEL, tr.framecount );

	R_SetupFrustum();

	// R_SetupFrame
	RI.viewplanedist = DotProduct( RI.rvp.vieworigin, RI.vforward );
	qsort( tr.draw_list->trans_entities, tr.draw_list->num_trans_entities, sizeof( cl_entity_t * ), R_TransEntityCompare );

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		R_FindViewLeaf();

	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );
	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );

	// R_SetupGL: viewport in GL window coordinates, scaled to the target
	{
		int scene_width, scene_height;

		VK_MainExtent( &scene_width, &scene_height );
		x = RI.rvp.viewport[0] * scene_width / gpGlobals->width;
		int x2 = (( RI.rvp.viewport[0] + RI.rvp.viewport[2] ) * scene_width + gpGlobals->width - 1 ) / gpGlobals->width;
		int y_top = scene_height - RI.rvp.viewport[1] * scene_height / gpGlobals->height;
		int y2 = scene_height - (( RI.rvp.viewport[1] + RI.rvp.viewport[3] ) * scene_height + gpGlobals->height - 1 ) / gpGlobals->height;

		y = y2;
		w = x2 - x;
		h = y_top - y2;
	}

	VK_SetViewport( x, y, w, h );
	VK_ImmSet3D( true );

	// R_Clear: depth only, and the clear colour becomes grey (green in overview)
	if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		VK_SetClearColor( 0.0f, 1.0f, 0.0f );
	else VK_SetClearColor( 0.5f, 0.5f, 0.5f );
	VK_ClearMainDepth();

	if( WORLDMODEL )
	{
		R_MarkLeaves();
		R_DrawWorld( cmd );
		R_DrawEntitiesOnList( cmd );
		R_DrawWaterSurfaces( cmd );
	}

	VK_ImmSet3D( false );
	VK_ResetViewport();
}

void GL_RenderFrame( const struct ref_viewpass_s *rvp )
{
	if( r_norefresh->value )
		return;

	RI.rvp = *rvp;
	RI.farClip = 0;

	// completely override rendering
	if( gEngfuncs.drawFuncs->GL_RenderFrame != NULL )
	{
		tr.fCustomRendering = true;

		if( gEngfuncs.drawFuncs->GL_RenderFrame( rvp ))
		{
			R_GatherPlayerLight( tr.viewent );
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	if( !FBitSet( RI.rvp.flags, RF_ONLY_CLIENTDRAW ))
		R_RunViewmodelEvents();

	tr.realframecount++; // right called after viewmodel events
	R_RenderScene();
}

/*
=================
CL_DrawParticlesExternal

render API: effects drawn from a custom renderer's own view
=================
*/
void CL_DrawParticlesExternal( const ref_viewpass_t *rvp, qboolean trans_pass, float frametime )
{
	static vk_refinst_t oldRI;

	oldRI = RI;

	RI.rvp = *rvp;
	RI.farClip = 0;
	R_SetupFrustum();
	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );
	Matrix4x4_Concat( RI.worldviewProjectionMatrix, RI.projectionMatrix, RI.worldviewMatrix );
	VK_ImmLoadModelview( RI.worldviewMatrix );
	tr.frametime = frametime;

	gEngfuncs.CL_DrawEFX( frametime, trans_pass );

	// restore internal state
	RI = oldRI;
	VK_ImmLoadModelview( RI.worldviewMatrix );
}

/*
=================
R_NewMap
=================
*/
void R_NewMap( void )
{
	tr.worldmodel = gp_cl->models[1];
	tr.world = (world_static_t *)ENGINE_GET_PARM( PARM_GET_WORLD_PTR );

	R_ClearDecals(); // clear all level decals
	R_StudioResetPlayerModels();

	for( int i = 0; i < WORLDMODEL->numleafs; i++ )
		WORLDMODEL->leafs[i+1].efrags = NULL;

	tr.skytexturenum = -1;

	for( int i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		texture_t *tx = WORLDMODEL->textures[i];

		if( !tx )
			continue;

		if( !Q_strncmp( tx->name, "sky", 3 ) && tx->width == ( tx->height * 2 ))
			tr.skytexturenum = i;

		tx->texturechain = NULL;
	}

	VK_BuildLightmaps();
	VK_BuildWorldGeometry();

	if( gEngfuncs.drawFuncs->R_NewMap != NULL )
		gEngfuncs.drawFuncs->R_NewMap();
}

// R_CheckCvars of ref_gl
void VK_WorldBeginFrame( void )
{
	if( FBitSet( gl_overbright.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_overbright.flags, FCVAR_CHANGED );
		VK_RebuildLightmaps();
	}
}

void VK_InitWorld( void )
{
	memset( &tr, 0, sizeof( tr ));
	memset( &RI, 0, sizeof( RI ));
	memset( &vk_wgeo, 0, sizeof( vk_wgeo ));
	tr.draw_list = &vk_draw_stack[0];
	tr.viewent = (cl_entity_t *)ENGINE_GET_PARM( PARM_GET_VIEWENT_PTR );
	tr.texgammatable = (byte *)ENGINE_GET_PARM( PARM_GET_TEXGAMMATABLE_PTR );
	tr.lightgammatable = (uint16_t *)ENGINE_GET_PARM( PARM_GET_LIGHTGAMMATABLE_PTR );
	tr.screengammatable = (uint16_t *)ENGINE_GET_PARM( PARM_GET_SCREENGAMMATABLE_PTR );
	tr.lineargammatable = (uint16_t *)ENGINE_GET_PARM( PARM_GET_LINEARGAMMATABLE_PTR );
	tr.elights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_ELIGHTS_PTR );
	tr.palette = (color24 *)ENGINE_GET_PARM( PARM_GET_PALETTE_PTR );
	tr.block_size = BLOCK_SIZE_DEFAULT;

	gEngfuncs.Cvar_RegisterVariable( &gl_overbright );
	gEngfuncs.Cvar_RegisterVariable( &gl_keeptjunctions );
	gEngfuncs.Cvar_RegisterVariable( &gl_litwater_force );
	gEngfuncs.Cvar_RegisterVariable( &gl_polyoffset_bmodels );
	gEngfuncs.Cvar_RegisterVariable( &r_novis );
	gEngfuncs.Cvar_RegisterVariable( &r_nocull );
	gEngfuncs.Cvar_RegisterVariable( &r_lockpvs );
	gEngfuncs.Cvar_RegisterVariable( &r_lockfrustum );
	gEngfuncs.Cvar_RegisterVariable( &r_large_lightmaps );
}

void VK_ShutdownWorld( void )
{
	if( vk.device )
		VK_FreeWorldGeometry();
}
