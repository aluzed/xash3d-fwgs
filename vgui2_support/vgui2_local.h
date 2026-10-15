/*
vgui2_local.h - vgui2_support internals
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef VGUI2_LOCAL_H
#define VGUI2_LOCAL_H

#include <stdint.h>
#include <string.h>
#if _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include "vgui2_api.h"
#include "interfaces.h"

extern const vgui2_engine_t *g_engine;

// the vgui2 library's interfaces
extern vgui2::IVGui *g_ivgui;
extern vgui2::IPanel *g_ipanel;
extern vgui2::IInputInternal *g_input;
extern vgui2::ISchemeManager *g_scheme;
extern vgui2::ILocalize *g_localize;
extern vgui2::ISystem *g_system;

#define Con_Printf  g_engine->Con_Printf
#define Con_DPrintf g_engine->Con_DPrintf

// fonts.cpp
struct glyph_t
{
	int page; // atlas page, -1 for nothing to draw (spaces)
	int x, y, w, h; // in the page
	int a, b, c; // left bearing, width, right bearing: a + b + c is the advance
	int top; // from the top of the line
};

void Font_Shutdown( void );
vgui2::HFont Font_Create( void );
bool Font_AddGlyphSet( vgui2::HFont font, const char *name, int tall, int weight, int blur, int scanlines, int flags, int low, int high );
bool Font_AddCustomFile( const char *path );
int Font_Tall( vgui2::HFont font );
int Font_Ascent( vgui2::HFont font );
bool Font_IsAdditive( vgui2::HFont font );
int Font_Blur( vgui2::HFont font );
const glyph_t *Font_Glyph( vgui2::HFont font, int ch ); // NULL for an unknown font
int Font_PageTexture( int page ); // uploads the page if glyphs were added to it

// surface.cpp
vgui2::ISurface *Surface_Get( void );
void Surface_Init( int width, int height );
void Surface_Shutdown( void );
void Surface_SetScreenSize( int width, int height );
void Surface_Paint( void );
bool Surface_NeedsMouse( void );
bool Surface_NeedsKeyboard( void );
void *Surface_GameUIFuncs( void );
vgui2::VPANEL Surface_ClientRoot( void );

#endif // VGUI2_LOCAL_H
