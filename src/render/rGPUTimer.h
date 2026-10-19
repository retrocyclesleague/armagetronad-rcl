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

#ifndef ArmageTron_GPUTIMER_H
#define ArmageTron_GPUTIMER_H

#include "config.h"

#ifndef DEDICATED

// How long the GPU spends on each pass of a frame, from timer queries. Only
// one such query can run at a time, so a pass begun inside another stops it
// and it goes on when the inner one ends (the cycles inside the world): each
// pass counts only its own time. Whatever is drawn outside the named passes
// (the clear, HUD, console, menus) is counted as "other", so the passes add
// up to the frame.
// The answers come a few frames late; nothing ever waits for them. Queries
// are only made while someone looks: the stats overlay (RCL_RENDER_STATS 1)
// or the frame log (RCL_FRAME_LOG).
namespace rGPUTimer
{
    enum Pass
    {
        Pass_World = 0,     //!< the 3D scenes: arena, walls, zones, mirror, shadows; the rest of the cycles
        Pass_Cycles,        //!< the light cycle meshes (RCL_CYCLE_MODEL 2), inside the world
        Pass_Resolve,       //!< multisampled scene to a single sample
        Pass_Bloom,         //!< the glow, down and up
        Pass_Tonemap,       //!< the scene onto the screen
        Pass_Other,         //!< everything else up to the swap
        Pass_Count
    };

    char const * Name( int pass );

    //! whether queries are made this frame
    bool Enabled();
    //! whether the stats overlay is asked for (RCL_RENDER_STATS)
    bool OverlayWanted();
    //! where the overlay's first line, what the renderer is doing, comes from
    void SetStatus( char const * ( *status )() );

    void FrameBegin();      //!< after the swap
    void FrameEnd();        //!< before the swap
    void Begin( Pass pass );
    void End( Pass pass );

    //! milliseconds, averaged over the last second or so; negative if nothing measured yet
    double Average( int pass );
    //! milliseconds of the newest frame whose answers are in; negative if none
    double Last( int pass );
    //! how many frames late Last() is
    int Lag();

    //! deletes the queries; the context must still be there
    void Release();
    //! drops them without deleting: the context is gone
    void Forget();
}

#endif // DEDICATED

#endif
