#version 450
#extension GL_GOOGLE_include_directive : require

// immediate mode emulation: the vertices carry what glTexCoord / glColor set

#include "imm_common.glsl"

layout( location = 0 ) in vec3 in_position;
layout( location = 1 ) in vec2 in_texcoord;
layout( location = 2 ) in vec4 in_color;

layout( location = 0 ) out vec2 out_texcoord;
layout( location = 1 ) out vec4 out_color;

void main()
{
	gl_Position = pc.mvp * vec4( in_position, 1.0 );
	gl_PointSize = 1.0;
	out_texcoord = in_texcoord;
	out_color = in_color;
}
