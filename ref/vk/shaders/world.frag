#version 450
#extension GL_GOOGLE_include_directive : require

// two passes like ref_gl: the texture (VK_WORLD_MODE_TEXTURE), then the
// lightmap times glColor (VK_WORLD_MODE_LIGHTMAP) blended over it with
// glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR ) when overbright or
// glBlendFunc( GL_ZERO, GL_SRC_COLOR ) otherwise

#include "world_common.glsl"
#include "fog.glsl"
#include "output_common.glsl"
#include "world_material.glsl"

layout( set = VK_SET_TEXTURES, binding = VK_BINDING_TEXTURES ) uniform sampler2D textures[VK_MAX_TEXTURES];

layout( location = 0 ) in vec2 in_texcoord;
layout( location = 1 ) in vec2 in_lightcoord;
layout( location = 2 ) in float in_fogdist;
layout( location = 3 ) flat in int in_surface;
layout( location = 4 ) in vec3 in_position;

layout( location = 0 ) out vec4 out_color;

/*
 * Materials, enhanced look only (linear_output): the normal map and the
 * occlusion scale the lightmap pass, which keeps lighting the texture pass
 * through the GL blending, and the material pass adds the specular
 * reflection of the dominant light and the emission. GoldSrc lightmaps
 * have no direction: it comes from the light direction maps (deluxe) when
 * the lightmap page has one, else the normal map changes nothing.
 */
#define GAMMA 2.2
#define PI 3.14159265

vec3 MaterialNormal( material_s m, surface_s s, vec2 uv )
{
	if( m.normal <= 0 )
		return s.normal.xyz;

	vec2 xy = texture( textures[m.normal], uv ).rg * 2.0 - 1.0;
	float z = sqrt( max( 1.0 - dot( xy, xy ), 0.0 ));

	return normalize( s.tangent.xyz * xy.x + s.bitangent.xyz * xy.y + s.normal.xyz * z );
}

// direction to the light, and the part of the light coming from it
vec4 LightDirection( surface_s s )
{
	if( s.info.x <= 0 )
		return vec4( s.normal.xyz, 0.0 );

	vec4 d = texture( textures[s.info.x], in_lightcoord );
	return vec4( normalize( d.xyz * 2.0 - 1.0 ), d.a );
}

vec3 MaterialRMA( material_s m, vec2 uv )
{
	return m.rma > 0 ? texture( textures[m.rma], uv ).rgb : vec3( 1.0, 0.0, 1.0 );
}

// linear factor on the diffuse light: normal map against the surface, occlusion, metalness
float MaterialDiffuse( material_s m, surface_s s )
{
	vec3 n = MaterialNormal( m, s, in_texcoord );
	vec4 l = LightDirection( s );
	vec3 rma = MaterialRMA( m, in_texcoord );
	float flat_light = max( dot( s.normal.xyz, l.xyz ), 0.1 );
	float bump = mix( 1.0, clamp( max( dot( n, l.xyz ), 0.0 ) / flat_light, 0.0, 4.0 ), l.w );

	return bump * rma.b * ( 1.0 - rma.g );
}

// GGX specular of the dominant light and emission, linear
vec3 MaterialAdd( material_s m, surface_s s )
{
	vec3 n = MaterialNormal( m, s, in_texcoord );
	vec4 l = LightDirection( s );
	vec3 rma = MaterialRMA( m, in_texcoord );
	vec3 albedo = pow( texture( textures[pc.texture], in_texcoord ).rgb, vec3( GAMMA ));
	vec3 light = pow( texture( textures[pc.lightmap], in_lightcoord ).rgb * pc.scale, vec3( GAMMA ));

	// the lightmap is what the surface receives: the light itself is stronger
	vec3 radiance = light / max( dot( s.normal.xyz, l.xyz ), 0.1 );
	vec3 v = normalize( pc.color.xyz - in_position );
	vec3 h = normalize( l.xyz + v );
	float nl = max( dot( n, l.xyz ), 0.0 );
	float nv = max( dot( n, v ), 1e-3 );
	float nh = max( dot( n, h ), 0.0 );
	float vh = max( dot( v, h ), 0.0 );
	float a = max( rma.r * rma.r, 0.002 );
	float a2 = a * a;
	float d = nh * nh * ( a2 - 1.0 ) + 1.0;
	float k = a * 0.5;
	float ggx = a2 / ( PI * d * d );
	float vis = 0.25 / (( nv * ( 1.0 - k ) + k ) * ( nl * ( 1.0 - k ) + k ));
	vec3 f0 = mix( vec3( 0.04 ), albedo, rma.g );
	vec3 fresnel = f0 + ( 1.0 - f0 ) * pow( 1.0 - vh, 5.0 );

	vec3 color = ggx * vis * fresnel * nl * radiance * l.w;
	color += f0 * light * rma.g * ( 1.0 - l.w ); // metals mirror the light from everywhere else
	color *= rma.b;

	if( m.emissive > 0 )
		color += pow( texture( textures[m.emissive], in_texcoord ).rgb, vec3( GAMMA )) * m.emissive_strength;

	return color;
}

void main()
{
	if( pc.mode == VK_WORLD_MODE_LIGHTMAP )
	{
		vec3 light = texture( textures[pc.lightmap], in_lightcoord ).rgb * pc.scale;

		// the output is decoded with the gamma power: so is the factor
		if( linear_output && pc.material > 0 )
			light *= pow( MaterialDiffuse( materials[pc.material], surfaces[in_surface] ), 1.0 / GAMMA );

		out_color = TargetColor( vec4( ApplyFog( light, pc.fog, in_fogdist ), 1.0 ));
		return;
	}

	if( pc.mode == VK_WORLD_MODE_MATERIAL )
	{
		vec3 color = linear_output && pc.material > 0 ? MaterialAdd( materials[pc.material], surfaces[in_surface] ) : vec3( 0.0 );

		// added light under fog: only the part the fog lets through
		color = ApplyFog( color, pc.fog, in_fogdist ) - ApplyFog( vec3( 0.0 ), pc.fog, in_fogdist );
		out_color = TargetColor( vec4( pow( max( color, vec3( 0.0 )), vec3( 1.0 / GAMMA )), 1.0 ));
		return;
	}

	// GL_MODULATE with the render mode colour, or untextured (kRenderTransColor)
	vec4 color = pc.color;
	if( pc.mode == VK_WORLD_MODE_TEXTURE )
		color *= texture( textures[pc.texture], in_texcoord );

	if( pc.alpha_test != 0 && !( color.a > pc.alpha_ref ))
		discard;

	color.rgb = ApplyFog( color.rgb, pc.fog, in_fogdist );
	out_color = TargetColor( color );
}
