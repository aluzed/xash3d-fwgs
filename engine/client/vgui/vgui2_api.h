/*
vgui2_api.h - interface between the engine and the vgui2_support library
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

/*
Client libraries made for Steam's GoldSrc (Counter-Strike 1.6 since 2013,
Condition Zero, Day of Defeat...) draw part of their interface with VGUI2:
they export the VClientVGUI001 interface and need the engine to hand them
Valve's vgui2 library set up, with a surface (VGUI_Surface026) the engine
implements. vgui2_support is that part of the engine, in C++ because the
interfaces are C++ classes: it loads the vgui2 library of the game's
install, implements the surface on top of the functions below and gives
the client its interfaces. Only loaded for a client that asks for it.

Positions are in pixels of the window, keys are SDL2 keycodes: the vgui2
library translates them itself, as it does on GoldSrc.
*/

#pragma once
#ifndef VGUI2_API_H
#define VGUI2_API_H

#include <stddef.h>

#define VGUI2_API_VERSION 1
#define VGUI2_GET_API "GetVGUI2SupportAPI"

typedef void *(*vgui2_factory_t)( const char *name, int *ret );

typedef struct vgui2_engine_s
{
	void (*Con_Printf)( const char *fmt, ... );
	void (*Con_DPrintf)( const char *fmt, ... );

	// the engine's filesystem
	unsigned char *(*LoadFile)( const char *path, int *length ); // through the game's search paths
	void (*FreeFile)( void *buffer );
	const char *(*RootDir)( void ); // where the engine runs from
	const char *(*ReadOnlyRootDir)( void ); // -rodir, the game's install, or empty

	// textures, returns the renderer's texture number, 0 on failure
	int (*LoadTextureRGBA)( const char *name, const unsigned char *rgba, int width, int height, int update );
	int (*LoadTextureFile)( const char *path, int *width, int *height ); // an image of the game, its extension may be left out
	void (*FreeTexture)( int texnum );

	// 2D drawing in window pixels, already clipped by the caller
	void (*FillRect)( float x, float y, float w, float h, const unsigned char *rgba );
	void (*DrawTexture)( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum, const unsigned char *rgba, int additive );
	void (*GetScreenSize)( int *width, int *height );

	// the cursor: visible over the game or not, and its position
	void (*ShowCursor)( int visible, int cursor_type );
	void (*GetCursorPos)( int *x, int *y );
	void (*SetCursorPos)( int x, int y );
	void (*EnableTextInput)( int enable );

	// keys, as engine key numbers
	const char *(*KeyName)( int key );
	const char *(*KeyBinding)( int key );
	int (*KeyFromName)( const char *name );
	int (*KeyIsDown)( int key );
	int (*KeyToSDL)( int key ); // the SDL2 keycode of an engine key, 0 if it has none
	void (*GetVideoMode)( int *width, int *height, int *bpp );

	double (*Time)( void );
	void (*PlaySound)( const char *sample );
	void (*ClientCmd)( const char *cmd );
	const char *(*GetLanguage)( void ); // like "english"
	const char *(*GetGameDir)( void ); // like "cstrike"
} vgui2_engine_t;

typedef struct vgui2_module_s
{
	// loads the vgui2 library, sets it up and starts the client's VGUI2
	// interface; returns 0 when the client can't run that way, the error
	// is printed
	int (*Startup)( const vgui2_engine_t *engine, vgui2_factory_t client_factory, vgui2_factory_t filesystem_factory );
	void (*Shutdown)( void );

	void (*SetScreenSize)( int width, int height );
	void (*RunFrame)( void ); // every frame, before drawing
	void (*Paint)( void ); // in 2D mode, after the HUD

	// input, each returns non-zero when VGUI2 took it
	int (*KeyEvent)( int sdl_key, int down );
	int (*MouseButton)( int button, int down, int double_click ); // 0 left, 1 right, 2 middle, 3 and 4 the side buttons
	int (*MouseWheel)( int delta );
	void (*MouseMove)( int x, int y );
	void (*TextInput)( const char *utf8 );
	int (*WantsMouse)( void ); // a panel that takes the mouse is up: show the cursor, keep the mouse from the game
	int (*WantsKeyboard)( void ); // a text entry or a dialog has the keyboard

	// the client's IClientVGUI
	void (*HideAllMenus)( void );
	void (*HideScoreBoard)( void );
	void (*ActivateClientUI)( void );
	void (*HideClientUI)( void );
} vgui2_module_t;

typedef int (*vgui2_get_api_t)( int version, vgui2_module_t *module );

#endif // VGUI2_API_H
