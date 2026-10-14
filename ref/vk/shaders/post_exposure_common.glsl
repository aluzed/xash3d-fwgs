// the exposure buffer: the adapted exposure, then the luminance pass sums

layout( std430, set = 0, binding = 2 ) buffer exposure_s
{
	float ev;           // adapted exposure, in EV
	float average;      // log2 average luminance of the last frame
	float pad0, pad1;
	vec2 sums[];        // per luminance group: sum of log2 luminance, pixel count
} exposure;
