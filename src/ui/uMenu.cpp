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
        :exitFlag(0),spaceBelow(.4),style_(uMenuStyle_RclPanel),
#ifndef DEDICATED
        menuMouseMode_(false),styleEnterTime_(0),mouseSelection_(-1),dragItem_(-1),
#endif
        title(t){
    if (exit_item) new uMenuItemExit(this);
    center=0;
    menuTop=.7;
    menuBot=-.7;
    yOffset=0;
    selected = 10000000;
}
#endif

uMenu::uMenu(const tOutput &t,bool exit_item)
        :exitFlag(0),spaceBelow(.4),style_(uMenuStyle_RclPanel),
#ifndef DEDICATED
        menuMouseMode_(false),styleEnterTime_(0),mouseSelection_(-1),dragItem_(-1),
#endif
        title(t){
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

REAL uMenu::YPos(int num){
    REAL const pitch = RclStyle() ? uRclTheme::RowPitch() : text_height;
    return yOffset-pitch*(menuentries-num);
}

#ifndef DEDICATED
static REAL MenuMouseY(Uint16 pixelY)
{
    if (sr_screenHeight <= 0)
        return 0;
    return 1.0f - 2.0f * pixelY / sr_screenHeight;
}

static REAL MenuMouseX(Uint16 pixelX)
{
    if (sr_screenWidth <= 0)
        return 0;
    return 2.0f * pixelX / sr_screenWidth - 1.0f;
}

// where on its track the mouse holds a slider
static REAL MenuSliderFraction(REAL mouseX)
{
    REAL const left = uRclTheme::SliderLeft();
    REAL const right = uRclTheme::SliderRight();
    if (right <= left)
        return 0;
    return std::max(0.0f, std::min(1.0f, (mouseX - left) / (right - left)));
}

// Escape and the right mouse button go back. From the main menu that would
// end the program, so there they first go to its last row, the one that quits.
void uMenu::Back()
{
    if (style_ == uMenuStyle_RclHome)
    {
        int bottom = 0;
        while (bottom < items.Len() && !items[bottom]->IsSelectable())
            ++bottom;
        if (bottom < items.Len() && selected != bottom)
        {
            selected = bottom;
            mouseSelection_ = -1;
            return;
        }
    }
    Exit();
}

void uMenu::ApplyTheme()
{
    // rows with a control need the wider page with a control column
    bool values = false;
    for (int i = items.Len()-1; i >= 0 && !values; --i)
        values = items[i]->Control() != uRclTheme::Control_None;

    uRclTheme::Layout layout = uRclTheme::Layout_Page;
    switch (style_)
    {
    case uMenuStyle_RclHome:   layout = uRclTheme::Layout_Home;   break;
    case uMenuStyle_RclFull:   layout = uRclTheme::Layout_Wide;   break;
    case uMenuStyle_RclPrompt: layout = uRclTheme::Layout_Prompt; break;
    default: break;
    }
    uRclTheme::Configure(layout, values);

    if (style_ == uMenuStyle_RclPrompt)
    {
        menuTop = uRclTheme::PromptTop();
        menuBot = uRclTheme::PromptBottom();
    }
    else
    {
        menuTop = uRclTheme::MenuTop();
        menuBot = uRclTheme::MenuBottom();
    }
    spaceBelow = 1 + menuBot;
    center = uRclTheme::LabelX();
}

REAL uMenu::ItemDrawY(int itemIndex)
{
    return YPos(itemIndex);
}

REAL uMenu::ItemRowHalf(int itemIndex)
{
    (void)itemIndex;
    if (RclStyle())
        return uRclTheme::RowHalfHeight();
    return text_height * 0.48f;
}

int uMenu::ItemAt(REAL mouseX, REAL mouseY)
{
    int best = -1;
    REAL bestDistance = 1E+30f;

    // rows end at the column's edges; beside them the pointer is over nothing
    if (RclStyle() && (mouseX < uRclTheme::RowLeft() || mouseX > uRclTheme::RowRight()))
        return -1;

    for (int i = 0; i < items.Len(); ++i)
    {
        // Match the render loop's visibility test, even when a subclass draws
        // an item at a transformed Y coordinate (the server browser does).
        const REAL layoutY = YPos(i);
        if (layoutY <= menuBot || layoutY >= menuTop || !items[i]->IsSelectable())
            continue;

        const REAL distance = fabsf(mouseY - ItemDrawY(i));
        if (distance <= ItemRowHalf(i) && distance < bestDistance)
        {
            best = i;
            bestDistance = distance;
        }
    }

    return best;
}

void uMenu::EnterMenuMouseMode()
{
    if (menuMouseMode_)
        return;

    savedCursorState_ = SDL_ShowCursor(SDL_QUERY);
    savedMouseGrab_ = SDL_WM_GrabInput(SDL_GRAB_QUERY);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(SDL_ENABLE);
    menuMouseMode_ = true;
}

void uMenu::LeaveMenuMouseMode()
{
    if (!menuMouseMode_)
        return;

    menuMouseMode_ = false;
    SDL_WM_GrabInput(static_cast<SDL_GrabMode>(savedMouseGrab_));
    SDL_ShowCursor(savedCursorState_);
}

class uMenuMouseGuard
{
public:
    explicit uMenuMouseGuard(uMenu &menu): menu_(menu)
    {
        menu_.EnterMenuMouseMode();
    }

    ~uMenuMouseGuard()
    {
        menu_.LeaveMenuMouseMode();
    }

private:
    uMenu &menu_;
};
#endif


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
void uMenu::ActivateSelected()
{
    s_globalRepeat = false;

    // Enter() may synchronously run gameplay or another menu. Restore the
    // caller's mouse state for that duration, then capture it again on return.
    LeaveMenuMouseMode();
    try
    {
        su_inMenu = false;
        items[selected]->Enter();
    }
    catch (tException const & e)
    {
        uMenu::SetIdle(NULL);

        // inform user of generic errors
        tConsole::Message( e.GetName(), e.GetDescription(), 20 );
    }
#ifdef _MSC_VER
#pragma warning ( disable : 4286 )
    // GRR. Visual C++ dones not handle generic exceptions with the above general statement.
    // A specialized version is needed. The best part: it warns about the code below being redundant.
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
    EnterMenuMouseMode();
    s_globalRepeat = false;
    lastkey = tSysTimeFloat();

    // a menu entered from here left its own layout behind
    if (RclStyle())
        ApplyTheme();
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
    uMenuMouseGuard mouseGuard(*this);
#endif

    if (items.Len()<=0)
    {
        uCallbackMenuLeave::MenuLeave();
        su_inMenu = false;
        return;
    }

#ifndef DEDICATED
    REAL const savedMenuTop = menuTop;
    REAL const savedMenuBot = menuBot;
    REAL const savedSpaceBelow = spaceBelow;
    REAL const savedCenter = center;
    if (RclStyle())
    {
        ApplyTheme();
        styleEnterTime_ = tSysTimeFloat();
    }
    dragItem_ = -1;
#endif

    exitFlag=0;
    yOffset=menuTop;
    REAL lastt=0;
    REAL ts=0;
    bool snapScroll = false;

#ifndef DEDICATED
    lastkey=tSysTimeFloat();
    static const REAL timeout=0;
#endif

    // inverted logic (0 = last item! prev(0) = top most item)
    selected = GetPrevSelectable(0);
#ifndef DEDICATED
    mouseSelection_ = -1;
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
        // the layout follows the window and the rows the menu has right now
        if (RclStyle())
            ApplyTheme();

        {
            SDL_Event tEvent;
            uInputProcessGuard inputProcessGuard;
            while (!exitFlag && !quickexit && !exitToMain && su_GetSDLInput(tEvent))
            {
                REAL entertime = tSysTimeFloat();

                switch (tEvent.type)
                {
                case SDL_KEYDOWN:
                    if ( tEvent.key.keysym.sym == SDLK_UNKNOWN )
                    {
                        // don't repeat unknown syms. They come from multi-key compositions and
                        // don't send keyup events when released.
                        break;
                    }
                    localRepeat = s_globalRepeat = true;
                    memcpy( &tEventRepeat, &tEvent, sizeof( SDL_Event ) );
                    nextrepeat = tSysTimeFloat() + repeatdelay;
                    break;
                case SDL_KEYUP:
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

        // how far the selected row stays inside the list, and how far the
        // first and last row stay from its ends
        REAL border=.3;
        REAL smallborder=.1;
#ifndef DEDICATED
        if (RclStyle())
        {
            border = uRclTheme::ScrollMargin();
            smallborder = uRclTheme::ScrollEdge();
        }
#endif

        menuentries=items.Len();

        if (style_ == uMenuStyle_RclPrompt)
            yOffset = uRclTheme::PromptRowY() +
                      uRclTheme::RowPitch() * (menuentries - selected);

        REAL ysel=YPos(selected);

        // A row the mouse picked is already visible under the cursor. Scrolling
        // it away from the edge would slide the rows out from under a
        // stationary pointer, so only keyboard selection moves the list.
        bool keepSelectedInView = true;
#ifndef DEDICATED
        keepSelectedInView = mouseSelection_ != selected;
#endif

        if (style_ != uMenuStyle_RclPrompt)
        {
            if (keepSelectedInView)
            {
                REAL scrollUp = menuBot+border-ysel;
                if(scrollUp > 0)
                    scrollBy(scrollUp);

                REAL scrollDown = menuTop-border-ysel;
                if(scrollDown < 0)
                    scrollBy(scrollDown);

                if (ysel<menuBot)
                    yOffset+=(menuBot-ysel);

                if (ysel>menuTop-smallborder)
                    yOffset+=(menuTop-smallborder-ysel);
            }

            if (YPos(0)>menuBot+smallborder)
                yOffset+=menuBot+smallborder-YPos(0);

            if (YPos(menuentries-1)<menuTop-smallborder)
                yOffset+=menuTop-smallborder-YPos(menuentries-1);
        }

#ifndef DEDICATED
        sr_ResetRenderState(true);
        if (items.Len() <= 0)
        {
            exitFlag = true;
            continue;
        }
        if (selected >= items.Len()) selected = items.Len()-1;
        items[selected]->RenderBackground();

        if (sr_glOut && !exitFlag && !quickexit){
            if (RclStyle())
            {
                // one short entrance: the page fades in and its rows settle
                REAL progress = (tSysTimeFloat() - styleEnterTime_) / .14f;
                REAL const entrance = uRclTheme::EaseIn(progress);
                REAL const slide = -uRclTheme::W(12) * (1 - entrance);
                bool const prompt = style_ == uMenuStyle_RclPrompt;

                uRclTheme::DrawBackground(.72f + .28f * entrance);
                uRclTheme::DrawChrome(tString(title), footnote_, entrance);

                // Some legacy items render a live preview. Their background
                // callback still prepares gameplay/console content; the
                // visible preview belongs above the shared RCL shell.
                RenderEnd();
                glDisable(GL_TEXTURE_2D);
                items[selected]->RenderForeground();

                // rows fade out over half a row at the list's ends
                REAL const fade = uRclTheme::RowPitch() * .5f;
                for (int i=items.Len()-1;i>=0;i--)
                {
                    REAL const layoutY=YPos(i);
                    if (layoutY<=menuBot || layoutY>=menuTop)
                        continue;

                    REAL alpha=entrance;
                    if (!prompt)
                    {
                        if (layoutY<menuBot+fade)
                            alpha*=std::max(0.0f, (layoutY-menuBot)/fade);
                        if (layoutY>menuTop-fade)
                            alpha*=std::max(0.0f, (menuTop-layoutY)/fade);
                    }

                    uMenuItem *item = items[i];
                    REAL const y = ItemDrawY(i);
                    uRclTheme::BeginRow(y, i == selected, item->IsPrimary(),
                                        item->Control());
                    uRclTheme::DrawRow(alpha, item->ControlOn(),
                                       item->ControlFraction());
                    item->Render(center + slide, y, alpha, i == selected);
                }

                uRclTheme::DrawHelp(items[selected]->Help(), entrance);
                disphelp = false;

                if (!prompt)
                    uRclTheme::DrawScrollMarks(
                        YPos(menuentries-1) > menuTop-smallborder+1E-4f,
                        YPos(0) < menuBot+smallborder-1E-4f,
                        entrance);
            }
            else
            {
                items[selected]->Render(center,YPos(selected),1,true);

                for (int i=items.Len()-1;i>=0;i--)
                    if (i!=selected){
                        REAL y=YPos(i);
                        REAL alpha=1;
                        const REAL b=.1;
                        if (y<menuBot+b)
                            alpha=(y-menuBot)/b;
                        if (y>menuTop-b)
                            alpha=(menuTop-y)/b;
                        if (y>menuBot && y<menuTop)
                        {
                            rTextField::SetDefaultColor( tColor(1,1,1,1) );
                            rTextField::SetBlendColor( tColor(1,1,1,1) );
                            items[i]->Render(center,y,alpha,false);
                        }
                    }

                rTextField::SetDefaultColor( tColor(1,1,1,1) );
                rTextField::SetBlendColor( tColor(1,1,1,1) );

                Color(.6,.6,1,1);
                ::DisplayText(0,menuTop+text_height*titlefac
                              ,text_width*titlefac*rTextField::AspectWidthMultiplier(),text_height*titlefac,
                              title,0);

                glDisable(GL_TEXTURE_2D);
                //glDisable(GL_TEXTURE);
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

    uCallbackMenuLeave::MenuLeave();
    su_inMenu = false;

#ifndef DEDICATED
    if (RclStyle())
    {
        menuTop = savedMenuTop;
        menuBot = savedMenuBot;
        spaceBelow = savedSpaceBelow;
        center = savedCenter;
    }
#endif
}

void uMenu::HandleEvent( SDL_Event event )
{
#ifndef DEDICATED
    if (!items[selected]->Event(event))
    {
        switch (event.type)
        {
        case SDL_MOUSEMOTION:
        {
            REAL const mouseX = MenuMouseX(event.motion.x);

            // a held slider follows the pointer wherever it goes
            if (dragItem_ >= 0)
            {
                if (dragItem_ < items.Len() &&
                    (event.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) &&
                    items[dragItem_]->SetControlFraction(MenuSliderFraction(mouseX)))
                    return;
                dragItem_ = -1;
            }

            const int hit = ItemAt(mouseX, MenuMouseY(event.motion.y));
            if (hit >= 0 && selected != hit)
            {
                selected = hit;
                mouseSelection_ = hit;
                lastkey = tSysTimeFloat();
                items[selected]->DisplayHelp(false, 0, 0.0f);
            }
            return;
        }

        case SDL_MOUSEBUTTONUP:
            dragItem_ = -1;
            return;

        default:
            break;
        }

        // keys and the wheel move the selection keyboard-style and get the
        // usual keep-in-view scrolling
        if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN)
            mouseSelection_ = -1;

        switch (event.type)
        {

        case SDL_MOUSEBUTTONDOWN:
            switch (event.button.button)
            {
            case SDL_BUTTON_LEFT:
            {
                REAL const mouseX = MenuMouseX(event.button.x);
                const int hit = ItemAt(mouseX, MenuMouseY(event.button.y));
                if (hit >= 0)
                {
                    if (selected != hit)
                        items[hit]->DisplayHelp(false, 0, 0.0f);
                    selected = hit;
                    mouseSelection_ = hit;
                    lastkey = tSysTimeFloat();

                    if (!RclStyle())
                    {
                        ActivateSelected();
                        break;
                    }

                    // on a slider's track, the press sets it and starts a drag
                    REAL const reach = uRclTheme::W(10);
                    if (items[hit]->Control() == uRclTheme::Control_Slider &&
                        mouseX >= uRclTheme::SliderLeft() - reach &&
                        mouseX <= uRclTheme::SliderRight() + reach &&
                        items[hit]->SetControlFraction(MenuSliderFraction(mouseX)))
                        dragItem_ = hit;
                    else if (!items[hit]->Click(mouseX))
                        ActivateSelected();
                }
                break;
            }

            case SDL_BUTTON_WHEELUP:
            {
                lastkey = tSysTimeFloat();
                const int next = GetNextSelectable(selected);
                if (next >= 0)
                {
                    selected = next;
                    items[selected]->DisplayHelp(false, 0, 0.0f);
                }
                break;
            }

            case SDL_BUTTON_WHEELDOWN:
            {
                lastkey = tSysTimeFloat();
                const int next = GetPrevSelectable(selected);
                if (next >= 0)
                {
                    selected = next;
                    items[selected]->DisplayHelp(false, 0, 0.0f);
                }
                break;
            }

            case SDL_BUTTON_RIGHT:
                s_globalRepeat = false;
                lastkey = tSysTimeFloat();
                Back();
                break;

            default:
                break;
            }
            return;

        default:
            break;
        }

        switch (event.type){
        case SDL_KEYDOWN:
        {
            if (!disphelp)
                lastkey=tSysTimeFloat();
            switch (event.key.keysym.sym){

            case(SDLK_ESCAPE):
                s_globalRepeat = false;
                lastkey=tSysTimeFloat();
                Back();
                break;

            case(SDLK_UP):
                lastkey=tSysTimeFloat();
                selected = GetNextSelectable(selected);
                items[selected]->DisplayHelp(false, 0, 0.0f);
                break;

            case(SDLK_DOWN):
                lastkey=tSysTimeFloat();
                selected = GetPrevSelectable(selected);
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

int uMenu::GetPrevSelectable(int start)
{
    int prev = start-1;
    while (prev!=start)
    {
        if (prev<0)
        {
            if (wrap)
                prev = items.Len()-1;
            else
                break;
        }
        if (items[prev]->IsSelectable())
        {
            return prev;
        }
        prev--;
    }
    return -1;
}

int uMenu::GetNextSelectable(int start)
{
    int next = start+1;
    while (next!=start)
    {
        if (next>=items.Len())
        {
            if (this->wrap)
                next = 0;
            else
                break;
        }
        if (items[next]->IsSelectable())
        {
            return next;
        }
        next++;
    }
    return -1;
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

            // Render the console so it appears behind the menu. Where the
            // pale arena was just drawn, ink would sink into the layers that
            // hold the arena back; there, the game left the console to us
            // and it follows the layer, in its usual light colours.
            bool const consoleAfterLayer = sr_cleanInk;
            if( sr_con.autoDisplayAtSwap && !consoleAfterLayer )
            {
                sr_con.Render();
            }

            // the menu drawn over this holds it back instead of hiding it
            uRclTheme::NoteSceneBehind();

            // fade everything rendered so far to black
            if( sr_alphaBlend && sr_chatLayer > 0 )
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
                    REAL limit = sr_chatLayer;

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

            if( sr_con.autoDisplayAtSwap && consoleAfterLayer )
            {
                sr_cleanInk = false;
                sr_con.Render();
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

    // what is drawn from here on lies on the menu, not on the arena behind it
    sr_cleanInk = false;
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
#ifndef DEDICATED
    if (menu && menu->RclStyle())
    {
        uRclTheme::SetLabelColor(selected, alpha);
        return;
    }
#endif

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
        if (menu && menu->RclStyle())
        {
            // the theme places a row's label, action or value; the interface
            // strings are lowercase already and typed text stays as typed
            uRclTheme::RowText(center, x, y, text, selected, alpha, c, cp, colorMode);
            return;
        }

        SetColor( selected, alpha );

        REAL tw = text_width;
        REAL th = text_height;
        
        //aspect ratio correction
        tw *= (REAL(sr_screenHeight)/sr_screenWidth)*(4.0/3.0);


#if 0
        // the function that is called takes care of that
        REAL availw = 1.9f;
        if (center < 0) availw = (.9f-x);
        if (center > 0) availw = (x + .9f);

        int len = strlen(text);
        if (len * tw > availw)
        {
            th *= availw/(len * tw);
            tw  = availw/len;
        }
#endif

        ::DisplayText(x,y,tw,th,text,center,c,cp, colorMode );
    }
#endif
}

void uMenuItem::DisplayTextSpecial(REAL x,REAL y,const char *text,
                                   bool selected,
                                   REAL alpha,int center){
    /*
     if(selected)
       glColor3f(.9,.3,.3);
     else
       glColor3f(.7,.7,1);

     ::DisplayText(x,y,text_width,text_height,text,center);
     */

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
}
#endif

// A value outside the range (set in a config file, or imported) is shown as
// it is and left alone; only changing it here brings it into the range.
uMenuItemInt::uMenuItemInt
(uMenu *m,const tOutput &tit,const tOutput &help,int &targ,
 int mi,int ma,int step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
}

REAL uMenuItemInt::ControlFraction(){
    if (Max<=Min)
        return -1;
    REAL const fraction = REAL(target-Min)/REAL(Max-Min);
    return fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
}

bool uMenuItemInt::SetControlFraction(REAL fraction){
    if (Max<=Min)
        return false;
    int const step = Step > 0 ? Step : 1;
    int const steps = static_cast<int>(floorf(fraction*(Max-Min)/step + .5f));
    int const wanted = std::max(Min, std::min(Max, Min + steps*step));

    // Step there with LeftRight, where items hang their side effects; a
    // value that is far away or off the step grid is set directly.
    int const distance = wanted > target ? wanted - target : target - wanted;
    if (distance % step != 0 || distance / step > 64)
    {
        target = wanted;
        return true;
    }
    for (int left = distance / step; left > 0 && target != wanted; --left)
        LeftRight(wanted > target ? 1 : -1);
    return true;
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
}
#endif

uMenuItemReal::uMenuItemReal
(uMenu *m,const tOutput &tit,const tOutput &help,REAL &targ,
 REAL mi,REAL ma,REAL step)
        :uMenuItem(m,help),title(tit),target(targ),Min(mi),Max(ma),
        Step(step){
}

REAL uMenuItemReal::ControlFraction(){
    if (!(Max>Min))
        return -1;
    REAL const fraction = (target-Min)/(Max-Min);
    return fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
}

bool uMenuItemReal::SetControlFraction(REAL fraction){
    if (!(Max>Min))
        return false;
    REAL value = Min + fraction*(Max-Min);
    if (Step > 0)
        value = Min + floorf((value-Min)/Step + .5f)*Step;
    target = value < Min ? Min : value > Max ? Max : value;
    return true;
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
    if (e.type!=SDL_KEYDOWN)
        return false;
    bool ret=true;
    SDL_keysym &c=e.key.keysym;
    SDLMod mod = c.mod;
    bool moveWordLeft, moveWordRight, deleteWordLeft, deleteWordRight, moveBeginning, moveEnd, killForwards, pasteText;
    moveWordLeft = moveWordRight = deleteWordLeft = deleteWordRight = moveBeginning = moveEnd = killForwards = pasteText = false;

#if defined (MACOSX)
    // For moving over/deleting words
    if (mod & KMOD_ALT) {
        if (c.sym == SDLK_LEFT) {
            moveWordLeft = true;
        }
        else if (c.sym == SDLK_RIGHT) {
            moveWordRight = true;
        }
        else if (c.sym == SDLK_DELETE) {
            deleteWordRight = true;
        }
        else if (c.sym == SDLK_BACKSPACE) {
            deleteWordLeft = true;
        }
    }
    // For moving to extremes of the line
    else if (mod & KMOD_META) {
        if (c.sym == SDLK_LEFT) {
            moveBeginning = true;
        }
        else if (c.sym == SDLK_RIGHT) {
            moveEnd = true;
        }
        else if (c.sym == SDLK_v) {
            pasteText = true;
        }
    }
    // Linux and Windows
#else
    // Word operations
    if (mod & KMOD_CTRL) {
        if (c.sym == SDLK_LEFT) {
            moveWordLeft = true;
        }
        else if (c.sym == SDLK_RIGHT) {
            moveWordRight = true;
        }
        else if (c.sym == SDLK_DELETE) {
            deleteWordRight = true;
        }
        else if (c.sym == SDLK_BACKSPACE) {
            deleteWordLeft = true;
        }
        else if (c.sym == SDLK_v) {
            pasteText = true;
        }
    }
    else if (c.sym == SDLK_HOME) {
        moveBeginning = true;
    }
    else if (c.sym == SDLK_END) {
        moveEnd = true;
    }
#endif
    // "bash" keys
    if (mod & KMOD_CTRL) {
        if (c.sym == SDLK_a) {
            moveBeginning = true;
        }
        else if (c.sym == SDLK_e) {
            moveEnd = true;
        }
        else if (c.sym == SDLK_k) {
            killForwards = true;
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
    else if (pasteText) {
#ifndef WIN32
        #ifdef MACOSX
            #define CMD_CLIPBOARD_PASTE "pbpaste"
        #else
            #define CMD_CLIPBOARD_PASTE "xclip -selection clipboard -o"
        #endif
        std::unique_ptr<FILE, decltype(&pclose)> clip(popen(CMD_CLIPBOARD_PASTE, "r"), pclose);
        if( clip )
#else
        if (OpenClipboard(0))
#endif
        {
#ifdef WIN32
            HANDLE hClipboardData = GetClipboardData(CF_TEXT);
            char *pchData = (char*)GlobalLock(hClipboardData);
            tString cData(pchData);
#else
            tString cData;
            char buf[128];
            while( std::fgets(buf, 128, clip.get()) )
            {
                cData << buf;
            }
            
            cData = st_UTF8ToLatin1( cData );
#endif

            tString oContent(*content);
            tString aContent = oContent.SubStr(0, cursorPos);
            tString bContent = oContent.SubStr(cursorPos);

            tString nContent = aContent + cData + bContent;

            *content = nContent;
            cursorPos += cData.Len()-1;

#ifdef WIN32
            GlobalUnlock(hClipboardData);
            CloseClipboard();
#endif
        }
    }
    else if (c.sym == SDLK_LEFT) {
        if (cursorPos > 0) {
            cursorPos--;
        }
    }
    else if (c.sym == SDLK_RIGHT) {
        if (cursorPos < content->Len()-1) {
            cursorPos++;
        }
    }
    else if (c.sym == SDLK_DELETE) {
        if (cursorPos < content->Len()-1) {
            content->RemoveSubStr(cursorPos,1);
        }
    }
    else if (c.sym == SDLK_BACKSPACE) {
        if (cursorPos > 0) {
            content->RemoveSubStr(cursorPos,-1);
            cursorPos--;
        }
    }
    else if (c.sym == SDLK_KP_ENTER || c.sym == SDLK_RETURN) {
        ret = false;
        //        c.sym = SDLK_DOWN;
    }
    else {
        if (32 <= c.unicode  && c.unicode < 256)
        {
            ret=true;

            int len;
            if( dynamic_cast<uMenuItemColorLine *>(this) )
                len = tColoredString::RemoveColors(*content).Len();
            else
                len = content->Len();
            
            // insert character if there is room
            if (len < maxLength_)
            {
                for (int i = content->Len() - 1; i>= cursorPos; i--)
                    (*content)[i+1]=(*content)[i];

                // guarantee proper null termination
                (*content)[content->Len()-1]='\0';
                (*content)[cursorPos]=c.unicode;
                cursorPos++;
            }
        }
        else {
            ret=false;
        }
    }

    if (cursorPos<0)    cursorPos=0;
    if (cursorPos > content->Len()-1) cursorPos=content->Len()-1;

    return ret;
#else
    return false;
#endif
}

// *****************************************************


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

//! @param e the event to process
/// @returns true if the event was handled, false if it wasn't
bool uMenuItemStringWithHistory::Event(SDL_Event &e)
{
    // flag indicating that the event was handled
    bool ret = false;
#ifndef DEDICATED
    SDLMod mod = e.key.keysym.mod;

    if (e.type == SDL_KEYDOWN
            && ((e.key.keysym.sym == SDLK_UP)
                || (e.key.keysym.sym == SDLK_p && mod & KMOD_CTRL)))
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
    else if (e.type == SDL_KEYDOWN
             && ((e.key.keysym.sym == SDLK_DOWN)
                 || (e.key.keysym.sym == SDLK_n && mod & KMOD_CTRL)))
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
        sr_LockSDL();
        SDL_PumpEvents();
        sr_UnlockSDL();
        return uMenu::quickexit != uMenu::QuickExit_Off;
    }

    SDL_Event event;
    uInputProcessGuard inputProcessGuard;
    while (!s_idleBackground && su_GetSDLInput(event))
    {
        switch (event.type)
        {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
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

        int offset = 0; // first line of the text in view
        tString const heading(message);
        tString const body(interpretation);
        while (  !quickexit &&
                 (to < 0 || tSysTimeFloat() < timeout)){
            //while(  !quickexit && ( !su_GetSDLInput(tEvent) || tEvent.type!=SDL_KEYDOWN) &&
            //        (to < 0 || tSysTimeFloat() < timeout)){
            if ( su_GetSDLInput(tEvent) && tEvent.type==SDL_KEYDOWN) {
                switch (tEvent.key.keysym.sym) {
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
                    // a screenshot of a message is not an answer to it
                    if (su_GlobalKey(tEvent.key.keysym.sym))
                        continue;
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
                offset = std::min(offset,
                                  uRclTheme::DrawDialog(heading, body, offset, 1));
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

// return value: false only if the user pressed ESC
bool uMenu::Busy(const tOutput& message, const tOutput& interpretation){
    bool ret = true;
#ifndef DEDICATED
    {
        SDL_Event tEvent;
        uInputProcessGuard inputProcessGuard;
        while (su_GetSDLInput(tEvent))
        {
            if (tEvent.type==SDL_KEYDOWN && tEvent.key.keysym.sym==SDLK_ESCAPE)
                ret = false;
        }
    }
    if (quickexit)
        ret = false;

    if ( sr_glOut )
    {
        sr_ResetRenderState(true);
        rViewport::s_viewportFullscreen.Select();

        rSysDep::ClearGL();

        GenericBackground();
        uRclTheme::DrawDialog(tString(message), tString(interpretation), 0, 1);
    }
    rSysDep::SwapGL();
#endif

    return ret;
}
