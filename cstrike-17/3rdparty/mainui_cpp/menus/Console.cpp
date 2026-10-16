/*
Console.cpp -- the console as a window, as Steam's GoldSrc had it
(story 066 of the xash3d-fwgs fork)

A window over the menu, in game too: its title bar moves it, its corner
resizes it; the log in a TrueType font, its text selected with the mouse
(drag, double click a word, triple click a line) and copied with Ctrl+C or
a right click, Ctrl+A for all of it; the input line with its history, the
commands and cvars suggested as one types, Tab to complete, and "Submit".
The engine gives the lines (numbered from the first ever printed, so that
a selection keeps its lines while older ones are dropped) and the
clipboard; the console key toggles the window.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "BaseWindow.h"
#include "FontManager.h"
#include "utlstring.h"

// Steam's GoldSrc scheme, as Counter-Strike 1.6 had it
static const unsigned int CON_BG          = PackRGBA( 76, 88, 68, 250 );
static const unsigned int CON_BG_DARK     = PackRGBA( 62, 70, 55, 255 );
static const unsigned int CON_BORDER_LIT  = PackRGBA( 136, 145, 128, 255 );
static const unsigned int CON_BORDER_DARK = PackRGBA( 40, 46, 34, 255 );
static const unsigned int CON_TEXT        = PackRGBA( 216, 222, 211, 255 );
static const unsigned int CON_TEXT_DIM    = PackRGBA( 160, 170, 149, 255 );
static const unsigned int CON_SELECT_BG   = PackRGBA( 149, 136, 49, 255 );
static const unsigned int CON_SELECT_TEXT = PackRGBA( 255, 255, 255, 255 );

static const unsigned int s_colors[10] =
{
	PackRGBA( 0, 0, 0, 255 ),       PackRGBA( 255, 80, 80, 255 ),  PackRGBA( 120, 255, 120, 255 ),
	PackRGBA( 255, 255, 110, 255 ), PackRGBA( 120, 140, 255, 255 ), PackRGBA( 110, 255, 255, 255 ),
	PackRGBA( 255, 110, 255, 255 ), PackRGBA( 216, 222, 211, 255 ), PackRGBA( 255, 170, 60, 255 ),
	PackRGBA( 160, 160, 160, 255 ),
};

#define CON_INPUT_MAX     1024
#define CON_HISTORY       64
#define CON_SUGGEST_MAX   10
#define CON_SUGGEST_BUF   16384
#define CON_VISIBLE_ROWS  512

struct conpos_t
{
	int line;  // the line's number (engine numbering)
	int ch;    // byte in it
};

static bool PosLess( const conpos_t &a, const conpos_t &b )
{
	return a.line < b.line || ( a.line == b.line && a.ch < b.ch );
}

// one row on screen: a line, or a part of a long line
struct conrow_t
{
	int line;
	int start, end; // bytes
	int y;
};

static int UtfDecode( const char *s, int *len )
{
	const unsigned char *p = (const unsigned char *)s;

	if( p[0] < 0x80 ) { *len = 1; return p[0]; }
	if(( p[0] & 0xE0 ) == 0xC0 && ( p[1] & 0xC0 ) == 0x80 ) { *len = 2; return (( p[0] & 0x1F ) << 6 ) | ( p[1] & 0x3F ); }
	if(( p[0] & 0xF0 ) == 0xE0 && ( p[1] & 0xC0 ) == 0x80 && ( p[2] & 0xC0 ) == 0x80 )
	{
		*len = 3;
		return (( p[0] & 0x0F ) << 12 ) | (( p[1] & 0x3F ) << 6 ) | ( p[2] & 0x3F );
	}
	*len = 1;
	return p[0]; // a cp1251 byte, as the engine's console would show it
}

static bool IsColorCode( const char *s )
{
	return s[0] == '^' && s[1] >= '0' && s[1] <= '9';
}

static bool IsWordChar( int ch )
{
	return ch > 127 || isalnum( ch ) || ch == '_' || ch == '.' || ch == '-' || ch == ':' || ch == '/';
}

class CMenuConsole : public CMenuBaseWindow
{
public:
	typedef CMenuBaseWindow BaseClass;
	CMenuConsole() : BaseClass( "CMenuConsole" ) { bAllowDrag = false; }

	void Show() override;
	void Hide() override;
	void Draw() override;
	bool KeyDown( int key ) override;
	bool KeyUp( int key ) override;
	void Char( int key ) override;
	bool MouseMove( int x, int y ) override { return true; }

private:
	void _Init() override;
	void _VidInit() override;

	void Layout();
	void BuildFont();
	int CharWidth( int ch );
	int TextWidth( const char *s, int from, int to );

	void DrawFrame();
	void DrawLog();
	void DrawInput();
	void DrawSuggestions();
	void DrawButton( int x, int y, int w, int h, const char *label, bool pressed );

	bool HitLog( int x, int y, conpos_t &pos );
	void SelectWord( const conpos_t &at );
	void SelectLine( const conpos_t &at );
	void SelectAll();
	void CopySelection();
	bool HasSelection() const { return m_bSelecting || PosLess( m_selA, m_selB ) || PosLess( m_selB, m_selA ); }

	void Submit();
	void UpdateSuggestions();
	void Complete();
	void InsertText( const char *text );

	HFont m_font = 0;
	int m_fontTall = 0;
	int m_scale = 1;

	// in pixels, kept between openings
	int m_x = -1, m_y, m_w, m_h;
	Point m_dragFrom;
	enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_SELECT, DRAG_SCROLL } m_drag = DRAG_NONE;

	// areas
	int m_titleH, m_pad;
	int m_logX, m_logY, m_logW, m_logH;
	int m_inX, m_inY, m_inW, m_inH;
	int m_btnX, m_btnW;
	int m_closeX, m_closeY, m_closeS;

	// the log
	int m_scroll = 0;  // lines up from the bottom
	conrow_t m_rows[CON_VISIBLE_ROWS];
	int m_numRows = 0;
	int m_lastSerial = -1;
	int m_lastEnd = 0;
	int m_scrollbarY, m_scrollbarH, m_thumbY, m_thumbH;

	conpos_t m_selA = { 0, 0 }, m_selB = { 0, 0 };
	bool m_bSelecting = false;
	int m_lastClickTime = 0, m_clicks = 0;
	conpos_t m_lastClickPos = { -1, -1 };

	// the input
	char m_input[CON_INPUT_MAX] = "";
	int m_cursor = 0;
	char m_history[CON_HISTORY][CON_INPUT_MAX];
	int m_historyCount = 0, m_historyPos = 0;

	char m_suggestBuf[CON_SUGGEST_BUF];
	const char *m_suggest[CON_SUGGEST_MAX];
	int m_numSuggest = 0, m_suggestSel = -1;
	bool m_submitDown = false;
};

static CMenuConsole s_console;

void CMenuConsole::_Init()
{
	iFlags |= QMF_INACTIVE;
}

void CMenuConsole::BuildFont()
{
	// sized for the screen: 15 pixels at 768 lines, as Steam's console read
	m_fontTall = Q_max( 12, (int)( 15 * uiStatic.scaleY ));
	m_font = CFontBuilder( "Tahoma", m_fontTall, 500 ).Create();
}

void CMenuConsole::_VidInit()
{
	BuildFont();

	if( m_x < 0 || m_x + 100 > ScreenWidth || m_y + 50 > ScreenHeight )
	{
		m_w = ScreenWidth * 0.6f;
		m_h = ScreenHeight * 0.6f;
		m_x = ( ScreenWidth - m_w ) / 2;
		m_y = ( ScreenHeight - m_h ) / 2;
	}
	Layout();
}

void CMenuConsole::Layout()
{
	m_pad = Q_max( 4, m_fontTall / 3 );
	m_titleH = m_fontTall + m_pad * 2;
	m_inH = m_fontTall + m_pad * 2;

	m_w = Q_max( m_w, m_fontTall * 24 );
	m_h = Q_max( m_h, m_fontTall * 10 );

	m_scPos.x = m_x; m_scPos.y = m_y;
	m_scSize.w = m_w; m_scSize.h = m_h;

	m_closeS = m_titleH - m_pad;
	m_closeX = m_x + m_w - m_closeS - m_pad / 2;
	m_closeY = m_y + m_pad / 2;

	m_btnW = TextWidth( L( "Submit" ), 0, strlen( L( "Submit" ))) + m_pad * 4;
	m_inX = m_x + m_pad;
	m_inY = m_y + m_h - m_pad - m_inH;
	m_inW = m_w - m_pad * 3 - m_btnW;
	m_btnX = m_inX + m_inW + m_pad;

	m_logX = m_x + m_pad;
	m_logY = m_y + m_titleH;
	m_logW = m_w - m_pad * 2;
	m_logH = m_inY - m_pad - m_logY;
}

void CMenuConsole::Show()
{
	BaseClass::Show();
	m_scroll = 0;
	m_lastSerial = -1;
	EngFuncs::KEY_ClearStates();
}

void CMenuConsole::Hide()
{
	m_drag = DRAG_NONE;
	m_numSuggest = 0;
	BaseClass::Hide();
}

int CMenuConsole::CharWidth( int ch )
{
	return g_FontMgr->GetCharacterWidthScaled( m_font, ch, m_fontTall );
}

int CMenuConsole::TextWidth( const char *s, int from, int to )
{
	int w = 0;
	for( int i = from; i < to; )
	{
		int len;
		if( IsColorCode( s + i )) { i += 2; continue; }
		w += CharWidth( UtfDecode( s + i, &len ));
		i += len;
	}
	return w;
}

void CMenuConsole::DrawButton( int x, int y, int w, int h, const char *label, bool pressed )
{
	UI_FillRect( x, y, w, h, CON_BG );
	UI_FillRect( x, y, w, 1, pressed ? CON_BORDER_DARK : CON_BORDER_LIT );
	UI_FillRect( x, y, 1, h, pressed ? CON_BORDER_DARK : CON_BORDER_LIT );
	UI_FillRect( x, y + h - 1, w, 1, pressed ? CON_BORDER_LIT : CON_BORDER_DARK );
	UI_FillRect( x + w - 1, y, 1, h, pressed ? CON_BORDER_LIT : CON_BORDER_DARK );
	int tw = TextWidth( label, 0, strlen( label ));
	int cx = x + ( w - tw ) / 2, cy = y + ( h - m_fontTall ) / 2;
	for( const char *p = label; *p; )
	{
		int len, ch = UtfDecode( p, &len );
		cx += g_FontMgr->DrawCharacter( m_font, ch, Point( cx, cy ), m_fontTall, CON_TEXT );
		p += len;
	}
}

void CMenuConsole::DrawFrame()
{
	UI_FillRect( m_x, m_y, m_w, m_h, CON_BG );
	UI_FillRect( m_x, m_y, m_w, 1, CON_BORDER_LIT );
	UI_FillRect( m_x, m_y, 1, m_h, CON_BORDER_LIT );
	UI_FillRect( m_x, m_y + m_h - 1, m_w, 1, CON_BORDER_DARK );
	UI_FillRect( m_x + m_w - 1, m_y, 1, m_h, CON_BORDER_DARK );

	// the title
	int cx = m_x + m_pad * 2, cy = m_y + m_pad;
	for( const char *p = L( "Console" ); *p; )
	{
		int len, ch = UtfDecode( p, &len );
		cx += g_FontMgr->DrawCharacter( m_font, ch, Point( cx, cy ), m_fontTall, CON_TEXT );
		p += len;
	}

	// the close box
	DrawButton( m_closeX, m_closeY, m_closeS, m_closeS, "x", false );

	// the corner to resize from
	for( int i = 1; i <= 3; i++ )
	{
		int s = i * m_pad;
		UI_FillRect( m_x + m_w - s - 2, m_y + m_h - 3, 2, 2, CON_BORDER_LIT );
		UI_FillRect( m_x + m_w - 3, m_y + m_h - s - 2, 2, 2, CON_BORDER_LIT );
	}
}

void CMenuConsole::DrawLog()
{
	UI_FillRect( m_logX, m_logY, m_logW, m_logH, CON_BG_DARK );
	UI_FillRect( m_logX, m_logY, m_logW, 1, CON_BORDER_DARK );
	UI_FillRect( m_logX, m_logY, 1, m_logH, CON_BORDER_DARK );
	UI_FillRect( m_logX, m_logY + m_logH - 1, m_logW, 1, CON_BORDER_LIT );
	UI_FillRect( m_logX + m_logW - 1, m_logY, 1, m_logH, CON_BORDER_LIT );

	int first, count = EngFuncs::textfuncs.pfnConsoleLines ? EngFuncs::textfuncs.pfnConsoleLines( &first ) : 0;
	int serial = EngFuncs::textfuncs.pfnConsoleSerial ? EngFuncs::textfuncs.pfnConsoleSerial() : 0;

	// new lines keep the view where it is when scrolled up
	if( serial != m_lastSerial && m_scroll > 0 && m_lastSerial >= 0 )
		m_scroll += Q_max( 0, first + count - m_lastEnd );
	m_lastSerial = serial;
	m_lastEnd = first + count;

	int sbW = Q_max( 8, m_fontTall * 2 / 3 );
	int textX = m_logX + m_pad, textW = m_logW - m_pad * 2 - sbW;
	int bottom = m_logY + m_logH - m_pad;
	int rowH = m_fontTall + 1;
	int visibleLines = Q_max( 1, ( m_logH - m_pad * 2 ) / rowH );

	m_scroll = bound( 0, m_scroll, Q_max( 0, count - 1 ));

	// rows from the bottom up: each line wrapped to the width
	m_numRows = 0;
	int y = bottom;
	conrow_t tmp[64];
	for( int n = first + count - 1 - m_scroll; n >= first && y > m_logY + m_pad; n-- )
	{
		const char *s = EngFuncs::textfuncs.pfnConsoleLine( n );
		if( !s ) continue;
		int len = strlen( s );
		while( len > 0 && ( s[len - 1] == '\n' || s[len - 1] == '\r' )) len--;

		// split into rows
		int ntmp = 0, start = 0, w = 0, lastSpace = -1;
		for( int i = 0; i < len; )
		{
			int clen;
			if( IsColorCode( s + i )) { i += 2; continue; }
			int ch = UtfDecode( s + i, &clen );
			int cw = CharWidth( ch );
			if( w + cw > textW && i > start )
			{
				int cut = ( lastSpace > start ) ? lastSpace + 1 : i;
				if( ntmp < 64 ) { tmp[ntmp].line = n; tmp[ntmp].start = start; tmp[ntmp].end = cut; ntmp++; }
				start = cut; i = cut; w = 0; lastSpace = -1;
				continue;
			}
			if( ch == ' ' ) lastSpace = i;
			w += cw;
			i += clen;
		}
		if( ntmp < 64 ) { tmp[ntmp].line = n; tmp[ntmp].start = start; tmp[ntmp].end = len; ntmp++; }

		for( int r = ntmp - 1; r >= 0 && y > m_logY + m_pad; r-- )
		{
			y -= rowH;
			if( m_numRows < CON_VISIBLE_ROWS )
			{
				tmp[r].y = y;
				m_rows[m_numRows++] = tmp[r];
			}
		}
	}

	// selection, in order
	conpos_t a = m_selA, b = m_selB;
	if( PosLess( b, a )) { conpos_t t = a; a = b; b = t; }

	for( int r = 0; r < m_numRows; r++ )
	{
		const conrow_t &row = m_rows[r];
		if( row.y < m_logY + m_pad / 2 ) continue;
		const char *s = EngFuncs::textfuncs.pfnConsoleLine( row.line );
		if( !s ) continue;

		// the color in force at the row's start
		unsigned int color = CON_TEXT;
		for( int i = 0; i < row.start; i++ )
			if( IsColorCode( s + i )) { color = s[i + 1] == '7' ? CON_TEXT : s_colors[s[i + 1] - '0']; i++; }

		int x = textX;
		for( int i = row.start; i < row.end; )
		{
			if( IsColorCode( s + i ))
			{
				color = s[i + 1] == '7' ? CON_TEXT : s_colors[s[i + 1] - '0'];
				i += 2;
				continue;
			}
			int clen, ch = UtfDecode( s + i, &clen );
			int cw = CharWidth( ch );
			conpos_t p = { row.line, i };
			bool selected = HasSelection() && !PosLess( p, a ) && PosLess( p, b );
			if( selected )
				UI_FillRect( x, row.y, cw, rowH, CON_SELECT_BG );
			if( ch > ' ' )
				g_FontMgr->DrawCharacter( m_font, ch, Point( x, row.y ), m_fontTall, selected ? CON_SELECT_TEXT : color );
			x += cw;
			i += clen;
		}
	}

	// the scroll bar: lines
	int sbX = m_logX + m_logW - sbW - 1;
	m_scrollbarY = m_logY + 1;
	m_scrollbarH = m_logH - 2;
	UI_FillRect( sbX, m_scrollbarY, sbW, m_scrollbarH, CON_BG );
	if( count > visibleLines )
	{
		m_thumbH = Q_max( sbW, m_scrollbarH * visibleLines / count );
		int range = count - visibleLines;
		int pos = Q_max( 0, range - m_scroll ); // from the top
		m_thumbY = m_scrollbarY + ( m_scrollbarH - m_thumbH ) * Q_min( pos, range ) / Q_max( 1, range );
		DrawButton( sbX, m_thumbY, sbW, m_thumbH, "", false );
	}
	else
	{
		m_thumbY = m_scrollbarY;
		m_thumbH = m_scrollbarH;
	}
}

void CMenuConsole::DrawInput()
{
	UI_FillRect( m_inX, m_inY, m_inW, m_inH, CON_BG_DARK );
	UI_FillRect( m_inX, m_inY, m_inW, 1, CON_BORDER_DARK );
	UI_FillRect( m_inX, m_inY, 1, m_inH, CON_BORDER_DARK );
	UI_FillRect( m_inX, m_inY + m_inH - 1, m_inW, 1, CON_BORDER_LIT );
	UI_FillRect( m_inX + m_inW - 1, m_inY, 1, m_inH, CON_BORDER_LIT );

	int len = strlen( m_input );
	int room = m_inW - m_pad * 2;
	// scroll the text so that the cursor shows
	int from = 0;
	while( TextWidth( m_input, from, m_cursor ) > room && from < m_cursor )
		from = Con_UtfMoveRight( m_input, from, len );

	int x = m_inX + m_pad, y = m_inY + m_pad, cursorX = x;
	for( int i = from; i < len; )
	{
		if( i == m_cursor ) cursorX = x;
		int clen, ch = UtfDecode( m_input + i, &clen );
		int cw = CharWidth( ch );
		if( x + cw > m_inX + m_inW - m_pad ) break;
		g_FontMgr->DrawCharacter( m_font, ch, Point( x, y ), m_fontTall, CON_TEXT );
		x += cw;
		i += clen;
	}
	if( m_cursor >= len ) cursorX = x;
	if(( uiStatic.realTime / 500 ) & 1 )
		UI_FillRect( cursorX, y, Q_max( 1, m_fontTall / 12 ), m_fontTall, CON_TEXT );

	DrawButton( m_btnX, m_inY, m_btnW, m_inH, L( "Submit" ), m_submitDown );
}

void CMenuConsole::DrawSuggestions()
{
	if( !m_numSuggest )
		return;

	int rowH = m_fontTall + 2;
	int w = 0;
	for( int i = 0; i < m_numSuggest; i++ )
	{
		char line[256];
		const char *value = EngFuncs::GetCvarString( m_suggest[i] );
		snprintf( line, sizeof( line ), "%s %s", m_suggest[i], value );
		w = Q_max( w, TextWidth( line, 0, strlen( line )));
	}
	w += m_pad * 2;
	int h = rowH * m_numSuggest + m_pad;
	int x = m_inX, y = m_inY + m_inH;
	if( y + h > ScreenHeight ) y = m_inY - h;

	UI_FillRect( x, y, w, h, CON_BG_DARK );
	UI_DrawRectangle( x, y, w, h, CON_BORDER_LIT );
	for( int i = 0; i < m_numSuggest; i++ )
	{
		int ry = y + m_pad / 2 + i * rowH;
		if( i == m_suggestSel )
			UI_FillRect( x + 1, ry, w - 2, rowH, CON_SELECT_BG );
		int cx = x + m_pad;
		for( const char *p = m_suggest[i]; *p; )
		{
			int len, ch = UtfDecode( p, &len );
			cx += g_FontMgr->DrawCharacter( m_font, ch, Point( cx, ry + 1 ), m_fontTall, i == m_suggestSel ? CON_SELECT_TEXT : CON_TEXT );
			p += len;
		}
		const char *value = EngFuncs::GetCvarString( m_suggest[i] );
		if( value && *value )
		{
			cx += CharWidth( ' ' );
			for( const char *p = value; *p; )
			{
				int len, ch = UtfDecode( p, &len );
				cx += g_FontMgr->DrawCharacter( m_font, ch, Point( cx, ry + 1 ), m_fontTall, CON_TEXT_DIM );
				p += len;
			}
		}
	}
}

void CMenuConsole::Draw()
{
	if( !m_font || m_fontTall != Q_max( 12, (int)( 15 * uiStatic.scaleY )))
	{
		BuildFont();
		Layout();
	}

	// dragging: the window, its size, the selection, the scroll bar
	int mx = uiStatic.cursorX, my = uiStatic.cursorY;
	switch( m_drag )
	{
	case DRAG_MOVE:
		m_x = bound( -m_w + 60, m_x + mx - m_dragFrom.x, (int)ScreenWidth - 60 );
		m_y = bound( 0, m_y + my - m_dragFrom.y, (int)ScreenHeight - m_titleH );
		m_dragFrom = Point( mx, my );
		Layout();
		break;
	case DRAG_RESIZE:
		m_w = Q_min( m_w + mx - m_dragFrom.x, (int)ScreenWidth - m_x );
		m_h = Q_min( m_h + my - m_dragFrom.y, (int)ScreenHeight - m_y );
		m_dragFrom = Point( mx, my );
		Layout();
		break;
	case DRAG_SELECT:
	{
		conpos_t p;
		if( my < m_logY ) m_scroll++;
		else if( my > m_logY + m_logH ) m_scroll = Q_max( 0, m_scroll - 1 );
		if( HitLog( mx, my, p ))
			m_selB = p;
		break;
	}
	case DRAG_SCROLL:
	{
		int first, count = EngFuncs::textfuncs.pfnConsoleLines( &first );
		int dy = my - m_dragFrom.y;
		int lines = dy * count / Q_max( 1, m_scrollbarH );
		if( lines )
		{
			m_scroll = bound( 0, m_scroll - lines, Q_max( 0, count - 1 ));
			m_dragFrom = Point( mx, my );
		}
		break;
	}
	default:
		break;
	}

	DrawFrame();
	DrawLog();
	DrawInput();
	DrawSuggestions();
}

bool CMenuConsole::HitLog( int x, int y, conpos_t &pos )
{
	if( !m_numRows )
		return false;

	int rowH = m_fontTall + 1;
	// the row under the point, else the nearest
	const conrow_t *row = &m_rows[0];
	for( int r = 0; r < m_numRows; r++ )
	{
		row = &m_rows[r];
		if( y >= m_rows[r].y )
			break;
	}
	if( y > m_rows[0].y + rowH )
		row = &m_rows[0];

	const char *s = EngFuncs::textfuncs.pfnConsoleLine( row->line );
	if( !s )
		return false;

	int cx = m_logX + m_pad;
	int i = row->start;
	while( i < row->end )
	{
		if( IsColorCode( s + i )) { i += 2; continue; }
		int clen, cw = CharWidth( UtfDecode( s + i, &clen ));
		if( x < cx + cw / 2 )
			break;
		cx += cw;
		i += clen;
	}
	pos.line = row->line;
	pos.ch = i;
	return true;
}

void CMenuConsole::SelectWord( const conpos_t &at )
{
	const char *s = EngFuncs::textfuncs.pfnConsoleLine( at.line );
	if( !s ) return;
	int len = strlen( s ), a = at.ch, b = at.ch;
	while( a > 0 && IsWordChar( (unsigned char)s[a - 1] )) a--;
	while( b < len && IsWordChar( (unsigned char)s[b] )) b++;
	m_selA.line = m_selB.line = at.line;
	m_selA.ch = a;
	m_selB.ch = b;
}

void CMenuConsole::SelectLine( const conpos_t &at )
{
	const char *s = EngFuncs::textfuncs.pfnConsoleLine( at.line );
	if( !s ) return;
	m_selA.line = at.line; m_selA.ch = 0;
	m_selB.line = at.line; m_selB.ch = strlen( s );
}

void CMenuConsole::SelectAll()
{
	int first, count = EngFuncs::textfuncs.pfnConsoleLines( &first );
	if( !count ) return;
	const char *last = EngFuncs::textfuncs.pfnConsoleLine( first + count - 1 );
	m_selA.line = first; m_selA.ch = 0;
	m_selB.line = first + count - 1; m_selB.ch = last ? strlen( last ) : 0;
}

void CMenuConsole::CopySelection()
{
	if( !EngFuncs::textfuncs.pfnSetClipboardData || !HasSelection() )
		return;

	conpos_t a = m_selA, b = m_selB;
	if( PosLess( b, a )) { conpos_t t = a; a = b; b = t; }

	CUtlString text;
	for( int n = a.line; n <= b.line; n++ )
	{
		const char *s = EngFuncs::textfuncs.pfnConsoleLine( n );
		if( !s ) continue;
		int len = strlen( s );
		int from = ( n == a.line ) ? Q_min( a.ch, len ) : 0;
		int to = ( n == b.line ) ? Q_min( b.ch, len ) : len;
		for( int i = from; i < to; i++ )
		{
			if( IsColorCode( s + i )) { i++; continue; }
			if( s[i] == '\r' ) continue;
			char c[2] = { s[i], 0 };
			text.Append( c );
		}
		if( n != b.line && !( len && s[len - 1] == '\n' ))
			text.Append( "\n" );
	}
#ifdef _WIN32
	// Windows' text fields want CR LF
	CUtlString crlf;
	for( const char *p = text.Get(); *p; p++ )
	{
		if( *p == '\n' ) crlf.Append( "\r" );
		char c[2] = { *p, 0 };
		crlf.Append( c );
	}
	EngFuncs::textfuncs.pfnSetClipboardData( crlf.Get() );
#else
	EngFuncs::textfuncs.pfnSetClipboardData( text.Get() );
#endif
}

void CMenuConsole::InsertText( const char *text )
{
	for( const char *p = text; *p; p++ )
	{
		if( (unsigned char)*p < 32 ) continue;
		int len = strlen( m_input );
		if( len >= CON_INPUT_MAX - 1 ) break;
		memmove( m_input + m_cursor + 1, m_input + m_cursor, len - m_cursor + 1 );
		m_input[m_cursor++] = *p;
	}
	UpdateSuggestions();
}

void CMenuConsole::UpdateSuggestions()
{
	m_numSuggest = 0;
	m_suggestSel = -1;

	// the command's name: the first word, while one types it
	if( !m_input[0] || strchr( m_input, ' ' ) || !EngFuncs::textfuncs.pfnConsoleCompletions )
		return;

	EngFuncs::textfuncs.pfnConsoleCompletions( m_input, m_suggestBuf, sizeof( m_suggestBuf ));

	// sorted, the shortest first among the same start
	const char *all[512];
	int n = 0;
	for( char *p = m_suggestBuf; *p && n < 512; )
	{
		char *e = strchr( p, '\n' );
		if( !e ) break;
		*e = 0;
		all[n++] = p;
		p = e + 1;
	}
	for( int i = 1; i < n; i++ )
		for( int j = i; j > 0 && stricmp( all[j], all[j - 1] ) < 0; j-- )
		{ const char *t = all[j]; all[j] = all[j - 1]; all[j - 1] = t; }

	for( int i = 0; i < n && m_numSuggest < CON_SUGGEST_MAX; i++ )
		m_suggest[m_numSuggest++] = all[i];
}

void CMenuConsole::Complete()
{
	if( !m_numSuggest )
		return;

	const char *pick = m_suggest[m_suggestSel >= 0 ? m_suggestSel : 0];
	snprintf( m_input, sizeof( m_input ), "%s ", pick );
	m_cursor = strlen( m_input );
	m_numSuggest = 0;
	m_suggestSel = -1;
}

void CMenuConsole::Submit()
{
	if( !m_input[0] )
		return;

	// as the engine's console: the command echoed, then run
	char safe[CON_INPUT_MAX];
	int j = 0;
	for( int i = 0; m_input[i] && j < CON_INPUT_MAX - 1; i++ )
		safe[j++] = m_input[i] == '"' ? '\'' : m_input[i];
	safe[j] = 0;
	EngFuncs::ClientCmdF( false, "echo \"] %s\"\n", safe );
	EngFuncs::ClientCmdF( false, "%s\n", m_input );

	if( !m_historyCount || strcmp( m_history[( m_historyCount - 1 ) % CON_HISTORY], m_input ))
		Q_strncpy( m_history[( m_historyCount++ ) % CON_HISTORY], m_input, CON_INPUT_MAX );
	m_historyPos = m_historyCount;

	m_input[0] = 0;
	m_cursor = 0;
	m_scroll = 0;
	m_numSuggest = 0;
}

bool CMenuConsole::KeyDown( int key )
{
	int mx = uiStatic.cursorX, my = uiStatic.cursorY;
	bool ctrl = EngFuncs::KEY_IsDown( K_CTRL );
	int len = strlen( m_input );

	if( UI::Key::IsEscape( key ))
	{
		if( m_numSuggest ) { m_numSuggest = 0; return true; }
		Hide();
		return true;
	}

	if( key == K_MOUSE1 )
	{
		if( mx >= m_closeX && mx < m_closeX + m_closeS && my >= m_closeY && my < m_closeY + m_closeS )
		{
			Hide();
			return true;
		}
		if( mx >= m_btnX && mx < m_btnX + m_btnW && my >= m_inY && my < m_inY + m_inH )
		{
			m_submitDown = true;
			return true;
		}
		if( mx >= m_x + m_w - m_pad * 3 && my >= m_y + m_h - m_pad * 3 && mx < m_x + m_w && my < m_y + m_h )
		{
			m_drag = DRAG_RESIZE;
			m_dragFrom = Point( mx, my );
			return true;
		}
		if( mx >= m_x && mx < m_x + m_w && my >= m_y && my < m_y + m_titleH )
		{
			m_drag = DRAG_MOVE;
			m_dragFrom = Point( mx, my );
			return true;
		}
		int sbW = Q_max( 8, m_fontTall * 2 / 3 );
		if( mx >= m_logX + m_logW - sbW - 1 && mx < m_logX + m_logW && my >= m_logY && my < m_logY + m_logH )
		{
			m_drag = DRAG_SCROLL;
			m_dragFrom = Point( mx, my );
			return true;
		}
		conpos_t p;
		if( mx >= m_logX && mx < m_logX + m_logW && my >= m_logY && my < m_logY + m_logH && HitLog( mx, my, p ))
		{
			// one click starts a selection, two a word, three a line
			bool same = p.line == m_lastClickPos.line && abs( p.ch - m_lastClickPos.ch ) <= 1;
			if( same && uiStatic.realTime - m_lastClickTime < 400 )
				m_clicks++;
			else
				m_clicks = 1;
			m_lastClickTime = uiStatic.realTime;
			m_lastClickPos = p;

			if( m_clicks == 2 ) SelectWord( p );
			else if( m_clicks >= 3 ) SelectLine( p );
			else
			{
				m_selA = m_selB = p;
				m_bSelecting = true;
				m_drag = DRAG_SELECT;
			}
			return true;
		}
		// a suggestion clicked
		if( m_numSuggest )
		{
			int rowH = m_fontTall + 2, y = m_inY + m_inH;
			if( y + rowH * m_numSuggest + m_pad > ScreenHeight ) y = m_inY - ( rowH * m_numSuggest + m_pad );
			int i = ( my - y - m_pad / 2 ) / rowH;
			if( mx >= m_inX && my >= y && i >= 0 && i < m_numSuggest )
			{
				m_suggestSel = i;
				Complete();
				return true;
			}
		}
		return true;
	}

	if( key == K_MOUSE2 )
	{
		CopySelection();
		return true;
	}

	if( key == K_MWHEELUP || key == K_PGUP )
	{
		m_scroll += ( key == K_PGUP ) ? Q_max( 1, m_logH / ( m_fontTall + 1 ) - 1 ) : 3;
		return true;
	}
	if( key == K_MWHEELDOWN || key == K_PGDN )
	{
		m_scroll = Q_max( 0, m_scroll - (( key == K_PGDN ) ? Q_max( 1, m_logH / ( m_fontTall + 1 ) - 1 ) : 3 ));
		return true;
	}
	if( ctrl && key == K_HOME ) { int first; m_scroll = EngFuncs::textfuncs.pfnConsoleLines( &first ); return true; }
	if( ctrl && key == K_END ) { m_scroll = 0; return true; }

	if( UI::Key::IsEnter( key ))
	{
		if( m_numSuggest && m_suggestSel >= 0 ) Complete();
		else Submit();
		return true;
	}
	if( key == K_TAB )
	{
		if( m_numSuggest )
		{
			m_suggestSel = ( m_suggestSel + 1 ) % m_numSuggest;
			if( m_numSuggest == 1 ) Complete();
		}
		return true;
	}
	if( key == K_UPARROW )
	{
		if( m_numSuggest ) m_suggestSel = m_suggestSel <= 0 ? m_numSuggest - 1 : m_suggestSel - 1;
		else if( m_historyPos > 0 && m_historyPos > m_historyCount - CON_HISTORY )
		{
			m_historyPos--;
			Q_strncpy( m_input, m_history[m_historyPos % CON_HISTORY], sizeof( m_input ));
			m_cursor = strlen( m_input );
		}
		return true;
	}
	if( key == K_DOWNARROW )
	{
		if( m_numSuggest ) m_suggestSel = ( m_suggestSel + 1 ) % m_numSuggest;
		else if( m_historyPos < m_historyCount )
		{
			m_historyPos++;
			if( m_historyPos == m_historyCount ) m_input[0] = 0;
			else Q_strncpy( m_input, m_history[m_historyPos % CON_HISTORY], sizeof( m_input ));
			m_cursor = strlen( m_input );
		}
		return true;
	}
	if( key == K_LEFTARROW ) { if( m_cursor > 0 ) m_cursor = Con_UtfMoveLeft( m_input, m_cursor ); return true; }
	if( key == K_RIGHTARROW ) { if( m_cursor < len ) m_cursor = Con_UtfMoveRight( m_input, m_cursor, len ); return true; }
	if( key == K_HOME ) { m_cursor = 0; return true; }
	if( key == K_END ) { m_cursor = len; return true; }
	if( key == K_BACKSPACE )
	{
		if( m_cursor > 0 )
		{
			int to = Con_UtfMoveLeft( m_input, m_cursor );
			memmove( m_input + to, m_input + m_cursor, len - m_cursor + 1 );
			m_cursor = to;
			UpdateSuggestions();
		}
		return true;
	}
	if( key == K_DEL )
	{
		if( m_cursor < len )
		{
			int to = Con_UtfMoveRight( m_input, m_cursor, len );
			memmove( m_input + m_cursor, m_input + to, len - to + 1 );
			UpdateSuggestions();
		}
		return true;
	}

	return true;
}

bool CMenuConsole::KeyUp( int key )
{
	if( key == K_MOUSE1 )
	{
		if( m_submitDown )
		{
			m_submitDown = false;
			int mx = uiStatic.cursorX, my = uiStatic.cursorY;
			if( mx >= m_btnX && mx < m_btnX + m_btnW && my >= m_inY && my < m_inY + m_inH )
				Submit();
		}
		if( m_drag == DRAG_SELECT )
			m_bSelecting = false;
		m_drag = DRAG_NONE;
	}
	return true;
}

void CMenuConsole::Char( int key )
{
	// Ctrl+C copies the selection, Ctrl+A selects all, Ctrl+V pastes
	if( key == 'c' - 'a' + 1 ) { CopySelection(); return; }
	if( key == 'a' - 'a' + 1 ) { SelectAll(); return; }
	if( key == 'v' - 'a' + 1 )
	{
		const char *clip = EngFuncs::GetClipboardData();
		if( clip ) InsertText( clip );
		return;
	}
	if( key < 32 || key == '`' || key == '~' )
		return;

	char c[2] = { (char)key, 0 };
	InsertText( c );
}

/*
=================
the engine's console key
=================
*/
void UI_ShowConsole( int show )
{
	if( show )
	{
		if( !s_console.IsVisible( ))
			s_console.Show();
	}
	else if( s_console.IsVisible( ))
	{
		s_console.Hide();
	}
}

int UI_ConsoleShown( void )
{
	return s_console.IsVisible() ? 1 : 0;
}
