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

//! The league in the menus: its next event and the ones before, its ladders
//! and where the player stands on them, the player's level, read from the
//! league's site and kept on disk so the menus have them at once on the next
//! start.
namespace gLeague
{
    //! Each frame of a menu that shows the league: asks the site again when
    //! what is known is old, and takes an answer that has come in. Never waits.
    void Poll();

    //! the main menu's right side: the next event, the player's level when
    //! the site has one for them, and their four ladders
    void DrawHome( REAL alpha );

    //! A press of the left mouse button at x, y (field coordinates) on the
    //! main menu; true when it was on something of the league's side.
    bool HomeClick( REAL x, REAL y );

    //! the pages: every ladder as a table, and the events
    void LeaderboardMenu();
    void EventsMenu();
}

#endif
