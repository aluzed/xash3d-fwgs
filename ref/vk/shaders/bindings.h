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

// push constants of the immediate mode passes (2D, TriAPI)
#define VK_IMM_TEXTURE_NONE  0    // texture index 0: untextured, like GL_TEXTURE_2D disabled

// brush surface shading
#define VK_WORLD_MODE_TEXTURE   0 // texture pass
#define VK_WORLD_MODE_LIGHTMAP  1 // lightmap pass, lightmap * glColor

#endif // VK_SHADER_BINDINGS_H
