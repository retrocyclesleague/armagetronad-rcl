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

#ifndef ArmageTron_RCL_BOARD_H
#define ArmageTron_RCL_BOARD_H

#include "tString.h"

#include <vector>

class ePlayerNetID;

//! What a server tells the scoreboard beyond the scores: a title, facts about
//! the match, a note, and columns of its own with a value in each for every
//! player. docs/SCOREBOARD_DATA.md is the description for whoever writes a
//! server's settings or the script that runs its game mode.
//!
//! All of it travels in one kind of message of its own, a numbered line of
//! text: a server sends a line to every client when it has changed, a client
//! that knows the message shows what it says, and a client that does not
//! drops it without a word, as this game does with any message it does not know.
//!
//! This file and its .cpp use nothing of the client's interface, so a server
//! built from another line of the source can take the pair as it is.
namespace eRclBoard
{
    tString const & Title();    //!< RCL_BOARD_TITLE: what is played, in the server's words
    tString const & Info();     //!< RCL_BOARD_INFO: facts about the match, separated by commas
    tString const & Note();     //!< RCL_BOARD_NOTE: one line under the list
    tString const & Columns();  //!< RCL_BOARD_COLUMNS: the server's own columns, separated by commas

    //! the items of a list separated by commas, without the space around them
    void Split( tString const & list, std::vector< tString > & items );

    //! The server's values for a player, one per column, as last set with
    //! RCL_BOARD_PLAYER. False if it has none.
    bool PlayerValues( ePlayerNetID const * player, std::vector< tString > & values );

    //! a player is gone: a server drops what it kept for them
    void Forget( ePlayerNetID const * player );
}

#endif
