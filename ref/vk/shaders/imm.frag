#version 450
#extension GL_GOOGLE_include_directive : require

// GL_MODULATE texture environment followed by the GL alpha test

#include "imm_common.glsl"
#include "fog.glsl"
#include "output_common.glsl"

layout( set = VK_SET_TEXTURES, binding = VK_BINDING_TEXTURES ) uniform sampler2D textures[VK_MAX_TEXTURES];

layout( location = 0 ) in vec2 in_texcoord;
layout( location = 1 ) in vec4 in_color;
layout( location = 2 ) in float in_fogdist;

layout( location = 0 ) out vec4 out_color;

void main()
{
	vec4 color = in_color;

	if( pc.texture != VK_IMM_TEXTURE_NONE )
		color *= texture( textures[pc.texture], in_texcoord );

	// glAlphaFunc( GL_GREATER, ref )
	if( pc.alpha_test != 0 && !( color.a > pc.alpha_ref ))
		discard;

	color.rgb = ApplyFog( color.rgb, pc.fog, in_fogdist );
	out_color = TargetColor( color );
}
