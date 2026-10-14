// shared by the post-processing passes of vk_post.c, mirrored by vk_post_push_t

#include "bindings.h"

layout( push_constant ) uniform post_push_s
{
	ivec4 rect;     // pixels written: x, y, width, height
	ivec4 iparams;
	vec4 params;
	vec4 params2;
	vec4 src_rect;  // texture coordinates the sources are clamped to: min xy, max xy
	vec4 texel;     // xy: 1 / size of input 0, zw: 1 / size of the output
} pc;

layout( set = 0, binding = 0 ) uniform sampler2D inputs[4];

// the output pixel of this invocation, false outside the rectangle
bool PostPixel( out ivec2 pixel )
{
	pixel = ivec2( gl_GlobalInvocationID.xy ) + pc.rect.xy;
	return all( lessThan( gl_GlobalInvocationID.xy, uvec2( pc.rect.zw )));
}

float Luminance( vec3 color )
{
	return dot( color, vec3( 0.2126, 0.7152, 0.0722 ));
}
