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
#define VK_IMM_MAX_PRIMITIVE 4096 // vertices between Begin and End

static struct
{
	// GL state
	vec4_t color;          // glColor
	vec2_t texcoord;       // glTexCoord
	int texture;           // bound on unit 0
	qboolean texture_2d;   // GL_TEXTURE_2D enabled
	qboolean blend;        // GL_BLEND enabled
	vk_blend_t blend_func; // glBlendFunc
	qboolean alpha_test;   // GL_ALPHA_TEST enabled
	float alpha_ref;       // glAlphaFunc( GL_GREATER, ref )
	int cull;              // TRICULLSTYLE, 2D mode never culls

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

		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.imm[imm.batch_topology][imm.batch_blend] );
		vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe.layout, 0, 1, &vk_pipe.texture_set, 0, NULL );
		vkCmdPushConstants( cmd, vk_pipe.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof( imm.batch_push ), &imm.batch_push );
		vkCmdBindVertexBuffers( cmd, 0, 1, &buffer, &offset );
		vkCmdDraw( cmd, imm.num_vertices, 1, 0, 0 );
	}

	imm.num_vertices = 0;
}

static vk_blend_t VK_ImmBlend( void )
{
	return imm.blend ? imm.blend_func : VK_BLEND_NONE;
}

// queue count vertices of topology with the current state
static void VK_ImmEmit( vk_topology_t topology, const vk_imm_vertex_t *verts, int count )
{
	vk_imm_push_t push = imm.push;
	vk_blend_t blend = VK_ImmBlend();

	if( count <= 0 || !VK_FrameRecording( ))
		return;

	push.texture = imm.texture_2d ? VK_TextureForDraw( imm.texture ) : VK_IMM_TEXTURE_NONE;
	push.alpha_test = imm.alpha_test;
	push.alpha_ref = imm.alpha_ref;

	if( imm.num_vertices && ( topology != imm.batch_topology || blend != imm.batch_blend
		|| memcmp( &push, &imm.batch_push, sizeof( push ))
		|| imm.num_vertices + count > VK_IMM_MAX_VERTICES ))
		VK_ImmFlush();

	imm.batch_topology = topology;
	imm.batch_blend = blend;
	imm.batch_push = push;

	memcpy( &imm.vertices[imm.num_vertices], verts, count * sizeof( *verts ));
	imm.num_vertices += count;
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
		VK_ImmEmit( VK_TOPOLOGY_TRIANGLES, v, n - n % 3 );
		return;
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

	VK_ImmEmit( VK_TOPOLOGY_TRIANGLES, tris, count );
}

/*
=================
TriAPI entry points of ref_interface_t (gl_triapi.c)
=================
*/
void TriRenderMode( int mode )
{
	switch( mode )
	{
	case kRenderNormal:
		imm.blend = false;
		break;
	case kRenderTransAlpha:
	case kRenderTransColor:
	case kRenderTransTexture:
		imm.blend = true;
		imm.blend_func = VK_BLEND_ALPHA;
		break;
	case kRenderGlow:
	case kRenderTransAdd:
		imm.blend = true;
		imm.blend_func = VK_BLEND_ADD;
		break;
	}
}

void TriBegin( int primitive )
{
	imm.primitive = primitive;
	imm.num_prim = 0;
}

void TriEnd( void )
{
	if( imm.primitive < 0 )
		return;

	// 3D TriAPI needs the world view (story 019)
	if( imm.in2d )
		VK_ImmAssemble();
	else
		VK_STUB();

	imm.primitive = -1;
	imm.num_prim = 0;
}

void TriColor4f( float r, float g, float b, float a )
{
	Vector4Set( imm.color, r, g, b, a );
}

void TriColor4ub( byte r, byte g, byte b, byte a )
{
	Vector4Set( imm.color, r * ( 1.0f / 255.0f ), g * ( 1.0f / 255.0f ), b * ( 1.0f / 255.0f ), a * ( 1.0f / 255.0f ));
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
	Vector4Copy( imm.color, vert->color );
}

void TriVertex3fv( const float *v )
{
	TriVertex3f( v[0], v[1], v[2] );
}

void TriCullFace( TRICULLSTYLE mode )
{
	imm.cull = mode;
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

void R_Set2DMode( qboolean enable )
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
	imm.primitive = -1;
}
