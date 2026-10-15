/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2025 APAmk2, Vladislav4KZ, Velaron

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
Crosshair options. Every change goes to the cvars at once, so the crosshair
in game follows the page; Cancel puts back the values the page opened with.

The page needs the client's crosshair modules (cl_dll/crosshair_geom.cpp
and cl_dll/crosshair_code.cpp), which the parent project builds into this
library and announces with MAINUI_CROSSHAIR_GEOM. The preview draws with
the client's own geometry: at 1:1 it is the in-game crosshair pixel for
pixel, and the zoomed view shows each of these pixels four times larger.
The presets page keeps named share codes in crosshair_presets.cfg.
*/

#include "Framework.h"

#ifdef MAINUI_CROSSHAIR_GEOM

#include <stdarg.h>
#include <stdlib.h>
#include "PicButton.h"
#include "Slider.h"
#include "CheckBox.h"
#include "SpinControl.h"
#include "Field.h"
#include "Action.h"
#include "Table.h"
#include "StringArrayModel.h"
#include "Scissor.h"
#include "crosshair_geom.h"
#include "crosshair_code.h"

#define ART_BANNER "gfx/shell/head_xhair"

#define XHAIR_ZOOM 4
#define XHAIR_ROW_STEP 36
#define XHAIR_PANEL_COLOR 0xC0101010

void UI_CrosshairPresets_Menu( void );

static const char *g_szCrosshairSizes[] = { "auto", "small", "medium", "large" };

static struct
{
	const char *name;
	int r, g, b;
} g_CrosshairColors[] = {
	{ "#Valve_Green", 50, 250, 50 },
	{ "#Valve_Red", 250, 50, 50 },
	{ "#Valve_Blue", 50, 50, 250 },
	{ "#Valve_Yellow", 250, 250, 50 },
	{ "#Valve_Ltblue", 50, 250, 250 }
};

// every cvar of the page with its default, as the client creates them
static const struct
{
	const char *name;
	const char *def;
} g_CrosshairCvars[] = {
	{ "xhair_enable", "0" },
	{ "xhair_style", "0" },
	{ "xhair_t", "0" },
	{ "xhair_gap", "0" },
	{ "xhair_size", "4" },
	{ "xhair_thick", "0" },
	{ "xhair_dot", "0" },
	{ "xhair_dot_size", "0" },
	{ "xhair_pad", "0" },
	{ "xhair_color", "0 255 0 255" },
	{ "xhair_pad_color", "0 0 0" },
	{ "xhair_additive", "0" },
	{ "xhair_aa", "1" },
	{ "xhair_scale", "1" },
	{ "xhair_dynamic_move", "1" },
	{ "xhair_dynamic_scale", "0" },
	{ "xhair_gap_useweaponvalue", "0" },
	{ "cl_crosshair_size", "auto" },
	{ "cl_crosshair_color", "50 250 50" },
	{ "cl_crosshair_translucent", "1" },
};

static const char *XHair_MenuGetCvar( const char *name )
{
	return EngFuncs::GetCvarString( name );
}

static void XHair_MenuSetCvar( const char *name, const char *value )
{
	EngFuncs::CvarSetString( name, value );
}

static void XHair_SetupLabel( CMenuAction &label, const char *name )
{
	label.szName = name;
	label.iFlags = QMF_INACTIVE | QMF_DROPSHADOW;
	label.colorBase = uiColorHelp;
	label.SetCharSize( QM_SMALLFONT );
}

static void XHair_SetupPanel( CMenuAction &panel, int x, int y, int w, int h )
{
	// a dark card under the controls, for contrast with the artwork
	panel.iFlags = QMF_INACTIVE;
	panel.SetBackground( XHAIR_PANEL_COLOR );
	panel.SetRect( x, y, w, h );
}

/*
=================
CMenuCrosshairPreview

Draws cfg at 1:1 and zoomed; the classic crosshair (cfg.enable false) comes
from the cl_crosshair_* cvars. The background is ui_xhair_background.
=================
*/
class CMenuCrosshairPreview : public CMenuBaseItem
{
public:
	CMenuCrosshairPreview();

	void Think() override;
	void Draw() override;

	xhair_config_t cfg;
	bool firing;
	HIMAGE hMap;
	HIMAGE hWhite;

private:
	void DrawBackground( Point pt, Size sz, int zoom );
	void DrawClassic( Point origin, int zoom );
	void DrawLayer( const xhair_params_t *params, bool outline, const float rgba[4], bool additive, Point origin, int zoom );

	float dynamicGap; // 640x480 units above the resting gap
	int lastTime;
	int nextShot;
};

CMenuCrosshairPreview::CMenuCrosshairPreview() : CMenuBaseItem(),
	firing(), hMap(), hWhite(), dynamicGap(), lastTime(), nextShot()
{
	XHair_DefaultConfig( &cfg );
	iFlags |= QMF_INACTIVE;
	SetCharSize( QM_SMALLFONT );
}

void CMenuCrosshairPreview::Think()
{
	const int now = uiStatic.realTime;
	float frametime = ( now - lastTime ) * 0.001f;
	bool fired = false;

	if( frametime < 0.0f || frametime > 0.1f )
		frametime = 0.0f;
	lastTime = now;

	// automatic fire at ten shots a second
	if( firing && now >= nextShot )
	{
		fired = true;
		nextShot = now + 100;
	}

	// standing still with the default weapon: resting gap 4, 3 per shot
	const float rest = 4.0f;
	dynamicGap = XHair_StepGap( rest + dynamicGap, rest, 3.0f * cfg.dynamic_scale, fired, frametime ) - rest;
}

void CMenuCrosshairPreview::DrawBackground( Point pt, Size sz, int zoom )
{
	switch( (int)EngFuncs::GetCvarFloat( "ui_xhair_background" ))
	{
	case 1:
		UI_FillRect( pt, sz, 0xFF000000 );
		break;
	case 2:
		UI_FillRect( pt, sz, 0xFFD8D8D8 );
		break;
	default:
		if( !hMap )
		{
			UI_FillRect( pt, sz, uiPromptBgColor );
			break;
		}

		// the picture enlarged like the crosshair, around the same centre
		EngFuncs::PIC_Set( hMap, 255, 255, 255 );
		EngFuncs::PIC_DrawTrans( pt.x + sz.w / 2 - sz.w * zoom / 2, pt.y + sz.h / 2 - sz.h * zoom / 2, sz.w * zoom, sz.h * zoom );
		break;
	}
}

// screen pixel x of the crosshair lands at origin.x + x * zoom, 0 being the centre
void CMenuCrosshairPreview::DrawClassic( Point origin, int zoom )
{
	const char *size = EngFuncs::GetCvarString( "cl_crosshair_size" );
	int r = 50, g = 250, b = 50, a = 180;
	int length;

	sscanf( EngFuncs::GetCvarString( "cl_crosshair_color" ), "%d %d %d", &r, &g, &b );

	if( !stricmp( size, "small" )) length = 10;
	else if( !stricmp( size, "medium" )) length = 20;
	else if( !stricmp( size, "large" )) length = 30;
	else if( ScreenWidth < 640 ) length = 30;
	else if( ScreenWidth < 1024 ) length = 20;
	else length = 10;

	length *= ScreenHeight / 768.0f;

	const int delta = length / 2;
	const struct { int x, y, w, h; } bars[] = {
		{ 0, -delta - length, 1, length },
		{ 0, delta, 1, length },
		{ -delta - length, 0, length, 1 },
		{ delta, 0, length, 1 },
	};

	for( size_t k = 0; k < V_ARRAYSIZE( bars ); k++ )
	{
		const int x = origin.x + bars[k].x * zoom, y = origin.y + bars[k].y * zoom;

		EngFuncs::PIC_Set( hWhite, r, g, b, a );
		if( EngFuncs::GetCvarFloat( "cl_crosshair_translucent" ))
			EngFuncs::PIC_DrawAdditive( x, y, bars[k].w * zoom, bars[k].h * zoom );
		else EngFuncs::PIC_DrawTrans( x, y, bars[k].w * zoom, bars[k].h * zoom );
	}
}

void CMenuCrosshairPreview::DrawLayer( const xhair_params_t *params, bool outline, const float rgba[4], bool additive, Point origin, int zoom )
{
	static xhair_rect_t rects[XHAIR_MAX_RECTS];
	const float centre = XHair_Centre( 0 ); // middle of pixel 0
	const int count = XHair_Build( params, centre, centre, outline, rects, XHAIR_MAX_RECTS );

	for( int i = 0; i < count; i++ )
	{
		const xhair_rect_t *rect = &rects[i];
		const int alpha = (int)( rgba[3] * rect->alpha * 255.0f + 0.5f );

		EngFuncs::PIC_Set( hWhite, rgba[0] * 255.0f + 0.5f, rgba[1] * 255.0f + 0.5f, rgba[2] * 255.0f + 0.5f, alpha );
		if( additive )
			EngFuncs::PIC_DrawAdditive( origin.x + rect->x * zoom, origin.y + rect->y * zoom, rect->w * zoom, rect->h * zoom );
		else EngFuncs::PIC_DrawTrans( origin.x + rect->x * zoom, origin.y + rect->y * zoom, rect->w * zoom, rect->h * zoom );
	}
}

void CMenuCrosshairPreview::Draw()
{
	// the real size view on top, the zoomed one below
	const int gapY = 20 * uiStatic.scaleY;
	Size realSize( m_scSize.w, ( m_scSize.h - gapY ) * 2 / 5 );
	Size zoomSize( m_scSize.w, m_scSize.h - gapY - realSize.h );
	Point realPos = m_scPos;
	Point zoomPos( m_scPos.x, m_scPos.y + realSize.h + gapY );

	xhair_settings_t settings;
	xhair_params_t params;
	float rgba[4], outlineRgba[4];
	char colorStr[64], outlineStr[64];

	settings.style = cfg.style;
	settings.t = cfg.t;
	settings.size = cfg.size;
	settings.thick = cfg.thick;
	settings.dot = cfg.dot;
	settings.dot_size = cfg.dot_size;
	settings.pad = cfg.pad;
	settings.aa = cfg.aa;
	settings.scale = cfg.scale;
	XHair_SetupParams( &params, &settings, 4.0f + dynamicGap + cfg.gap, ScreenHeight );

	// through the same parser as the game, outline alpha rule included
	snprintf( colorStr, sizeof( colorStr ), "%d %d %d %d", cfg.color[0], cfg.color[1], cfg.color[2], cfg.color[3] );
	if( cfg.pad_alpha )
		snprintf( outlineStr, sizeof( outlineStr ), "%d %d %d %d", cfg.pad_color[0], cfg.pad_color[1], cfg.pad_color[2], cfg.pad_color[3] );
	else snprintf( outlineStr, sizeof( outlineStr ), "%d %d %d", cfg.pad_color[0], cfg.pad_color[1], cfg.pad_color[2] );
	XHair_ParseColors( colorStr, outlineStr, rgba, outlineRgba );

	for( int view = 0; view < 2; view++ )
	{
		const Point pt = view ? zoomPos : realPos;
		const Size sz = view ? zoomSize : realSize;
		const int zoom = view ? XHAIR_ZOOM : 1;

		// the centre pixel in the middle of the view, as in game
		const Point origin( pt.x + sz.w / 2 - zoom / 2, pt.y + sz.h / 2 - zoom / 2 );

		UI::Scissor::PushScissor( pt, sz );
		DrawBackground( pt, sz, zoom );

		if( !cfg.enable )
			DrawClassic( origin, zoom );
		else
		{
			if( params.outline > 0.0f )
				DrawLayer( &params, true, outlineRgba, false, origin, zoom );
			DrawLayer( &params, false, rgba, cfg.additive, origin, zoom );
		}
		UI::Scissor::PopScissor();

		UI_DrawString( font, pt.x + 6, pt.y + 4, sz.w, m_scChSize, view ? "x4" : "1:1", uiColorHelp, m_scChSize, QM_LEFT, ETF_SHADOW | ETF_NOSIZELIMIT | ETF_FORCECOL );
		UI_DrawRectangle( pt, sz, uiInputFgColor );
	}
}

/*
=================
CMenuCrosshairValue

A labelled value: a slider for quick changes and a field for exact ones.
=================
*/
class CMenuCrosshair;

class CMenuCrosshairValue
{
public:
	void Setup( CMenuCrosshair *page, const char *name, float min, float max, float step, int precision );
	void SetCoord( int x, int y );
	void SetVisible( bool visible );
	void SetValue( float v );
	float Value() const { return value; }

	CMenuAction label;
	CMenuSlider slider;
	CMenuField field;

private:
	static void SliderChanged( CMenuBaseItem *pSelf, void *pExtra );
	static void FieldChanged( CMenuBaseItem *pSelf, void *pExtra );
	void UpdateField();

	CMenuCrosshair *page;
	float value;
	int precision;
};

/*
=================
CMenuCrosshair
=================
*/
class CMenuCrosshair : public CMenuFramework
{
public:
	CMenuCrosshair() : CMenuFramework( "CMenuCrosshair" ) { }

	void Show() override;
	void Reload() override;

	// controls to cvars, after any change
	void Apply();

	CMenuCheckBox enable;

	// classic crosshair
	CMenuSpinControl classicSize;
	CMenuSpinControl classicColor;
	CMenuCheckBox classicTranslucent;

	// enhanced crosshair
	CMenuAction panel, previewPanel;
	CMenuAction styleLabel;
	CMenuSpinControl style;
	CMenuCrosshairValue length, gap, thickness, dotSize, outline, scale;
	CMenuCheckBox dot, antialias, additive;
	CMenuCrosshairValue color[4];
	CMenuCrosshairValue outlineColor[4];
	CMenuCheckBox dynamicMove, gapUseWeapon;
	CMenuCrosshairValue dynamicScale;
	bool outlineAlphaSet; // xhair_pad_color has its own alpha

	CMenuCrosshairPreview preview;
	CMenuSpinControl background;
	CMenuCheckBox fire;

	CMenuPicButton done, cancel, defaults, presets;

private:
	void _Init() override;
	void UpdateVisibility();
	void Revert();
	void UseDefaults();
	void SetCvars( bool defaults );
	void ControlsToConfig( xhair_config_t *cfg );
	void ConfigToControls( const xhair_config_t *cfg );

	char saved[V_ARRAYSIZE( g_CrosshairCvars )][CS_SIZE];
	bool loading;
};

void CMenuCrosshairValue::Setup( CMenuCrosshair *p, const char *name, float min, float max, float step, int prec )
{
	page = p;
	precision = prec;
	value = min;

	XHair_SetupLabel( label, name );

	slider.Setup( min, max, step );
	slider.onChanged = SliderChanged;
	slider.onChanged.pExtra = this;

	field.iMaxLength = 8;
	field.SetCharSize( QM_SMALLFONT );
	field.onChanged = FieldChanged;
	field.onChanged.pExtra = this;

	page->AddItem( label );
	page->AddItem( slider );
	page->AddItem( field );
}

void CMenuCrosshairValue::SetCoord( int x, int y )
{
	label.SetRect( x, y, 150, 26 );
	slider.SetRect( x + 150, y + 6, 120, 14 );
	field.SetRect( x + 280, y, 60, 26 );
}

void CMenuCrosshairValue::SetVisible( bool visible )
{
	CMenuBaseItem *items[] = { &label, &slider, &field };

	for( size_t i = 0; i < V_ARRAYSIZE( items ); i++ )
	{
		if( visible ) items[i]->Show();
		else items[i]->Hide();
	}
}

void CMenuCrosshairValue::SetValue( float v )
{
	value = v;
	slider.SetCurrentValue( v );
	UpdateField();
}

void CMenuCrosshairValue::UpdateField()
{
	char buf[32];
	snprintf( buf, sizeof( buf ), "%.*f", precision, value );

	// no trailing zeros: 4.25, 4.5, 4
	if( strchr( buf, '.' ))
	{
		char *end = buf + strlen( buf ) - 1;
		while( *end == '0' )
			*end-- = 0;
		if( *end == '.' )
			*end = 0;
	}

	field.SetBuffer( buf );
}

void CMenuCrosshairValue::SliderChanged( CMenuBaseItem *, void *pExtra )
{
	CMenuCrosshairValue *self = (CMenuCrosshairValue *)pExtra;

	self->value = self->slider.GetCurrentValue();
	self->UpdateField();
	self->page->Apply();
}

void CMenuCrosshairValue::FieldChanged( CMenuBaseItem *, void *pExtra )
{
	CMenuCrosshairValue *self = (CMenuCrosshairValue *)pExtra;
	const char *text = self->field.GetBuffer();
	char *end;
	float v = strtof( text, &end );

	// the field keeps what is typed, values outside the slider range included
	if( end == text || *end )
		return;

	self->value = v;
	self->slider.SetCurrentValue( v );
	self->page->Apply();
}

void CMenuCrosshair::Show()
{
	// what Cancel goes back to
	for( size_t i = 0; i < V_ARRAYSIZE( g_CrosshairCvars ); i++ )
		Q_strncpy( saved[i], EngFuncs::GetCvarString( g_CrosshairCvars[i].name ), sizeof( saved[i] ));

	CMenuFramework::Show();
}

void CMenuCrosshair::ControlsToConfig( xhair_config_t *cfg )
{
	XHair_DefaultConfig( cfg );

	cfg->enable = enable.bChecked;
	cfg->style = style.GetCurrentValue() == 2 ? XHAIR_STYLE_CIRCLE : XHAIR_STYLE_CROSS;
	cfg->t = style.GetCurrentValue() == 1;
	cfg->size = length.Value();
	cfg->gap = gap.Value();
	cfg->thick = thickness.Value();
	cfg->dot = dot.bChecked;
	cfg->dot_size = dotSize.Value();
	cfg->pad = outline.Value();
	cfg->additive = additive.bChecked;
	cfg->aa = antialias.bChecked;
	cfg->scale = scale.Value();
	cfg->dynamic_move = dynamicMove.bChecked;
	cfg->dynamic_scale = dynamicScale.Value();
	cfg->weapon_gap = gapUseWeapon.bChecked;

	for( int i = 0; i < 4; i++ )
	{
		cfg->color[i] = (int)color[i].Value();
		cfg->pad_color[i] = (int)outlineColor[i].Value();
	}

	// without an alpha of its own, the outline follows the crosshair's
	cfg->pad_alpha = outlineAlphaSet;
}

void CMenuCrosshair::ConfigToControls( const xhair_config_t *cfg )
{
	enable.bChecked = cfg->enable;
	style.SetCurrentValue( cfg->style == XHAIR_STYLE_CIRCLE ? 2 : cfg->t ? 1 : 0 );
	length.SetValue( cfg->size );
	gap.SetValue( cfg->gap );
	thickness.SetValue( cfg->thick );
	dot.bChecked = cfg->dot;
	dotSize.SetValue( cfg->dot_size );
	outline.SetValue( cfg->pad );
	additive.bChecked = cfg->additive;
	antialias.bChecked = cfg->aa;
	scale.SetValue( cfg->scale );
	dynamicMove.bChecked = cfg->dynamic_move;
	dynamicScale.SetValue( cfg->dynamic_scale );
	gapUseWeapon.bChecked = cfg->weapon_gap;

	for( int i = 0; i < 4; i++ )
		color[i].SetValue( cfg->color[i] );

	outlineAlphaSet = cfg->pad_alpha;
	for( int i = 0; i < 3; i++ )
		outlineColor[i].SetValue( cfg->pad_color[i] );
	outlineColor[3].SetValue( cfg->pad_alpha ? cfg->pad_color[3] : cfg->color[3] );
}

void CMenuCrosshair::Reload()
{
	xhair_config_t cfg;
	int rgb[3];
	int i;

	loading = true;
	CMenuFramework::Reload();

	// classic
	classicColor.SetCurrentValue( 0.0f );
	if( sscanf( EngFuncs::GetCvarString( "cl_crosshair_color" ), "%d %d %d", &rgb[0], &rgb[1], &rgb[2] ) == 3 )
	{
		for( i = 0; i < (int)V_ARRAYSIZE( g_CrosshairColors ); i++ )
		{
			if( g_CrosshairColors[i].r == rgb[0] && g_CrosshairColors[i].g == rgb[1] && g_CrosshairColors[i].b == rgb[2] )
				classicColor.SetCurrentValue( i );
		}
	}

	classicSize.SetCurrentValue( 0.0f );
	for( i = 0; i < (int)V_ARRAYSIZE( g_szCrosshairSizes ); i++ )
	{
		if( !stricmp( EngFuncs::GetCvarString( "cl_crosshair_size" ), g_szCrosshairSizes[i] ))
			classicSize.SetCurrentValue( i );
	}

	classicTranslucent.bChecked = EngFuncs::GetCvarFloat( "cl_crosshair_translucent" ) != 0.0f;

	// enhanced
	XHair_ReadConfig( &cfg, XHair_MenuGetCvar );
	ConfigToControls( &cfg );
	preview.cfg = cfg;

	loading = false;
	UpdateVisibility();
}

void CMenuCrosshair::Apply()
{
	xhair_config_t cfg;
	char str[64];
	int i;

	if( loading )
		return;

	// classic
	i = classicSize.GetCurrentValue();
	if( i >= 0 && i < (int)V_ARRAYSIZE( g_szCrosshairSizes ))
		EngFuncs::CvarSetString( "cl_crosshair_size", g_szCrosshairSizes[i] );

	i = classicColor.GetCurrentValue();
	if( i >= 0 && i < (int)V_ARRAYSIZE( g_CrosshairColors ))
	{
		snprintf( str, sizeof( str ), "%d %d %d", g_CrosshairColors[i].r, g_CrosshairColors[i].g, g_CrosshairColors[i].b );
		EngFuncs::CvarSetString( "cl_crosshair_color", str );
	}

	EngFuncs::CvarSetValue( "cl_crosshair_translucent", classicTranslucent.bChecked ? 1.0f : 0.0f );

	// enhanced
	ControlsToConfig( &cfg );
	XHair_WriteConfig( &cfg, XHair_MenuSetCvar );
	preview.cfg = cfg;
}

void CMenuCrosshair::SetCvars( bool defaults )
{
	for( size_t i = 0; i < V_ARRAYSIZE( g_CrosshairCvars ); i++ )
		EngFuncs::CvarSetString( g_CrosshairCvars[i].name, defaults ? g_CrosshairCvars[i].def : saved[i] );

	Reload();
}

void CMenuCrosshair::Revert()
{
	SetCvars( false );
	Hide();
}

void CMenuCrosshair::UseDefaults()
{
	SetCvars( true );
}

void CMenuCrosshair::UpdateVisibility()
{
	const bool enhanced = enable.bChecked;
	CMenuBaseItem *classic[] = { &classicSize, &classicColor, &classicTranslucent };
	CMenuBaseItem *items[] = { &styleLabel, &style, &dot, &antialias, &additive, &dynamicMove, &gapUseWeapon, &fire };
	CMenuCrosshairValue *values[] = { &length, &gap, &thickness, &dotSize, &outline, &scale, &dynamicScale,
		&color[0], &color[1], &color[2], &color[3], &outlineColor[0], &outlineColor[1], &outlineColor[2], &outlineColor[3] };

	for( size_t i = 0; i < V_ARRAYSIZE( classic ); i++ )
	{
		if( enhanced ) classic[i]->Hide();
		else classic[i]->Show();
	}

	for( size_t i = 0; i < V_ARRAYSIZE( items ); i++ )
	{
		if( enhanced ) items[i]->Show();
		else items[i]->Hide();
	}

	for( size_t i = 0; i < V_ARRAYSIZE( values ); i++ )
		values[i]->SetVisible( enhanced );
}

static void XHair_SetupCheckBox( CMenuCrosshair *page, CMenuCheckBox &box, const char *name, int x, int y )
{
	box.SetNameAndStatus( name, NULL );
	box.SetRect( x, y, 26, 26 );
	box.SetCharSize( QM_SMALLFONT );
	box.onChanged = VoidCb( &CMenuCrosshair::Apply );
	page->AddItem( box );
}

void CMenuCrosshair::_Init()
{
	static const char *colorNames[V_ARRAYSIZE( g_CrosshairColors )];
	static CStringArrayModel colorModel( colorNames, V_ARRAYSIZE( g_CrosshairColors ));
	for( size_t i = 0; i < V_ARRAYSIZE( g_CrosshairColors ); i++ )
		colorNames[i] = L( g_CrosshairColors[i].name );

	static const char *sizeNames[] = { L( "GameUI_Auto" ), L( "GameUI_Small" ), L( "GameUI_Medium" ), L( "GameUI_Large" ) };
	static CStringArrayModel sizeModel( sizeNames, V_ARRAYSIZE( sizeNames ));

	static const char *styleNames[] = { L( "Cross" ), L( "T" ), L( "Circle" ) };
	static CStringArrayModel styleModel( styleNames, V_ARRAYSIZE( styleNames ));

	static const char *backgroundNames[] = { L( "Map" ), L( "Black" ), L( "Light" ) };
	static CStringArrayModel backgroundModel( backgroundNames, V_ARRAYSIZE( backgroundNames ));

	static const char *channels[] = { L( "Red" ), L( "Green" ), L( "Blue" ), L( "Alpha" ) };
	static const char *outlineChannels[] = { L( "Outline red" ), L( "Outline green" ), L( "Outline blue" ), L( "Outline alpha" ) };

	const int colA = 72, colB = 440, top = 320;
	int y;

	banner.SetPicture( ART_BANNER );
	AddItem( banner );

	XHair_SetupPanel( panel, colA - 12, 205, colB + 340 + 12 - ( colA - 12 ), 525 );
	AddItem( panel );
	XHair_SetupPanel( previewPanel, 788, 205, 224, 525 );
	AddItem( previewPanel );

	enable.SetNameAndStatus( L( "Enhanced crosshair" ), L( "Enables enhanced crosshair" ) );
	enable.SetCoord( colA, 215 );
	SET_EVENT_MULTI( enable.onChanged,
	{
		CMenuCrosshair *page = (CMenuCrosshair *)pSelf->Parent();
		page->Apply();
		page->UpdateVisibility();
	});
	AddItem( enable );

	done.szName = L( "GameUI_OK" );
	done.SetPicture( PC_DONE );
	done.SetCoord( colA, 262 );
	done.onReleased = VoidCb( &CMenuCrosshair::SaveAndPopMenu );
	AddItem( done );

	cancel.szName = L( "GameUI_Cancel" );
	cancel.SetPicture( PC_CANCEL );
	cancel.SetCoord( colA + 150, 262 );
	cancel.onReleased = VoidCb( &CMenuCrosshair::Revert );
	AddItem( cancel );

	defaults.szName = L( "GameUI_UseDefaults" );
	defaults.SetPicture( PC_USE_DEFAULTS );
	defaults.SetCoord( colA + 300, 262 );
	defaults.onReleased = VoidCb( &CMenuCrosshair::UseDefaults );
	AddItem( defaults );

	presets.szName = L( "Presets" );
	presets.SetCoord( colA + 450, 256 ); // text, not a picture: centred on the pictures' row
	presets.onReleased = UI_CrosshairPresets_Menu;
	AddItem( presets );

	// classic crosshair, in place of the first column
	y = top;
	classicSize.Setup( &sizeModel );
	classicSize.SetRect( colA, y, 260, 32 );
	classicSize.onChanged = VoidCb( &CMenuCrosshair::Apply );
	AddItem( classicSize );
	y += 50;

	classicColor.Setup( &colorModel );
	classicColor.SetRect( colA, y, 260, 32 );
	classicColor.onChanged = VoidCb( &CMenuCrosshair::Apply );
	AddItem( classicColor );
	y += 50;

	classicTranslucent.SetNameAndStatus( L( "GameUI_Translucent" ), NULL );
	classicTranslucent.SetCoord( colA, y );
	classicTranslucent.onChanged = VoidCb( &CMenuCrosshair::Apply );
	AddItem( classicTranslucent );

	// first column: shape
	y = top;
	XHair_SetupLabel( styleLabel, L( "Style" ));
	styleLabel.SetRect( colA, y, 150, 26 );
	AddItem( styleLabel );

	style.Setup( &styleModel );
	style.SetRect( colA + 150, y, 190, 26 );
	style.SetCharSize( QM_SMALLFONT );
	style.onChanged = VoidCb( &CMenuCrosshair::Apply );
	AddItem( style );
	y += XHAIR_ROW_STEP;

	length.Setup( this, L( "Length" ), 0.0f, 20.0f, 0.25f, 2 );
	length.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	gap.Setup( this, L( "Gap" ), -5.0f, 15.0f, 0.25f, 2 );
	gap.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	thickness.Setup( this, L( "Thickness" ), 0.0f, 6.0f, 0.05f, 2 );
	thickness.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	outline.Setup( this, L( "Outline" ), 0.0f, 4.0f, 0.25f, 2 );
	outline.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	XHair_SetupCheckBox( this, dot, L( "Dot" ), colA, y );
	y += XHAIR_ROW_STEP;
	dotSize.Setup( this, L( "Dot size" ), 0.0f, 6.0f, 0.05f, 2 );
	dotSize.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	scale.Setup( this, L( "Scale" ), 0.25f, 4.0f, 0.05f, 2 );
	scale.SetCoord( colA, y ); y += XHAIR_ROW_STEP;
	XHair_SetupCheckBox( this, antialias, L( "Smooth edges" ), colA, y );
	y += XHAIR_ROW_STEP;
	XHair_SetupCheckBox( this, additive, L( "Additive" ), colA, y );

	// second column: colours and dynamic gap
	y = top;
	for( int i = 0; i < 4; i++ )
	{
		color[i].Setup( this, channels[i], 0.0f, 255.0f, 1.0f, 0 );
		color[i].SetCoord( colB, y );
		y += XHAIR_ROW_STEP;
	}

	for( int i = 0; i < 4; i++ )
	{
		outlineColor[i].Setup( this, outlineChannels[i], 0.0f, 255.0f, 1.0f, 0 );
		outlineColor[i].SetCoord( colB, y );
		y += XHAIR_ROW_STEP;
	}

	// touching the outline alpha gives the outline an alpha of its own
	SET_EVENT_MULTI( outlineColor[3].slider.onPressed,
	{
		((CMenuCrosshair *)pSelf->Parent())->outlineAlphaSet = true;
	});
	SET_EVENT_MULTI( outlineColor[3].field.onGotFocus,
	{
		((CMenuCrosshair *)pSelf->Parent())->outlineAlphaSet = true;
	});

	XHair_SetupCheckBox( this, dynamicMove, L( "Dynamic" ), colB, y );
	XHair_SetupCheckBox( this, gapUseWeapon, L( "Weapon gap" ), colB + 170, y );
	y += XHAIR_ROW_STEP;

	dynamicScale.Setup( this, L( "Dynamic scale" ), 0.0f, 3.0f, 0.1f, 2 );
	dynamicScale.SetCoord( colB, y );

	// preview
	preview.SetRect( 800, 215, 200, 420 );
	preview.hMap = EngFuncs::PIC_Load( "gfx/vgui/crosshair.tga", 0 );
	preview.hWhite = EngFuncs::PIC_Load( "*white" );
	AddItem( preview );

	background.Setup( &backgroundModel );
	background.SetRect( 800, 650, 200, 26 );
	background.SetCharSize( QM_SMALLFONT );
	background.LinkCvar( "ui_xhair_background", CMenuEditable::CVAR_VALUE );
	background.onChanged = CMenuEditable::WriteCvarCb;
	AddItem( background );

	fire.SetNameAndStatus( L( "Fire" ), L( "Shoots to show the dynamic gap" ) );
	fire.SetRect( 800, 690, 26, 26 );
	fire.SetCharSize( QM_SMALLFONT );
	fire.bChecked = false;
	SET_EVENT_MULTI( fire.onChanged,
	{
		CMenuCrosshair *page = (CMenuCrosshair *)pSelf->Parent();
		page->preview.firing = page->fire.bChecked;
	});
	AddItem( fire );
}

ADD_MENU( menu_crosshair, CMenuCrosshair, UI_Crosshair_Menu );

/*
=================
CMenuCrosshairPresets

Built-in presets first, then the player's, saved in crosshair_presets.cfg
as name and share code.
=================
*/
class CMenuCrosshairPresets;

class CMenuCrosshairPresetsModel : public CMenuBaseModel
{
public:
	void Update() override { }
	int GetColumns() const override { return 2; }
	int GetRows() const override;
	const char *GetCellText( int line, int column ) override;
	void OnActivateEntry( int line ) override;

	CMenuCrosshairPresets *page;
};

class CMenuCrosshairPresets : public CMenuFramework
{
public:
	CMenuCrosshairPresets() : CMenuFramework( "CMenuCrosshairPresets" ) { }

	void Show() override;

	int NumRows() const { return XHair_NumBuiltinPresets() + numPresets; }
	const char *RowName( int row );
	bool RowConfig( int row, xhair_config_t *cfg, char *code, size_t size );

	void ApplySelected();

private:
	void _Init() override;

	void Load();
	void Save();
	void Select();
	void SaveCurrent();
	void RenameSelected();
	void DeleteSelected();
	void CopyCode();
	void ImportCode();
	void ApplyConfig( const xhair_config_t *cfg );
	void SetStatus( const char *fmt, ... );
	xhair_preset_t *SelectedPreset();

	CMenuAction panel, previewPanel;
	CMenuTable list;
	CMenuCrosshairPresetsModel model;
	CMenuAction nameLabel, codeLabel, status;
	CMenuField name, code;
	CMenuPicButton apply, save, rename, remove, copy, import, done;
	CMenuCrosshairPreview preview;
	CMenuSpinControl background;

	xhair_preset_t presets[XHAIR_MAX_PRESETS];
	int numPresets;
	char statusText[256];
};

int CMenuCrosshairPresetsModel::GetRows() const
{
	return page->NumRows();
}

const char *CMenuCrosshairPresetsModel::GetCellText( int line, int column )
{
	if( column == 0 )
		return page->RowName( line );

	return line < XHair_NumBuiltinPresets() ? L( "Built in" ) : L( "Saved" );
}

void CMenuCrosshairPresetsModel::OnActivateEntry( int line )
{
	page->ApplySelected();
}

const char *CMenuCrosshairPresets::RowName( int row )
{
	xhair_config_t cfg;

	if( row < XHair_NumBuiltinPresets())
		return XHair_BuiltinPreset( row, &cfg );

	row -= XHair_NumBuiltinPresets();
	return row < numPresets ? presets[row].name : "";
}

bool CMenuCrosshairPresets::RowConfig( int row, xhair_config_t *cfg, char *text, size_t size )
{
	if( row < 0 || row >= NumRows())
		return false;

	if( row < XHair_NumBuiltinPresets())
	{
		XHair_BuiltinPreset( row, cfg );
		return XHair_Encode( cfg, text, size );
	}

	const xhair_preset_t *preset = &presets[row - XHair_NumBuiltinPresets()];
	Q_strncpy( text, preset->code, size );
	return XHair_Decode( preset->code, cfg, NULL ) == XHAIR_CODE_OK;
}

xhair_preset_t *CMenuCrosshairPresets::SelectedPreset()
{
	const int row = list.GetCurrentIndex() - XHair_NumBuiltinPresets();

	return row >= 0 && row < numPresets ? &presets[row] : NULL;
}

void CMenuCrosshairPresets::SetStatus( const char *fmt, ... )
{
	va_list args;

	va_start( args, fmt );
	vsnprintf( statusText, sizeof( statusText ), fmt, args );
	va_end( args );
}

void CMenuCrosshairPresets::Load()
{
	int length = 0;
	char *text = (char *)EngFuncs::COM_LoadFile( XHAIR_PRESETS_FILE, &length );

	numPresets = 0;
	if( text )
	{
		numPresets = XHair_ParsePresets( text, length, presets, XHAIR_MAX_PRESETS );
		EngFuncs::COM_FreeFile( text );
	}
}

void CMenuCrosshairPresets::Save()
{
	static char text[XHAIR_MAX_PRESETS * ( XHAIR_PRESET_NAME + XHAIR_CODE_MAX + 8 ) + 128];
	const size_t len = XHair_FormatPresets( presets, numPresets, text, sizeof( text ));

	if( !len || !EngFuncs::COM_SaveFile( XHAIR_PRESETS_FILE, text, len ))
		SetStatus( L( "Could not write %s" ), XHAIR_PRESETS_FILE );
}

void CMenuCrosshairPresets::Show()
{
	Load();
	CMenuFramework::Show();
	statusText[0] = 0;
	Select();
}

void CMenuCrosshairPresets::Select()
{
	xhair_config_t cfg;
	char text[XHAIR_CODE_MAX];
	const int row = list.GetCurrentIndex();

	if( row < 0 || row >= NumRows())
		return;

	name.SetBuffer( RowName( row ));
	if( !RowConfig( row, &cfg, text, sizeof( text )))
	{
		// a damaged line of the file: show it, it can be fixed or deleted
		code.SetBuffer( text );
		SetStatus( L( "This preset's code is damaged" ));
		return;
	}

	preview.cfg = cfg;
	code.SetBuffer( text );
}

void CMenuCrosshairPresets::ApplyConfig( const xhair_config_t *cfg )
{
	XHair_WriteConfig( cfg, XHair_MenuSetCvar );

	// the crosshair page below shows the new values
	if( menu_crosshair && menu_crosshair->WasInit())
		menu_crosshair->Reload();
}

void CMenuCrosshairPresets::ApplySelected()
{
	xhair_config_t cfg;
	char text[XHAIR_CODE_MAX];
	const int row = list.GetCurrentIndex();

	if( !RowConfig( row, &cfg, text, sizeof( text )))
		return;

	ApplyConfig( &cfg );
	SetStatus( L( "Using \"%s\"" ), RowName( row ));
}

void CMenuCrosshairPresets::SaveCurrent()
{
	xhair_config_t cfg;
	xhair_preset_t *preset = NULL;
	char presetName[XHAIR_PRESET_NAME];

	Q_strncpy( presetName, name.GetBuffer(), sizeof( presetName ));
	if( !presetName[0] )
		snprintf( presetName, sizeof( presetName ), "Preset %d", numPresets + 1 );

	// the same name replaces the saved preset
	for( int i = 0; i < numPresets; i++ )
	{
		if( !strcmp( presets[i].name, presetName ))
			preset = &presets[i];
	}

	if( !preset )
	{
		if( numPresets >= XHAIR_MAX_PRESETS )
		{
			SetStatus( L( "No room for more presets" ));
			return;
		}
		preset = &presets[numPresets++];
	}

	XHair_ReadConfig( &cfg, XHair_MenuGetCvar );
	Q_strncpy( preset->name, presetName, sizeof( preset->name ));
	XHair_Encode( &cfg, preset->code, sizeof( preset->code ));
	Save();

	list.SetCurrentIndex( XHair_NumBuiltinPresets() + ( preset - presets ));
	Select();
	SetStatus( L( "Saved the current crosshair as \"%s\"" ), preset->name );
}

void CMenuCrosshairPresets::RenameSelected()
{
	xhair_preset_t *preset = SelectedPreset();

	if( !preset )
	{
		SetStatus( L( "Built-in presets can't be renamed" ));
		return;
	}

	if( !name.GetBuffer()[0] )
		return;

	Q_strncpy( preset->name, name.GetBuffer(), sizeof( preset->name ));
	Save();
	SetStatus( L( "Renamed to \"%s\"" ), preset->name );
}

void CMenuCrosshairPresets::DeleteSelected()
{
	xhair_preset_t *preset = SelectedPreset();
	char removed[XHAIR_PRESET_NAME];

	if( !preset )
	{
		SetStatus( L( "Built-in presets can't be deleted" ));
		return;
	}

	Q_strncpy( removed, preset->name, sizeof( removed ));
	memmove( preset, preset + 1, ( numPresets - ( preset - presets ) - 1 ) * sizeof( *preset ));
	numPresets--;
	Save();

	list.SetCurrentIndex( Q_min( list.GetCurrentIndex(), NumRows() - 1 ));
	Select();
	SetStatus( L( "Deleted \"%s\"" ), removed );
}

void CMenuCrosshairPresets::CopyCode()
{
	const char *text = code.GetBuffer();

	if( XHair_SetClipboard( text ))
		SetStatus( L( "Code copied to the clipboard" ));
	else
	{
		Con_Printf( "%s\n", text );
		SetStatus( L( "No clipboard: the code is in the console" ));
	}
}

void CMenuCrosshairPresets::ImportCode()
{
	xhair_config_t cfg;
	xhair_import_t info;
	const int error = XHair_Decode( code.GetBuffer(), &cfg, &info );

	if( error != XHAIR_CODE_OK )
	{
		SetStatus( "%s", XHair_CodeError( error ));
		return;
	}

	ApplyConfig( &cfg );
	preview.cfg = cfg;

	if( info.unknown )
		SetStatus( L( "Imported. The code is from a newer version: %d setting(s) ignored" ), info.unknown );
	else SetStatus( L( "Imported. Save it to keep it as a preset" ));
}

void CMenuCrosshairPresets::_Init()
{
	static const char *backgroundNames[] = { L( "Map" ), L( "Black" ), L( "Light" ) };
	static CStringArrayModel backgroundModel( backgroundNames, V_ARRAYSIZE( backgroundNames ));

	banner.SetPicture( ART_BANNER );
	AddItem( banner );

	XHair_SetupPanel( panel, 60, 205, 720, 525 );
	AddItem( panel );
	XHair_SetupPanel( previewPanel, 788, 205, 224, 525 );
	AddItem( previewPanel );

	model.page = this;
	list.SetModel( &model );
	list.SetupColumn( 0, L( "Name" ), 0.7f );
	list.SetupColumn( 1, L( "Type" ), 0.3f );
	list.SetRect( 72, 215, 696, 300 );
	list.SetCharSize( QM_SMALLFONT );
	list.onChanged = VoidCb( &CMenuCrosshairPresets::Select );
	AddItem( list );

	XHair_SetupLabel( nameLabel, L( "Name" ));
	nameLabel.SetRect( 72, 530, 80, 30 );
	AddItem( nameLabel );

	name.iMaxLength = XHAIR_PRESET_NAME - 1;
	name.SetRect( 160, 530, 608, 30 );
	name.SetCharSize( QM_SMALLFONT );
	AddItem( name );

	XHair_SetupLabel( codeLabel, L( "Code" ));
	codeLabel.SetRect( 72, 575, 80, 30 );
	AddItem( codeLabel );

	code.iMaxLength = XHAIR_CODE_MAX - 1;
	code.SetRect( 160, 575, 608, 30 );
	code.SetCharSize( QM_SMALLFONT );
	AddItem( code );

	struct
	{
		CMenuPicButton *button;
		const char *name;
		const char *status;
		VoidIHCallback cb;
		int x, y;
	} buttons[] = {
		{ &apply, L( "Use" ), L( "Use the selected preset" ), VoidCb( &CMenuCrosshairPresets::ApplySelected ), 72, 625 },
		{ &save, L( "Save current" ), L( "Save the current crosshair under the name above" ), VoidCb( &CMenuCrosshairPresets::SaveCurrent ), 222, 625 },
		{ &rename, L( "Rename" ), L( "Give the selected preset the name above" ), VoidCb( &CMenuCrosshairPresets::RenameSelected ), 432, 625 },
		{ &remove, L( "Delete" ), L( "Delete the selected preset" ), VoidCb( &CMenuCrosshairPresets::DeleteSelected ), 592, 625 },
		{ &copy, L( "Copy code" ), L( "Copy the code above to the clipboard" ), VoidCb( &CMenuCrosshairPresets::CopyCode ), 72, 665 },
		{ &import, L( "Use code" ), L( "Use the crosshair of the code above (paste with Ctrl+V)" ), VoidCb( &CMenuCrosshairPresets::ImportCode ), 272, 665 },
		{ &done, L( "Done" ), NULL, VoidCb( &CMenuCrosshairPresets::Hide ), 592, 665 },
	};

	for( size_t i = 0; i < V_ARRAYSIZE( buttons ); i++ )
	{
		buttons[i].button->SetNameAndStatus( buttons[i].name, buttons[i].status );
		buttons[i].button->SetCoord( buttons[i].x, buttons[i].y );
		buttons[i].button->onReleased = buttons[i].cb;
		AddItem( buttons[i].button );
	}
	status.szName = statusText;
	status.iFlags = QMF_INACTIVE | QMF_DROPSHADOW;
	status.colorBase = uiColorHelp;
	status.SetCharSize( QM_SMALLFONT );
	status.SetRect( 72, 700, 696, 26 );
	AddItem( status );

	preview.SetRect( 800, 215, 200, 420 );
	preview.hMap = EngFuncs::PIC_Load( "gfx/vgui/crosshair.tga", 0 );
	preview.hWhite = EngFuncs::PIC_Load( "*white" );
	AddItem( preview );

	background.Setup( &backgroundModel );
	background.SetRect( 800, 650, 200, 26 );
	background.SetCharSize( QM_SMALLFONT );
	background.LinkCvar( "ui_xhair_background", CMenuEditable::CVAR_VALUE );
	background.onChanged = CMenuEditable::WriteCvarCb;
	AddItem( background );
}

ADD_MENU( menu_crosshair_presets, CMenuCrosshairPresets, UI_CrosshairPresets_Menu );

#else // !MAINUI_CROSSHAIR_GEOM

// the client's crosshair modules are not built in: no crosshair page
void UI_Crosshair_Menu( void )
{
}

#endif // !MAINUI_CROSSHAIR_GEOM
