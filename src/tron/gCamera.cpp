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

#include "tMemManager.h"
#include "gCycle.h"
#include "gCamera.h"
#include "gGame.h"
#include "ePlayer.h"
#include "rFont.h"
#include "rScreen.h"
#include "rViewport.h"
#include "rSDL.h"
#include "eAdvWall.h"
#include "eRectangle.h"
#include "eGrid.h"
#include "uInput.h"
#include "uMenu.h"
#include "tConfiguration.h"
#include "tDirectories.h"
#include "tSysTime.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

void gCamera::MyInit(){
    if (mode == CAMERA_SMART)
        if (sg_currentSettings->finishType==gFINISH_EXPRESS && sn_GetNetState() != nCLIENT )
            pos=CenterPos()+dir.Turn(eCoord(-2,-10)) ;

    lastCenter=Center();
}

gCamera::gCamera(eGrid *grid, rViewport *view,ePlayerNetID *p,ePlayer *lp,eCamMode m)
        :eCamera(grid, view,p,lp,m), lastCenter(NULL){
    MyInit();
}


gCamera::~gCamera(){
#ifdef DEBUG
    int x;
    x =0;
#endif
}

eCoord gCamera::CenterCycleDir() const {
    gCycle *c = dynamic_cast<gCycle *>( Center());
    if (c)
        return c->CamDir();
    else
        return CenterDir();
}

REAL gCamera::SpeedMultiplier() const
{
    return gCycle::SpeedMultiplier();
}

void gCamera::Timestep(REAL ts){
    eCamera::Timestep(ts);
    if (Center()!=lastCenter){
        if (!netPlayer || !netPlayer->Object() ||
                netPlayer->Object()!=center){
            if (dynamic_cast<gCycle *>(Center())){
                eNetGameObject *x=dynamic_cast<eNetGameObject *>(Center());
                if (x){
                    const ePlayerNetID *p=x->Player();
                    if (p==NULL)
                        con << tOutput("$camera_watching_ai");
                    else{
                        tOutput o;
                        o.SetTemplateParameter(1, p->GetName());
                        o << "$camera_watching_player";
                        con << o;
                    }
                }
            }
            lastCenter=Center();
        }
    }
}

// ---- the watcher's saved views ----
// Jamie: "when spectacting id like a way to save views and hotkey through
// views". While watching, with no cycle of one's own in play, a view's key
// (1 to 9 to start with) puts the camera in that view and Ctrl with it saves
// the view the camera has there; ] goes to the next view there is here, [ to
// the one before (SPECTATOR_KEY_VIEW_*, uInput.cpp). The keys are the
// watcher's own, so the same digits say instant chats while playing. Tab is
// the scores, watching or not.
// A view saved flying the free camera is a spot in the arena and belongs to
// that arena (its map file and size); one saved following a cycle is where
// the eye is from the cycle, and goes with whoever is watched, in any arena.
// A slot here is this arena's spot if it has one, else the following view.
// All of it is kept in var/rcl-views.cfg, written at every save.

extern tString mapfile;     // gGame.cpp

#ifndef DEDICATED
static bool sg_viewGlide = true;
static tConfItem< bool > sg_viewGlideConf( "RCL_SPECTATOR_VIEW_GLIDE", sg_viewGlide );
#else
static bool sg_viewGlide = false;
#endif
bool & sg_SpectatorViewGlide(){ return sg_viewGlide; }

#ifndef DEDICATED
namespace
{
const REAL sg_viewGlideTime = .35f;
char const * const sg_viewsFile = "rcl-views.cfg";
char const * const sg_viewsTag = "rcl-views 1";

struct gSavedView
{
    int slot;               // 1 to 9
    std::string arena;      // where a spot belongs; empty for a view that follows
    eCameraView view;
};
std::vector< gSavedView > sg_views;
bool sg_viewsLoaded = false;
int sg_viewLast = 0;        // the slot last gone to or saved, 0 for none

// the arena: its map file and its bounds, which its size gives
std::string sg_ViewArena()
{
    eRectangle const & bounds = eWallRim::GetBounds();
    char size[ 160 ];
    snprintf( size, sizeof( size ), "@%.1f,%.1f,%.1f,%.1f", double( bounds.GetLow().x ), double( bounds.GetLow().y ),
              double( bounds.GetHigh().x ), double( bounds.GetHigh().y ) );
    std::string arena( static_cast< char const * >( mapfile ) );
    for ( size_t i = 0; i < arena.size(); ++i )
        if ( arena[i] == '\t' || arena[i] == '\n' || arena[i] == '\r' )
            arena[i] = ' ';
    return arena + size;
}

void sg_ViewsLoad()
{
    if ( sg_viewsLoaded )
        return;
    sg_viewsLoaded = true;
    sg_views.clear();

    std::ifstream in( static_cast< char const * >( tDirectories::Var().GetReadPath( sg_viewsFile ) ), std::ios::binary );
    std::string line;
    if ( !in || !std::getline( in, line ) )
        return;
    if ( !line.empty() && line[ line.size() - 1 ] == '\r' )
        line.erase( line.size() - 1 );
    if ( line.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 )
        line.erase( 0, 3 );
    if ( line != sg_viewsTag )
        return;
    while ( std::getline( in, line ) )
    {
        if ( !line.empty() && line[ line.size() - 1 ] == '\r' )
            line.erase( line.size() - 1 );
        if ( line.empty() || line[0] == '#' )
            continue;
        // slot, arena (- for a view that follows), then the view
        std::string::size_type const first = line.find( '\t' );
        std::string::size_type const second = first == std::string::npos ? first : line.find( '\t', first + 1 );
        if ( second == std::string::npos )
            continue;
        gSavedView saved;
        saved.slot = atoi( line.substr( 0, first ).c_str() );
        saved.arena = line.substr( first + 1, second - first - 1 );
        if ( saved.slot < 1 || saved.slot > 9 || !saved.view.Parse( line.c_str() + second + 1 ) )
            continue;
        if ( saved.arena == "-" )
            saved.arena.clear();
        // a spot needs its arena, a view that follows has none
        if ( saved.view.fixed == saved.arena.empty() )
            continue;
        sg_views.push_back( saved );
    }
}

// The whole file again, at every save: a game that is closed by force or
// falls over keeps what was saved. Written aside and put in place after.
void sg_ViewsSave()
{
    tString const path = tDirectories::Var().GetWritePath( sg_viewsFile );
    std::string const aside = std::string( static_cast< char const * >( path ) ) + ".new";
    {
        std::ofstream out( aside.c_str(), std::ios::binary );
        if ( !out )
            return;
        out << sg_viewsTag << "\n";
        out << "# The spectator's saved views (keys 1 to 9 to go to one, Ctrl with one to save, ] and [ to go through them).\n";
        out << "# slot, arena (map file @ bounds; - for a view that follows the cycle watched), then the view; tab separated. Kept on this PC only.\n";
        for ( size_t i = 0; i < sg_views.size(); ++i )
        {
            gSavedView const & saved = sg_views[i];
            out << saved.slot << '\t' << ( saved.arena.empty() ? std::string( "-" ) : saved.arena ) << '\t'
                << static_cast< char const * >( saved.view.Serialise() ) << '\n';
        }
        if ( !out )
            return;
    }
    std::remove( static_cast< char const * >( path ) );
    std::rename( aside.c_str(), static_cast< char const * >( path ) );
}

// what a slot is here: this arena's spot, or else the view that follows
gSavedView const * sg_ViewHere( int slot, std::string const & arena )
{
    for ( size_t i = 0; i < sg_views.size(); ++i )
        if ( sg_views[i].slot == slot && !sg_views[i].arena.empty() && sg_views[i].arena == arena )
            return &sg_views[i];
    for ( size_t i = 0; i < sg_views.size(); ++i )
        if ( sg_views[i].slot == slot && sg_views[i].arena.empty() )
            return &sg_views[i];
    return NULL;
}

// a small line low on the screen, gone after a moment
std::string sg_viewNotice;
double sg_viewNoticeAt = -100;

// the view's number in it, or (with none) the key the first view is saved with
void sg_ViewNotice( char const * text, int slot )
{
    tOutput line;
    if ( slot > 0 )
        line.SetTemplateParameter( 1, slot );
    else
        line.SetTemplateParameter( 1, su_SpectatorKeyName( uSpectatorKey_View1 ) );
    line << text;
    tString said;
    said << line;
    sg_viewNotice = static_cast< char const * >( said );
    sg_viewNoticeAt = tSysTimeFloat();
}

void sg_ViewNoticeDraw()
{
    if ( sg_viewNotice.empty() )
        return;
    double const age = tSysTimeFloat() - sg_viewNoticeAt;
    static const double hold = 1.1, fade = .4;
    if ( age < 0 || age > hold + fade )
    {
        sg_viewNotice.clear();
        return;
    }
    REAL const alpha = age < hold ? REAL( 1 ) : REAL( 1 - ( age - hold ) / fade );

    sr_ResetRenderState( true );
    rViewport::s_viewportFullscreen.Select();
    tColor const before = rTextField::GetBlendColor();
    rTextField::SetBlendColor( tColor( 1, 1, 1, alpha * REAL( .8 ) ) );
    DisplayTextAutoWidth( 0, REAL( -.5 ), sg_viewNotice.c_str(), REAL( .045 ), 0 );
    rTextField::SetBlendColor( before );
}
rPerFrameTask sg_viewNoticeTask( &sg_ViewNoticeDraw );
}
#endif

bool sg_SpectatorViewEvent( eGrid * grid, SDL_Event const & event )
{
#ifdef DEDICATED
    return false;
#else
    int sym = 0;
    int mod = 0;
    if ( event.type == SDL_KEYDOWN )
    {
        sym = event.key.keysym.sym;
        mod = event.key.keysym.mod;
    }
    else if ( event.type == SDL_MOUSEBUTTONDOWN && event.button.button >= 1 && event.button.button <= MOUSE_BUTTONS )
    {
        sym = SDLK_MOUSE_BUTTON_1 + event.button.button - 1;
        mod = SDL_GetModState();
    }
    if ( sym <= 0 )
        return false;

    // which of the view keys it is
    int slot = 0, step = 0;
    for ( int i = 0; i < 9 && !slot; ++i )
        if ( su_SpectatorKeySym( static_cast< uSpectatorKey >( uSpectatorKey_View1 + i ) ) == sym )
            slot = i + 1;
    if ( !slot )
    {
        int const next = su_SpectatorKeySym( uSpectatorKey_ViewNext );
        int const back = su_SpectatorKeySym( uSpectatorKey_ViewPrev );
        if ( sym == next && sym == back )
            step = ( mod & KMOD_SHIFT ) ? -1 : 1;
        else if ( sym == back )
            step = -1;
        else if ( sym == next )
            step = 1;
    }
    if ( !slot && !step )
        return false;

    // only while watching: not playing, typing or in a menu
    if ( !grid || uMenu::MenuActive() )
        return false;
    eCamera * camera = NULL;
    tList< eCamera > const & cameras = grid->Cameras();
    for ( int i = 0; i < cameras.Len() && !camera; ++i )
        if ( cameras( i ) && cameras( i )->Spectating() )
            camera = cameras( i );
    if ( !camera )
        return false;

    sg_ViewsLoad();
    std::string const arena = sg_ViewArena();
    REAL const glide = sg_viewGlide ? sg_viewGlideTime : 0;

    if ( slot && ( mod & KMOD_CTRL ) )
    {
        gSavedView saved;
        saved.slot = slot;
        if ( !camera->SaveView( saved.view ) )
            return true;
        if ( saved.view.fixed )
            saved.arena = arena;
        // it takes the slot's place here: this arena's spot, and the
        // following view if it is one (the spots of other arenas stay)
        for ( size_t i = sg_views.size(); i-- > 0; )
        {
            gSavedView const & old = sg_views[i];
            if ( old.slot == slot && ( old.arena == saved.arena || ( !saved.view.fixed && old.arena == arena ) ) )
                sg_views.erase( sg_views.begin() + i );
        }
        sg_views.push_back( saved );
        sg_ViewsSave();
        sg_viewLast = slot;
        sg_ViewNotice( "$spectator_view_saved", slot );
        return true;
    }

    if ( slot )
    {
        gSavedView const * saved = sg_ViewHere( slot, arena );
        if ( !saved )
        {
            sg_ViewNotice( "$spectator_view_empty", slot );
            return true;
        }
        camera->ApplyView( saved->view, glide );
        sg_viewLast = slot;
        sg_ViewNotice( "$spectator_view_gone", slot );
        return true;
    }

    // through those there are here, in order, from the one last used
    for ( int i = 1; i <= 9; ++i )
    {
        // nothing used yet: from the first of them, or back from the last
        int const candidate = sg_viewLast > 0 ? ( ( sg_viewLast - 1 + step * i ) % 9 + 9 ) % 9 + 1 : ( step > 0 ? i : 10 - i );
        gSavedView const * saved = sg_ViewHere( candidate, arena );
        if ( saved )
        {
            camera->ApplyView( saved->view, glide );
            sg_viewLast = saved->slot;
            sg_ViewNotice( "$spectator_view_gone", saved->slot );
            return true;
        }
    }
    sg_ViewNotice( "$spectator_view_none", 0 );
    return true;
#endif
}

