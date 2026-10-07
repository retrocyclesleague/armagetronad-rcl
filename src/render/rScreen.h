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

#ifndef ArmageTron_SCREEN_H
#define ArmageTron_SCREEN_H

#include "tString.h"
#include "tCallback.h"
#include "tCallbackString.h"

typedef enum {
    ArmageTron_Desktop=0,ArmageTron_320_200,ArmageTron_Min=ArmageTron_320_200, ArmageTron_320_240,ArmageTron_400_300,
    ArmageTron_512_384,ArmageTron_640_480,ArmageTron_800_600,
    ArmageTron_1024_768,ArmageTron_1280_800,ArmageTron_1280_854,ArmageTron_1280_1024,
    ArmageTron_1600_1200,ArmageTron_1680_1050,ArmageTron_2048_1572,ArmageTron_Custom, ArmageTron_Invalid=-1
}
rResolution;

typedef enum {
    ArmageTron_ColorDepth_16, ArmageTron_ColorDepth_Desktop,
    ArmageTron_ColorDepth_32
}
rColorDepth;

typedef enum {
    ArmageTron_VSync_On, ArmageTron_VSync_Default, ArmageTron_VSync_Off,
    ArmageTron_VSync_MotionBlur
}
rVSync;

struct rScreenSize
{
    rResolution         res;
    int                 width, height;

    rScreenSize( int width, int height ); //!< constructor
    explicit rScreenSize( rResolution r = ArmageTron_Invalid ); //!< constructor
    void UpdateSize();                                          //!< update size from res enum

    bool operator ==( rScreenSize const & other ) const; //!< comparison operator
    bool operator !=( rScreenSize const & other ) const; //!< comparison operator

    int Compare( rScreenSize const & other ) const; //!< comparison function
};

class rScreenSettings
{
public:
    rScreenSize			res;
    rScreenSize			windowSize;
    bool				fullscreen;
    rColorDepth			colorDepth;
    rColorDepth			zDepth;
    bool				useSDL;
    bool				checkErrors;
    rVSync              vSync;          // whether to wait for vsync
    REAL				aspect;			// aspect ratio of pixels ( width/height )

    rScreenSettings(rResolution r,
                    bool fs=true,
                    rColorDepth cd=ArmageTron_ColorDepth_Desktop,
                    bool sdl=true,
                    bool ce =true);
};

bool sr_DesktopScreensizeSupported();

extern rScreenSettings currentScreensetting;
extern rScreenSettings lastSuccess;

struct SDL_Surface;
extern SDL_Surface *sr_screen;

// SDL 1.2 reports window/input coordinates in logical pixels. On a HiDPI
// display, the OpenGL drawable can be larger; keep both so input and layout
// remain stable while rendering uses every physical pixel.
extern int sr_screenWidth,sr_screenHeight;
extern int sr_renderWidth,sr_renderHeight;

// size in pixels of the viewport that is currently selected; text is placed
// on whole pixels of it
extern int sr_viewportPixelWidth,sr_viewportPixelHeight;

//! Refresh physical drawable dimensions after SDL creates a GL context.
void sr_UpdateRenderDimensions();

extern bool sr_alphaBlend;
extern bool sr_screenshotIsPlanned;
extern bool sr_smoothShading;

extern bool sr_glOut;           // do we have gl-output at all?
extern bool sr_textOut;          // display game text graphically?
extern bool sr_FPSOut;           // display frame counter?
extern bool sr_RecordingTimeOut;  // display recording/playback timer?

//! how should caching display lists be used?
enum rDisplayListUsage
{
    rDisplayList_Off=0, // not at all
    rDisplayList_CAC,   // yes, with GL_COMPILE, then glCallList.
    rDisplayList_CAE,   // yes, with GL_COMPILE_AND_EXECUTE
    rDisplayList_Count
};

extern rDisplayListUsage sr_useDisplayLists;   // use GL display lists
extern bool sr_blacklistDisplayLists;   // use GL display lists (override for buggy implementations)
// not delete the screen, just pait the background with depth test
// disabled. Gives 20% speedup.


#define rMIRROR_OFF     0
#define rMIRROR_OBJECTS 1
#define rMIRROR_WALLS   2
#define rMIRROR_ALL     10

extern int sr_floorMirror;

// The clean arena look: a pale, glossy open floor without grid or rim walls
// under a low sun, with solid trails that cast long soft shadows.
extern bool sr_cleanArena;
extern int sr_antialias;                   //!< multisample count, applied at display init

// The clean arena at night: the same open floor and solid trails on pure
// black. No sky, no sun and so no shadows; text stays light.
extern bool sr_cleanDark;

//! which arena is drawn: 0 the classic grid, 1 the pale clean arena, 2 the dark one.
//! Whatever caches drawing of one look compares this.
int sr_CleanArenaLook();

//! floor at the viewer's feet; also what the floor mirrors where nothing stands
REAL const * sr_CleanFloorColor();
extern const REAL sr_cleanShadowColor[3];  //!< shadows on the pale floor; the dark one has none

//! True while what is drawn lies straight on the clean arena's pale floor and
//! sky, until something covers them (a menu) or the frame ends. Text drawn
//! meanwhile has its light colours darkened to ink. Never set in the dark one.
extern bool sr_cleanInk;

//! says that the clean arena is being drawn into the current frame
void sr_CleanArenaDrawn();

//! true if the clean arena was on screen within the last moments
bool sr_CleanArenaRecent();

//! darkens a text colour to ink, keeping its hue
void sr_CleanInk( REAL & r, REAL & g, REAL & b );

//! The colour a player's or a zone's own colour takes in the clean arena:
//! with the glare taken out, so it sits on the pale floor like paint rather
//! than like light. On the dark floor it stays a light, and one too dim to
//! see there is brought up.
void sr_CleanPaint( REAL & r, REAL & g, REAL & b );

#define rFLOOR_OFF        0
#define rFLOOR_GRID       1
#define rFLOOR_TEXTURE    2
#define rFLOOR_TWOTEXTURE 3

extern int sr_floorDetail;

#define rFEAT_OFF    -1
#define rFEAT_DEFAULT 0
#define rFEAT_ON      1

extern bool sr_highRim;
extern bool sr_upperSky,sr_lowerSky;
extern bool sr_skyWobble;
extern bool sr_dither;
extern bool sr_infinityPlane;
extern bool sr_laggometer;
extern bool sr_predictObjects;
extern bool sr_texturesTruecolor;
extern bool sr_keepWindowActive;

extern tString renderer_identification;  // type of renderer used

extern tString gl_vendor;
extern tString gl_renderer;
extern tString gl_version;
extern tString gl_extensions;

class rPerFrameTask:public tCallback{
public:
    rPerFrameTask(VOIDFUNC *f);
    static void DoPerFrameTasks();
};

class rRenderIdCallback:public tCallbackString{
public:
    rRenderIdCallback(STRINGRETFUNC *f);
    static tString RenderId();
};

class rCallbackBeforeScreenModeChange:public tCallback{
public:
    rCallbackBeforeScreenModeChange(VOIDFUNC *f);
    static void Exec();
};

class rCallbackAfterScreenModeChange:public tCallback{
public:
    rCallbackAfterScreenModeChange(VOIDFUNC *f);
    static void Exec();
};

bool sr_InitDisplay();
void sr_ExitDisplay();
void sr_ReinitDisplay();

void sr_LoadDefaultConfig();

void sr_ResetRenderState(bool menu=0);
void sr_DepthOffset(bool offset);
void sr_Activate(bool active); // set activation staus

void sr_LockSDL();
void sr_UnlockSDL();
#endif
