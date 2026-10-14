// SMAA 1x (Jimenez et al., 3rdparty/smaa, MIT licence), its reference
// implementation as it is, run from compute shaders: the vertex shader
// functions give the offsets, the pixel shader functions the results. The
// images are top down like Direct3D's, so the area texture needs no flip.

#define SMAA_GLSL_4 1
#define SMAA_PRESET_HIGH 1
#define SMAA_RT_METRICS vec4( pc.texel.xy, 1.0 / pc.texel.xy )

// the only discard is the edge detection's early out, which returns no edge
#define discard return vec2( 0.0 )

#include "SMAA.hlsl"

#undef discard
