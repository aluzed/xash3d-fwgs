/*
vgui2_draw.c - the engine's side of vgui2_support
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
A client library that exports the VClientVGUI001 interface (Steam's
Counter-Strike and the other games of the 2013 GoldSrc update) gets the
vgui2_support library, between its Initialize and HUD_Init, like GoldSrc
starts VGUI2 before the client's HUD. Everything else reaches it through
the VGui_* functions of vgui_draw.c: painting after the HUD, the mouse and
the text typed. Keys and mouse buttons go to VGUI2 first while one of its
panels has the cursor or the keyboard.
*/

#include "common.h"
#include "client.h"
#include "vgui_draw.h"
#include "vgui2_api.h"
#include "library.h"
#include "keydefs.h"
#include "ref_common.h"
#include "input.h"
#include "platform/platform.h"

static struct
{
	HINSTANCE hInstance;
	vgui2_module_t dllFuncs;
	qboolean active;
} vgui2;

static unsigned char *VGUI2_LoadFile( const char *path, int *length )
{
	fs_offset_t len = 0;
	byte *buf = FS_LoadFile( path, &len, false );

	*length = len;
	return buf;
}

static void VGUI2_FreeFile( void *buffer )
{
	Mem_Free( buffer );
}

static int VGUI2_LoadTextureRGBA( const char *name, const unsigned char *rgba, int width, int height, int update )
{
	rgbdata_t pic = {
		.width = width,
		.height = height,
		.type = PF_RGBA_32,
		.size = width * height * 4,
		.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
		.buffer = (byte *)rgba,
	};

	return update ? GL_UpdateTextureInternal( name, &pic, TF_IMAGE ) : GL_LoadTextureInternal( name, &pic, TF_IMAGE );
}

// GoldSrc also searches the install's platform directory, where Steam keeps
// the images of VGUI2's own controls (resource/icon_close.tga...)
static int VGUI2_LoadPlatformImage( const char *path )
{
	const char *roots[] = { FS_ReadOnlyRootDirectory(), FS_RootDirectory() };
	int texnum = 0;

	for( int i = 0; i < ARRAYSIZE( roots ) && !texnum; i++ )
	{
		char fullpath[MAX_OSPATH];
		fs_offset_t size = 0;
		rgbdata_t *pic = NULL;
		byte *buf;

		if( COM_StringEmptyOrNULL( roots[i] ))
			continue;

		Q_snprintf( fullpath, sizeof( fullpath ), "%s/platform/%s.tga", roots[i], path );

		if( !( buf = FS_LoadDirectFile( fullpath, &size )))
			continue;

		pic = FS_LoadImage( "#platform.tga", buf, size );
		Mem_Free( buf );

		if( pic )
		{
			string name;

			Q_snprintf( name, sizeof( name ), "platform/%s", path );
			texnum = GL_LoadTextureInternal( name, pic, TF_IMAGE );
			FS_FreeImage( pic );
		}
	}

	return texnum;
}

static int VGUI2_LoadTextureFile( const char *path, int *width, int *height )
{
	static const char *const exts[] = { "", ".tga", ".bmp" };
	int texnum = 0;

	for( int i = 0; i < ARRAYSIZE( exts ) && !texnum; i++ )
	{
		string name;

		// GoldSrc's VGUI2 asks for images without their extension
		if( !i && !Q_strcmp( COM_FileExtension( path ), "" ))
			continue;

		Q_snprintf( name, sizeof( name ), "%s%s", path, exts[i] );

		if( FS_FileExists( name, false ))
			texnum = ref.dllFuncs.GL_LoadTexture( name, NULL, 0, TF_IMAGE );
	}

	if( !texnum && !COM_StringEmptyOrNULL( path ))
		texnum = VGUI2_LoadPlatformImage( path );

	if( texnum )
		R_GetTextureParms( width, height, texnum );
	else *width = *height = 0;

	return texnum;
}

static void VGUI2_FreeTexture( int texnum )
{
	ref.dllFuncs.GL_FreeTexture( texnum );
}

static void VGUI2_FillRect( float x, float y, float w, float h, const unsigned char *rgba )
{
	ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, rgba[0], rgba[1], rgba[2], rgba[3] );
}

static void VGUI2_DrawTexture( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum, const unsigned char *rgba, int additive )
{
	ref.dllFuncs.GL_SetRenderMode( additive ? kRenderTransAdd : kRenderTransTexture );
	ref.dllFuncs.Color4ub( rgba[0], rgba[1], rgba[2], rgba[3] );
	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, texnum );
}

static void VGUI2_GetScreenSize( int *width, int *height )
{
	*width = refState.width;
	*height = refState.height;
}

static void VGUI2_ShowCursor( int visible, int cursor_type )
{
	Platform_SetCursorType( visible ? cursor_type : dc_none );
}

static void VGUI2_EnableTextInput( int enable )
{
	Key_EnableTextInput( enable, false );
}

static int VGUI2_KeyFromName( const char *name )
{
	return Key_StringToKeynum( name );
}

static int VGUI2_KeyIsDown( int key )
{
	return Key_IsDown( key );
}

// SDL2 keycodes: printable keys are their character, the others their scancode | 1 << 30
#define SDLK_SC( scancode ) (( scancode ) | ( 1 << 30 ))

static int VGUI2_KeyToSDL( int key )
{
	if( key >= K_F1 && key <= K_F12 )
		return SDLK_SC( 58 + key - K_F1 );

	switch( key )
	{
	case K_TAB: return 9;
	case K_ENTER: return 13;
	case K_ESCAPE: return 27;
	case K_SPACE: return 32;
	case K_BACKSPACE: return 8;
	case K_DEL: return 127;
	case K_CAPSLOCK: return SDLK_SC( 57 );
	case K_SCROLLLOCK: return SDLK_SC( 71 );
	case K_PAUSE: return SDLK_SC( 72 );
	case K_INS: return SDLK_SC( 73 );
	case K_HOME: return SDLK_SC( 74 );
	case K_PGUP: return SDLK_SC( 75 );
	case K_END: return SDLK_SC( 77 );
	case K_PGDN: return SDLK_SC( 78 );
	case K_RIGHTARROW: return SDLK_SC( 79 );
	case K_LEFTARROW: return SDLK_SC( 80 );
	case K_DOWNARROW: return SDLK_SC( 81 );
	case K_UPARROW: return SDLK_SC( 82 );
	case K_KP_NUMLOCK: return SDLK_SC( 83 );
	case K_KP_SLASH: return SDLK_SC( 84 );
	case K_KP_MUL: return SDLK_SC( 85 );
	case K_KP_MINUS: return SDLK_SC( 86 );
	case K_KP_PLUS: return SDLK_SC( 87 );
	case K_KP_ENTER: return SDLK_SC( 88 );
	case K_KP_END: return SDLK_SC( 89 );
	case K_KP_DOWNARROW: return SDLK_SC( 90 );
	case K_KP_PGDN: return SDLK_SC( 91 );
	case K_KP_LEFTARROW: return SDLK_SC( 92 );
	case K_KP_5: return SDLK_SC( 93 );
	case K_KP_RIGHTARROW: return SDLK_SC( 94 );
	case K_KP_HOME: return SDLK_SC( 95 );
	case K_KP_UPARROW: return SDLK_SC( 96 );
	case K_KP_PGUP: return SDLK_SC( 97 );
	case K_KP_INS: return SDLK_SC( 98 );
	case K_KP_DEL: return SDLK_SC( 99 );
	case K_CTRL: return SDLK_SC( 224 );
	case K_SHIFT: return SDLK_SC( 225 );
	case K_ALT: return SDLK_SC( 226 );
	case K_WIN: return SDLK_SC( 227 );
	}

	// the engine's printable keys are their lowercase character, like SDL's
	if( key > 32 && key < 127 )
		return key;

	return 0;
}

static void VGUI2_GetVideoMode( int *width, int *height, int *bpp )
{
	*width = refState.width;
	*height = refState.height;
	*bpp = 32;
}

static double VGUI2_Time( void )
{
	return host.realtime;
}

static void VGUI2_PlaySound( const char *sample )
{
	S_StartLocalSound( sample, VOL_NORM, false );
}

static void VGUI2_ClientCmd( const char *cmd )
{
	Cbuf_AddText( cmd );
	Cbuf_AddText( "\n" );
}

static const char *VGUI2_GetLanguage( void )
{
	const char *lang = Cvar_VariableString( "ui_language" );
	return COM_StringEmptyOrNULL( lang ) ? "english" : lang;
}

static const char *VGUI2_GetGameDir( void )
{
	return GI->gamefolder;
}

static void *VGUI2_FileSystemFactory( const char *name, int *ret )
{
	void *iface = FS_GetNativeObject( name );

	if( ret )
		*ret = iface ? 0 : 1;

	return iface;
}

static const vgui2_engine_t vgui2_engine =
{
	Con_Printf,
	Con_DPrintf,
	VGUI2_LoadFile,
	VGUI2_FreeFile,
	FS_RootDirectory,
	FS_ReadOnlyRootDirectory,
	VGUI2_LoadTextureRGBA,
	VGUI2_LoadTextureFile,
	VGUI2_FreeTexture,
	VGUI2_FillRect,
	VGUI2_DrawTexture,
	VGUI2_GetScreenSize,
	VGUI2_ShowCursor,
	Platform_GetMousePos,
	Platform_SetMousePos,
	VGUI2_EnableTextInput,
	Key_KeynumToString,
	Key_GetBinding,
	VGUI2_KeyFromName,
	VGUI2_KeyIsDown,
	VGUI2_KeyToSDL,
	VGUI2_GetVideoMode,
	VGUI2_Time,
	VGUI2_PlaySound,
	VGUI2_ClientCmd,
	VGUI2_GetLanguage,
	VGUI2_GetGameDir,
};

/*
================
VGui2_Startup

after the client's Initialize, before HUD_Init: false when the client has
no VGUI2 interface, or VGUI2 couldn't start
================
*/
qboolean VGui2_Startup( HINSTANCE client )
{
	void *(*ClientFactory)( void ) = COM_GetProcAddress( client, "ClientFactory" );
	vgui2_factory_t client_factory;
	vgui2_get_api_t GetAPI;

	if( !ClientFactory || !( client_factory = (vgui2_factory_t)ClientFactory( )))
		return false;

	if( !client_factory( "VClientVGUI001", NULL ))
		return false;

	vgui2.hInstance = COM_LoadLibrary( "vgui2_support." OS_LIB_EXT, false, false );

	if( !vgui2.hInstance )
	{
		Con_Printf( S_ERROR "%s: the client needs VGUI2 and vgui2_support can't load: %s\n", __func__, COM_GetLibraryError( ));
		return false;
	}

	if( !( GetAPI = (vgui2_get_api_t)COM_GetProcAddress( vgui2.hInstance, VGUI2_GET_API )) || !GetAPI( VGUI2_API_VERSION, &vgui2.dllFuncs ))
	{
		Con_Printf( S_ERROR "%s: vgui2_support has no API version %d\n", __func__, VGUI2_API_VERSION );
		VGui2_Shutdown();
		return false;
	}

	if( !vgui2.dllFuncs.Startup( &vgui2_engine, client_factory, VGUI2_FileSystemFactory ))
	{
		VGui2_Shutdown();
		return false;
	}

	vgui2.active = true;
	return true;
}

void VGui2_Shutdown( void )
{
	if( vgui2.active )
		vgui2.dllFuncs.Shutdown();

	if( vgui2.hInstance )
		COM_FreeLibrary( vgui2.hInstance );

	memset( &vgui2, 0, sizeof( vgui2 ));
}

void VGui2_Paint( void )
{
	if( !vgui2.active )
		return;

	vgui2.dllFuncs.SetScreenSize( refState.width, refState.height );
	vgui2.dllFuncs.RunFrame();
	vgui2.dllFuncs.Paint();
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
}

// keys of the game, before the client sees them
qboolean VGui2_KeyEvent( int key, int down )
{
	if( !vgui2.active )
		return false;

	if( key >= K_MOUSE1 && key <= K_MOUSE5 )
		return false; // VGui2_MouseEvent had them

	// the console key and escape keep their meaning
	const qboolean taken = vgui2.dllFuncs.KeyEvent( VGUI2_KeyToSDL( key ), down ) != 0;
	return taken && key != '`' && key != '~' && key != K_ESCAPE;
}

qboolean VGui2_MouseEvent( int key, int down )
{
	if( !vgui2.active )
		return false;

	return vgui2.dllFuncs.MouseButton( key - K_MOUSE1, down, false ) != 0;
}

qboolean VGui2_MWheelEvent( int y )
{
	return vgui2.active && vgui2.dllFuncs.MouseWheel( y ) != 0;
}

void VGui2_MouseMove( int x, int y )
{
	if( vgui2.active )
		vgui2.dllFuncs.MouseMove( x, y );
}

void VGui2_ReportTextInput( const char *text )
{
	if( vgui2.active )
		vgui2.dllFuncs.TextInput( text );
}
