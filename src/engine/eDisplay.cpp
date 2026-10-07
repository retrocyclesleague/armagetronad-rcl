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

//void se_FetchAndStoreSDLInput();

#include "rSDL.h"

#include "tConfiguration.h"

// floor mirror
#ifndef DEDICATED
static REAL sr_floorMirror_strength=.1;
static tSettingItem<REAL> f_m("FLOOR_MIRROR_INT",sr_floorMirror_strength);

#include "tEventQueue.h"
#include "uInputQueue.h"
#include "eTess2.h"
#include "rTexture.h"
#include "eGameObject.h"
#include "rFont.h"
#include "eTimer.h"
#include "eCamera.h"
#include "eSensor.h"
#include "rScreen.h"
#include "rRender.h"
#include "eWall.h"
#include "eAdvWall.h"
#include "eFloor.h"
#include "ePath.h"
#include "eGrid.h"
#include "eDebugLine.h"
#include "tDirectories.h"
#include "eRectangle.h"

#define eWall_h 4
#define view_h 2.7

#ifdef DEBUG
bool debug_grid=0;
#endif

REAL upper_height=100;
REAL lower_height=50;

static tSettingItem<REAL> sec_upperSkyHeight("UPPER_SKY_HEIGHT",upper_height);
static tSettingItem<REAL> sec_lowerSkyHeight("LOWER_SKY_HEIGHT",lower_height);


#ifndef DEDICATED

static rFileTexture sky(rTextureGroups::TEX_FLOOR,"textures/sky.png",1,1,true);
static rFileTexture sky_moviepack(rTextureGroups::TEX_FLOOR,"moviepack/sky.png",1,1,true);

extern bool sg_MoviePack();

// select the lower sky
static rFileTexture & se_Sky()
{
    static char const * skyPath="textures/sky.png";
    static char const * skyPathMoviepack="moviepack/sky.png";
    static rFileTexture sky(rTextureGroups::TEX_FLOOR,skyPath,1,1,true);
    static rFileTexture sky_moviepack(rTextureGroups::TEX_FLOOR,skyPathMoviepack,1,1,true);

    if (sg_MoviePack()){
        // Since old movie packs usually don't include sky.png we need to
        // be nice and fall back to the default sky tecture. -k
        tString s = tDirectories::Data().GetReadPath( skyPathMoviepack );
        if(strlen(s) > 0)
            return sky_moviepack;
    }

    return sky;
}

static void se_SelectSky()
{
    static rFileTexture & sky = se_Sky();
    sky.Select();
}

static REAL se_upperSkyScale=1;
static REAL se_upperSkyColorR=.5;
static REAL se_upperSkyColorG=.5;
static REAL se_upperSkyColorB=1;
static tSettingItem<REAL> sec_upperSkyScale("UPPER_SKY_SCALE",se_upperSkyScale);
static tSettingItem<REAL> sec_upperSkyColorR("UPPER_SKY_RED",se_upperSkyColorR);
static tSettingItem<REAL> sec_upperSkyColorG("UPPER_SKY_GREEN",se_upperSkyColorG);
static tSettingItem<REAL> sec_upperSkyColorB("UPPER_SKY_BLUE",se_upperSkyColorB);

// select the upper sky
static rFileTexture * se_UpperSky()
{
    static char const * skyPath="textures/upper_sky.png";
    static char const * skyPathMoviepack="moviepack/upper_sky.png";
    static rFileTexture sky(rTextureGroups::TEX_FLOOR,skyPath,1,1,true);
    static rFileTexture sky_moviepack(rTextureGroups::TEX_FLOOR,skyPathMoviepack,1,1,true);

    if (sg_MoviePack()){
        tString s = tDirectories::Data().GetReadPath( skyPathMoviepack );
        if(s.Len() > 1)
            return &sky_moviepack;
    }

    if( tDirectories::Data().GetReadPath( skyPath ).Len() > 1 ){
        return &sky;
    }

    return NULL;
}

static void se_SelectUpperSky()
{
    static rFileTexture * sky = se_UpperSky();
    if( sky )
    {
        sky->Select();
    }
    else
    {
        se_glFloorTexture();
    }
}

// if the rip bug is activated, don't use the rim to draw the floor
extern short se_bugRip;

// passes a vertex with z-projected texture coordinates to OpenGL
static inline void TexVertex( REAL x, REAL y, REAL h)
{
    glTexCoord2f(x, y);
    glVertex3f  (x, y, h);
}

// renders a finite rectangle
static void finite_xy_plane( const eCoord &pos,const eCoord &dir,REAL h, eRectangle rect )
{
    // expand plane to camera position to avoid embarrasing reflection bug
    if ( sr_floorMirror || sr_cleanArena )
        rect.Include( pos );

    // fetch rectangle coordinates
    REAL lx = rect.GetLow().x;
    REAL ly = rect.GetLow().y;
    REAL hx = rect.GetHigh().x;
    REAL hy = rect.GetHigh().y;

    // draw rectangle as triangle fan (good for avoiding artefacts near pos)
    BeginTriangleFan();
    TexVertex( pos.x-dir.x, pos.y-dir.y, h );
    TexVertex(lx, ly, h);
    TexVertex(lx, hy, h);
    TexVertex(hx, hy, h);
    TexVertex(hx, ly, h);
    TexVertex(lx, ly, h);
    RenderEnd();
}

static void infinity_xy_plane(eCoord const & pos, const eCoord &dir,REAL h=0){
    bool use_rim=false;
    REAL zero=0;

    if (sr_highRim)
        use_rim=true;

    if ( se_bugRip )
        use_rim=false;

    // always use the rim if infinity rendering is turned off
    use_rim |= !sr_infinityPlane;

    if (use_rim){
        /*
          // the rim wall based rendering does not work properly for shaped arenas, so
          // it's been replaced.

                BeginTriangles();
                for(int i=se_rimWalls.Len()-1;i>=0;i--){
                    eCoord p1=se_rimWalls(i)->EndPoint(0);
                    eCoord p2=se_rimWalls(i)->EndPoint(1);

                    glTexCoord2f(pos.x, pos.y);
                    glVertex3f  (pos.x, pos.y, h);

                    glTexCoord2f(p1.x, p1.y);
                    glVertex3f  (p1.x, p1.y, h);

                    glTexCoord2f(p2.x, p2.y);
                    glVertex3f  (p2.x, p2.y, h);
                }
                RenderEnd();
        */
        finite_xy_plane( pos, dir, h, eWallRim::GetBounds() );
    }
    else
    {
        if (!sr_infinityPlane)
            zero=.001;

        BeginTriangleFan();

        glTexCoord4f(pos.x-dir.x, pos.y-dir.y, h, 1);
        glVertex4f  (pos.x-dir.x, pos.y-dir.y, h, 1);

        glTexCoord4f(1,0.1,zero*h,zero);
        glVertex4f  (1,0.1,zero*h,zero);

        glTexCoord4f(0.1,1.1,zero*h,zero);
        glVertex4f  (0.1,1.1,zero*h,zero);

        glTexCoord4f(-1,0.1,zero*h,zero);
        glVertex4f  (-1,0.1,zero*h,zero);

        glTexCoord4f(0.1,-1.1,zero*h,zero);
        glVertex4f  (0.1,-1.1,zero*h,zero);

        glTexCoord4f(1,0.1,zero*h,zero);
        glVertex4f  (1,0.1,zero*h,zero);

        RenderEnd();
    }
}

static REAL z=0;


int           eGrid::NumberOfCameras(){return cameras.Len();}
const eCoord& eGrid::CameraPos(int i){return cameras(i)->CameraPos();}
eCoord eGrid::CameraGlancePos(int i){return cameras(i)->CameraGlancePos();}
const eCoord& eGrid::CameraDir(int i){return cameras(i)->CameraDir();}
REAL          eGrid::CameraHeight(int i){return cameras(i)->CameraZ();}




class eZNearSensor: public eSensor
{
public:
    static bool AdaptZNear( REAL & zNear, eWall const * wall, eCamera const * camera )
    {
        REAL len = wall->Len();
        if ( len > .01)
        {
            REAL zDist = ::z - wall->Height();
            if ( zDist < zNear )
            {
                const eCoord& camPos = camera->CameraPos();
                const eCoord& camDir = camera->CameraDir();
                eCoord base = wall->EndPoint(0);
                eCoord end = wall->EndPoint(1);

                if ( eCoord::F( base-camPos, camDir ) > 0.01f || eCoord::F( end-camPos, camDir ) > 0.01f )
                {
                    eCoord dirNorm = end - base;
                    dirNorm.Normalize();
                    eCoord camRelative = ( camPos - base ).Turn( dirNorm.Conj() );
                    REAL dist = fabs( camRelative.y );
                    if ( camRelative.x < 0 )
                    {
                        dist -= camRelative.x;
                    }
                    if ( camRelative.x > len )
                    {
                        dist += camRelative.x - len;
                    }
                    if ( dist < zDist )
                    {
                        dist = zDist;
                    }
                    // TODO: better criterion for ingoring of walls
                    if ( dist < zNear && dist > 0.001f )
                    {
                        zNear = dist;
                        return true;
                    }
                }
            }
        }

        return false;
    }

    eZNearSensor(eGameObject *o,const eCoord &start,const eCoord &d, REAL & zNear, eCamera const * camera )
    :eSensor( o, start, d ), zNear_( zNear ), camera_( camera )
    {}

    void Detect()
    {
        detect( zNear_ * 2 );
    }

    // called when passing an edge
    void PassEdge( const eWall * w, REAL time, REAL, int)
    {
        // adapt zNear and be done
        if( AdaptZNear( zNear_, w, camera_ ) )
        {
            throw eSensorFinished();
        }
    }
private:
    REAL & zNear_; // the reference to the near clipping plane value
    eCamera const * camera_; // the camera currently in use
};

void paint_sr_lowerSky(eGrid *grid, int viewer,bool sr_upperSky, eCoord const & camPos){
    TexMatrix();
    glLoadIdentity();
    glScalef(.005,.005,.005);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);

    if (sr_skyWobble){
        glTranslatef(se_GameTime()*.1,se_GameTime()*.07145,0);
        glScalef(1+.2*sin(se_GameTime()),1+.1*cos(se_GameTime()),1);
        glTranslatef(-300,-200,0);
    }

    se_SelectSky();

    REAL sa=(lower_height-z)*.1;
    if (sa>1) sa=1;
    if (!sr_upperSky){
        sa=1;
        glBlendFunc(GL_SRC_ALPHA,GL_ZERO);
    }
    if (sa>0){
        glColor4f(1,1,1,sa);
        infinity_xy_plane(camPos,grid->CameraDir(viewer),lower_height);
    }
    if (!sr_upperSky && sr_alphaBlend)
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}

// ---- the clean arena: an open, glossy plane under a low sun ----

void se_CleanArenaSun( eCoord & toSun, REAL & reach )
{
    // The bearing is fixed in the arena and lies off the grid axes, so walls
    // in either direction throw a shadow with some width.
    toSun = eCoord( -.8f, .6f );
    reach = 3.2f;
}

namespace
{
// everything here is drawn around the eye and far beyond the arena, without
// depth, so it stays put while the camera moves
const REAL se_cleanFar = 20000;

// A glow around a direction: rings of the given angular radius, in degrees,
// each blended towards one colour by its own share.
void se_CleanArenaGlow( eCoord const & eye, REAL eyeHeight, REAL dx, REAL dy, REAL dz,
                        REAL const * radius, REAL const * share, int rings,
                        REAL r, REAL g, REAL b )
{
    // two directions across the one the glow lies in
    REAL const level = sqrt( dx * dx + dy * dy );
    if ( !( level > 1E-4f ) )
        return;
    REAL const ux = dy / level, uy = -dx / level;
    REAL const vx = uy * dz, vy = -ux * dz, vz = ux * dy - uy * dx;

    static const int segments = 40;
    for ( int ring = 0; ring < rings - 1; ++ring )
    {
        BeginQuadStrip();
        for ( int i = 0; i <= segments; ++i )
        {
            REAL const angle = i * ( 2 * M_PI / segments );
            REAL const c = cos( angle ), s = sin( angle );
            for ( int edge = 0; edge < 2; ++edge )
            {
                REAL const rho = radius[ring + edge] * ( M_PI / 180 );
                REAL const along = cos( rho ) * se_cleanFar, across = sin( rho ) * se_cleanFar;
                glColor4f( r, g, b, share[ring + edge] );
                glVertex3f( eye.x + dx * along + ( ux * c + vx * s ) * across,
                            eye.y + dy * along + ( uy * c + vy * s ) * across,
                            eyeHeight + dz * along + vz * s * across );
            }
        }
        RenderEnd();
    }
}

// What the floor mirrors where nothing stands on it: its own plain colour,
// over the whole view.
void se_CleanArenaBackdrop()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_TEXTURE_2D);

    ProjMatrix();
    glPushMatrix();
    glLoadIdentity();
    REAL const * const floor = sr_CleanFloorColor();
    glColor4f( floor[0], floor[1], floor[2], 1 );
    BeginQuads();
    glVertex2f( -1, -1 );
    glVertex2f(  1, -1 );
    glVertex2f(  1,  1 );
    glVertex2f( -1,  1 );
    RenderEnd();
    glPopMatrix();
}

// where the sun stands, as a direction
void se_CleanArenaSunDirection( REAL & dx, REAL & dy, REAL & dz )
{
    eCoord toSun;
    REAL reach;
    se_CleanArenaSun( toSun, reach );
    REAL const norm = 1 / sqrt( reach * reach + 1 );
    dx = toSun.x * reach * norm;
    dy = toSun.y * reach * norm;
    dz = norm;
}

// The sky: haze over the horizon that clears upwards, and the sun in it.
void se_CleanArenaSky( eCoord const & eye, REAL eyeHeight )
{
    // it starts just under the horizon, where the far floor covers it; what
    // the floor mirrors further down must stay as it is
    static const int segments = 24;
    static const int rings = 11;
    static const REAL elevation[rings] = { -.5f, 0, 1, 2.5f, 6.5f, 8.5f, 10.5f, 12.5f, 20, 40, 90 };
    static const unsigned char day[rings][3] = {
        { 212, 211, 213 }, { 212, 211, 213 }, { 215, 212, 214 }, { 219, 214, 215 },
        { 219, 214, 215 }, { 211, 213, 215 }, { 203, 212, 215 }, { 193, 210, 214 },
        { 184, 205, 213 }, { 174, 198, 211 }, { 166, 193, 210 } };
    // night: a dim glow along the horizon that is gone a hand's width up
    static const unsigned char night[rings][3] = {
        { 40, 46, 58 }, { 40, 46, 58 }, { 42, 48, 61 }, { 44, 50, 64 },
        { 40, 47, 61 }, { 34, 41, 55 }, { 29, 35, 48 }, { 24, 30, 42 },
        { 16, 20, 30 }, { 9, 11, 17 }, { 4, 5, 8 } };
    unsigned char const (* const colour)[3] = sr_cleanDark ? night : day;

    glDisable(GL_TEXTURE_2D);
    for ( int ring = 0; ring < rings - 1; ++ring )
    {
        BeginQuadStrip();
        for ( int i = 0; i <= segments; ++i )
        {
            REAL const angle = i * ( 2 * M_PI / segments );
            REAL const c = cos( angle ), s = sin( angle );
            for ( int edge = 0; edge < 2; ++edge )
            {
                unsigned char const * rgb = colour[ring + edge];
                REAL const up = elevation[ring + edge] * ( M_PI / 180 );
                REAL const level = cos( up ) * se_cleanFar;
                glColor4f( rgb[0] / 255.f, rgb[1] / 255.f, rgb[2] / 255.f, 1 );
                glVertex3f( eye.x + c * level, eye.y + s * level, eyeHeight + sin( up ) * se_cleanFar );
            }
        }
        RenderEnd();
    }

    if ( !sr_alphaBlend )
        return;

    // the sun: a wide warm halo in the haze and a pale disc
    REAL dx, dy, dz;
    se_CleanArenaSunDirection( dx, dy, dz );

    static const REAL haloRadius[] = { 0, 1.6f, 2.4f, 4, 7, 10, 14, 18, 24, 32 };
    static const REAL haloShare[]  = { .62f, .62f, .58f, .52f, .45f, .40f, .28f, .12f, .03f, 0 };
    static const REAL discRadius[] = { 0, 1.4f, 2.0f };
    static const REAL discShare[]  = { .95f, .95f, 0 };

    if ( sr_cleanDark )
    {
        // the moon, where the sun would stand: a cold, thin halo and a
        // disc that does not dazzle
        static const REAL moonHalo[] = { .20f, .20f, .18f, .15f, .12f, .09f, .06f, .03f, .01f, 0 };
        static const REAL moonDisc[] = { .80f, .80f, 0 };
        se_CleanArenaGlow( eye, eyeHeight, dx, dy, dz, haloRadius, moonHalo, 10,
                           .52f, .62f, .82f );
        se_CleanArenaGlow( eye, eyeHeight, dx, dy, dz, discRadius, moonDisc, 3,
                           .80f, .86f, .96f );
        return;
    }

    se_CleanArenaGlow( eye, eyeHeight, dx, dy, dz, haloRadius, haloShare, 10,
                       1, .855f, .729f );
    se_CleanArenaGlow( eye, eyeHeight, dx, dy, dz, discRadius, discShare, 3,
                       1, .969f, .910f );
}

// The floor: rings around the viewer. What a glossy floor shows depends on
// the angle it is seen at, so the rings are spaced in camera heights: close
// by it mirrors what stands on it, towards the horizon it takes on the haze.
void se_CleanArenaFloor( eCoord const & centre, REAL eyeHeight, bool mirror )
{
    // The last rings lie within two degrees of the horizon. They take the
    // floor up to the colour the sky starts with, so no line shows there.
    static const int segments = 48;
    static const int rings = 14;
    static const REAL radius[rings] = { 0, 1, 1.5f, 2, 3, 6, 7.6f, 10.6f, 17, 29, 57, 115, 290, 3000 };
    static const unsigned char day[rings][3] = {
        { 177, 192, 205 }, { 177, 192, 205 }, { 177, 193, 205 }, { 178, 195, 205 },
        { 178, 198, 204 }, { 179, 200, 204 }, { 184, 201, 206 }, { 187, 202, 207 },
        { 192, 203, 208 }, { 197, 205, 209 }, { 203, 207, 211 }, { 207, 209, 212 },
        { 210, 210, 213 }, { 212, 211, 213 } };
    // night: slate at the feet, lifting to the horizon's glow
    static const unsigned char night[rings][3] = {
        { 16, 19, 25 }, { 16, 19, 25 }, { 16, 19, 25 }, { 17, 20, 26 },
        { 17, 21, 27 }, { 18, 22, 28 }, { 20, 24, 31 }, { 22, 26, 34 },
        { 25, 30, 38 }, { 29, 34, 43 }, { 33, 38, 49 }, { 36, 42, 53 },
        { 39, 45, 57 }, { 40, 46, 58 } };
    unsigned char const (* const colour)[3] = sr_cleanDark ? night : day;
    static const REAL mirrored[rings] = { .30f, .30f, .30f, .30f, .30f, .24f, .20f, .15f, .08f, 0, 0, 0, 0, 0 };

    REAL const unit = eyeHeight > 4 ? eyeHeight : 4;
    REAL const * const floor = sr_CleanFloorColor();

    glDisable(GL_TEXTURE_2D);
    for ( int ring = 0; ring < rings - 1; ++ring )
    {
        BeginQuadStrip();
        for ( int i = 0; i <= segments; ++i )
        {
            REAL const angle = i * ( 2 * M_PI / segments );
            REAL const c = cos( angle ), s = sin( angle );
            for ( int edge = 0; edge < 2; ++edge )
            {
                int const at = ring + edge;
                REAL const alpha = mirror ? 1 - mirrored[at] : 1;

                // What lies under the floor shows through by the share it
                // mirrors; where nothing stands, that is the plain floor
                // colour the frame was cleared to. Allow for it, so the
                // floor ends up the colour it is meant to have.
                REAL rgb[3];
                for ( int k = 0; k < 3; ++k )
                {
                    rgb[k] = floor[k] + ( colour[at][k] / 255.f - floor[k] ) / alpha;
                    if ( rgb[k] < 0 ) rgb[k] = 0;
                    if ( rgb[k] > 1 ) rgb[k] = 1;
                }
                glColor4f( rgb[0], rgb[1], rgb[2], alpha );
                glVertex2f( centre.x + c * radius[at] * unit, centre.y + s * radius[at] * unit );
            }
        }
        RenderEnd();
    }
}

// The sun's glint: where the floor mirrors the sun, it shows as a pale warm
// light on the floor that moves with the viewer.
void se_CleanArenaGlint( eCoord const & eye, REAL eyeHeight )
{
    REAL dx, dy, dz;
    se_CleanArenaSunDirection( dx, dy, dz );

    static const REAL radius[] = { 0, 1.7f, 2.1f, 4, 6, 8, 10, 12, 14, 20 };
    static const REAL share[]  = { .50f, .50f, .39f, .35f, .27f, .19f, .135f, .07f, .04f, 0 };
    if ( sr_cleanDark )
    {
        // the moon's: cold, and a fraction of the sun's
        static const REAL faint[] = { .16f, .16f, .13f, .11f, .085f, .06f, .04f, .02f, .01f, 0 };
        se_CleanArenaGlow( eye, eyeHeight, dx, dy, -dz, radius, faint, 10,
                           .52f, .62f, .82f );
        return;
    }
    se_CleanArenaGlow( eye, eyeHeight, dx, dy, -dz, radius, share, 10,
                       1, .914f, .776f );
}
}

void eGrid::display_simple( eCamera* cam, int viewer,bool floor,
                            bool sr_upperSky,bool sr_lowerSky,
                            REAL flooralpha,
                            bool eWalls,bool gameObjects,
                            REAL& zNear){
    /*
    static GLfloat S[]={1,0,0,0};
    static GLfloat T[]={0,1,0,0};
    static GLfloat R[]={0,0,1,0};
    static GLfloat Q[]={0,0,0,1};

    glTexGeni(GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
    glTexGenfv(GL_S,GL_OBJECT_PLANE,S);

    glTexGeni(GL_T,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
    glTexGenfv(GL_T,GL_OBJECT_PLANE,T);

    glTexGeni(GL_R,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
    glTexGenfv(GL_R,GL_OBJECT_PLANE,R);

    glTexGeni(GL_Q,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);
    glTexGenfv(GL_Q,GL_OBJECT_PLANE,Q);

    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_R);
    glDisable(GL_TEXTURE_GEN_Q);
    */


    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glDisable(GL_CULL_FACE);

    eCoord camPos = cam->CameraGlancePos();
    // eWallRim::Bound( camPos, 10 );

    // the mirrored pass leaves the cleared floor colour as its backdrop
    if ( sr_cleanArena && cam->RenderingMain() )
        se_CleanArenaSky( camPos, cam->CameraZ() );

    if (!sr_cleanArena && (sr_upperSky || se_BlackSky())){
        if (se_BlackSky()){
            //glDisable(GL_TEXTURE);
            glDisable(GL_TEXTURE_2D);

            glColor3f(0,0,0);

            if ( z < lower_height )
                infinity_xy_plane(cam->CameraPos(), cam->CameraDir(), lower_height);

            glEnable(GL_TEXTURE_2D);
        }
        else {
            TexMatrix();
            glLoadIdentity();
            glScalef(upper_height,upper_height,upper_height);

            se_glFloorTexture();

            se_SelectUpperSky();
            glColor3f(se_upperSkyColorR,se_upperSkyColorG,se_upperSkyColorB);

            if ( z < upper_height )
                infinity_xy_plane(cam->CameraPos(), cam->CameraDir(), upper_height);
        }
    }

    if (!sr_cleanArena && sr_lowerSky && !sr_highRim){
        paint_sr_lowerSky(this, viewer,sr_upperSky, camPos);
    }

    if (floor){
        sr_DepthOffset(false);

        su_FetchAndStoreSDLInput();
        int floorDetail = sr_floorDetail;

        // no multitexturing without alpha blending
        if ( !sr_alphaBlend && floorDetail > rFLOOR_TEXTURE )
            floorDetail = rFLOOR_TEXTURE;

        if ( sr_cleanArena && floorDetail != rFLOOR_OFF )
        {
            // one pale, glossy floor to the horizon; no grid, no texture
            se_CleanArenaFloor( camPos, cam->CameraZ(), sr_alphaBlend && flooralpha < 1 );
            if ( sr_alphaBlend )
                se_CleanArenaGlint( camPos, cam->CameraZ() );
            floorDetail = rFLOOR_OFF;
        }

        switch(floorDetail){
        case rFLOOR_OFF:
            break;
        case rFLOOR_GRID:
            {
	#define SIDELEN   (se_GridSize())
	#define EXTENSION 10

                eCoord center = CameraPos(viewer) + CameraDir(viewer) * (SIDELEN * EXTENSION * .8);

                REAL x=center.x;
                REAL y=center.y;
                int xn=static_cast<int>(x/SIDELEN);
                int yn=static_cast<int>(y/SIDELEN);


                //glDisable(GL_TEXTURE);
                glDisable(GL_TEXTURE_2D);

	#define INTENSITY(x,xx) (1-(((x)-(xx))*((x)-(xx))/(EXTENSION*SIDELEN*EXTENSION*SIDELEN)))


                BeginLines();
                for(int i=xn-EXTENSION;i<=xn+EXTENSION;i++){
                    REAL intens=INTENSITY(i*SIDELEN,x);
                    if (intens<0) intens=0;
                    se_glFloorColor(intens,intens);
                    glVertex2f(i*SIDELEN,y-SIDELEN*(EXTENSION+1));
                    glVertex2f(i*SIDELEN,y+SIDELEN*(EXTENSION+1));
                }
                for(int j=yn-EXTENSION;j<=yn+EXTENSION;j++){
                    REAL intens=INTENSITY(j*SIDELEN,y);
                    if (intens<0) intens=0;
                    se_glFloorColor(intens,intens);
                    glVertex2f(x-(EXTENSION+1)*SIDELEN,j*SIDELEN);
                    glVertex2f(x+(EXTENSION+1)*SIDELEN,j*SIDELEN);
                }
                RenderEnd();
            }
            break;

        case rFLOOR_TEXTURE:
            TexMatrix();
            glLoadIdentity();
            glScalef(1/se_GridSize(),1/se_GridSize(),1/se_GridSize());

            se_glFloorTexture();
            se_glFloorColor(flooralpha);

            infinity_xy_plane( camPos, CameraDir(viewer) );

            /* old way: draw every triangle
            for(int i=eFace::faces.Len()-1;i>=0;i--){
            eFace *f=eFace::faces(i);

            if (f->visHeight[viewer]<z){
            glBegin(GL_TRIANGLES);
            for(int j=0;j<=2;j++){
            glVertex3f(f->p[j]->x,f->p[j]->y,0);
            }
            glEnd();
            }
            }
            */

            break;

        case rFLOOR_TWOTEXTURE:
            se_glFloorColor(flooralpha);

            TexMatrix();
            glLoadIdentity();
            REAL gs = 1/se_GridSize();
            glScalef(0.01*gs,gs,gs);

            se_glFloorTexture_a();
            infinity_xy_plane( camPos, CameraDir(viewer) );

            se_glFloorColor(flooralpha);

            TexMatrix();
            glLoadIdentity();
            glScalef(gs,.01*gs,gs);

            se_glFloorTexture_b();

            glDepthFunc(GL_LEQUAL);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE);
            infinity_xy_plane( camPos, CameraDir(viewer) );
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

            break;
        }
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    TexMatrix();
    glLoadIdentity();
    ModelMatrix();

    //  glDisable(GL_TEXTURE_GEN_S);
    //  glDisable(GL_TEXTURE_GEN_T);
    //  glDisable(GL_TEXTURE_GEN_Q);
    //  glDisable(GL_TEXTURE_GEN_R);

    if(eWalls){
        {
            su_FetchAndStoreSDLInput();

            eWallRim::RenderAll( cameras(viewer) );
        }

        if (!sr_cleanArena && sr_lowerSky && sr_highRim){
            //      glEnable(GL_TEXTURE_GEN_S);
            //      glEnable(GL_TEXTURE_GEN_T);
            //      glEnable(GL_TEXTURE_GEN_Q);
            //      glEnable(GL_TEXTURE_GEN_R);

            paint_sr_lowerSky(this, viewer,sr_upperSky, camPos);

            //      glDisable(GL_TEXTURE_GEN_S);
            //      glDisable(GL_TEXTURE_GEN_T);
            //      glDisable(GL_TEXTURE_GEN_Q);
            //      glDisable(GL_TEXTURE_GEN_R);

            TexMatrix();
            glLoadIdentity();
            ModelMatrix();
        }
    }

    if (eWalls){
        // send out sensors to find walls close to the camera
        if ( z < 3 )
        {
            eCamera const * camera = cameras(viewer);
            if( camera && camera->Center() )
            {
                eCoord dir = camera->CameraDir().Turn(1,.5);
                for(int i = 8; i > 0; --i)
                {
                    dir = dir.Turn(sqrt(.5),sqrt(.5));
                    eZNearSensor s( camera->Center(), camPos, dir, zNear, camera );
                    s.Detect();
                }
            }
        }

        // glDisable(GL_CULL_FACE);
        // draw_eWall(this,viewer,0,zNear,cameras(viewer));

        /*
        #ifdef DEBUG
        for(int i=sg_netPlayerWalls.Len()-1;i>=0;i--){
          glMatrixMode(GL_MODELVIEW);
          glPushMatrix();
          if (sg_netPlayerWalls(i)->Preliminary())
        glTranslatef(0,0,4);
          else
        glTranslatef(0,0,8);
          if (sg_netPlayerWalls(i)->Wall())
        sg_netPlayerWalls(i)->Wall()->RenderList(false);
          glPopMatrix();
          }
        #endif
        */

        /*
        static int oldlen=0;
        int newlen=sg_netPlayerWalls.Len();
        if (newlen!=oldlen){
          con << "Number of player eWalls now " << newlen << '\n';
          oldlen=newlen;
        }
        */

    }

    if (gameObjects)
        eGameObject::RenderAll(this, cameras(viewer));

    eDebugLine::Render();
#ifdef DEBUG

    ePath::RenderLast();

    if (debug_grid){
        //glDisable(GL_TEXTURE);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        BeginLines();

        int i;
        for(i=edges.Len()-1;i>=0;i--){
            eHalfEdge *e=edges[i];
            if (e->Face())
                glColor4f(1,1,1,1);
            else
                glColor4f(0,0,1,1);

            glVertex3f(e->Point()->x,e->Point()->y,10);
            glVertex3f(e->Point()->x,e->Point()->y,15);
            glVertex3f(e->Point()->x,e->Point()->y,.1);
            glVertex3f(e->other->Point()->x,e->other->Point()->y,.1);
            glVertex3f(e->other->Point()->x,e->other->Point()->y,10);
            glVertex3f(e->other->Point()->x,e->other->Point()->y,15);

        }

        for(i=points.Len()-1;i>=0;i--){
            ePoint *p=points[i];
            glColor4f(1,0,0,1);
            glVertex3f(p->x,p->y,0);
            glVertex3f(p->x,p->y,(p->GetRefcount()+1)*5);
        }
        /*
        for(int i=sg_netPlayerWalls.Len()-1;i>=0;i--){
          eEdge *e=sg_netPlayerWalls[i]->Edge();
        glColor4f(0,1,0,1);

          glVertex3f(e->Point()->x,e->Point()->y,5);
          glVertex3f(e->Point()->x,e->Point()->y,10);
          glVertex3f(e->Point()->x,e->Point()->y,10);
          glVertex3f(e->other->Point()->x,e->other->Point()->y,10);
          glVertex3f(e->other->Point()->x,e->other->Point()->y,10);
          glVertex3f(e->other->Point()->x,e->other->Point()->y,5);
        }
        */
        RenderEnd();
    }
#endif

}
#endif

void eGrid::Render( eCamera* cam, int viewer, REAL& zNear ){
    if (!sr_glOut)
        return;
#ifndef DEDICATED
    ProjMatrix();

    z=CameraHeight(viewer);
    if ( zNear > z )
    {
        zNear = z;
    }

    // The clean arena's glossy floor is part of that look, whatever the floor
    // mirror setting says for the classic arena. Trails put their own sheen
    // on it; the mirrored pass is there for everything else that stands on it.
    int const mirror = sr_cleanArena ? ( sr_alphaBlend ? rMIRROR_WALLS : rMIRROR_OFF ) : sr_floorMirror;

    if (mirror){
        if ( sr_cleanArena )
            se_CleanArenaBackdrop();

        ModelMatrix();
        glScalef(1,1,-1);

        if (z>10) z=10;
        glFrontFace(GL_CW);

        bool us=false;
        bool ls=false;

        if (mirror>=rMIRROR_ALL){
            us=sr_upperSky;
            ls=sr_lowerSky;
        }
        else if (mirror>=rMIRROR_WALLS){
            if (sr_lowerSky)
                ls=true;
            else if (sr_upperSky)
                us=true;
        }

        cam->SetRenderingMain(false);
        display_simple(cam, viewer,false,
                       us,ls,
                       0,
                       mirror>=rMIRROR_WALLS,
                       mirror>=rMIRROR_OBJECTS,
                       zNear);
        z=CameraHeight(viewer);
        glFrontFace(GL_CCW);
        ModelMatrix();
        glScalef(1,1,-1);


        cam->SetRenderingMain(true);
        display_simple(cam, viewer,true,
                       sr_upperSky,sr_lowerSky,
                       // the clean arena's floor knows how much it mirrors at which distance
                       sr_cleanArena ? REAL(.7f) : 1-sr_floorMirror_strength,
                       true,true,zNear);

    }
    else
    {
        cam->SetRenderingMain(true);
        display_simple(cam, viewer,true,
                       sr_upperSky,sr_lowerSky,
                       1,
                       true,true,zNear);
    }


#ifdef EVENT_DEB
    //  for(int i=eEdge_crossing.Len()-1;i>=0;i--){
    //    eEdge_crossing(i)->Render();
    //  }
#endif
#endif
}


//void eEdgeViewer::Render(){}

/*
void eViewerCrossesEdge::Render(){
#ifndef DEDICATED
  ePoint *p1=e->Point();
  ePoint *p2=e->other->Point();

  REAL timeLeft=value-se_GameTime();

  REAL h;

  if (viewer==1){
    if (timeLeft>0){
      h=timeLeft+4;
      glColor4f(0,0,1,.5);
    }
    else{
      h=-timeLeft+4;
      glColor4f(1,0,0,.5);
    }

    //  else
    //glColor4f(1,0,0,.5);

    static rTexture ArmageTron_invis_eWall(rTEX_WALL,"textures/eWall2.png",1,0);

    ArmageTron_invis_eWall.Select();

    eWall::Render_helper(e,(p1->x+p1->y)/4,(p2->x+p2->y)/4,h,1,4);
  }
#endif
}
*/

#endif


