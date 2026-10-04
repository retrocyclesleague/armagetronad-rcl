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


class gServerMenu: public uMenu
{
    int sortKey_;
#ifndef DEDICATED
    bool headerDrawn_;

    void RenderColumnHeader( REAL alpha );
    void EnsureColumnHeader( REAL alpha );
#endif

public:
    virtual void OnRender();
    tString filter_string;

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
public:
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





void gServerMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    // Ignore event when we have the filter menu item
    if(items.Len() - selected == 1 && filter_string.Len() > 1)
    {
        return uMenu::HandleEvent( event );
    }

    switch (event.type)
    {
    case SDL_KEYDOWN:
        switch (event.key.keysym.sym)
        {
        case(SDLK_LEFT):
            sortKey_ = ( sortKey_ + nServerInfo::KEY_MAX-1 ) % nServerInfo::KEY_MAX;
            Update();
            return;
            break;
        case(SDLK_RIGHT):
            sortKey_ = ( sortKey_ + 1 ) % nServerInfo::KEY_MAX;
            Update();
            return;
            break;
		case(SDLK_m):
            if(items.Len() - selected == 1) break;
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

	run = gServerInfo::GetFirstServer();
	{
   		while (run)
    	{
			if (run->show || oneFound == false)
			{
	        	if (mi >= items.Len())
    		        tNEW(gServerMenuItem)(this);

    	    	gServerMenuItem *item = dynamic_cast<gServerMenuItem*>(items(mi));
    	    	item->SetServer(run);
	    	    mi++;
			}
        	run = run->Next();
		}
    }

    if (items.Len() == 1)
        selected = 1;

    while(mi < items.Len() && items.Len() > 2)
    {
        uMenuItem *it = items(items.Len()-1);
        delete it;
    }

    ReverseItems();

    // keep the cursor position relative to the top, if possible ( calling function will handle the clamping )
    selected = items.Len() - selectedFromTop;

    // set cursor to currently selected server, if possible
    if ( info && info->menuItem )
    {
        selected = info->menuItem->GetID();
    }

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
    (void)itemIndex;
    if ( RclStyle() )
        return uRclTheme::RowHalfHeight();

    return text_height * shrink * 0.48f;
}

// ---- the RCL table, measured in the theme's logical pixels ----
static REAL const sg_cellPad    = 16;   // from a row's edge to its text
static REAL const sg_scoreWidth = 150;  // the number columns; their text is right-aligned
static REAL const sg_usersWidth = 130;
static REAL const sg_pingWidth  = 110;
static REAL const sg_tableText  = 17;
static REAL const sg_labelText  = 14;
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
    REAL const top = uRclTheme::Y( uRclTheme::LogicalHeight() - 200 );
    uRclTheme::Paragraph( uRclTheme::LabelX(), top,
                          uRclTheme::RowRight() - uRclTheme::W( sg_cellPad ) - uRclTheme::LabelX(),
                          15, 400, uRclTheme::textSecondary, alpha, text, 4 );
}

void gServerMenu::RenderColumnHeader( REAL alpha )
{
    // The strip between the page title and the first row holds the headings;
    // the column the list is sorted by is the lit one.
    REAL const y = uRclTheme::MenuTop() + uRclTheme::H( 12 );
    gBrowserColumns const c = sg_BrowserColumns();

    tString name, ping, users, score;
    name << tOutput( "$network_master_servername" );
    if ( getFriendsEnabled() )
        name << "  \xb7  " << tOutput( "$friends_enable" );
    ping << tOutput( "$network_master_ping" );
    users << tOutput( "$network_master_users" );
    score << tOutput( "$network_master_score" );

    uRclTheme::Color const & lit = uRclTheme::accent;
    uRclTheme::Color const & quiet = uRclTheme::textSecondary;
    uRclTheme::Text( c.name, y, sg_labelText, 500,
                     sortKey_ == nServerInfo::KEY_NAME ? lit : quiet, alpha, name,
                     -1, c.nameWidth );
    uRclTheme::Text( c.ping, y, sg_labelText, 500,
                     sortKey_ == nServerInfo::KEY_PING ? lit : quiet, alpha, ping, 1 );
    uRclTheme::Text( c.users, y, sg_labelText, 500,
                     sortKey_ == nServerInfo::KEY_USERS ? lit : quiet, alpha, users, 1 );
    uRclTheme::Text( c.score, y, sg_labelText, 500,
                     sortKey_ == nServerInfo::KEY_SCORE ? lit : quiet, alpha, score, 1 );
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
        REAL const y = uRclTheme::Y( uRclTheme::LogicalHeight() - 188 + 26 * i );
        uRclTheme::Text( labelX, y, sg_labelText, 500, uRclTheme::textSecondary,
                         alpha, labels[i] );
        uRclTheme::Text( valueX, y, 15, 400, uRclTheme::textPrimary, alpha,
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
        :gBrowserMenuItem(men, sg_HelpText), server(NULL), lastPing_(-100), favorite_(false)
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
        // an ordinary text row: label, then the box with what was typed
        uMenuItemString::Render( x, y, alpha, selected );
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
