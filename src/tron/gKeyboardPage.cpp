/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

***************************************************************************

*/

#include "gMenus.h"

// DEDICATED comes from config.h
#include "defs.h"

#ifdef DEDICATED

void sg_KeyboardPage(){}

#else

#include "rSDL.h"

#include "ePlayer.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tSysTime.h"
#include "uInput.h"
#include "uMenu.h"
#include "uRclTheme.h"
#include "gHud.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// /kb: the keyboard over the game, to see and change binds quickly. Jamie:
// "what if /kb loaded a virtual keyboard that allowed quick bind", and
// "there are more game controls that can be bound like instant chats and
// saved views". Typed in chat (ePlayer.cpp keeps it from the server) or from
// Settings > Controls. A key is picked by clicking it or pressing it, then
// what it does is picked on the right. Two tabs: what the keys do playing
// (player 1's binds and the global ones, the usual bind table) and what they
// do watching (the spectator's own keys, uSpectatorKey). Every change is
// saved at once the usual way (st_SaveConfig); nothing changes until a pick.
// The look is the mock's (rcl-cfg/client-ui/kb-mock.png): the RCL palette,
// the interface font, the lime accent.

int sg_KeyboardLayoutIso();     // at the end: the Windows keyboard layout

namespace
{
using namespace uRclTheme;

// ---- colours: the theme's and the mock's ----
Color const sg_kbCard   = { 42 / 255.f, 44 / 255.f, 46 / 255.f };
Color const sg_kbField  = { 24 / 255.f, 25 / 255.f, 26 / 255.f };
Color const sg_kbQuiet  = { 122 / 255.f, 124 / 255.f, 126 / 255.f };
Color const sg_kbDim    = { 90 / 255.f, 92 / 255.f, 94 / 255.f };
Color const sg_kbHover  = { 52 / 255.f, 54 / 255.f, 57 / 255.f };
Color const sg_kbBlue   = { 110 / 255.f, 200 / 255.f, 1 };
Color const sg_kbPurple = { 190 / 255.f, 140 / 255.f, 1 };
Color const sg_kbPink   = { 1, 140 / 255.f, 190 / 255.f };

// ---- what a key can do ----
enum Group
{
    Group_Riding, Group_Looking, Group_Camera, Group_Chat, Group_InstantChat, Group_Game, Group_Other,
    Group_Flying, Group_Views, Group_Nothing, Group_Count
};
char const * const sg_groupName[ Group_Count ] =
{
    "riding", "looking", "camera", "chat", "instant chat", "game", "other", "flying", "views", "nothing"
};
Color const & GroupColor( int group )
{
    switch ( group )
    {
    case Group_Riding:      return accent;
    case Group_Looking:     return sg_kbBlue;
    case Group_Camera:      return sg_kbPurple;
    case Group_Chat:
    case Group_InstantChat: return sg_kbPink;
    case Group_Game:        return textSecondary;
    case Group_Flying:      return sg_kbBlue;
    case Group_Views:       return sg_kbPurple;
    default:                return sg_kbQuiet;
    }
}

// what the playing tab's actions are, by name, with their short labels for
// the keys; the order is the order they are listed in
struct Known
{
    char const * name;
    int group;
    char const * cap;
};
Known const sg_known[] =
{
    { "CYCLE_TURN_LEFT", Group_Riding, "turn L" },
    { "CYCLE_TURN_RIGHT", Group_Riding, "turn R" },
    { "CYCLE_BRAKE", Group_Riding, "brake" },
    { "CYCLE_BRAKE_TOGGLE", Group_Riding, "brake lock" },
    { "GLANCE_BACK", Group_Looking, "look back" },
    { "GLANCE_FORWARD", Group_Looking, "look fwd" },
    { "GLANCE_LEFT", Group_Looking, "look L" },
    { "GLANCE_RIGHT", Group_Looking, "look R" },
    { "SWITCH_VIEW", Group_Camera, "view" },
    { "ZOOM_IN", Group_Camera, "zoom in" },
    { "ZOOM_OUT", Group_Camera, "zoom out" },
    { "LOOK_LEFT", Group_Camera, "pan L" },
    { "LOOK_RIGHT", Group_Camera, "pan R" },
    { "BANK_UP", Group_Camera, "tilt up" },
    { "BANK_DOWN", Group_Camera, "tilt dn" },
    { "MOVE_FORWARD", Group_Camera, "cam fwd" },
    { "MOVE_BACK", Group_Camera, "cam back" },
    { "MOVE_LEFT", Group_Camera, "cam L" },
    { "MOVE_RIGHT", Group_Camera, "cam R" },
    { "MOVE_UP", Group_Camera, "cam up" },
    { "MOVE_DOWN", Group_Camera, "cam dn" },
    { "CHAT", Group_Chat, "chat" },
    { "CONSOLE_INPUT", Group_Chat, "console" },
    { "MESS_UP", Group_Chat, "msgs up" },
    { "MESS_DOWN", Group_Chat, "msgs dn" },
    { "MESS_END", Group_Chat, "msgs end" },
    { "SCORE", Group_Game, "scores" },
    { "INGAME_MENU", Group_Game, "menu" },
    { "PAUSE_GAME", Group_Game, "pause" },
    { "TOGGLE_SPECTATOR", Group_Game, "spectate" },
    { "SCREENSHOT", Group_Game, "picture" },
    { "TOGGLE_FULLSCREEN", Group_Game, "fullscreen" },
    { "TOGGLE_MOUSEGRAB", Group_Game, "mouse grab" },
    { "RELOAD_TEXTURES", Group_Game, "textures" },
};

// the spectating tab's: the watcher's keys, by uSpectatorKey
char const * const sg_spectatorCap[ uSpectatorKey_Count ] =
{
    "fwd", "back", "turn L", "turn R", "side L", "side R", "up", "down", "fast", "look", "steer",
    "view 1", "view 2", "view 3", "view 4", "view 5", "view 6", "view 7", "view 8", "view 9", "next", "prev"
};

struct Doing
{
    int group;
    uAction * action;       // playing: the action
    int player;             // and the player its binds are for: 1, or 0 for the global ones
    int spectator;          // spectating: the key's uSpectatorKey, else -1
    int chat;               // an instant chat's number, 1 to 25, else 0
    int order;
    std::string label;      // in the picker
    std::string cap;        // on the key
};

std::string Lower( std::string text )
{
    for ( size_t i = 0; i < text.size(); ++i )
        text[i] = char( tolower( static_cast< unsigned char >( text[i] ) ) );
    return text;
}

std::string Said( tOutput const & output )
{
    tString text;
    text << output;
    return std::string( static_cast< char const * >( text ) );
}

// ---- the keys, as they lie on the keyboard ----
struct Cap
{
    char const * legend;
    int sym;
    REAL x, y, w, h;        // in key units from the keyboard's top left
};

struct Row
{
    char const * legend;
    int sym;
    REAL units;
};

// the main block, US ANSI and UK ISO. SDL names a key by where it is on a US
// keyboard: the UK's # is its backslash, the UK's \ is its "less" key.
Row const sg_ansi[5][16] =
{
    { { "`", SDLK_BACKQUOTE, 1 }, { "1", SDLK_1, 1 }, { "2", SDLK_2, 1 }, { "3", SDLK_3, 1 }, { "4", SDLK_4, 1 },
      { "5", SDLK_5, 1 }, { "6", SDLK_6, 1 }, { "7", SDLK_7, 1 }, { "8", SDLK_8, 1 }, { "9", SDLK_9, 1 },
      { "0", SDLK_0, 1 }, { "-", SDLK_MINUS, 1 }, { "=", SDLK_EQUALS, 1 }, { "back", SDLK_BACKSPACE, 2 }, { 0, 0, 0 } },
    { { "tab", SDLK_TAB, 1.5f }, { "Q", SDLK_q, 1 }, { "W", SDLK_w, 1 }, { "E", SDLK_e, 1 }, { "R", SDLK_r, 1 },
      { "T", SDLK_t, 1 }, { "Y", SDLK_y, 1 }, { "U", SDLK_u, 1 }, { "I", SDLK_i, 1 }, { "O", SDLK_o, 1 },
      { "P", SDLK_p, 1 }, { "[", SDLK_LEFTBRACKET, 1 }, { "]", SDLK_RIGHTBRACKET, 1 }, { "\\", SDLK_BACKSLASH, 1.5f }, { 0, 0, 0 } },
    { { "caps", SDLK_CAPSLOCK, 1.75f }, { "A", SDLK_a, 1 }, { "S", SDLK_s, 1 }, { "D", SDLK_d, 1 }, { "F", SDLK_f, 1 },
      { "G", SDLK_g, 1 }, { "H", SDLK_h, 1 }, { "J", SDLK_j, 1 }, { "K", SDLK_k, 1 }, { "L", SDLK_l, 1 },
      { ";", SDLK_SEMICOLON, 1 }, { "'", SDLK_QUOTE, 1 }, { "enter", SDLK_RETURN, 2.25f }, { 0, 0, 0 } },
    { { "shift", SDLK_LSHIFT, 2.25f }, { "Z", SDLK_z, 1 }, { "X", SDLK_x, 1 }, { "C", SDLK_c, 1 }, { "V", SDLK_v, 1 },
      { "B", SDLK_b, 1 }, { "N", SDLK_n, 1 }, { "M", SDLK_m, 1 }, { ",", SDLK_COMMA, 1 }, { ".", SDLK_PERIOD, 1 },
      { "/", SDLK_SLASH, 1 }, { "shift", SDLK_RSHIFT, 2.75f }, { 0, 0, 0 } },
    { { "ctrl", SDLK_LCTRL, 1.25f }, { "win", SDLK_LSUPER, 1.25f }, { "alt", SDLK_LALT, 1.25f }, { "space", SDLK_SPACE, 6.25f },
      { "alt", SDLK_RALT, 1.25f }, { "win", SDLK_RSUPER, 1.25f }, { "menu", SDLK_MENU, 1.25f }, { "ctrl", SDLK_RCTRL, 1.25f }, { 0, 0, 0 } }
};
Row const sg_iso[5][16] =
{
    { { "`", SDLK_BACKQUOTE, 1 }, { "1", SDLK_1, 1 }, { "2", SDLK_2, 1 }, { "3", SDLK_3, 1 }, { "4", SDLK_4, 1 },
      { "5", SDLK_5, 1 }, { "6", SDLK_6, 1 }, { "7", SDLK_7, 1 }, { "8", SDLK_8, 1 }, { "9", SDLK_9, 1 },
      { "0", SDLK_0, 1 }, { "-", SDLK_MINUS, 1 }, { "=", SDLK_EQUALS, 1 }, { "back", SDLK_BACKSPACE, 2 }, { 0, 0, 0 } },
    { { "tab", SDLK_TAB, 1.5f }, { "Q", SDLK_q, 1 }, { "W", SDLK_w, 1 }, { "E", SDLK_e, 1 }, { "R", SDLK_r, 1 },
      { "T", SDLK_t, 1 }, { "Y", SDLK_y, 1 }, { "U", SDLK_u, 1 }, { "I", SDLK_i, 1 }, { "O", SDLK_o, 1 },
      { "P", SDLK_p, 1 }, { "[", SDLK_LEFTBRACKET, 1 }, { "]", SDLK_RIGHTBRACKET, 1 }, { "enter", SDLK_RETURN, 1.5f }, { 0, 0, 0 } },
    { { "caps", SDLK_CAPSLOCK, 1.75f }, { "A", SDLK_a, 1 }, { "S", SDLK_s, 1 }, { "D", SDLK_d, 1 }, { "F", SDLK_f, 1 },
      { "G", SDLK_g, 1 }, { "H", SDLK_h, 1 }, { "J", SDLK_j, 1 }, { "K", SDLK_k, 1 }, { "L", SDLK_l, 1 },
      { ";", SDLK_SEMICOLON, 1 }, { "'", SDLK_QUOTE, 1 }, { "#", SDLK_BACKSLASH, 1 }, { "", SDLK_RETURN, 1.25f }, { 0, 0, 0 } },
    { { "shift", SDLK_LSHIFT, 1.25f }, { "\\", SDLK_LESS, 1 }, { "Z", SDLK_z, 1 }, { "X", SDLK_x, 1 }, { "C", SDLK_c, 1 },
      { "V", SDLK_v, 1 }, { "B", SDLK_b, 1 }, { "N", SDLK_n, 1 }, { "M", SDLK_m, 1 }, { ",", SDLK_COMMA, 1 },
      { ".", SDLK_PERIOD, 1 }, { "/", SDLK_SLASH, 1 }, { "shift", SDLK_RSHIFT, 2.75f }, { 0, 0, 0 } },
    { { "ctrl", SDLK_LCTRL, 1.25f }, { "win", SDLK_LSUPER, 1.25f }, { "alt", SDLK_LALT, 1.25f }, { "space", SDLK_SPACE, 6.25f },
      { "alt gr", SDLK_RALT, 1.25f }, { "win", SDLK_RSUPER, 1.25f }, { "menu", SDLK_MENU, 1.25f }, { "ctrl", SDLK_RCTRL, 1.25f }, { 0, 0, 0 } }
};

// the mock's measures, in its pixels at 1920 by 1080
const REAL sg_unit = 74, sg_gap = 6;

// something on the page that a click does something with
enum Spot
{
    Spot_Tab, Spot_Key, Spot_Chip, Spot_Row, Spot_Search, Spot_Message, Spot_Replace, Spot_Cancel, Spot_Picker
};
struct Hit
{
    REAL left, top, right, bottom;  // logical pixels
    int spot;
    int value;
};

class gKeyboardPage;

// the page's one row: it draws nothing, and keeps the menu's own way out hidden
class gKeyboardRow: public uMenuItem
{
public:
    gKeyboardRow( uMenu * menu ): uMenuItem( menu, tOutput() ){}
    virtual bool OwnsRow(){ return true; }
    virtual tString Help(){ return tString(); }
};

class gKeyboardPage: public uMenu
{
public:
    gKeyboardPage();
    virtual ~gKeyboardPage();

protected:
    virtual void RenderExtras( REAL alpha );
    virtual void HandleEvent( SDL_Event event );

private:
    // what the keys do
    void Build();
    int DoingOf( int tab, int sym ) const;          //!< what the key does in the tab, -1 nothing, -2 another player's
    std::string KeysOf( int doing ) const;          //!< the keys that do it, in the tab
    void Pick( int doing );                         //!< the selected key does it; asks first if that replaces something
    void Apply( int doing );                        //!< and now
    void Select( int sym );

    // drawing
    void Measure();
    REAL PX( REAL mock ) const { return left_ + ( mock - 110 ) * k_; }
    REAL PY( REAL mock ) const { return top_ + ( mock - 150 ) * k_; }
    REAL S( REAL mock ) const { return mock * k_; }
    void Box( REAL l, REAL t, REAL r, REAL b, Color const & color, REAL alpha, REAL radius );
    void Outline( REAL l, REAL t, REAL r, REAL b, Color const & color, REAL alpha, int pixels );
    void Label( REAL x, REAL y, REAL size, int weight, Color const & color, REAL alpha, char const * text, int align = -1, REAL most = 0 );
    void AddHit( REAL l, REAL t, REAL r, REAL b, int spot, int value );
    void DrawKey( char const * legend, int sym, REAL l, REAL t, REAL r, REAL b, REAL alpha, REAL radius = 10, bool centreLegend = false );
    void DrawKeyboard( REAL alpha );
    void DrawPicker( REAL alpha );
    std::vector< int > Listed() const;              //!< the picker's rows

    // typing in the search box or an instant chat's message
    void StopTyping( bool keep );

    std::vector< Doing > doings_[2];
    int tab_;
    int selected_;          // the selected key's sym, -1 for none
    int category_;          // the group listed
    std::string search_;
    int typing_;            // 0 nothing, -1 the search box, n an instant chat's message
    std::string typed_;     // the message being typed
    int clash_;             // a pick waiting for a yes: the doing, -1 for none
    REAL scroll_;
    REAL mouseX_, mouseY_;
    double noteAt_;
    std::string note_;
    bool iso_;
    bool hudWas_;           // whether the HUD was held before the page
    gKeyboardRow row_;

    // where things are this frame
    REAL k_, left_, top_;
    std::vector< Hit > hits_;
};

gKeyboardPage::gKeyboardPage()
    : uMenu( tOutput(), false ), tab_( 0 ), selected_( -1 ), category_( Group_Riding ), typing_( 0 ), clash_( -1 ),
      scroll_( 0 ), mouseX_( -1 ), mouseY_( -1 ), noteAt_( -100 ), iso_( sg_KeyboardLayoutIso() != 0 ), row_( this ),
      k_( 1 ), left_( 0 ), top_( 0 )
{
    // over the game, held back behind it; the HUD waits until the page is closed
    SetStyle( uMenuStyle_RclOverlay );
    hudWas_ = sg_hudHeld;
    sg_hudHeld = true;
    SetKeyHint( tString( " " ) );
    Build();
}

gKeyboardPage::~gKeyboardPage()
{
    sg_hudHeld = hudWas_;
}

// ---- what the keys can do, from the code ----

void gKeyboardPage::Build()
{
    for ( int tab = 0; tab < 2; ++tab )
        doings_[ tab ].clear();

    int const knownCount = int( sizeof( sg_known ) / sizeof( sg_known[0] ) );
    for ( int list = 0; list < 3; ++list )
        for ( uAction * action = su_ActionList( list ); action; action = action->Next() )
        {
            Doing doing;
            doing.action = action;
            doing.player = list == 2 ? 0 : 1;
            doing.spectator = -1;
            doing.chat = 0;
            std::string const name( static_cast< char const * >( action->internalName ) );
            doing.label = Said( action->description );
            doing.group = Group_Other;
            doing.order = 1000;
            doing.cap = Lower( name );
            for ( size_t i = 0; i < doing.cap.size(); ++i )
                if ( doing.cap[i] == '_' )
                    doing.cap[i] = ' ';
            for ( int i = 0; i < knownCount; ++i )
                if ( name == sg_known[i].name )
                {
                    doing.group = sg_known[i].group;
                    doing.cap = sg_known[i].cap;
                    doing.order = i;
                }
            if ( name.compare( 0, 13, "INSTANT_CHAT_" ) == 0 )
            {
                doing.group = Group_InstantChat;
                doing.chat = atoi( name.c_str() + 13 );
                doing.order = 100 + doing.chat;
                char cap[16];
                snprintf( cap, sizeof( cap ), "chat %d", doing.chat );
                doing.cap = doing.label = cap;
            }
            if ( doing.label.empty() )
                doing.label = doing.cap;
            doings_[0].push_back( doing );
        }

    for ( int key = 0; key < uSpectatorKey_Count; ++key )
    {
        uSpectatorKeyInfo const & info = su_SpectatorKeyInfo( static_cast< uSpectatorKey >( key ) );
        Doing doing;
        doing.action = 0;
        doing.player = 0;
        doing.spectator = key;
        doing.chat = 0;
        doing.group = key < uSpectatorKey_FlightCount ? Group_Flying : Group_Views;
        doing.order = key;
        doing.label = Said( tOutput( info.label ) );
        doing.cap = sg_spectatorCap[ key ];
        doings_[1].push_back( doing );
    }

    for ( int tab = 0; tab < 2; ++tab )
    {
        std::vector< Doing > & list = doings_[ tab ];
        for ( size_t i = 1; i < list.size(); ++i )
            for ( size_t j = i; j > 0; --j )
            {
                Doing const & a = list[ j - 1 ], & b = list[ j ];
                bool const after = a.group > b.group || ( a.group == b.group && ( a.order > b.order ||
                                   ( a.order == b.order && a.label > b.label ) ) );
                if ( !after )
                    break;
                std::swap( list[ j - 1 ], list[ j ] );
            }
    }
}

int gKeyboardPage::DoingOf( int tab, int sym ) const
{
    if ( sym < 0 || sym >= SDLK_NEWLAST )
        return -1;
    std::vector< Doing > const & list = doings_[ tab ];
    if ( tab == 0 )
    {
        uBind * bind = keymap[ sym ];
        if ( !bind || !bind->act )
            return -1;
        for ( size_t i = 0; i < list.size(); ++i )
            if ( list[i].action == bind->act && bind->CheckPlayer( list[i].player ) )
                return int( i );
        return -2;
    }
    // if the previous view shares the next view's key (Shift goes back), the key is shown as the next view
    for ( size_t i = 0; i < list.size(); ++i )
        if ( su_SpectatorKeySym( static_cast< uSpectatorKey >( list[i].spectator ) ) == sym &&
             !( list[i].spectator == uSpectatorKey_ViewPrev && su_SpectatorKeySym( uSpectatorKey_ViewNext ) == sym ) )
            return int( i );
    return -1;
}

std::string gKeyboardPage::KeysOf( int doing ) const
{
    std::string keys;
    std::vector< Doing > const & list = doings_[ tab_ ];
    if ( doing < 0 || doing >= int( list.size() ) )
        return keys;
    for ( int sym = 1; sym < SDLK_NEWLAST; ++sym )
    {
        bool on = false;
        if ( tab_ == 0 )
            on = keymap[ sym ] && keymap[ sym ]->act == list[ doing ].action && keymap[ sym ]->CheckPlayer( list[ doing ].player );
        else
            on = su_SpectatorKeySym( static_cast< uSpectatorKey >( list[ doing ].spectator ) ) == sym;
        if ( !on )
            continue;
        if ( !keys.empty() )
            keys += "  ";
        keys += Lower( su_KeyName( sym ) );
    }
    return keys;
}

void gKeyboardPage::Select( int sym )
{
    // a held key comes again and again
    if ( sym <= 0 || sym >= SDLK_NEWLAST || sym == selected_ )
        return;
    selected_ = sym;
    clash_ = -1;
    // the picker opens on what the key does
    int const doing = DoingOf( tab_, sym );
    if ( doing >= 0 )
    {
        category_ = doings_[ tab_ ][ doing ].group;
        search_.clear();
        scroll_ = 0;
    }
    uMenu::MenuSound( uMenu::Sound_Move );
}

void gKeyboardPage::Pick( int doing )
{
    if ( selected_ <= 0 )
    {
        note_ = "pick a key first: click it or press it";
        noteAt_ = tSysTimeFloat();
        return;
    }
    int const now = DoingOf( tab_, selected_ );
    // the key already does something else: ask first
    if ( now != -1 && now != doing && doing >= 0 )
    {
        clash_ = doing;
        return;
    }
    Apply( doing );
}

void gKeyboardPage::Apply( int doing )
{
    clash_ = -1;
    int const sym = selected_;
    if ( sym <= 0 )
        return;
    std::vector< Doing > const & list = doings_[ tab_ ];
    if ( tab_ == 0 )
    {
        keymap[ sym ] = NULL;
        if ( doing >= 0 && doing < int( list.size() ) )
            keymap[ sym ] = uBindPlayer::NewBind( list[ doing ].action, list[ doing ].player );
    }
    else
    {
        // a watcher's key is one key: the key's old job is left without one,
        // and the job leaves the key it had
        for ( int key = 0; key < uSpectatorKey_Count; ++key )
            if ( su_SpectatorKeySym( static_cast< uSpectatorKey >( key ) ) == sym &&
                 !( doing >= 0 && doing < int( list.size() ) && list[ doing ].spectator == uSpectatorKey_ViewNext && key == uSpectatorKey_ViewPrev ) )
                su_SetSpectatorKeySym( static_cast< uSpectatorKey >( key ), 0 );
        if ( doing >= 0 && doing < int( list.size() ) )
            su_SetSpectatorKeySym( static_cast< uSpectatorKey >( list[ doing ].spectator ), sym );
    }
    st_SaveConfig();
    note_ = "saved";
    noteAt_ = tSysTimeFloat();
    uMenu::MenuSound( uMenu::Sound_Adjust );
}

std::vector< int > gKeyboardPage::Listed() const
{
    std::vector< int > rows;
    std::vector< Doing > const & list = doings_[ tab_ ];
    std::string const wanted = Lower( search_ );
    for ( size_t i = 0; i < list.size(); ++i )
    {
        Doing const & doing = list[i];
        if ( !wanted.empty() )
        {
            std::string const name = doing.action ? Lower( std::string( static_cast< char const * >( doing.action->internalName ) ) ) : std::string();
            if ( Lower( doing.label ).find( wanted ) == std::string::npos && Lower( doing.cap ).find( wanted ) == std::string::npos &&
                 name.find( wanted ) == std::string::npos )
                continue;
        }
        else if ( doing.group != category_ )
            continue;
        rows.push_back( int( i ) );
    }
    return rows;
}

void gKeyboardPage::StopTyping( bool keep )
{
    if ( typing_ > 0 && keep )
    {
        ePlayer * player = ePlayer::PlayerConfig( 0 );
        if ( player && typing_ <= MAX_INSTANT_CHAT )
        {
            player->instantChatString[ typing_ - 1 ] = tString( typed_.c_str() );
            st_SaveConfig();
            note_ = "saved";
            noteAt_ = tSysTimeFloat();
        }
    }
    typing_ = 0;
    typed_.clear();
}

// ---- drawing ----

void gKeyboardPage::Measure()
{
    // the mock's panel, 1700 by 780, as large as the window lets it be
    REAL const w = LogicalWidth(), h = LogicalHeight();
    k_ = std::min( REAL( 1 ), std::min( ( w - 40 ) / 1700, ( h - 40 ) / 780 ) );
    left_ = ( w - 1700 * k_ ) * .5f;
    top_ = ( h - 780 * k_ ) * .5f;
}

void gKeyboardPage::Box( REAL l, REAL t, REAL r, REAL b, Color const & color, REAL alpha, REAL radius )
{
    FillRect( X( l ), Y( t ), X( r ), Y( b ), color, alpha, radius * k_ );
}

void gKeyboardPage::Outline( REAL l, REAL t, REAL r, REAL b, Color const & color, REAL alpha, int pixels )
{
    FrameRect( X( l ), Y( t ), X( r ), Y( b ), pixels, color, alpha );
}

void gKeyboardPage::Label( REAL x, REAL y, REAL size, int weight, Color const & color, REAL alpha, char const * text, int align, REAL most )
{
    Text( X( x ), Y( y ), size * k_, weight, color, alpha, text, align, most > 0 ? W( most ) : 0 );
}

void gKeyboardPage::AddHit( REAL l, REAL t, REAL r, REAL b, int spot, int value )
{
    Hit const hit = { l, t, r, b, spot, value };
    hits_.push_back( hit );
}

void gKeyboardPage::DrawKey( char const * legend, int sym, REAL l, REAL t, REAL r, REAL b, REAL alpha, REAL radius, bool centreLegend )
{
    int const doing = DoingOf( tab_, sym );
    bool const chosen = sym == selected_;
    bool const over = mouseX_ >= l && mouseX_ <= r && mouseY_ >= t && mouseY_ <= b;
    Box( l, t, r, b, over && !chosen ? sg_kbHover : sg_kbCard, alpha, radius );
    if ( chosen )
        Outline( l, t, r, b, accent, alpha, 3 );
    if ( legend && legend[0] )
    {
        bool const word = strlen( legend ) > 1;
        Color const & ink = doing != -1 || chosen ? textSecondary : sg_kbQuiet;
        if ( centreLegend )
            Label( ( l + r ) * .5f, t + S( 18 ), word ? 15 : 17, 600, ink, alpha, legend, 0 );
        else
            Label( l + S( 10 ), t + S( 18 ), word ? 15 : 19, 600, ink, alpha, legend );
    }
    if ( doing >= 0 )
    {
        Doing const & d = doings_[ tab_ ][ doing ];
        Box( l + S( 8 ), b - S( 14 ), r - S( 8 ), b - S( 8 ), GroupColor( d.group ), alpha, 3 );
        std::string cap = d.cap;
        // one key for both: the next view, and with Shift the one before
        if ( tab_ == 1 && d.spectator == uSpectatorKey_ViewNext && su_SpectatorKeySym( uSpectatorKey_ViewPrev ) == sym )
            cap = "next / prev";
        Label( ( l + r ) * .5f, b - S( 31 ), 15, 600, textPrimary, alpha, cap.c_str(), 0, r - l - S( 8 ) );
    }
    else if ( doing == -2 )
        Label( ( l + r ) * .5f, b - S( 31 ), 13, 500, sg_kbQuiet, alpha, "player 2+", 0 );
    AddHit( l, t, r, b, Spot_Key, sym );
}

void gKeyboardPage::DrawKeyboard( REAL alpha )
{
    Row const ( * rows )[16] = iso_ ? sg_iso : sg_ansi;
    REAL const unit = S( sg_unit ), gap = S( sg_gap );
    REAL y = PY( 290 );
    for ( int r = 0; r < 5; ++r )
    {
        REAL x = PX( 150 );
        for ( int c = 0; rows[r][c].legend; ++c )
        {
            Row const & key = rows[r][c];
            REAL const w = key.units * unit + ( key.units - 1 ) * gap;
            if ( r == 2 && key.legend[0] == '\0' )
            {
                // the lower half of the ISO enter key, joined to the upper
                Box( x, y - gap - S( 12 ), x + w, y + unit, sg_kbCard, alpha, 10 );
                if ( selected_ == key.sym )
                    Outline( x, y - gap, x + w, y + unit, accent, alpha, 3 );
                AddHit( x, y - gap, x + w, y + unit, Spot_Key, key.sym );
            }
            else
                DrawKey( key.legend, key.sym, x, y, x + w, y + unit, alpha );
            x += w + gap;
        }
        y += unit + gap;
    }

    // the arrows
    static char const * const arrow[4] = { "up", "left", "down", "right" };
    static int const arrowSym[4] = { SDLK_UP, SDLK_LEFT, SDLK_DOWN, SDLK_RIGHT };
    static int const arrowAt[4][2] = { { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } };
    for ( int i = 0; i < 4; ++i )
    {
        REAL const l = PX( 900 ) + arrowAt[i][0] * ( unit + gap ), t = PY( 712 ) + arrowAt[i][1] * ( unit + gap );
        DrawKey( arrow[i], arrowSym[i], l, t, l + unit, t + unit, alpha );
    }

    // the mouse: its buttons and its wheel
    static char const * const button[3] = { "L", "M", "R" };
    for ( int i = 0; i < 3; ++i )
    {
        REAL const l = PX( 1160 ) + i * S( 54 + sg_gap );
        DrawKey( button[i], SDLK_MOUSE_BUTTON_1 + i, l, PY( 712 ), l + S( 54 ), PY( 712 + 96 ), alpha, 12, true );
    }
    static char const * const wheel[2] = { "wheel up", "wheel down" };
    for ( int i = 0; i < 2; ++i )
    {
        REAL const t = PY( 712 + 106 + i * 44 );
        DrawKey( wheel[i], SDLK_MOUSE_BUTTON_4 + i, PX( 1160 ), t, PX( 1160 + 174 ), t + S( 38 ), alpha, 10 );
    }

    // what the colours are, and how the page works
    REAL lx = PX( 150 );
    int const first = tab_ == 0 ? Group_Riding : Group_Flying;
    int const last = tab_ == 0 ? Group_Game : Group_Views;
    for ( int g = first; g <= last; ++g )
    {
        if ( g == Group_InstantChat || g == Group_Other )
            continue;
        Box( lx, PY( 719 ), lx + S( 22 ), PY( 725 ), GroupColor( g ), alpha, 3 );
        Label( lx + S( 30 ), PY( 722 ), 16, 500, textSecondary, alpha, sg_groupName[g] );
        lx += S( 60 ) + TextWidth( sg_groupName[g], 16 * k_, 500 ) / W( 1 );
    }
    static char const * const notes[2][3] =
    {
        { "two keys can share an action, as your turns do; a key does one thing per tab.",
          "a clash is shown before it is saved. esc closes; nothing changes until you pick.",
          "the spectating tab has its own keys: flying, and views (ctrl + a view's key saves it)." },
        { "these keys work while you watch: flying the free camera, and your saved views.",
          0,
          "each of these is one key: picking one for a key moves it there. tab stays the scores." }
    };
    // the keys that go through the views, as they are set now
    std::string const through = "ctrl + a view's key saves the view the camera has; " +
                                Lower( su_SpectatorKeyName( uSpectatorKey_ViewNext ) ) + " and " +
                                Lower( su_SpectatorKeyName( uSpectatorKey_ViewPrev ) ) + " go through them.";
    for ( int i = 0; i < 3; ++i )
        Label( PX( 150 ), PY( 762 + 26 * i ), 16, 400, sg_kbQuiet, alpha, notes[ tab_ ][i] ? notes[ tab_ ][i] : through.c_str(), -1, S( 730 ) );
}

void gKeyboardPage::DrawPicker( REAL alpha )
{
    REAL const pl = PX( 1366 ), pt = PY( 284 ), pr = PX( 1366 + 424 ), pb = PY( 284 + 626 );
    Box( pl, pt, pr, pb, surfaceRaised, alpha, 16 );
    AddHit( pl, pt, pr, pb, Spot_Picker, 0 );
    std::vector< Doing > const & list = doings_[ tab_ ];

    // what the selected key does
    int const now = DoingOf( tab_, selected_ );
    if ( selected_ > 0 )
    {
        std::string const head = Lower( su_KeyName( selected_ ) ) + " does";
        Label( pl + S( 20 ), pt + S( 26 ), 16, 500, sg_kbQuiet, alpha, head.c_str(), -1, S( 190 ) );
        std::string what = now >= 0 ? list[ now ].label : now == -2 ? std::string( "another player's action" ) : std::string( "nothing yet" );
        Label( pr - S( 20 ), pt + S( 26 ), 16, 500, now >= 0 ? textPrimary : sg_kbQuiet, alpha, what.c_str(), 1, S( 200 ) );
    }
    else
        Label( pl + S( 20 ), pt + S( 26 ), 16, 500, sg_kbQuiet, alpha, "click a key or press it" );

    // the search box
    REAL const sl = pl + S( 20 ), st = pt + S( 46 ), sr = pr - S( 20 ), sb = pt + S( 86 );
    Box( sl, st, sr, sb, sg_kbField, alpha, 12 );
    if ( typing_ == -1 )
        Outline( sl, st, sr, sb, accent, alpha, 2 );
    if ( search_.empty() && typing_ != -1 )
        Label( sl + S( 16 ), ( st + sb ) * .5f, 16, 400, sg_kbDim, alpha, "search actions" );
    else
    {
        std::string shown = search_;
        if ( typing_ == -1 && fmod( tSysTimeFloat(), 1.0 ) < .55 )
            shown += "|";
        Label( sl + S( 16 ), ( st + sb ) * .5f, 16, 400, textPrimary, alpha, shown.c_str(), -1, sr - sl - S( 32 ) );
    }
    AddHit( sl, st, sr, sb, Spot_Search, 0 );

    // the groups, with how many each has (from the code)
    int counts[ Group_Count ] = { 0 };
    for ( size_t i = 0; i < list.size(); ++i )
        counts[ list[i].group ]++;
    REAL cx = sl, cy = pt + S( 100 );
    for ( int g = 0; g < Group_Count; ++g )
    {
        if ( g != Group_Nothing && counts[g] == 0 )
            continue;
        char label[48];
        if ( g == Group_Nothing )
            snprintf( label, sizeof( label ), "%s", sg_groupName[g] );
        else
            snprintf( label, sizeof( label ), "%s %d", sg_groupName[g], counts[g] );
        REAL const w = TextWidth( label, 15 * k_, 600 ) / W( 1 ) + S( 26 );
        if ( cx + w > sr )
        {
            cx = sl;
            cy += S( 38 );
        }
        bool const on = search_.empty() && g == category_;
        Box( cx, cy, cx + w, cy + S( 30 ), on ? accent : sg_kbCard, alpha, 15 );
        Label( cx + S( 13 ), cy + S( 15 ), 15, 600, on ? onAccent : textPrimary, alpha, label );
        AddHit( cx, cy, cx + w, cy + S( 30 ), Spot_Chip, g );
        cx += w + S( 8 );
    }

    // the rows
    REAL y = cy + S( 50 );
    char const * hint = 0;
    if ( !search_.empty() )
        hint = "everything that matches";
    else if ( category_ == Group_InstantChat )
        hint = "the message is sent when the key is pressed";
    else if ( category_ == Group_Views )
        hint = "ctrl + a view's key saves the view the camera has";
    else if ( category_ == Group_Flying )
        hint = "each is one key; picking it here moves it to the selected key";
    if ( hint )
    {
        Label( sl, y + S( 8 ), 14, 400, sg_kbQuiet, alpha, hint, -1, sr - sl );
        y += S( 28 );
    }

    REAL const listTop = y, listBottom = pb - S( 16 );
    std::vector< int > rows;
    if ( search_.empty() && category_ == Group_Nothing )
        rows.push_back( -1 );
    else
        rows = Listed();
    REAL const pitch = S( 52 );
    REAL const most = std::max( REAL( 0 ), rows.size() * pitch - ( listBottom - listTop ) );
    scroll_ = std::max( REAL( 0 ), std::min( scroll_, most / k_ ) );
    y -= S( scroll_ );
    ePlayer const * const player = ePlayer::PlayerConfig( 0 );
    for ( size_t i = 0; i < rows.size(); ++i, y += pitch )
    {
        REAL const rt = y, rb = y + S( 44 );
        if ( rt < listTop - S( 2 ) || rb > listBottom + S( 2 ) )
            continue;
        int const doing = rows[i];
        bool const current = doing >= 0 ? doing == now : now == -1 && selected_ > 0;
        Box( sl, rt, sr, rb, sg_kbCard, alpha, 12 );
        if ( current )
            Outline( sl, rt, sr, rb, accent, alpha, 2 );
        AddHit( sl, rt, sr, rb, Spot_Row, doing );
        if ( doing < 0 )
        {
            Label( sl + S( 14 ), ( rt + rb ) * .5f, 16, 600, textPrimary, alpha, "nothing" );
            Label( sr - S( 14 ), ( rt + rb ) * .5f, 13, 400, sg_kbQuiet, alpha, "frees the key", 1 );
            continue;
        }
        Doing const & d = list[ doing ];
        std::string const keys = KeysOf( doing );
        if ( d.chat > 0 )
        {
            // the slot, the key it is on, and its message to edit
            Label( sl + S( 14 ), ( rt + rb ) * .5f, 16, 600, textPrimary, alpha, d.label.c_str() );
            Label( sl + S( 150 ), ( rt + rb ) * .5f, 13, 400, sg_kbQuiet, alpha, keys.empty() ? "no key" : keys.c_str(), 1, S( 70 ) );
            REAL const fl = sl + S( 158 ), fr = sr - S( 10 );
            Box( fl, rt + S( 8 ), fr, rb - S( 8 ), sg_kbField, alpha, 8 );
            std::string message;
            if ( typing_ == d.chat )
            {
                message = typed_;
                if ( fmod( tSysTimeFloat(), 1.0 ) < .55 )
                    message += "|";
                Outline( fl, rt + S( 8 ), fr, rb - S( 8 ), accent, alpha, 2 );
            }
            else if ( player && d.chat <= MAX_INSTANT_CHAT )
                message = static_cast< char const * >( player->instantChatString[ d.chat - 1 ] );
            if ( message.empty() )
                Label( fl + S( 12 ), ( rt + rb ) * .5f, 15, 400, sg_kbDim, alpha, "type its message" );
            else
                Label( fl + S( 12 ), ( rt + rb ) * .5f, 15, 400, textPrimary, alpha, message.c_str(), -1, fr - fl - S( 24 ) );
            AddHit( fl, rt, fr, rb, Spot_Message, d.chat );
            continue;
        }
        Label( sl + S( 14 ), ( rt + rb ) * .5f, 16, 600, textPrimary, alpha, d.label.c_str(), -1, S( 220 ) );
        Label( sr - S( 14 ), ( rt + rb ) * .5f, 13, 400, sg_kbQuiet, alpha, keys.c_str(), 1, S( 120 ) );
    }

    // a pick that would replace what the key does: asked first
    if ( clash_ >= 0 && clash_ < int( list.size() ) && selected_ > 0 && now != -1 )
    {
        REAL const ct = pb - S( 150 ), cb = pb - S( 16 );
        Box( sl - S( 4 ), ct, sr + S( 4 ), cb, surface, alpha, 12 );
        Outline( sl - S( 4 ), ct, sr + S( 4 ), cb, accent, alpha, 2 );
        std::string const was = now >= 0 ? list[ now ].label : std::string( "another player's action" );
        std::string const line = Lower( su_KeyName( selected_ ) ) + " does " + Lower( was ) + "; replace?";
        Label( sl + S( 12 ), ct + S( 30 ), 16, 600, textPrimary, alpha, line.c_str(), -1, sr - sl - S( 16 ) );
        std::string const then = "it would " + Lower( list[ clash_ ].label ) + " instead";
        Label( sl + S( 12 ), ct + S( 58 ), 14, 400, textSecondary, alpha, then.c_str(), -1, sr - sl - S( 16 ) );
        REAL const bt = cb - S( 50 ), bb = cb - S( 14 );
        Box( sl + S( 12 ), bt, sl + S( 140 ), bb, accent, alpha, 18 );
        Label( sl + S( 76 ), ( bt + bb ) * .5f, 15, 600, onAccent, alpha, "replace", 0 );
        AddHit( sl + S( 12 ), bt, sl + S( 140 ), bb, Spot_Replace, 0 );
        Box( sl + S( 150 ), bt, sl + S( 270 ), bb, sg_kbCard, alpha, 18 );
        Label( sl + S( 210 ), ( bt + bb ) * .5f, 15, 600, textPrimary, alpha, "cancel", 0 );
        AddHit( sl + S( 150 ), bt, sl + S( 270 ), bb, Spot_Cancel, 0 );
    }
}

void gKeyboardPage::RenderExtras( REAL alpha )
{
    Measure();
    hits_.clear();
    sr_ResetRenderState( true );

    Box( PX( 110 ), PY( 150 ), PX( 1810 ), PY( 930 ), surface, alpha * REAL( .97 ), 22 );
    Label( PX( 150 ), PY( 205 ), 40, 600, accent, alpha, "/kb" );
    Label( PX( 232 ), PY( 205 ), 22, 400, textSecondary, alpha, "click a key or press it, then pick what it does" );

    // the tabs
    static char const * const tabName[2] = { "playing", "spectating" };
    REAL tx = PX( 1770 );
    for ( int tab = 1; tab >= 0; --tab )
    {
        REAL const w = TextWidth( tabName[ tab ], 18 * k_, 600 ) / W( 1 ) + S( 36 );
        bool const on = tab == tab_;
        Box( tx - w, PY( 184 ), tx, PY( 222 ), on ? accent : surfaceRaised, alpha, 19 );
        Label( tx - w * .5f, PY( 203 ), 18, 600, on ? onAccent : textPrimary, alpha, tabName[ tab ], 0 );
        AddHit( tx - w, PY( 184 ), tx, PY( 222 ), Spot_Tab, tab );
        tx -= w + S( 10 );
    }

    DrawKeyboard( alpha );
    DrawPicker( alpha );

    // a short word on what just happened
    double const age = tSysTimeFloat() - noteAt_;
    if ( !note_.empty() && age >= 0 && age < 1.6 )
    {
        REAL const fade = age < 1.2 ? 1 : REAL( 1 - ( age - 1.2 ) / .4 );
        Label( PX( 1770 ), PY( 245 ), 15, 500, accent, alpha * fade, note_.c_str(), 1 );
    }
    Label( PX( 1770 ), PY( 920 ), 14, 400, sg_kbDim, alpha, "esc closes", 1 );
}

// ---- events ----

void gKeyboardPage::HandleEvent( SDL_Event event )
{
    REAL const scale = Scale();
    switch ( event.type )
    {
    case SDL_MOUSEMOTION:
        mouseX_ = event.motion.x / scale;
        mouseY_ = event.motion.y / scale;
        return;

    case SDL_KEYDOWN:
    {
        SDL_keysym const & key = event.key.keysym;

        // typing: the keys go to the text
        if ( typing_ != 0 )
        {
            std::string & text = typing_ == -1 ? search_ : typed_;
            if ( key.sym == SDLK_ESCAPE )
            {
                if ( typing_ == -1 )
                    search_.clear();
                StopTyping( false );
            }
            else if ( key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER )
                StopTyping( true );
            else if ( key.sym == SDLK_BACKSPACE )
            {
                if ( !text.empty() )
                    text.erase( text.size() - 1 );
            }
            else if ( key.unicode >= 32 && key.unicode < 256 && text.size() < 180 )
                text += char( key.unicode );
            if ( typing_ == -1 )
                scroll_ = 0;
            return;
        }

        // a pick waiting for a yes
        if ( clash_ >= 0 )
        {
            if ( key.sym == SDLK_RETURN || key.sym == SDLK_KP_ENTER || key.sym == SDLK_y )
            {
                Apply( clash_ );
                return;
            }
            if ( key.sym == SDLK_ESCAPE || key.sym == SDLK_n )
            {
                clash_ = -1;
                return;
            }
        }

        if ( key.sym == SDLK_ESCAPE )
        {
            Exit();
            uMenu::MenuSound( uMenu::Sound_Back );
            return;
        }

        // the screenshot key still takes a picture, and only that: a picture
        // of the page leaves what is selected as it was (click the key to
        // change what it does)
        if ( key.sym > 0 && key.sym < SDLK_NEWLAST && keymap[ key.sym ] && keymap[ key.sym ]->act &&
             std::string( static_cast< char const * >( keymap[ key.sym ]->act->internalName ) ) == "SCREENSHOT" )
        {
            su_HandleEvent( event, false );
            return;
        }

        // any other key is the key picked
        Select( key.sym );
        return;
    }

    case SDL_MOUSEBUTTONDOWN:
    {
        REAL const x = event.button.x / scale, y = event.button.y / scale;
        mouseX_ = x;
        mouseY_ = y;
        Hit const * hit = 0;
        bool inPicker = false;
        for ( size_t i = hits_.size(); i-- > 0; )
            if ( x >= hits_[i].left && x <= hits_[i].right && y >= hits_[i].top && y <= hits_[i].bottom )
            {
                if ( hits_[i].spot == Spot_Picker )
                {
                    inPicker = true;
                    continue;
                }
                if ( !hit )
                    hit = &hits_[i];
            }

        // the wheel: scrolls the picker over it, else it is the key picked
        if ( event.button.button == SDL_BUTTON_WHEELUP || event.button.button == SDL_BUTTON_WHEELDOWN )
        {
            if ( inPicker )
                scroll_ += event.button.button == SDL_BUTTON_WHEELUP ? -52 : 52;
            else
                Select( event.button.button == SDL_BUTTON_WHEELUP ? SDLK_MOUSE_BUTTON_4 : SDLK_MOUSE_BUTTON_5 );
            return;
        }
        // the middle and right buttons pressed outside the picker are those keys
        if ( event.button.button != SDL_BUTTON_LEFT )
        {
            if ( !inPicker && event.button.button >= 1 && event.button.button <= MOUSE_BUTTONS )
                Select( SDLK_MOUSE_BUTTON_1 + event.button.button - 1 );
            return;
        }

        if ( typing_ != 0 && !( hit && ( ( hit->spot == Spot_Search && typing_ == -1 ) || ( hit->spot == Spot_Message && hit->value == typing_ ) ) ) )
            StopTyping( true );
        if ( !hit )
            return;
        switch ( hit->spot )
        {
        case Spot_Tab:
            if ( tab_ != hit->value )
            {
                tab_ = hit->value;
                category_ = tab_ == 0 ? Group_Riding : Group_Flying;
                search_.clear();
                scroll_ = 0;
                clash_ = -1;
                uMenu::MenuSound( uMenu::Sound_Move );
            }
            break;
        case Spot_Key:
            Select( hit->value );
            break;
        case Spot_Chip:
            category_ = hit->value;
            search_.clear();
            scroll_ = 0;
            uMenu::MenuSound( uMenu::Sound_Move );
            break;
        case Spot_Row:
            Pick( hit->value );
            break;
        case Spot_Search:
            typing_ = -1;
            break;
        case Spot_Message:
            if ( typing_ != hit->value )
            {
                ePlayer const * const player = ePlayer::PlayerConfig( 0 );
                typing_ = hit->value;
                typed_ = player && typing_ <= MAX_INSTANT_CHAT ? static_cast< char const * >( player->instantChatString[ typing_ - 1 ] ) : "";
            }
            break;
        case Spot_Replace:
            if ( clash_ >= 0 )
                Apply( clash_ );
            break;
        case Spot_Cancel:
            clash_ = -1;
            break;
        default:
            break;
        }
        return;
    }

    default:
        return;
    }
}
}

void sg_KeyboardPage()
{
    gKeyboardPage page;
    page.Enter();
}

// what /kb in chat opens (ePlayer.cpp)
static struct gKeyboardPageHook
{
    gKeyboardPageHook(){ se_keyboardPage = &sg_KeyboardPage; }
} sg_keyboardPageHook;

// The physical layout from the keyboard layout Windows has for the game:
// the UK's is ISO, anything else is drawn as US ANSI.
#ifdef WIN32
#include <windows.h>
int sg_KeyboardLayoutIso()
{
    LANGID const language = LANGID( reinterpret_cast< uintptr_t >( GetKeyboardLayout( 0 ) ) & 0xFFFF );
    return language == 0x0809 || language == 0x1809 ? 1 : 0;
}
#else
int sg_KeyboardLayoutIso(){ return 0; }
#endif

#endif
