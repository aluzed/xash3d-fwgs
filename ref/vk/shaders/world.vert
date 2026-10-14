#version 450
#extension GL_GOOGLE_include_directive : require

// brush surfaces: the vertices are ref_gl's glpoly2_t vertices as they are

#include "world_common.glsl"

layout( location = 0 ) in vec3 in_position;
layout( location = 1 ) in vec2 in_texcoord;
layout( location = 2 ) in vec2 in_lightcoord;

layout( location = 0 ) out vec2 out_texcoord;
layout( location = 1 ) out vec2 out_lightcoord;
layout( location = 2 ) out float out_fogdist; // eye plane distance: -z of the eye, w of the clip
layout( location = 3 ) flat out int out_surface; // VK_DrawSurface draws a surface as its instance
layout( location = 4 ) out vec3 out_position;    // model space

void main()
{
	gl_Position = pc.mvp * vec4( in_position, 1.0 );
	out_texcoord = in_texcoord + pc.tcoffset;
	out_lightcoord = in_lightcoord;
	out_fogdist = abs( gl_Position.w );
	out_surface = gl_InstanceIndex;
	out_position = in_position;
}
