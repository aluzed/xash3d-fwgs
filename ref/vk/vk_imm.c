/*
vk_imm.c - GL immediate mode emulation: 2D drawing and TriAPI
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
#include "triangleapi.h"

/*
 * The engine, mainui and the client DLLs draw 2D through calls that map 1:1
 * to GL 1.x immediate mode (ref_gl/gl_draw.c, gl_triapi.c, gl_backend.c).
 * The state they rely on persists across calls and frames exactly like GL
 * state: current colour, bound texture, blend enable and function, alpha
 * test. It is tracked here, and every primitive becomes triangles (or lines
 * or points) in a CPU batch that turns into one draw per state change.
 */

#define VK_IMM_MAX_VERTICES 65536

typedef struct
{
	VkBool32 depth_test;   // GL_DEPTH_TEST enabled
	VkBool32 depth_write;  // glDepthMask
	VkCompareOp depth_func; // glDepthFunc
	VkCullModeFlags cull;  // GL_Cull
	float offset_factor;   // glPolygonOffset, 0 when GL_POLYGON_OFFSET_FILL is off
	float offset_units;
	VkBool32 stencil_test; // GL_STENCIL_TEST enabled
	VkCompareOp stencil_func; // glStencilFunc
	uint32_t stencil_ref;
	uint32_t stencil_mask;
	VkStencilOp stencil_fail; // glStencilOp
	VkStencilOp stencil_zfail;
	VkStencilOp stencil_zpass;
} vk_imm_raster_t;

static const vk_imm_raster_t imm_raster_2d =
{
	.depth_func = VK_COMPARE_OP_LESS_OR_EQUAL,
	.cull = VK_CULL_MODE_NONE,
	.stencil_func = VK_COMPARE_OP_ALWAYS,
	.stencil_mask = 0xFFFFFFFF,
};

static struct
{
	// GL state
	vec4_t color;          // glColor
	vec4_t tri_rgba;       // TriColor, what TriBrightness scales
	vec2_t texcoord;       // glTexCoord
	int texture;           // bound on unit 0
	qboolean texture_2d;   // GL_TEXTURE_2D enabled
	qboolean blend;        // GL_BLEND enabled
	vk_blend_t blend_func; // glBlendFunc
	qboolean alpha_test;   // GL_ALPHA_TEST enabled
	float alpha_ref;       // glAlphaFunc( GL_GREATER, ref )
	int cull;              // TRICULLSTYLE, 2D mode never culls
	vk_imm_raster_t raster; // depth and culling of 3D draws, 2D mode has neither
	struct { float factor, units; } polyoffset[4]; // GL_PushPolygonOffset stack
	int num_polyoffsets;
	qboolean tex_replace;  // GL_REPLACE texture environment instead of GL_MODULATE
	qboolean flat_shade;   // glShadeModel( GL_FLAT )
	qboolean color_mask;   // glColorMask, all channels or none

	qboolean in2d;
	vec2_t offset2d;
	vk_imm_push_t push;    // matrices and texture of the current draws

	// primitive being built between Begin and End
	int primitive;         // TRI_*, -1 outside Begin / End
	int num_prim;
	vk_imm_vertex_t prim[VK_IMM_MAX_PRIMITIVE];

	// pending batch
	vk_imm_vertex_t vertices[VK_IMM_MAX_VERTICES];
	int num_vertices;
	vk_topology_t batch_topology;
	vk_blend_t batch_blend;
	vk_imm_push_t batch_push;
	vk_imm_raster_t batch_raster;
} imm;

/*
=================
Batching
=================
*/
void VK_ImmFlush( void )
{
	VkCommandBuffer cmd;
	VkBuffer buffer;
	VkDeviceSize offset;
	void *dst;

	if( !imm.num_vertices )
		return;

	cmd = VK_BeginMainPass();

	if( cmd == VK_NULL_HANDLE )
	{
		imm.num_vertices = 0;
		return;
	}

	dst = VK_DynamicAlloc( imm.num_vertices * sizeof( vk_imm_vertex_t ), 16, &buffer, &offset );

	if( dst )
	{
		memcpy( dst, imm.vertices, imm.num_vertices * sizeof( vk_imm_vertex_t ));

		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, VK_ImmPipeline( imm.batch_topology, imm.batch_blend ) );
		vkCmdSetCullMode( cmd, imm.batch_raster.cull );
		vkCmdSetDepthTestEnable( cmd, imm.batch_raster.depth_test );
		vkCmdSetDepthWriteEnable( cmd, imm.batch_raster.depth_write );
		vkCmdSetDepthCompareOp( cmd, imm.batch_raster.depth_func );
		vkCmdSetDepthBias( cmd, imm.batch_raster.offset_units, 0.0f, imm.batch_raster.offset_factor );
		vkCmdSetStencilTestEnable( cmd, imm.batch_raster.stencil_test );
		vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, imm.batch_raster.stencil_fail,
			imm.batch_raster.stencil_zpass, imm.batch_raster.stencil_zfail, imm.batch_raster.stencil_func );
		vkCmdSetStencilCompareMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, imm.batch_raster.stencil_mask );
		vkCmdSetStencilWriteMask( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFFFFFFF );
		vkCmdSetStencilReference( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, imm.batch_raster.stencil_ref );
		VK_BindDescriptorSets( cmd );
		vkCmdPushConstants( cmd, vk_pipe.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( imm.batch_push ), &imm.batch_push );
		vkCmdBindVertexBuffers( cmd, 0, 1, &buffer, &offset );
		vkCmdDraw( cmd, imm.num_vertices, 1, 0, 0 );
	}

	imm.num_vertices = 0;
}

static vk_blend_t VK_ImmBatchBlend( void )
{
	if( !imm.color_mask && !imm.in2d )
		return VK_BLEND_NO_COLOR;

	return imm.blend ? imm.blend_func : VK_BLEND_NONE;
}

// queue count vertices of topology with the current state
static void VK_ImmEmit( vk_topology_t topology, const vk_imm_vertex_t *verts, int count )
{
	vk_imm_push_t push = imm.push;
	vk_blend_t blend = VK_ImmBatchBlend();
	const vk_imm_raster_t *raster = imm.in2d ? &imm_raster_2d : &imm.raster;

	if( count <= 0 || !VK_FrameRecording( ))
		return;

	push.texture = imm.texture_2d ? VK_TextureForDraw( imm.texture ) : VK_IMM_TEXTURE_NONE;
	push.alpha_test = imm.alpha_test;
	push.alpha_ref = imm.alpha_ref;
	push.fog = imm.in2d ? 0 : VK_FogIndex( );

	if( imm.num_vertices && ( topology != imm.batch_topology || blend != imm.batch_blend
		|| memcmp( &push, &imm.batch_push, sizeof( push ))
		|| memcmp( raster, &imm.batch_raster, sizeof( *raster ))
		|| imm.num_vertices + count > VK_IMM_MAX_VERTICES ))
		VK_ImmFlush();

	imm.batch_topology = topology;
	imm.batch_blend = blend;
	imm.batch_push = push;
	imm.batch_raster = *raster;

	memcpy( &imm.vertices[imm.num_vertices], verts, count * sizeof( *verts ));
	imm.num_vertices += count;

	// studio models drawn now are part of the ray traced scene (vk_rt.c)
	if( topology == VK_TOPOLOGY_TRIANGLES && !imm.in2d )
		VK_RTCaptureTriangles( verts, count, push.texture, imm.alpha_test );
}

/*
=================
Matrices
=================
*/
static void VK_ImmSetup2DMatrix( void )
{
	matrix4x4 projection, modelview, mvp;
	int width, height;

	VK_MainExtent( &width, &height );

	// same matrices as ref_gl, the flipped viewport keeps GL's Y axis
	Matrix4x4_CreateOrtho( projection, 0, gpGlobals->width, gpGlobals->height, 0, -99999, 99999 );
	Matrix4x4_LoadIdentity( modelview );
	Matrix4x4_ConcatTranslate( modelview, imm.offset2d[0], imm.offset2d[1], 0.0f );
	Matrix4x4_Concat( mvp, projection, modelview );
	Matrix4x4_ToArrayFloatGL( mvp, imm.push.mvp );
}

/*
=================
Primitive assembly
=================
*/
static void VK_ImmAssemble( void )
{
	static vk_imm_vertex_t tris[VK_IMM_MAX_PRIMITIVE * 3];
	const vk_imm_vertex_t *v = imm.prim;
	const int n = imm.num_prim;
	int count = 0;

	switch( imm.primitive )
	{
	case TRI_TRIANGLES:
		count = n - n % 3;
		memcpy( tris, v, count * sizeof( *v ));
		break;
	case TRI_LINES:
		VK_ImmEmit( VK_TOPOLOGY_LINES, v, n & ~1 );
		return;
	case TRI_POINTS:
		VK_ImmEmit( VK_TOPOLOGY_POINTS, v, n );
		return;
	case TRI_QUADS:
		for( int i = 0; i + 3 < n; i += 4 )
		{
			tris[count++] = v[i + 0]; tris[count++] = v[i + 1]; tris[count++] = v[i + 2];
			tris[count++] = v[i + 0]; tris[count++] = v[i + 2]; tris[count++] = v[i + 3];
		}
		break;
	case TRI_TRIANGLE_STRIP:
		for( int i = 0; i + 2 < n; i++ )
		{
			// keep the winding of every other triangle, like GL
			tris[count++] = v[( i & 1 ) ? i + 1 : i];
			tris[count++] = v[( i & 1 ) ? i : i + 1];
			tris[count++] = v[i + 2];
		}
		break;
	case TRI_QUAD_STRIP:
		for( int i = 0; i + 3 < n; i += 2 )
		{
			tris[count++] = v[i + 0]; tris[count++] = v[i + 1]; tris[count++] = v[i + 3];
			tris[count++] = v[i + 0]; tris[count++] = v[i + 3]; tris[count++] = v[i + 2];
		}
		break;
	case TRI_TRIANGLE_FAN:
	case TRI_POLYGON:
	default:
		for( int i = 1; i + 1 < n; i++ )
		{
			tris[count++] = v[0];
			tris[count++] = v[i];
			tris[count++] = v[i + 1];
		}
		break;
	}

	// glShadeModel( GL_FLAT ): the last vertex of each triangle colours it
	if( imm.flat_shade )
	{
		for( int i = 0; i + 2 < count; i += 3 )
		{
			Vector4Copy( tris[i + 2].color, tris[i + 0].color );
			Vector4Copy( tris[i + 2].color, tris[i + 1].color );
		}
	}

	VK_ImmEmit( VK_TOPOLOGY_TRIANGLES, tris, count );
}

/*
=================
TriAPI entry points of ref_interface_t (gl_triapi.c)
=================
*/
void TriRenderMode( int mode )
{
	imm.tex_replace = false;

	switch( mode )
	{
	case kRenderNormal:
		imm.blend = false;
		imm.raster.depth_write = VK_TRUE;
		break;
	case kRenderTransAlpha:
		imm.blend = true;
		imm.blend_func = VK_BLEND_ALPHA;
		imm.raster.depth_write = VK_FALSE;
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		imm.blend = true;
		imm.blend_func = VK_BLEND_ALPHA;
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		imm.blend = true;
		imm.blend_func = VK_BLEND_ADD;
		imm.raster.depth_write = VK_FALSE;
		break;
	}
}

void TriBegin( int primitive )
{
	imm.primitive = primitive;
	imm.num_prim = 0;
}

// glEnd for the renderer's own drawing, 2D or 3D
void VK_ImmEnd( void )
{
	if( imm.primitive < 0 )
		return;

	VK_ImmAssemble();

	imm.primitive = -1;
	imm.num_prim = 0;
}

void TriEnd( void )
{
	VK_ImmEnd();
}

void TriColor4f( float r, float g, float b, float a )
{
	Vector4Set( imm.tri_rgba, r, g, b, a );
	Vector4Set( imm.color, r, g, b, a );
}

void TriColor4ub( byte r, byte g, byte b, byte a )
{
	Vector4Set( imm.tri_rgba, r * ( 1.0f / 255.0f ), g * ( 1.0f / 255.0f ), b * ( 1.0f / 255.0f ), a * ( 1.0f / 255.0f ));
	Vector4Copy( imm.tri_rgba, imm.color );
}

void TriTexCoord2f( float u, float v )
{
	Vector2Set( imm.texcoord, u, v );
}

void TriVertex3f( float x, float y, float z )
{
	vk_imm_vertex_t *vert;

	if( imm.primitive < 0 || imm.num_prim >= VK_IMM_MAX_PRIMITIVE )
		return;

	vert = &imm.prim[imm.num_prim++];
	VectorSet( vert->position, x, y, z );
	Vector2Copy( imm.texcoord, vert->texcoord );

	// GL_REPLACE takes the texel as is, which GL_MODULATE does with white
	if( imm.tex_replace && imm.texture_2d && !imm.in2d )
		Vector4Set( vert->color, 1.0f, 1.0f, 1.0f, 1.0f );
	else Vector4Copy( imm.color, vert->color );
}

// a vertex with its own colour (a GL colour array), rgba NULL for glColor
void VK_ImmVertex( const float *v, float s, float t, const byte *rgba )
{
	vk_imm_vertex_t *vert;

	if( imm.primitive < 0 || imm.num_prim >= VK_IMM_MAX_PRIMITIVE )
		return;

	vert = &imm.prim[imm.num_prim++];
	VectorCopy( v, vert->position );
	Vector2Set( vert->texcoord, s, t );

	if( imm.tex_replace && imm.texture_2d && !imm.in2d )
		Vector4Set( vert->color, 1.0f, 1.0f, 1.0f, 1.0f );
	else if( rgba )
		Vector4Set( vert->color, rgba[0] * ( 1.0f / 255.0f ), rgba[1] * ( 1.0f / 255.0f ), rgba[2] * ( 1.0f / 255.0f ), rgba[3] * ( 1.0f / 255.0f ));
	else Vector4Copy( imm.color, vert->color );
}

void TriVertex3fv( const float *v )
{
	TriVertex3f( v[0], v[1], v[2] );
}

void TriCullFace( TRICULLSTYLE mode )
{
	imm.cull = mode;
	imm.raster.cull = mode == TRI_FRONT ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
}

/*
=================
2D entry points (gl_draw.c, gl_backend.c, gl_context.c)
=================
*/
void GL_Bind( int tmu, unsigned int texnum )
{
	if( tmu != GL_KEEP_UNIT && tmu != XASH_TEXTURE0 )
		return; // multitexture comes with the world passes

	imm.texture = texnum;
}

void GL_SetRenderMode( int mode )
{
	switch( mode )
	{
	case kRenderNormal:
	default:
		imm.blend = false;
		imm.alpha_test = false;
		break;
	case kRenderTransColor:
	case kRenderTransTexture:
		imm.blend = true;
		imm.alpha_test = false;
		imm.blend_func = VK_BLEND_ALPHA;
		break;
	case kRenderTransAlpha:
		imm.blend = false;
		imm.alpha_test = true;
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		imm.blend = true;
		imm.alpha_test = false;
		imm.blend_func = VK_BLEND_ADD;
		break;
	case kRenderScreenFadeModulate:
		imm.blend = true;
		imm.alpha_test = false;
		imm.blend_func = VK_BLEND_MODULATE;
		break;
	}
}

static void VK_ImmSet2D( qboolean enable )
{
	if( enable )
	{
		if( imm.in2d )
			return;

		imm.in2d = true;
		VK_ImmSetup2DMatrix();

		// HUD and console state, as ref_gl sets it
		imm.alpha_test = true;
		Vector4Set( imm.color, 1.0f, 1.0f, 1.0f, 1.0f );
	}
	else
	{
		VK_ImmFlush();
		imm.in2d = false;
	}
}

void R_Set2DMode( qboolean enable )
{
	VK_FogSet2D( enable );
	VK_ImmSet2D( enable );
}

void R_Set2DOffset( float x, float y )
{
	Vector2Set( imm.offset2d, x, y );

	if( imm.in2d )
		VK_ImmSetup2DMatrix();
}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	GL_Bind( XASH_TEXTURE0, texnum );

	TriBegin( TRI_QUADS );
		TriTexCoord2f( s1, t1 );
		TriVertex3f( x, y, 0.0f );
		TriTexCoord2f( s2, t1 );
		TriVertex3f( x + w, y, 0.0f );
		TriTexCoord2f( s2, t2 );
		TriVertex3f( x + w, y + h, 0.0f );
		TriTexCoord2f( s1, t2 );
		TriVertex3f( x, y + h, 0.0f );
	TriEnd();
}

// CL_FillRGBA: untextured, blended, and it leaves its colour current
void FillRGBA( int rendermode, float x, float y, float w, float h, byte r, byte g, byte b, byte a )
{
	imm.texture_2d = false;
	imm.blend = true;
	imm.blend_func = rendermode == kRenderTransAdd ? VK_BLEND_ADD : VK_BLEND_ALPHA;
	TriColor4ub( r, g, b, a );

	TriBegin( TRI_QUADS );
		TriVertex3f( x, y, 0.0f );
		TriVertex3f( x + w, y, 0.0f );
		TriVertex3f( x + w, y + h, 0.0f );
		TriVertex3f( x, y + h, 0.0f );
	TriEnd();

	imm.texture_2d = true;
	imm.blend = false;
}

void VGUI_SetupDrawing( qboolean rect )
{
	imm.blend = true;
	imm.blend_func = VK_BLEND_ALPHA;

	if( rect )
		imm.alpha_test = false;
	else
	{
		imm.alpha_test = true;
		imm.alpha_ref = 0.0f;
	}
}

/*
=================
3D state of the renderer's own drawing (studio models), as ref_gl sets it
with direct GL calls. Every change that a pending batch can't share ends
it, like a GL state change splits a draw.
=================
*/
void VK_ImmSet3D( qboolean enable )
{
	VK_ImmFlush();

	if( enable )
	{
		// R_SetupGL and R_Set2DMode( false )
		imm.in2d = false;
		VK_ImmLoadModelview( RI.worldviewMatrix );
		imm.raster.depth_test = VK_TRUE;
		imm.raster.depth_write = VK_TRUE;
		imm.raster.depth_func = VK_COMPARE_OP_LESS_OR_EQUAL;
		imm.raster.cull = VK_CULL_MODE_FRONT_BIT;
		imm.raster.stencil_test = VK_FALSE; // glStencilFunc and glStencilOp persist like GL state
		imm.color_mask = true;
		imm.blend = false;
		imm.alpha_test = false;
		imm.tex_replace = false;
		imm.flat_shade = false;
		Vector4Set( imm.color, 1.0f, 1.0f, 1.0f, 1.0f );
	}
	else
	{
		VK_SetDepthRange( 0.0f, 1.0f );
		imm.in2d = false;
		VK_ImmSet2D( true ); // the fog stays: other views may follow
	}
}

// GL_LoadMatrix( modelview ) with the scene projection
void VK_ImmLoadModelview( const matrix4x4 modelview )
{
	VK_ComputeMVP( imm.push.mvp, modelview );
}

void VK_ImmDepthTest( qboolean enable )
{
	imm.raster.depth_test = enable ? VK_TRUE : VK_FALSE;
}

void VK_ImmDepthMask( qboolean write )
{
	imm.raster.depth_write = write ? VK_TRUE : VK_FALSE;
}

void VK_ImmDepthFunc( VkCompareOp op )
{
	imm.raster.depth_func = op;
}

// glDepthRange: viewport state, which pending draws must not see
void VK_ImmDepthRange( float min, float max )
{
	VK_ImmFlush();
	VK_SetDepthRange( min, max );
}

void VK_ImmCull( VkCullModeFlags mode )
{
	imm.raster.cull = mode;
}

// glState.faceCull
VkCullModeFlags VK_ImmGetCull( void )
{
	return imm.raster.cull;
}

void VK_ImmBlendEnable( qboolean enable )
{
	imm.blend = enable;
}

void VK_ImmBlendFunc( vk_blend_t func )
{
	imm.blend_func = func;
}

void VK_ImmAlphaTestEnable( qboolean enable )
{
	imm.alpha_test = enable;
}

void VK_ImmAlphaFunc( float ref )
{
	imm.alpha_ref = ref;
}

// glColor, which leaves the TriAPI colour of TriBrightness alone
void VK_ImmColor4f( float r, float g, float b, float a )
{
	Vector4Set( imm.color, r, g, b, a );
}

void VK_ImmColor4ub( byte r, byte g, byte b, byte a )
{
	Vector4Set( imm.color, r * ( 1.0f / 255.0f ), g * ( 1.0f / 255.0f ), b * ( 1.0f / 255.0f ), a * ( 1.0f / 255.0f ));
}

// gl_triapi.c
int TriSpriteTexture( model_t *pSpriteModel, int frame )
{
	if( !pSpriteModel || pSpriteModel->type != mod_sprite || !pSpriteModel->cache.data )
		return 0;

	int gl_texturenum = gEngfuncs.R_GetSpriteFrame( pSpriteModel, frame, 0.0f )->gl_texturenum;
	if( gl_texturenum == 0 )
		return 0;

	GL_Bind( XASH_TEXTURE0, gl_texturenum );

	return 1;
}

void TriBrightness( float brightness )
{
	float r = imm.tri_rgba[0] * imm.tri_rgba[3] * brightness;
	float g = imm.tri_rgba[1] * imm.tri_rgba[3] * brightness;
	float b = imm.tri_rgba[2] * imm.tri_rgba[3] * brightness;

	VK_ImmColor4f( r, g, b, 1.0f );
}

void VK_ImmTexture2D( qboolean enable )
{
	imm.texture_2d = enable;
}

void VK_ImmTexReplace( qboolean replace )
{
	imm.tex_replace = replace;
}

void VK_ImmFlatShade( qboolean flat )
{
	imm.flat_shade = flat;
}

void VK_ImmStencilTest( qboolean enable )
{
	// the depth format may lack the stencil aspect
	imm.raster.stencil_test = enable && VK_FormatHasStencil( VK_MainDepthFormat( )) ? VK_TRUE : VK_FALSE;
}

void VK_ImmStencilFunc( VkCompareOp op, uint32_t ref, uint32_t mask )
{
	imm.raster.stencil_func = op;
	imm.raster.stencil_ref = ref;
	imm.raster.stencil_mask = mask;
}

void VK_ImmStencilOp( VkStencilOp fail, VkStencilOp zfail, VkStencilOp zpass )
{
	imm.raster.stencil_fail = fail;
	imm.raster.stencil_zfail = zfail;
	imm.raster.stencil_zpass = zpass;
}

void VK_ImmColorMask( qboolean write )
{
	imm.color_mask = write;
}

// gl_backend.c: the offset on top of the stack is the one in use
void GL_PushPolygonOffset( float factor, float units )
{
	if( imm.num_polyoffsets >= ARRAYSIZE( imm.polyoffset ))
	{
		gEngfuncs.Con_Reportf( "%s: overflow\n", __func__ );
		return;
	}

	imm.raster.offset_factor = factor;
	imm.raster.offset_units = units;

	imm.polyoffset[imm.num_polyoffsets].factor = factor;
	imm.polyoffset[imm.num_polyoffsets].units = units;
	imm.num_polyoffsets++;
}

void GL_PopPolygonOffset( void )
{
	if( imm.num_polyoffsets <= 0 )
	{
		gEngfuncs.Con_Reportf( "%s: underflow\n", __func__ );
		return;
	}

	imm.num_polyoffsets--;

	if( imm.num_polyoffsets > 0 )
	{
		imm.raster.offset_factor = imm.polyoffset[imm.num_polyoffsets - 1].factor;
		imm.raster.offset_units = imm.polyoffset[imm.num_polyoffsets - 1].units;
	}
	else
	{
		imm.raster.offset_factor = 0.0f;
		imm.raster.offset_units = 0.0f;
	}
}

/*
=================
Frame hooks
=================
*/
void VK_ImmBeginFrame( void )
{
	imm.in2d = false;
	R_Set2DMode( true );
}

void VK_ImmEndFrame( void )
{
	R_Set2DMode( false );
}

void VK_InitImm( void )
{
	memset( &imm, 0, sizeof( imm ));

	// GL_SetDefaultState
	Vector4Set( imm.color, 1.0f, 1.0f, 1.0f, 1.0f );
	imm.texture_2d = true;
	imm.alpha_ref = 0.0f; // DEFAULT_ALPHATEST
	imm.color_mask = true;
	imm.primitive = -1;

	// GL_SetDefaultState with a stencil buffer: each pixel drawn once until
	// the next clear, what studio shadows rely on
	imm.raster.stencil_func = VK_COMPARE_OP_EQUAL;
	imm.raster.stencil_ref = 0;
	imm.raster.stencil_mask = 0xFFFFFFFF;
	imm.raster.stencil_fail = VK_STENCIL_OP_KEEP;
	imm.raster.stencil_zfail = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	imm.raster.stencil_zpass = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
}
