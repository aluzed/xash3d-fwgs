// GL fixed function fog: the per-frame states written by vk_fog.c, applied
// after texturing like glFog does, on the eye plane distance

struct fog_state_s
{
	vec4 color;
	vec4 params; // VK_FOG_*, density, start, end
};

layout( set = VK_SET_FOG, binding = VK_BINDING_FOG ) uniform fog_table_s
{
	fog_state_s states[VK_MAX_FOG_STATES];
} fog_table;

vec3 ApplyFog( vec3 color, int index, float distance )
{
	if( index <= 0 )
		return color;

	fog_state_s fog = fog_table.states[index];
	int mode = int( fog.params.x );
	float factor;

	if( mode == VK_FOG_LINEAR )
		factor = ( fog.params.w - distance ) / ( fog.params.w - fog.params.z );
	else if( mode == VK_FOG_EXP )
		factor = exp( -fog.params.y * distance );
	else
	{
		float d = fog.params.y * distance;
		factor = exp( -d * d );
	}

	return mix( fog.color.rgb, color, clamp( factor, 0.0, 1.0 ));
}
