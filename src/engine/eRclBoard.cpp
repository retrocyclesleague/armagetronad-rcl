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

#include "eRclBoard.h"

#include "ePlayer.h"
#include "nNetObject.h"
#include "nNetwork.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tToDo.h"

#include <cstdlib>
#include <map>
#include <string>

namespace eRclBoard
{
    namespace
    {
        // A line travels whole in one message; this keeps it well inside
        // what a message may carry.
        size_t const lineLimit  = 220;
        size_t const valueLimit = 24;   // one value of one player

        // The board's lines. The first four are set as they are; the rest
        // are written here from the players' values, "<network ID>=<value>,
        // <value>;" for each player that has any, over as many as it takes.
        enum { Line_Title, Line_Info, Line_Note, Line_Columns, Line_Rows, lines = Line_Rows + 8 };
        tString text_[lines];

        // how often anything here changed, for whoever keeps a reading of it
        unsigned int revision_ = 0;

        // one line, as it may be sent and shown: no control characters, not too long
        std::string Clean( char const * text, size_t limit )
        {
            std::string out;
            for ( ; *text != '\0' && out.size() < limit; ++text )
                if ( static_cast< unsigned char >( *text ) >= ' ' )
                    out += *text;
            std::string::size_type const first = out.find_first_not_of( ' ' );
            if ( first == std::string::npos )
                return std::string();
            return out.substr( first, out.find_last_not_of( ' ' ) - first + 1 );
        }

        bool Take( int line, char const * text )
        {
            tString const clean( Clean( text, lineLimit ).c_str() );
            if ( clean == text_[line] )
                return false;
            text_[line] = clean;
            ++revision_;
            return true;
        }

        // ---- from the server to its clients ----
        // A message of its own kind: the line's number and its text. A client
        // that does not know the kind drops it without a word, which a
        // setting it does not know would not let it do. The server sends a
        // line when it has changed, and all that are not empty to a client
        // that has just come in.
        void Receive( nMessage & m );
        nDescriptor lineDescriptor_( 270, Receive, "RCL board line" );

        tString sent_[lines];       // what the clients have, on the server
        bool fromServer_ = false;   // on a client: some of the text came over the net

        nMessage * LineMessage( int line )
        {
            nMessage * m = new nMessage( lineDescriptor_ );
            m->Write( static_cast< unsigned short >( line ) );
            *m << text_[line];
            return m;
        }

        void Receive( nMessage & m )
        {
            // only a client listens, and only to its server
            if ( sn_GetNetState() != nCLIENT || m.SenderID() != 0 )
                return;
            unsigned short line;
            m.Read( line );
            tString text;
            m >> text;
            if ( line >= lines )
                return;
            fromServer_ = true;
            Take( line, text );
        }

        void LoginLogout()
        {
            int const user = nCallbackLoginLogout::User();
            if ( sn_GetNetState() == nSERVER )
            {
                if ( nCallbackLoginLogout::Login() && user > 0 && user <= MAXCLIENTS )
                    for ( int i = 0; i < lines; ++i )
                        if ( sent_[i].Len() > 1 )
                            LineMessage( i )->Send( user );
            }
            else if ( user == 0 && !nCallbackLoginLogout::Login() &&
                      ( fromServer_ || sn_GetNetState() == nCLIENT ) )
            {
                // what one server said is not carried to the next
                for ( int i = 0; i < lines; ++i )
                    Take( i, "" );
                fromServer_ = false;
            }
        }
        nCallbackLoginLogout loginLogout_( &LoginLogout );

        // ---- on the server (and in a local game): what was set for whom ----
        // By player, not by ID: an ID is given out again after its player has
        // gone, and in a local game nobody has one. Forget() keeps this free
        // of players that no longer exist.
        typedef std::map< ePlayerNetID const *, std::string > Rows;
        Rows rows_;
        bool rowsChanged_ = false;

        // puts the players' values into their lines and sends what changed
        void Flush()
        {
            if ( rowsChanged_ )
            {
                rowsChanged_ = false;
                std::string text[lines];
                int line = Line_Rows;
                bool full = false;
                for ( Rows::const_iterator i = rows_.begin(); i != rows_.end(); ++i )
                {
                    if ( i->first->ID() == 0 )
                        continue;   // not known to any client yet
                    tString id;
                    id << i->first->ID();
                    std::string const entry = std::string( id ) + "=" + i->second + ";";
                    if ( text[line].size() + entry.size() > lineLimit && line + 1 < lines )
                        ++line;
                    if ( text[line].size() + entry.size() <= lineLimit )
                        text[line] += entry;
                    else
                        full = true;
                }
                if ( full )
                    con << "RCL_BOARD_PLAYER: more values than the scoreboard has room for; some players are left out.\n";
                for ( int i = Line_Rows; i < lines; ++i )
                    Take( i, text[i].c_str() );
            }

            if ( sn_GetNetState() != nSERVER )
                return;
            for ( int i = 0; i < lines; ++i )
                if ( sent_[i] != text_[i] )
                {
                    sent_[i] = text_[i];
                    LineMessage( i )->BroadCast();
                }
        }

        // A client's board is its server's to write.
        bool MaySet()
        {
            return sn_GetNetState() != nCLIENT;
        }

        void SetLine( int line, std::istream & s )
        {
            tString rest;
            rest.ReadLine( s, true );
            if ( MaySet() && Take( line, rest ) )
                st_ToDoOnce( &Flush );  // once, after everything said in one go has been read
        }

        void SetTitle( std::istream & s )   { SetLine( Line_Title, s ); }
        void SetInfo( std::istream & s )    { SetLine( Line_Info, s ); }
        void SetNote( std::istream & s )    { SetLine( Line_Note, s ); }
        void SetColumns( std::istream & s ) { SetLine( Line_Columns, s ); }
        tConfItemFunc titleConf_( "RCL_BOARD_TITLE", &SetTitle );
        tConfItemFunc infoConf_( "RCL_BOARD_INFO", &SetInfo );
        tConfItemFunc noteConf_( "RCL_BOARD_NOTE", &SetNote );
        tConfItemFunc columnsConf_( "RCL_BOARD_COLUMNS", &SetColumns );

        // RCL_BOARD_PLAYER <player> <value>, <value>, ...
        void SetPlayer( std::istream & s )
        {
            if ( !MaySet() )
                return;

            tString name;
            s >> name;
            tString rest;
            rest.ReadLine( s );
            if ( name.Len() <= 1 )
            {
                con << "Usage: RCL_BOARD_PLAYER <player> <value>, <value>, ...\n";
                return;
            }

            // says what is wrong itself if it finds nobody, or more than one
            ePlayerNetID const * player = ePlayerNetID::FindPlayerByName( name, 0 );
            if ( !player )
                return;

            // as the clients will read it: values separated by commas alone
            std::vector< tString > items;
            Split( rest, items );
            std::string values;
            for ( size_t i = 0; i < items.size(); ++i )
            {
                std::string value = Clean( items[i], valueLimit );
                for ( size_t c = 0; c < value.size(); ++c )
                    if ( value[c] == ';' || value[c] == '=' )
                        value[c] = ' ';
                if ( i )
                    values += ",";
                values += value;
            }

            Rows::iterator const found = rows_.find( player );
            if ( found != rows_.end() && found->second == values )
                return;
            rows_[ player ] = values;
            ++revision_;
            rowsChanged_ = true;
            st_ToDoOnce( &Flush );
        }
        tConfItemFunc setPlayerConf_( "RCL_BOARD_PLAYER", &SetPlayer );

        // RCL_BOARD_CLEAR: nobody has values any more
        void Clear( std::istream & )
        {
            if ( !MaySet() || rows_.empty() )
                return;
            rows_.clear();
            ++revision_;
            rowsChanged_ = true;
            st_ToDoOnce( &Flush );
        }
        tConfItemFunc clearConf_( "RCL_BOARD_CLEAR", &Clear );

        // ---- on a client: the players' lines, read back ----
        std::map< unsigned short, std::vector< tString > > read_;
        unsigned int readRevision_ = ~0u;

        void Read()
        {
            if ( readRevision_ == revision_ )
                return;
            readRevision_ = revision_;
            read_.clear();
            for ( int i = Line_Rows; i < lines; ++i )
            {
                std::string const text( static_cast< char const * >( text_[i] ) );
                std::string::size_type at = 0;
                while ( at < text.size() )
                {
                    std::string::size_type end = text.find( ';', at );
                    if ( end == std::string::npos )
                        end = text.size();
                    std::string::size_type const equals = text.find( '=', at );
                    if ( equals != std::string::npos && equals < end )
                    {
                        int const id = atoi( text.substr( at, equals - at ).c_str() );
                        if ( id > 0 && id < 0x10000 )
                            Split( tString( text.substr( equals + 1, end - equals - 1 ).c_str() ),
                                   read_[ static_cast< unsigned short >( id ) ] );
                    }
                    at = end + 1;
                }
            }
        }
    }

    tString const & Title()   { return text_[Line_Title]; }
    tString const & Info()    { return text_[Line_Info]; }
    tString const & Note()    { return text_[Line_Note]; }
    tString const & Columns() { return text_[Line_Columns]; }

    void Split( tString const & list, std::vector< tString > & items )
    {
        items.clear();
        std::string const text( static_cast< char const * >( list ) );
        if ( text.find_first_not_of( ' ' ) == std::string::npos )
            return;
        std::string::size_type at = 0;
        for ( ;; )
        {
            std::string::size_type const end = text.find( ',', at );
            std::string const item = text.substr( at, end == std::string::npos ? end : end - at );
            items.push_back( tString( Clean( item.c_str(), lineLimit ).c_str() ) );
            if ( end == std::string::npos )
                break;
            at = end + 1;
        }
    }

    bool PlayerValues( ePlayerNetID const * player, std::vector< tString > & values )
    {
        if ( sn_GetNetState() != nCLIENT )
        {
            // here is where they were set
            Rows::const_iterator const row = rows_.find( player );
            if ( row == rows_.end() )
                return false;
            Split( tString( row->second.c_str() ), values );
            return true;
        }

        Read();
        std::map< unsigned short, std::vector< tString > >::const_iterator const found =
            read_.find( player->ID() );
        if ( found == read_.end() )
            return false;
        values = found->second;
        return true;
    }

    void Forget( ePlayerNetID const * player )
    {
        if ( rows_.erase( player ) )
        {
            ++revision_;
            rowsChanged_ = true;
            st_ToDoOnce( &Flush );
        }
    }
}
