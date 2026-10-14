// push constants of the brush surface pipelines, mirrored by vk_world_push_t

#include "bindings.h"

layout( push_constant ) uniform world_push_s
{
	mat4 mvp;        // projection * modelview, depth already remapped to [0, 1]
	int texture;     // diffuse texture number
	int lightmap;    // lightmap page texture number
	int mode;        // VK_WORLD_MODE_*
	float scale;     // glColor of the lightmap pass: 128/192 with gl_overbright, else 1
	vec4 color;      // glColor of the texture and colour passes (render modes)
	float alpha_ref; // glAlphaFunc( GL_GREATER, alpha_ref )
	int alpha_test;  // GL_ALPHA_TEST enabled
} pc;
