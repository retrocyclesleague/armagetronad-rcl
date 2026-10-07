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

#ifndef ArmageTron_RCL_SCOREBOARD_H
#define ArmageTron_RCL_SCOREBOARD_H

#include "tString.h"

//! The scoreboard as a panel in the RCL interface language, in place of the
//! table of fixed-width text: where the game is played, then the teams with
//! their scores, then everyone in them.
//!
//! It shows what a client knows: names, colours, teams, scores, pings, who is
//! alive, who is typing and who only watches, the server's name and the map.
//! A server sends no kills or deaths and neither its limits nor the round's
//! number of its own accord. What a server wants shown beyond the scores it
//! says through eRclBoard: a title, facts, a note, columns of its own.
namespace eRclScoreboard
{
    //! RCL_SCOREBOARD: off brings the text table back
    extern bool enabled;

    //! Where the game is played: the server's name (empty for a local game)
    //! and the map's resource path. Cheap to call every frame.
    void SetContext( tString const & server, tString const & mapfile );

    //! Draws the board over the frame. False if it is switched off and the
    //! caller should draw the text table instead.
    bool Render();
}

#endif
