/*
vgui2_main.cpp - loads the vgui2 library and starts the client's VGUI2
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
The start follows GoldSrc's BaseUI: the vgui2 library gets the factories
(the engine's, its own, the filesystem's, the client's), the default
scheme and the localized strings are loaded, the engine's root panels are
made, then the client's IClientVGUI is initialized, started and given its
parent panel. All of this before HUD_Init, which already uses KeyValues.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string>

#if _WIN32
#include <windows.h>
#define VGUI2_LIBRARY "vgui2.dll"
#else
#include <dlfcn.h>
#define VGUI2_LIBRARY "vgui2.so"
#endif

#include "vgui2_local.h"

#if _WIN32
#define EXPORT __declspec( dllexport )
#else
#define EXPORT __attribute__(( visibility( "default" )))
#endif

const vgui2_engine_t *g_engine;

vgui2::IVGui *g_ivgui;
vgui2::IPanel *g_ipanel;
vgui2::IInputInternal *g_input;
vgui2::ISchemeManager *g_scheme;
vgui2::ILocalize *g_localize;
vgui2::ISystem *g_system;

static void *vgui2_library;
static vgui2_factory_t vgui2_factory;
static IClientVGUI *client_vgui;
static int screen_width, screen_height;

static void *Lib_Open( const char *path )
{
#if _WIN32
	return (void *)LoadLibraryA( path );
#else
	return dlopen( path, RTLD_NOW );
#endif
}

static void *Lib_Symbol( void *lib, const char *name )
{
#if _WIN32
	return (void *)GetProcAddress( (HMODULE)lib, name );
#else
	return dlsym( lib, name );
#endif
}

static void Lib_Close( void *lib )
{
#if _WIN32
	FreeLibrary( (HMODULE)lib );
#else
	dlclose( lib );
#endif
}

static const char *Lib_Error( void )
{
#if _WIN32
	return "LoadLibrary failed";
#else
	const char *err = dlerror();
	return err ? err : "unknown error";
#endif
}

// what the engine itself gives to the vgui2 library and the client
static void *EngineFactory( const char *name, int *ret )
{
	void *iface = NULL;

	if( !strcmp( name, "VGUI_Surface026" ))
		iface = Surface_Get();
	else if( !strcmp( name, "VENGINE_GAMEUIFUNCS_VERSION001" ))
		iface = Surface_GameUIFuncs();
	else
		Con_DPrintf( "vgui2: the engine has no interface %s\n", name );

	if( ret )
		*ret = iface ? 0 : 1; // IFACE_OK, IFACE_FAILED

	return iface;
}

// vgui2 from the game's install: next to the engine, or where -rodir points
static bool LoadVGUI2Library( void )
{
	const char *roots[2] = { g_engine->ReadOnlyRootDir(), g_engine->RootDir() };
	std::string error;

	vgui2_library = Lib_Open( VGUI2_LIBRARY );

	if( !vgui2_library )
		error = Lib_Error();

	for( int i = 0; i < 2 && !vgui2_library; i++ )
	{
		if( !roots[i] || !roots[i][0] )
			continue;

		vgui2_library = Lib_Open(( std::string( roots[i] ) + "/" VGUI2_LIBRARY ).c_str());

		if( !vgui2_library )
			error += std::string( "; " ) + Lib_Error();
	}

	if( !vgui2_library )
	{
		Con_Printf( "vgui2: can't load " VGUI2_LIBRARY " of the game's install: %s\n", error.c_str());
		return false;
	}

	vgui2_factory = (vgui2_factory_t)Lib_Symbol( vgui2_library, "CreateInterface" );

	if( !vgui2_factory )
	{
		Con_Printf( "vgui2: " VGUI2_LIBRARY " has no CreateInterface\n" );
		return false;
	}

	g_ivgui = (vgui2::IVGui *)vgui2_factory( "VGUI_ivgui006", NULL );
	g_ipanel = (vgui2::IPanel *)vgui2_factory( "VGUI_Panel007", NULL );
	g_input = (vgui2::IInputInternal *)vgui2_factory( "VGUI_InputInternal001", NULL );
	g_scheme = (vgui2::ISchemeManager *)vgui2_factory( "VGUI_Scheme009", NULL );
	g_localize = (vgui2::ILocalize *)vgui2_factory( "VGUI_Localize003", NULL );
	g_system = (vgui2::ISystem *)vgui2_factory( "VGUI_System009", NULL );

	if( !g_ivgui || !g_ipanel || !g_input || !g_scheme || !g_localize || !g_system )
	{
		Con_Printf( "vgui2: " VGUI2_LIBRARY " lacks one of the interfaces GoldSrc's has, wrong version?\n" );
		return false;
	}

	return true;
}

static void AddLocalization( void *filesystem, const char *game )
{
	char path[256];

	snprintf( path, sizeof( path ), "resource/%s_%s.txt", game, g_engine->GetLanguage());

	if( !g_localize->AddFile( filesystem, path ) && strcmp( g_engine->GetLanguage(), "english" ))
	{
		snprintf( path, sizeof( path ), "resource/%s_english.txt", game );
		g_localize->AddFile( filesystem, path );
	}
}

static void VGUI2_Shutdown( void );

static int VGUI2_Startup( const vgui2_engine_t *engine, vgui2_factory_t client_factory, vgui2_factory_t filesystem_factory )
{
	g_engine = engine;

	if( !client_factory || !( client_vgui = (IClientVGUI *)client_factory( "VClientVGUI001", NULL )))
		return 0; // not a VGUI2 client

	if( !LoadVGUI2Library())
	{
		VGUI2_Shutdown();
		return 0;
	}

	void *( *factories[] )( const char *, int * ) = { EngineFactory, vgui2_factory, filesystem_factory, client_factory };
	const int count = sizeof( factories ) / sizeof( factories[0] );
	void *filesystem = filesystem_factory( "VFileSystem009", NULL );

	if( !g_ivgui->Init( factories, count ))
	{
		Con_Printf( "vgui2: the vgui2 library failed to start\n" );
		VGUI2_Shutdown();
		return 0;
	}

	// what every module's VGuiControls_Init does: KeyValues are allocated by
	// the vgui2 library in blocks of their size, a GoldSrc KeyValues is 24 bytes
	vgui2::IKeyValues *keyvalues = (vgui2::IKeyValues *)vgui2_factory( "KeyValues003", NULL );

	if( keyvalues )
		keyvalues->RegisterSizeofKeyValues( 0x18 );

	g_engine->GetScreenSize( &screen_width, &screen_height );
	g_scheme->LoadSchemeFromFile( "resource/TrackerScheme.res", "BaseUI" );
	AddLocalization( filesystem, "valve" );

	if( strcmp( g_engine->GetGameDir(), "valve" ))
		AddLocalization( filesystem, g_engine->GetGameDir());

	g_ivgui->Start();
	g_ivgui->SetSleep( false );
	Surface_Init( screen_width, screen_height );

	client_vgui->Initialize( factories, count );
	client_vgui->Start();
	client_vgui->SetParent( Surface_ClientRoot());

	Con_Printf( "vgui2: started the client's VGUI2 with " VGUI2_LIBRARY " of the game's install, %dx%d\n", screen_width, screen_height );
	return 1;
}

static void VGUI2_Shutdown( void )
{
	if( client_vgui && g_ivgui )
		client_vgui->Shutdown();

	Surface_Shutdown();

	if( g_ivgui )
		g_ivgui->Shutdown();

	if( vgui2_library )
		Lib_Close( vgui2_library );

	vgui2_library = NULL;
	vgui2_factory = NULL;
	client_vgui = NULL;
	g_ivgui = NULL;
	g_ipanel = NULL;
	g_input = NULL;
	g_scheme = NULL;
	g_localize = NULL;
	g_system = NULL;
}

static void VGUI2_SetScreenSize( int width, int height )
{
	if( width == screen_width && height == screen_height )
		return;

	screen_width = width;
	screen_height = height;
	Surface_SetScreenSize( width, height );
}

static void VGUI2_RunFrame( void )
{
	if( !g_ivgui )
		return;

	g_ivgui->RunFrame();
	Surface_Get()->CalculateMouseVisible();
}

static void VGUI2_Paint( void )
{
	if( g_ivgui )
		Surface_Paint();
}

static bool TextEntryHasFocus( void )
{
	vgui2::VPANEL focus;

	if( !Surface_NeedsKeyboard() || !( focus = g_input->GetFocus()))
		return false;

	const char *name = g_ipanel->GetClassName( focus );
	return name && strstr( name, "TextEntry" ) != NULL;
}

static int VGUI2_KeyEvent( int key, int down )
{
	if( !g_ivgui || !key )
		return 0;

	const vgui2::KeyCode code = g_system->KeyCode_VirtualKeyToVGUI( key );

	if( down )
	{
		g_input->InternalKeyCodePressed( code );
		g_input->InternalKeyCodeTyped( code );
	}
	else g_input->InternalKeyCodeReleased( code );

	// like GoldSrc, VGUI2 sees the keys without taking them from the game:
	// the client's menus read their hot keys in HUD_Key_Event. Only a text
	// entry with the focus keeps them, its typing must not run bindings
	return TextEntryHasFocus();
}

static int VGUI2_MouseButton( int button, int down, int double_click )
{
	if( !g_ivgui || !Surface_NeedsMouse())
		return 0;

	if( double_click && down )
		g_input->InternalMouseDoublePressed( button );
	else if( down )
		g_input->InternalMousePressed( button );
	else
		g_input->InternalMouseReleased( button );

	return 1;
}

static int VGUI2_MouseWheel( int delta )
{
	if( !g_ivgui || !Surface_NeedsMouse())
		return 0;

	g_input->InternalMouseWheeled( delta );
	return 1;
}

static void VGUI2_MouseMove( int x, int y )
{
	if( g_ivgui )
		g_input->InternalCursorMoved( x, y );
}

static void VGUI2_TextInput( const char *utf8 )
{
	if( !g_ivgui || !TextEntryHasFocus() || !utf8 )
		return;

	const unsigned char *s = (const unsigned char *)utf8;

	while( *s )
	{
		unsigned int ch = *s++;
		int more = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : ch >= 0xc0 ? 1 : 0;

		if( more )
			ch &= 0x3f >> more;

		for( ; more && ( *s & 0xc0 ) == 0x80; more-- )
			ch = ( ch << 6 ) | ( *s++ & 0x3f );

		if( !more && ch >= 32 )
			g_input->InternalKeyTyped( (wchar_t)ch );
	}
}

static int VGUI2_WantsMouse( void )
{
	return g_ivgui && Surface_NeedsMouse();
}

static int VGUI2_WantsKeyboard( void )
{
	return g_ivgui && TextEntryHasFocus();
}

static void VGUI2_HideAllMenus( void )
{
	if( client_vgui )
		client_vgui->HideAllVGUIMenu();
}

static void VGUI2_HideScoreBoard( void )
{
	if( client_vgui )
		client_vgui->HideScoreBoard();
}

static void VGUI2_ActivateClientUI( void )
{
	if( client_vgui )
		client_vgui->ActivateClientUI();
}

static void VGUI2_HideClientUI( void )
{
	if( client_vgui )
		client_vgui->HideClientUI();
}

extern "C" EXPORT int GetVGUI2SupportAPI( int version, vgui2_module_t *module )
{
	if( version != VGUI2_API_VERSION || !module )
		return 0;

	module->Startup = VGUI2_Startup;
	module->Shutdown = VGUI2_Shutdown;
	module->SetScreenSize = VGUI2_SetScreenSize;
	module->RunFrame = VGUI2_RunFrame;
	module->Paint = VGUI2_Paint;
	module->KeyEvent = VGUI2_KeyEvent;
	module->MouseButton = VGUI2_MouseButton;
	module->MouseWheel = VGUI2_MouseWheel;
	module->MouseMove = VGUI2_MouseMove;
	module->TextInput = VGUI2_TextInput;
	module->WantsMouse = VGUI2_WantsMouse;
	module->WantsKeyboard = VGUI2_WantsKeyboard;
	module->HideAllMenus = VGUI2_HideAllMenus;
	module->HideScoreBoard = VGUI2_HideScoreBoard;
	module->ActivateClientUI = VGUI2_ActivateClientUI;
	module->HideClientUI = VGUI2_HideClientUI;
	return 1;
}
