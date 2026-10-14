// push constants of the immediate mode pipelines, mirrored by vk_imm_push_t

#include "bindings.h"

layout( push_constant ) uniform imm_push_s
{
	mat4 mvp;        // GL projection * modelview
	int texture;     // engine texture number, VK_IMM_TEXTURE_NONE when untextured
	int alpha_test;  // GL_ALPHA_TEST enabled
	float alpha_ref; // glAlphaFunc( GL_GREATER, alpha_ref )
} pc;
