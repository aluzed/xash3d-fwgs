#version 450
#extension GL_GOOGLE_include_directive : require

// two passes like ref_gl: the texture (VK_WORLD_MODE_TEXTURE), then the
// lightmap times glColor (VK_WORLD_MODE_LIGHTMAP) blended over it with
// glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR ) when overbright or
// glBlendFunc( GL_ZERO, GL_SRC_COLOR ) otherwise

#include "world_common.glsl"

layout( set = VK_SET_TEXTURES, binding = VK_BINDING_TEXTURES ) uniform sampler2D textures[VK_MAX_TEXTURES];

layout( location = 0 ) in vec2 in_texcoord;
layout( location = 1 ) in vec2 in_lightcoord;

layout( location = 0 ) out vec4 out_color;

void main()
{
	if( pc.mode == VK_WORLD_MODE_LIGHTMAP )
		out_color = vec4( texture( textures[pc.lightmap], in_lightcoord ).rgb * pc.scale, 1.0 );
	else
		out_color = texture( textures[pc.texture], in_texcoord );
}
