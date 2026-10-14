// shared by the ray traced effects of vk_rtfx.c, mirrored by vk_rtfx_params_t

#include "rt_common.glsl"

#define FX_SHADOWS     1
#define FX_AO          2
#define FX_REFLECTIONS 4
#define FX_HISTORY     8   // the history image holds the previous frame

layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rtfx_params_r
{
	mat4 inv_view_proj;  // clip space (Vulkan depth) to world
	mat4 prev_view_proj; // world to the previous frame's clip space
	vec4 eye;            // xyz, w: depth near plane
	vec4 forward;        // xyz, w: depth far plane
	vec4 prev_eye;       // the previous frame's eye
	vec4 ao;             // radius, strength
	vec4 refl;           // strength, time
	vec4 shadow;         // sun scale, part of the light a shadow keeps
	vec4 light;          // lightmap pass: GL blend factor (2 overbright, else 1), glColor scale
	ivec4 misc;          // FX_* flags, frame number, online
	ivec4 rect;          // the view, pixels: x, y, width, height
};

layout( push_constant ) uniform rtfx_push_s
{
	uint64_t scene;
	uint64_t params;
	ivec4 iparams;
} pc;

layout( set = 1, binding = 0 ) uniform accelerationStructureEXT tlas;
layout( set = 1, binding = 1 ) uniform sampler2D inputs[4];

// eye ray through a pixel centre of the view (GL's bottom-up y: top is +1)
vec3 ViewRay( rtfx_params_r p, vec2 uv )
{
	vec4 far = p.inv_view_proj * vec4( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0 );
	return normalize( far.xyz / far.w - p.eye.xyz );
}

// eye distance along the view axis of a depth buffer value
float EyeDepth( rtfx_params_r p, float d )
{
	float n = p.eye.w, f = p.forward.w;
	return d >= 1.0 ? 1e9 : n * f / ( f - d * ( f - n ));
}
