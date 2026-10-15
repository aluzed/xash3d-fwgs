// what a fragment shader writes to the colour target, set per pipeline by
// specialization (vk_pipeline.c VK_TargetSpecialization): the main target
// takes GL's gamma space colours as they are, the HDR scene of vk_post.c
// takes them linear, with a gamma 2.2 power so products stay products

layout( constant_id = 0 ) const bool linear_output = false;
layout( constant_id = 1 ) const float output_scale = 1.0;
// alpha blending into the linear scene: GL blends gamma colours, so what
// shows through is ( 1 - a ) of the gamma colour, ( 1 - a )^2.2 of the
// linear one; without this, translucent things (smoke, sprites, water)
// look much thinner than with ref_gl
layout( constant_id = 2 ) const bool gamma_alpha = false;
// the HDR scene's alpha is the share of the pixel that the rays and the
// occlusion work on (vk_pipeline.c VK_SetBlend): whole for opaque draws
layout( constant_id = 3 ) const bool opaque_alpha = false;

vec4 TargetColor( vec4 color )
{
	if( linear_output )
		color.rgb = pow( max( color.rgb, vec3( 0.0 )), vec3( 2.2 )) * output_scale;

	if( gamma_alpha )
		color.a = 1.0 - pow( 1.0 - clamp( color.a, 0.0, 1.0 ), 2.2 );

	if( opaque_alpha )
		color.a = 1.0;

	return color;
}
