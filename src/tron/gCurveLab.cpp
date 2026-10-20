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

#include "gCurveLab.h"

// DEDICATED comes from config.h
#include "defs.h"

#ifdef DEDICATED

void sg_CurveLab(){}
bool sg_CurveLabAtStart(){ return false; }

#else

#include "rSDL.h"

#include "eCoord.h"
#include "eFloor.h"
#include "eGameObject.h"
#include "ePlayer.h"
#include "gCurveLabSim.h"
#include "gCycle.h"
#include "gExplosion.h"
#include "gWall.h"
#include "rGL.h"
#include "rPost.h"
#include "rRender.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tSysTime.h"
#include "uInput.h"
#include "uMenu.h"
#include "uRclTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

// Jamie: "curved trails using the cycle delay as some radius of a circle so
// delays can make big circles and then you can reverse triple left right
// right right to do a U turn and go back into the big circle", then: "maybe
// the radius should actually be really tiny so mazes actually work". This
// page tries three ways of it side by side with the classic game's line,
// on a floor of its own, with the light cycle mesh riding them:
//
//   look only        the bike rides the classic line, corners and all; only
//                    the drawing changes: its heading follows a corner over
//                    a few hundredths of a second, and its wall's corners are
//                    drawn round (what every server could have today)
//   rounded corners  corners as far apart as the classic game's, each a
//                    quarter circle of a share of the delay distance, taking
//                    effect that radius later (gCurveLabSim.h)
//   arc = delay      a turn is a quarter circle as long as the delay
//                    distance; quick presses make big circles
//
// The riders follow rcl-cfg/curves/curves_ref.py step for step. Nothing here
// touches the game: no server, no game objects, no collisions, nothing
// recorded.

// RCL_CURVE_LAB_START 1 opens the lab as the main menu first shows, for the
// test harness; it is not saved.
static int sg_curveLabStart = 0;
static tSettingItem< int > sg_curveLabStartConf( "RCL_CURVE_LAB_START", sg_curveLabStart );

namespace
{
    using namespace uRclTheme;
    namespace sim = ::gCurveLab;

    // ---- the lab: two riders, the keys and what is counted ----

    double const sg_step = .001;            // the riders' step, as the reference model's
    REAL const sg_arenaHalf = 200;          // the square the bike is kept in
    double const sg_touch = .25;            // nearer its own trail than this is a hit
    double const sg_touchGap = 2.5;         // what lies this close behind the head does not count
    double const sg_fadeTime = .45;         // out, and in again, when the bike nears the edge
    size_t const sg_maxRings = 32;
    double const sg_presetGap = .03;        // a preset's presses, as if pressed quickly
    double const sg_yawEase = .02;          // look only: the heading follows a corner in about three of these

    enum Model { Model_Look, Model_Rounded, Model_Arc, Model_Count };
    char const * const sg_modelNames[ Model_Count ] = { "look only", "rounded corners", "arc = delay" };

    enum Camera { Camera_Chase, Camera_High, Camera_Top, Camera_Orbit, Camera_Count };
    char const * const sg_cameraNames[ Camera_Count ] = { "chase", "high", "top-down", "orbit" };

    struct Preset
    {
        char const * name;
        int keys[4];
        int count;
    };
    Preset const sg_presets[4] =
    {
        { "reverse triple", { -1, 1, 1, 1 }, 4 },
        { "regular triple", { 1, 1, 1, -1 }, 4 },
        { "double", { 1, 1, 0, 0 }, 2 },
        { "four the same", { 1, 1, 1, 1 }, 4 }
    };

    // where the bike ran into its own trail, and when (on the riders' clock)
    struct Ring
    {
        double x, y, born;
    };

    struct V3
    {
        float x, y, z;
    };

    V3 Make( float x, float y, float z )
    {
        V3 const v = { x, y, z };
        return v;
    }
    V3 operator+( V3 const & a, V3 const & b ) { return Make( a.x + b.x, a.y + b.y, a.z + b.z ); }
    V3 operator-( V3 const & a, V3 const & b ) { return Make( a.x - b.x, a.y - b.y, a.z - b.z ); }
    V3 operator*( V3 const & a, float f ) { return Make( a.x * f, a.y * f, a.z * f ); }
    float Length( V3 const & a ) { return sqrtf( a.x * a.x + a.y * a.y + a.z * a.z ); }
    V3 Cross( V3 const & a, V3 const & b )
    {
        return Make( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
    }
    V3 Mix( V3 const & a, V3 const & b, float f ) { return a + ( b - a ) * f; }

    struct Pose
    {
        V3 eye, target, up;
    };

    // how far up from the middle the view reaches at a distance of 1: the
    // player's field of view, as the game's viewports take it (rViewport)
    float ViewTangent( float aspect )
    {
        ePlayer const * const player = ePlayer::PlayerConfig( 0 );
        float const fov = player && player->startFOV > 10 ? float( player->startFOV ) : 90.f;
        return tanf( fov * 3.14159265f / 360 ) / std::min( aspect, 5.f / 3.f );
    }

    double Wrapped( double angle )
    {
        double const whole = 4 * sim::quarter;
        angle = std::fmod( angle, whole );
        if ( angle > whole * .5 )
            angle -= whole;
        if ( angle < -whole * .5 )
            angle += whole;
        return angle;
    }

    class Lab
    {
    public:
        Model model;
        double speed, delay, roundness, roundLook;
        bool ghostShown, dark;
        Camera camera;
        sim::Rider bike, ghost;

        // the demo: presets in turn with straights between, until a key is pressed
        bool demo;
        int demoIndex;
        bool demoWaiting;
        double demoAt;
        std::string showing;        // what was played last, for the HUD

        // Preset 5, Jamie's sequence (sim::BigCircle): playing, where its parts
        // start on the riders' clock and when it is over, mirrored or not;
        // and the model the demo switched from to play it in arc = delay
        bool big;
        double bigMarks[5], bigAlong;   // and how far the bike had come when it began
        int bigMirror, bigPart;
        bool bigSwitched;
        Model bigFrom;

        std::string keys;           // the last presses, as a strip
        double keysAt;

        int hits;
        bool touching;
        std::deque< Ring > rings;

        // how far the trail got from the classic line, turn by turn
        std::deque< double > offTurns;
        size_t offMeasured;

        int fading;                 // 0 not, 1 out, 2 in
        double fadeStart;

        // the cameras, kept moving whether shown or not, so a cut is smooth
        double chaseAngle, highAngle, orbitAngle;
        Camera previousCamera;
        double cameraSince;
        bool snapCamera;

        double clock;               // real time of the last update
        int steps;
        double leaves;              // how far behind the bike its wall leaves, as last drawn
        int slow;                   // 0 real time, 1 half, 2 a quarter (S)
        float aspect;               // the window's, as last drawn
        // top-down: the stretch of floor framed, its middle and half its size
        double frameX, frameY, frameHalfX, frameHalfY;
        bool snapFrame;

        Lab()
        : model( Model_Look ), speed( 30 ), delay( .25 ), roundness( .15 ), roundLook( .4 ),
          ghostShown( false ), dark( !sr_cleanArena || sr_cleanDark ), camera( Camera_Chase ),
          demo( false ), demoIndex( 0 ), demoWaiting( false ), demoAt( 1 ),
          big( false ), bigAlong( 0 ), bigMirror( 1 ), bigPart( -1 ), bigSwitched( false ), bigFrom( Model_Look ), keysAt( -100 ),
          hits( 0 ), touching( false ), offMeasured( 0 ), fading( 0 ), fadeStart( 0 ),
          chaseAngle( sim::quarter ), highAngle( sim::quarter ), orbitAngle( -2.2 ),
          previousCamera( Camera_Chase ), cameraSince( -100 ), snapCamera( true ),
          clock( tSysTimeFloat() ), steps( 0 ), leaves( .9 ), slow( 0 ), aspect( 16.f / 9 ),
          frameX( 0 ), frameY( 0 ), frameHalfX( 15 ), frameHalfY( 15 ), snapFrame( true )
        {
            Configure();
            Restart();
        }

        double DelayDistance() const { return speed * delay; }

        // the rules each rider rides by, from the settings
        void Configure()
        {
            sim::Rider * const riders[2] = { &bike, &ghost };
            for ( int i = 0; i < 2; ++i )
            {
                riders[i]->speed = speed;
                riders[i]->delay = delay;
            }
            // the ghost is the classic game: corners at the press, the delay apart
            ghost.rule = sim::Rule_Rounded;
            ghost.roundness = 0;
            ghost.yawEase = 0;
            bike.rule = model == Model_Arc ? sim::Rule_Arc : sim::Rule_Rounded;
            bike.roundness = model == Model_Rounded ? roundness : 0;
            bike.yawEase = model == Model_Look ? sg_yawEase : 0;
        }

        // both riders back at the middle, heading north, trails gone
        void Restart()
        {
            bike.Reset( 0, 0, sim::quarter );
            ghost.Reset( 0, 0, sim::quarter );
            hits = 0;
            touching = false;
            rings.clear();
            offTurns.clear();
            offMeasured = 0;
            steps = 0;
            demoWaiting = false;
            demoAt = 1;
            snapCamera = true;
            snapFrame = true;
            big = false;
            bigPart = -1;
            if ( bigSwitched )
                GiveModelBack();
        }

        // the model the demo had before it played the big circles in arc = delay
        void GiveModelBack()
        {
            bigSwitched = false;
            model = bigFrom;
            ghostShown = model != Model_Look;
            Configure();
        }

        double TimeScale() const { return slow == 0 ? 1 : slow == 1 ? .5 : .25; }

        void AddKey( int direction, double now )
        {
            if ( now - keysAt > 1.5 )
                keys.clear();
            keysAt = now;
            if ( !keys.empty() )
                keys += ' ';
            keys += direction > 0 ? 'R' : 'L';
            // the last eight
            if ( keys.size() > 15 )
                keys.erase( 0, keys.size() - 15 );
        }

        void Press( double at, int direction )
        {
            bike.Press( at, direction );
            ghost.Press( at, direction );
        }

        void Turn( int direction, double now )
        {
            demo = false;
            bigSwitched = false;
            showing.clear();
            Press( bike.time, direction );
            AddKey( direction, now );
        }

        void Play( int preset, bool mirrored, double now, bool byHand )
        {
            if ( byHand )
            {
                demo = false;
                bigSwitched = false;
            }
            if ( preset == 4 )
            {
                PlayBig( mirrored, now, byHand );
                return;
            }
            Preset const & p = sg_presets[ preset ];
            keysAt = -100;
            for ( int i = 0; i < p.count; ++i )
            {
                int const direction = mirrored ? -p.keys[i] : p.keys[i];
                Press( bike.time + i * sg_presetGap, direction );
                AddKey( direction, now );
            }
            showing = std::string( p.name ) + ( mirrored ? ", mirrored" : "" );
        }

        // Preset 5: Jamie's sequence, in arc = delay, the model it was thought
        // of in. By hand it starts a run of its own in that model; the demo
        // plays it where it is, between presets, where nothing is under way,
        // and gives the model back after.
        void PlayBig( bool mirrored, double now, bool byHand )
        {
            if ( model != Model_Arc )
            {
                if ( byHand )
                {
                    model = Model_Arc;
                    ghostShown = true;
                    Configure();
                    Restart();
                }
                else
                {
                    bigFrom = model;
                    bigSwitched = true;
                    model = Model_Arc;
                    ghostShown = true;
                    Configure();
                }
            }
            std::vector< sim::Press > presses;
            bigMirror = mirrored ? -1 : 1;
            sim::BigCircle( delay, delay, bike.time + .3, sg_presetGap, bigMirror, presses, bigMarks );
            for ( size_t i = 0; i < presses.size(); ++i )
                Press( presses[i].time, presses[i].direction );
            big = true;
            bigAlong = bike.along;
            bigPart = -1;
            keysAt = -100;
            (void)now;
            showing = std::string( "big circles" ) + ( mirrored ? ", mirrored" : "" );
        }

        // which part of the sequence is playing, -1 none
        int BigPart() const
        {
            if ( !big )
                return -1;
            int part = -1;
            for ( int i = 0; i < 4; ++i )
                if ( bike.time >= bigMarks[i] )
                    part = i;
            return part;
        }

        // the keys of a part, as pressed
        std::string BigKeys( int part ) const
        {
            static const int pressed[4][4] = { { 1, 1, 1, 1 }, { -1, 1, 1, 1 }, { 1, 1, 1, -1 }, { 1, 1, 1, 1 } };
            std::string strip;
            for ( int i = 0; i < 4 && part >= 0; ++i )
            {
                if ( i )
                    strip += ' ';
                strip += pressed[ part ][i] * bigMirror > 0 ? 'R' : 'L';
            }
            return strip;
        }

        void StartDemo()
        {
            demo = true;
            demoIndex = 0;
            demoWaiting = false;
            demoAt = bike.time + .8;
        }

        // the demo's next preset, at the riders' time
        void DemoStep( double now )
        {
            if ( !demo )
                return;
            if ( demoWaiting )
            {
                // a straight after each, a second and a half or more, so it can be seen
                if ( bike.Idle() && ghost.Idle() )
                {
                    if ( bigSwitched )
                        GiveModelBack();
                    demoWaiting = false;
                    demoAt = bike.time + std::max( 1.5, 1 + 2 * delay );
                }
                return;
            }
            if ( bike.time < demoAt )
                return;
            // the five, then the five mirrored, so the bike stays about the middle
            Play( demoIndex % 5, ( demoIndex / 5 ) % 2 == 1, now, false );
            ++demoIndex;
            demoWaiting = true;
        }

        void CheckHit()
        {
            double const nearest = sim::NearestOwnTrail( bike, sg_touchGap );
            if ( nearest < sg_touch )
            {
                if ( !touching )
                {
                    touching = true;
                    ++hits;
                    Ring const ring = { bike.x, bike.y, bike.time };
                    rings.push_back( ring );
                    if ( rings.size() > sg_maxRings )
                        rings.pop_front();
                }
            }
            else if ( nearest > sg_touch + .15 )
                touching = false;
        }

        // The trail of each turn against the ghost's, once both have made it
        // and the ghost has a little of its next line down.
        void MeasureOff()
        {
            while ( offMeasured < bike.turnEnds.size() && offMeasured < ghost.turnEnds.size() )
            {
                size_t const k = offMeasured;
                double const end = bike.turnEnds[k];
                if ( bike.along < end + .5 || ghost.along < ghost.turnEnds[k] + 1 )
                    return;
                double from = k ? bike.turnEnds[k - 1] : 0;
                from = std::max( from, std::max( end - 60, bike.trail.front().along ) );
                double const to = std::min( end + .5, bike.along );
                offTurns.push_back( from < to ? sim::FarthestFrom( bike, from, to, ghost, .25 ) : 0 );
                if ( offTurns.size() > 4 )
                    offTurns.pop_front();
                ++offMeasured;
            }
        }

        double Off() const
        {
            double off = 0;
            for ( size_t i = 0; i < offTurns.size(); ++i )
                off = std::max( off, offTurns[i] );
            return off;
        }

        bool NearEdge( sim::Rider const & rider ) const
        {
            double const margin = speed * sg_fadeTime + 6;
            return std::fabs( rider.x ) > sg_arenaHalf - margin || std::fabs( rider.y ) > sg_arenaHalf - margin;
        }

        double Fade( double now ) const
        {
            double const f = std::min( 1.0, ( now - fadeStart ) / sg_fadeTime );
            return fading == 1 ? f : fading == 2 ? 1 - f : 0;
        }

        void Update( double now )
        {
            double dt = now - clock;
            clock = now;
            if ( !( dt > 0 ) )
                dt = 0;
            if ( dt > .1 )
                dt = .1;

            // both riders on the same clock, slowed down if asked; the demo
            // and the hits in step with them
            double const target = bike.time + dt * TimeScale();
            while ( bike.time < target )
            {
                DemoStep( now );
                bike.Step( sg_step );
                while ( ghost.time < bike.time )
                    ghost.Step( sg_step );
                if ( ++steps % 4 == 0 )
                    CheckHit();
            }
            MeasureOff();

            // the big circles: the keys of the part playing, until it is over
            if ( big )
            {
                int const part = BigPart();
                if ( part != bigPart )
                {
                    bigPart = part;
                    if ( part >= 0 )
                    {
                        keys = BigKeys( part );
                        keysAt = now;
                    }
                }
                if ( bike.time >= bigMarks[4] )
                    big = false;
            }

            // near the edge: fade out, start again in the middle, fade in
            if ( fading == 0 && ( NearEdge( bike ) || NearEdge( ghost ) ) )
            {
                fading = 1;
                fadeStart = now;
            }
            else if ( fading == 1 && now - fadeStart >= sg_fadeTime )
            {
                Restart();
                fading = 2;
                fadeStart = now;
            }
            else if ( fading == 2 && now - fadeStart >= sg_fadeTime )
                fading = 0;

            // the cameras turn after the bike, at their own pace
            double const yaw = bike.yaw;
            if ( snapCamera )
            {
                chaseAngle = highAngle = yaw;
                snapCamera = false;
            }
            chaseAngle += Wrapped( yaw - chaseAngle ) * ( 1 - std::exp( -dt / .22 ) );
            // the game's custom camera turns at 4 (CAMERA_CUSTOM_TURN_SPEED)
            highAngle += Wrapped( yaw - highAngle ) * ( 1 - std::exp( -dt * 4 ) );
            orbitAngle += dt * .3;
            Frame( dt );
        }

        // the box round the last `way` of a rider's trail, to its head
        static void Around( sim::Rider const & rider, double way, double & lowX, double & lowY,
                            double & highX, double & highY )
        {
            double const from = rider.along - way;
            sim::Vertex const ends[2] = { sim::TrailAt( rider, from ), sim::TrailPoint( rider, rider.trail.size() ) };
            for ( int e = 0; e < 2; ++e )
            {
                lowX = std::min( lowX, ends[e].x );
                highX = std::max( highX, ends[e].x );
                lowY = std::min( lowY, ends[e].y );
                highY = std::max( highY, ends[e].y );
            }
            for ( size_t i = 0; i < rider.trail.size(); ++i )
            {
                sim::Vertex const & v = rider.trail[i];
                if ( v.along < from )
                    continue;
                lowX = std::min( lowX, v.x );
                highX = std::max( highX, v.x );
                lowY = std::min( lowY, v.y );
                highY = std::max( highY, v.y );
            }
        }

        // Top-down frames what was just ridden, not only the bike: the last
        // four seconds of both trails and the bike, with room round them and
        // never less than 30 across. It follows smoothly, takes room at once
        // and gives it back slowly, so it does not pump; the bike stays in.
        void Frame( double dt )
        {
            double lowX = bike.x, highX = bike.x, lowY = bike.y, highY = bike.y;
            // the big circles last longer than that: all of them while they play
            double const way = big ? std::max( speed * 4, bike.along - bigAlong ) : speed * 4;
            Around( bike, way, lowX, lowY, highX, highY );
            if ( ghostShown )
                Around( ghost, way, lowX, lowY, highX, highY );
            static const double margin = 8, least = 15;
            double const x = ( lowX + highX ) * .5, y = ( lowY + highY ) * .5;
            double const halfX = std::max( least, ( highX - lowX ) * .5 + margin );
            double const halfY = std::max( least, ( highY - lowY ) * .5 + margin );
            if ( snapFrame )
            {
                frameX = x;
                frameY = y;
                frameHalfX = halfX;
                frameHalfY = halfY;
                snapFrame = false;
                return;
            }
            double const follow = 1 - std::exp( -dt / .4 );
            double const grow = 1 - std::exp( -dt / .25 ), give = 1 - std::exp( -dt / 1.6 );
            frameX += ( x - frameX ) * follow;
            frameY += ( y - frameY ) * follow;
            frameHalfX += ( halfX - frameHalfX ) * ( halfX > frameHalfX ? grow : give );
            frameHalfY += ( halfY - frameHalfY ) * ( halfY > frameHalfY ? grow : give );
            frameHalfX = std::max( frameHalfX, std::fabs( bike.x - frameX ) + margin * .5 );
            frameHalfY = std::max( frameHalfY, std::fabs( bike.y - frameY ) + margin * .5 );
        }

        Pose ShotOf( Camera which ) const
        {
            V3 const at = Make( float( bike.x ), float( bike.y ), 0 );
            V3 const sky = Make( 0, 0, 1 );
            Pose pose;
            pose.up = sky;
            switch ( which )
            {
            case Camera_Chase:
            {
                V3 const dir = Make( float( cos( chaseAngle ) ), float( sin( chaseAngle ) ), 0 );
                pose.eye = at - dir * 7 + Make( 0, 0, 3.2f );
                pose.target = at + dir * 5 + Make( 0, 0, .4f );
                break;
            }
            case Camera_High:
            {
                // as the shipped custom camera (config/settings_visual.cfg):
                // back 20 + .5 a unit of speed, up 20 + .4, pitch -.75
                V3 const dir = Make( float( cos( highAngle ) ), float( sin( highAngle ) ), 0 );
                float const back = float( 20 + .5 * speed ), rise = float( 20 + .4 * speed );
                pose.eye = at - dir * back + Make( 0, 0, rise );
                pose.target = pose.eye + dir + Make( 0, 0, -.75f );
                break;
            }
            case Camera_Top:
            {
                // straight down, north up, over what was just ridden (Frame)
                float const tall = ViewTangent( aspect ), wide = tall * aspect;
                float const height = float( std::max( frameHalfY / tall, frameHalfX / wide ) );
                V3 const middle = Make( float( frameX ), float( frameY ), 0 );
                pose.eye = middle + Make( 0, 0, height );
                pose.target = middle;
                pose.up = Make( 0, 1, 0 );
                break;
            }
            default:
            {
                V3 const round = Make( float( cos( orbitAngle ) ), float( sin( orbitAngle ) ), 0 );
                pose.eye = at + round * 16 + Make( 0, 0, 7 );
                pose.target = at + Make( 0, 0, .6f );
                break;
            }
            }
            return pose;
        }

        // the camera shown, and for a moment after C the one before it handing over
        Pose CameraPose( double now ) const
        {
            Pose pose = ShotOf( camera );
            float const t = float( ( now - cameraSince ) / .6 );
            if ( t < 1 && previousCamera != camera )
            {
                float const s = t <= 0 ? 0 : t * t * ( 3 - 2 * t );
                Pose const from = ShotOf( previousCamera );
                pose.eye = Mix( from.eye, pose.eye, s );
                pose.target = Mix( from.target, pose.target, s );
                pose.up = Mix( from.up, pose.up, s );
            }
            return pose;
        }

        void NextCamera( double now )
        {
            previousCamera = camera;
            camera = Camera( ( camera + 1 ) % Camera_Count );
            cameraSince = now;
        }
    };

    // ---- drawing ----

    // the player's colour as every client takes it (gCycle's constructor; the
    // colour studio's Shown): the bike's, which over full wraps when drawn,
    // and the trail's
    void PlayerColours( REAL bike[3], REAL trail[3] )
    {
        ePlayer const * const player = ePlayer::PlayerConfig( 0 );
        for ( int c = 0; c < 3; ++c )
            bike[c] = trail[c] = REAL( ( player ? player->rgb[c] : 15 ) / 15.0 );
        se_MakeColorValid( bike[0], bike[1], bike[2], 1.0f );
        se_MakeColorValid( trail[0], trail[1], trail[2], .5f );
    }

    void ApplyCamera( Pose const & pose, float aspect )
    {
        float const top = ViewTangent( aspect );
        float const nearPlane = .3f;
        double const farPlane = 1E+20;

        ProjMatrix();
        IdentityMatrix();
        glFrustum( -top * aspect * nearPlane, top * aspect * nearPlane, -top * nearPlane, top * nearPlane,
                   nearPlane, farPlane );

        V3 forward = pose.target - pose.eye;
        forward = forward * ( 1 / std::max( 1E-6f, Length( forward ) ) );
        V3 side = Cross( forward, pose.up );
        if ( Length( side ) < 1E-3f )
            side = Cross( forward, Make( 0, 1, 0 ) );
        side = side * ( 1 / std::max( 1E-6f, Length( side ) ) );
        V3 const up = Cross( side, forward );
        GLfloat const matrix[16] = { side.x, up.x, -forward.x, 0,
                                     side.y, up.y, -forward.y, 0,
                                     side.z, up.z, -forward.z, 0,
                                     0, 0, 0, 1 };
        ModelMatrix();
        IdentityMatrix();
        glMultMatrixf( matrix );
        glTranslatef( -pose.eye.x, -pose.eye.y, -pose.eye.z );
    }

    typedef std::vector< sim::Vertex > Path;

    // the rider's trail as it is, to its head
    void PlainPath( sim::Rider const & rider, Path & out )
    {
        out.assign( rider.trail.begin(), rider.trail.end() );
        out.push_back( sim::TrailPoint( rider, rider.trail.size() ) );
    }

    // Look only: the classic line with every corner drawn as a circle's arc
    // of the given radius, as far as half of each line beside it allows. The
    // newest corner's next line is still being laid; the delay keeps it at
    // least D long, so half of D stands in for it. The radius is never more
    // than how far behind the bike the wall leaves, so a corner is never
    // drawn round before the wall reaches it.
    void LookPath( sim::Rider const & rider, double round, Path & out )
    {
        out.clear();
        size_t const n = rider.trail.size();
        out.push_back( sim::TrailPoint( rider, 0 ) );
        for ( size_t i = 1; i < n; ++i )
        {
            sim::Vertex const a = sim::TrailPoint( rider, i - 1 ), c = sim::TrailPoint( rider, i ),
                              b = sim::TrailPoint( rider, i + 1 );
            double const inX = c.x - a.x, inY = c.y - a.y, inLength = std::sqrt( inX * inX + inY * inY );
            double outX = b.x - c.x, outY = b.y - c.y, outLength = std::sqrt( outX * outX + outY * outY );
            if ( outLength < 1E-6 )
            {
                // the head is at the corner: its heading is the next line's
                outX = std::cos( rider.heading );
                outY = std::sin( rider.heading );
                outLength = 1;
            }
            if ( !( inLength > 1E-6 ) )
            {
                out.push_back( c );
                continue;
            }
            double const ux = inX / inLength, uy = inY / inLength, vx = outX / outLength, vy = outY / outLength;
            // the turn there: a classic corner is a quarter; what an arc left
            // behind (after the big circles) is a few degrees, and stays near enough as it is
            double const cross = ux * vy - uy * vx;
            double const turn = std::atan2( std::fabs( cross ), ux * vx + uy * vy );
            double const half = std::tan( turn * .5 );
            // how far along each line the arc reaches
            double cut = round * half;
            cut = std::min( cut, std::min( inLength * .5, rider.DelayDistance() * .5 ) );
            if ( i + 1 < n )
                cut = std::min( cut, outLength * .5 );
            if ( !( cut > 1E-4 ) || !( half > 1E-3 ) )
            {
                out.push_back( c );
                continue;
            }
            double const r = cut / half;
            // into the turn from where the arc starts: its middle, and the way round
            double const sense = cross > 0 ? 1 : -1;
            double const sx = c.x - ux * cut, sy = c.y - uy * cut;
            double const cx = sx - sense * uy * r, cy = sy + sense * ux * r;
            double const spokeX = sx - cx, spokeY = sy - cy;
            int pieces = int( std::ceil( turn / sim::Rider::MaxPieceTurn() ) );
            pieces = std::max( pieces, int( std::ceil( r * turn / sim::Rider::MaxPiece() ) ) );
            pieces = std::max( 1, std::min( 64, pieces ) );
            for ( int k = 0; k <= pieces; ++k )
            {
                double const angle = sense * turn * k / pieces;
                double const cosine = std::cos( angle ), sine = std::sin( angle );
                sim::Vertex const p = { cx + spokeX * cosine - spokeY * sine, cy + spokeX * sine + spokeY * cosine,
                                        c.along - cut + 2 * cut * k / pieces };
                out.push_back( p );
            }
        }
        out.push_back( sim::TrailPoint( rider, n ) );
    }

    sim::Vertex Between( sim::Vertex const & a, sim::Vertex const & b, double along )
    {
        double const f = b.along > a.along ? ( along - a.along ) / ( b.along - a.along ) : 0;
        sim::Vertex const v = { a.x + ( b.x - a.x ) * f, a.y + ( b.y - a.y ) * f, along };
        return v;
    }

    // The trail in one of sg_CleanWallPiece's passes, from its oldest end to
    // where it leaves the bike, `leaves` behind the head along the trail. It
    // leaves low and comes up to its full height over the next few lengths,
    // as the game's wall does (gWall.cpp, sg_CleanTip).
    void DrawTrail( Path const & path, double leaves, REAL const colour[3], int pass )
    {
        if ( path.size() < 2 )
            return;
        double const end = path.back().along - leaves;
        // as tall as the bike, and out of it as square, as the game draws the
        // mesh cycle's walls (sg_CleanWallTip)
        double const full = sg_CleanWallHeight( 2 );
        gCleanWallTip const tip = sg_CleanWallTip( 2 );
        double const low = tip.low, rise = tip.rise;
        static const int steps = 6;

        if ( pass != 3 )
            BeginQuads();
        for ( size_t i = 0; i + 1 < path.size(); ++i )
        {
            sim::Vertex const a = path[i];
            if ( a.along >= end )
                break;
            sim::Vertex b = path[i + 1];
            if ( b.along > end )
                b = Between( a, b, end );

            // cut where the rise has its steps, so it is a curve
            double cuts[ steps + 2 ];
            int count = 0;
            cuts[ count++ ] = a.along;
            for ( int k = steps; k >= 1; --k )
            {
                double const at = end - rise * k / steps;
                if ( at > a.along && at < b.along )
                    cuts[ count++ ] = at;
            }
            cuts[ count++ ] = b.along;

            for ( int j = 0; j + 1 < count; ++j )
            {
                sim::Vertex const p = Between( a, b, cuts[j] ), q = Between( a, b, cuts[j + 1] );
                REAL height[2];
                double const at[2] = { p.along, q.along };
                for ( int e = 0; e < 2; ++e )
                {
                    double f = ( end - at[e] ) / rise;
                    f = f < 0 ? 0 : ( f > 1 ? 1 : f );
                    height[e] = REAL( full * ( low + ( 1 - low ) * f * f * ( 3 - 2 * f ) ) );
                }
                sg_CleanWallPiece( eCoord( REAL( p.x ), REAL( p.y ) ), eCoord( REAL( q.x ), REAL( q.y ) ),
                                   height[0], height[1], colour[0], colour[1], colour[2], 1, pass );
            }
        }
        RenderEnd();
    }

    // The classic ghost: the same keys with the classic game's corners, as a
    // thin, see-through grey wall, low, and an arrowhead on the floor where it is.
    void DrawGhost( sim::Rider const & ghost, Path const & path, bool dark )
    {
        if ( path.size() < 2 )
            return;
        static const REAL height = .55f;
        REAL const grey = dark ? .78f : .48f;
        double const end = path.back().along - .3;

        glDepthMask( GL_FALSE );
        BeginQuads();
        for ( size_t i = 0; i + 1 < path.size() && path[i].along < end; ++i )
        {
            sim::Vertex const a = path[i], b = path[i + 1].along > end ? Between( a, path[i + 1], end ) : path[i + 1];
            glColor4f( grey, grey, grey, .30f );
            glVertex3f( REAL( a.x ), REAL( a.y ), 0 );
            glVertex3f( REAL( b.x ), REAL( b.y ), 0 );
            glColor4f( grey, grey, grey, .16f );
            glVertex3f( REAL( b.x ), REAL( b.y ), height );
            glVertex3f( REAL( a.x ), REAL( a.y ), height );
        }
        RenderEnd();
        BeginLines();
        glColor4f( grey, grey, grey, .7f );
        for ( size_t i = 0; i + 1 < path.size() && path[i].along < end; ++i )
        {
            sim::Vertex const a = path[i], b = path[i + 1].along > end ? Between( a, path[i + 1], end ) : path[i + 1];
            glVertex3f( REAL( a.x ), REAL( a.y ), height );
            glVertex3f( REAL( b.x ), REAL( b.y ), height );
        }
        RenderEnd();

        REAL const hx = REAL( std::cos( ghost.heading ) ), hy = REAL( std::sin( ghost.heading ) );
        REAL const x = REAL( ghost.x ), y = REAL( ghost.y );
        glColor4f( grey, grey, grey, .45f );
        BeginTriangles();
        glVertex3f( x + hx * .9f, y + hy * .9f, .03f );
        glVertex3f( x - hx * .5f - hy * .55f, y - hy * .5f + hx * .55f, .03f );
        glVertex3f( x - hx * .5f + hy * .55f, y - hy * .5f - hx * .55f, .03f );
        RenderEnd();
        glDepthMask( GL_TRUE );
    }

    // Where the bike ran into its own trail: a red ring on the floor, light,
    // glowing with the new renderer, and a wider one that opens and fades.
    void DrawRings( Lab const & lab )
    {
        if ( lab.rings.empty() )
            return;
        static const int segments = 32;
        static const float pi = 3.14159265f;
        glDepthMask( GL_FALSE );
        {
            rEmissive glow( std::max( REAL( 2 ), sg_ExplosionGlow() ) );
            BeginQuads();
            for ( size_t i = 0; i < lab.rings.size(); ++i )
            {
                Ring const & ring = lab.rings[i];
                float const age = float( lab.bike.time - ring.born );
                float const grown = std::min( 1.f, std::max( 0.f, age / .18f ) );
                float const x = float( ring.x ), y = float( ring.y );
                for ( int band = 0; band < 2; ++band )
                {
                    float outer = .5f + .45f * grown, inner = outer * .62f, alpha = .9f;
                    if ( band )
                    {
                        if ( age > 1 )
                            continue;
                        outer = .95f + 2.4f * age;
                        inner = outer * .82f;
                        alpha = .55f * ( 1 - age );
                    }
                    for ( int k = 0; k < segments; ++k )
                    {
                        float const a0 = 2 * pi * k / segments, a1 = 2 * pi * ( k + 1 ) / segments;
                        glColor4f( 1, .16f, .12f, alpha );
                        glVertex3f( x + cosf( a0 ) * inner, y + sinf( a0 ) * inner, .05f );
                        glVertex3f( x + cosf( a1 ) * inner, y + sinf( a1 ) * inner, .05f );
                        glVertex3f( x + cosf( a1 ) * outer, y + sinf( a1 ) * outer, .05f );
                        glVertex3f( x + cosf( a0 ) * outer, y + sinf( a0 ) * outer, .05f );
                    }
                }
            }
            RenderEnd();
        }
        glDepthMask( GL_TRUE );
    }

    // the radius look only draws the wall's corners with
    double LookRadius( Lab const & lab, double leaves )
    {
        return std::max( 0.0, std::min( lab.roundLook, std::min( leaves, lab.DelayDistance() * .5 ) ) );
    }

    // The scene across the window: the clean arena, pale or night, the
    // bike's trail and the ghost's, the bike, and the rings, in a scene of
    // its own (rPost) as the colour studio's preview is.
    void DrawScene( Lab & lab, double now, Path & bikePath, Path & ghostPath )
    {
        if ( !sr_glOut )
            return;

        GLint viewport[4];
        glGetIntegerv( GL_VIEWPORT, viewport );
        if ( viewport[2] < 16 || viewport[3] < 16 )
            return;
        float const aspect = float( viewport[2] ) / viewport[3];
        lab.aspect = aspect;

        RenderEnd();
        ProjMatrix();
        PushMatrix();
        ModelMatrix();
        PushMatrix();
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        glDepthMask( GL_TRUE );
        glClear( GL_DEPTH_BUFFER_BIT );

        bool const wasDark = sr_cleanDark;
        sr_cleanDark = lab.dark;
        {
            rScene scene;

            Pose const pose = lab.CameraPose( now );
            ApplyCamera( pose, aspect );
            se_CleanArenaBackdrop( eCoord( pose.eye.x, pose.eye.y ), pose.eye.z );

            glDisable( GL_TEXTURE_2D );
            glDisable( GL_LIGHTING );
            glDisable( GL_CULL_FACE );
            glDisable( GL_ALPHA_TEST );
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glEnable( GL_DEPTH_TEST );
            glDepthFunc( GL_LEQUAL );

            // the square the bike is kept in, marked on the floor
            REAL const h = sg_arenaHalf;
            eCoord const corner[5] = { eCoord( -h, -h ), eCoord( -h, h ), eCoord( h, h ), eCoord( h, -h ), eCoord( -h, -h ) };
            for ( int i = 0; i < 4; ++i )
                sg_CleanRimMarking( corner[i], corner[i + 1] );

            // the bike in its own colours however small: the lab is for looking at it
            REAL bike[3], trail[3];
            PlayerColours( bike, trail );
            eCoord const pos( REAL( lab.bike.x ), REAL( lab.bike.y ) );
            eCoord const dir( REAL( std::cos( lab.bike.yaw ) ), REAL( std::sin( lab.bike.yaw ) ) );
            REAL const lean = REAL( lab.bike.lean ), distance = REAL( lab.bike.along );

            if ( sr_alphaBlend )
            {
                // the floor mirrors the bike, as the colour studio's preview has it
                ModelMatrix();
                glPushMatrix();
                glScalef( 1, 1, -1 );
                glFrontFace( GL_CW );
                sg_RenderMeshCycleAt( pos, dir, lean, distance, bike[0], bike[1], bike[2], false );
                ModelMatrix();
                glPopMatrix();
                glFrontFace( GL_CCW );

                // the floor's gloss over the image, only where it is
                glDepthRange( 1, 1 );
                glDepthFunc( GL_GREATER );
                glDepthMask( GL_FALSE );
                REAL const * const floor = sr_CleanFloorColor();
                glColor4f( floor[0], floor[1], floor[2], .7f );
                static const REAL around = 8;
                BeginQuads();
                glVertex3f( pos.x - around, pos.y - around, 0 );
                glVertex3f( pos.x + around, pos.y - around, 0 );
                glVertex3f( pos.x + around, pos.y + around, 0 );
                glVertex3f( pos.x - around, pos.y + around, 0 );
                RenderEnd();
                glDepthRange( 0, 1 );
                glDepthFunc( GL_LEQUAL );
                glDepthMask( GL_TRUE );
                glClear( GL_DEPTH_BUFFER_BIT );
            }

            // where the wall leaves the bike: the mesh cycle's rear axle once it draws
            double const leaves = lab.leaves = sg_CleanWallStart( 2 );
            if ( lab.model == Model_Look )
                LookPath( lab.bike, LookRadius( lab, leaves ), bikePath );
            else
                PlainPath( lab.bike, bikePath );
            PlainPath( lab.ghost, ghostPath );

            if ( sr_alphaBlend )
            {
                // the trail's mirror image, then every shadow laid once
                for ( int pass = 0; pass < 2; ++pass )
                {
                    sg_CleanSheenBegin( pass );
                    DrawTrail( bikePath, leaves, trail, 1 );
                    sg_CleanSheenEnd();
                }
                REAL const reach = sg_arenaHalf + 40;
                if ( sg_CleanShadowsBegin() )
                {
                    DrawTrail( bikePath, leaves, trail, 0 );
                    sg_CleanShadowsEnd( -reach, -reach, reach, reach );
                }
                else
                {
                    glDepthMask( GL_FALSE );
                    DrawTrail( bikePath, leaves, trail, 0 );
                    glDepthMask( GL_TRUE );
                }
                sg_RenderCleanCycleShadowAt( pos, dir );
            }

            glEnable( GL_DEPTH_TEST );
            glDepthMask( GL_TRUE );
            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            glDisable( GL_CULL_FACE );
            DrawTrail( bikePath, leaves, trail, 2 );
            sg_RenderMeshCycleAt( pos, dir, lean, distance, bike[0], bike[1], bike[2], false );

            // the line along the trail's top, as light, where the scene can glow
            if ( rPost::Glows( sg_CleanTrailGlow() ) )
            {
                rEmissive glow( sg_CleanTrailGlow() );
                glDepthMask( GL_FALSE );
                sr_DepthOffset( true );
                DrawTrail( bikePath, leaves, trail, 3 );
                sr_DepthOffset( false );
                glDepthMask( GL_TRUE );
            }

            glEnable( GL_BLEND );
            glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
            if ( lab.ghostShown )
                DrawGhost( lab.ghost, ghostPath, lab.dark );
            DrawRings( lab );

            // out and in through the floor's own colour, at the edge
            double const fade = lab.Fade( now );
            if ( fade > 0 )
            {
                ProjMatrix();
                IdentityMatrix();
                ModelMatrix();
                IdentityMatrix();
                REAL const * const floor = sr_CleanFloorColor();
                glDisable( GL_DEPTH_TEST );
                glColor4f( floor[0], floor[1], floor[2], REAL( fade ) );
                BeginQuads();
                glVertex2f( -1, -1 );
                glVertex2f( 1, -1 );
                glVertex2f( 1, 1 );
                glVertex2f( -1, 1 );
                RenderEnd();
            }
        }
        sr_cleanDark = wasDark;

        glPopAttrib();
        ProjMatrix();
        PopMatrix();
        ModelMatrix();
        PopMatrix();
        glColor4f( 1, 1, 1, 1 );
    }

    // ---- the HUD ----

    uRclTheme::Color const sg_quiet = { .478f, .486f, .494f };

    // the block bottom left, lowercase and small, over a band that keeps it readable
    void DrawHud( Lab const & lab, double now, REAL alpha )
    {
        REAL const margin = MarginPx();
        Text( X( margin ), Y( 52 ), 22, 500, textPrimary, alpha, "curve lab" );
        Text( X( margin ), Y( 80 ), 13, 400, textSecondary, alpha,
              "curved trails, tried on a floor of their own: no server, nothing recorded. the game itself is unchanged." );

        double const D = lab.DelayDistance();
        char model[160], pace[96], off[128], keys[96], mode[128], view[96];
        double const leaves = lab.leaves;
        switch ( lab.model )
        {
        case Model_Look:
        {
            double const r = LookRadius( lab, leaves );
            std::snprintf( model, sizeof( model ), "look only \xb7 the bike rides the classic line; its wall's corners drawn round, r %.2f", r );
            // a quarter circle in a corner is .29 of its radius from either line
            std::snprintf( off, sizeof( off ), "bike on the classic line \xb7 wall off it %.2f at corners \xb7 hits %d",
                           r * ( 1 - std::sqrt( .5 ) ), lab.hits );
            break;
        }
        case Model_Rounded:
        {
            double const r = lab.roundness * D;
            std::snprintf( model, sizeof( model ), "rounded corners \xb7 roundness %.2f \xb7 r %.2f \xb7 lag %.0f ms",
                           lab.roundness, r, lab.speed > 0 ? 1000 * r / lab.speed : 0 );
            std::snprintf( off, sizeof( off ), "off the classic line %.2f \xb7 hits %d", lab.Off(), lab.hits );
            break;
        }
        default:
            std::snprintf( model, sizeof( model ), "arc = delay \xb7 a turn is a quarter circle %.2f long, radius %.2f",
                           D, D / sim::quarter );
            std::snprintf( off, sizeof( off ), "off the classic line %.2f \xb7 hits %d", lab.Off(), lab.hits );
            break;
        }
        std::snprintf( pace, sizeof( pace ), "speed %.0f \xb7 delay %.2f s \xb7 D %.2f", lab.speed, lab.delay, D );
        std::snprintf( keys, sizeof( keys ), "%s", lab.keys.empty() || now - lab.keysAt > 6 ? "-" : lab.keys.c_str() );
        if ( lab.demo )
            std::snprintf( mode, sizeof( mode ), "auto demo%s%s \xb7 a turn key takes over", lab.showing.empty() ? "" : ": ",
                           lab.showing.c_str() );
        else
            std::snprintf( mode, sizeof( mode ), "by hand%s%s \xb7 0 or space for the demo", lab.showing.empty() ? "" : ": ",
                           lab.showing.c_str() );
        std::snprintf( view, sizeof( view ), "camera %s \xb7 arena %s \xb7 ghost %s \xb7 time %gx",
                       sg_cameraNames[ lab.camera ], lab.dark ? "night" : "pale", lab.ghostShown ? "on" : "off",
                       lab.TimeScale() );
        static char const * const help1 = "your turn keys turn \xb7 1 2 3 4 presets, 5 big circles, shift mirrors \xb7 0 space demo \xb7 [ ] delay \xb7 - = speed \xb7 , . roundness";
        static char const * const help2 = "t model \xb7 g ghost \xb7 c camera \xb7 l arena \xb7 s slow motion \xb7 r reset \xb7 esc back";

        // from the foot up
        static const REAL line = 21;
        REAL const bottom = LogicalHeight() - 30;
        REAL y[9];
        for ( int i = 0; i < 9; ++i )
            y[i] = bottom - ( 8 - i ) * line - ( i >= 7 ? 0 : 8 );

        char const * const lines[6] = { model, pace, off, NULL, mode, view };
        REAL width = std::max( TextWidth( help1, 12, 400 ), TextWidth( help2, 12, 400 ) );
        for ( int i = 0; i < 6; ++i )
            if ( lines[i] )
                width = std::max( width, TextWidth( lines[i], 13, 400 ) );
        width = width / W( 1 ) + 70;
        // the big circles: what they are played in, and the part playing, over the rest
        int const part = lab.BigPart();
        REAL const extra = part >= 0 ? 2 * line : 0;
        FillRect( X( margin - 16 ), Y( y[0] - 20 - extra ), X( margin + width ), Y( bottom + 16 ), canvas, .74f * alpha, 6 );
        if ( part >= 0 )
        {
            static char const * const parts[4] = { "circle", "reverse triple", "regular triple", "circle again" };
            std::string const playing = part == 1 || part == 2 ? std::string( parts[ part ] ) + ": " + lab.BigKeys( part )
                                                           : std::string( parts[ part ] );
            Text( X( margin ), Y( y[0] - 2 * line ), 13, 500, accent, alpha, "big circles: arc = delay; T to change" );
            Text( X( margin ), Y( y[0] - line ), 15, 500, textPrimary, alpha, playing.c_str() );
        }

        Text( X( margin ), Y( y[0] ), 13, 500, textPrimary, alpha, model );
        Text( X( margin ), Y( y[1] ), 13, 400, textSecondary, alpha, pace );
        Text( X( margin ), Y( y[2] ), 13, 400, textSecondary, alpha, off );
        Text( X( margin ), Y( y[3] ), 13, 400, sg_quiet, alpha, "keys" );
        Text( X( margin + 44 ), Y( y[3] ), 15, 500, accent, alpha, keys );
        Text( X( margin ), Y( y[4] ), 13, 400, textSecondary, alpha, mode );
        Text( X( margin ), Y( y[5] ), 13, 400, textSecondary, alpha, view );
        Text( X( margin ), Y( y[7] ), 12, 400, sg_quiet, alpha, help1 );
        Text( X( margin ), Y( y[8] ), 12, 400, sg_quiet, alpha, help2 );
    }

    // ---- the page ----

    class gCurveLabMenu: public uMenu
    {
    public:
        gCurveLabMenu();

    protected:
        // the one row stands in the middle and draws nothing: the lab is the page
        virtual REAL YPos( int num ){ (void)num; return ( menuTop + menuBot ) * .5f; }
        virtual void RenderExtras( REAL alpha );
        virtual void HandleEvent( SDL_Event event );

    private:
        void Adjust( double & value, double step, double low, double high, bool live );

        Lab lab_;
        Path bikePath_, ghostPath_;
        bool held_[ SDLK_LAST ];
    };

    class gCurveLabRow: public uMenuItem
    {
    public:
        explicit gCurveLabRow( uMenu * lab ): uMenuItem( lab, tOutput() ){}
        virtual bool OwnsRow(){ return true; }
        virtual void Render( REAL x, REAL y, REAL alpha, bool selected ){ (void)x; (void)y; (void)alpha; (void)selected; }
        // the lab covers the window: the menus' own backdrop would only be drawn under it
        virtual void RenderBackground(){}
    };

    gCurveLabMenu::gCurveLabMenu()
    : uMenu( tOutput( "$rcl_curve_lab_text" ), false )
    {
        SetStyle( uMenuStyle_RclFull );
        for ( int i = 0; i < SDLK_LAST; ++i )
            held_[i] = false;
        tNEW( gCurveLabRow )( this );
    }

    void gCurveLabMenu::Adjust( double & value, double step, double low, double high, bool live )
    {
        double const next = std::max( low, std::min( high, std::floor( ( value + step ) / std::fabs( step ) + .5 ) * std::fabs( step ) ) );
        if ( std::fabs( next - value ) < 1E-9 )
            return;
        value = next;
        if ( live )
            lab_.Configure();
        uMenu::MenuSound( uMenu::Sound_Adjust );
    }

    // which way the player's own binding for this key or button turns: -1, 1, or 0 when
    // it is bound to something else or nothing
    static int sg_BoundTurn( int sym )
    {
        if ( sym < 0 || sym >= SDLK_NEWLAST )
            return 0;
        uBind * const bind = keymap[ sym ];
        if ( !bind )
            return 0;
        if ( bind->act == &eGameObject::se_turnLeft )
            return -1;
        if ( bind->act == &eGameObject::se_turnRight )
            return 1;
        return 0;
    }

    void gCurveLabMenu::HandleEvent( SDL_Event event )
    {
        // the player's own turn buttons on the mouse turn the bike, as in a game
        if ( event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP )
        {
            int const button = event.button.button;
            int const turn = button >= 1 && button <= MOUSE_BUTTONS ? sg_BoundTurn( SDLK_MOUSE_BUTTON_1 + button - 1 ) : 0;
            if ( turn )
            {
                if ( event.type == SDL_MOUSEBUTTONDOWN )
                    lab_.Turn( turn, tSysTimeFloat() );
                return;
            }
        }
        if ( event.type == SDL_KEYUP )
        {
            if ( event.key.keysym.sym < SDLK_LAST )
                held_[ event.key.keysym.sym ] = false;
        }
        else if ( event.type == SDL_ACTIVEEVENT )
        {
            // a key let go of elsewhere is never heard of: none is held now
            for ( int i = 0; i < SDLK_LAST; ++i )
                held_[i] = false;
        }
        else if ( event.type == SDL_KEYDOWN )
        {
            SDLKey const key = event.key.keysym.sym;
            // the menus send a held key again and again; a turn is one press
            bool const again = key < SDLK_LAST && held_[ key ];
            if ( key < SDLK_LAST )
                held_[ key ] = true;
            bool const shift = ( event.key.keysym.mod & KMOD_SHIFT ) != 0;
            double const now = tSysTimeFloat();

            // the player's own turn keys first, so a bind always turns, whatever the lab uses the key for
            int const bound = sg_BoundTurn( key );
            if ( bound )
            {
                if ( !again )
                    lab_.Turn( bound, now );
                return;
            }

            int preset = -1;
            switch ( key )
            {
            case SDLK_LEFT:
            case SDLK_RIGHT:
                if ( !again )
                    lab_.Turn( key == SDLK_RIGHT ? 1 : -1, now );
                return;
            case SDLK_1: case SDLK_KP1: preset = 0; break;
            case SDLK_2: case SDLK_KP2: preset = 1; break;
            case SDLK_3: case SDLK_KP3: preset = 2; break;
            case SDLK_4: case SDLK_KP4: preset = 3; break;
            case SDLK_5: case SDLK_KP5: preset = 4; break;
            case SDLK_0: case SDLK_KP0: case SDLK_SPACE:
                if ( !again )
                {
                    lab_.StartDemo();
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_LEFTBRACKET:  Adjust( lab_.delay, -.05, .05, 1, true ); return;
            case SDLK_RIGHTBRACKET: Adjust( lab_.delay, .05, .05, 1, true ); return;
            case SDLK_MINUS: case SDLK_KP_MINUS: Adjust( lab_.speed, -5, 10, 80, true ); return;
            case SDLK_EQUALS: case SDLK_KP_PLUS: Adjust( lab_.speed, 5, 10, 80, true ); return;
            case SDLK_COMMA:
            case SDLK_PERIOD:
            {
                double const step = key == SDLK_PERIOD ? .05 : -.05;
                if ( lab_.model == Model_Rounded )
                    Adjust( lab_.roundness, step, 0, .5, true );
                else if ( lab_.model == Model_Look )
                    Adjust( lab_.roundLook, step, 0, .85, false );
                return;
            }
            case SDLK_t:
                if ( !again )
                {
                    // another way of turning starts the run again: one trail, one
                    // rule; the one chosen stays, whatever the demo had borrowed
                    lab_.bigSwitched = false;
                    lab_.model = Model( ( lab_.model + 1 ) % Model_Count );
                    // look only rides the ghost's own line: there it would only double the wall
                    lab_.ghostShown = lab_.model != Model_Look;
                    lab_.Configure();
                    lab_.Restart();
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_g:
                if ( !again )
                {
                    lab_.ghostShown = !lab_.ghostShown;
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_c:
                if ( !again )
                {
                    lab_.NextCamera( now );
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_l:
                if ( !again )
                {
                    lab_.dark = !lab_.dark;
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_s:
                if ( !again )
                {
                    lab_.slow = ( lab_.slow + 1 ) % 3;
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            case SDLK_r:
                if ( !again )
                {
                    lab_.Restart();
                    uMenu::MenuSound( uMenu::Sound_Adjust );
                }
                return;
            // there is nothing to move to or enter
            case SDLK_UP: case SDLK_DOWN: case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_TAB:
                return;
            default:
                break;
            }
            if ( preset >= 0 )
            {
                if ( !again )
                    lab_.Play( preset, shift, now, true );
                return;
            }
        }
        uMenu::HandleEvent( event );
    }

    void gCurveLabMenu::RenderExtras( REAL alpha )
    {
        double const now = tSysTimeFloat();
        lab_.Update( now );
        DrawScene( lab_, now, bikePath_, ghostPath_ );
        DrawHud( lab_, now, alpha );
    }
}

void sg_CurveLab()
{
    gCurveLabMenu lab;
    lab.Enter();
}

bool sg_CurveLabAtStart()
{
    static bool asked = false;
    if ( asked )
        return false;
    asked = true;
    return sg_curveLabStart > 0;
}

#endif
