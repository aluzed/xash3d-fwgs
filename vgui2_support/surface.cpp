/*
surface.cpp - VGUI_Surface026, the engine's side of VGUI2
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
The vgui2 library keeps the panel tree, the input focus and the messages;
the surface draws, holds the popups (top level panels, in drawing order)
and says whether a panel needs the mouse or the keyboard.

A panel draws between PushMakeCurrent and PopMakeCurrent, in coordinates
of the panel, clipped to its clip rectangle: everything is translated and
clipped here, the renderer gets window pixels.

The engine owns the root of the tree (the embedded panel) and the panel
the client library puts its viewport in, both the size of the window.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "vgui2_local.h"

using namespace vgui2;

// the engine's panels: they only hold children
class CRootPanel : public IClientPanel
{
public:
	VPANEL vpanel = 0;
	const char *name = "";

	VPANEL GetVPanel() override { return vpanel; }
	void Think() override { }
	void PerformApplySchemeSettings() override { }
	void PaintTraverse( bool forceRepaint, bool allowForce ) override;
	void Repaint() override { }
	VPANEL IsWithinTraverse( int x, int y, bool traversePopups ) override;
	void GetInset( int &top, int &left, int &right, int &bottom ) override { top = left = right = bottom = 0; }
	void GetClipRect( int &x0, int &y0, int &x1, int &y1 ) override;
	void OnChildAdded( VPANEL child ) override { }
	void OnSizeChanged( int newWide, int newTall ) override { }
	void InternalFocusChanged( bool lost ) override { }
	bool RequestInfo( void *outputData ) override { return false; }
	void RequestFocus( int direction ) override { }
	bool RequestFocusPrev( VPANEL existingPanel ) override { return false; }
	bool RequestFocusNext( VPANEL existingPanel ) override { return false; }
	void OnMessage( const void *params, VPANEL fromPanel ) override { }
	VPANEL GetCurrentKeyFocus() override { return 0; }
	int GetTabPosition() override { return 0; }
	const char *GetName() override { return name; }
	const char *GetClassName() override { return "CRootPanel"; }
	HScheme GetScheme() override { return g_scheme->GetDefaultScheme(); } // what the children without a scheme of their own get
	bool IsProportional() override { return false; }
	bool IsAutoDeleteSet() override { return false; }
	void DeletePanel() override { }
	void *QueryInterface( int id ) override { return NULL; }
	void *GetPanel() override { return NULL; }
	const char *GetModuleName() override { return "engine"; }
	void *GetMessageMap() override { return NULL; }
};

void CRootPanel::PaintTraverse( bool forceRepaint, bool allowForce )
{
	if( !g_ipanel->IsVisible( vpanel ))
		return;

	const int count = g_ipanel->GetChildCount( vpanel );

	for( int i = 0; i < count; i++ )
	{
		VPANEL child = g_ipanel->GetChild( vpanel, i );

		// popups are drawn by the surface, over everything
		if( child && !g_ipanel->IsPopup( child ) && g_ipanel->IsVisible( child ))
			g_ipanel->PaintTraverse( child, forceRepaint, allowForce );
	}
}

VPANEL CRootPanel::IsWithinTraverse( int x, int y, bool traversePopups )
{
	if( !g_ipanel->IsVisible( vpanel ))
		return 0;

	for( int i = g_ipanel->GetChildCount( vpanel ) - 1; i >= 0; i-- )
	{
		VPANEL child = g_ipanel->GetChild( vpanel, i );

		if( !child || g_ipanel->IsPopup( child ) != traversePopups )
			continue;

		if(( child = g_ipanel->IsWithinTraverse( child, x, y, traversePopups )))
			return child;
	}

	return 0; // the root panels themselves never take the mouse
}

void CRootPanel::GetClipRect( int &x0, int &y0, int &x1, int &y1 )
{
	int w, h;

	g_ipanel->GetAbsPos( vpanel, x0, y0 );
	g_ipanel->GetSize( vpanel, w, h );
	x1 = x0 + w;
	y1 = y0 + h;
}

/*
GoldSrc renders HTML (the MOTD, the info pages) with Steam's Chromium,
through a controller the surface gives out and a browser object it makes
per HTML panel. There is no browser here: both are objects whose every
virtual function does nothing and returns 0, except the one that makes a
browser. The HTML panels stay empty instead of keeping a dangling browser.
*/
#define NULL_VTABLE_SIZE 256

struct null_object_t
{
	void **vtable;
};

static void *null_controller_vtable[NULL_VTABLE_SIZE];
static void *null_browser_vtable[NULL_VTABLE_SIZE];
static null_object_t null_controller = { null_controller_vtable };
static null_object_t null_browser = { null_browser_vtable };

#if _WIN32
/*
MSVC's virtual functions are __thiscall: the callee pops the arguments, so
every slot needs a function of its own size. The sizes, in 4 bytes words,
are those of chromehtml.dll's CHTMLController and of the IHTMLSerializer
its slot 14 makes (one destructor slot, not gcc's two).
*/
#define CHROME_CREATE_BROWSER 14

static const unsigned char controller_args[] = { 1, 2, 0, 0, 5, 3, 2, 0, 3, 1, 0, 2, 1, 1, 1 };
static const unsigned char browser_args[] = { 0, 0, 2, 2, 7, 0, 0, 0, 0, 1, 1, 1, 2, 1, 2, 2, 2, 1, 1, 1, 1, 1, 2, 1, 1, 0, 1, 2 };

struct null_methods_t
{
	int Args0() { return 0; }
	int Args1( int ) { return 0; }
	int Args2( int, int ) { return 0; }
	int Args3( int, int, int ) { return 0; }
	int Args4( int, int, int, int ) { return 0; }
	int Args5( int, int, int, int, int ) { return 0; }
	int Args6( int, int, int, int, int, int ) { return 0; }
	int Args7( int, int, int, int, int, int, int ) { return 0; }
	void *CreateBrowser( int ) { return &null_browser; }
};

template<typename T> static void *MethodAddress( T method )
{
	void *address;

	static_assert( sizeof( method ) == sizeof( address ), "a method of a class without bases is its address" );
	memcpy( &address, &method, sizeof( address ));
	return address;
}

static void FillNullVtable( void **vtable, const unsigned char *args, size_t count )
{
	void *const methods[] = {
		MethodAddress( &null_methods_t::Args0 ), MethodAddress( &null_methods_t::Args1 ),
		MethodAddress( &null_methods_t::Args2 ), MethodAddress( &null_methods_t::Args3 ),
		MethodAddress( &null_methods_t::Args4 ), MethodAddress( &null_methods_t::Args5 ),
		MethodAddress( &null_methods_t::Args6 ), MethodAddress( &null_methods_t::Args7 ),
	};

	for( size_t i = 0; i < NULL_VTABLE_SIZE; i++ )
		vtable[i] = methods[i < count ? args[i] : 0];
}

static void *NullChromeController( void )
{
	if( !null_controller_vtable[0] )
	{
		FillNullVtable( null_controller_vtable, controller_args, sizeof( controller_args ));
		FillNullVtable( null_browser_vtable, browser_args, sizeof( browser_args ));
		null_controller_vtable[CHROME_CREATE_BROWSER] = MethodAddress( &null_methods_t::CreateBrowser );
	}

	return &null_controller;
}
#else
#define CHROME_CREATE_BROWSER 15 // IHTMLChromeController slot, read from vgui2::HTML::HTML

static int NullMethod( void *self )
{
	return 0;
}

static void *NullCreateBrowser( void *self )
{
	return &null_browser;
}

static void *NullChromeController( void )
{
	if( !null_controller_vtable[0] )
	{
		for( int i = 0; i < NULL_VTABLE_SIZE; i++ )
			null_controller_vtable[i] = null_browser_vtable[i] = (void *)NullMethod;

		null_controller_vtable[CHROME_CREATE_BROWSER] = (void *)NullCreateBrowser;
	}

	return &null_controller;
}
#endif

struct texture_t
{
	int texnum;
	int width, height;
	std::string file; // the image it was loaded from
	std::vector<unsigned char> rgba; // kept for the BGRA region updates
};

struct paint_state_t
{
	VPANEL panel;
	int x, y; // where the panel's coordinates start
	int clip[4]; // x0, y0, x1, y1
};

class CSurface : public ISurface, public IGameUIFuncs
{
public:
	CRootPanel root, client_root;
	int width = 640, height = 480;

	std::vector<paint_state_t> paint_stack;
	unsigned char draw_color[4] = { 255, 255, 255, 255 };
	unsigned char text_color[4] = { 255, 255, 255, 255 };
	HFont text_font = 0;
	int text_x = 0, text_y = 0;
	int bound_texture = 0;

	std::unordered_map<int, texture_t> textures;
	int next_texture = 1;

	std::vector<unsigned long> popups; // handles of the vgui2 library, back to front
	VPANEL modal = 0, restrict_panel = 0;
	HCursor cursor = dc_arrow;
	bool needs_mouse = false, needs_keyboard = false;
	bool mouse_control = true;
	int proportional_base[2] = { 640, 480 }, hd_proportional_base[2] = { 640, 480 };
	char language[64] = "english";

	// drawing, in the current panel's coordinates
	bool Clip( float &x0, float &y0, float &x1, float &y1, float &s0, float &t0, float &s1, float &t1 );
	void Fill( int x0, int y0, int x1, int y1 );
	void Textured( float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1, int texnum, const unsigned char *color, bool additive );
	void DrawChar( wchar_t ch, bool additive );
	bool PopupVisible( VPANEL panel );
	void ApplySchemeTraverse( VPANEL panel, bool force );
	void ThinkTraverse( VPANEL panel );
	void SolvePanelTraverse( VPANEL panel );

	// ISurface
	void Shutdown() override { }
	void RunFrame() override { }
	VPANEL GetEmbeddedPanel() override { return root.vpanel; }
	void SetEmbeddedPanel( VPANEL panel ) override { root.vpanel = panel; }
	void PushMakeCurrent( VPANEL panel, bool useInsets ) override;
	void PopMakeCurrent( VPANEL panel ) override;
	void DrawSetColor( int r, int g, int b, int a ) override { draw_color[0] = r; draw_color[1] = g; draw_color[2] = b; draw_color[3] = a; }
	void DrawSetColor( Color col ) override { DrawSetColor( col.r, col.g, col.b, col.a ); }
	void DrawFilledRect( int x0, int y0, int x1, int y1 ) override { Fill( x0, y0, x1, y1 ); }
	void DrawOutlinedRect( int x0, int y0, int x1, int y1 ) override;
	void DrawLine( int x0, int y0, int x1, int y1 ) override;
	void DrawPolyLine( int *px, int *py, int numPoints ) override;
	void DrawSetTextFont( HFont font ) override { text_font = font; }
	void DrawSetTextColor( int r, int g, int b, int a ) override { text_color[0] = r; text_color[1] = g; text_color[2] = b; text_color[3] = a; }
	void DrawSetTextColor( Color col ) override { DrawSetTextColor( col.r, col.g, col.b, col.a ); }
	void DrawSetTextPos( int x, int y ) override { text_x = x; text_y = y; }
	void DrawGetTextPos( int &x, int &y ) override { x = text_x; y = text_y; }
	void DrawPrintText( const wchar_t *text, int textLen ) override;
	void DrawUnicodeChar( wchar_t wch ) override { DrawChar( wch, false ); }
	void DrawUnicodeCharAdd( wchar_t wch ) override { DrawChar( wch, true ); }
	void DrawFlushText() override { }
	IHTML *CreateHTMLWindow( IHTMLEvents *events, VPANEL context ) override { return NULL; }
	void PaintHTMLWindow( IHTML *htmlwin ) override { }
	void DeleteHTMLWindow( IHTML *htmlwin ) override { }
	void DrawSetTextureFile( int id, const char *filename, int hardwareFilter, bool forceReload ) override;
	void DrawSetTextureRGBA( int id, const unsigned char *rgba, int wide, int tall, int hardwareFilter, bool forceReload ) override;
	void DrawSetTexture( int id ) override { bound_texture = id; }
	void DrawGetTextureSize( int id, int &wide, int &tall ) override;
	void DrawTexturedRect( int x0, int y0, int x1, int y1 ) override;
	bool IsTextureIDValid( int id ) override { return textures.count( id ) != 0; }
	int CreateNewTextureID( bool procedural ) override;
	void GetScreenSize( int &wide, int &tall ) override { wide = width; tall = height; }
	void SetAsTopMost( VPANEL panel, bool state ) override { }
	void BringToFront( VPANEL panel ) override;
	void SetForegroundWindow( VPANEL panel ) override { BringToFront( panel ); }
	void SetPanelVisible( VPANEL panel, bool state ) override { }
	void SetMinimized( VPANEL panel, bool state ) override { }
	bool IsMinimized( VPANEL panel ) override { return false; }
	void FlashWindow( VPANEL panel, bool state ) override { }
	void SetTitle( VPANEL panel, const wchar_t *title ) override { }
	void SetAsToolBar( VPANEL panel, bool state ) override { }
	void CreatePopup( VPANEL panel, bool minimised, bool showTaskbarIcon, bool disabled, bool mouseInput, bool kbInput ) override;
	void SwapBuffers( VPANEL panel ) override { }
	void Invalidate( VPANEL panel ) override { }
	void SetCursor( HCursor c ) override { cursor = c; }
	bool IsCursorVisible() override { return needs_mouse; }
	void ApplyChanges() override { }
	bool IsWithin( int x, int y ) override { return x >= 0 && y >= 0 && x < width && y < height; }
	bool HasFocus() override { return true; }
	bool SupportsFeature( int feature ) override { return false; }
	void RestrictPaintToSinglePanel( VPANEL panel ) override { restrict_panel = panel; }
	void SetModalPanel( VPANEL panel ) override { modal = panel; }
	VPANEL GetModalPanel() override { return modal; }
	void UnlockCursor() override { }
	void LockCursor() override { }
	void SetTranslateExtendedKeys( bool state ) override { }
	VPANEL GetTopmostPopup() override { return popups.empty() ? 0 : g_ivgui->HandleToPanel( popups.back()); }
	void SetTopLevelFocus( VPANEL panel ) override;
	HFont CreateFont() override { return Font_Create(); }
	bool AddGlyphSetToFont( HFont font, const char *fontName, int tall, int weight, int blur, int scanlines, int flags, int lowRange, int highRange ) override
	{
		return Font_AddGlyphSet( font, fontName, tall, weight, blur, scanlines, flags, lowRange, highRange );
	}
	bool AddCustomFontFile( const char *fontFileName ) override { return Font_AddCustomFile( fontFileName ); }
	int GetFontTall( HFont font ) override { return Font_Tall( font ); }
	void GetCharABCwide( HFont font, int ch, int &a, int &b, int &c ) override;
	int GetCharacterWidth( HFont font, int ch ) override;
	void GetTextSize( HFont font, const wchar_t *text, int &wide, int &tall ) override;
	VPANEL GetNotifyPanel() override { return 0; }
	void SetNotifyIcon( VPANEL context, unsigned long icon, VPANEL panelToReceiveMessages, const char *text ) override { }
	void PlaySound( const char *fileName ) override { g_engine->PlaySound( fileName ); }
	int GetPopupCount() override { return popups.size(); }
	VPANEL GetPopup( int index ) override;
	bool ShouldPaintChildPanel( VPANEL panel ) override;
	bool RecreateContext( VPANEL panel ) override { return true; }
	void AddPanel( VPANEL panel ) override { }
	void ReleasePanel( VPANEL panel ) override;
	void MovePopupToFront( VPANEL panel ) override;
	void MovePopupToBack( VPANEL panel ) override;
	void SolveTraverse( VPANEL panel, bool forceApplySchemeSettings ) override;
	void PaintTraverse( VPANEL panel ) override;
	void EnableMouseCapture( VPANEL panel, bool state ) override { }
	void GetWorkspaceBounds( int &x, int &y, int &wide, int &tall ) override { x = y = 0; wide = width; tall = height; }
	void GetAbsoluteWindowBounds( int &x, int &y, int &wide, int &tall ) override { GetWorkspaceBounds( x, y, wide, tall ); }
	void GetProportionalBase( int &w, int &h ) override { w = proportional_base[0]; h = proportional_base[1]; }
	void CalculateMouseVisible() override;
	bool NeedKBInput() override { return needs_keyboard; }
	bool HasCursorPosFunctions() override { return true; }
	void SurfaceGetCursorPos( int &x, int &y ) override { g_engine->GetCursorPos( &x, &y ); }
	void SurfaceSetCursorPos( int x, int y ) override { g_engine->SetCursorPos( x, y ); }
	void DrawTexturedPolygon( VGuiVertex *vertices, int n ) override;
	int GetFontAscent( HFont font, wchar_t wch ) override { return Font_Ascent( font ); }
	void SetAllowHTMLJavaScript( bool state ) override { }
	void SetLanguage( const char *lang ) override { snprintf( language, sizeof( language ), "%s", lang ); }
	const char *GetLanguage() override { return language; }
	bool DeleteTextureByID( int id ) override;
	void DrawUpdateRegionTextureBGRA( int id, int x, int y, const unsigned char *data, int wide, int tall ) override;
	void DrawSetTextureBGRA( int id, const unsigned char *bgra, int wide, int tall ) override;
	void CreateBrowser( VPANEL panel, void *browser, bool popupWindow, const char *userAgent ) override { }
	void RemoveBrowser( VPANEL panel, void *browser ) override { }
	void *AccessChromeHTMLController() override { return NullChromeController(); }
	void DrawTexturedRectAdd( int x0, int y0, int x1, int y1 ) override;
	void SetSupportsEsc( bool state ) override { }
	int GetFontBlur( HFont font ) override { return Font_Blur( font ); }
	bool IsAdditive( HFont font ) override { return Font_IsAdditive( font ); }
	void SetProportionalBase( int w, int h ) override { proportional_base[0] = w; proportional_base[1] = h; }
	void GetHDProportionalBase( int &w, int &h ) override { w = hd_proportional_base[0]; h = hd_proportional_base[1]; }
	void SetHDProportionalBase( int w, int h ) override { hd_proportional_base[0] = w; hd_proportional_base[1] = h; }
#if _WIN32
	void Unknown106( int a ) override { }
	void Unknown107( int a ) override { }
	void Unknown108( int a, int b, int c ) override { }
	bool SetFullscreenMode( int w, int h, int bpp ) override { return false; }
	void SetWindowedMode() override { }
	void Unknown111( int a ) override { }
	void Unknown112( int a ) override { }
	void DrawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 ) override;
	bool SetWatchForComputerUse( bool state ) override { return false; }
	double GetTimeSinceLastUse() override { return 0.0; }
#else
	void SetFullscreenMode( int w, int h, int bpp ) override { }
	void SetWindowedMode() override { }
	int Unknown109( int a, int b ) override { return 0; }
	int Unknown110( int a, int b ) override { return 0; }
	int Unknown111( int a, int b ) override { return 0; }
	int Unknown112( int a, int b ) override { return 0; }
	void DrawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 ) override;
	void SetNotifyIcon( Image *image, VPANEL panelToReceiveMessages, const char *text ) override { }
	void SetWatchForComputerUse( bool state ) override { }
	double GetTimeSinceLastUse() override { return 0.0; }
	bool VGUI2MouseControl() override { return mouse_control; }
	void SetVGUI2MouseControl( bool state ) override { mouse_control = state; }
#endif

	// IGameUIFuncs
	bool IsKeyDown( const char *keyname, bool &isdown ) override;
	const char *Key_NameForKey( int keynum ) override { return g_engine->KeyName( keynum ); }
	const char *Key_BindingForKey( int keynum ) override { return g_engine->KeyBinding( keynum ); }
	KeyCode GetVGUI2KeyCodeForBind( const char *bind ) override;
	void GetVideoModes( vmode_t **liststart, int *count ) override;
	void GetCurrentVideoMode( int *wide, int *tall, int *bpp ) override { g_engine->GetVideoMode( wide, tall, bpp ); }
	void GetCurrentRenderer( char *name, int namelen, int *windowed, int *hdmodels, int *addons_folder ) override;
	bool IsConnectedToVACSecureServer() override { return false; }
	int Key_KeyStringToKeyNum( const char *string ) override { return g_engine->KeyFromName( string ); }
};

static CSurface *surface;

/*
==============================================================================

DRAWING

==============================================================================
*/
void CSurface::PushMakeCurrent( VPANEL panel, bool useInsets )
{
	paint_state_t s;
	int inset[4] = { 0, 0, 0, 0 }; // left, top, right, bottom

	if( useInsets )
		g_ipanel->GetInset( panel, inset[0], inset[1], inset[2], inset[3] );

	s.panel = panel;
	g_ipanel->GetAbsPos( panel, s.x, s.y );
	s.x += inset[0];
	s.y += inset[1];
	g_ipanel->GetClipRect( panel, s.clip[0], s.clip[1], s.clip[2], s.clip[3] );
	paint_stack.push_back( s );
}

void CSurface::PopMakeCurrent( VPANEL panel )
{
	if( !paint_stack.empty())
		paint_stack.pop_back();
}

// to window pixels, clipped to the current panel; false when nothing is left
bool CSurface::Clip( float &x0, float &y0, float &x1, float &y1, float &s0, float &t0, float &s1, float &t1 )
{
	float clip[4] = { 0.0f, 0.0f, (float)width, (float)height };

	if( !paint_stack.empty())
	{
		const paint_state_t &s = paint_stack.back();

		x0 += s.x; x1 += s.x;
		y0 += s.y; y1 += s.y;

		for( int i = 0; i < 4; i++ )
			clip[i] = s.clip[i];
	}

	if( x1 <= x0 || y1 <= y0 )
		return false;

	// cut the texture coordinates with the rectangle
	const float ds = ( s1 - s0 ) / ( x1 - x0 ), dt = ( t1 - t0 ) / ( y1 - y0 );

	if( x0 < clip[0] ) { s0 += ( clip[0] - x0 ) * ds; x0 = clip[0]; }
	if( y0 < clip[1] ) { t0 += ( clip[1] - y0 ) * dt; y0 = clip[1]; }
	if( x1 > clip[2] ) { s1 -= ( x1 - clip[2] ) * ds; x1 = clip[2]; }
	if( y1 > clip[3] ) { t1 -= ( y1 - clip[3] ) * dt; y1 = clip[3]; }

	return x1 > x0 && y1 > y0;
}

void CSurface::Fill( int ix0, int iy0, int ix1, int iy1 )
{
	float x0 = ix0, y0 = iy0, x1 = ix1, y1 = iy1, s0 = 0, t0 = 0, s1 = 1, t1 = 1;

	if( draw_color[3] && Clip( x0, y0, x1, y1, s0, t0, s1, t1 ))
		g_engine->FillRect( x0, y0, x1 - x0, y1 - y0, draw_color );
}

void CSurface::Textured( float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1, int texnum, const unsigned char *color, bool additive )
{
	if( texnum && color[3] && Clip( x0, y0, x1, y1, s0, t0, s1, t1 ))
		g_engine->DrawTexture( x0, y0, x1 - x0, y1 - y0, s0, t0, s1, t1, texnum, color, additive );
}

void CSurface::DrawOutlinedRect( int x0, int y0, int x1, int y1 )
{
	Fill( x0, y0, x1, y0 + 1 );
	Fill( x0, y1 - 1, x1, y1 );
	Fill( x0, y0 + 1, x0 + 1, y1 - 1 );
	Fill( x1 - 1, y0 + 1, x1, y1 - 1 );
}

void CSurface::DrawLine( int x0, int y0, int x1, int y1 )
{
	// VGUI2 draws straight lines: borders, separators, the graphs of the net panel step by pixels
	if( x0 == x1 || y0 == y1 )
	{
		Fill( x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, ( x0 < x1 ? x1 : x0 ) + 1, ( y0 < y1 ? y1 : y0 ) + 1 );
		return;
	}

	const int dx = x1 - x0, dy = y1 - y0;
	const int steps = abs( dx ) > abs( dy ) ? abs( dx ) : abs( dy );

	for( int i = 0; i <= steps; i++ )
	{
		int x = x0 + dx * i / steps, y = y0 + dy * i / steps;
		Fill( x, y, x + 1, y + 1 );
	}
}

void CSurface::DrawPolyLine( int *px, int *py, int numPoints )
{
	for( int i = 1; i < numPoints; i++ )
		DrawLine( px[i - 1], py[i - 1], px[i], py[i] );
}

void CSurface::DrawChar( wchar_t ch, bool additive )
{
	const glyph_t *g = Font_Glyph( text_font, ch );

	if( !g )
		return;

	if( g->page >= 0 )
	{
		const float inv = 1.0f / 512.0f;
		const float x = text_x + g->a, y = text_y + g->top;

		Textured( x, y, x + g->w, y + g->h, g->x * inv, g->y * inv, ( g->x + g->w ) * inv, ( g->y + g->h ) * inv,
			Font_PageTexture( g->page ), text_color, additive || Font_IsAdditive( text_font ));
	}

	text_x += g->a + g->b + g->c;
}

void CSurface::DrawPrintText( const wchar_t *text, int textLen )
{
	for( int i = 0; text && i < textLen; i++ )
		DrawChar( text[i], false );
}

void CSurface::DrawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 )
{
	auto it = textures.find( bound_texture );

	if( it != textures.end())
		Textured( x, y, x + wide, y + tall, s0, t0, s1, t1, it->second.texnum, text_color, false );
}

void CSurface::DrawTexturedRect( int x0, int y0, int x1, int y1 )
{
	auto it = textures.find( bound_texture );

	if( it != textures.end())
		Textured( x0, y0, x1, y1, 0, 0, 1, 1, it->second.texnum, draw_color, false );
}

void CSurface::DrawTexturedRectAdd( int x0, int y0, int x1, int y1 )
{
	auto it = textures.find( bound_texture );

	if( it != textures.end())
		Textured( x0, y0, x1, y1, 0, 0, 1, 1, it->second.texnum, draw_color, true );
}

void CSurface::DrawTexturedPolygon( VGuiVertex *v, int n )
{
	auto it = textures.find( bound_texture );

	if( it == textures.end() || n < 3 )
		return;

	// VGUI2 only draws rectangles this way (rotated images aside): their bounds
	float x0 = v[0].x, y0 = v[0].y, x1 = v[0].x, y1 = v[0].y;
	float s0 = v[0].s, t0 = v[0].t, s1 = v[0].s, t1 = v[0].t;

	for( int i = 1; i < n; i++ )
	{
		if( v[i].x < x0 ) { x0 = v[i].x; s0 = v[i].s; }
		if( v[i].x > x1 ) { x1 = v[i].x; s1 = v[i].s; }
		if( v[i].y < y0 ) { y0 = v[i].y; t0 = v[i].t; }
		if( v[i].y > y1 ) { y1 = v[i].y; t1 = v[i].t; }
	}

	Textured( x0, y0, x1, y1, s0, t0, s1, t1, it->second.texnum, draw_color, false );
}

/*
==============================================================================

TEXTURES

==============================================================================
*/
int CSurface::CreateNewTextureID( bool procedural )
{
	texture_t &t = textures[next_texture];

	t.texnum = 0;
	t.width = t.height = 0;
	return next_texture++;
}

void CSurface::DrawSetTextureFile( int id, const char *filename, int hardwareFilter, bool forceReload )
{
	texture_t &t = textures[id];

	if( !forceReload && t.texnum && t.file == filename )
		return;

	if( t.texnum )
		g_engine->FreeTexture( t.texnum );

	t.file = filename;
	t.texnum = g_engine->LoadTextureFile( filename, &t.width, &t.height );
	bound_texture = id;

	if( !t.texnum )
		Con_DPrintf( "vgui2: no image %s\n", filename );
}

void CSurface::DrawSetTextureRGBA( int id, const unsigned char *rgba, int wide, int tall, int hardwareFilter, bool forceReload )
{
	texture_t &t = textures[id];
	char name[32];

	if( !rgba || wide <= 0 || tall <= 0 )
		return;

	snprintf( name, sizeof( name ), "*vgui2_%d", id );
	t.texnum = g_engine->LoadTextureRGBA( name, rgba, wide, tall, t.texnum != 0 );
	t.width = wide;
	t.height = tall;
	t.file.clear();
	bound_texture = id;
}

void CSurface::DrawSetTextureBGRA( int id, const unsigned char *bgra, int wide, int tall )
{
	texture_t &t = textures[id];

	if( !bgra || wide <= 0 || tall <= 0 )
		return;

	t.rgba.resize( wide * tall * 4 );

	for( int i = 0; i < wide * tall; i++ )
	{
		t.rgba[i * 4 + 0] = bgra[i * 4 + 2];
		t.rgba[i * 4 + 1] = bgra[i * 4 + 1];
		t.rgba[i * 4 + 2] = bgra[i * 4 + 0];
		t.rgba[i * 4 + 3] = bgra[i * 4 + 3];
	}

	std::vector<unsigned char> copy( t.rgba ); // DrawSetTextureRGBA doesn't keep it
	DrawSetTextureRGBA( id, copy.data(), wide, tall, 0, true );
	t.rgba.swap( copy );
}

void CSurface::DrawUpdateRegionTextureBGRA( int id, int x, int y, const unsigned char *data, int wide, int tall )
{
	auto it = textures.find( id );

	if( it == textures.end() || it->second.rgba.empty() || !data )
		return;

	texture_t &t = it->second;

	for( int row = 0; row < tall && y + row < t.height; row++ )
	{
		for( int col = 0; col < wide && x + col < t.width; col++ )
		{
			const unsigned char *in = &data[( row * wide + col ) * 4];
			unsigned char *out = &t.rgba[(( y + row ) * t.width + x + col ) * 4];

			out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3];
		}
	}

	char name[32];
	snprintf( name, sizeof( name ), "*vgui2_%d", id );
	t.texnum = g_engine->LoadTextureRGBA( name, t.rgba.data(), t.width, t.height, t.texnum != 0 );
}

void CSurface::DrawGetTextureSize( int id, int &wide, int &tall )
{
	auto it = textures.find( id );

	wide = it != textures.end() ? it->second.width : 0;
	tall = it != textures.end() ? it->second.height : 0;
}

bool CSurface::DeleteTextureByID( int id )
{
	auto it = textures.find( id );

	if( it == textures.end())
		return false;

	if( it->second.texnum )
		g_engine->FreeTexture( it->second.texnum );

	textures.erase( it );
	return true;
}

/*
==============================================================================

TEXT

==============================================================================
*/
void CSurface::GetCharABCwide( HFont font, int ch, int &a, int &b, int &c )
{
	const glyph_t *g = Font_Glyph( font, ch );

	a = g ? g->a : 0;
	b = g ? g->b : 0;
	c = g ? g->c : 0;
}

int CSurface::GetCharacterWidth( HFont font, int ch )
{
	const glyph_t *g = Font_Glyph( font, ch );
	return g ? g->a + g->b + g->c : 0;
}

void CSurface::GetTextSize( HFont font, const wchar_t *text, int &wide, int &tall )
{
	wide = 0;
	tall = Font_Tall( font );

	for( ; text && *text; text++ )
		wide += GetCharacterWidth( font, *text );
}

/*
==============================================================================

POPUPS AND TRAVERSALS

==============================================================================
*/
void CSurface::CreatePopup( VPANEL panel, bool minimised, bool showTaskbarIcon, bool disabled, bool mouseInput, bool kbInput )
{
	if( !g_ipanel->GetParent( panel ))
		g_ipanel->SetParent( panel, root.vpanel );

	g_ipanel->SetPopup( panel, true );
	g_ipanel->SetKeyBoardInputEnabled( panel, kbInput );
	g_ipanel->SetMouseInputEnabled( panel, mouseInput );

	const unsigned long handle = g_ivgui->PanelToHandle( panel );

	for( unsigned long h : popups )
	{
		if( h == handle )
		{
			MovePopupToFront( panel );
			return;
		}
	}

	popups.push_back( handle );
}

VPANEL CSurface::GetPopup( int index )
{
	if( index < 0 || index >= (int)popups.size())
		return 0;

	return g_ivgui->HandleToPanel( popups[index] );
}

void CSurface::ReleasePanel( VPANEL panel )
{
	const unsigned long handle = g_ivgui->PanelToHandle( panel );

	for( size_t i = 0; i < popups.size(); i++ )
	{
		if( popups[i] == handle )
		{
			popups.erase( popups.begin() + i );
			break;
		}
	}

	if( modal == panel )
		modal = 0;
	if( restrict_panel == panel )
		restrict_panel = 0;
}

void CSurface::MovePopupToFront( VPANEL panel )
{
	const unsigned long handle = g_ivgui->PanelToHandle( panel );

	for( size_t i = 0; i < popups.size(); i++ )
	{
		if( popups[i] == handle )
		{
			popups.erase( popups.begin() + i );
			popups.push_back( handle );
			return;
		}
	}
}

void CSurface::MovePopupToBack( VPANEL panel )
{
	const unsigned long handle = g_ivgui->PanelToHandle( panel );

	for( size_t i = 0; i < popups.size(); i++ )
	{
		if( popups[i] == handle )
		{
			popups.erase( popups.begin() + i );
			popups.insert( popups.begin(), handle );
			return;
		}
	}
}

void CSurface::BringToFront( VPANEL panel )
{
	g_ipanel->MoveToFront( panel );

	if( g_ipanel->IsPopup( panel ))
		MovePopupToFront( panel );
}

void CSurface::SetTopLevelFocus( VPANEL panel )
{
	// the popup the panel is in comes to the front
	while( panel && !g_ipanel->IsPopup( panel ))
		panel = g_ipanel->GetParent( panel );

	if( panel )
		MovePopupToFront( panel );
}

bool CSurface::ShouldPaintChildPanel( VPANEL panel )
{
	if( restrict_panel && !g_ipanel->HasParent( panel, restrict_panel ))
		return false;

	// a popup is drawn on its own, after the tree
	return !g_ipanel->IsPopup( panel );
}

// the popup and all its parents are visible
bool CSurface::PopupVisible( VPANEL panel )
{
	for( ; panel; panel = g_ipanel->GetParent( panel ))
	{
		if( !g_ipanel->IsVisible( panel ))
			return false;
	}

	return true;
}

void CSurface::ApplySchemeTraverse( VPANEL panel, bool force )
{
	if( !force && !g_ipanel->IsVisible( panel ))
		return;

	g_ipanel->PerformApplySchemeSettings( panel );

	for( int i = 0; i < g_ipanel->GetChildCount( panel ); i++ )
		ApplySchemeTraverse( g_ipanel->GetChild( panel, i ), force );
}

void CSurface::ThinkTraverse( VPANEL panel )
{
	if( !g_ipanel->IsVisible( panel ))
		return;

	g_ipanel->Think( panel );

	for( int i = 0; i < g_ipanel->GetChildCount( panel ); i++ )
		ThinkTraverse( g_ipanel->GetChild( panel, i ));
}

void CSurface::SolvePanelTraverse( VPANEL panel )
{
	if( !g_ipanel->IsVisible( panel ))
		return;

	g_ipanel->Solve( panel );

	for( int i = 0; i < g_ipanel->GetChildCount( panel ); i++ )
		SolvePanelTraverse( g_ipanel->GetChild( panel, i ));
}

void CSurface::SolveTraverse( VPANEL panel, bool forceApplySchemeSettings )
{
	ApplySchemeTraverse( panel, forceApplySchemeSettings );
	ThinkTraverse( panel );
	SolvePanelTraverse( panel );
}

void CSurface::PaintTraverse( VPANEL panel )
{
	paint_stack.clear();

	if( panel != root.vpanel )
	{
		g_ipanel->PaintTraverse( panel, true, true );
		return;
	}

	g_ipanel->PaintTraverse( panel, true, true );

	// the popups over it, back to front; one may close another while drawing
	for( size_t i = 0; i < popups.size(); i++ )
	{
		VPANEL popup = g_ivgui->HandleToPanel( popups[i] );

		if( popup && PopupVisible( popup ) && ( !restrict_panel || g_ipanel->HasParent( popup, restrict_panel )))
			g_ipanel->PaintTraverse( popup, true, true );
	}

	paint_stack.clear();
}

void CSurface::CalculateMouseVisible()
{
	bool mouse = false, keyboard = false;

	for( size_t i = 0; i < popups.size(); i++ )
	{
		VPANEL popup = g_ivgui->HandleToPanel( popups[i] );

		if( !popup || !PopupVisible( popup ))
			continue;

		mouse |= g_ipanel->IsMouseInputEnabled( popup );
		keyboard |= g_ipanel->IsKeyBoardInputEnabled( popup );
	}

	if( mouse != needs_mouse )
	{
		needs_mouse = mouse;
		g_engine->ShowCursor( mouse, mouse ? dc_arrow : dc_none );

		// what was under the cursor no longer is
		if( !mouse )
			g_input->SetMouseFocus( 0 );
	}

	if( keyboard != needs_keyboard )
	{
		needs_keyboard = keyboard;
		g_engine->EnableTextInput( keyboard );
	}
}

/*
==============================================================================

IGameUIFuncs

==============================================================================
*/
bool CSurface::IsKeyDown( const char *keyname, bool &isdown )
{
	int key = g_engine->KeyFromName( keyname );

	isdown = key >= 0 && g_engine->KeyIsDown( key );
	return key >= 0;
}

KeyCode CSurface::GetVGUI2KeyCodeForBind( const char *bind )
{
	for( int key = 0; key < 256; key++ )
	{
		const char *b = g_engine->KeyBinding( key );
		int code;

		if( b && !strcasecmp( b, bind ) && ( code = g_engine->KeyToSystem( key )))
			return g_system->KeyCode_VirtualKeyToVGUI( code );
	}

	return 0; // KEY_NONE
}

void CSurface::GetVideoModes( vmode_t **liststart, int *count )
{
	static vmode_t mode;

	g_engine->GetVideoMode( &mode.width, &mode.height, &mode.bpp );
	*liststart = &mode;
	*count = 1;
}

void CSurface::GetCurrentRenderer( char *name, int namelen, int *windowed, int *hdmodels, int *addons_folder )
{
	if( name && namelen > 0 )
		snprintf( name, namelen, "%s", "opengl" );
	if( windowed ) *windowed = 1;
	if( hdmodels ) *hdmodels = 0;
	if( addons_folder ) *addons_folder = 0;
}

/*
==============================================================================

MODULE

==============================================================================
*/
ISurface *Surface_Get( void )
{
	if( !surface )
		surface = new CSurface;
	return surface;
}

void *Surface_GameUIFuncs( void )
{
	Surface_Get();
	return static_cast<IGameUIFuncs *>( surface );
}

static void Surface_InitRoot( CRootPanel &panel, const char *name, VPANEL parent, int width, int height )
{
	panel.name = name;
	panel.vpanel = g_ivgui->AllocPanel();
	g_ipanel->Init( panel.vpanel, &panel );
	g_ipanel->SetPos( panel.vpanel, 0, 0 );
	g_ipanel->SetSize( panel.vpanel, width, height );
	g_ipanel->SetVisible( panel.vpanel, true );
	g_ipanel->SetMouseInputEnabled( panel.vpanel, false );
	g_ipanel->SetKeyBoardInputEnabled( panel.vpanel, false );

	if( parent )
		g_ipanel->SetParent( panel.vpanel, parent );
}

void Surface_Init( int width, int height )
{
	Surface_Get();
	surface->width = width;
	surface->height = height;
	Surface_InitRoot( surface->root, "RootPanel", 0, width, height );
	Surface_InitRoot( surface->client_root, "ClientDLLPanel", surface->root.vpanel, width, height );
}

VPANEL Surface_ClientRoot( void )
{
	return surface ? surface->client_root.vpanel : 0;
}

void Surface_SetScreenSize( int width, int height )
{
	if( !surface )
		return;

	surface->width = width;
	surface->height = height;

	if( surface->root.vpanel )
	{
		g_ipanel->SetSize( surface->root.vpanel, width, height );
		g_ipanel->SetSize( surface->client_root.vpanel, width, height );
	}
}

void Surface_Paint( void )
{
	if( surface && surface->root.vpanel )
		surface->PaintTraverse( surface->root.vpanel );
}

bool Surface_NeedsMouse( void )
{
	return surface && surface->needs_mouse;
}

bool Surface_NeedsKeyboard( void )
{
	return surface && surface->needs_keyboard;
}

void Surface_Shutdown( void )
{
	if( !surface )
		return;

	for( auto &t : surface->textures )
	{
		if( t.second.texnum )
			g_engine->FreeTexture( t.second.texnum );
	}

	if( surface->needs_mouse )
		g_engine->ShowCursor( false, dc_none );

	Font_Shutdown();
	delete surface;
	surface = NULL;
}
