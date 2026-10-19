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

#include "rGLExt.h"
#include "rGPUTimer.h"

#ifndef DEDICATED

#include "rFont.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tSysTime.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // a frame's answers are read this many frames later at the earliest;
    // when the GPU is further behind than that, frames go unmeasured
    int const sr_frames = 4;

    struct gMark
    {
        int pass;
        GLuint query;
    };

    struct gFrame
    {
        std::vector< gMark > marks;
        bool closed;
        int serial;
        gFrame(): closed( false ), serial( 0 ){}
    };

    gFrame sr_frame[ sr_frames ];
    int sr_current = -1;            // the frame being measured; -1 none
    int sr_next = 0;                // where the next one goes
    int sr_serial = 0;
    int sr_active = -1;             // the pass whose query runs; -1 none
    std::vector< GLuint > sr_free;  // queries not in use

    double sr_last[ rGPUTimer::Pass_Count ];
    double sr_sum[ rGPUTimer::Pass_Count ];
    double sr_average[ rGPUTimer::Pass_Count ];
    int sr_counted = 0;
    double sr_countedSince = 0;
    int sr_lastSerial = -1;
    bool sr_haveLast = false, sr_haveAverage = false;

    bool sr_FrameLogOn()
    {
        static int on = -1;
        if ( on < 0 )
        {
            char const * const name = getenv( "RCL_FRAME_LOG" );
            on = ( name && *name ) ? 1 : 0;
        }
        return on > 0;
    }

#ifdef RGL_EXT_AVAILABLE
    GLuint sr_Query()
    {
        if ( sr_free.empty() )
        {
            GLuint ids[16];
            glGenQueries( 16, ids );
            for ( int i = 15; i >= 0; --i )
                if ( ids[i] )
                    sr_free.push_back( ids[i] );
            if ( sr_free.empty() )
                return 0;
        }
        GLuint const query = sr_free.back();
        sr_free.pop_back();
        return query;
    }

    void sr_Start( int pass )
    {
        if ( sr_current < 0 )
            return;
        GLuint const query = sr_Query();
        if ( !query )
            return;
        glBeginQuery( RGL_TIME_ELAPSED, query );
        gMark const mark = { pass, query };
        sr_frame[ sr_current ].marks.push_back( mark );
        sr_active = pass;
    }

    void sr_Stop()
    {
        if ( sr_active < 0 )
            return;
        glEndQuery( RGL_TIME_ELAPSED );
        sr_active = -1;
    }

    // Reads the answers of closed frames, oldest first, as far as they are in.
    // Queries finish in order, so a frame's last one says for all of it.
    void sr_Collect()
    {
        for ( ;; )
        {
            int oldest = -1;
            for ( int i = 0; i < sr_frames; ++i )
                if ( sr_frame[i].closed && ( oldest < 0 || sr_frame[i].serial < sr_frame[ oldest ].serial ) )
                    oldest = i;
            if ( oldest < 0 )
                return;

            gFrame & frame = sr_frame[ oldest ];
            if ( !frame.marks.empty() )
            {
                GLint available = 0;
                glGetQueryObjectiv( frame.marks.back().query, RGL_QUERY_RESULT_AVAILABLE, &available );
                if ( !available )
                    return;
            }

            double ms[ rGPUTimer::Pass_Count ];
            for ( int p = 0; p < rGPUTimer::Pass_Count; ++p )
                ms[p] = 0;
            for ( size_t m = 0; m < frame.marks.size(); ++m )
            {
                ms[ frame.marks[m].pass ] += static_cast< double >( rGLExt::QueryTime( frame.marks[m].query ) ) * 1E-6;
                sr_free.push_back( frame.marks[m].query );
            }
            frame.marks.clear();
            frame.closed = false;

            for ( int p = 0; p < rGPUTimer::Pass_Count; ++p )
            {
                sr_last[p] = ms[p];
                sr_sum[p] += ms[p];
            }
            sr_haveLast = true;
            sr_lastSerial = frame.serial;
            ++sr_counted;

            // the averages are published twice a second, so they can be read
            double const now = tRealSysTimeFloat();
            if ( now - sr_countedSince > .5 && sr_counted > 0 )
            {
                for ( int p = 0; p < rGPUTimer::Pass_Count; ++p )
                {
                    sr_average[p] = sr_sum[p] / sr_counted;
                    sr_sum[p] = 0;
                }
                sr_counted = 0;
                sr_countedSince = now;
                sr_haveAverage = true;
            }
        }
    }
#endif

    // the queries belong to the context, which may go with the screen mode
    void sr_GPUTimerRelease()
    {
        rGPUTimer::Release();
    }
    rCallbackBeforeScreenModeChange sr_gpuTimerRelease( &sr_GPUTimerRelease );
}

// RCL_RENDER_STATS 1: the overlay with the GPU time of every pass (drawn by
// rPost, which knows what the renderer is doing as well)
static int sr_renderStats = 0;
static tSettingItem< int > sr_renderStatsConf( "RCL_RENDER_STATS", sr_renderStats );

char const * rGPUTimer::Name( int pass )
{
    static char const * const names[ Pass_Count ] = { "world", "resolve", "bloom", "tonemap", "other" };
    return pass >= 0 && pass < Pass_Count ? names[ pass ] : "?";
}

bool rGPUTimer::Enabled()
{
    return rGLExt::TimerQueries() && ( sr_renderStats > 0 || sr_FrameLogOn() );
}

bool rGPUTimer::OverlayWanted()
{
    return sr_renderStats > 0;
}

static char const * ( *sr_renderStatus )() = 0;

void rGPUTimer::SetStatus( char const * ( *status )() )
{
    sr_renderStatus = status;
}

// The overlay, bottom left: what the renderer is doing, the GPU's
// milliseconds per pass and their sum, averaged twice a second, and the frame
// time the CPU sees, so it shows which of the two holds a frame up.
static void sr_RenderStats()
{
    if ( sr_renderStats <= 0 || !sr_glOut )
        return;

    static double last = 0, sum = 0, since = 0, frameMs = -1;
    static int frames = 0;
    double const now = tRealSysTimeFloat();
    if ( last > 0 && now > last )
    {
        sum += now - last;
        ++frames;
    }
    last = now;
    if ( now - since > .5 && frames > 0 )
    {
        frameMs = sum * 1000 / frames;
        sum = 0;
        frames = 0;
        since = now;
    }

    std::ostringstream gpu;
    gpu.setf( std::ios::fixed );
    gpu.precision( 3 );
    if ( !rGLExt::TimerQueries() )
        gpu << "gpu ms: no timer queries on this driver";
    else if ( rGPUTimer::Average( 0 ) < 0 )
        gpu << "gpu ms: measuring";
    else
    {
        double total = 0;
        gpu << "gpu ms:";
        for ( int p = 0; p < rGPUTimer::Pass_Count; ++p )
        {
            double const ms = rGPUTimer::Average( p );
            total += ms;
            gpu << "  " << rGPUTimer::Name( p ) << ' ' << ms;
        }
        gpu << "  = " << total;
    }

    std::ostringstream cpu;
    cpu.setf( std::ios::fixed );
    cpu.precision( 2 );
    cpu << "frame " << frameMs << " ms";
    if ( frameMs > 0 )
        cpu << " (" << static_cast< int >( 1000 / frameMs + .5 ) << " fps)";
    cpu << ", " << rGLExt::Describe() << ", " << static_cast< char const * >( gl_renderer );

    std::string const status = sr_renderStatus ? sr_renderStatus() : "classic renderer";

    sr_ResetRenderState( true );
    REAL const size = .15f;
    REAL const w = .15f * size * rTextField::AspectWidthMultiplier(), h = .3f * size;
    rTextField text( -.97f, -.52f, w, h );
    text << "0xffffff" << status.c_str() << "\n";
    text << "0xffffff" << gpu.str().c_str() << "\n";
    text << "0xffffff" << cpu.str().c_str() << "\n";
}
static rPerFrameTask sr_renderStatsTask( &sr_RenderStats );

void rGPUTimer::FrameBegin()
{
#ifdef RGL_EXT_AVAILABLE
    if ( sr_current >= 0 )
        FrameEnd();
    sr_Stop();
    sr_current = -1;
    if ( !Enabled() )
        return;

    // the slot must have been read; a GPU that far behind goes unmeasured for now
    sr_Collect();
    gFrame & frame = sr_frame[ sr_next ];
    if ( frame.closed )
        return;

    frame.marks.clear();
    frame.serial = ++sr_serial;
    sr_current = sr_next;
    sr_next = ( sr_next + 1 ) % sr_frames;
    sr_Start( Pass_Other );
#endif
}

void rGPUTimer::FrameEnd()
{
#ifdef RGL_EXT_AVAILABLE
    if ( sr_current < 0 )
        return;
    sr_Stop();
    sr_frame[ sr_current ].closed = true;
    sr_current = -1;
    sr_Collect();
#endif
}

void rGPUTimer::Begin( Pass pass )
{
#ifdef RGL_EXT_AVAILABLE
    if ( sr_current < 0 || sr_active == pass )
        return;
    sr_Stop();
    sr_Start( pass );
#else
    (void)pass;
#endif
}

void rGPUTimer::End( Pass pass )
{
#ifdef RGL_EXT_AVAILABLE
    if ( sr_current < 0 || sr_active != pass )
        return;
    sr_Stop();
    sr_Start( Pass_Other );
#else
    (void)pass;
#endif
}

double rGPUTimer::Average( int pass )
{
    return sr_haveAverage && pass >= 0 && pass < Pass_Count ? sr_average[ pass ] : -1;
}

double rGPUTimer::Last( int pass )
{
    return sr_haveLast && pass >= 0 && pass < Pass_Count ? sr_last[ pass ] : -1;
}

int rGPUTimer::Lag()
{
    return sr_haveLast ? sr_serial - sr_lastSerial : 0;
}

void rGPUTimer::Release()
{
#ifdef RGL_EXT_AVAILABLE
    if ( rGLExt::TimerQueries() )
    {
        sr_Stop();
        for ( int i = 0; i < sr_frames; ++i )
            for ( size_t m = 0; m < sr_frame[i].marks.size(); ++m )
                sr_free.push_back( sr_frame[i].marks[m].query );
        if ( !sr_free.empty() )
            glDeleteQueries( static_cast< GLsizei >( sr_free.size() ), &sr_free[0] );
    }
#endif
    Forget();
}

void rGPUTimer::Forget()
{
    for ( int i = 0; i < sr_frames; ++i )
    {
        sr_frame[i].marks.clear();
        sr_frame[i].closed = false;
    }
    sr_free.clear();
    sr_current = -1;
    sr_active = -1;
    sr_next = 0;
}

#endif // DEDICATED
