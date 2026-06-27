/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)
Copyright (C) 2004  Armagetron Advanced Team (http://sourceforge.net/projects/armagetronad/)

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

#include "tSysTime.h"
#include "uMenu.h"
#include "uRclTheme.h"
#include "rSysdep.h"
#include "rScreen.h"
#include "rViewport.h"
#include "tString.h"
#include "math.h"
#include "uInputQueue.h"
#include "rConsole.h"
#include "uInput.h"
#include "tDirectories.h"
//#include "tRecording.h"
#include "tToDo.h"
#include "tException.h"

#ifndef DEDICATED
#include "rRender.h"
#include "rSDL.h"
#endif

#include <vector>

FUNCPTR  uMenu::idle(NULL);

bool uMenu::wrap=true;
uMenu::QuickExit uMenu::quickexit=uMenu::QuickExit_Off;
bool uMenu::exitToMain=false;

// *****************************************************

#ifdef SLOPPYLOCALE
uMenu::uMenu(const char *t="",bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t),rclLayout_(uRclLayout_Off),
        hoverPending_(-1),hoverPendingSince_(0){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 10000000;
}
#endif

uMenu::uMenu(const tOutput &t,bool exit_item)
        :exitFlag(0),spaceBelow(.4),title(t),rclLayout_(uRclLayout_Off),
        hoverPending_(-1),hoverPendingSince_(0){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 100000000;
}

uMenu::~uMenu(){
    for (int i=items.Len()-1;i>=0;i--)
        delete items[i];
}

void uMenu::ReverseItems(){
    tList<uMenuItem> dummy;
    dummy.Swap( items );

    for (int i=dummy.Len()-1; i>=0; i--){
        uMenuItem *x = dummy[i];
        dummy.Remove(x, x->idnum);
        items.Add  (x, x->idnum);
    }
}

//static REAL text_height=rCHEIGHT_NORMAL;
//static REAL text_width=rCWIDTH_NORMAL;

static REAL text_height=.11;
#ifndef DEDICATED
static REAL text_width=.05;
#endif

#ifndef DEDICATED
static REAL titlefac=1.2;
#endif
int menuentries=0;

#ifndef DEDICATED
static bool menu_saved_relative_mouse = false;

static void MenuScreenToNormalized( float px, float py, REAL & mx, REAL & my )
{
    mx = 2.0f * px / sr_screenWidth - 1.0f;
    my = 1.0f - 2.0f * py / sr_screenHeight;
}
#endif

REAL uMenu::YPos(int num){
#ifndef DEDICATED
    REAL h = RclTheme() ? uRclTheme::TextH() : text_height;
#else
    REAL h = text_height;
#endif
    return yOffset-h*(menuentries-num);
}

static inline void arrow(REAL x,REAL y,REAL dy,REAL size){
#ifndef DEDICATED
    if (sr_glOut){
        BeginLineLoop();
        Vertex(x,y+2*dy*size);
        Vertex(x+size,y);
        Vertex(x+.3*size,y);
        Vertex(x+.3*size,y-2*dy*size);
        Vertex(x-.3*size,y-2*dy*size);
        Vertex(x-.3*size,y);
        Vertex(x-size,y);
        RenderEnd();
    }
#endif
}

static bool s_globalRepeat = false;

#ifndef DEDICATED
static bool disphelp=false;
static REAL lastkey;
#endif

// inhibit console newline display while in a menu, it causes flickering
static bool su_inMenu = false;
bool uMenu::MenuActive()
{
    return su_inMenu;
}
static rNoAutoDisplayAtNewlineCallback su_noNewline( uMenu::MenuActive );
// static rSmallConsoleCallback su_smallConsole( su_InMenu );

#ifndef DEDICATED
int uMenu::ShortcutForItem(int itemIndex) const
{
    const int sk = items.Len() - itemIndex;
    return ( sk >= 1 && sk <= 9 ) ? sk : 0;
}

int uMenu::ItemAt(REAL mx, REAL my)
{
    REAL rowHalf = RclTheme() ? uRclTheme::RowHalfH() : text_height * 0.55f;
    REAL left = RclTheme() ? uRclTheme::MenuLeft() : -0.95f;
    REAL right = RclTheme() ? uRclTheme::MenuRight() : 0.95f;

    (void)rowHalf;
    if ( mx < left || mx > right )
        return -1;

    int best = -1;
    REAL bestDist = 1e9f;
    for ( int i = 0; i < items.Len(); ++i )
    {
        const REAL cy = ItemDrawY( i ) + ItemRowHalf( i );
        const REAL dist = fabsf( my - cy );
        if ( dist < bestDist )
        {
            bestDist = dist;
            best = i;
        }
    }

    if ( best < 0 || bestDist > ItemRowHalf( best ) )
        return -1;
    return best;
}

REAL uMenu::ItemDrawY(int itemIndex)
{
    return YPos( itemIndex );
}

REAL uMenu::ItemRowHalf(int itemIndex)
{
    (void)itemIndex;
    REAL h = RclTheme() ? uRclTheme::TextH() : text_height;
    return h * 0.55f;
}

REAL uMenu::ShortcutGutterX() const
{
    if ( RclTheme() )
        return uRclTheme::MenuLeft() - 0.07f;
    return -0.90f * rTextField::AspectWidthMultiplier();
}

void uMenu::DrawItemShortcut(REAL y, int shortcutNum, REAL alpha)
{
    if ( shortcutNum < 1 || shortcutNum > 9 )
        return;

    tString label;
    label << "[" << shortcutNum << "]";

    const REAL w = text_width * 0.52f * rTextField::AspectWidthMultiplier();
    const REAL h = text_height * 0.72f;

    rTextField::SetDefaultColor( tColor( 0.5f, 0.55f, 0.65f, alpha ) );
    rTextField::SetBlendColor( tColor( 1, 1, 1, alpha ) );
    ::DisplayText( ShortcutGutterX(), y, w, h, label, -1 );
}

void uMenu::ActivateSelected()
{
    s_globalRepeat = false;
    try
    {
        su_inMenu = false;
        items[selected]->Enter();
    }
    catch ( tException const & e )
    {
        uMenu::SetIdle(NULL);
        tConsole::Message( e.GetName(), e.GetDescription(), 20 );
    }
#ifdef _MSC_VER
#pragma warning ( disable : 4286 )
    catch ( tGenericException const & e )
    {
        try
        {
            tConsole::Message( e.GetName(), e.GetDescription(), 20 );
        }
        catch (...)
        {
        }
    }
#endif
    su_inMenu = true;
    s_globalRepeat = false;
    lastkey = tSysTimeFloat();
}
#endif

void uMenu::OnEnter(){
#ifndef DEDICATED
    bool localRepeat = false;
    float nextrepeat = 0.0f;
    static const float repeatdelay = 0.2f;
    static const float repeatrateStart  = 0.2f;
    static const float repeatrateMin  = 0.05f;
    static float repeatrate  = repeatrateStart;
    SDL_Event tEventRepeat;
#else
    return;
#endif

    // delete stuck keys, maybe a menu item catches key release events.
    su_ClearKeys();

    uCallbackMenuEnter::MenuEnter();
    su_inMenu = true;

#ifndef DEDICATED
    if ( sr_window )
    {
        menu_saved_relative_mouse = SDL_GetWindowRelativeMouseMode( sr_window );
        SDL_SetWindowRelativeMouseMode( sr_window, false );
        SDL_ShowCursor();
    }
#endif

    if (items.Len()<=0)
        return;

    exitFlag=0;
    yOffset=menuTop;
    hoverPending_ = -1;
    hoverPendingSince_ = 0;
    REAL lastt=0;
    REAL ts=0;
    bool snapScroll = false;

#ifndef DEDICATED
    REAL savedMenuTop = menuTop;
    REAL savedMenuBot = menuBot;
    REAL savedCenter = center;
    if ( RclTheme() )
    {
        menuTop = ( rclLayout_ == uRclLayout_Full ) ? uRclTheme::FullMenuTop() : uRclTheme::MenuTop();
        menuBot = ( rclLayout_ == uRclLayout_Full ) ? uRclTheme::FullMenuBot() : uRclTheme::MenuBot();
        center = uRclTheme::MenuLabelX();
        yOffset = menuTop;
    }
#endif

#ifndef DEDICATED
    lastkey=tSysTimeFloat();
    static const REAL timeout=.5;
#endif

    while (!exitFlag && !quickexit && !exitToMain){
        st_DoToDo();
        tAdvanceFrame();

        ts=tSysTimeFloat()-lastt;
        lastt=tSysTimeFloat();
        if (ts>.2) ts=.2;

        if(snapScroll)
        {
            if(ts * 30 < 1)
                snapScroll = false;
        }
        else
        {
            if(ts * 15 > 1)
                snapScroll = true;
        }
        auto scrollBy = [this, snapScroll, ts](REAL delta)
        {
            if(snapScroll || fabsf(delta) < 1E-6)
            {
                yOffset += delta;
            }
            else
            {
                // almost standard exponential decay; the proximity factor makes it
                // approach the target position like t -> t^2 for negative t
                REAL proximity = std::min(1.0f, 10.0f * sqrtf(fabsf(delta)));
                REAL speed = std::min(1.0f, ts * 6 / proximity);
                yOffset += speed * delta;
            }
        };

        menuentries=items.Len();

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;

#ifndef DEDICATED
        {
            SDL_Event tEvent;
            uInputProcessGuard inputProcessGuard;
            while (!exitFlag && !quickexit && !exitToMain && su_GetSDLInput(tEvent))
            {
                REAL entertime = tSysTimeFloat();

                switch (tEvent.type)
                {
                case SDL_EVENT_KEY_DOWN:
                    if ( tEvent.key.key == SDLK_UNKNOWN )
                    {
                        // don't repeat unknown syms. They come from multi-key compositions and
                        // don't send keyup events when released.
                        break;
                    }
                    localRepeat = s_globalRepeat = true;
                    memcpy( &tEventRepeat, &tEvent, sizeof( SDL_Event ) );
                    nextrepeat = tSysTimeFloat() + repeatdelay;
                    break;
                case SDL_EVENT_KEY_UP:
                    localRepeat = s_globalRepeat = false;
                    repeatrate = repeatrateStart;
                    break;
                }

                this->HandleEvent( tEvent );

                // quit shortcut
                if ( quickexit )
                    break;

                if ( tSysTimeFloat() - entertime > 1 )
                {
                    localRepeat = s_globalRepeat = false;
                }
            }

            if ( localRepeat && s_globalRepeat && tSysTimeFloat() > nextrepeat )
            {
                this->HandleEvent( tEventRepeat );
                nextrepeat = tSysTimeFloat() + repeatrate;
                repeatrate *= .71;
                if ( repeatrate < repeatrateMin )
                    repeatrate = repeatrateMin;
            }
        }

        // we're about to render, last chance to make changes to the menu
        OnRender();

        // clamp cursor
        if (selected < 0 )
            selected = 0;
        if ( selected >= items.Len())
            selected = items.Len()-1;
#endif
        // quit shortcut
        if ( quickexit )
            break;


        menuBot=-1+spaceBelow;

        const REAL border=.3;
        const REAL smallborder=.1;

        menuentries=items.Len();

        REAL ysel=YPos(selected);

        {
            REAL scrollUp = menuBot+border-ysel;
            if(scrollUp > 0)
                scrollBy(scrollUp);
        }
        {
            REAL scrollDown = menuTop-border-ysel;
            if(scrollDown < 0)
                scrollBy(scrollDown);
        }

        if (ysel<menuBot)
            yOffset+=(menuBot-ysel);

        if (ysel>menuTop-smallborder)
            yOffset+=(menuTop-smallborder-ysel);

        if (YPos(0)>menuBot+smallborder)
            yOffset+=menuBot+smallborder-YPos(0);

        if (YPos(menuentries-1)<menuTop-smallborder)
            yOffset+=menuTop-smallborder-YPos(menuentries-1);

#ifndef DEDICATED
        sr_ResetRenderState(true);
        if ( RclTheme() )
        {
            if ( rclLayout_ == uRclLayout_Full )
            {
                uRclTheme::DrawBackground();
                uRclTheme::DrawFullChrome();
            }
            else
            {
                if ( idle )
                    GenericBackground();
                else
                    uRclTheme::DrawBackground();
                uRclTheme::DrawGridOverlay( .25f );
                uRclTheme::DrawCornerPluses( uRclTheme::MenuLeft(), uRclTheme::MenuRight(),
                                             menuBot - .04f, menuTop + .06f );
                uRclTheme::DrawHeaderTitle( tString( title ) );
            }
        }
        else
        {
            items[selected]->RenderBackground();
        }

        if (selected >= items.Len()) selected = items.Len()-1;
        if (items.Len() <= 0)
            return;

        if (sr_glOut && !exitFlag && !quickexit){
            if ( RclTheme() )
            {
                for ( int i = items.Len() - 1; i >= 0; --i )
                {
                    REAL y = YPos(i);
                    if ( y <= menuBot + .1 || y >= menuTop - .1 )
                        continue;
                    bool sel = ( i == selected );
                    if ( sel )
                        uRclTheme::DrawBracketSelection( uRclTheme::MenuLeft() + .01f,
                                                         uRclTheme::MenuRight(),
                                                         y );
                    items[i]->Render( uRclTheme::MenuLabelX(), y, 1, sel );
                    if ( ShowItemShortcuts() )
                        DrawItemShortcut( y, ShortcutForItem( i ), 1 );
                    if ( uRclTheme::ShowInlineDesc( sel ) )
                    {
                        tString desc = uRclTheme::FirstLine( items[i]->Help() );
                        if ( desc.Len() > 0 && items[i]->SpaceRight() <= 0 )
                            uRclTheme::DrawDesc( uRclTheme::MenuDescX(), y, desc, sel, 1 );
                    }
                }
                disphelp = false;
            }
            else
            {
            for ( int i = items.Len() - 1; i >= 0; --i )
            {
                REAL y = YPos(i);
                if ( y <= menuBot + .1 || y >= menuTop - .1 )
                    continue;
                REAL alpha = 1;
                if ( i == selected )
                    items[i]->Render( center, y, 1, true );
                else
                    items[i]->Render( center, y, alpha, false );
                if ( ShowItemShortcuts() )
                    DrawItemShortcut( y, ShortcutForItem( i ), alpha );
            }

            rTextField::SetDefaultColor( tColor(1,1,1,1) );
            rTextField::SetBlendColor( tColor(1,1,1,1) );

            Color(.6,.6,1,1);
            ::DisplayText(0,menuTop+text_height*titlefac
                          ,text_width*titlefac*rTextField::AspectWidthMultiplier(),text_height*titlefac,
                          title,0);

            glDisable(GL_TEXTURE_2D);
            Color(1,.2,.2,.5);
            if (YPos(0)<menuBot+smallborder && (int(tSysTimeFloat()))%2)
                arrow(.9,menuBot+.1,-1,.05);
            if (YPos(menuentries-1)>menuTop && (int(tSysTimeFloat())+1)%2)
                arrow(.9,menuTop,1,.05);

            REAL helpAlpha = tSysTimeFloat()-lastkey-timeout;
            if( helpAlpha > 1 )
            {
                helpAlpha = 1;
            }
            
            disphelp = helpAlpha > 0;
            if ( items[selected]->DisplayHelp( disphelp, menuBot, helpAlpha ) )
            {
                if (sr_alphaBlend)
                    glColor4f(1,.8,.8, helpAlpha );
                else
                    Color(helpAlpha,
                          .8*helpAlpha,
                          .8*helpAlpha);

                rTextField c(-.95f,menuBot-.04f,rCWIDTH_NORMAL*rTextField::AspectWidthMultiplier());
                c.SetWidth(static_cast<int>((1.9f-items[selected]->SpaceRight())/c.GetCWidth()));
                c << items[selected]->Help();
            }
            }
        }
        else
#endif
            if ( !sr_glOut )
            {
                tDelay( 10000 );
            }

#ifndef DEDICATED
        rSysDep::SwapGL();
        rSysDep::ClearGL();
#endif
    }

    s_globalRepeat = false;

#ifndef DEDICATED
    if ( sr_window )
    {
        if ( menu_saved_relative_mouse )
            SDL_SetWindowRelativeMouseMode( sr_window, true );
        else
            SDL_HideCursor();
    }
#endif

    uCallbackMenuLeave::MenuLeave();
    su_inMenu = false;

#ifndef DEDICATED
    if ( RclTheme() )
    {
        menuTop = savedMenuTop;
        menuBot = savedMenuBot;
        center = savedCenter;
    }
#endif
}

void uMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    if (!items[selected]->Event(event))
    {
        switch ( event.type )
        {
        case SDL_EVENT_MOUSE_MOTION:
        {
            static const double hoverDebounce = 0.07;

            REAL mx, my;
            MenuScreenToNormalized( event.motion.x, event.motion.y, mx, my );
            int hit = ItemAt( mx, my );
            if ( hit < 0 )
            {
                hoverPending_ = -1;
                return;
            }
            if ( hit != hoverPending_ )
            {
                hoverPending_ = hit;
                hoverPendingSince_ = tSysTimeFloat();
                return;
            }
            if ( selected != hit && tSysTimeFloat() - hoverPendingSince_ >= hoverDebounce )
            {
                selected = hit;
                items[selected]->DisplayHelp( false, 0, 0.0f );
            }
            return;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if ( event.button.button == 1 )
            {
                REAL mx, my;
                MenuScreenToNormalized( event.button.x, event.button.y, mx, my );
                int hit = ItemAt( mx, my );
                if ( hit >= 0 )
                {
                    hoverPending_ = hit;
                    hoverPendingSince_ = tSysTimeFloat();
                    selected = hit;
                    ActivateSelected();
                }
            }
            return;
        default:
            break;
        }

        if ( event.type == SDL_EVENT_KEY_DOWN &&
             event.key.key >= SDLK_1 && event.key.key <= SDLK_9 )
        {
            if ( !disphelp )
                lastkey = tSysTimeFloat();
            int num = event.key.key - SDLK_1 + 1;
            int idx = items.Len() - num;
            if ( idx >= 0 && idx < items.Len() )
            {
                selected = idx;
                ActivateSelected();
            }
            return;
        }

        switch (event.type){
        case SDL_EVENT_KEY_DOWN:
        {
            if (!disphelp)
                lastkey=tSysTimeFloat();
            switch (event.key.key){

            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                Exit();
                break;

            case(SDLK_UP):
                lastkey=tSysTimeFloat();
                selected++;
                if (selected>=items.Len())
                {
                    if (wrap)
                        selected=0;
                    else
                        selected=items.Len()-1;
                }
                items[selected]->DisplayHelp(false, 0, 0.0f);
                break;

            case(SDLK_DOWN):
                lastkey=tSysTimeFloat();
                selected--;
                if (selected<0)
                {
                    if (wrap)
                        selected=items.Len()-1;
                    else
                        selected=0;
                }
                items[selected]->DisplayHelp(false, 0, 0.0f);
                break;

            case(SDLK_LEFT):
                items[selected]->LeftRight(-1);
                break;
            case(SDLK_RIGHT):
                items[selected]->LeftRight(1);
                break;

            case(SDLK_SPACE):
                        case(SDLK_KP_ENTER):
                            case(SDLK_RETURN):
                                    s_globalRepeat = false;
                ActivateSelected();
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                break;

            default:
                // let the input subsystem handle events for later processing
                su_HandleEvent( event, true );
                break;
            }
        }
        break;
        default:
            // let the input subsystem handle events for later processing
            su_HandleEvent( event, true );
            break;
        }
    }

    su_inMenu = true;
#endif
}

#ifndef DEDICATED
static bool s_idleBackground = false;
#endif

// paints a nice background
void uMenu::GenericBackground(REAL top){
#ifndef DEDICATED
    if (idle)
    {
        s_idleBackground = true;

        try
        {
            // throw tGenericException("test"); // (test exception throw to see if error handling works right)
            (*idle)();

            // render the console so it appears behind the menu
            if( sr_con.autoDisplayAtSwap )
            {
                sr_con.Render();
            }

            // fade everything rendered so far to black
            if( sr_alphaBlend )
            {
                sr_ResetRenderState(true);

                double time = tSysTimeFloat();
                static double lastTime = time - 100;
                static REAL alpha = 0.0f;
                double timePassed = time - lastTime;
                if( time - lastTime > 1.0 )
                {
                    alpha = 0.0f;
                }
                else
                {
                    alpha += timePassed;
                    static const REAL limit = .5;

                    if( alpha > limit )
                    {
                        alpha = limit;
                    }
                }
                lastTime = time;

                RenderEnd();
                glColor4f(0, 0, 0, alpha);
                glRectf(-1,-1,1,top);
            }
        }
        catch ( ... )
        {
            s_idleBackground = false;

            // the idle background function is broken. Disable it and rethrow.
            idle = 0;
            throw;
        }
        s_idleBackground = false;
    }
    else if (sr_glOut){
        uCallbackMenuBackground::MenuBackground();
    }
    else
        tDelay(100000);
#endif
    sr_ResetRenderState(true);
}

// marks the menu for exit
void uMenu::OnExit(){
    exitFlag=1;
}

//! called every frame before the menu is rendered
void uMenu::OnRender()
{
}

// *****************************************************

// *******************************************************************************************
// *
// *   SetColor
// *
// *******************************************************************************************
//!
//!        @param  selected    flag indicating whether the menu item is currently selected
//!        @param  alpha       transparency to use
//!
// *******************************************************************************************

void uMenuItem::SetColor( bool selected, REAL alpha )
{
    if ( menu->RclTheme() )
    {
        if ( selected )
            rTextField::SetDefaultColor( tColor( 1, 1, 0, alpha ) );
        else
            rTextField::SetDefaultColor( tColor( .92f, .92f, .92f, alpha ) );
        rTextField::SetBlendColor( tColor( 1, 1, 1, alpha ) );
        return;
    }

    //   rTextField::SetBlendColor( tColor(.8+.2*sin(time),.3-.1*sin(time),.3-.1*sin(time),alpha) );
    rTextField::SetDefaultColor( tColor(1,1,1,alpha) );

    if (selected)
    {
        REAL time=tSysTimeFloat()*10;
        REAL intensity = 1+.3*sin(time);
        rTextField::SetDefaultColor( tColor(.8,.3,.3,alpha) );
        rTextField::SetBlendColor( tColor(intensity,intensity,intensity,alpha) );
    }
}

void uMenuItem::DisplayText(REAL x,REAL y,const char *text,
                            bool selected,REAL alpha,
                            int center,int c,int cp, rTextField::ColorMode colorMode ){
#ifndef DEDICATED
    if (sr_glOut){
        SetColor( selected, alpha );

        REAL tw = menu->RclTheme() ? uRclTheme::TextW() : text_width;
        REAL th = menu->RclTheme() ? uRclTheme::TextH() : text_height;

        tw *= (REAL(sr_screenHeight)/sr_screenWidth)*(4.0/3.0);

        if ( menu->RclTheme() )
        {
            ::DisplayText( x, y, tw, th, text, -1, 0, 0, rTextField::COLOR_IGNORE );
            return;
        }

        int align = center;

        ::DisplayText(x,y,tw,th,text,align,c,cp, colorMode );
    }
#endif
}

void uMenuItem::DisplayTextSpecial(REAL x,REAL y,const char *text,
                                   bool selected,
                                   REAL alpha,int center){
#ifndef DEDICATED
    if ( menu->RclTheme() )
    {
        tString label;
        label << "> " << text;
        DisplayText( x, y, label, selected, alpha, center ? center : 1 );
        return;
    }
#endif

    DisplayText(x,y,text,selected,alpha,center);
}

// *************************************

const tOutput& uMenuItemExit::ExitText()
{
    static tOutput exitText("$menuitem_exit_text");

    return exitText;
}

const tOutput& uMenuItemExit::ExitHelp()
{
    static tOutput exitHelp("$menuitem_exit_help");

    return exitHelp;
}

// *************************************

void uMenuItemToggle::NewChoice(uSelectItem<bool> *){}
void uMenuItemToggle::NewChoice(const char *,bool ){}

#ifdef SLOPPYLOCALE
uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const char *tit,
                                 const char *help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}
#endif

uMenuItemToggle::uMenuItemToggle(uMenu *m,
                                 const tOutput& tit,
                                 const tOutput& help,
                                 bool &targ)
        :uMenuItemSelection<bool>(m,tit,help,targ){
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_on","",true);
    uMenuItemSelection<bool>::NewChoice("$menuitem_toggle_off","",false);
}

uMenuItemToggle::~uMenuItemToggle(){}

void uMenuItemToggle::LeftRight(int){
    select=1-select;
    *target=!(*target);
}

void uMenuItemToggle::Enter(){
    LeftRight(0);
}
// *****************************************
//               Integer Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemInt::uMenuItemInt
(uMenu *m,const char *tit,const char *help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemInt::uMenuItemInt
(uMenu *m,const tOutput &tit,const tOutput &help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemInt::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemInt::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}

// *****************************************
//               Float Choose
// *****************************************

#ifdef SLOPPYLOCALE
uMenuItemReal::uMenuItemReal
(uMenu *m,const char *tit,const char *help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}
#endif

uMenuItemReal::uMenuItemReal
(uMenu *m,const tOutput &tit,const tOutput &help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}


void uMenuItemReal::LeftRight(int dir){
    target+=dir*Step;
    if (target<Min) target=Min;
    if (target>Max) target=Max;
}

void uMenuItemReal::Render(REAL x,REAL y,REAL alpha,
                          bool selected){
    DisplayText(x-.02,y,title,selected,alpha,1);

    tString s;
    s << target;
    DisplayText(x+.02,y,s,selected,alpha,-1);
}


// *****************************************************

uMenuItemString::uMenuItemString(uMenu *M,
                                 const tOutput& de,
                                 const tOutput& help,
                                 tString &c,
                                 int maxLength )
        :uMenuItem(M,help),description(de),content(&c),cursorPos(0), maxLength_( maxLength ){
    int len=content->Len();
    if (len==0 || (*content)(len-1)!=0)
        (*content)[len]=0;
    cursorPos=content->Len()-1;
    colorMode_ = rTextField::COLOR_SHOW;
}

void uMenuItemString::Render(REAL x,REAL y,
                             REAL alpha,bool selected){
#ifndef DEDICATED
    static int counter=0;
    counter++;

    int cmode=0;
    if (selected){
        cmode=1;
        if (counter & 32) cmode=2;
    }

    // unslected items with COLOR_SHOW should be rendered with COLOR_USE
    rTextField::ColorMode colorMode = colorMode_;
    if ( colorMode == rTextField::COLOR_SHOW && !selected )
        colorMode = rTextField::COLOR_USE;

    DisplayText(x-.02,y,description,selected,alpha,1);
    DisplayText(x+.02,y,&((*content)[0]),selected,alpha,-1,cmode,cursorPos, colorMode );
#endif
}

bool uMenuItemString::Event(SDL_Event &e){
#ifndef DEDICATED
    if (e.type == SDL_EVENT_TEXT_INPUT)
    {
        bool inserted = false;
        for (int i = 0; e.text.text[i] && content->Len() < maxLength_; ++i)
        {
            unsigned char c = static_cast<unsigned char>( e.text.text[i] );
            if ( c < 32 )
                continue;

            for (int j=content->Len()-1;j>=cursorPos;j--)
                (*content)[j+1]=(*content)[j];

            (*content)[content->Len()-1]='\0';
            (*content)[cursorPos]=c;
            cursorPos++;
            inserted = true;
        }

        if (cursorPos<0)    cursorPos=0;
        if (cursorPos > content->Len()-1) cursorPos=content->Len()-1;
        return inserted;
    }

    if (e.type!=SDL_EVENT_KEY_DOWN)
        return false;

    bool ret=true;
    auto & c = e.key;
    SDL_Keymod mod = c.mod;
    bool moveWordLeft, moveWordRight, deleteWordLeft, deleteWordRight, moveBeginning, moveEnd, killForwards, doPaste;
    moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards = doPaste = false;

#if defined (MACOSX)
    // For moving over/deleting words
    if (mod & SDL_KMOD_ALT) {
        if (c.key == SDLK_LEFT) {
            moveWordLeft = true;
        }
        else if (c.key == SDLK_RIGHT) {
            moveWordRight = true;
        }
        else if (c.key == SDLK_DELETE) {
            deleteWordRight = true;
        }
        else if (c.key == SDLK_BACKSPACE) {
            deleteWordLeft = true;
        }
    }
    // For moving to extremes of the line
    else if (mod & SDL_KMOD_GUI) {
        if (c.key == SDLK_LEFT) {
            moveBeginning = true;
        }
        else if (c.key == SDLK_RIGHT) {
            moveEnd = true;
        }
        else if (c.key == SDLK_V) {
            doPaste = true;
        }
    }
    // Linux and Windows
#else
    // Word operations
    if (mod & SDL_KMOD_CTRL) {
        if (c.key == SDLK_LEFT) {
            moveWordLeft = true;
        }
        else if (c.key == SDLK_RIGHT) {
            moveWordRight = true;
        }
        else if (c.key == SDLK_DELETE) {
            deleteWordRight = true;
        }
        else if (c.key == SDLK_BACKSPACE) {
            deleteWordLeft = true;
        }
    }
    else if (c.key == SDLK_HOME) {
        moveBeginning = true;
    }
    else if (c.key == SDLK_END) {
        moveEnd = true;
    }
#endif
    // "bash" keys
    if (mod & SDL_KMOD_CTRL) {
        if (c.key == SDLK_A) {
            moveBeginning = true;
        }
        else if (c.key == SDLK_E) {
            moveEnd = true;
        }
        else if (c.key == SDLK_K) {
            killForwards = true;
        }
        else if (c.key == SDLK_V) {
            doPaste = true;
        }
    }
    // moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards

    if (moveWordLeft) {
        cursorPos += content->PosWordLeft(cursorPos);
    }
    else if (moveWordRight) {
        cursorPos += content->PosWordRight(cursorPos);
    }
    else if (deleteWordLeft) {
        cursorPos += content->RemoveWordLeft(cursorPos);
    }
    else if (deleteWordRight) {
        content->RemoveWordRight(cursorPos);
    }
    else if (moveBeginning) {
        cursorPos = 0;
    }
    else if (moveEnd) {
        cursorPos = content->Len()-1;
    }
    else if (killForwards) {
        content->RemoveSubStr(cursorPos,content->Len()-1-cursorPos);
    }
    else if (c.key == SDLK_LEFT) {
        if (cursorPos > 0) {
            cursorPos--;
        }
    }
    else if (c.key == SDLK_RIGHT) {
        if (cursorPos < content->Len()-1) {
            cursorPos++;
        }
    }
    else if (c.key == SDLK_DELETE) {
        if (cursorPos < content->Len()-1) {
            content->RemoveSubStr(cursorPos,1);
        }
    }
    else if (c.key == SDLK_BACKSPACE) {
        if (cursorPos > 0) {
            content->RemoveSubStr(cursorPos,-1);
            cursorPos--;
        }
    }
    else if (c.key == SDLK_KP_ENTER || c.key == SDLK_RETURN) {
        ret = false;
        //        c.key = SDLK_DOWN;
    }
    else if (doPaste) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            for (int i = 0; clip[i] && content->Len() < maxLength_; ++i) {
                unsigned char ch = static_cast<unsigned char>(clip[i]);
                if (ch < 32) continue;
                for (int j = content->Len()-1; j >= cursorPos; j--)
                    (*content)[j+1] = (*content)[j];
                (*content)[content->Len()-1] = '\0';
                (*content)[cursorPos] = ch;
                cursorPos++;
            }
            SDL_free(clip);
        }
    }
    else {
        // ponytail: text comes through SDL_EVENT_TEXT_INPUT in SDL2, consume text-like keydowns so they don't trigger binds
        ret = !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
              && (c.key == SDLK_UNKNOWN || (c.key >= SDLK_SPACE && c.key < SDLK_DELETE));
    }

    if (cursorPos<0)    cursorPos=0;
    if (cursorPos > content->Len()-1) cursorPos=content->Len()-1;

    return ret;
#else
    return false;
#endif
}

uMenuItemStringWithHistory::uMenuItemStringWithHistory(uMenu *M,const tOutput& desc, const tOutput& help,tString &c, int maxLength, std::deque<tString> &history, int limit ):
        uMenuItemString(M, desc,help,c, maxLength ),
        m_History(history),
        m_HistoryPos(0),
        m_HistoryLimit(limit)
{
    m_History.push_front(tString());
}

uMenuItemStringWithHistory::~uMenuItemStringWithHistory()
{
    if (content->Len() > 1)
    {
        for (std::deque<tString>::iterator i=m_History.begin(); i!=m_History.end(); ++i)
        {
            if (*i == *content)
            {
                m_History.erase(i);
                break;
            }
        }
        m_History.front() = *content;
    }
    else
    {
        m_History.pop_front();
    }
    if (m_History.size() > m_HistoryLimit)
        m_History.pop_back();
}

bool uMenuItemStringWithHistory::Event(SDL_Event &e)
{
    // flag indicating that the event was handled
    bool ret = false;
#ifndef DEDICATED
    if (e.type == SDL_EVENT_KEY_DOWN
            && ((e.key.key == SDLK_UP)
                || (e.key.key == SDLK_P && (e.key.mod & SDL_KMOD_CTRL))))
    {
        if (m_History.size() - 1 > m_HistoryPos)
        {
            // the new entry... save it before overwriting it
            if (m_HistoryPos == 0)
                m_History.front() = *content;
            m_HistoryPos++;
            *content = m_History[m_HistoryPos];
            cursorPos = content->Len() - 1;
        }

        ret = true;
    }
    else if (e.type == SDL_EVENT_KEY_DOWN
             && ((e.key.key == SDLK_DOWN)
                 || (e.key.key == SDLK_N && (e.key.mod & SDL_KMOD_CTRL))))
    {
        if (m_HistoryPos > 0)
        {
            m_HistoryPos--;
            *content = m_History[m_HistoryPos];
            cursorPos = content->Len() - 1;
        }

        ret = true;
    }

    // clamp cursor position
    if (cursorPos<0)
        cursorPos=0;
    if (cursorPos > content->Len() - 1)
        cursorPos=content->Len() - 1;
#endif

    // return result or delegate
    return ret || uMenuItemString::Event(e);
}

// *****************************************************
//  Submenu
// *****************************************************


uMenuItemSubmenu::uMenuItemSubmenu(uMenu *M,
                                   uMenu *s,
                                   const tOutput& help)
        :uMenuItem(M,help),submenu(s){}


void uMenuItemSubmenu::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,submenu->title,selected,alpha,0);
}

void uMenuItemSubmenu::Enter(){
    submenu->Enter();
}

// *****************************************************
//  action
// *****************************************************


uMenuItemAction::uMenuItemAction(uMenu *M,
                                 const tOutput& n, const tOutput& help )
        :uMenuItem(M,help),name_(n){}


void uMenuItemAction::Render(REAL x,REAL y,REAL alpha,bool selected){
    DisplayTextSpecial(x,y,name_,selected,alpha,0);
}


void uMenuItemAction::Enter()
{
    tASSERT( 0 )
}


// *****************************************************
//  function
// *****************************************************


uMenuItemFunction::uMenuItemFunction(uMenu *M,
                                     const tOutput& n, const tOutput& help,
                                     FUNCPTR f)
        :uMenuItemAction(M,n,help),func(f){}

void uMenuItemFunction::Enter(){
    (*func)();
}



uMenuItemFunctionInt::uMenuItemFunctionInt(uMenu *M,
        const tOutput& n,
        const tOutput& help,
        INTFUNCPTR f,int a)
        :uMenuItemAction(M,n,help),func(f),arg(a){}


void uMenuItemFunctionInt::Enter(){
    (*func)(arg);
}

// *****************************************************
//  File Selection (added by k)
// *****************************************************

void uMenuItemFileSelection::NewChoice( uSelectItem<bool> * ) {}
void uMenuItemFileSelection::NewChoice( char *, bool ) {}

void uMenuItemFileSelection::Reload()
{
    Clear();
    if ( defaultFileName_.Len() > 1 && defaultFilePath_.Len() > 1 )
        AddFile( defaultFileName_, defaultFilePath_, formatName_ );
    LoadDirectory( dir_, fileSpec_, formatName_ );
}

void uMenuItemFileSelection::LoadDirectory( const char *dir, const char *fileSpec,
        bool formatName /*= true*/ )
{
    tArray <tString> files;
    tString filePath ( dir );
    tDirectories::GetFiles( tString( dir ), tString( fileSpec ), files, getFilesFlag_ );
    for ( int i = 0; i < files.Len(); i++ )
    {
        AddFile( files( i ), filePath + files( i ), formatName );
    }
}

void uMenuItemFileSelection::AddFile( const char *fileName, const char *filePath,
                                      bool formatName /*= true*/ )
{
    tString menuName ( fileName );
    if ( formatName )
        tDirectories::FileNameToMenuName( fileName, menuName );
    uMenuItemSelection<tString>::NewChoice( menuName, "", tString( filePath ) );
}

// *****************************************************
// Menu Enter/Leave-Callback
// *****************************************************

static tCallback *enter_anchor=NULL,*leave_anchor=NULL, *background_anchor=NULL;

uCallbackMenuEnter::uCallbackMenuEnter(VOIDFUNC *f)
        :tCallback(enter_anchor,f){}

void uCallbackMenuEnter::MenuEnter(){
    Exec(enter_anchor);
}

uCallbackMenuLeave::uCallbackMenuLeave(VOIDFUNC *f)
        :tCallback(leave_anchor,f){}

void uCallbackMenuLeave::MenuLeave(){
    Exec(leave_anchor);
}

uCallbackMenuBackground::uCallbackMenuBackground(VOIDFUNC *f)
        :tCallback(background_anchor,f){}

void uCallbackMenuBackground::MenuBackground(){
    Exec(background_anchor);
}

// poll input, return true if ESC was pressed
bool uMenu::IdleInput( bool processInput )
{
#ifndef DEDICATED
    if( !processInput )
    {
        SDL_PumpEvents();
        return uMenu::quickexit != uMenu::QuickExit_Off;
    }

    SDL_Event event;
    uInputProcessGuard inputProcessGuard;
    while (!s_idleBackground && su_GetSDLInput(event))
    {   
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key)
            {
            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                return true;
                break;
            default:
                break;
            }   
        default:
            break;
        }
    }   

    return uMenu::quickexit != uMenu::QuickExit_Off;
#endif

    return false;
}

// return value: false only if the user pressed ESC
bool uMenu::Message(const tOutput& message, const tOutput& interpretation, REAL to){
    bool ret = true;
#ifdef DEDICATED
    con << message << ":\n";
    con << interpretation << '\n';
#else

    // reload textures (just in case)
    rITexture::UnloadAll();

    bool textOutBack = sr_textOut;
    sr_textOut = false;

    FUNCPTR idle_back = idle;
    uMenu::SetIdle(NULL);

    rTextField::SetDefaultColor( tColor(1,1,1,1) );
    rTextField::SetBlendColor( tColor(1,1,1,1) );

    rSysDep::ClearGL();
    rSysDep::SwapGL();
    if (sr_glOut)
    {
        rFont::s_defaultFont.Select();
        rFont::s_defaultFontSmall.Select();
    }
    rSysDep::ClearGL();
    rSysDep::SwapGL();

    REAL timeout = tSysTimeFloat() + to;
    SDL_Event tEvent;

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    {
        uInputProcessGuard inputProcessGuard;

        unsigned offset = 0; //amount of scrolling taking place
        //convert to an array for scrolling
        tString interpretationString;
        interpretationString << interpretation << "\n";
        std::vector<tString> lines;
        int lastNewline = 0;
        for (int i = 0; i < interpretationString.Len() - 1; ++i) {
            if (interpretationString[i] == '\n' && i != 0) {
                lines.push_back(interpretationString.SubStr(lastNewline, i - lastNewline));
                lastNewline = i + 1;
            }
        }
        while (  !quickexit &&
                 (to < 0 || tSysTimeFloat() < timeout)){
            //while(  !quickexit && ( !su_GetSDLInput(tEvent) || tEvent.type!=SDL_EVENT_KEY_DOWN) &&
            //        (to < 0 || tSysTimeFloat() < timeout)){
            if ( su_GetSDLInput(tEvent) && tEvent.type==SDL_EVENT_KEY_DOWN) {
                switch (tEvent.key.key) {
                case SDLK_UP:
                    if (offset > 0)
                        offset -= 1;
                    continue;
                case SDLK_DOWN:
                    offset += 1;
                    continue;
                case SDLK_ESCAPE:
                    ret = false;
                    break;
                default:
                    break;
                }
                break;
            }
            if ( sr_glOut )
            {
                sr_ResetRenderState(true);
                rViewport::s_viewportFullscreen.Select();

                rSysDep::ClearGL();

                GenericBackground();

                //16*3/640.0, 32*3/480.0
                REAL w=0.1*(REAL(sr_screenHeight)/sr_screenWidth),h=0.2;

                //REAL middle=-.6;

                tString m(message);
                int len = m.Len();
                if (w * len > 1.8)
                {
                    h = h * 1.8 / (w * len);
                    w = 1.8 / len;
                }

                Color(1,1,1);
                DisplayText(0,.8,w,h, message);

                //16/640.0
                w = 1/30.0*(REAL(sr_screenHeight)/sr_screenWidth);
                h = 32/480.0;

                if (offset >= lines.size()) offset = lines.size() - 1;
                {
                    rTextField c(-.8,.6, w, h);

                    for (unsigned i = offset; i < lines.size(); ++i)
                        c << lines[i] << "\n";
                }
            }
            rSysDep::SwapGL();
            tAdvanceFrame();
        }
    }

    // catch some keyboard input
    {
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent)) ;
    }

    uMenu::SetIdle(idle_back);

    // reload textures (just in case)
    rITexture::UnloadAll();

    sr_textOut = textOutBack;
#endif

    return ret;
}
