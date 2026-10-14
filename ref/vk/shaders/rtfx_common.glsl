// shared by the ray traced effects of vk_rtfx.c, mirrored by vk_rtfx_params_t

#include "rt_common.glsl"

#define FX_SHADOWS     1
#define FX_AO          2
#define FX_REFLECTIONS 4
#define FX_HISTORY     8   // the history image holds the previous frame
#define FX_GI          16  // ray traced lighting of the lightmapped surfaces (026)
#define FX_SOFT        32  // the shadows of rt_shadows.comp (027)

#define VK_RTFX_MAX_INPUTS 6

layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer rtfx_params_r
{
	mat4 inv_view_proj;  // clip space (Vulkan depth) to world
	mat4 prev_view_proj; // world to the previous frame's clip space
	vec4 eye;            // xyz, w: depth near plane
	vec4 forward;        // xyz, w: depth far plane
	vec4 prev_eye;       // the previous frame's eye
	vec4 ao;             // radius, strength
	vec4 refl;           // strength, time
	vec4 shadow;         // sun scale, part of the light a shadow keeps, tan of the sun's radius
	vec4 light;          // lightmap pass: GL blend factor (2 overbright, else 1), glColor scale
	ivec4 misc;          // FX_* flags, frame number, online
	ivec4 rect;          // the view, pixels: x, y, width, height
	vec4 gi;             // part of the lighting the rays give (the lightmaps the rest), scale, bounce, sky
	vec4 styles[16];     // g_lightstylevalue / 256 of the 64 lightstyles
};

layout( push_constant ) uniform rtfx_push_s
{
	uint64_t scene;
	uint64_t params;
	ivec4 iparams;
} pc;

layout( set = 1, binding = 0 ) uniform accelerationStructureEXT tlas;
layout( set = 1, binding = 1 ) uniform sampler2D inputs[VK_RTFX_MAX_INPUTS];

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

float StyleValue( rtfx_params_r p, int style )
{
	return p.styles[style >> 2][style & 3];
}

#define BLOCKERS ( RT_MASK_WORLD | RT_MASK_BRUSH | RT_MASK_MODELS )

uint Hash( uint x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

vec2 Random2( ivec2 pixel, int frame, uint salt )
{
	uint h = Hash( uint( pixel.x ) * 1973u + Hash( uint( pixel.y ) * 9277u + uint( frame ) * 26699u + salt ));
	return vec2( h & 0xffffu, h >> 16 ) / 65536.0;
}

vec3 CosineDirection( vec3 n, vec2 r )
{
	vec3 up = abs( n.z ) < 0.99 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 );
	vec3 t = normalize( cross( up, n ));
	vec3 b = cross( n, t );
	float phi = r.x * 6.2831853;
	float s = sqrt( r.y );

	return normalize( t * ( cos( phi ) * s ) + b * ( sin( phi ) * s ) + n * sqrt( 1.0 - r.y ));
}

// the eye ray of a pixel of the view hits a surface the raster drew: false
// for the sky, and for what isn't in the scene (sprites, particles, drawn
// nearer than the ray's hit)
bool PrimaryHit( rtfx_params_r p, rt_scene_r scene, ivec2 pixel, vec2 uv, float raster, out vec3 dir,
	out rt_hit_s hit, out int instance, out float t )
{
	dir = ViewRay( p, uv );

	if( raster >= 1e8 || !RTTraceSkip( tlas, scene, p.eye.xyz, dir, 0.0, 65536.0, BLOCKERS | RT_MASK_VIEWMODEL | RT_MASK_WATER,
		RT_SKIP_NONE, hit, instance, t ))
		return false;

	float traced = t * dot( dir, p.forward.xyz );
	return !( raster < traced - 2.0 - traced * 0.01 );
}

// the lightmap units R_BuildLightMap adds (lightmap texel * style) to its 10 bit light
float ToLight( rt_scene_r scene )
{
	return 4.0 * float( scene.sky2.w ) / 256.0;
}

#define RT_LIGHT_SIZE2 ( 48.0 * 48.0 ) // mirrored by vk_rtlights.c

// a light of the map at a surface point: lightmap units before its style;
// the texture's colour of a surface light only when textured
vec3 GridLight( rt_glight_s l, vec3 pos, vec3 n, bool textured, out vec3 to, out float dist )
{
	vec3 d = l.origin - pos;
	float d2 = dot( d, d );

	dist = sqrt( d2 );
	to = d / max( dist, 1e-3 );

	// the inverse square of a light fixture's size, not of a point: no hot spots
	float f = max( dot( n, to ), 0.0 ) * ( 128.0 * 128.0 + RT_LIGHT_SIZE2 ) / ( d2 + RT_LIGHT_SIZE2 );
	if( f <= 0.0 )
		return vec3( 0.0 );

	if( l.type == RT_LIGHT_SPOT )
		f *= smoothstep( l.cone_outer, l.cone_inner, dot( -to, l.dir ));
	else if( l.type == RT_LIGHT_SURFACE )
		f *= max( dot( -to, l.dir ), 0.0 );

	vec3 color = l.color;
	if( l.texture > 0 && textured )
		color *= textureLod( textures[nonuniformEXT( l.texture )], vec2( 0.5 ), 16.0 ).rgb;

	return color * f;
}

// the cell of the light grid around a point: first index, static lights, styled lights
uvec3 GridCell( rt_lightgrid_r grid, vec3 pos )
{
	ivec3 c = clamp( ivec3( floor(( pos - grid.mins.xyz ) / grid.mins.w )), ivec3( 0 ), grid.dims.xyz - 1 );
	uvec2 cell = rt_uvec2s_r( uint64_t( grid ) + grid.offsets.x ).v[( c.z * grid.dims.y + c.y ) * grid.dims.x + c.x];

	return uvec3( cell.x, cell.y & 0xffffu, cell.y >> 16 );
}

// octahedral unit vector in 32 bits
float PackNormal( vec3 n )
{
	n /= abs( n.x ) + abs( n.y ) + abs( n.z );
	vec2 o = n.z >= 0.0 ? n.xy : ( 1.0 - abs( n.yx )) * sign( n.xy );
	return uintBitsToFloat( packSnorm2x16( o ));
}

vec3 UnpackNormal( float f )
{
	vec2 o = unpackSnorm2x16( floatBitsToUint( f ));
	vec3 n = vec3( o, 1.0 - abs( o.x ) - abs( o.y ));
	if( n.z < 0.0 )
		n.xy = ( 1.0 - abs( n.yx )) * sign( n.xy );
	return normalize( n );
}

float Luminance( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ));
}

// the pixel of the previous frame that saw a point where it was then (its
// motion: RTPrevPosition), false when out of that view
bool PrevPixel( rtfx_params_r p, vec3 prev_pos, out ivec2 prev_pixel )
{
	vec4 clip = p.prev_view_proj * vec4( prev_pos, 1.0 );
	vec2 prev = vec2( clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5 );

	prev_pixel = ivec2( prev * vec2( p.rect.zw )) + p.rect.xy;
	return clip.w > 0.0 && all( greaterThanEqual( prev, vec2( 0.0 ))) && all( lessThan( prev, vec2( 1.0 )));
}

// that pixel showed the same surface: as far from the previous eye
bool SameSurface( rtfx_params_r p, float prev_dist, vec3 prev_pos )
{
	return prev_dist > 0.0 && abs( prev_dist - length( prev_pos - p.prev_eye.xyz )) < 1.0 + 0.03 * prev_dist;
}
