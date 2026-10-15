/*
interfaces.h - the GoldSrc VGUI2 interfaces vgui2_support talks through
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
Only the order of the virtual functions matters: these classes are called
through the vtables of Valve's binaries, or call into them. The order was
read from the symbols of the Steam GoldSrc libraries (hw.so, vgui2.so,
cstrike's client.so, 2023 update): see Documentation/vgui2.md. Functions
nothing here calls are declared with a placeholder name to keep the slots.
*/

#pragma once
#ifndef VGUI2_INTERFACES_H
#define VGUI2_INTERFACES_H

#include <stddef.h>
#include <wchar.h>

namespace vgui2
{

typedef unsigned int VPANEL;
typedef unsigned long HFont;
typedef unsigned long HCursor;
typedef unsigned long HScheme;
typedef int KeyCode;
typedef int MouseCode;

// passed and returned by value like Valve's Color, a trivially copyable 4 bytes class
struct Color
{
	unsigned char r, g, b, a;
};

struct VGuiVertex
{
	float x, y, s, t;
};

enum
{
	dc_user, dc_none, dc_arrow, dc_ibeam, dc_hourglass, dc_waitarrow, dc_crosshair, dc_up,
	dc_sizenwse, dc_sizenesw, dc_sizewe, dc_sizens, dc_sizeall, dc_no, dc_hand, dc_blank, dc_last,
};

enum
{
	FONTFLAG_NONE = 0,
	FONTFLAG_ITALIC = 0x001,
	FONTFLAG_UNDERLINE = 0x002,
	FONTFLAG_STRIKEOUT = 0x004,
	FONTFLAG_SYMBOL = 0x008,
	FONTFLAG_ANTIALIAS = 0x010,
	FONTFLAG_GAUSSIANBLUR = 0x020,
	FONTFLAG_ROTARY = 0x040,
	FONTFLAG_DROPSHADOW = 0x080,
	FONTFLAG_ADDITIVE = 0x100,
	FONTFLAG_OUTLINE = 0x200,
};

enum
{
	MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE, MOUSE_4, MOUSE_5,
};

class IClientPanel;
class IHTML;
class IHTMLEvents;
class Image;

class ISurface // VGUI_Surface026, implemented here
{
public:
	virtual ~ISurface() { }
	virtual void Shutdown() = 0;
	virtual void RunFrame() = 0;
	virtual VPANEL GetEmbeddedPanel() = 0;
	virtual void SetEmbeddedPanel( VPANEL panel ) = 0;
	virtual void PushMakeCurrent( VPANEL panel, bool useInsets ) = 0;
	virtual void PopMakeCurrent( VPANEL panel ) = 0;
	virtual void DrawSetColor( int r, int g, int b, int a ) = 0;
	virtual void DrawSetColor( Color col ) = 0;
	virtual void DrawFilledRect( int x0, int y0, int x1, int y1 ) = 0;
	virtual void DrawOutlinedRect( int x0, int y0, int x1, int y1 ) = 0; // 11
	virtual void DrawLine( int x0, int y0, int x1, int y1 ) = 0;
	virtual void DrawPolyLine( int *px, int *py, int numPoints ) = 0;
	virtual void DrawSetTextFont( HFont font ) = 0;
	virtual void DrawSetTextColor( int r, int g, int b, int a ) = 0;
	virtual void DrawSetTextColor( Color col ) = 0;
	virtual void DrawSetTextPos( int x, int y ) = 0;
	virtual void DrawGetTextPos( int &x, int &y ) = 0;
	virtual void DrawPrintText( const wchar_t *text, int textLen ) = 0;
	virtual void DrawUnicodeChar( wchar_t wch ) = 0;
	virtual void DrawUnicodeCharAdd( wchar_t wch ) = 0; // 21
	virtual void DrawFlushText() = 0;
	virtual IHTML *CreateHTMLWindow( IHTMLEvents *events, VPANEL context ) = 0;
	virtual void PaintHTMLWindow( IHTML *htmlwin ) = 0;
	virtual void DeleteHTMLWindow( IHTML *htmlwin ) = 0;
	virtual void DrawSetTextureFile( int id, const char *filename, int hardwareFilter, bool forceReload ) = 0;
	virtual void DrawSetTextureRGBA( int id, const unsigned char *rgba, int wide, int tall, int hardwareFilter, bool forceReload ) = 0;
	virtual void DrawSetTexture( int id ) = 0;
	virtual void DrawGetTextureSize( int id, int &wide, int &tall ) = 0;
	virtual void DrawTexturedRect( int x0, int y0, int x1, int y1 ) = 0;
	virtual bool IsTextureIDValid( int id ) = 0; // 31
	virtual int CreateNewTextureID( bool procedural ) = 0;
	virtual void GetScreenSize( int &wide, int &tall ) = 0;
	virtual void SetAsTopMost( VPANEL panel, bool state ) = 0;
	virtual void BringToFront( VPANEL panel ) = 0;
	virtual void SetForegroundWindow( VPANEL panel ) = 0;
	virtual void SetPanelVisible( VPANEL panel, bool state ) = 0;
	virtual void SetMinimized( VPANEL panel, bool state ) = 0;
	virtual bool IsMinimized( VPANEL panel ) = 0;
	virtual void FlashWindow( VPANEL panel, bool state ) = 0;
	virtual void SetTitle( VPANEL panel, const wchar_t *title ) = 0; // 41
	virtual void SetAsToolBar( VPANEL panel, bool state ) = 0;
	virtual void CreatePopup( VPANEL panel, bool minimised, bool showTaskbarIcon, bool disabled, bool mouseInput, bool kbInput ) = 0;
	virtual void SwapBuffers( VPANEL panel ) = 0;
	virtual void Invalidate( VPANEL panel ) = 0;
	virtual void SetCursor( HCursor cursor ) = 0;
	virtual bool IsCursorVisible() = 0;
	virtual void ApplyChanges() = 0;
	virtual bool IsWithin( int x, int y ) = 0;
	virtual bool HasFocus() = 0;
	virtual bool SupportsFeature( int feature ) = 0; // 51
	virtual void RestrictPaintToSinglePanel( VPANEL panel ) = 0;
	virtual void SetModalPanel( VPANEL panel ) = 0;
	virtual VPANEL GetModalPanel() = 0;
	virtual void UnlockCursor() = 0;
	virtual void LockCursor() = 0;
	virtual void SetTranslateExtendedKeys( bool state ) = 0;
	virtual VPANEL GetTopmostPopup() = 0;
	virtual void SetTopLevelFocus( VPANEL panel ) = 0;
	virtual HFont CreateFont() = 0;
	virtual bool AddGlyphSetToFont( HFont font, const char *fontName, int tall, int weight, int blur, int scanlines, int flags, int lowRange, int highRange ) = 0; // 61
	virtual bool AddCustomFontFile( const char *fontFileName ) = 0;
	virtual int GetFontTall( HFont font ) = 0;
	virtual void GetCharABCwide( HFont font, int ch, int &a, int &b, int &c ) = 0;
	virtual int GetCharacterWidth( HFont font, int ch ) = 0;
	virtual void GetTextSize( HFont font, const wchar_t *text, int &wide, int &tall ) = 0;
	virtual VPANEL GetNotifyPanel() = 0;
	virtual void SetNotifyIcon( VPANEL context, unsigned long icon, VPANEL panelToReceiveMessages, const char *text ) = 0;
	virtual void PlaySound( const char *fileName ) = 0;
	virtual int GetPopupCount() = 0;
	virtual VPANEL GetPopup( int index ) = 0; // 71
	virtual bool ShouldPaintChildPanel( VPANEL panel ) = 0;
	virtual bool RecreateContext( VPANEL panel ) = 0;
	virtual void AddPanel( VPANEL panel ) = 0;
	virtual void ReleasePanel( VPANEL panel ) = 0;
	virtual void MovePopupToFront( VPANEL panel ) = 0;
	virtual void MovePopupToBack( VPANEL panel ) = 0;
	virtual void SolveTraverse( VPANEL panel, bool forceApplySchemeSettings ) = 0;
	virtual void PaintTraverse( VPANEL panel ) = 0;
	virtual void EnableMouseCapture( VPANEL panel, bool state ) = 0;
	virtual void GetWorkspaceBounds( int &x, int &y, int &wide, int &tall ) = 0; // 81
	virtual void GetAbsoluteWindowBounds( int &x, int &y, int &wide, int &tall ) = 0;
	virtual void GetProportionalBase( int &width, int &height ) = 0;
	virtual void CalculateMouseVisible() = 0;
	virtual bool NeedKBInput() = 0;
	virtual bool HasCursorPosFunctions() = 0;
	virtual void SurfaceGetCursorPos( int &x, int &y ) = 0;
	virtual void SurfaceSetCursorPos( int x, int y ) = 0;
	virtual void DrawTexturedPolygon( VGuiVertex *vertices, int n ) = 0;
	virtual int GetFontAscent( HFont font, wchar_t wch ) = 0;
	virtual void SetAllowHTMLJavaScript( bool state ) = 0; // 91
	virtual void SetLanguage( const char *language ) = 0;
	virtual const char *GetLanguage() = 0;
	virtual bool DeleteTextureByID( int id ) = 0;
	virtual void DrawUpdateRegionTextureBGRA( int id, int x, int y, const unsigned char *data, int wide, int tall ) = 0;
	virtual void DrawSetTextureBGRA( int id, const unsigned char *bgra, int wide, int tall ) = 0;
	virtual void CreateBrowser( VPANEL panel, void *browser, bool popupWindow, const char *userAgent ) = 0;
	virtual void RemoveBrowser( VPANEL panel, void *browser ) = 0;
	virtual void *AccessChromeHTMLController() = 0;
	// the 2013 and later additions
	virtual void DrawTexturedRectAdd( int x0, int y0, int x1, int y1 ) = 0;
	virtual void SetSupportsEsc( bool state ) = 0; // 101
	virtual int GetFontBlur( HFont font ) = 0;
	virtual bool IsAdditive( HFont font ) = 0;
	virtual void SetProportionalBase( int width, int height ) = 0;
	virtual void GetHDProportionalBase( int &width, int &height ) = 0;
	virtual void SetHDProportionalBase( int width, int height ) = 0;
	virtual void SetFullscreenMode( int width, int height, int bpp ) = 0;
	virtual void SetWindowedMode() = 0;
	virtual int Unknown109( int a, int b ) = 0; // folded with empty functions in hw.so, names unknown
	virtual int Unknown110( int a, int b ) = 0;
	virtual int Unknown111( int a, int b ) = 0; // 111
	virtual int Unknown112( int a, int b ) = 0;
	virtual void DrawPrintChar( int x, int y, int wide, int tall, float s0, float t0, float s1, float t1 ) = 0;
	virtual void SetNotifyIcon( Image *image, VPANEL panelToReceiveMessages, const char *text ) = 0;
	virtual void SetWatchForComputerUse( bool state ) = 0;
	virtual double GetTimeSinceLastUse() = 0;
	virtual bool VGUI2MouseControl() = 0;
	virtual void SetVGUI2MouseControl( bool state ) = 0;
};

class IClientPanel // implemented by every panel, here by the engine's root panels
{
public:
	virtual VPANEL GetVPanel() = 0;
	virtual void Think() = 0;
	virtual void PerformApplySchemeSettings() = 0;
	virtual void PaintTraverse( bool forceRepaint, bool allowForce ) = 0;
	virtual void Repaint() = 0;
	virtual VPANEL IsWithinTraverse( int x, int y, bool traversePopups ) = 0;
	virtual void GetInset( int &top, int &left, int &right, int &bottom ) = 0;
	virtual void GetClipRect( int &x0, int &y0, int &x1, int &y1 ) = 0;
	virtual void OnChildAdded( VPANEL child ) = 0;
	virtual void OnSizeChanged( int newWide, int newTall ) = 0;
	virtual void InternalFocusChanged( bool lost ) = 0;
	virtual bool RequestInfo( void *outputData ) = 0;
	virtual void RequestFocus( int direction ) = 0;
	virtual bool RequestFocusPrev( VPANEL existingPanel ) = 0;
	virtual bool RequestFocusNext( VPANEL existingPanel ) = 0;
	virtual void OnMessage( const void *params, VPANEL fromPanel ) = 0;
	virtual VPANEL GetCurrentKeyFocus() = 0;
	virtual int GetTabPosition() = 0;
	virtual const char *GetName() = 0;
	virtual const char *GetClassName() = 0;
	virtual HScheme GetScheme() = 0;
	virtual bool IsProportional() = 0;
	virtual bool IsAutoDeleteSet() = 0;
	virtual void DeletePanel() = 0;
	virtual void *QueryInterface( int id ) = 0;
	virtual void *GetPanel() = 0;
	virtual const char *GetModuleName() = 0;
	virtual void *GetMessageMap() = 0;
};

class IVGui // VGUI_ivgui006, vgui2 library
{
public:
	virtual ~IVGui() { }
	virtual bool Init( void *( **factories )( const char *, int * ), int count ) = 0;
	virtual void Shutdown() = 0;
	virtual void Start() = 0;
	virtual void Stop() = 0;
	virtual bool IsRunning() = 0;
	virtual void RunFrame() = 0;
	virtual void ShutdownMessage( unsigned int shutdownID ) = 0;
	virtual VPANEL AllocPanel() = 0;
	virtual void FreePanel( VPANEL panel ) = 0;
	virtual void DPrintf( const char *format, ... ) = 0;
	virtual void DPrintf2( const char *format, ... ) = 0;
	virtual void SpewAllActivePanelNames() = 0;
	virtual unsigned long PanelToHandle( VPANEL panel ) = 0;
	virtual VPANEL HandleToPanel( unsigned long index ) = 0;
	virtual void MarkPanelForDeletion( VPANEL panel ) = 0;
	virtual void AddTickSignal( VPANEL panel, int intervalMilliseconds ) = 0;
	virtual void RemoveTickSignal( VPANEL panel ) = 0;
	virtual void PostMessage( VPANEL target, void *params, VPANEL from, float delaySeconds ) = 0;
	virtual int CreateContext() = 0;
	virtual void DestroyContext( int context ) = 0;
	virtual void AssociatePanelWithContext( int context, VPANEL root ) = 0;
	virtual void ActivateContext( int context ) = 0;
	virtual void SetSleep( bool state ) = 0;
	virtual bool GetShouldVGuiControlSleep() = 0;
};

class IPanel // VGUI_Panel007, vgui2 library
{
public:
	virtual ~IPanel() { }
	virtual void Init( VPANEL panel, IClientPanel *clientPanel ) = 0;
	virtual void SetPos( VPANEL panel, int x, int y ) = 0;
	virtual void GetPos( VPANEL panel, int &x, int &y ) = 0;
	virtual void SetSize( VPANEL panel, int wide, int tall ) = 0;
	virtual void GetSize( VPANEL panel, int &wide, int &tall ) = 0;
	virtual void SetMinimumSize( VPANEL panel, int wide, int tall ) = 0;
	virtual void GetMinimumSize( VPANEL panel, int &wide, int &tall ) = 0;
	virtual void SetZPos( VPANEL panel, int z ) = 0;
	virtual int GetZPos( VPANEL panel ) = 0;
	virtual void GetAbsPos( VPANEL panel, int &x, int &y ) = 0;
	virtual void GetClipRect( VPANEL panel, int &x0, int &y0, int &x1, int &y1 ) = 0;
	virtual void SetInset( VPANEL panel, int left, int top, int right, int bottom ) = 0;
	virtual void GetInset( VPANEL panel, int &left, int &top, int &right, int &bottom ) = 0;
	virtual void SetVisible( VPANEL panel, bool state ) = 0;
	virtual bool IsVisible( VPANEL panel ) = 0;
	virtual void SetParent( VPANEL panel, VPANEL newParent ) = 0;
	virtual int GetChildCount( VPANEL panel ) = 0;
	virtual VPANEL GetChild( VPANEL panel, int index ) = 0;
	virtual VPANEL GetParent( VPANEL panel ) = 0;
	virtual void MoveToFront( VPANEL panel ) = 0;
	virtual void MoveToBack( VPANEL panel ) = 0;
	virtual bool HasParent( VPANEL panel, VPANEL potentialParent ) = 0;
	virtual bool IsPopup( VPANEL panel ) = 0;
	virtual void SetPopup( VPANEL panel, bool state ) = 0;
	virtual bool Render_GetPopupVisible( VPANEL panel ) = 0;
	virtual void Render_SetPopupVisible( VPANEL panel, bool state ) = 0;
	virtual HScheme GetScheme( VPANEL panel ) = 0;
	virtual bool IsProportional( VPANEL panel ) = 0;
	virtual bool IsAutoDeleteSet( VPANEL panel ) = 0;
	virtual void DeletePanel( VPANEL panel ) = 0;
	virtual void SetKeyBoardInputEnabled( VPANEL panel, bool state ) = 0;
	virtual void SetMouseInputEnabled( VPANEL panel, bool state ) = 0;
	virtual bool IsKeyBoardInputEnabled( VPANEL panel ) = 0;
	virtual bool IsMouseInputEnabled( VPANEL panel ) = 0;
	virtual void Solve( VPANEL panel ) = 0;
	virtual const char *GetName( VPANEL panel ) = 0;
	virtual const char *GetClassName( VPANEL panel ) = 0;
	virtual void SendMessage( VPANEL panel, void *params, VPANEL ifromPanel ) = 0;
	virtual void Think( VPANEL panel ) = 0;
	virtual void PerformApplySchemeSettings( VPANEL panel ) = 0;
	virtual void PaintTraverse( VPANEL panel, bool forceRepaint, bool allowForce ) = 0;
	virtual void Repaint( VPANEL panel ) = 0;
	virtual VPANEL IsWithinTraverse( VPANEL panel, int x, int y, bool traversePopups ) = 0;
	virtual void OnChildAdded( VPANEL panel, VPANEL child ) = 0;
	virtual void OnSizeChanged( VPANEL panel, int newWide, int newTall ) = 0;
	virtual void InternalFocusChanged( VPANEL panel, bool lost ) = 0;
	virtual bool RequestInfo( VPANEL panel, void *outputData ) = 0;
	virtual void RequestFocus( VPANEL panel, int direction ) = 0;
	virtual bool RequestFocusPrev( VPANEL panel, VPANEL existingPanel ) = 0;
	virtual bool RequestFocusNext( VPANEL panel, VPANEL existingPanel ) = 0;
	virtual VPANEL GetCurrentKeyFocus( VPANEL panel ) = 0;
	virtual int GetTabPosition( VPANEL panel ) = 0;
	virtual void *Plat( VPANEL panel ) = 0;
	virtual void SetPlat( VPANEL panel, void *plat ) = 0;
	virtual void *GetPanel( VPANEL panel, const char *destinationModule ) = 0;
	virtual bool IsEnabled( VPANEL panel ) = 0;
	virtual void SetEnabled( VPANEL panel, bool state ) = 0;
	virtual IClientPanel *Client( VPANEL panel ) = 0;
	virtual const char *GetModuleName( VPANEL panel ) = 0;
};

class IInputInternal // VGUI_InputInternal001, vgui2 library
{
public:
	virtual ~IInputInternal() { }
	virtual void SetMouseFocus( VPANEL newMouseFocus ) = 0;
	virtual void SetMouseCapture( VPANEL panel ) = 0;
	virtual void GetKeyCodeText( KeyCode code, char *buf, int buflen ) = 0;
	virtual VPANEL GetFocus() = 0;
	virtual VPANEL GetMouseOver() = 0;
	virtual void SetCursorPos( int x, int y ) = 0;
	virtual void GetCursorPos( int &x, int &y ) = 0;
	virtual bool WasMousePressed( MouseCode code ) = 0;
	virtual bool WasMouseDoublePressed( MouseCode code ) = 0;
	virtual bool IsMouseDown( MouseCode code ) = 0;
	virtual void SetCursorOveride( HCursor cursor ) = 0;
	virtual HCursor GetCursorOveride() = 0;
	virtual bool WasMouseReleased( MouseCode code ) = 0;
	virtual bool WasKeyPressed( KeyCode code ) = 0;
	virtual bool IsKeyDown( KeyCode code ) = 0;
	virtual bool WasKeyTyped( KeyCode code ) = 0;
	virtual bool WasKeyReleased( KeyCode code ) = 0;
	virtual VPANEL GetAppModalSurface() = 0;
	virtual void SetAppModalSurface( VPANEL panel ) = 0;
	virtual void ReleaseAppModalSurface() = 0;
	virtual void GetCursorPosition( int &x, int &y ) = 0;
	virtual void RunFrame() = 0;
	virtual void UpdateMouseFocus( int x, int y ) = 0;
	virtual void PanelDeleted( VPANEL panel ) = 0;
	virtual void InternalCursorMoved( int x, int y ) = 0;
	virtual void InternalMousePressed( MouseCode code ) = 0;
	virtual void InternalMouseDoublePressed( MouseCode code ) = 0;
	virtual void InternalMouseReleased( MouseCode code ) = 0;
	virtual void InternalMouseWheeled( int delta ) = 0;
	virtual void InternalKeyCodePressed( KeyCode code ) = 0;
	virtual void InternalKeyCodeTyped( KeyCode code ) = 0;
	virtual void InternalKeyTyped( wchar_t unichar ) = 0;
	virtual void InternalKeyCodeReleased( KeyCode code ) = 0;
	virtual int CreateInputContext() = 0;
	virtual void DestroyInputContext( int context ) = 0;
	virtual void AssociatePanelWithInputContext( int context, VPANEL root ) = 0;
	virtual void ActivateInputContext( int context ) = 0;
	virtual VPANEL GetMouseCapture() = 0;
	virtual bool IsChildOfModalPanel( VPANEL panel ) = 0;
	virtual void ResetInputContext( int context ) = 0;
};

class ISchemeManager // VGUI_Scheme009, vgui2 library
{
public:
	virtual ~ISchemeManager() { }
	virtual HScheme LoadSchemeFromFile( const char *fileName, const char *tag ) = 0;
	virtual void ReloadSchemes() = 0;
	virtual HScheme GetDefaultScheme() = 0;
	virtual HScheme GetScheme( const char *tag ) = 0;
	virtual void *GetImage( const char *imageName, bool hardwareFiltered ) = 0;
	virtual int GetImageID( const char *imageName, bool hardwareFiltered ) = 0;
	virtual void *GetIScheme( HScheme scheme ) = 0;
	virtual void Shutdown( bool full ) = 0;
	virtual int GetProportionalScaledValue( int normalizedValue ) = 0;
	virtual int GetProportionalNormalizedValue( int scaledValue ) = 0;
};

class IKeyValues // KeyValues003, vgui2 library
{
public:
	virtual ~IKeyValues() { }
	virtual void RegisterSizeofKeyValues( int size ) = 0; // before the first KeyValues is made
	virtual void *AllocKeyValuesMemory( int size ) = 0;
	virtual void FreeKeyValuesMemory( void *memory ) = 0;
};

class ILocalize // VGUI_Localize003, vgui2 library
{
public:
	virtual ~ILocalize() { }
	virtual bool AddFile( void *fileSystem, const char *fileName ) = 0;
	virtual void RemoveAll() = 0;
	virtual wchar_t *Find( const char *tokenName ) = 0;
};

class ISystem // VGUI_System009, vgui2 library
{
public:
	virtual ~ISystem() { }
	virtual void Shutdown() = 0;
	virtual void RunFrame() = 0;
	virtual void ShellExecute( const char *command, const char *file ) = 0;
	virtual double GetFrameTime() = 0;
	virtual double GetCurrentTime() = 0;
	virtual long GetTimeMillis() = 0;
	virtual int GetClipboardTextCount() = 0;
	virtual void SetClipboardText( const char *text, int textLen ) = 0;
	virtual void SetClipboardText( const wchar_t *text, int textLen ) = 0;
	virtual int GetClipboardText( int offset, char *buf, int bufLen ) = 0;
	virtual int GetClipboardText( int offset, wchar_t *buf, int bufLen ) = 0;
	virtual bool SetRegistryString( const char *key, const char *value ) = 0;
	virtual bool GetRegistryString( const char *key, char *value, int valueLen ) = 0;
	virtual bool SetRegistryInteger( const char *key, int value ) = 0;
	virtual bool GetRegistryInteger( const char *key, int &value ) = 0;
	virtual void *GetUserConfigFileData( const char *dialogName, int dialogID ) = 0;
	virtual void SetUserConfigFile( const char *fileName, const char *pathName ) = 0;
	virtual void SaveUserConfigFile() = 0;
	virtual bool SetWatchForComputerUse( bool state ) = 0;
	virtual double GetTimeSinceLastUse() = 0;
	virtual int GetAvailableDrives( char *buf, int bufLen ) = 0;
	virtual bool CommandLineParamExists( const char *paramName ) = 0;
	virtual const char *GetFullCommandLine() = 0;
	virtual bool GetCurrentTimeAndDate( int *year, int *month, int *dayOfWeek, int *day, int *hour, int *minute, int *second ) = 0;
	virtual double GetFreeDiskSpace( const char *path ) = 0;
	virtual bool CreateShortcut( const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory, const char *iconFile ) = 0;
	virtual bool GetShortcutTarget( const char *linkFileName, char *targetPath, char *arguments, int destBufferSizes ) = 0;
	virtual bool ModifyShortcutTarget( const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory ) = 0;
	virtual bool GetCommandLineParamValue( const char *paramName, char *value, int valueBufferSize ) = 0;
	virtual bool DeleteRegistryKey( const char *keyName ) = 0;
	virtual const char *GetDesktopFolderPath() = 0;
	virtual KeyCode KeyCode_VirtualKeyToVGUI( int keyCode ) = 0; // an SDL2 keycode on Linux
	virtual int KeyCode_VGUIToVirtualKey( KeyCode keyCode ) = 0;
};

} // namespace vgui2

class IClientVGUI // VClientVGUI001, client library
{
public:
	virtual ~IClientVGUI() { }
	virtual void Initialize( void *( **factories )( const char *, int * ), int count ) = 0;
	virtual void Start() = 0;
	virtual void SetParent( vgui2::VPANEL parent ) = 0;
	virtual bool UseVGUI1() = 0;
	virtual void HideScoreBoard() = 0;
	virtual void HideAllVGUIMenu() = 0;
	virtual void ActivateClientUI() = 0;
	virtual void HideClientUI() = 0;
	virtual void Shutdown() = 0;
};

struct vmode_t
{
	int width, height, bpp;
};

class IGameUIFuncs // VENGINE_GAMEUIFUNCS_VERSION001, implemented here
{
public:
	virtual ~IGameUIFuncs() { }
	virtual bool IsKeyDown( const char *keyname, bool &isdown ) = 0;
	virtual const char *Key_NameForKey( int keynum ) = 0;
	virtual const char *Key_BindingForKey( int keynum ) = 0;
	virtual vgui2::KeyCode GetVGUI2KeyCodeForBind( const char *bind ) = 0;
	virtual void GetVideoModes( vmode_t **liststart, int *count ) = 0;
	virtual void GetCurrentVideoMode( int *wide, int *tall, int *bpp ) = 0;
	virtual void GetCurrentRenderer( char *name, int namelen, int *windowed, int *hdmodels, int *addons_folder ) = 0;
	virtual bool IsConnectedToVACSecureServer() = 0;
	virtual int Key_KeyStringToKeyNum( const char *string ) = 0;
};

#endif // VGUI2_INTERFACES_H
