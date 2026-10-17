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

#ifndef ArmageTron_LEAGUE_H
#define ArmageTron_LEAGUE_H

#include "defs.h"

//! The league on the main menu: its next event and how the last one ended,
//! its ladders and where the player stands on them, read from the league's
//! site and kept on disk so the menu has them at once on the next start.
namespace gLeague
{
    //! Each frame of the main menu: asks the site again when what is known is
    //! old, and takes an answer that has come in. Never waits.
    void Poll();

    //! the right side of the main menu, beside the rail
    void DrawHome( REAL alpha );

    //! Left and right pick the ladder shown; true when the key was used.
    bool HomeKey( int key );

    //! A press of the left mouse button at x, y (field coordinates); true
    //! when it was on something of the league's side.
    bool HomeClick( REAL x, REAL y );

    //! the league's pages, in the system's browser
    void OpenLeaderboard();
    void OpenEvents();
}

#endif
