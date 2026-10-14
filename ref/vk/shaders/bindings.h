/*
bindings.h - descriptor bindings and push constants shared by C and GLSL
Copyright (C) 2026 aluzed

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef VK_SHADER_BINDINGS_H
#define VK_SHADER_BINDINGS_H

// single source of truth for binding numbers, included by the C code and
// by every shader

// set 0: every texture, indexed by the engine texture number
#define VK_SET_TEXTURES      0
#define VK_BINDING_TEXTURES  0
#define VK_MAX_TEXTURES      8192 // same limit as ref_gl

// set 1: the fog states of the frame (fog.glsl), indexed from the push constants
#define VK_SET_FOG           1
#define VK_BINDING_FOG       0
#define VK_MAX_FOG_STATES    256  // 32 bytes each, index 0 is "no fog"

// set 2: world data of the enhanced look, indexed from the push constants and
// gl_InstanceIndex (world_material.glsl)
#define VK_SET_WORLD         2
#define VK_BINDING_MATERIALS 0    // vk_material.c
#define VK_BINDING_SURFACES  1    // vk_world.c, one entry per surface of the world model

// glFogi( GL_FOG_MODE )
#define VK_FOG_LINEAR        1
#define VK_FOG_EXP           2
#define VK_FOG_EXP2          3

// push constants of the immediate mode passes (2D, TriAPI)
#define VK_IMM_TEXTURE_NONE  0    // texture index 0: untextured, like GL_TEXTURE_2D disabled

// brush surface shading
#define VK_WORLD_MODE_TEXTURE   0 // texture pass
#define VK_WORLD_MODE_LIGHTMAP  1 // lightmap pass, lightmap * glColor
#define VK_WORLD_MODE_COLOR     2 // untextured, glColor only
#define VK_WORLD_MODE_MATERIAL  3 // enhanced look: specular and emissive, added

#endif // VK_SHADER_BINDINGS_H
