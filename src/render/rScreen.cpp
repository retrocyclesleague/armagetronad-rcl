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

#include "rFont.h"

#include "defs.h"

#include <string>
#include "rTexture.h"
#include "rScreen.h"
#include "rSysdep.h"
#include "rConsole.h"
#include "rViewport.h"
#include "tConfiguration.h"
#include "tSysTime.h"

#ifndef DEDICATED
// #include "../network/nNetwork.h"
#include "rGL.h"
#include "rSDL.h"
#include "rGraphicsBackend.h"
#include "rMetalBackend.h"
#include "rMatrixState.h"
#include "rMetalGLCompat.h"


#ifdef POWERPAK_DEB
#include <PowerPak/powerdraw>
#endif
#endif

#ifndef SDL_OPENGL
#ifndef __APPLE__
#ifndef DIRTY
#define DIRTY
#endif
#endif
#endif

#ifdef DEBUG
//#ifdef WIN32
#define FORCE_WINDOW
//#endif
#endif

tCONFIG_ENUM( rResolution );
tCONFIG_ENUM( rColorDepth );
tCONFIG_ENUM( rVSync );

#ifndef DEDICATED
SDL_Window *sr_window=NULL; // our window
SDL_GLContext sr_glcontext=NULL; // our GL context
#endif

// display the window was last on, so settings changes (which destroy and
// recreate the window) keep it on the same monitor instead of following the
// mouse. -1 means "not known yet", i.e. first window creation.
static int sr_lastDisplayIndex = -1;

#ifndef DEDICATED
static int default_texturemode = GL_LINEAR_MIPMAP_LINEAR;
#endif

rDisplayListUsage sr_useDisplayLists=rDisplayList_Off;
bool              sr_blacklistDisplayLists=false;

static int width[ArmageTron_Custom+2]  = {0, 320, 320, 400, 512, 640, 800, 1024	, 1280, 1280, 1280, 1600, 1680, 2048,800,320};
static int height[ArmageTron_Custom+2] = {0, 200, 240, 300, 384, 480, 600,  768	,  800,  854, 1024, 1200, 1050, 1572,600,200};
static REAL aspect[ArmageTron_Custom+2]= {1, 1	, 1  , 1  , 1  , 1  , 1	 , 1	,    1,    1, 1   ,    1,    1,    1,1,  1};

int sr_screenWidth,sr_screenHeight;

static tSettingItem<int>  at_ch("CUSTOM_SCREEN_HEIGHT"	, height[ArmageTron_Custom]);
static tSettingItem<int>  at_cw("CUSTOM_SCREEN_WIDTH" 	, width	[ArmageTron_Custom]);
static tSettingItem<REAL> at_ca("CUSTOM_SCREEN_ASPECT" , aspect[ArmageTron_Custom]);

#define MAXEMERGENCY 6

rScreenSettings lastSuccess(ArmageTron_640_480, false);

/*
std::ostream & operator << ( std::ostream & s, rScreenSize const & size )
{
    return s;
}

std::istream & operator >> ( std::istream & s, rScreenSize const & size )
{
    return s;
}
*/

static rScreenSettings em6(ArmageTron_320_240, false, ArmageTron_ColorDepth_16, true, false);
static rScreenSettings em5(ArmageTron_320_240, false, ArmageTron_ColorDepth_Desktop, true, false);
static rScreenSettings em4(ArmageTron_640_480, false,ArmageTron_ColorDepth_16);
static rScreenSettings em3(ArmageTron_640_480, true, ArmageTron_ColorDepth_16);
static rScreenSettings em2(ArmageTron_640_480, true, ArmageTron_ColorDepth_16, false);
static rScreenSettings em1(ArmageTron_640_480);

static rScreenSettings *emergency[MAXEMERGENCY+2]={ &lastSuccess, &lastSuccess, &em1, &em2, &em3 , &em4, &em5, &em6};

#ifdef DEBUG
rScreenSettings currentScreensetting(ArmageTron_640_480);
#else
rScreenSettings currentScreensetting(sr_DesktopScreensizeSupported() ? ArmageTron_Desktop : ArmageTron_800_600, true);
#endif

bool sr_DesktopScreensizeSupported()
{
#ifndef DEDICATED
    // SDL2 always supports desktop resolution
    return true;
#else
    return false;
#endif
}

static int failed_attempts = 0;

static tConfItem<rResolution> screenres("ARMAGETRON_SCREENMODE",currentScreensetting.res.res);
static tConfItem<rResolution> screenresLast("ARMAGETRON_LAST_SCREENMODE",lastSuccess.res.res);

static tConfItem<rResolution> winsize("ARMAGETRON_WINDOWSIZE",currentScreensetting.windowSize.res);
static tConfItem<rResolution> winsizeLast("ARMAGETRON_LAST_WINDOWSIZE",lastSuccess.windowSize.res);

static tConfItem<rVSync> vSync("ARMAGETRON_VSYNC",currentScreensetting.vSync);
static tConfItem<rVSync> vSyncLast("ARMAGETRON_VSYNC_LAST",lastSuccess.vSync);

static tConfItem<int> screenres_w("ARMAGETRON_SCREENMODE_W",currentScreensetting.res.width);
static tConfItem<int> screenresLast_w("ARMAGETRON_LAST_SCREENMODE_W", lastSuccess.res.width);

static tConfItem<int> winsize_w("ARMAGETRON_WINDOWSIZE_W",currentScreensetting.windowSize.width);
static tConfItem<int> winsizeLast_w("ARMAGETRON_LAST_WINDOWSIZE_W",lastSuccess.windowSize.width);

static tConfItem<int> screenres_h("ARMAGETRON_SCREENMODE_H",currentScreensetting.res.height);
static tConfItem<int> screenresLast_h("ARMAGETRON_LAST_SCREENMODE_H", lastSuccess.res.height);

// static tConfItem<rScreenSize> winsize_wh("ARMAGETRON_WINDOWSIZE_WH",currentScreensetting.windowSize);

static tConfItem<int> winsize_h("ARMAGETRON_WINDOWSIZE_H",currentScreensetting.windowSize.height);
static tConfItem<int> winsizeLast_h("ARMAGETRON_LAST_WINDOWSIZE_H",lastSuccess.windowSize.height);

static tConfItem<bool> fs_ci("FULLSCREEN",currentScreensetting.fullscreen);
static tConfItem<bool> fs_lci("LAST_FULLSCREEN",lastSuccess.fullscreen);

static tConfItem<rColorDepth> tc("COLORDEPTH",currentScreensetting.colorDepth);
static tConfItem<rColorDepth> ltc("LAST_COLORDEPTH",lastSuccess.colorDepth);
static tConfItem<rColorDepth> tzd("ZDEPTH",currentScreensetting.zDepth);
static tConfItem<rColorDepth> ltzd("LAST_ZDEPTH",lastSuccess.zDepth);

#ifdef DIRTY
#ifdef SDL_OPENGL
static tConfItem<bool> sdl("USE_SDL",currentScreensetting.useSDL);
static tConfItem<bool> lsdl("LAST_USE_SDL",lastSuccess.useSDL);
#endif
#endif

static tConfItem<bool> check_errors("CHECK_ERRORS",currentScreensetting.checkErrors);
static tConfItem<bool> check_errorsl("LAST_CHECK_ERRORS",lastSuccess.checkErrors);

static tConfItem<int> fa("FAILED_ATTEMPTS", failed_attempts);

// *******************************************

static tCallback *rPerFrameTask_anchor;

bool sr_True(){return true;}

rPerFrameTask::rPerFrameTask(VOIDFUNC *f):tCallback(rPerFrameTask_anchor, f){}
void rPerFrameTask::DoPerFrameTasks(){
    // prevent console rendering, that can cause nasty recursions
    rNoAutoDisplayAtNewlineCallback noAutoDisplay( sr_True );
    Exec(rPerFrameTask_anchor);
}


// *******************************************

static tCallbackString *RenderId_anchor;

rRenderIdCallback::rRenderIdCallback(STRINGRETFUNC *f)
        :tCallbackString(RenderId_anchor, f){}
tString rRenderIdCallback::RenderId(){return Exec(RenderId_anchor);}

// *******************************************

// *******************************************************************************************
// *
// *   rScreenSize
// *
// *******************************************************************************************
//!
//!        @param  w   screen width
//!        @param  h  screen height
//!
// *******************************************************************************************

rScreenSize::rScreenSize( int w, int h )
        :res( ArmageTron_Invalid ), width(w), height(h)
{
}

// *******************************************************************************************
// *
// *   rScreenSize
// *
// *******************************************************************************************
//!
//!        @param  r
//!
// *******************************************************************************************

rScreenSize::rScreenSize( rResolution r )
        :res( r ), width(0), height(0)
{
    UpdateSize();
}

// *******************************************************************************************
// *
// *   UpdateSize
// *
// *******************************************************************************************
//!
//!
// *******************************************************************************************

void rScreenSize::UpdateSize( void )
{
    if ( res != ArmageTron_Invalid )
    {
        width = ::width[res];
        height = ::height[res];
        // res = ArmageTron_Invalid;
    }
}

// *******************************************************************************************
// *
// *   operator ==
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return true iff equal
//!
// *******************************************************************************************

bool rScreenSize::operator ==( rScreenSize const & other ) const
{
    return Compare( other ) == 0;
}

// *******************************************************************************************
// *
// *   operator !=
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return  true iff not equal
//!
// *******************************************************************************************

bool rScreenSize::operator !=( rScreenSize const & other ) const
{
    return Compare( other ) != 0;
}

// *******************************************************************************************
// *
// *   Compare
// *
// *******************************************************************************************
//!
//!        @param  other   size to compare with
//!        @return         0 if eqal, -1 if this is smaller, +1 if other is smaller
//!
// *******************************************************************************************

int rScreenSize::Compare( rScreenSize const & other ) const
{
    // desktop size dominates all
    if ( width == 0 && other.width != 0 )
        return 1;
    if ( other.width == 0 && width != 0 )
        return -1;

    if ( width < other.width )
        return -1;
    else if ( width > other.width )
        return 1;

    if ( height < other.height )
        return -1;
    else if ( height > other.height )
        return 1;

    /* res is not really a criterion, ignore it
    if ( res < other.res )
        return -1;
    else if ( res > other.res )
        return 1;
    */

    return 0;
}


// *******************************************************************************************
// *
// *   rScreenSettings
// *
// *******************************************************************************************
//!
//!        @param  r   the resolution
//!        @param  fs  fullscreen flag
//!        @param  cd  color depth
//!        @param  sdl use clean sdl initialization
//!        @param  ce  check for errors
//!
// *******************************************************************************************

rScreenSettings::rScreenSettings( rResolution r, bool fs, rColorDepth cd, bool sdl, bool ce )
        :res(r), windowSize(r), fullscreen(fs), colorDepth(cd), zDepth( ArmageTron_ColorDepth_Desktop ), useSDL(sdl), checkErrors(true), vSync( ArmageTron_VSync_Default ), aspect (1)
{
    // special case for desktop resolution: window size of 640x480
    if ( r == ArmageTron_Desktop )
    {
        windowSize = rScreenSize( ArmageTron_640_480 );
    }
}

void sr_ReinitDisplay(){
    // ponytail: guard against rapid reinit calls causing loop
    static double lastReinit = 0;
    double now = tRealSysTimeFloat();
    if (now - lastReinit < 0.5)
        return;
    lastReinit = now;

    sr_ExitDisplay();
    if (!sr_InitDisplay()){
        tERR_ERROR("Oops. Failed to reinit video hardware. "
                   "Resetting to defaults..\n");
        exit(-1);
    }

}


// *******************************************



// GL information

tString gl_vendor;
tString gl_renderer;
tString gl_version;
tString gl_extensions;

bool software_renderer=false;
bool last_software_renderer=false;

static tConfItem<bool> lsr("SOFTWARE_RENDERER",last_software_renderer);

tString lastError("Unknown");

#ifndef DEDICATED
static int countBits(unsigned int count)
{
    int ret = 0;
    while (count)
    {
        ret    += count & 1;
        count >>= 1;
    }

    return ret;
}
#endif

// flag indicating whether directX is supposed to be used for input (defaults to false, crashes on my Win7)
bool sr_useDirectX = false;
static bool use_directx_back = false;
#ifndef DEDICATED
static void sr_SetGLAttributes( int rDepth, int gDepth, int bDepth, int zDepth )
{
    // must be called before window/context creation. In SDL2 vsync is applied
    // separately via SDL_GL_SetSwapInterval() once the context exists.
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, rDepth );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, gDepth );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, bDepth );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, zDepth );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
}
#endif

static bool lowlevel_sr_InitDisplay(){
#ifndef DEDICATED
    sr_ApplyGraphicsBackendSetting();

    rScreenSize & res = currentScreensetting.fullscreen ? currentScreensetting.res : currentScreensetting.windowSize;

    // update pixel aspect ratio
    if ( res.res != ArmageTron_Invalid && size_t(res.res) < sizeof(aspect)/sizeof(aspect[0]) )
        currentScreensetting.aspect = aspect[res.res];

    // ponytail: SDL2 build always uses SDL window/context path
    currentScreensetting.useSDL = true;
    res.UpdateSize();
    sr_screenWidth = res.width;
    sr_screenHeight= res.height;

    // desktop color depth
    static int desktopCD_R = 5;
    static int desktopCD_G = 5;
    static int desktopCD_B = 5;
    static int desktopCD   = 16;
    // desktop resolution
    static int sr_desktopWidth = 0, sr_desktopHeight = 0;

    // determine those values
    if ( sr_desktopWidth == 0 && !sr_window )
    {
        // select sane defaults in case the following operation fails
        sr_desktopWidth = 800;
        sr_desktopHeight = 600;

        // SDL3: use desktop mode of the display under the mouse cursor
        {
            float mouseXf = 0, mouseYf = 0;
            SDL_GetGlobalMouseState( &mouseXf, &mouseYf );
            int mouseX = (int)mouseXf, mouseY = (int)mouseYf;
            int numDisplays = 0;
            SDL_DisplayID *displays = SDL_GetDisplays( &numDisplays );
            SDL_DisplayID displayID = displays && numDisplays > 0 ? displays[0] : 0;
            for ( int i = 0; i < numDisplays; ++i )
            {
                SDL_Rect bounds;
                if ( SDL_GetDisplayBounds( displays[i], &bounds ) &&
                     mouseX >= bounds.x && mouseX < bounds.x + bounds.w &&
                     mouseY >= bounds.y && mouseY < bounds.y + bounds.h )
                {
                    displayID = displays[i];
                    break;
                }
            }
            SDL_free( displays );

            const SDL_DisplayMode *dm = displayID ? SDL_GetCurrentDisplayMode( displayID ) : nullptr;
            if (dm)
            {
                sr_desktopWidth  = dm->w;
                sr_desktopHeight = dm->h;
                desktopCD    = dm->format ? __builtin_popcount(dm->format & 0xFF) : 24;
                desktopCD_R  = 8;
                desktopCD_G  = 8;
                desktopCD_B  = 8;
            }
        }
    }

    if (!sr_window)
    {
        {
            tOutput o("$game_name");
            tString s;
            s << o;
            SDL_SetWindowTitle(sr_window, s);
        }

        int singleCD_R	= 5;
        int singleCD_G	= 5;
        int singleCD_B	= 5;
        int zDepth		= 16;

        switch (currentScreensetting.colorDepth)
        {
        case ArmageTron_ColorDepth_16:
            // parameters already set for this depth
            break;
        case ArmageTron_ColorDepth_Desktop:
            {
                singleCD_R = desktopCD_R;
                singleCD_G = desktopCD_G;
                singleCD_B = desktopCD_B;
            }
            break;
        case ArmageTron_ColorDepth_32:
            singleCD_R	= 8;
            singleCD_G	= 8;
            singleCD_B	= 8;
            zDepth		= 32;
            break;
        }

        switch ( currentScreensetting.zDepth )
        {
        case ArmageTron_ColorDepth_16: zDepth = 16; break;
        case ArmageTron_ColorDepth_32: zDepth = 32; break;
        default: break;
        }

        if (currentScreensetting.useSDL && !sr_UsingMetalBackend())
        {
            sr_SetGLAttributes( singleCD_R, singleCD_G, singleCD_B, zDepth );
        }

        // if desktop resolution was selected, pick it
        if ( sr_screenWidth + sr_screenHeight == 0 )
        {
            sr_screenWidth = sr_desktopWidth;
            sr_screenHeight = sr_desktopHeight;
        }

        // SDL3: create window and GL context
        {
             SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
             if (sr_UsingMetalBackend())
                 flags |= SDL_WINDOW_METAL;
             else
                 flags |= SDL_WINDOW_OPENGL;
             int numDisplays = 0;
             SDL_DisplayID *displays = SDL_GetDisplays( &numDisplays );
             SDL_DisplayID displayID = displays && numDisplays > 0 ? displays[0] : 0;
             if ( sr_lastDisplayIndex > 0 )
             {
                 // recreating the window: stay on the same monitor
                 displayID = (SDL_DisplayID)sr_lastDisplayIndex;
             }
             else if ( displays )
             {
                 // first launch: open on the monitor under the mouse cursor
                 float mouseXf = 0, mouseYf = 0;
                 SDL_GetGlobalMouseState( &mouseXf, &mouseYf );
                 int mouseX = (int)mouseXf, mouseY = (int)mouseYf;
                 for ( int i = 0; i < numDisplays; ++i )
                 {
                     SDL_Rect bounds;
                     if ( SDL_GetDisplayBounds( displays[i], &bounds ) &&
                          mouseX >= bounds.x && mouseX < bounds.x + bounds.w &&
                          mouseY >= bounds.y && mouseY < bounds.y + bounds.h )
                     {
                         displayID = displays[i];
                         break;
                     }
                 }
             }
             SDL_free( displays );

  #ifndef FORCE_WINDOW
             bool const wantFullscreen = currentScreensetting.fullscreen;
  #else
             bool const wantFullscreen = false;
  #endif

             // SDL3: CreateWindow no longer takes position; set it after creation.
             int createW = sr_screenWidth;
             int createH = sr_screenHeight;
             if (wantFullscreen && displayID)
             {
                 SDL_Rect bounds;
                 if (SDL_GetDisplayBounds(displayID, &bounds))
                 {
                     createW = bounds.w;
                     createH = bounds.h;
                 }
             }

             sr_window = SDL_CreateWindow("Armagetron Advanced", createW, createH, flags);
            if (!sr_window)
            {
                lastError.Clear();
                lastError << "Couldn't create window: ";
                lastError << SDL_GetError();
                std::cerr << lastError << '\n';
                return false;
            }

             // Position window on the correct display
             if (displayID && !wantFullscreen)
                 SDL_SetWindowPosition(sr_window,
                     SDL_WINDOWPOS_CENTERED_DISPLAY(displayID),
                     SDL_WINDOWPOS_CENTERED_DISPLAY(displayID));

             if (wantFullscreen)
             {
                 if (!SDL_SetWindowFullscreen(sr_window, true))
                 {
                     // fall back to windowed mode if fullscreen fails
                     currentScreensetting.fullscreen = false;
                 }
             }

             // set the window icon (SDL2 needs an existing window, and the window
             // is recreated on every mode change, so set it here each time)
             {
                 rSurface icon( "textures/icon.png" );
                 if ( icon.GetSurface() )
                     SDL_SetWindowIcon( sr_window, icon.GetSurface() );
             }

             if (!sr_CreateGraphicsContext())
             {
                 lastError.Clear();
                 lastError << "Couldn't create graphics context: ";
                 lastError << SDL_GetError();
                 std::cerr << lastError << '\n';
                 SDL_DestroyWindow(sr_window);
                 sr_window = NULL;
                 return false;
             }
             SDL_StartTextInput(sr_window);

             if (!sr_UsingMetalBackend())
             {
                 // apply vsync (SDL2: set after context creation, not via GL attribute)
                 switch (currentScreensetting.vSync)
                 {
                 case ArmageTron_VSync_On:
                     if ( !SDL_GL_SetSwapInterval( -1 ) )
                         SDL_GL_SetSwapInterval( 1 );
                     break;
                 case ArmageTron_VSync_Off:
                 case ArmageTron_VSync_MotionBlur:
                     SDL_GL_SetSwapInterval( 0 );
                     break;
                 case ArmageTron_VSync_Default:
                     break;
                 }
             }

             int windowW = 0;
             int windowH = 0;
             int drawableW = 0;
             int drawableH = 0;
             SDL_GetWindowSize( sr_window, &windowW, &windowH );
             SDL_GetWindowSizeInPixels( sr_window, &drawableW, &drawableH );

             // ponytail: render and viewport sizes should follow actual drawable size
             sr_screenWidth = drawableW > 0 ? drawableW : windowW;
             sr_screenHeight = drawableH > 0 ? drawableH : windowH;
         }
    }

    // sanity check texture modes
    for(int i=rTextureGroups::TEX_GROUPS-1; i>=0; --i)
    {
        int & texmode = rTextureGroups::TextureMode[i];

        // don't do anything for deliberately disabled textures
        if( i == rTextureGroups::TEX_FONT || texmode >= 0 )
        {
            // to default if the modes have been reset for some reason
            if( texmode == 0 )
            {
                texmode = default_texturemode;
            }
            if( texmode < GL_NEAREST )
            {
                texmode = GL_NEAREST;
            }
            if( texmode > GL_LINEAR_MIPMAP_LINEAR )
            {
                texmode = GL_LINEAR_MIPMAP_LINEAR;
            }
        }
    }

#ifdef DIRTY
    if (!currentScreensetting.useSDL)
        if(!rSysDep::InitGL()) return false;
#endif

#ifndef DEDICATED
    gl_vendor.SetLen(0);
    gl_renderer.SetLen(0);
    gl_version.SetLen(0);
    gl_extensions.SetLen(0);
    renderer_identification.SetLen(0);

    if (sr_UsingMetalBackend())
    {
        gl_vendor << "Apple";
        gl_renderer << "Metal (experimental)";
        gl_version << "Metal";
        gl_extensions << "";
        sr_blacklistDisplayLists = true;
    }
    else
    {
        gl_vendor     << reinterpret_cast<const char *>(glGetString(GL_VENDOR));
        gl_renderer   << reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        gl_version    << reinterpret_cast<const char *>(glGetString(GL_VERSION));
        gl_extensions << reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));

        // display list blacklist
        sr_blacklistDisplayLists=false;

        if(strstr(gl_version,"Mesa 7.0") || strstr(gl_version,"Mesa 7.1"))
            sr_blacklistDisplayLists=true;

        if(strstr(gl_vendor,"SiS"))
            sr_blacklistDisplayLists=true;
    }

   
#ifndef WIN32
    if(!strstr(gl_renderer,"Voodoo3"))
#endif
    {
        if(currentScreensetting.fullscreen)
            SDL_HideCursor();
        else
            SDL_ShowCursor();
    }

#ifdef WIN32
    renderer_identification << "WIN32 ";
#else
#ifdef MACOSX
    renderer_identification << "MACOSX ";
#else
    renderer_identification << "LINUX ";
#endif
#endif
    renderer_identification << rRenderIdCallback::RenderId() << ' ';
    if (sr_UsingMetalBackend())
        renderer_identification << "SDL 3 Metal (experimental)\n";
    else
#ifdef SDL_OPENGL
    renderer_identification << "SDL 2\n";
    renderer_identification << "USE_SDL=" << currentScreensetting.useSDL
    << '\n';
#else
    renderer_identification << "SDL 2\n";
#endif
    renderer_identification << "CD=" << currentScreensetting.colorDepth  << '\n';
    renderer_identification << "FS=" << currentScreensetting.fullscreen  << '\n';
    renderer_identification << "GL_VENDOR=" << gl_vendor   << '\n';
    renderer_identification << "GL_RENDERER=" << gl_renderer << '\n';
    renderer_identification << "GL_VERSION=" << gl_version  << '\n';
#endif

    if (// test for Windows software GL (be a little flexible...)
        (
            strstr(gl_vendor,"icrosoft") || strstr(gl_vendor,"SGI")
        )
        && strstr(gl_renderer,"eneric")
    )
        software_renderer=true;

    if ( // test for Mesa software GL
        strstr(gl_vendor,"rian") && strstr(gl_renderer,"X11") &&
        strstr(gl_renderer,"esa")
    )
        software_renderer=true;

    if ( // test for Mesa software GL, new versions
        strstr(gl_vendor,"Mesa") &&
        strstr(gl_renderer,"Software Rasterizer")
        )
        software_renderer=true;

    if ( // test for GLX software GL
        strstr(gl_renderer,"GLX") &&
        strstr(gl_renderer,"ndirect") &&
        strstr(gl_renderer,"esa")
    )
        software_renderer=true;

    // disable storage of non-alpha textures on Savage MX
    if ( strstr( gl_renderer, "SavageMX" ) )
    {
        rISurfaceTexture::storageHack_ = true;
    }

    // fonts look best in bilinear filtering, no mipmaps
    if ( rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] > GL_LINEAR )
        rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] = GL_LINEAR;

    // disable trilinear filtering for ATI cards
    if ( strstr( gl_vendor, "ATI" ) )
    {
        default_texturemode = GL_LINEAR_MIPMAP_NEAREST;
    }

    // wait for activation if we were ALT-Tabbed away:
    // SDL2: SDL_GetAppState is deprecated, poll window events instead
    {
        SDL_Event evt;
        bool focused = false;
        SDL_Delay(100);
        while (SDL_PollEvent(&evt))
        {
            if (evt.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                focused = true;
        }
    }

    if (software_renderer && !last_software_renderer)
        sr_LoadDefaultConfig();

    last_software_renderer=software_renderer;


    // wait for activation if we were ALT-Tabbed away:
    {
        SDL_Event evt;
        SDL_Delay(100);
        while (SDL_PollEvent(&evt))
        {
            // drain events
        }
    }

    sr_ResetRenderState(true);

    rCallbackAfterScreenModeChange::Exec();

    lastSuccess=currentScreensetting;
    failed_attempts = 0;
    sr_useDirectX = use_directx_back;
    st_SaveConfig();
#endif
    return true;
}

bool cycleprograminited = false;

bool sr_InitDisplay(){
    use_directx_back = sr_useDirectX;

    cycleprograminited = false;
    while (failed_attempts <= MAXEMERGENCY+1)
    {
        if (failed_attempts)
        {
#ifdef DEBUG
            std::cout << "failed attempts:" << failed_attempts << "\n";
            std::cout.flush();
#endif
            currentScreensetting = *emergency[failed_attempts];

            sr_useDirectX = false;
        }

        // prepare for crash, note failure and save config
        failed_attempts++;
        st_SaveConfig();


#ifdef MACOSX
        // init the screen once in windowed mode
        static bool first = true;
        if ( first && currentScreensetting.fullscreen )
        {
            first = false;
            currentScreensetting.fullscreen = false;

            if (lowlevel_sr_InitDisplay())
            {
                sr_ExitDisplay();
            }

            currentScreensetting.fullscreen = true;
        }
#endif

        if (lowlevel_sr_InitDisplay())
        {
            return true;
        }

        st_SaveConfig();

        if (lowlevel_sr_InitDisplay())
        {
            return true;
        }


    }

    failed_attempts = 1;
    st_SaveConfig();

    tERR_ERROR("\nSorry, played all my cards trying to "
               "initialize your video system.\n"
               << tOutput("$program_name") << " won't run on your computer. Reason:\n\n"
               << lastError
               << "\n\nI'll try again from the beginning, but the "
               << "chances of success are minimal.\n"
              );

    return false;
}


void sr_ExitDisplay(){
#ifndef DEDICATED
    rCallbackBeforeScreenModeChange::Exec();

#ifdef DIRTY
    rSysDep::ExitGL();
#endif

    if (sr_window){
        // remember the monitor so the recreated window stays put
        int displayIndex = (int)SDL_GetDisplayForWindow(sr_window);
        if (displayIndex >= 0)
            sr_lastDisplayIndex = displayIndex;
        sr_DestroyGraphicsContext();
        SDL_DestroyWindow(sr_window);
        sr_window = NULL;
    }
#endif
}

bool    sr_alphaBlend=true;
bool    sr_glOut=true;
bool    sr_smoothShading=true;


int sr_floorMirror=0;
int sr_floorDetail=rFLOOR_TEXTURE;
bool sr_highRim=true;
bool sr_upperSky=false;
bool sr_lowerSky=false;
bool sr_skyWobble=true;
bool sr_dither=true;
bool sr_infinityPlane=false;
bool sr_laggometer=true;
bool sr_predictObjects=false;
bool sr_texturesTruecolor=false;

bool sr_textOut=false;
bool sr_FPSOut=true;
bool sr_RecordingTimeOut=true;

bool sr_keepWindowActive=true;

tString renderer_identification;

void sr_LoadDefaultConfig(){

    // High detail defaults; no problem for your ordinary 3d-card.
    sr_alphaBlend=true;
    sr_useDisplayLists=rDisplayList_Off;
    sr_textOut=true;
    sr_dither=true;
    sr_smoothShading=true;
    int i;
#ifndef DEDICATED
    for (i=rTextureGroups::TEX_GROUPS-1;i>=0;i--)
        rTextureGroups::TextureMode[i]=default_texturemode;

    // fonts look best in bilinear filtering, no mipmaps
    rTextureGroups::TextureMode[rTextureGroups::TEX_FONT]=GL_LINEAR;
#endif
    sr_floorDetail=rFLOOR_TWOTEXTURE;
    sr_floorMirror=rMIRROR_OFF;
    sr_infinityPlane=false;
    sr_lowerSky=false;
    sr_upperSky=false;
    sr_keepWindowActive=true;
    rSysDep::swapMode_=rSysDep::rSwap_glFinish;

    if (software_renderer){
        // A software renderer! Poor soul. Set low details:
        for (i=rTextureGroups::TEX_GROUPS-1;i>=0;i--)
            rTextureGroups::TextureMode[i]=-1;

#ifndef DEDICATED
        rTextureGroups::TextureMode[rTextureGroups::TEX_OBJ]=GL_NEAREST_MIPMAP_NEAREST;
        rTextureGroups::TextureMode[rTextureGroups::TEX_FONT]=GL_NEAREST_MIPMAP_NEAREST;
#endif

        sr_highRim=false;
        sr_dither=false;
        sr_alphaBlend=false;
        sr_smoothShading=true; // smooth shading does not slow down the
        // two tested renderers; leave it it.
        sr_floorDetail=rFLOOR_GRID;
        sr_floorMirror=rMIRROR_OFF;
    }
    else if(strstr(gl_vendor,"3Dfx")){
        //workaround for 3dfx renderer: aliasing must be turned on
        //sr_lineAntialias=rFEAT_OFF;
    }
    else if(strstr(gl_vendor,"NVIDIA")){
        // infinity , display lists and glFlush swapping work for NVIDIA
        sr_infinityPlane=true;
        sr_useDisplayLists=rDisplayList_CAC;
        rSysDep::swapMode_=rSysDep::rSwap_glFlush;
    }
#ifdef MACOSX
    else if(strstr(gl_vendor,"ATI")){
        // glFlush swapping work for ATI on the mac
        rSysDep::swapMode_=rSysDep::rSwap_glFlush;
    }
#endif
    else if(strstr(gl_vendor,"Matrox")){
        sr_floorDetail = rFLOOR_TEXTURE;  // double textured floor does not work
    }

    /*
    else if(strstr(gl_version,"Mesa"))
    {
        sr_useDisplayLists=rDisplayList_Off;
    }
    */
}

void sr_ResetRenderState(bool menu){
    if(!sr_glOut)
        return;
#ifndef DEDICATED
    if (sr_UsingMetalBackend())
    {
        sr_MetalBeginFrame();
        rMatrixState::ResetAll();

        rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] = GL_LINEAR;

        if (menu)
        {
            sr_metal_glDisable(GL_DEPTH_TEST);
            sr_metal_glViewport(0, 0, GLsizei(sr_screenWidth), GLsizei(sr_screenHeight));
        }
        else
        {
            sr_metal_glEnable(GL_DEPTH_TEST);
            sr_metal_glDepthFunc(GL_LEQUAL);
        }

        sr_metal_glDisable(GL_TEXTURE_2D);

        if (sr_alphaBlend)
        {
            sr_metal_glEnable(GL_ALPHA_TEST);
            sr_metal_glAlphaFunc(GL_GREATER, 0);
            sr_metal_glEnable(GL_BLEND);
            sr_metal_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            sr_metal_glDisable(GL_ALPHA_TEST);
            sr_metal_glDisable(GL_BLEND);
        }

        sr_metal_glMatrixMode(GL_TEXTURE);
        sr_metal_glLoadIdentity();
        sr_metal_glMatrixMode(GL_PROJECTION);
        sr_metal_glLoadIdentity();
        sr_metal_glMatrixMode(GL_MODELVIEW);
        sr_metal_glLoadIdentity();
        return;
    }

    // Z-Buffering and perspective correction

    if (menu){
        glDisable(GL_DEPTH_TEST);
        glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
        glViewport (0, 0, GLsizei(sr_screenWidth), GLsizei(sr_screenHeight));
    }
    else{
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    }

    if (sr_dither)
        glEnable(GL_DITHER);
    else
        glDisable(GL_DITHER);

    glDisable(GL_LIGHTING);

    // disable texture mapping (selecting textures will reactivate it)

    //  glDisable(GL_TEXTURE);
    glDisable(GL_TEXTURE_2D);


    // flat or smooth shading
    if (sr_smoothShading)
        glShadeModel(GL_SMOOTH);
    else
        glShadeModel(GL_FLAT);

    // alpha blending
    if (sr_alphaBlend){
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER,0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    }
    else{
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
    }

    // reset matrices
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
}


/*
static uMenuItemFunction apply
(&sg_screenMenu_mode,"Apply Changes",
"This activates the changes to the resolution and fullscreen/windowed mode "
"made above. This does not work on all systems; exit and reenter Armagetron "
"instead if you experience problems.",
 sr_ReinitDisplay);
*/





//static bool offs=false;

void sr_DepthOffset(bool offset){
    // return;
    //  if(offset!=offs){
    //offs=offset;
#ifndef DEDICATED
    if (offset){
        //glMatrixMode(GL_PROJECTION);
        //glScalef(.9,.9,.9);
        glPolygonOffset(-2,-5);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glEnable(GL_POLYGON_OFFSET_POINT);
        glEnable(GL_POLYGON_OFFSET_FILL);
    }
    else{
        glPolygonOffset(0,0);
        glDisable(GL_POLYGON_OFFSET_POINT);
        glDisable(GL_POLYGON_OFFSET_LINE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        //glMatrixMode(GL_PROJECTION);
        //glScalef(1/.9,1/.9,1/.9);
    }
    //  }
#endif
}

// set activation staus
void sr_Activate(bool active)
{
#ifndef DEDICATED
    if ( !currentScreensetting.fullscreen && !active && sr_keepWindowActive )
    {
        sr_glOut=!active;
    }
    else
    {
        sr_glOut=active;
    }

    // unload textures and stuff if rendering gets disabled
    if (!sr_glOut)
        rCallbackBeforeScreenModeChange::Exec();

    // Jonathans fullscreen bugfix.
    // z-man's ammendmend: apparently, doing this in Linux is painful as well.
    // Only on Windows, you get a deactivation event when you ALT-TAB away
    // from th application, then iconification is the right thing to do.
    // On Linux at least, there is no standard alt-tab for fullscreen applications.
#ifdef WIN32
    if ( currentScreensetting.fullscreen && !active )
    {
        SDL_MinimizeWindow(sr_window);
    }
#endif
#endif
}

//**************************************
//** Screen mode callbacks            **
//**************************************


static tCallback *sr_BeforeAnchor;

rCallbackBeforeScreenModeChange::rCallbackBeforeScreenModeChange(VOIDFUNC *f)
        :tCallback(sr_BeforeAnchor, f){}

void rCallbackBeforeScreenModeChange::Exec()
{
    tCallback::Exec(sr_BeforeAnchor);
}

static tCallback *sr_AfterAnchor;

rCallbackAfterScreenModeChange::rCallbackAfterScreenModeChange(VOIDFUNC *f)
        :tCallback(sr_AfterAnchor, f){}

void rCallbackAfterScreenModeChange::Exec()
{
    tCallback::Exec(sr_AfterAnchor);
}
