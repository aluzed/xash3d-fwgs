// what a fragment shader writes to the colour target, set per pipeline by
// specialization (vk_pipeline.c VK_TargetSpecialization): the main target
// takes GL's gamma space colours as they are, the HDR scene of vk_post.c
// takes them linear, with a gamma 2.2 power so products stay products

layout( constant_id = 0 ) const bool linear_output = false;
layout( constant_id = 1 ) const float output_scale = 1.0;

vec4 TargetColor( vec4 color )
{
	if( linear_output )
		color.rgb = pow( max( color.rgb, vec3( 0.0 )), vec3( 2.2 )) * output_scale;

	return color;
}
