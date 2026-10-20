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

#include "gColourStudio.h"

// DEDICATED comes from config.h
#include "defs.h"

#ifdef DEDICATED

void sg_ColourStudio(){}

#else

#include "rSDL.h"

#include "eCoord.h"
#include "eFloor.h"
#include "ePlayer.h"
#include "gCycle.h"
#include "gWall.h"
#include "nNetwork.h"
#include "rGL.h"
#include "rPost.h"
#include "rRender.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tDirectories.h"
#include "tSysTime.h"
#include "uMenu.h"
#include "uRclTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

// A player has one colour, three numbers, and every client draws two things
// from it: the trail, a channel at 17 times the number and full from 15 up,
// and the bike, whose classic texture is tinted through a byte, so that from
// 15 up a channel wraps round to (17v) mod 256 (sg_BikeColor in gCycle.cpp).
// Where the trail is full, then, the bike can be any colour at all. This page
// lets the player set the two as what they are, a bike and a trail, and
// writes the one setting that makes them.

namespace
{
    using namespace uRclTheme;

    // ---- the colour: what is edited, and the setting it makes ----

    // a trail channel at 15 is full: 255 on screen, and the bike's channel is free there
    int const sg_full = 15;

    struct Pick
    {
        int trail[3];   // 0 to 15 a channel, 15 full
        int bike[3];    // 0 to 255 a channel; it counts only where the trail is full
    };

    // a channel of the setting from a channel of each: below full, the
    // trail's; at full, the smallest v from 15 up whose bike is the one asked for
    int Setting( int trail, int bike )
    {
        if ( trail < sg_full )
            return trail;
        // 17 times 241 is 4097, one more than 16 times 256: times 241 undoes times 17
        int v = ( 241 * bike ) & 255;
        if ( v < sg_full )
            v += 256;
        return v;
    }

    void Settings( Pick const & pick, int v[3] )
    {
        for ( int c = 0; c < 3; ++c )
            v[c] = Setting( pick.trail[c], pick.bike[c] );
    }

    // and back
    Pick FromSettings( int const v[3] )
    {
        Pick pick;
        for ( int c = 0; c < 3; ++c )
        {
            int const value = std::max( 0, v[c] );
            pick.trail[c] = std::min( value, sg_full );
            pick.bike[c] = value < sg_full ? 17 * value : ( 17 * value ) & 255;
        }
        return pick;
    }

    // the bike's channel as edited: where the trail is not full, the trail's
    int BikeByte( Pick const & pick, int c )
    {
        return pick.trail[c] < sg_full ? 17 * pick.trail[c] : pick.bike[c];
    }

    bool SameSettings( Pick const & a, Pick const & b )
    {
        int va[3], vb[3];
        Settings( a, va );
        Settings( b, vb );
        return va[0] == vb[0] && va[1] == vb[1] && va[2] == vb[2];
    }

    // What every client makes of a setting (gCycle's constructor): the
    // cycle's colour, which the bike is drawn from, and its trail's. A colour
    // too dark to be seen is lifted; a channel over full stays over it.
    void Shown( int const v[3], REAL bike[3], REAL trail[3] )
    {
        for ( int c = 0; c < 3; ++c )
            bike[c] = trail[c] = REAL( v[c] / 15.0 );
        se_MakeColorValid( bike[0], bike[1], bike[2], 1.0f );
        se_MakeColorValid( trail[0], trail[1], trail[2], .5f );
    }

    REAL Unit( REAL c )
    {
        return std::max( REAL( 0 ), std::min( REAL( 1 ), c ) );
    }

    // the same as bytes: the classic bike's tint, which wraps over full, and
    // the trail, which GL takes to full
    void ShownBytes( int const v[3], int bike[3], int trail[3] )
    {
        REAL b[3], t[3];
        Shown( v, b, t );
        for ( int c = 0; c < 3; ++c )
        {
            bike[c] = v[c] >= sg_full ? ( 17 * v[c] ) & 255 : static_cast< int >( Unit( b[c] ) * 255 );
            trail[c] = v[c] >= sg_full ? 255 : static_cast< int >( Unit( t[c] ) * 255 + .5f );
        }
    }

    uRclTheme::Color Swatch( int const bytes[3] )
    {
        uRclTheme::Color const color = { bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f };
        return color;
    }

    // Pairs that work: a bike that stands out from a trail that is easy to
    // race against. The same twelve as the colour studio on the league's site
    // (its lib/arma/cycle-colors.ts), so the two agree; every one is a setting
    // of 40 or less. The bike in bytes, the trail in steps of 0 to 15 (15 full).
    struct Pair
    {
        int bike[3], trail[3];
    };

    Pair const sg_pairs[] =
    {
        { { 168, 255, 136 }, { 15, 15,  8 } },     // lime bike, pale yellow trail: 40 15 8
        { {  16,  16,  16 }, { 15, 15, 15 } },     // black bike, white trail: 16 16 16
        { { 255, 152, 255 }, { 15, 15, 15 } },     // pink bike, white trail: 15 24 15
        { { 135, 255,   0 }, { 15, 15,  0 } },     // chartreuse bike, yellow trail: 23 15 0
        { { 255,  16, 169 }, { 15, 15, 15 } },     // hot pink bike, white trail: 15 16 25
        { {  16,  16, 255 }, { 15, 15, 15 } },     // blue bike, white trail: 16 16 15
        { {   0, 135, 255 }, {  0, 15, 15 } },     // azure bike, cyan trail: 0 23 15
        { {  16, 255,  16 }, { 15, 15, 15 } },     // green bike, white trail: 16 15 16
        { { 255,  16,  16 }, { 15, 15, 15 } },     // red bike, white trail: 15 16 16
        { { 255, 135,   0 }, { 15, 15,  0 } },     // orange bike, yellow trail: 15 23 0
        { {  84,   0,   0 }, { 15,  0,  0 } },     // maroon bike, red trail: 20 0 0
        { { 135,   0, 255 }, { 15,  0, 15 } }      // violet bike, magenta trail: 23 0 15
    };
    int const sg_pairCount = sizeof( sg_pairs ) / sizeof( sg_pairs[0] );
    int const sg_picks = sg_pairCount + 1;          // the player's own first

    uRclTheme::Color const sg_cardColor = { .165f, .169f, .173f };
    uRclTheme::Color const sg_quiet = { .478f, .486f, .494f };

    REAL WidthPx( char const * text, REAL sizePx, int weight )
    {
        return TextWidth( text, sizePx, weight ) / W( 1 );
    }

    // ---- where things are, in logical pixels from the window's top left ----

    struct Layout
    {
        REAL left, right;                           // the column of cards
        REAL bikeTop, trailTop, cardHeight;
        REAL settingTop, settingHeight;
        REAL pairsLabel, pairsTop, pairsHeight, pairsNote, pairWidth, pairGap;
        REAL label, trackLeft, trackRight, value;   // a channel's row in a card
        REAL previewLeft, previewTop, previewRight, previewBottom;
    };

    Layout Measure()
    {
        Layout l;
        REAL const margin = MarginPx();
        REAL const width = LogicalWidth(), height = LogicalHeight();
        l.left = margin;
        l.right = margin + std::min( REAL( 808 ), ( width - 3 * margin ) * .46f );
        l.cardHeight = 160;
        l.bikeTop = 154;
        l.trailTop = l.bikeTop + l.cardHeight + 18;
        l.settingTop = l.trailTop + l.cardHeight + 18;
        l.settingHeight = 146;
        l.pairsLabel = l.settingTop + l.settingHeight + 28;
        l.pairsTop = l.pairsLabel + 18;
        l.pairsHeight = 46;
        l.pairsNote = l.pairsTop + l.pairsHeight + 22;
        l.pairGap = 8;
        l.pairWidth = ( l.right - l.left - ( sg_picks - 1 ) * l.pairGap ) / sg_picks;
        l.value = l.right - 16;
        l.trackRight = l.right - 52;
        l.trackLeft = std::max( l.left + 340, l.trackRight - 288 );
        l.label = l.trackLeft - 24;
        l.previewLeft = l.right + std::min( REAL( 86 ), margin * 1.5f );
        l.previewRight = width - margin;
        l.previewTop = 144;
        l.previewBottom = std::max( l.previewTop + 200, std::min( l.pairsTop + l.pairsHeight + 32, height - 90 ) );
        return l;
    }

    // the rows from the top: the bike's r g b, the trail's r g b, the pairs
    int const sg_rows = 7;
    int const sg_pairsRow = 6;

    REAL RowCentre( Layout const & l, int row )
    {
        if ( row >= sg_pairsRow )
            return l.pairsTop + l.pairsHeight * .5f;
        return ( row < 3 ? l.bikeTop : l.trailTop ) + 40 + 36 * ( row % 3 );
    }

    // ---- the studio camera, for pictures to set beside Blender's ----

    // The preview's camera as it was last drawn, so that the same view can
    // be set up in Blender and an in-game frame judged beside a render of
    // the model (RCL_STUDIO_CAMERA writes it out). RCL_STUDIO_HOLD 1 stops
    // the cycle turning, at RCL_STUDIO_SWING degrees from facing the camera.
    struct StudioCamera
    {
        bool drawn;
        int width, height, look;
        float eye[3], target[3], fovY, nearPlane, farPlane;
        float pos[2], dir[2], swing;
    };

    StudioCamera sg_studioCamera = { false, 0, 0, 0, { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, 0, { 0, 0 }, { 1, 0 }, 0 };

    int sg_studioHold = 0;
    tSettingItem< int > sg_studioHoldConf( "RCL_STUDIO_HOLD", sg_studioHold );
    REAL sg_studioSwing = 0;
    tSettingItem< REAL > sg_studioSwingConf( "RCL_STUDIO_SWING", sg_studioSwing );

    // A point of the arena in the frame the cycle is drawn in: the frame of
    // gLightCycle and of the Blender light cycle (x ahead, y left, z up, the
    // cycle twice its size in the arena; see sg_CleanCycleFrame in gCycle.cpp).
    void ToCycleFrame( StudioCamera const & c, float const * at, float * out, bool direction )
    {
        float x = at[0], y = at[1];
        if ( !direction )
        {
            x -= c.pos[0];
            y -= c.pos[1];
        }
        float const ahead = x * c.dir[0] + y * c.dir[1], left = -x * c.dir[1] + y * c.dir[0];
        float const scale = direction ? 1.f : 2.f;
        out[0] = ahead * scale + ( direction ? 0.f : 1.5f );
        out[1] = left * scale;
        out[2] = at[2] * scale;
    }

    void WriteVector( std::ostream & o, float const * v, int n )
    {
        o << "[";
        for ( int i = 0; i < n; ++i )
            o << ( i ? ", " : "" ) << v[i];
        o << "]";
    }

    bool WriteStudioCamera( char const * name )
    {
        StudioCamera const & c = sg_studioCamera;
        if ( !c.drawn )
        {
            con << "RCL_STUDIO_CAMERA: the colour studio has not drawn its preview yet; open it first\n";
            return false;
        }
        std::ofstream o;
        if ( !tDirectories::Var().Open( o, name ) )
        {
            con << "RCL_STUDIO_CAMERA: cannot write " << name << "\n";
            return false;
        }

        float eye[3], target[3], up[3];
        float const sky[3] = { 0, 0, 1 };
        ToCycleFrame( c, c.eye, eye, false );
        ToCycleFrame( c, c.target, target, false );
        ToCycleFrame( c, sky, up, true );
        static char const * const looks[3] = { "light", "dark", "classic" };

        o.precision( 7 );
        o << "{\n";
        o << "  \"what\": \"Retrocycles RCL colour studio preview camera\",\n";
        o << "  \"frame\": \"cycle model frame: +x ahead, +y left, +z up, the frame the light cycle is built in (gLightCycle, rcl-cfg/art/lightcycle); the cycle stands at the origin unrotated\",\n";
        o << "  \"image\": { \"width\": " << c.width << ", \"height\": " << c.height << " },\n";
        o << "  \"fov_y_degrees\": " << c.fovY << ",\n";
        o << "  \"clip\": { \"near\": " << c.nearPlane * 2 << ", \"far\": " << c.farPlane * 2 << " },\n";
        o << "  \"camera\": { \"eye\": ";
        WriteVector( o, eye, 3 );
        o << ", \"target\": ";
        WriteVector( o, target, 3 );
        o << ", \"up\": ";
        WriteVector( o, up, 3 );
        o << " },\n";
        o << "  \"arena\": { \"eye\": ";
        WriteVector( o, c.eye, 3 );
        o << ", \"target\": ";
        WriteVector( o, c.target, 3 );
        o << ", \"cycle_position\": ";
        WriteVector( o, c.pos, 2 );
        o << ", \"cycle_direction\": ";
        WriteVector( o, c.dir, 2 );
        o << ", \"model_scale\": 0.5, \"model_offset_x\": -1.5 },\n";
        o << "  \"swing_degrees\": " << c.swing << ",\n";
        o << "  \"held\": " << ( sg_studioHold ? "true" : "false" ) << ",\n";
        o << "  \"look\": \"" << looks[ c.look >= 0 && c.look <= 2 ? c.look : 0 ] << "\"\n";
        o << "}\n";

        con << "RCL_STUDIO_CAMERA: wrote " << tDirectories::Var().GetWritePath( name ) << "\n";
        return true;
    }

    void sg_StudioCameraCommand( std::istream & s )
    {
        tString name;
        s >> name;
        if ( name.Len() <= 1 )
            name = "studio-camera.json";
        WriteStudioCamera( name );
    }
    tConfItemFunc sg_studioCameraCommand( "RCL_STUDIO_CAMERA", &sg_StudioCameraCommand );

    // ---- the preview ----

    // the camera's matrix, from the eye towards a target, the sky up
    void LookAt( float ex, float ey, float ez, float tx, float ty, float tz )
    {
        float fx = tx - ex, fy = ty - ey, fz = tz - ez;
        float const length = sqrtf( fx * fx + fy * fy + fz * fz );
        fx /= length;
        fy /= length;
        fz /= length;
        float sx = fy, sy = -fx;
        float const across = sqrtf( sx * sx + sy * sy );
        sx /= across;
        sy /= across;
        // up: across times ahead
        float const ux = sy * fz, uy = -sx * fz, uz = sx * fy - sy * fx;
        GLfloat const matrix[16] = { sx, ux, -fx, 0,
                                     sy, uy, -fy, 0,
                                     0,  uz, -fz, 0,
                                     0,  0,  0,   1 };
        ModelMatrix();
        IdentityMatrix();
        glMultMatrixf( matrix );
        glTranslatef( -ex, -ey, -ez );
    }

    // The end of the cycle's trail: it leaves the cycle low, a little behind
    // it, comes up to its full height over the next few lengths as the
    // game's does (gWall.cpp), and goes on behind it, away out of the picture.
    void DrawTrail( eCoord const & pos, eCoord const & dir, REAL const colour[3], int pass )
    {
        // out of the cycle as the game's walls come: low out of the arrow's
        // notch, square out of the mesh cycle's rear axle (sg_CleanWallTip)
        gCleanWallTip const tip = sg_CleanWallTip();
        REAL const leaves = tip.leaves;
        REAL const full = sg_CleanWallHeight();     // as tall as the game draws its walls
        static const REAL length = 12;
        static const int steps = 6;

        // the mesh cycle's as the game draws it: filling in under its top
        // line, the emission point where that leaves the tail
        if ( !tip.arrow )
        {
            BeginQuads();
            sg_CleanWallTipPiece( pos - dir * length, pos - dir * leaves, length, leaves, tip, full,
                                  colour[0], colour[1], colour[2], pass, true );
            RenderEnd();
            return;
        }

        REAL back[ steps + 2 ], height[ steps + 2 ];
        for ( int i = 0; i <= steps; ++i )
        {
            REAL const f = REAL( i ) / steps;
            back[i] = leaves + tip.rise * f;
            height[i] = full * ( tip.low + ( 1 - tip.low ) * f * f * ( 3 - 2 * f ) );
        }
        back[ steps + 1 ] = length;
        height[ steps + 1 ] = full;

        BeginQuads();
        for ( int i = 0; i + 1 < steps + 2; ++i )
        {
            // as thin as the game's wall where it is inside the tyre
            REAL const f = i < steps ? ( i + .5f ) / steps : 1;
            sg_CleanWallWidthShare( tip.narrow + ( 1 - tip.narrow ) * f * f * ( 3 - 2 * f ) );
            sg_CleanWallPiece( pos - dir * back[i + 1], pos - dir * back[i], height[i + 1], height[i],
                               colour[0], colour[1], colour[2], 1, pass );
        }
        sg_CleanWallWidthShare( 1 );
        RenderEnd();
    }

    // The cycle on the floor with the end of its trail behind it, slowly
    // turning, drawn with the clean arena's own functions the way the menu's
    // replay draws its scene, into the rectangle the layout has for it. look
    // 0 is the light arena, 1 the dark one, 2 the classic cycle as every other
    // client draws it (on the dark floor). bike and trail are the cycle's two
    // colours as the game holds them.
    void DrawScene( Layout const & l, int look, REAL const bike[3], REAL const trail[3], double time )
    {
        if ( !sr_glOut )
            return;

        GLint viewport[4];
        glGetIntegerv( GL_VIEWPORT, viewport );
        int const x0 = viewport[0] + static_cast< int >( ( X( l.previewLeft ) + 1 ) * .5f * viewport[2] + .5f );
        int const x1 = viewport[0] + static_cast< int >( ( X( l.previewRight ) + 1 ) * .5f * viewport[2] + .5f );
        int const y0 = viewport[1] + static_cast< int >( ( Y( l.previewBottom ) + 1 ) * .5f * viewport[3] + .5f );
        int const y1 = viewport[1] + static_cast< int >( ( Y( l.previewTop ) + 1 ) * .5f * viewport[3] + .5f );
        int const width = x1 - x0, height = y1 - y0;
        if ( width < 16 || height < 16 )
            return;

        RenderEnd();
        ProjMatrix();
        PushMatrix();
        ModelMatrix();
        PushMatrix();
        glPushAttrib( GL_ALL_ATTRIB_BITS );

        glViewport( x0, y0, width, height );
        glScissor( x0, y0, width, height );
        glEnable( GL_SCISSOR_TEST );
        glDepthMask( GL_TRUE );
        glClear( GL_DEPTH_BUFFER_BIT );

        // a scene of its own, as the world is (rPost)
        rPost::BeginScene();

        bool const wasDark = sr_cleanDark;
        bool const classic = look == 2;
        sr_cleanDark = look != 0;

        // a camera standing still, a little above the floor, with the sun
        // behind it to the right, so the cycle's near side is lit and its
        // shadow falls across the floor to the left
        static const float pi = 3.14159265f;
        static const float bearing = 68 * pi / 180, distance = 4.6f, eyeHeight = 1.2f, fov = 26 * pi / 180;
        float const ex = cosf( bearing ) * distance, ey = sinf( bearing ) * distance;
        float const nearPlane = .1f, farPlane = 40000;
        float const top = nearPlane * tanf( fov * .5f ), right = top * width / height;
        ProjMatrix();
        IdentityMatrix();
        glFrustum( -right, right, -top, top, nearPlane, farPlane );
        LookAt( ex, ey, eyeHeight, 0, 0, .36f );

        // The cycle turns about its middle, from showing the one side through
        // facing the camera to showing the other; its trail stays behind it.
        // Held still, it shows the side asked for.
        float const swing = sg_studioHold ? float( sg_studioSwing ) * pi / 180
                                          : static_cast< float >( sin( time * 2 * pi / 26 ) ) * 100 * pi / 180;
        eCoord const dir( cosf( bearing + swing ), sinf( bearing + swing ) );
        eCoord const pos = dir * .42f;     // about the middle of the arrow and of the light cycle

        StudioCamera & camera = sg_studioCamera;
        camera.drawn = true;
        camera.width = width;
        camera.height = height;
        camera.look = look;
        camera.eye[0] = ex;
        camera.eye[1] = ey;
        camera.eye[2] = eyeHeight;
        camera.target[0] = 0;
        camera.target[1] = 0;
        camera.target[2] = .36f;
        camera.fovY = fov * 180 / pi;
        camera.nearPlane = nearPlane;
        camera.farPlane = farPlane;
        camera.pos[0] = pos.x;
        camera.pos[1] = pos.y;
        camera.dir[0] = dir.x;
        camera.dir[1] = dir.y;
        camera.swing = swing * 180 / pi;

        se_CleanArenaBackdrop( eCoord( ex, ey ), eyeHeight );

        glDisable( GL_TEXTURE_2D );
        glDisable( GL_LIGHTING );
        glDisable( GL_CULL_FACE );
        glDisable( GL_ALPHA_TEST );
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LEQUAL );

        // the trail as the game holds it; the wall drawing takes a channel over full to full
        REAL const * const wall = trail;

        if ( sr_alphaBlend )
        {
            if ( !classic )
            {
                // the floor mirrors the cycle, as the game's mirrored pass has it
                ModelMatrix();
                glPushMatrix();
                glScalef( 1, 1, -1 );
                glFrontFace( GL_CW );
                sg_RenderCleanCycleAt( pos, dir, bike[0], bike[1], bike[2] );
                ModelMatrix();
                glPopMatrix();
                glFrontFace( GL_CCW );

                // The floor's gloss lies over the mirror image, as much of it
                // as the floor at the cycle's feet lets through. Only where the
                // image is: where depth was written, which nothing else did.
                glDepthRange( 1, 1 );
                glDepthFunc( GL_GREATER );
                glDepthMask( GL_FALSE );
                REAL const * const floor = sr_CleanFloorColor();
                glColor4f( floor[0], floor[1], floor[2], .7f );
                static const REAL around = 8;
                BeginQuads();
                glVertex3f( -around, -around, 0 );
                glVertex3f( around, -around, 0 );
                glVertex3f( around, around, 0 );
                glVertex3f( -around, around, 0 );
                RenderEnd();
                glDepthRange( 0, 1 );
                glDepthFunc( GL_LEQUAL );
                glDepthMask( GL_TRUE );
                glClear( GL_DEPTH_BUFFER_BIT );
            }

            // the trail's own mirror image, then every shadow laid once
            for ( int pass = 0; pass < 2; ++pass )
            {
                sg_CleanSheenBegin( pass );
                DrawTrail( pos, dir, wall, 1 );
                sg_CleanSheenEnd();
            }
            static const REAL reach = 18;
            if ( sg_CleanShadowsBegin() )
            {
                DrawTrail( pos, dir, wall, 0 );
                sg_CleanShadowsEnd( -reach, -reach, reach, reach );
            }
            else
            {
                glDepthMask( GL_FALSE );
                DrawTrail( pos, dir, wall, 0 );
                glDepthMask( GL_TRUE );
            }
            sg_RenderCleanCycleShadowAt( pos, dir );
        }

        glEnable( GL_DEPTH_TEST );
        glDepthMask( GL_TRUE );
        glEnable( GL_BLEND );
        glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
        glDisable( GL_CULL_FACE );
        DrawTrail( pos, dir, wall, 2 );
        if ( !classic || !sg_RenderClassicCycleAt( pos, dir, bike[0], bike[1], bike[2] ) )
            sg_RenderCleanCycleAt( pos, dir, bike[0], bike[1], bike[2] );

        // the line along the trail's top, as light, where the scene can glow
        if ( rPost::Glows( sg_CleanTrailGlow() ) )
        {
            rEmissive glow( sg_CleanTrailGlow() );
            glDepthMask( GL_FALSE );
            sr_DepthOffset( true );
            DrawTrail( pos, dir, wall, 3 );
            sr_DepthOffset( false );
            glDepthMask( GL_TRUE );
        }

        rPost::EndScene();

        sr_cleanDark = wasDark;
        glPopAttrib();
        ProjMatrix();
        PopMatrix();
        ModelMatrix();
        PopMatrix();
    }

    // ---- the page ----

    class gColourStudioMenu: public uMenu
    {
    public:
        gColourStudioMenu();
        ~gColourStudioMenu();

        void DrawRow( int row, REAL slide, REAL alpha, bool selected );
        void Adjust( int row, int step );
        void ClickRow( int row, REAL x );
        void Save();

    protected:
        virtual REAL YPos( int num );
        virtual REAL ItemDrawY( int index );
        virtual REAL ItemRowHalf( int index );
        virtual bool ItemContainsX( int index, REAL mouseX );
        virtual void RenderExtras( REAL alpha );
        virtual void HandleEvent( SDL_Event event );

    private:
        // rows are kept bottom first; row 0 is the top one
        int RowOf( int index ) const { return items.Len() - 1 - index; }
        int SelectedRow() const { return selected >= 0 && selected < items.Len() ? RowOf( selected ) : -1; }

        Pick PickAt( int pick ) const;
        void Choose( int pick );
        void SetTrail( int c, int step );
        void DrawPairs( Layout const & l, REAL slide, REAL alpha, bool selected );
        void DrawCard( Layout const & l, bool bike, REAL alpha );
        void DrawSetting( Layout const & l, REAL alpha );
        void DrawPreview( Layout const & l, REAL alpha );

        Pick edit_;                 // what is on the page
        Pick saved_;                // what the player has, as the studio found it or last saved it
        int savedSetting_[3];       // and the setting itself, as written
        int look_;                  // the preview: 0 the light arena, 1 the dark one, 2 the classic cycle
        int pairCursor_;
        double savedAt_;
        double adjustAt_;           // a bike channel held down: its steps grow
        int adjustRow_, adjustStep_, adjustRun_;
        double start_;
    };

    // one row of the page: a channel of the bike or of the trail, or the pairs
    class gColourStudioRow: public uMenuItem
    {
    public:
        gColourStudioRow( gColourStudioMenu * studio, int row )
        : uMenuItem( studio, tOutput() ), studio_( studio ), row_( row )
        {
        }

        virtual bool OwnsRow(){ return true; }

        virtual void Render( REAL x, REAL y, REAL alpha, bool selected )
        {
            (void)y;
            studio_->DrawRow( row_, x - LabelX(), alpha, selected );
        }

        virtual void LeftRight( int step ){ studio_->Adjust( row_, step ); }
        virtual void Enter(){ studio_->Save(); }

        // a click sets what it is on, and never saves
        virtual bool Click( REAL x )
        {
            studio_->ClickRow( row_, x );
            return true;
        }

    private:
        gColourStudioMenu * studio_;
        int row_;
    };

    gColourStudioMenu::gColourStudioMenu()
    : uMenu( tOutput( "$rcl_colour_studio_text" ), false ), look_( 0 ), pairCursor_( 0 ), savedAt_( -100 ),
      adjustAt_( -100 ), adjustRow_( -1 ), adjustStep_( 0 ), adjustRun_( 0 ), start_( tSysTimeFloat() )
    {
        SetStyle( uMenuStyle_RclFull );
        SetKeyHint( tString( "left right  adjust  \xb7  up down  move  \xb7  1 2 3  preview  \xb7  enter  save  \xb7  esc  back, unsaved changes are dropped" ) );

        ePlayer const * const player = ePlayer::PlayerConfig( 0 );
        for ( int c = 0; c < 3; ++c )
            savedSetting_[c] = player ? player->rgb[c] : sg_full;
        saved_ = edit_ = FromSettings( savedSetting_ );

        // the preview opens in the look the player plays in
        look_ = !sr_cleanArena ? 2 : sr_cleanDark ? 1 : 0;

        for ( int row = sg_rows - 1; row >= 0; --row )
            tNEW( gColourStudioRow )( this, row );
    }

    gColourStudioMenu::~gColourStudioMenu()
    {
        sg_ReleaseClassicCycle();
    }

    Pick gColourStudioMenu::PickAt( int pick ) const
    {
        if ( pick <= 0 || pick > sg_pairCount )
            return saved_;
        Pair const & pair = sg_pairs[ pick - 1 ];
        Pick chosen;
        for ( int c = 0; c < 3; ++c )
        {
            chosen.trail[c] = pair.trail[c];
            chosen.bike[c] = pair.bike[c];
        }
        return chosen;
    }

    void gColourStudioMenu::Choose( int pick )
    {
        pick = std::max( 0, std::min( sg_picks - 1, pick ) );
        if ( pick == pairCursor_ && SameSettings( edit_, PickAt( pick ) ) )
            return;
        pairCursor_ = pick;
        edit_ = PickAt( pick );
        uMenu::MenuSound( uMenu::Sound_Adjust );
    }

    void gColourStudioMenu::SetTrail( int c, int step )
    {
        step = std::max( 0, std::min( sg_full, step ) );
        if ( step == edit_.trail[c] )
            return;
        // at full the bike's channel comes free, as bright as the trail's: nothing jumps
        if ( step == sg_full )
            edit_.bike[c] = 255;
        edit_.trail[c] = step;
        uMenu::MenuSound( uMenu::Sound_Adjust );
    }

    void gColourStudioMenu::Adjust( int row, int step )
    {
        if ( row == sg_pairsRow )
        {
            if ( pairCursor_ + step >= 0 && pairCursor_ + step < sg_picks )
                Choose( pairCursor_ + step );
            return;
        }

        int const c = row % 3;
        if ( row >= 3 )
        {
            SetTrail( c, edit_.trail[c] + step );
            return;
        }

        // the bike's channel follows the trail's until that is full; the card says so
        if ( edit_.trail[c] < sg_full )
            return;

        double const now = tSysTimeFloat();
        if ( row == adjustRow_ && step == adjustStep_ && now - adjustAt_ < .3 )
            ++adjustRun_;
        else
            adjustRun_ = 0;
        adjustRow_ = row;
        adjustStep_ = step;
        adjustAt_ = now;
        int const amount = adjustRun_ < 6 ? 1 : adjustRun_ < 14 ? 4 : 8;

        int const value = std::max( 0, std::min( 255, edit_.bike[c] + step * amount ) );
        if ( value == edit_.bike[c] )
            return;
        edit_.bike[c] = value;
        uMenu::MenuSound( uMenu::Sound_Adjust );
    }

    void gColourStudioMenu::ClickRow( int row, REAL x )
    {
        Layout const l = Measure();
        REAL const px = ( x + 1 ) / W( 1 );
        if ( row == sg_pairsRow )
        {
            int const pick = static_cast< int >( ( px - l.left ) / ( l.pairWidth + l.pairGap ) );
            if ( px >= l.left && pick < sg_picks )
                Choose( pick );
            return;
        }

        if ( px < l.trackLeft - 10 || px > l.trackRight + 10 )
            return;
        REAL const f = Unit( ( px - l.trackLeft ) / ( l.trackRight - l.trackLeft ) );
        int const c = row % 3;
        if ( row >= 3 )
            SetTrail( c, static_cast< int >( f * sg_full + .5f ) );
        else if ( edit_.trail[c] >= sg_full )
        {
            int const value = static_cast< int >( f * 255 + .5f );
            if ( value != edit_.bike[c] )
            {
                edit_.bike[c] = value;
                uMenu::MenuSound( uMenu::Sound_Adjust );
            }
        }
    }

    void gColourStudioMenu::Save()
    {
        ePlayer * const player = ePlayer::PlayerConfig( 0 );
        if ( !player )
            return;

        int v[3];
        Settings( edit_, v );
        Pick const before = FromSettings( savedSetting_ );
        for ( int c = 0; c < 3; ++c )
        {
            // a number that already shows this channel stays as it was written
            if ( Setting( before.trail[c], before.bike[c] ) != v[c] )
                player->rgb[c] = v[c];
            savedSetting_[c] = player->rgb[c];
        }
        saved_ = FromSettings( savedSetting_ );
        savedAt_ = tSysTimeFloat();

        // a server that takes a new colour mid-game is told, as the player page does
        static nVersionFeature inGameRenames( 5 );
        if ( inGameRenames.Supported() )
        {
            ePlayerNetID::Update();
            ePlayer::SendAuthNames();
        }
        st_SaveConfig();
    }

    // The rows stand where the layout puts them, not in a scrolled list. The
    // menu is told they all lie in its middle, so it shows them all and has
    // nothing to scroll; where each is drawn is ItemDrawY.
    REAL gColourStudioMenu::YPos( int num )
    {
        (void)num;
        return ( menuTop + menuBot ) * .5f;
    }

    REAL gColourStudioMenu::ItemDrawY( int index )
    {
        return Y( RowCentre( Measure(), RowOf( index ) ) );
    }

    REAL gColourStudioMenu::ItemRowHalf( int index )
    {
        return RowOf( index ) == sg_pairsRow ? H( Measure().pairsHeight * .5f + 4 ) : H( 17 );
    }

    bool gColourStudioMenu::ItemContainsX( int index, REAL mouseX )
    {
        (void)index;
        Layout const l = Measure();
        return mouseX >= X( l.left ) && mouseX <= X( l.right );
    }

    void gColourStudioMenu::HandleEvent( SDL_Event event )
    {
        if ( event.type == SDL_KEYDOWN )
        {
            int look = -1;
            switch ( event.key.keysym.sym )
            {
            case SDLK_1: case SDLK_KP1: look = 0; break;
            case SDLK_2: case SDLK_KP2: look = 1; break;
            case SDLK_3: case SDLK_KP3: look = 2; break;
            case SDLK_c:
                // the preview's camera to a file, for Blender (RCL_STUDIO_CAMERA)
                if ( WriteStudioCamera( "studio-camera.json" ) )
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                return;
            default: break;
            }
            if ( look >= 0 )
            {
                if ( look != look_ )
                {
                    look_ = look;
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            }
        }
        uMenu::HandleEvent( event );
    }

    // a channel's row: its letter, a slider and its value, as the theme draws a slider
    void gColourStudioMenu::DrawRow( int row, REAL slide, REAL alpha, bool selected )
    {
        Layout const l = Measure();
        if ( row == sg_pairsRow )
        {
            DrawPairs( l, slide, alpha, selected );
            return;
        }

        REAL const y = RowCentre( l, row );
        bool const bike = row < 3;
        int const c = row % 3;
        bool const locked = bike && edit_.trail[c] < sg_full;

        REAL fraction;
        char value[16];
        if ( bike )
        {
            int const byte = BikeByte( edit_, c );
            fraction = byte / REAL( 255 );
            snprintf( value, sizeof( value ), "%d", byte );
        }
        else
        {
            fraction = edit_.trail[c] / REAL( sg_full );
            if ( edit_.trail[c] >= sg_full )
                snprintf( value, sizeof( value ), "full" );
            else
                snprintf( value, sizeof( value ), "%d", 17 * edit_.trail[c] );
        }

        // the selected row: a raised band with the lime edge the theme marks rows with
        if ( selected )
        {
            FillRect( X( l.label - 14 ) + slide, Y( y - 15 ), X( l.right - 6 ) + slide, Y( y + 15 ), surfaceRaised, alpha, 3 );
            FillRect( X( l.label - 14 ) + slide, Y( y - 15 ), X( l.label - 11 ) + slide, Y( y + 15 ), accent, alpha );
        }

        static char const * const letter[3] = { "r", "g", "b" };
        Text( X( l.label ) + slide, Y( y ), 13, 500, selected ? textPrimary : sg_quiet, alpha, letter[c] );

        REAL const shown = locked ? .38f * alpha : alpha;
        REAL const left = X( l.trackLeft ) + slide, right = X( l.trackRight ) + slide;
        REAL const at = left + ( right - left ) * fraction;
        FillRect( left, Y( y - 2 ), right, Y( y + 2 ), borderStrong, shown );
        FillRect( left, Y( y - 2 ), at, Y( y + 2 ), selected && !locked ? accent : textSecondary, shown );
        FillRect( at - W( 3 ), Y( y - 8 ), at + W( 3 ), Y( y + 8 ), textPrimary, shown, 2 );
        Text( X( l.value ) + slide, Y( y ), 14, 500, locked ? sg_quiet : textPrimary, alpha, value, 1 );
    }

    // the player's own colour, then the pairs: the left half of each the bike, the right half the trail
    void gColourStudioMenu::DrawPairs( Layout const & l, REAL slide, REAL alpha, bool selected )
    {
        REAL const top = l.pairsTop, bottom = l.pairsTop + l.pairsHeight;
        static const REAL inset = 7;
        for ( int i = 0; i < sg_picks; ++i )
        {
            Pick const pick = PickAt( i );
            int v[3], bike[3], trail[3];
            Settings( pick, v );
            ShownBytes( v, bike, trail );

            REAL const left = l.left + i * ( l.pairWidth + l.pairGap ), right = left + l.pairWidth;
            REAL const middle = ( left + right ) * .5f;
            bool const cursor = selected && i == pairCursor_;
            FillRect( X( left ) + slide, Y( top ), X( right ) + slide, Y( bottom ), cursor ? surfaceRaised : sg_cardColor, alpha, 6 );
            FillRect( X( left + inset ) + slide, Y( top + inset ), X( middle ) + slide, Y( bottom - inset ), Swatch( bike ), alpha );
            FillRect( X( middle ) + slide, Y( top + inset ), X( right - inset ) + slide, Y( bottom - inset ), Swatch( trail ), alpha );
            if ( cursor )
                FrameRect( X( left ) + slide, Y( top ), X( right ) + slide, Y( bottom ), 2, accent, alpha );
            else if ( SameSettings( pick, edit_ ) )
                FrameRect( X( left ) + slide, Y( top ), X( right ) + slide, Y( bottom ), 1, textSecondary, alpha );
        }
    }

    // the bike's card or the trail's: its colour as every client shows it, its hex and a note
    void gColourStudioMenu::DrawCard( Layout const & l, bool bike, REAL alpha )
    {
        REAL const top = bike ? l.bikeTop : l.trailTop;
        FillRect( X( l.left ), Y( top ), X( l.right ), Y( top + l.cardHeight ), sg_cardColor, alpha, 8 );

        int v[3], bikeShown[3], trailShown[3];
        Settings( edit_, v );
        ShownBytes( v, bikeShown, trailShown );
        int const * const shown = bike ? bikeShown : trailShown;
        FillRect( X( l.left + 24 ), Y( top + 24 ), X( l.left + 136 ), Y( top + 136 ), Swatch( shown ), alpha, 6 );
        // a dark swatch would sink into the card
        if ( .299f * shown[0] + .587f * shown[1] + .114f * shown[2] < 60 )
            FrameRect( X( l.left + 24 ), Y( top + 24 ), X( l.left + 136 ), Y( top + 136 ), 1, borderStrong, alpha );

        REAL const textLeft = l.left + 163;
        Text( X( textLeft ), Y( top + 42 ), 20, 500, textPrimary, alpha, bike ? "bike" : "trail" );
        char hex[16];
        snprintf( hex, sizeof( hex ), "#%02x%02x%02x", shown[0], shown[1], shown[2] );
        Text( X( textLeft ), Y( top + 74 ), 16, 400, textSecondary, alpha, hex );

        // what the card's channels can do, or why one cannot, or what the game did to it
        bool lifted = false;
        for ( int c = 0; c < 3; ++c )
            lifted = lifted || shown[c] != ( bike ? BikeByte( edit_, c ) : std::min( 255, 17 * edit_.trail[c] ) );
        static char const * const letter[3] = { "r", "g", "b" };
        int const row = SelectedRow();
        std::string note = bike ? "any colour where the trail is full" : "0 to 15 a channel, or full";
        if ( bike && row >= 0 && row < 3 && edit_.trail[ row ] < sg_full )
            note = std::string( "follows the trail here: set the trail's " ) + letter[ row ] + " to full to free it";
        else if ( lifted )
            note = "too dark to be seen: the game lifts it to this";
        Paragraph( X( textLeft ), Y( top + 95 ), W( l.label - 24 - textLeft ), 13, 400, sg_quiet, alpha, note.c_str(), 2 );
    }

    void gColourStudioMenu::DrawSetting( Layout const & l, REAL alpha )
    {
        REAL const top = l.settingTop, left = l.left + 20, room = W( l.right - l.left - 40 );
        FillRect( X( l.left ), Y( top ), X( l.right ), Y( top + l.settingHeight ), sg_cardColor, alpha, 8 );
        Text( X( left ), Y( top + 28 ), 12, 500, sg_quiet, alpha, "your setting" );

        if ( !SameSettings( edit_, saved_ ) )
            Text( X( l.value ), Y( top + 28 ), 12, 500, sg_quiet, alpha, "not saved: enter saves it", 1 );
        else if ( tSysTimeFloat() - savedAt_ < 2.5 )
            Text( X( l.value ), Y( top + 28 ), 12, 500, accent, alpha, "saved", 1 );

        int v[3];
        Settings( edit_, v );
        char line[96];
        snprintf( line, sizeof( line ), "COLOR_R_1 %d     COLOR_G_1 %d     COLOR_B_1 %d", v[0], v[1], v[2] );
        Text( X( left ), Y( top + 60 ), 18, 500, textPrimary, alpha, line );
        Text( X( left ), Y( top + 94 ), 13, 400, textSecondary, alpha,
              "every client draws this bike and trail the same way: rcl clients and the original game alike.", -1, room );
        Text( X( left ), Y( top + 118 ), 13, 400, sg_quiet, alpha,
              "in a team of two or more the game clamps each channel to 15 and blends the team's colour in. some servers always clamp.", -1, room );
    }

    void gColourStudioMenu::DrawPreview( Layout const & l, REAL alpha )
    {
        FillRect( X( l.previewLeft ), Y( l.previewTop ), X( l.previewRight ), Y( l.previewBottom ), canvas, alpha );

        int v[3];
        Settings( edit_, v );
        REAL bike[3], trail[3];
        Shown( v, bike, trail );
        DrawScene( l, look_, bike, trail, tSysTimeFloat() - start_ );

        // the caption, on a band along its foot; the look shown in the accent
        REAL const band = l.previewBottom - 62;
        FillRect( X( l.previewLeft ), Y( band ), X( l.previewRight ), Y( l.previewBottom ), canvas, .72f * alpha );
        Text( X( l.previewLeft + 18 ), Y( band + 20 ), 12, 500, sg_quiet, alpha, "preview" );
        static char const * const intro = "drawn by the game itself, turning.";
        REAL x = l.previewLeft + 18;
        Text( X( x ), Y( band + 42 ), 13, 400, textSecondary, alpha, intro );
        x += WidthPx( intro, 13, 400 ) + 18;
        static char const * const looks[3] = { "1 light arena", "2 dark arena", "3 classic bike" };
        for ( int i = 0; i < 3; ++i )
        {
            bool const on = i == look_;
            Text( X( x ), Y( band + 42 ), 13, on ? 500 : 400, on ? accent : textSecondary, alpha, looks[i] );
            x += WidthPx( looks[i], 13, on ? 500 : 400 ) + 16;
        }
        FrameRect( X( l.previewLeft ), Y( l.previewTop ), X( l.previewRight ), Y( l.previewBottom ), 1, borderSubtle, alpha );
    }

    void gColourStudioMenu::RenderExtras( REAL alpha )
    {
        Layout const l = Measure();
        Text( X( l.left ), Y( 88 ), 15, 400, textSecondary, alpha,
              "your bike and your trail. the trail is what others race against; it can differ from the bike where it is at full.",
              -1, W( l.previewRight - l.left ) );
        DrawCard( l, true, alpha );
        DrawCard( l, false, alpha );
        DrawSetting( l, alpha );
        Text( X( l.left ), Y( l.pairsLabel ), 13, 500, textSecondary, alpha, "pairs that work" );
        Text( X( l.left ), Y( l.pairsNote ), 12, 400, sg_quiet, alpha,
              "left half the bike, right half the trail. the first is yours as saved.", -1, W( l.right - l.left ) );
        DrawPreview( l, alpha );
    }
}

void sg_ColourStudio()
{
    if ( !ePlayer::PlayerConfig( 0 ) )
        return;
    gColourStudioMenu studio;
    studio.Enter();
}

#endif
