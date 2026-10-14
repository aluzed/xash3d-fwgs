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
#include "atlas.h"

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
CVAR_DEFINE_AUTO( r_large_lightmaps, "0", FCVAR_GLCONFIG|FCVAR_LATCH, "enable larger lightmap atlas textures (might break custom renderer mods)" );

#define BLOCK_SIZE_DEFAULT 128 // for keep backward compatibility
#define BLOCK_SIZE_MAX     1024
#define MAX_LIGHTMAPS      256
#define SUBDIVIDE_SIZE     64

#define FRUSTUM_LEFT    0
#define FRUSTUM_RIGHT   1
#define FRUSTUM_BOTTOM  2
#define FRUSTUM_TOP     3
#define FRUSTUM_FAR     4
#define FRUSTUM_NEAR    5
#define FRUSTUM_PLANES  6

#define CULL_VISIBLE    0
#define CULL_BACKSIDE   1
#define CULL_FRUSTUM    2
#define CULL_VISFRAME   3
#define CULL_OTHER      4

#define R_StaticEntity( ent ) ( VectorIsNull( ent->origin ) && VectorIsNull( ent->angles ))

typedef struct vk_frustum_s
{
	mplane_t planes[FRUSTUM_PLANES];
	unsigned int clipFlags;
} vk_frustum_t;

// RI of ref_gl
static struct
{
	ref_viewpass_t rvp;
	vec3_t vforward, vright, vup;
	vec3_t cullorigin, cull_vforward, cull_vright, cull_vup;
	vk_frustum_t frustum;
	mleaf_t *viewleaf, *oldviewleaf;
	byte visbytes[(MAX_MAP_LEAFS+7)/8];
	float farClip;
	float viewplanedist;
	matrix4x4 projectionMatrix;
	matrix4x4 worldviewMatrix;
	matrix4x4 objectMatrix;
	cl_entity_t *currententity;
	model_t *currentmodel;
	float skyMins[2][SKYBOX_MAX_SIDES];
	float skyMaxs[2][SKYBOX_MAX_SIDES];
} RI;

// tr of ref_gl
static struct
{
	model_t *worldmodel;
	world_static_t *world;
	cl_entity_t *entities;
	int max_entities;

	int framecount;
	int visframecount;
	int realframecount;
	int dlightframecount;
	qboolean fResetVis;
	qboolean fCustomRendering;
	vec3_t modelorg;

	int skytexturenum;
	int block_size;
	int lightmapTextures[MAX_LIGHTMAPS];
	int current_lightmap_texture;
	atlas_t atlas;
	byte lightmap_buffer[BLOCK_SIZE_MAX * BLOCK_SIZE_MAX * 4];

	msurface_t *skychain;
	vec2_t ortho_mins, ortho_maxs;

	int skyboxTextures[SKYBOX_MAX_SIDES];
	int solidskyTexture, alphaskyTexture; // Quake sky, unused by HL / CS
} tr;

#define WORLDMODEL ( tr.worldmodel )
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

static cl_entity_t *CL_GetEntityByIndex( int index )
{
	if( unlikely( index < 0 || index >= tr.max_entities || !tr.entities ))
		return NULL;

	return &tr.entities[index];
}

static model_t *CL_ModelHandle( int index )
{
	if( unlikely( index < 0 || index >= gp_cl->nummodels ))
		return NULL;

	return gp_cl->models[index];
}

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

/*
=================
Polygons (gl_rsurf.c)
=================
*/
static void R_LightmapCoord( const vec3_t v, const msurface_t *surf, const float sample_size, vec2_t coords )
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

		// world surfaces only for now: no bmodel transform
		VectorCopy( dl->origin, origin_l );

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
static texture_t *R_TextureAnimation( msurface_t *s )
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

static void VK_ComputeMVP( float out[16], const matrix4x4 modelview )
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
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.world_opaque );
	vkCmdBindVertexBuffers( cmd, 0, 1, &buffer, &offset );

	VK_ComputeMVP( push.mvp, RI.worldviewMatrix );
	push.mode = VK_WORLD_MODE_TEXTURE; // GL_REPLACE

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

// R_HasLightmap of ref_gl, world entity
static qboolean R_HasLightmap( void )
{
	if( r_fullbright->value || !WORLDMODEL->lightdata )
		return false;

	if( RI.currententity && FBitSet( RI.currententity->curstate.effects, EF_FULLBRIGHT ))
		return false;

	return true;
}

/*
=================
VK_DrawWorldSurfaces

R_DrawTextureChains then R_BlendLightmaps of ref_gl
=================
*/
static void VK_DrawWorldSurfaces( VkCommandBuffer cmd )
{
	vk_world_push_t push = { 0 };
	VkDeviceSize offset = 0;
	const qboolean lit = R_HasLightmap();

	if( !vk_wgeo.vertices.buffer )
		return;

	memset( lightmap_surfaces, 0, sizeof( lightmap_surfaces ));

	VK_BeginLabel( cmd, "world textures" );

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.world_opaque );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.layout, 0, 1, &vk_pipe.texture_set, 0, NULL );
	vkCmdBindVertexBuffers( cmd, 0, 1, &vk_wgeo.vertices.buffer, &offset );
	vkCmdBindIndexBuffer( cmd, vk_wgeo.indices.buffer, 0, VK_INDEX_TYPE_UINT32 );

	VK_ComputeMVP( push.mvp, RI.worldviewMatrix );
	push.mode = VK_WORLD_MODE_TEXTURE;
	push.texture = -1;

	for( int i = 0; i < WORLDMODEL->numtextures; i++ )
	{
		texture_t *t = WORLDMODEL->textures[i];
		msurface_t *s;

		if( !t || !( s = t->texturechain ) || i == tr.skytexturenum )
			continue;

		for( ; s != NULL; s = s->texturechain )
		{
			const int surfnum = s - WORLDMODEL->surfaces;
			int texnum;

			// water comes with story 014
			if( FBitSet( s->flags, SURF_DRAWTURB|SURF_DRAWSKY ))
				continue;

			texnum = VK_TextureForDraw( R_TextureAnimation( s )->gl_texturenum );

			if( texnum != push.texture )
			{
				push.texture = texnum;
				VK_PushWorld( cmd, &push );
			}

			VK_DrawSurface( cmd, surfnum );

			// R_RenderLightmapForSurface
			if( lit && s->polys && !FBitSet( s->flags, SURF_DRAWTILED ))
			{
				R_UpdateSurfaceLightmap( s, surfnum );
				s->info->lightmapchain = lightmap_surfaces[s->lightmaptexturenum];
				lightmap_surfaces[s->lightmaptexturenum] = s;
			}
		}

		t->texturechain = NULL;
	}

	VK_EndLabel( cmd );

	if( !lit )
		return;

	VK_BeginLabel( cmd, "world lightmaps" );

	if( r_lightmap->value )
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.world_lightmap[2] );
	else
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.world_lightmap[gl_overbright.value ? 1 : 0] );

	push.mode = VK_WORLD_MODE_LIGHTMAP;
	push.scale = gl_overbright.value ? 128.0f / 192.0f : 1.0f;

	for( int i = 0; i < MAX_LIGHTMAPS; i++ )
	{
		if( !lightmap_surfaces[i] )
			continue;

		push.lightmap = VK_TextureForDraw( tr.lightmapTextures[i] );
		VK_PushWorld( cmd, &push );

		for( msurface_t *surf = lightmap_surfaces[i]; surf != NULL; surf = surf->info->lightmapchain )
			VK_DrawSurface( cmd, surf - WORLDMODEL->surfaces );
	}

	VK_EndLabel( cmd );
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

static void R_RenderScene( void )
{
	VkCommandBuffer cmd;
	int x, y, w, h;

	if( !WORLDMODEL && FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		gEngfuncs.Host_Error( "%s: NULL worldmodel\n", __func__ );

	cmd = VK_BeginMainPass();
	if( !cmd )
		return;

	tr.framecount++;
	if( WORLDMODEL )
		tr.dlightframecount = R_PushDlights( WORLDMODEL, tr.framecount );

	R_SetupFrustum();
	RI.viewplanedist = DotProduct( RI.rvp.vieworigin, RI.vforward );

	if( FBitSet( RI.rvp.flags, RF_DRAW_WORLD ))
		R_FindViewLeaf();

	R_SetupModelviewMatrix( RI.worldviewMatrix );
	R_SetupProjectionMatrix( RI.projectionMatrix );

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

	// R_Clear: depth only, and the clear colour becomes grey (green in overview)
	if( ENGINE_GET_PARM( PARM_DEV_OVERVIEW ))
		VK_SetClearColor( 0.0f, 1.0f, 0.0f );
	else VK_SetClearColor( 0.5f, 0.5f, 0.5f );
	VK_ClearMainDepth();

	if( WORLDMODEL )
	{
		R_MarkLeaves();
		R_DrawWorld( cmd );
	}

	// entities, water and sky come with their stories

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
			tr.realframecount++;
			tr.fResetVis = true;
			return;
		}
	}

	tr.fCustomRendering = false;
	tr.realframecount++;
	R_RenderScene();
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
	tr.block_size = BLOCK_SIZE_DEFAULT;

	gEngfuncs.Cvar_RegisterVariable( &gl_overbright );
	gEngfuncs.Cvar_RegisterVariable( &gl_keeptjunctions );
	gEngfuncs.Cvar_RegisterVariable( &gl_litwater_force );
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
