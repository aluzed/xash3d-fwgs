// push constants of the brush surface pipelines, mirrored by vk_world_push_t

#include "bindings.h"

layout( push_constant ) uniform world_push_s
{
	mat4 mvp;       // projection * modelview, depth already remapped to [0, 1]
	int texture;    // diffuse texture number
	int lightmap;   // lightmap page texture number
	int mode;       // VK_WORLD_MODE_*
	float scale;    // glColor of the lightmap pass: 128/192 with gl_overbright, else 1
} pc;
