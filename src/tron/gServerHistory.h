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

#ifndef ArmageTron_SERVER_HISTORY_H
#define ArmageTron_SERVER_HISTORY_H

#include "defs.h"
#include "tString.h"

#include <vector>

//! The servers the player plays on: how long, how often and when, kept in
//! var/server-history.txt on this PC and nowhere else, so the main menu can
//! offer the ones they like best, with who is on them now. The same asking,
//! never waited on, counts who is on the league's servers for the Play page.
namespace gServerHistory
{
    //! what is known about who is on a server right now
    enum Live
    {
        Live_Unknown,   //!< not asked yet, or too long ago
        Live_Silent,    //!< asked, and it did not answer
        Live_Answered
    };

    //! one of the player's servers, as the main menu shows it
    struct Server
    {
        tString host;           //!< what it is connected by
        unsigned int port;
        tString name;           //!< what it calls itself, colour codes kept; empty when not known
        double seconds;         //!< played there in all
        int visits;             //!< sessions of half a minute or more
        long long last;         //!< unix time the last session there ended
        int live;               //!< a Live
        int users, maxUsers;    //!< when live is Live_Answered
        double liveWhen;        //!< tSysTimeFloat() of that answer

        Server()
        : port( 0 ), seconds( 0 ), visits( 0 ), last( 0 ), live( Live_Unknown ),
          users( 0 ), maxUsers( 0 ), liveWhen( -1E10 )
        {
        }

        //! whether the answer is recent enough to show
        bool Fresh() const;
    };

    //! A session on a server ended after the given seconds. name is what the
    //! connection called the server; address its number and port
    //! ("192.0.2.7:4534"), empty if not known. Does nothing while the
    //! history is switched off.
    void Played( tString const & host, unsigned int port, tString const & name,
                 tString const & address, double seconds );

    //! What host:port is called, for the next session there to keep when the
    //! connection itself knows the server only by its address (Play Now asked
    //! it for its name before joining).
    void NameHint( tString const & host, unsigned int port, tString const & name );

    //! The quick-join rows: the player's favourite servers, at most three,
    //! those with players first. None while the history is switched off.
    std::vector< Server > Favourites();

    //! counts up whenever what Favourites() or Counted() gives may have changed
    int Generation();

    // ---- who is on: the main menu's favourites, the Play page's fleet ----

    //! a server to ask who is on, by what it is connected by
    struct Target
    {
        tString host;
        unsigned int port;

        Target(): port( 0 ){}
        Target( tString const & h, unsigned int p ): host( h ), port( p ){}
    };

    //! what one server said when it was last asked
    struct Heard
    {
        int live;               //!< a Live
        int users, maxUsers;    //!< when live is Live_Answered
        tString name;           //!< what it calls itself, colour codes kept
        tString address;        //!< the number and port it was asked at: two names for one server have one
        double when;            //!< tSysTimeFloat() of the answer, or of the silence

        Heard(): live( Live_Unknown ), users( 0 ), maxUsers( 0 ), when( -1E10 ){}

        //! whether it is recent enough to show
        bool Fresh() const;
    };

    //! the pages that ask; one at a time has the asking
    enum Asker
    {
        Asker_MainMenu,
        Asker_PlayPage,
        Asker_Count
    };

    //! Each frame of a page that shows who is on: now and then asks the
    //! player's favourites (while the history is kept) and the page's own
    //! servers who is on. Never waits: names are looked up on a thread and
    //! answers are read as they come in. When another page asked last, what
    //! it had out is given up first.
    void CountFrame( int asker, std::vector< Target > const & others );

    //! The page is about to hand over to something else (a row was chosen):
    //! what was being asked is given up and the shared server list left
    //! empty, for the browser and Play Now to start theirs in.
    void CountLeaves();

    //! what was last heard from host:port, one of the servers a page asked about
    Heard Counted( tString const & host, unsigned int port );

    //! Each frame of the main menu, out of a game: now and then asks the
    //! favourites who is on. Never waits. (CountFrame for the main menu.)
    void MainMenuFrame();

    //! the main menu is about to hand over (CountLeaves)
    void MainMenuLeaves();

    //! How much host:port counts as one of the player's own: their time there,
    //! weighted to the last weeks. Negative when it is not in the history.
    double Score( tString const & host, unsigned int port );

    //! whether the history is kept at all (SERVER_HISTORY)
    bool On();

    //! forgets every server and removes the file
    void Clear();
}

#endif
