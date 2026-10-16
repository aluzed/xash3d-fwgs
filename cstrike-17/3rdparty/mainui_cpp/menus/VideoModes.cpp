/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "Framework.h"
#include "MenuStrings.h"
#include "Table.h"
#include "CheckBox.h"
#include "YesNoMessageBox.h"
#include "SpinControl.h"
#include "utlvector.h"
#include "StringArrayModel.h"

#define ART_BANNER		"gfx/shell/head_vidmodes"

// TODO:
// - Get rid of vid mode strings, always compare the Width X Height of the window
//   Because if user has resized the window, or set it to cmdline, it will go
//   out of sync with the vid mode strings list
//   Doing this, however, will require API extension

// the aspect ratios the list can be narrowed to, the first shows every mode
enum
{
	ASPECT_ALL = 0,
	ASPECT_4_3,
	ASPECT_16_10,
	ASPECT_16_9,
	ASPECT_OTHER
};

static int AspectOf( int w, int h )
{
	if( w <= 0 || h <= 0 )
		return ASPECT_OTHER;

	const float r = (float)w / h;
	if( fabs( r - 4.0f / 3.0f ) < 0.02f )
		return ASPECT_4_3;
	if( fabs( r - 16.0f / 10.0f ) < 0.02f )
		return ASPECT_16_10;
	if( fabs( r - 16.0f / 9.0f ) < 0.03f ) // 1366x768 too
		return ASPECT_16_9;
	return ASPECT_OTHER;
}

static const char *AspectName( int w, int h )
{
	switch( AspectOf( w, h ))
	{
	case ASPECT_4_3: return "4:3";
	case ASPECT_16_10: return "16:10";
	case ASPECT_16_9: return "16:9";
	}

	const float r = (float)w / h;
	if( fabs( r - 5.0f / 4.0f ) < 0.02f )
		return "5:4";
	if( r > 2.2f && r < 2.5f )
		return "21:9";
	return nullptr;
}

// the size of one of the engine's modes; false past the last
static bool ModeSize( int mode, int &w, int &h )
{
	const char *str = EngFuncs::GetModeString( mode );
	w = h = 0;
	return str && sscanf( str, "%ix%i", &w, &h ) == 2;
}

// the engine's mode of a size, -1 when it has none: the size the game runs
// at is width and height, vid_mode can be left over from another size
static int ModeOfSize( int w, int h )
{
	int mw, mh;
	for( int i = 0; EngFuncs::GetModeString( i ); i++ )
		if( ModeSize( i, mw, mh ) && mw == w && mh == h )
			return i;
	return -1;
}

// the engine's modes, narrowed to one aspect ratio; a row keeps the index
// of its mode, which vid_mode and vid_setmode take
class CMenuVidModesModel : public CMenuBaseArrayModel
{
public:
	void Update() override;
	int GetRows() const override
	{
		return m_modes.Count();
	}

	const char *GetText( int line ) override
	{
		return m_modes[line].text;
	}

	void SetAspect( int aspect ) { m_iAspect = aspect; }

	// the engine's mode of a row, the row of a mode (-1 when filtered out)
	int ModeOf( int row ) const
	{
		return row >= 0 && row < m_modes.Count() ? m_modes[row].index : -1;
	}

	int RowOf( int mode ) const
	{
		for( int i = 0; i < m_modes.Count(); i++ )
			if( m_modes[i].index == mode )
				return i;
		return -1;
	}

private:
	struct mode_t
	{
		int index;
		char text[64];
	};

	CUtlVector<mode_t> m_modes;
	int m_iAspect = ASPECT_ALL;
};

class CMenuRenderersModel : public CMenuBaseArrayModel
{
public:
	void Update() override;
	int GetRows() const override
	{
		return m_refs.Count();
	}

	const char *GetText( int i ) override
	{
		return m_refs[i].readable;
	}

	const char *GetShortName( int i )
	{
		return m_refs[i].shortName;
	}

	void Add( const char *str )
	{
		refdll temp;

		Q_strncpy( temp.shortName, str, sizeof( temp.shortName ));
		Q_strncpy( temp.readable, str, sizeof( temp.readable ));

		m_refs.AddToTail( temp );
	}
private:
	struct refdll
	{
		char shortName[64];
		char readable[64];
	};

	CUtlVector<refdll> m_refs;
};

class CMenuVidModes : public CMenuFramework
{
private:
	void _Init() override;
	void Reload() override;
	void Draw() override; // put test mode timer here

public:
	CMenuVidModes() : CMenuFramework( "CMenuVidModes" ), restartMsgBox( true ), testModeTimer( 0 ) { }

	void SetMode( int mode );
	void SetMode( int w, int h );
	void SetConfig();
	void RevertChanges();
	void ApplyChanges();
	void FinalizeChanges();

	void GetConfig();

	void GetRendererConfig();
	void WriteRendererConfig();
	bool IsRendererChanged();

	void SetAspect( int aspect );
	bool HasChanges();

	CMenuPicButton *apply = nullptr;
	CMenuCheckBox	vsync;

	CMenuTable	vidList;
	CMenuVidModesModel vidListModel;

	CMenuYesNoMessageBox testModeMsgBox;
	CMenuYesNoMessageBox restartMsgBox;

	CMenuRenderersModel renderersModel;
	CMenuSpinControl renderers;
	CMenuSpinControl windowMode;
	CMenuSpinControl aspect;

	int prevMode;
	int prevModeX;
	int prevModeY;
	int prevFullscreen;
	float testModeTimer;
	char testModeMsg[256];
};

void CMenuVidModesModel::Update()
{
	m_modes.Purge();

	for( int i = 0; ; i++ )
	{
		const char *mode = EngFuncs::GetModeString( i );

		if( !mode )
			break;

		int w = 0, h = 0;
		sscanf( mode, "%ix%i", &w, &h );
		if( m_iAspect != ASPECT_ALL && AspectOf( w, h ) != m_iAspect )
			continue;

		mode_t m;
		m.index = i;
		const char *aspect = AspectName( w, h );
		if( aspect )
			snprintf( m.text, sizeof( m.text ), "%s  (%s)", mode, aspect );
		else
			Q_strncpy( m.text, mode, sizeof( m.text ));
		m_modes.AddToTail( m );
	}
}

void CMenuRenderersModel::Update()
{
	const char *r_refdll_loaded = EngFuncs::GetCvarString( "r_refdll_loaded" );
	m_refs.Purge();

	for( int i = 0; ; i++ )
	{
		refdll temp;

		if( !EngFuncs::GetRenderers( i, temp.shortName, sizeof( temp.shortName ), temp.readable, sizeof( temp.readable )))
			break;

		// append asterisk to currently loaded renderer
		if( !stricmp( r_refdll_loaded, temp.shortName ))
			strncat( temp.readable, "*", sizeof( temp.readable ) - strlen( temp.readable ) - 1 );

		m_refs.AddToTail( temp );
	}
}

void CMenuVidModes::GetRendererConfig()
{
	// get current _configured_ renderer
	const char *refdll = EngFuncs::GetCvarString( "r_refdll" );

	if( !refdll[0] )
	{
		renderers.SetCurrentValue( 0.0f );
		return;
	}

	int i;
	for( i = 0; i < renderersModel.GetRows(); i++ )
	{
		if( !stricmp( renderersModel.GetShortName( i ), refdll ))
		{
			renderers.SetCurrentValue( i );
			break;
		}
	}

	if( i == renderersModel.GetRows( ))
	{
		renderersModel.Add( refdll ); // add unknown user selection
		renderers.SetCurrentValue( i );
	}
}

void CMenuVidModes::WriteRendererConfig()
{
	int i = renderers.GetCurrentValue();
	EngFuncs::CvarSetString( "r_refdll", renderersModel.GetShortName( i ));
}

// the renderer library is only loaded on startup
bool CMenuVidModes::IsRendererChanged()
{
	int i = renderers.GetCurrentValue();

	if( i < 0 || i >= renderersModel.GetRows( ))
		return false;

	return stricmp( renderersModel.GetShortName( i ), EngFuncs::GetCvarString( "r_refdll_loaded" )) != 0;
}

void CMenuVidModes::GetConfig()
{
	float fullscreen = EngFuncs::GetCvarFloat( "fullscreen" );
	fullscreen = bound( 0, fullscreen, 2 ); // windowed, fullscreen, borderless

	// the list opens on the aspect ratio of the current resolution
	const int width = EngFuncs::GetCvarFloat( "width" );
	const int height = EngFuncs::GetCvarFloat( "height" );
	int current = AspectOf( width, height );
	if( current == ASPECT_OTHER )
		current = ASPECT_ALL;
	aspect.SetCurrentValue( current );
	vidListModel.SetAspect( current );
	vidListModel.Update();

	int vid_mode = ModeOfSize( width, height );
	if( vid_mode < 0 )
		vid_mode = EngFuncs::GetCvarFloat( "vid_mode" );
	int row = vidListModel.RowOf( vid_mode );
	if( row < 0 )
	{
		aspect.SetCurrentValue( ASPECT_ALL );
		vidListModel.SetAspect( ASPECT_ALL );
		vidListModel.Update();
		row = bound( 0, vid_mode, vidListModel.GetRows( ));
	}

	windowMode.SetCurrentValue( fullscreen );
	vidList.SetCurrentIndex( row );

	renderers.UpdateCvar( true );
	vsync.UpdateCvar( true );

	ApplyChanges();
}

void CMenuVidModes::SetAspect( int value )
{
	// keep the chosen resolution selected when it is in the new list
	const int mode = vidListModel.ModeOf( vidList.GetCurrentIndex( ));

	vidListModel.SetAspect( value );
	vidListModel.Update();

	const int row = vidListModel.RowOf( mode );
	vidList.SetCurrentIndex( row >= 0 ? row : 0 );
}

void CMenuVidModes::SetMode( int w, int h )
{
	// only possible on Xash3D FWGS!
	EngFuncs::ClientCmdF( true, "vid_setmode %i %i\n", w, h );
}

void CMenuVidModes::SetMode( int mode )
{
	// vid_setmode is a new command, which does not depends on
	// static resolution list but instead uses dynamic resolution
	// list provided by video backend
	EngFuncs::ClientCmdF( true, "vid_setmode %i\n", mode );
}

/*
=================
UI_VidModes_SetConfig
=================
*/
void CMenuVidModes::SetConfig( )
{
	bool testMode = false;
	int  currentWindowModeIndex = windowMode.GetCurrentValue();
	int  currentModeIndex = vidListModel.ModeOf( vidList.GetCurrentIndex( ));
	int  w, h;
	bool isVidModeChanged = currentModeIndex >= 0 && ModeSize( currentModeIndex, w, h )
		&& ( w != prevModeX || h != prevModeY );
	bool isWindowedModeChanged = prevFullscreen != currentWindowModeIndex;

	/*
	checking windowed mode first because it'll be checked next in
	screen resolution changing code, otherwise when user try to
	change screen resolution and windowed flag at same time,
	only resolution will be changed.
	*/

	if( isWindowedModeChanged ) // moved to fullscreen, enable test mode
		testMode |= currentWindowModeIndex > 0;

	if( isVidModeChanged ) // have changed resolution, but enable test mode only in fullscreen
		testMode |= currentWindowModeIndex > 0;

	if( testMode ) // show this dialog before changing any settings
	{
		testModeMsgBox.Show();
		testModeTimer = gpGlobals->time + 10.0f; // ten seconds should be enough
	}

	vsync.WriteCvar();
	renderers.WriteCvar();

	if( isWindowedModeChanged )
		EngFuncs::CvarSetValue( "fullscreen", currentWindowModeIndex );

	if( isVidModeChanged )
	{
		SetMode( currentModeIndex );
		EngFuncs::CvarSetValue( "vid_mode", currentModeIndex );
	}

	// let the test mode dialog have the screen to itself, it's on a timer
	if( testMode )
		return;

	if( IsRendererChanged( ))
		restartMsgBox.Show();
	else
		Hide();
}

void CMenuVidModes::FinalizeChanges()
{
	prevFullscreen = EngFuncs::GetCvarFloat( "fullscreen" );
	prevModeX = EngFuncs::GetCvarFloat( "width" );
	prevModeY = EngFuncs::GetCvarFloat( "height" );
	prevMode = ModeOfSize( prevModeX, prevModeY );
	if( prevMode < 0 )
		prevMode = EngFuncs::GetCvarFloat( "vid_mode" );
}

// Apply does something only when a choice differs from what runs now: the
// size (not the mode's index), the window mode, the renderer, the vsync
bool CMenuVidModes::HasChanges()
{
	if( windowMode.GetCurrentValue() != prevFullscreen )
		return true;

	int w, h;
	const int mode = vidListModel.ModeOf( vidList.GetCurrentIndex( ));
	if( windowMode.GetCurrentValue() != 2 && mode >= 0 && ModeSize( mode, w, h ) && ( w != prevModeX || h != prevModeY ))
		return true;

	// a renderer other than the one chosen last (that one waits for a restart)
	const int r = renderers.GetCurrentValue();
	if( r >= 0 && r < renderersModel.GetRows() && stricmp( renderersModel.GetShortName( r ), EngFuncs::GetCvarString( "r_refdll" )) != 0 )
		return true;

	return vsync.bChecked != ( EngFuncs::GetCvarFloat( "gl_vsync" ) != 0.0f );
}

void CMenuVidModes::ApplyChanges()
{
	if( testModeMsgBox.IsVisible( ))
		return;

	FinalizeChanges();
}

void CMenuVidModes::RevertChanges()
{
	bool fullscreenChanged = false;

	// if we switched FROM fullscreen TO windowed, then we must get rid of fullscreen mode
	// so we can easily change the window size, without jerking display mode
	if( prevFullscreen == false && EngFuncs::GetCvarFloat( "fullscreen" ) != 0.0f )
	{
		EngFuncs::CvarSetValue( "fullscreen", prevFullscreen );
		fullscreenChanged = true;
	}

	SetMode( prevModeX, prevModeY );

	// otherwise, we better to set window size at first, then switch TO fullscreen
	if( !fullscreenChanged )
		EngFuncs::CvarSetValue( "fullscreen", prevFullscreen );
}

void CMenuVidModes::Draw()
{
	if( testModeMsgBox.IsVisible( ) && !FBitSet( testModeMsgBox.iFlags, QMF_CLOSING ))
	{
		if( testModeTimer - gpGlobals->time > 0 )
		{
			snprintf( testModeMsg, sizeof( testModeMsg ), L( "Keep this resolution? %i seconds remaining" ), (int)(testModeTimer - gpGlobals->time) );
		}
		else
		{
			testModeMsgBox.Hide();
			RevertChanges();
		}
	}

	if( apply )
	{
		const bool changes = HasChanges();
		apply->SetGrayed( !changes );
		apply->SetInactive( !changes );
	}

	CMenuFramework::Draw();
}

/*
=================
UI_VidModes_Init
=================
*/
void CMenuVidModes::_Init( void )
{
	static const char *windowModeStr[] =
	{
		L( "Windowed" ),
		L( "Fullscreen" ),
		L( "Borderless" ),
	};
	static CStringArrayModel windowModeModel( windowModeStr, V_ARRAYSIZE( windowModeStr ));
	static const char *aspectStr[] =
	{
		L( "All" ),
		"4:3",
		"16:10",
		"16:9",
	};
	static CStringArrayModel aspectModel( aspectStr, V_ARRAYSIZE( aspectStr ));

	banner.SetPicture( ART_BANNER );

	vidList.SetRect( 360, 230, -20, 365 );
	vidList.SetupColumn( 0, L( "GameUI_Resolution" ), 1.0f );
	vidList.SetModel( &vidListModel );

	vsync.szName = L( "GameUI_VSync" );
	vsync.SetCoord( 360, 670 );
	vsync.LinkCvar( "gl_vsync" );

	testModeMsgBox.SetMessage( testModeMsg );
	testModeMsgBox.onPositive = VoidCb( &CMenuVidModes::FinalizeChanges );
	testModeMsgBox.onNegative = VoidCb( &CMenuVidModes::RevertChanges );
	testModeMsgBox.Link( this );

	restartMsgBox.SetMessage( L( "Restart the game to apply changes" ) );
	restartMsgBox.onPositive = VoidCb( &CMenuVidModes::Hide );
	restartMsgBox.Link( this );

	renderersModel.Update();
	renderers.szName = L( "GameUI_Renderer" );
	renderers.Setup( &renderersModel );
	renderers.SetRect( 80, 480, 250, 32 );
	renderers.SetCharSize( QM_SMALLFONT );
	renderers.onCvarGet = VoidCb( &CMenuVidModes::GetRendererConfig );
	renderers.onCvarWrite = VoidCb( &CMenuVidModes::WriteRendererConfig );

	windowMode.szName = L( "Window mode" );
	windowMode.Setup( &windowModeModel );
	windowMode.SetRect( 80, 550, 250, 32 );
	windowMode.SetCharSize( QM_SMALLFONT );
	SET_EVENT_MULTI( windowMode.onChanged,
	{
		// disable vid modes list when borderless is used
		CMenuVidModes *parent = pSelf->GetParent( CMenuVidModes );
		parent->vidList.SetGrayed( parent->windowMode.GetCurrentValue( ) == 2 );
		parent->vidList.SetInactive( parent->windowMode.GetCurrentValue( ) == 2 );
	});

	aspect.szName = L( "Aspect ratio" );
	aspect.Setup( &aspectModel );
	aspect.SetRect( 80, 620, 250, 32 );
	aspect.SetCharSize( QM_SMALLFONT );
	SET_EVENT_MULTI( aspect.onChanged,
	{
		CMenuVidModes *parent = pSelf->GetParent( CMenuVidModes );
		parent->SetAspect( parent->aspect.GetCurrentValue( ));
	});

	AddItem( banner );
	apply = AddButton( L( "GameUI_Apply" ), nullptr, PC_OK, VoidCb( &CMenuVidModes::SetConfig ) );
	AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuVidModes::Hide ) );
	AddItem( renderers );
	AddItem( windowMode );
	AddItem( aspect );
	AddItem( vsync );
	AddItem( vidList );

	renderers.LinkCvar( "r_refdll", CMenuEditable::CVAR_STRING );
}

void CMenuVidModes::Reload()
{
	GetConfig();
	CMenuFramework::Reload();
}

ADD_MENU( menu_vidmodes, CMenuVidModes, UI_VidModes_Menu )
