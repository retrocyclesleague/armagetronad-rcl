/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

***************************************************************************

*/

#include "gServerBrowser.h"
#include "gGame.h"
#include "gLogo.h"
#include "gServerFavorites.h"
#include "gFriends.h"

#include "nServerInfo.h"
#include "nNetwork.h"

#include "rSysdep.h"
#include "rScreen.h"
#include "rConsole.h"
#include "rRender.h"

#include "uMenu.h"
#include "uInputQueue.h"
#include "uRclTheme.h"

#include "tMemManager.h"
#include "tSysTime.h"
#include "tToDo.h"

#include "tDirectories.h"
#include "tConfiguration.h"

#include <vector>
#include <algorithm>
#include <string>
#include <string.h>
#include <ctype.h>

int gServerBrowser::lowPort  = 4534;

int gServerBrowser::highPort = 4540;
static bool continuePoll = false;
static int sg_simultaneous = 20;
static tSettingItem< int > sg_simultaneousConf( "BROWSER_QUERIES_SIMULTANEOUS", sg_simultaneous );

static tOutput *sg_StartHelpText = NULL;

nServerInfo::QueryType sg_queryType = nServerInfo::QUERY_OPTOUT;
tCONFIG_ENUM( nServerInfo::QueryType );
static tSettingItem< nServerInfo::QueryType > sg_query_type( "BROWSER_QUERY_FILTER", sg_queryType );

class gServerMenuItem;


class gServerInfo: public nServerInfo
{
public:
    gServerMenuItem *menuItem;
	bool show; //for server browser hiding

    gServerInfo():menuItem(NULL), show(true)
    {
    }

    virtual ~gServerInfo();

    // during browsing, the whole server list consists of gServerInfos
    static gServerInfo * GetFirstServer()
    {
        return dynamic_cast< gServerInfo * >( nServerInfo::GetFirstServer() );
    }

    gServerInfo * Next()
    {
        return dynamic_cast< gServerInfo * >( nServerInfo::Next() );
    }
};

nServerInfo* CreateGServer()
{
    nServerInfo *ret = tNEW(gServerInfo);

    //if (!continuePoll)
    //{
    //    nServerInfo::StartQueryAll( sg_queryType );
    //    continuePoll = true;
    // }

    return ret;
}


// ---- the RCL browser: who is playing where ----
// Servers with players come first, the empty ones after them, nearest first.
// They are shown as cards, three in a row, or as a list; the order, two
// filters and the view are chips beside the search field, and are kept.
enum { gBrowserView_Cards = 0, gBrowserView_List = 1 };
enum { gBrowserSort_Players = 0, gBrowserSort_Ping = 1, gBrowserSort_Name = 2, gBrowserSort_Count = 3 };
enum { gBrowserCell_Row = 0, gBrowserCell_Section, gBrowserCell_Card, gBrowserCell_Compact, gBrowserCell_Line };

static int sg_browserView = gBrowserView_Cards;
static tConfItem< int > sg_browserViewConf( "SERVER_BROWSER_VIEW", sg_browserView );
static int sg_browserSort = gBrowserSort_Players;
static tConfItem< int > sg_browserSortConf( "SERVER_BROWSER_SORT", sg_browserSort );
static bool sg_browserHideEmpty = false;
static tConfItem< bool > sg_browserHideEmptyConf( "SERVER_BROWSER_HIDE_EMPTY", sg_browserHideEmpty );
static bool sg_browserRclOnly = false;
static tConfItem< bool > sg_browserRclOnlyConf( "SERVER_BROWSER_RCL_ONLY", sg_browserRclOnly );

//! where a row of the list lies and what it is
struct gBrowserSlot
{
    REAL top, height;   // from the top of the list's content, and how tall, in field units
    REAL left, right;   // its edges; rows that share a top stand side by side
    int row, col;       // for the arrow keys
    int cell;           // a gBrowserCell
};

class gServerMenu: public uMenu
{
    int sortKey_;
    std::vector< gBrowserSlot > slots_;     //!< one for every item, by its index
    int livePlayers_, liveServers_, emptyServers_;
#ifndef DEDICATED
    bool headerDrawn_;

    void RenderColumnHeader( REAL alpha );
    void EnsureColumnHeader( REAL alpha );
#endif

public:
    virtual void OnRender();
    tString filter_string;

    virtual REAL YPos(int num);
    void Layout();                           //!< places every row
    bool Step( int rows, int cols );         //!< moves the selection over the rows and columns; false if there is nothing that way
    void Changed( int chip );                //!< one of the chips was used
#ifndef DEDICATED
    virtual bool ItemContainsX(int itemIndex, REAL mouseX);
    void RenderServer( int index, gServerInfo * server, bool favorite, REAL y, REAL alpha, bool selected );
    void RenderSection( int index, REAL y, REAL alpha, char const * label );
    void RenderChips( REAL y, REAL alpha );
    bool ClickChip( REAL mouseX );
#endif

    void Update(); // sort the server view by score
    gServerMenu(const char *title);
    ~gServerMenu();

    virtual void HandleEvent( SDL_Event event );

#ifndef DEDICATED
    virtual REAL ItemDrawY(int itemIndex);
    virtual REAL ItemRowHalf(int itemIndex);
#endif

    void Render(REAL y,
                const tString &servername, const tOutput &score,
                const tOutput &users     , const tOutput &ping,
                bool selected = false, REAL alpha = 1);

    void Render(REAL y,
                const tString &servername, const tString &score,
                const tString &users     , const tString &ping,
                bool selected = false, REAL alpha = 1);

    void RenderDetails( gServerInfo *server, REAL alpha );
    
    void Render(REAL x, REAL y, tString &c, int center, int cursor,
                int cursorPos, REAL alpha, REAL right);
};


class gBrowserMenuItem: public uMenuItem
{
protected:
    bool displayHelp_;
    REAL helpAlpha_;

    gBrowserMenuItem(uMenu *M,const tOutput &help): uMenuItem( M, help )
      , displayHelp_{false}
      , helpAlpha_(0.0f)
    {
    }

    // handles a key press
    virtual bool Event( SDL_Event& event );

    virtual void RenderBackground();

    virtual bool DisplayHelp( bool display, REAL y, REAL alpha )
    {
        helpAlpha_ = alpha;
        displayHelp_ = display;
        return false;
    }
};

//! a heading between two stretches of the list (or, without words, room under its end); never selected
class gBrowserSectionItem: public uMenuItem
{
    tString label_;
public:
    gBrowserSectionItem( gServerMenu * men, char const * label )
        :uMenuItem( men, "" ), label_( label )
    {}

    bool Spacer() const { return label_.Len() <= 1; }

    virtual bool IsSelectable(){ return false; }
    virtual bool OwnsRow(){ return true; }
    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0)
    {
#ifndef DEDICATED
        (void)x; (void)selected;
        static_cast< gServerMenu * >( menu )->RenderSection( GetID(), y, alpha, label_ );
#endif
    }
};

class gServerFilterMenuItem: public uMenuItemString
{
public:
    gServerFilterMenuItem(gServerMenu *M)
        :uMenuItemString(M,"$network_master_filter","",M->filter_string)
    {}
        
    virtual ~gServerFilterMenuItem(){}
    
    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0);
    virtual void RenderBackground(){
        menu->Item(menu->NumItems()-2)->RenderBackground();
    };
#ifndef DEDICATED
    virtual bool Click( REAL mouseX )
    {
        return static_cast< gServerMenu * >( menu )->ClickChip( mouseX );
    }
#endif
    virtual bool Event( SDL_Event& event );
    
    
private:
    tString prev_filter_string;
};

class gServerMenuItem: public gBrowserMenuItem
{
protected:
    gServerInfo *server;
    double      lastPing_; //!< the time of the last manual ping
    bool        favorite_; //!< flag indicating whether this is a favorite
    int         cell_;     //!< how it is drawn: a gBrowserCell
public:
    int Cell() const { return cell_; }
    void SetCell( int cell ) { cell_ = cell; }
    virtual bool OwnsRow(){ return menu->RclStyle(); }
    void AddFavorite();
    void SetServer(nServerInfo *s);
    gServerInfo *GetServer();

    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0);
    virtual void RenderBackground();

    virtual void Enter();

    // handles a key press
    virtual bool Event( SDL_Event& event );

    gServerMenuItem(gServerMenu *men);
    virtual ~gServerMenuItem();
};

class gServerStartMenuItem: public gBrowserMenuItem
{
public:
    virtual void Render(REAL x,REAL y,REAL alpha=1, bool selected=0);

    virtual void Enter();

    gServerStartMenuItem(gServerMenu *men);
    virtual ~gServerStartMenuItem();
};






static bool sg_RequestLANcontinuously = false;

void gServerBrowser::BrowseMaster()
{
    BrowseSpecialMaster(0,"");
}

// the currently active master
static nServerInfoBase * sg_currentMaster = 0;
nServerInfoBase * gServerBrowser::CurrentMaster()
{
    return sg_currentMaster;
}


void gServerBrowser::BrowseSpecialMaster( nServerInfoBase * master, char const * prefix )
{
    sg_currentMaster = master;

    sg_RequestLANcontinuously = false;

    sn_ServerInfoCreator *cback = nServerInfo::SetCreator(&CreateGServer);

    sr_con.autoDisplayAtNewline=true;
    sr_con.fullscreen=true;

#ifndef DEDICATED
    rSysDep::SwapGL();
    rSysDep::ClearGL();
    rSysDep::SwapGL();
    rSysDep::ClearGL();
#endif

    bool to=sr_textOut;
    sr_textOut=true;

    nServerInfo::DeleteAll();
    nServerInfo::GetFromMaster( master, prefix );
    nServerInfo::Save();

    //  gLogo::SetBig(true);
    //  gLogo::SetSpinning(false);

    sr_textOut = to;

    tOutput StartHelpTextInternet("$network_master_host_inet_help");
    sg_StartHelpText = &StartHelpTextInternet;
    sg_TalkToMaster = true;

    BrowseServers();

    nServerInfo::Save();

    sg_TalkToMaster = false;

    nServerInfo::SetCreator(cback);

    sg_currentMaster = master;
}

void gServerBrowser::BrowseLAN()
{
    // TODO: reacivate and see what happens. Done.
    sg_RequestLANcontinuously = true;
    //	sg_RequestLANcontinuously = false;

    sn_ServerInfoCreator *cback = nServerInfo::SetCreator(&CreateGServer);

    sr_con.autoDisplayAtNewline=true;
    sr_con.fullscreen=true;

#ifndef DEDICATED
    rSysDep::SwapGL();
    rSysDep::ClearGL();
    rSysDep::SwapGL();
    rSysDep::ClearGL();
#endif

    bool to=sr_textOut;
    sr_textOut=true;

    nServerInfo::DeleteAll();
    nServerInfo::GetFromLAN(lowPort, highPort);

    sr_textOut = to;

    tOutput StartHelpTextLAN("$network_master_host_lan_help");
    sg_StartHelpText = &StartHelpTextLAN;
    sg_TalkToMaster = false;

    BrowseServers();

    nServerInfo::SetCreator(cback);
}

void gServerBrowser::BrowseServers()
{
    //nServerInfo::CalcScoreAll();
    //nServerInfo::Sort();
    nServerInfo::StartQueryAll( sg_queryType );
    continuePoll = true;

    gServerMenu browser("server browser");

    gServerStartMenuItem start(&browser);
    gServerFilterMenuItem filter(&browser);

    /*
      while (nServerInfo::DoQueryAll(sg_simultaneous));
      sn_SetNetState(nSTANDALONE);
      nServerInfo::Sort();

      if (nServerInfo::GetFirstServer())
      ConnectToServer(nServerInfo::GetFirstServer());
    */
    browser.Update();

    // eat excess input the user made while the list was fetched
    SDL_Event ignore;
    REAL time;
    while(su_GetSDLInput(ignore, time)) ;

    browser.Enter();

    nServerInfo::GetFromLANContinuouslyStop();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(true);
}





void gServerMenu::Changed( int chip )
{
    switch ( chip )
    {
    case 0: case 1: case 2:
        sg_browserSort = chip;
        break;
    case 3:
        sg_browserRclOnly = !sg_browserRclOnly;
        break;
    case 4:
        sg_browserHideEmpty = !sg_browserHideEmpty;
        break;
    case 5:
        sg_browserView = gBrowserView_Cards;
        break;
    case 6:
        sg_browserView = gBrowserView_List;
        break;
    default:
        return;
    }
    Update();
}

bool gServerMenu::Step( int rows, int cols )
{
    if ( selected < 0 || selected >= int( slots_.size() ) || int( slots_.size() ) != items.Len() )
        return false;
    gBrowserSlot const & at = slots_[ selected ];

    int best = -1;
    if ( cols )
    {
        for ( int i = items.Len()-1; i >= 0; --i )
            if ( slots_[i].row == at.row && slots_[i].col == at.col + cols && items(i)->IsSelectable() )
                best = i;
    }
    else
    {
        // the nearest row that way with something to select, and in it the column nearest this one
        int nearRow = 1 << 30, nearCol = 1 << 30;
        for ( int i = items.Len()-1; i >= 0; --i )
        {
            if ( !items(i)->IsSelectable() )
                continue;
            int const away = ( slots_[i].row - at.row ) * rows;
            if ( away <= 0 )
                continue;
            int const aside = slots_[i].col > at.col ? slots_[i].col - at.col : at.col - slots_[i].col;
            if ( away < nearRow || ( away == nearRow && aside < nearCol ) )
            {
                best = i;
                nearRow = away;
                nearCol = aside;
            }
        }
    }
    if ( best < 0 )
        return false;

    selected = best;
#ifndef DEDICATED
    mouseSelection_ = -1;
#endif
    MenuSound( Sound_Move );
    return true;
}

void gServerMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    // on the search row, keys are what is searched for
    bool const searching = items.Len() - selected == 1;
    if( searching && filter_string.Len() > 1 )
    {
        return uMenu::HandleEvent( event );
    }

    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case(SDLK_TAB):
            Changed( ( sg_browserSort + 1 ) % gBrowserSort_Count );
            return;
        case(SDLK_LEFT):
        case(SDLK_RIGHT):
            // to the card beside this one; where there is none, the order changes as it always did
            if ( !Step( 0, event.key.keysym.sym == SDLK_RIGHT ? 1 : -1 ) )
                Changed( ( sg_browserSort + ( event.key.keysym.sym == SDLK_RIGHT ? 1 : gBrowserSort_Count - 1 ) ) % gBrowserSort_Count );
            return;
        case(SDLK_UP):
        case(SDLK_DOWN):
            if ( Step( event.key.keysym.sym == SDLK_DOWN ? 1 : -1, 0 ) )
                return;
            break;
        case(SDLK_v):
            if ( searching ) break;
            Changed( sg_browserView == gBrowserView_Cards ? 6 : 5 );
            return;
        case(SDLK_e):
            if ( searching ) break;
            Changed( 4 );
            return;
        case(SDLK_o):
            if ( searching ) break;
            Changed( 3 );
            return;
		case(SDLK_m):
            if( searching ) break;
			FriendsToggle();
            Update();
			return;
			break;
        case(SDLK_HOME):
            SetSelected(NumItems() - 1);
            Update();
            return;
        case(SDLK_END):
            SetSelected(0);
            Update();
            return;
        default:
            break;
        }
    }
#endif

    uMenu::HandleEvent( event );
}

void gServerMenu::OnRender()
{
    uMenu::OnRender();

#ifndef DEDICATED
    headerDrawn_ = false;
#endif

    // next time the server list is to be resorted
    static double sg_serverMenuRefreshTimeout=-1E+32f;

    if (sg_serverMenuRefreshTimeout < tSysTimeFloat())
    {
        Update();
        sg_serverMenuRefreshTimeout = tSysTimeFloat()+2.0f;
    }

    // the rows are measured in the window's own units, so they are placed every frame
    Layout();

#ifndef DEDICATED
    // A card is taller than the room the menu keeps for a row: the one the
    // keys chose is brought into the list whole.
    if ( RclStyle() && mouseSelection_ != selected && selected >= 0 && selected < int( slots_.size() ) &&
         slots_[ selected ].height > uRclTheme::RowPitch() )
    {
        REAL const margin = uRclTheme::H( 10 );
        REAL const y = YPos( selected ), half = slots_[ selected ].height * .5f;
        if ( y - half < menuBot + margin )
            yOffset += menuBot + margin - ( y - half );
        else if ( y + half > menuTop - margin )
            yOffset -= ( y + half ) - ( menuTop - margin );
    }
#endif
}

// ---- the order of the list ----
namespace
{
bool sg_BrowserIsRcl( gServerInfo * server )
{
#ifndef DEDICATED
    tString kind, mode, region;
    return server && sg_RclServerFacts( server->GetConnectionName(), server->GetPort(), kind, mode, region );
#else
    (void)server;
    return false;
#endif
}

struct gBrowserOrder
{
    int key_;
    explicit gBrowserOrder( int key ): key_( key ) {}

    bool operator()( gServerInfo * a, gServerInfo * b ) const
    {
        // those that answer before those that do not
        if ( a->Reachable() != b->Reachable() )
            return a->Reachable();
        if ( key_ == gBrowserSort_Players && a->Users() != b->Users() )
            return a->Users() > b->Users();
        if ( key_ == gBrowserSort_Name )
        {
            std::string an( static_cast< char const * >( tColoredString::RemoveColors( a->GetName(), false ) ) );
            std::string bn( static_cast< char const * >( tColoredString::RemoveColors( b->GetName(), false ) ) );
            std::transform( an.begin(), an.end(), an.begin(), ::tolower );
            std::transform( bn.begin(), bn.end(), bn.begin(), ::tolower );
            if ( an != bn )
                return an < bn;
        }
        return a->Ping() < b->Ping();
    }
};
}

// logical pixels
static REAL const sg_cardHeight    = 118;
static REAL const sg_cardRowGap    = 12;
static REAL const sg_cardGap       = 14;
static REAL const sg_compactHeight = 28;
static REAL const sg_sectionHeight = 32;
static REAL const sg_spacerHeight  = 14;

void gServerMenu::Layout()
{
#ifndef DEDICATED
    int const count = items.Len();
    slots_.resize( count );
    if ( !RclStyle() )
        return;

    REAL const pitch = uRclTheme::RowPitch();
    REAL const left = uRclTheme::RowLeft(), right = uRclTheme::RowRight();
    REAL const gap = uRclTheme::W( sg_cardGap );
    REAL const third = ( right - left - 2 * gap ) / 3;

    REAL cursor = 0;
    int row = 0;
    for ( int i = count - 1; i >= 0; )
    {
        gServerMenuItem * const server = dynamic_cast< gServerMenuItem * >( items(i) );
        gBrowserSectionItem * const section = dynamic_cast< gBrowserSectionItem * >( items(i) );
        int const cell = server && server->GetServer() ? server->Cell()
                         : section ? int( gBrowserCell_Section ) : int( gBrowserCell_Row );

        if ( cell == gBrowserCell_Card || cell == gBrowserCell_Compact )
        {
            // up to three side by side
            REAL const height = uRclTheme::H( cell == gBrowserCell_Card ? sg_cardHeight : sg_compactHeight );
            for ( int col = 0; col < 3 && i >= 0; ++col )
            {
                gServerMenuItem * const next = dynamic_cast< gServerMenuItem * >( items(i) );
                if ( !next || !next->GetServer() || next->Cell() != cell )
                    break;
                gBrowserSlot & slot = slots_[i];
                slot.top = cursor;
                slot.height = height;
                slot.left = left + col * ( third + gap );
                slot.right = slot.left + third;
                slot.row = row;
                slot.col = col;
                slot.cell = cell;
                --i;
            }
            cursor += height + ( cell == gBrowserCell_Card ? uRclTheme::H( sg_cardRowGap ) : 0 );
            ++row;
            continue;
        }

        gBrowserSlot & slot = slots_[i];
        slot.top = cursor;
        slot.height = section ? uRclTheme::H( section->Spacer() ? sg_spacerHeight : sg_sectionHeight ) : pitch;
        slot.left = left;
        slot.right = right;
        slot.row = row;
        slot.col = 0;
        slot.cell = server ? int( gBrowserCell_Line ) : cell;
        cursor += slot.height;
        ++row;
        --i;
    }
#endif
}

REAL gServerMenu::YPos( int num )
{
#ifndef DEDICATED
    if ( RclStyle() && num >= 0 && num < int( slots_.size() ) && int( slots_.size() ) == items.Len() )
    {
        gBrowserSlot const & slot = slots_[ num ];
        return yOffset - uRclTheme::RowPitch() * .5f - slot.top - slot.height * .5f;
    }
#endif
    return uMenu::YPos( num );
}

void gServerMenu::Update()
{
    // get currently selected server
    gServerMenuItem *item = NULL;
    if ( selected < items.Len() )
    {
        item = dynamic_cast<gServerMenuItem*>(items(selected));
    }
    gServerInfo* info = NULL;
    if ( item )
    {
        info = item->GetServer();
    }

    // keep the cursor position relative to the top, if possible
    int selectedFromTop = items.Len() - selected;

    ReverseItems();

    nServerInfo::CalcScoreAll();
    nServerInfo::Sort( nServerInfo::PrimaryKey( sortKey_ ) );

    int mi = 2;
    gServerInfo *run = gServerInfo::GetFirstServer();
	bool oneFound = false; //so we can display all if none were found

    while (run)
    {
        if(filter_string.Len() > 1)
        {
            run->show = false;
            oneFound = true;

            tString name;
            name << tColoredString::RemoveColors( run->GetName(), false );

            if(name.ToLower().Contains(filter_string.ToLower()))
            {
                run->show = true;
            }
        }
		//check friend filter
		else if (getFriendsEnabled())
		{
			run->show = false;
			int i;
			tString userNames = run->UserNames();
			tArray<tString> userNamesList = userNames.Split("\n");
			tString* friends = getFriends();
			for (i = MAX_FRIENDS-1; i>=0; i--)
            {
                tString friendName;
                friendName << friends[i];
                if (!getFriendsCasingEnabled)
                    friendName = friendName.ToLower();

                if (friendName.Filter() == "")
                    continue;

                if (run->Users() > 0 && friendName.Len() > 1 && userNamesList.Len() > 0)
                {
                    for(int j = 0; j < userNamesList.Len(); j++)
                    {
                        tString userNames;
                        userNames << userNamesList[j];
                        if (!getFriendsCasingEnabled)
                            userNames = userNames.ToLower();

                        if (userNames.Filter() != "")
                        {
                            if (userNames.Contains(friendName))
                            {
                                oneFound = true;
                                run->show = true;
                            }
                        }
                    }
                }
            }
		}
        run = run->Next();
	}

	// Who is shown, in two stretches: the servers people are on, and the empty ones.
    std::vector< gServerInfo * > live, empty;
    livePlayers_ = 0;
    for ( run = gServerInfo::GetFirstServer(); run; run = run->Next() )
    {
        if ( !( run->show || oneFound == false ) )
            continue;
        if ( sg_browserRclOnly && !sg_BrowserIsRcl( run ) )
            continue;
        if ( run->Users() > 0 && run->Reachable() )
        {
            live.push_back( run );
            livePlayers_ += run->Users();
        }
        else
            empty.push_back( run );
    }
    liveServers_ = int( live.size() );
    emptyServers_ = int( empty.size() );
    if ( sg_browserSort < 0 || sg_browserSort >= gBrowserSort_Count )
        sg_browserSort = gBrowserSort_Players;
    bool const byName = sg_browserSort == gBrowserSort_Name;
    std::stable_sort( live.begin(), live.end(), gBrowserOrder( sg_browserSort ) );
    std::stable_sort( empty.begin(), empty.end(), gBrowserOrder( byName ? int( gBrowserSort_Name ) : int( gBrowserSort_Ping ) ) );
    if ( sg_browserHideEmpty )
        empty.clear();

    // The rows under the search field and the host row are made anew. (The
    // list is top first here: those two are its first.)
    while ( items.Len() > 2 )
    {
        uMenuItem *it = items(items.Len()-1);
        delete it;
    }

    bool const cards = sg_browserView == gBrowserView_Cards;
    if ( !live.empty() )
    {
        tNEW( gBrowserSectionItem )( this, "playing now" );
        for ( size_t i = 0; i < live.size(); ++i )
        {
            gServerMenuItem * const item = tNEW( gServerMenuItem )( this );
            item->SetServer( live[i] );
            item->SetCell( cards ? gBrowserCell_Card : gBrowserCell_Line );
        }
    }
    if ( !empty.empty() )
    {
        tNEW( gBrowserSectionItem )( this, byName ? "empty" : "empty, nearest first" );
        for ( size_t i = 0; i < empty.size(); ++i )
        {
            gServerMenuItem * const item = tNEW( gServerMenuItem )( this );
            item->SetServer( empty[i] );
            item->SetCell( cards ? gBrowserCell_Compact : gBrowserCell_Line );
        }
    }
    if ( live.empty() && empty.empty() )
    {
        // the row that says there is none
        tNEW( gServerMenuItem )( this );
    }
    // room under the last row, so a tall one clears the end of the list
    tNEW( gBrowserSectionItem )( this, "" );

    ReverseItems();

    // keep the cursor position relative to the top, if possible ( calling function will handle the clamping )
    selected = items.Len() - selectedFromTop;

    // set cursor to currently selected server, if possible
    if ( info && info->menuItem )
    {
        selected = info->menuItem->GetID();
    }

    // never on a heading: the nearest row under it, or else over it
    if ( selected >= items.Len() )
        selected = items.Len() - 1;
    if ( selected < 0 )
        selected = 0;
    if ( items.Len() > 0 && !items( selected )->IsSelectable() )
    {
        int found = -1;
        for ( int i = selected - 1; i >= 0 && found < 0; --i )
            if ( items(i)->IsSelectable() )
                found = i;
        for ( int i = selected + 1; i < items.Len() && found < 0; ++i )
            if ( items(i)->IsSelectable() )
                found = i;
        if ( found >= 0 )
            selected = found;
    }

    Layout();

    if (sg_RequestLANcontinuously)
    {
        static REAL timeout=-1E+32f;

        if (timeout < tSysTimeFloat())
        {
            nServerInfo::GetFromLANContinuously();
            if (!continuePoll)
            {
                nServerInfo::StartQueryAll( sg_queryType );
                continuePoll = true;
            }
            timeout = tSysTimeFloat()+10;
        }
    }
}

gServerMenu::gServerMenu(const char *title)
        : uMenu(title, false)
        , sortKey_( nServerInfo::KEY_SCORE )
        , livePlayers_( 0 ), liveServers_( 0 ), emptyServers_( 0 )
#ifndef DEDICATED
        , headerDrawn_( false )
#endif
{
    SetStyle( uMenuStyle_RclFull );

    nServerInfo *run = nServerInfo::GetFirstServer();
    while (run)
    {
        gServerMenuItem *item = tNEW(gServerMenuItem)(this);
        item->SetServer(run);
        run = run->Next();
    }

    ReverseItems();

    if (items.Len() <= 0)
    {
        selected = 1;
        tNEW(gServerMenuItem)(this);
    }
    else
        selected = items.Len();
}

gServerMenu::~gServerMenu()
{
    for (int i=items.Len()-1; i>=0; i--)
        delete items(i);
}

#ifndef DEDICATED
static REAL text_height=.05;
#define gSBTEXTWIDTH 0.025
static REAL text_width=gSBTEXTWIDTH;
static REAL aspect = 1;
static int resW=1, resH=1;

static REAL shrink = .6f;
static REAL displace = .15;
REAL gServerMenu::ItemDrawY(int itemIndex)
{
    if ( RclStyle() )
        return YPos(itemIndex);

    return YPos(itemIndex) * shrink + displace;
}

REAL gServerMenu::ItemRowHalf(int itemIndex)
{
    if ( RclStyle() )
    {
        if ( itemIndex >= 0 && itemIndex < int( slots_.size() ) && slots_[ itemIndex ].cell != gBrowserCell_Row )
            return slots_[ itemIndex ].height * .5f;
        return uRclTheme::RowHalfHeight();
    }

    return text_height * shrink * 0.48f;
}

bool gServerMenu::ItemContainsX(int itemIndex, REAL mouseX)
{
    if ( !RclStyle() || itemIndex < 0 || itemIndex >= int( slots_.size() ) )
        return true;
    return mouseX >= slots_[ itemIndex ].left && mouseX <= slots_[ itemIndex ].right;
}

// ---- the RCL table, measured in the theme's logical pixels ----
static REAL const sg_cellPad    = 16;   // from a row's edge to its text
static REAL const sg_scoreWidth = 150;  // the number columns; their text is right-aligned
static REAL const sg_usersWidth = 130;
static REAL const sg_pingWidth  = 110;
static REAL const sg_tableText  = 15;
static REAL const sg_labelText  = 13;
static REAL const sg_detailLabelWidth = 96;

struct gBrowserColumns
{
    REAL name, nameWidth;       // left edge and room of the name column
    REAL ping, users, score;    // right edges of the number columns
};

static gBrowserColumns sg_BrowserColumns()
{
    gBrowserColumns c;
    c.score = uRclTheme::RowRight() - uRclTheme::W( sg_cellPad );
    c.users = c.score - uRclTheme::W( sg_scoreWidth );
    c.ping  = c.users - uRclTheme::W( sg_usersWidth );
    c.name  = uRclTheme::LabelX();
    c.nameWidth = c.ping - uRclTheme::W( sg_pingWidth ) - c.name;
    return c;
}

// copy in the strip between the list and the key hints
static void sg_BrowserFooter( tString const & text, REAL alpha )
{
    REAL const top = uRclTheme::Y( uRclTheme::LogicalHeight() - 160 );
    uRclTheme::Paragraph( uRclTheme::LabelX(), top,
                          uRclTheme::RowRight() - uRclTheme::W( sg_cellPad ) - uRclTheme::LabelX(),
                          15, 400, uRclTheme::textSecondary, alpha, text, 4 );
}

void gServerMenu::RenderColumnHeader( REAL alpha )
{
    // The strip between the page title and the first row says how many play.
    REAL const y = uRclTheme::MenuTop() + uRclTheme::H( 12 );

    tString line;
    line << livePlayers_ << " playing on " << liveServers_ << ( liveServers_ == 1 ? " server" : " servers" );
    if ( !sg_browserHideEmpty )
        line << "  \xb7  " << emptyServers_ << " empty";
    if ( getFriendsEnabled() )
        line << "  \xb7  " << tOutput( "$friends_enable" );
    uRclTheme::Text( uRclTheme::LabelX(), y, sg_labelText, 500, uRclTheme::textSecondary, alpha, line );
}

// ---- drawing the rows ----
namespace
{
uRclTheme::Color const sg_cardColor   = { .165f, .172f, .180f };   // a card at rest, between the page and a control
uRclTheme::Color const sg_trackColor  = { .231f, .239f, .247f };
uRclTheme::Color const sg_quietText   = { .470f, .478f, .486f };
uRclTheme::Color const sg_pingGood    = { .384f, .839f, .510f };
uRclTheme::Color const sg_pingFair    = { .941f, .745f, .314f };
uRclTheme::Color const sg_pingPoor    = { .925f, .392f, .353f };

int sg_HexDigit( char c )
{
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return -1;
}

// The colours a server wrote its name in, in the order they first colour a
// letter: the flag down the left of its row. Dark ones are lifted so they
// show on the page.
int sg_NameColours( tString const & raw, uRclTheme::Color * out, int room )
{
    char const * const s = raw;
    int const length = int( strlen( s ) );
    enum { most = 8 };
    uRclTheme::Color seen[ most ];
    int letters[ most ];
    int count = 0, current = -1;

    for ( int i = 0; i < length; )
    {
        if ( s[i] == '0' && i + 7 < length && s[i+1] == 'x' )
        {
            int v[6];
            bool hex = true;
            for ( int k = 0; k < 6 && hex; ++k )
            {
                v[k] = sg_HexDigit( s[ i + 2 + k ] );
                hex = v[k] >= 0;
            }
            if ( hex )
            {
                uRclTheme::Color c = { ( v[0] * 16 + v[1] ) / 255.0f, ( v[2] * 16 + v[3] ) / 255.0f, ( v[4] * 16 + v[5] ) / 255.0f };
                current = -1;
                for ( int k = 0; k < count; ++k )
                    if ( fabs( seen[k].r - c.r ) + fabs( seen[k].g - c.g ) + fabs( seen[k].b - c.b ) < .06f )
                        current = k;
                if ( current < 0 && count < most )
                {
                    seen[ count ] = c;
                    letters[ count ] = 0;
                    current = count++;
                }
                i += 8;
                continue;
            }
            if ( strncmp( s + i, "0xRESETT", 8 ) == 0 )
            {
                current = -1;
                i += 8;
                continue;
            }
        }
        if ( current >= 0 && s[i] != ' ' && s[i] != '|' && s[i] != '-' )
            ++letters[ current ];
        ++i;
    }

    int n = 0;
    for ( int k = 0; k < count && n < room; ++k )
    {
        if ( letters[k] <= 0 )
            continue;
        uRclTheme::Color c = seen[k];
        REAL const light = .299f * c.r + .587f * c.g + .114f * c.b;
        if ( light < .32f )
        {
            REAL const lift = ( .32f - light ) / ( 1 - light );
            c.r += ( 1 - c.r ) * lift;
            c.g += ( 1 - c.g ) * lift;
            c.b += ( 1 - c.b ) * lift;
        }
        out[ n++ ] = c;
    }
    return n;
}

//! everything a row shows of a server
struct gBrowserFacts
{
    tString name, who, state, ping, count, most;
    int users, maxUsers, pingMs;
    bool rcl;
    tString kind, mode, region;
    uRclTheme::Color colours[4];
    int colourCount;
};

void sg_BrowserFacts( gServerInfo * server, bool favorite, gBrowserFacts & f )
{
    f.users = server->Users();
    f.maxUsers = server->MaxUsers();
    int p = static_cast< int >( server->Ping() * 1000 );
    if ( p < 0 ) p = 0;
    if ( p > 9999 ) p = 9999;
    f.pingMs = p;

    f.name << tColoredString::RemoveColors( server->GetName(), false );
    if ( favorite )
        f.name << "  \xb7  bookmarked";

    if ( server->Polling() )
        f.state << tOutput( "$network_master_polling" );
    else if ( !server->Reachable() )
        f.state << tOutput( "$network_master_unreachable" );
    else if ( nServerInfo::Compat_Ok != server->Compatibility() )
    {
        switch ( server->Compatibility() )
        {
        case nServerInfo::Compat_Upgrade:   f.state << tOutput( "$network_master_upgrage" ); break;
        case nServerInfo::Compat_Downgrade: f.state << tOutput( "$network_master_downgrage" ); break;
        default:                            f.state << tOutput( "$network_master_incompatible" ); break;
        }
    }
    else if ( f.maxUsers > 0 && f.users >= f.maxUsers )
        f.state << "full";

    if ( server->Reachable() )
        f.ping << p << " ms";
    f.count << f.users;
    f.most << "/ " << f.maxUsers;
    if ( server->UserNamesOneLine().Len() > 2 )
        f.who << tColoredString::RemoveColors( server->UserNamesOneLine(), false );

    f.rcl = sg_RclServerFacts( server->GetConnectionName(), server->GetPort(), f.kind, f.mode, f.region );
    f.colourCount = sg_NameColours( server->GetName(), f.colours, 4 );
    if ( f.colourCount == 0 )
    {
        // a name in no colour of its own: the league's lime for its servers, a quiet grey for the rest
        f.colours[0] = f.rcl ? uRclTheme::accent : uRclTheme::borderStrong;
        f.colourCount = 1;
    }
}

// a small outlined word; the league's own is filled. Returns where the next one starts.
REAL sg_BrowserTag( REAL x, REAL y, char const * text, bool brand, REAL alpha )
{
    REAL const size = 11.5f;
    REAL const w = uRclTheme::TextWidth( text, size, 500 ) + uRclTheme::W( 14 );
    REAL const half = uRclTheme::H( 9 );
    if ( brand )
    {
        uRclTheme::FillRect( x, y + half, x + w, y - half, uRclTheme::accent, alpha, 4 );
        uRclTheme::Text( x + w * .5f, y, size, 500, uRclTheme::onAccent, alpha, text, 0 );
    }
    else
    {
        uRclTheme::FrameRect( x, y + half, x + w, y - half, 1, uRclTheme::borderStrong, alpha );
        uRclTheme::Text( x + w * .5f, y, size, 500, uRclTheme::textSecondary, alpha, text, 0 );
    }
    return x + w + uRclTheme::W( 6 );
}

// the words the league adds to its own servers
REAL sg_BrowserTags( REAL x, REAL y, gBrowserFacts const & f, REAL alpha )
{
    if ( !f.rcl )
        return x;
    x = sg_BrowserTag( x, y, "rcl", true, alpha );
    if ( f.kind.Len() > 1 && !( f.mode.Len() > 1 && strstr( f.mode, f.kind ) ) )
        x = sg_BrowserTag( x, y, f.kind, false, alpha );
    if ( f.mode.Len() > 1 )
        x = sg_BrowserTag( x, y, f.mode, false, alpha );
    if ( f.region.Len() > 1 )
        x = sg_BrowserTag( x, y, f.region, false, alpha );
    return x;
}

// the server's colours as a flag: a narrow upright band in as many parts
void sg_BrowserFlag( REAL left, REAL top, REAL right, REAL bottom, gBrowserFacts const & f, REAL alpha )
{
    REAL const part = ( top - bottom ) / f.colourCount;
    for ( int i = 0; i < f.colourCount; ++i )
        uRclTheme::FillRect( left, top - part * i, right, top - part * ( i + 1 ), f.colours[i], alpha,
                             f.colourCount == 1 ? 2 : 0 );
}

// how full, as a track with its filled part
void sg_BrowserBar( REAL left, REAL right, REAL y, gBrowserFacts const & f, REAL alpha )
{
    REAL const half = uRclTheme::H( 3 );
    uRclTheme::FillRect( left, y + half, right, y - half, sg_trackColor, alpha, 3 );
    if ( f.users > 0 && f.maxUsers > 0 )
    {
        REAL share = REAL( f.users ) / REAL( f.maxUsers );
        if ( share > 1 ) share = 1;
        REAL const least = uRclTheme::W( 6 );
        REAL fill = ( right - left ) * share;
        if ( fill < least ) fill = least;
        uRclTheme::FillRect( left, y + half, left + fill, y - half, uRclTheme::accent, alpha, 3 );
    }
}

// the ping with its dot, right aligned; returns the left edge of what was drawn
REAL sg_BrowserPing( REAL right, REAL y, gBrowserFacts const & f, REAL size, uRclTheme::Color const & color, REAL alpha )
{
    if ( f.ping.Len() <= 1 )
        return right;
    uRclTheme::Text( right, y, size, 400, color, alpha, f.ping, 1 );
    REAL const x = right - uRclTheme::TextWidth( f.ping, size, 400 ) - uRclTheme::W( 8 );
    REAL const dot = uRclTheme::W( 8 ), half = uRclTheme::H( 4 );
    uRclTheme::FillRect( x - dot, y + half, x, y - half,
                         f.pingMs <= 50 ? sg_pingGood : f.pingMs <= 100 ? sg_pingFair : sg_pingPoor, alpha, 4 );
    return x - dot;
}
}

void gServerMenu::RenderSection( int index, REAL y, REAL alpha, char const * label )
{
    (void)index;
    if ( !sr_glOut || !RclStyle() )
        return;
    EnsureColumnHeader( alpha );
    if ( label && *label )
        uRclTheme::Text( uRclTheme::LabelX(), y - uRclTheme::H( 3 ), sg_labelText, 500, uRclTheme::textSecondary, alpha, label );
}

void gServerMenu::RenderServer( int index, gServerInfo * server, bool favorite, REAL y, REAL alpha, bool selected )
{
    if ( !sr_glOut || !RclStyle() )
        return;
    EnsureColumnHeader( alpha );
    if ( index < 0 || index >= int( slots_.size() ) )
        return;
    gBrowserSlot const & slot = slots_[ index ];
    REAL const half = slot.height * .5f;
    REAL const top = y + half, bottom = y - half;
    REAL const l = slot.left, r = slot.right;

    // a tall row thins out as it leaves the list, by its edge and not its middle
    {
        REAL const band = uRclTheme::RowPitch() * .5f;
        if ( top > menuTop )
            alpha *= std::max( 0.0f, 1 - ( top - menuTop ) / band );
        if ( bottom < menuBot )
            alpha *= std::max( 0.0f, 1 - ( menuBot - bottom ) / band );
        if ( alpha <= .01f )
            return;
    }

    if ( !server )
    {
        tString none;
        none << tOutput( "$network_master_noserver" );
        if ( selected )
            uRclTheme::FillRect( l, top, r, bottom, uRclTheme::surfaceSelected, alpha );
        uRclTheme::Text( uRclTheme::LabelX(), y, sg_tableText, 400, uRclTheme::textSecondary, alpha, none );
        return;
    }

    gBrowserFacts f;
    sg_BrowserFacts( server, favorite, f );

    if ( slot.cell == gBrowserCell_Card )
    {
        uRclTheme::FillRect( l, top, r, bottom, selected ? uRclTheme::surfaceRaised : sg_cardColor, alpha, 6 );
        if ( selected )
            uRclTheme::FrameRect( l, top, r, bottom, 2, uRclTheme::accent, alpha );
        sg_BrowserFlag( l + uRclTheme::W( 10 ), top - uRclTheme::H( 12 ), l + uRclTheme::W( 14 ), bottom + uRclTheme::H( 12 ), f, alpha );

        REAL const textLeft = l + uRclTheme::W( 26 );
        REAL const textRight = r - uRclTheme::W( 14 );
        REAL const nameY = top - uRclTheme::H( 22 );
        REAL const pingLeft = sg_BrowserPing( textRight, nameY, f, 13, uRclTheme::textSecondary, alpha );
        uRclTheme::Text( textLeft, nameY, 16, 500, uRclTheme::textPrimary, alpha, f.name, -1,
                         pingLeft - uRclTheme::W( 12 ) - textLeft );

        sg_BrowserTags( textLeft, top - uRclTheme::H( 47 ), f, alpha );

        REAL const lineY = bottom + uRclTheme::H( 26 );
        if ( f.state.Len() > 1 && f.users <= 0 )
        {
            uRclTheme::Text( textLeft, lineY, 14, 400, uRclTheme::textSecondary, alpha, f.state, -1, textRight - textLeft );
        }
        else
        {
            uRclTheme::Text( textLeft, lineY + uRclTheme::H( 2 ), 26, 500, uRclTheme::textPrimary, alpha, f.count );
            REAL x = textLeft + uRclTheme::TextWidth( f.count, 26, 500 ) + uRclTheme::W( 6 );
            uRclTheme::Text( x, lineY - uRclTheme::H( 3 ), 13, 400, uRclTheme::textSecondary, alpha, f.most );
            REAL const barLeft = textLeft + uRclTheme::W( 92 );
            REAL const barRight = barLeft + uRclTheme::W( 120 );
            sg_BrowserBar( barLeft, barRight, lineY - uRclTheme::H( 2 ), f, alpha );
            REAL const whoLeft = barRight + uRclTheme::W( 16 );
            if ( f.state.Len() > 1 )
                uRclTheme::Text( whoLeft, lineY - uRclTheme::H( 2 ), 12.5f, 500, uRclTheme::textSecondary, alpha, f.state, -1, textRight - whoLeft );
            else if ( f.who.Len() > 1 && textRight - whoLeft > uRclTheme::W( 40 ) )
                uRclTheme::Text( whoLeft, lineY - uRclTheme::H( 2 ), 12.5f, 400, sg_quietText, alpha, f.who, -1, textRight - whoLeft );
        }
        return;
    }

    // one line: an empty server in the cards view, or any server in the list
    bool const compact = slot.cell == gBrowserCell_Compact;
    if ( selected )
    {
        uRclTheme::FillRect( l, top - uRclTheme::H( 1 ), r, bottom + uRclTheme::H( 1 ), uRclTheme::surfaceSelected, alpha, 4 );
        uRclTheme::FillRect( l, top - uRclTheme::H( 1 ), l + uRclTheme::W( 3 ), bottom + uRclTheme::H( 1 ), uRclTheme::accent, alpha );
    }
    sg_BrowserFlag( l + uRclTheme::W( 10 ), top - uRclTheme::H( 7 ), l + uRclTheme::W( 13 ), bottom + uRclTheme::H( 7 ), f, alpha );

    REAL const textLeft = l + uRclTheme::W( 24 );
    REAL const textRight = r - uRclTheme::W( 14 );
    bool const quiet = f.users <= 0;
    uRclTheme::Color const & nameColor = quiet && !selected ? uRclTheme::textSecondary : uRclTheme::textPrimary;

    if ( compact )
    {
        REAL const pingLeft = f.state.Len() > 1
            ? textRight - uRclTheme::TextWidth( f.state, 12.5f, 400 )
            : sg_BrowserPing( textRight, y, f, 12.5f, selected ? uRclTheme::textSecondary : sg_quietText, alpha );
        if ( f.state.Len() > 1 )
            uRclTheme::Text( textRight, y, 12.5f, 400, sg_quietText, alpha, f.state, 1 );
        REAL x = textLeft;
        if ( f.rcl )
            x = sg_BrowserTag( x, y, "rcl", true, alpha ) + uRclTheme::W( 2 );
        uRclTheme::Text( x, y, 14, 400, nameColor, alpha, f.name, -1, pingLeft - uRclTheme::W( 12 ) - x );
        return;
    }

    // the list's line: name and tags, who is on, how full, the ping
    REAL const pingLeft = sg_BrowserPing( textRight, y, f, 13.5f, uRclTheme::textSecondary, alpha );
    (void)pingLeft;
    REAL const countRight = textRight - uRclTheme::W( 84 );
    REAL const barRight = countRight - uRclTheme::W( 70 );
    REAL const barLeft = barRight - uRclTheme::W( 120 );
    REAL const whoLeft = l + ( r - l ) * .50f;
    REAL const whoRight = barLeft - uRclTheme::W( 20 );

    if ( f.state.Len() > 1 )
    {
        uRclTheme::Text( countRight, y, 13, 400, uRclTheme::textSecondary, alpha, f.state, 1 );
    }
    else
    {
        tString count;
        count << f.users << " / " << f.maxUsers;
        uRclTheme::Text( countRight, y, 14, 500, quiet ? uRclTheme::textSecondary : uRclTheme::textPrimary, alpha, count, 1 );
        if ( !quiet )
            sg_BrowserBar( barLeft, barRight, y, f, alpha );
    }
    if ( f.who.Len() > 1 && whoRight - whoLeft > uRclTheme::W( 60 ) )
        uRclTheme::Text( whoLeft, y, 12.5f, 400, sg_quietText, alpha, f.who, -1, whoRight - whoLeft );

    REAL const nameRoom = whoLeft - uRclTheme::W( 16 ) - textLeft;
    REAL const nameWidth = std::min( nameRoom, uRclTheme::TextWidth( f.name, 15, 500 ) );
    uRclTheme::Text( textLeft, y, 15, 500, nameColor, alpha, f.name, -1, nameRoom );
    if ( f.rcl && nameRoom - nameWidth > uRclTheme::W( 60 ) )
        sg_BrowserTags( textLeft + nameWidth + uRclTheme::W( 12 ), y, f, alpha );
}

// ---- the chips beside the search field ----
namespace
{
struct gBrowserChip
{
    REAL left, right;
    int id;
};
gBrowserChip sg_browserChips[8];
int sg_browserChipCount = 0;

// draws one at x (its left edge, or its right edge when leftwards) and returns the far edge
REAL sg_BrowserChip( REAL x, REAL y, char const * text, bool on, int id, REAL alpha, bool leftwards )
{
    REAL const size = 13;
    REAL const w = uRclTheme::TextWidth( text, size, 500 ) + uRclTheme::W( 22 );
    REAL const half = uRclTheme::H( 12 );
    REAL const left = leftwards ? x - w : x;
    uRclTheme::FillRect( left, y + half, left + w, y - half, on ? uRclTheme::accent : uRclTheme::surfaceRaised, alpha, 5 );
    uRclTheme::Text( left + w * .5f, y, size, 500, on ? uRclTheme::onAccent : uRclTheme::textSecondary, alpha, text, 0 );
    if ( sg_browserChipCount < 8 )
    {
        gBrowserChip & chip = sg_browserChips[ sg_browserChipCount++ ];
        chip.left = left;
        chip.right = left + w;
        chip.id = id;
    }
    return leftwards ? left - uRclTheme::W( 8 ) : left + w + uRclTheme::W( 8 );
}
}

void gServerMenu::RenderChips( REAL y, REAL alpha )
{
    if ( !sr_glOut || !RclStyle() )
        return;
    sg_browserChipCount = 0;

    REAL x = uRclTheme::ControlRight() + uRclTheme::W( 30 );
    uRclTheme::Text( x, y, 13, 400, sg_quietText, alpha, "sort" );
    x += uRclTheme::TextWidth( "sort", 13, 400 ) + uRclTheme::W( 12 );
    x = sg_BrowserChip( x, y, "players", sg_browserSort == gBrowserSort_Players, 0, alpha, false );
    x = sg_BrowserChip( x, y, "ping", sg_browserSort == gBrowserSort_Ping, 1, alpha, false );
    x = sg_BrowserChip( x, y, "name", sg_browserSort == gBrowserSort_Name, 2, alpha, false );
    x += uRclTheme::W( 18 );
    x = sg_BrowserChip( x, y, "rcl only", sg_browserRclOnly, 3, alpha, false );
    x = sg_BrowserChip( x, y, "hide empty", sg_browserHideEmpty, 4, alpha, false );

    // the view, from the right edge
    REAL right = uRclTheme::RowRight() - uRclTheme::W( 14 );
    right = sg_BrowserChip( right, y, "list", sg_browserView == gBrowserView_List, 6, alpha, true );
    right = sg_BrowserChip( right, y, "cards", sg_browserView == gBrowserView_Cards, 5, alpha, true );
    if ( right - uRclTheme::W( 40 ) > x )
        uRclTheme::Text( right - uRclTheme::W( 4 ), y, 13, 400, sg_quietText, alpha, "view", 1 );
}

bool gServerMenu::ClickChip( REAL mouseX )
{
    for ( int i = 0; i < sg_browserChipCount; ++i )
    {
        if ( mouseX >= sg_browserChips[i].left && mouseX <= sg_browserChips[i].right )
        {
            Changed( sg_browserChips[i].id );
            return true;
        }
    }
    return false;
}

void gServerMenu::EnsureColumnHeader( REAL alpha )
{
    if ( !RclStyle() || headerDrawn_ || alpha <= .01f )
        return;

    // Item rendering happens after uMenu paints the shared RCL background.
    // Drawing on the first rendered item keeps these labels above that layer.
    RenderColumnHeader( alpha );
    headerDrawn_ = true;
}

void gServerMenu::Render(REAL x,REAL y, tString &c, int center = 0,
                         int cursor = 0, int cursorPos = 0, REAL alpha = 1,
                         REAL right = .83f)
{
    if (sr_glOut)
    {
        if ( RclStyle() )
        {
            EnsureColumnHeader( alpha );
            uRclTheme::Text( x, y, sg_tableText, 400, uRclTheme::textPrimary, alpha,
                             c, -1, right - x );
        }
        else
        {
            DisplayTextAutoWidth(x, y, c, text_height, -1, cursor, cursorPos);
        }
    }
}

void gServerMenu::Render(REAL y,
                         const tString &servername, const tString &score,
                         const tString &users     , const tString &ping,
                         bool selected, REAL alpha)
{
    if (sr_glOut)
    {
        if ( RclStyle() )
        {
            EnsureColumnHeader( alpha );
            gBrowserColumns const c = sg_BrowserColumns();
            uRclTheme::Color const & numbers = selected ? uRclTheme::textPrimary
                                                        : uRclTheme::textSecondary;

            // authored name colours stay, lifted where they would sink
            // into the row
            uRclTheme::Text( c.name, y, sg_tableText, 500, uRclTheme::textPrimary, alpha,
                             uRclTheme::ReadableColors( servername, selected ? .45f : .24f ),
                             -1, c.nameWidth );
            uRclTheme::Text( c.ping, y, sg_tableText, 400, numbers, alpha, ping, 1 );
            uRclTheme::Text( c.users, y, sg_tableText, 400, numbers, alpha, users, 1 );
            // a state (polling, unreachable, full) may take the place of the numbers
            uRclTheme::Text( c.score, y, sg_tableText, 400, numbers, alpha, score, 1,
                             users.Len() > 1 ? 0 : c.score - c.name - c.nameWidth );
            return;
        }

        rTextField c(-.9f, y+text_height*.5, text_width, text_height);
        c.SetWidth(1000);

        c.SetIndent(5);


        //int posDisplacement = 0;
        tColoredString text;
        if (tColoredString::RemoveColors(servername).Len() > 1)
        {
            text << servername << "0xRESETT";
        }
        else
            text << tOutput("$network_master_unknown");

        if (ping.Len() > 1)
        {
            text.SetPos(static_cast<int>((2.0-0.45*aspect-0.2)/c.GetCWidth())  - tColoredString::RemoveColors( ping ).Len() - 1, true );
            text << "0xRESETT";
            text << " " << ping;
        }
        else
        {
            text << "0xRESETT";
        }

        if (users.Len() > 1)
        {
            text.SetPos(static_cast<int>((2.0-0.2*aspect-0.2)/c.GetCWidth()) - tColoredString::RemoveColors( users ).Len(), false );
            text << users;
        }

        if (score.Len() > 1)
        {
            text.SetPos(static_cast<int>(1.8/c.GetCWidth()) - tColoredString::RemoveColors( score ).Len(), false );
            text << score;
        }

        c << text;
    }
}

void gServerMenu::Render(REAL y,
                         const tString &servername, const tOutput &score,
                         const tOutput &users     , const tOutput &ping,
                         bool selected, REAL alpha)
{
    if ( RclStyle() )
    {
        // The RCL table tells the sort order in its column header. Row text
        // stays free of the legacy inline colour codes, so the theme's
        // palette is the only one in the table.
        tString sn, s, u, p;
        sn << servername;
        s << score;
        u << users;
        p << ping;
        Render( y, sn, s, u, p, selected, alpha );
        return;
    }

    tColoredString highlight, normal;
    highlight << tColoredString::ColorString( 1,.7,.7 );
    normal << tColoredString::ColorString( .7,.3,.3 );

    tString sn, s, u, p;

    sn << normal;
    s << normal;
    u << normal;
    p << normal;

    switch ( sortKey_ )
    {
    case nServerInfo::KEY_NAME:
        sn = highlight;
        break;
    case nServerInfo::KEY_PING:
        p = highlight;
        break;
    case nServerInfo::KEY_USERS:
        u = highlight;
        break;
    case nServerInfo::KEY_SCORE:
        s = highlight;
        break;
    case nServerInfo::KEY_MAX:
        break;
    }

    sn << servername;// tColoredString::RemoveColors( servername );
    s  << score;
    u  << users;
    p  << ping;

    Render(y, sn, s, u, p, selected, alpha);
}

void gServerMenu::RenderDetails( gServerInfo *server, REAL alpha )
{
    if ( !sr_glOut || !RclStyle() || !server )
        return;

    tString players;
    if ( server->UserNamesOneLine().Len() > 2 )
        players << server->UserNamesOneLine();
    else
        players << tOutput( "$network_master_players_empty" );

    tString endpoint;
    endpoint << server->Release();
    if ( server->Url().Len() > 1 )
        endpoint << "  \xb7  " << server->Url();

    tString options;
    options << server->Options();

    // three lines about the selected server, between the list and the hints
    REAL const labelX = uRclTheme::LabelX();
    REAL const valueX = labelX + uRclTheme::W( sg_detailLabelWidth );
    REAL const room = uRclTheme::RowRight() - uRclTheme::W( sg_cellPad ) - valueX;
    char const * const labels[3] = { "players", "server", "options" };
    tString const * const values[3] = { &players, &endpoint, &options };

    for ( int i = 0; i < 3; ++i )
    {
        REAL const y = uRclTheme::Y( uRclTheme::LogicalHeight() - 150 + 22 * i );
        uRclTheme::Text( labelX, y, sg_labelText, 500, uRclTheme::textSecondary,
                         alpha, labels[i] );
        uRclTheme::Text( valueX, y, 14, 400, uRclTheme::textPrimary, alpha,
                         *values[i], -1, room );
    }
}

#endif /* DEDICATED */
static bool sg_filterServernameColorStrings = true;
static tSettingItem< bool > removeServerNameColors("FILTER_COLOR_SERVER_NAMES", sg_filterServernameColorStrings);
static bool sg_filterServernameDarkColorStrings = true;
static tSettingItem< bool > removeServerNameDarkColors("FILTER_DARK_COLOR_SERVER_NAMES", sg_filterServernameDarkColorStrings);

void gServerMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    // REAL time=tSysTimeFloat()*10;

    SetColor( selected, alpha );

    gServerMenu *serverMenu = static_cast<gServerMenu*>(menu);

    if ( serverMenu->RclStyle() )
    {
        serverMenu->RenderServer( GetID(), server, favorite_, y, alpha, selected );
        if ( selected && server )
            serverMenu->RenderDetails( server, alpha );
        return;
    }

    if (server)
    {
        tColoredString name;
        tString score;
        tString users;
        tString ping;

        int p = static_cast<int>(server->Ping()*1000);
        if (p < 0)
            p = 0;
        if (p > 10000)
            p = 10000;

        int s = static_cast<int>(server->Score());
        if (server->Score() > 10000)
            s = 10000;
        if (server->Score() < -10000)
            s = -10000;

        if (server->Polling())
        {
            score << tOutput("$network_master_polling");
        }
        else if (!server->Reachable())
        {
            score << tOutput("$network_master_unreachable");
        }
        else if ( nServerInfo::Compat_Ok != server->Compatibility() )
        {
            switch( server->Compatibility() )
            {
            case nServerInfo::Compat_Upgrade:
                score << tOutput( "$network_master_upgrage" );
                score << " " << nServerInfo::Compat_Ok << " " << server->Compatibility() << '\n';
                break;
            case nServerInfo::Compat_Downgrade:
                score << tOutput( "$network_master_downgrage" );
                break;
            default:
                score << tOutput( "$network_master_incompatible" );
                break;
            }
        }
        else if ( server->Users() >= server->MaxUsers() )
        {
            score << tOutput( "$network_master_full" );
            score << " (" << server->Users() << "/" << server->MaxUsers() << ")";
        }
        else
        {
            if ( favorite_ )
            {
                score << "B ";
            }

            score << s;
            users << server->Users() << "/" << server->MaxUsers();
            ping  << p;
        }

        if ( sg_filterServernameColorStrings )
            name << tColoredString::RemoveColors( server->GetName(), false );
	else if ( sg_filterServernameDarkColorStrings )
            name << tColoredString::RemoveColors( server->GetName(), true );
        else
        {
            name << server->GetName();
        }

        REAL const drawY = serverMenu->RclStyle()
                           ? y : y*shrink + displace;
        serverMenu->Render(drawY,
                           name,
                           score, users, ping, selected, alpha);

        if ( selected )
            serverMenu->RenderDetails( server, alpha );
    }
    else
    {
        tOutput o("$network_master_noserver");
        tString s;
        s << o;
        REAL const drawY = serverMenu->RclStyle()
                           ? y : y*shrink + displace;
        serverMenu->Render(drawY,
                           s,
                           tString(""), tString(""), tString(""),
                           selected, alpha);

    }
#endif
}

static REAL sg_menuBottom    = -.9;
static REAL sg_requestBottom = -.9;

void gServerMenuItem::RenderBackground()
{
#ifndef DEDICATED
    if ( menu->RclStyle() )
    {
        // The shared RCL pass paints its background after this callback. Keep
        // the polling work here; the selected-server summary is painted later
        // with the selected row so it remains visible.
        gBrowserMenuItem::RenderBackground();
        return;
    }

    REAL helpTopReal = sg_requestBottom*shrink + displace - .05;;

    gBrowserMenuItem::RenderBackground();

    rTextField::SetDefaultColor( tColor(1,1,1) );

    rTextField players( -.9, helpTopReal, text_width, text_height );
    if ( server )
    {
        players  << tOutput( "$network_master_players" ) << tOutput(" ");
        if ( server->UserNamesOneLine().Len() > 2 )
            players << server->UserNamesOneLine();
        else
            players << tOutput( "$network_master_players_empty" );
        players << "\n" << tColoredString::ColorString(1,1,1);
        tColoredString uri;
        uri << server->Url() << tColoredString::ColorString(1,1,1);
        players << tOutput( "$network_master_serverinfo", server->Release(), uri, server->Options() );
    }

    {
        players << "\n";
        players.SetColor(tColor(1,1,1,displayHelp_ ? helpAlpha_ : 0));
        players << Help();
    }

    REAL helpSpace = players.GetTop() - players.GetBottom();
    REAL helpTop = -.85 + helpSpace;
    REAL helpTopScaled = ( helpTop - displace )/shrink;
    REAL helpTopMax = .25;
    REAL helpTopMin = -.9;
    if( helpTopScaled > helpTopMax )
    {
        helpTopScaled = helpTopMax;
    }
    if( helpTopScaled < helpTopMin )
    {
        helpTopScaled = helpTopMin;
    }
    sg_requestBottom = helpTopScaled;
#endif
}

#ifndef DEDICATED
static void Refresh()
{
    continuePoll = true;
    nServerInfo::StartQueryAll( sg_queryType );
}
#endif

bool gBrowserMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_r:
            {
                static double lastRefresh = - 100; //!< the time of the last manual refresh
                if ( tSysTimeFloat() - lastRefresh > 2.0 )
                {
                    lastRefresh = tSysTimeFloat();
                    // trigger refresh
                    st_ToDo( Refresh );
                    return true;
                }
            }
            break;
        default:
            break;
        }
    }
#endif

    return uMenuItem::Event( event );
}


bool gServerMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case SDLK_p:
            continuePoll = true;
            if ( server && tSysTimeFloat() - lastPing_ > .5f )
            {
                lastPing_ = tSysTimeFloat();

                server->SetQueryType( nServerInfo::QUERY_ALL );
                server->QueryServer();
                server->ClearInfoFlags();
            }
            return true;
            break;
        default:
            break;
        }
        switch (event.key.keysym.unicode)
        {
        case '+':
            if ( server )
            {
                server->SetScoreBias( server->GetScoreBias() + 10 );
                server->CalcScore();
            }
            (static_cast<gServerMenu*>(menu))->Update();

            return true;
            break;
        case '-':
            if ( server )
            {
                server->SetScoreBias( server->GetScoreBias() - 10 );
                server->CalcScore();
            }
            (static_cast<gServerMenu*>(menu))->Update();

            return true;
            break;
        case 'b':
            if ( server && !favorite_ )
            {
                favorite_ = gServerFavorites::AddFavorite( server );
            }
            return true;
            break;
        default:
            break;
        }
    }
#endif

    return gBrowserMenuItem::Event( event );
}

void gBrowserMenuItem::RenderBackground()
{
#ifndef DEDICATED
    if(resH != sr_screenHeight || resW != sr_screenWidth)
    {
        // since we're rendering a little more text
        // here's a (probably minimal) performance benefit over doing this math every time
        aspect = (REAL(sr_screenHeight)/sr_screenWidth)*1.15;
        text_width = gSBTEXTWIDTH*aspect;
        resH = sr_screenHeight; resW = sr_screenWidth;
    }
#endif
    
    if( menu && !menu->RclStyle() )
    {
        double now = tSysTimeFloat();
        static double lastTime = now;
        if( sg_menuBottom > sg_requestBottom )
        {
            sg_menuBottom -= now - lastTime;
        }
        lastTime = now;
        if( sg_menuBottom < sg_requestBottom )
        {
            sg_menuBottom = sg_requestBottom;
        }

        menu->SetBot( sg_menuBottom );
        sg_requestBottom = -.9;
    }

    sn_Receive();
    sn_SendPlanned();

    if ( !menu->RclStyle() )
        menu->GenericBackground();
    if (continuePoll)
    {
        continuePoll = nServerInfo::DoQueryAll(sg_simultaneous);
        sn_Receive();
        sn_SendPlanned();
    }

#ifndef DEDICATED
    if ( menu->RclStyle() )
        return;

    rTextField::SetDefaultColor( tColor(.8,.3,.3,1) );

	tString sn2 = tString(tOutput("$network_master_servername"));
	if (getFriendsEnabled()) //display that friends filter is on
		sn2 << " - " << tOutput("$friends_enable");

    static_cast<gServerMenu*>(menu)->Render(.62,
                                            sn2,
                                            tOutput("$network_master_score"),
                                            tOutput("$network_master_users"),
                                            tOutput("$network_master_ping"));
#endif
}

void gServerMenuItem::Enter()
{
    nServerInfo::GetFromLANContinuouslyStop();

    menu->Exit();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(false);

    if (server)
        ConnectToServer(server);
}


void gServerMenuItem::SetServer(nServerInfo *s)
{
    if (s == server)
        return;

    if (server)
        server->menuItem = NULL;

    server = dynamic_cast<gServerInfo*>(s);

    if (server)
    {
        if (server->menuItem)
            server->menuItem->SetServer(NULL);

        server->menuItem = this;
    }

    favorite_ = gServerFavorites::IsFavorite( server );
}

gServerInfo *gServerMenuItem::GetServer()
{
    return server;
}

static char const * sg_HelpText = "$network_master_browserhelp";

gServerMenuItem::gServerMenuItem(gServerMenu *men)
        :gBrowserMenuItem(men, sg_HelpText), server(NULL), lastPing_(-100), favorite_(false), cell_(gBrowserCell_Line)
{}

gServerMenuItem::~gServerMenuItem()
{
    SetServer(NULL);

    // make sure the last entry in the array (the first menuitem)
    // stays the same
    uMenuItem* last = menu->Item(menu->NumItems()-1);
    menu->RemoveItem(last);
    menu->RemoveItem(this);
    menu->AddItem(last);
}


gServerInfo::~gServerInfo()
{
    if (menuItem)
        delete menuItem;
}


void gServerStartMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    // REAL time=tSysTimeFloat()*10;

    SetColor( selected, alpha );

    tString s;
    s << tOutput("$network_master_start");
    gServerMenu *serverMenu = static_cast<gServerMenu*>(menu);
    REAL const drawY = serverMenu->RclStyle()
                       ? y : y*shrink + displace;
    serverMenu->Render(drawY, s,
                       tString(), tString(), tString(), selected, alpha);

    if ( selected && serverMenu->RclStyle() )
        sg_BrowserFooter( Help(), alpha );
#endif
}

void gServerStartMenuItem::Enter()
{
    nServerInfo::GetFromLANContinuouslyStop();

    menu->Exit();

    //  gLogo::SetBig(false);
    //  gLogo::SetSpinning(true);
    // gLogo::SetDisplayed(false);

    sg_HostGameMenu();
}



void gServerFilterMenuItem::Render(REAL x,REAL y,REAL alpha, bool selected)
{
#ifndef DEDICATED
    SetColor( selected, alpha );
    
    tString s;
    s << description;

    gServerMenu *serverMenu = static_cast<gServerMenu*>(menu);
    if ( serverMenu->RclStyle() )
    {
        // an ordinary text row: label, then the box with what was typed;
        // beside it, the order, the filters and the view
        uMenuItemString::Render( x, y, alpha, selected );
        serverMenu->RenderChips( y, alpha );
        return;
    }
    
    x = -.9f;
    REAL x2 = s.Len() * 0.018 + x;
    
    int cMode = selected ? 1 : 0;
    
    serverMenu->Render(x, y*shrink + displace, s);
    serverMenu->Render(x2, y*shrink + displace, *content, 1, cMode, cursorPos);
#endif
}

bool gServerFilterMenuItem::Event( SDL_Event& event )
{
#ifndef DEDICATED
    if (event.type!=SDL_KEYDOWN)
        return false;
    
    bool update = prev_filter_string != *content;
    prev_filter_string = *content;
    
    switch (event.key.keysym.sym)
    {
    case(SDLK_ESCAPE):
        if(content->Len() > 0)
        {
            *content = "";
            
            (static_cast<gServerMenu*>(menu))->Update();
            return true;
            
            break;
        }
        else    
        {
            return uMenuItemString::Event( event );
        }

        break;
    default:
        break;
    }
        
    if(update)
    {
        (static_cast<gServerMenu*>(menu))->Update();
    }
    
#endif

    return uMenuItemString::Event( event );
}

gServerStartMenuItem::gServerStartMenuItem(gServerMenu *men)
        :gBrowserMenuItem(men, *sg_StartHelpText)
{}

gServerStartMenuItem::~gServerStartMenuItem()
{
}
