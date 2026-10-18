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

#include "gServerHistory.h"

#ifndef DEDICATED

#include "nNetwork.h"
#include "nServerInfo.h"
#include "rConsole.h"
#include "rSDL.h"
#include "tConfiguration.h"
#include "tDirectories.h"
#include "tHttp.h"
#include "tRecorder.h"
#include "tSysTime.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Which servers the player plays on. The history is this PC's: it is kept in
// var/server-history.txt and nothing of it is sent anywhere. The only traffic
// it causes is the usual "who is on?" question the server browser asks, put
// to the player's three favourite servers now and then while the main menu
// or the Play page is open, and never while the history is off or empty.
// The Play page also has the league's own servers asked the same question,
// in every region; that is the page's, history or not.
//
// Everything here runs on the main thread except the address lookups, which
// get a thread of their own so that a slow name server cannot stall a frame.
// That thread is handed host names and gives back numbers; nothing else
// crosses over.

namespace
{
    bool sg_historyOn = true;
    tConfItem< bool > sg_historyOnConf( "SERVER_HISTORY", sg_historyOn );

    void sg_ClearConf( std::istream & )
    {
        gServerHistory::Clear();
    }
    tConfItemFunc sg_clearConf( "SERVER_HISTORY_CLEAR", &sg_ClearConf );

    char const * const sg_fileName = "server-history.txt";
    char const * const sg_fileTag = "rcl-server-history\t1";

    size_t const sg_most = 50;              // servers kept; the least played go first
    size_t const sg_nameMost = 480;         // bytes of a server's name kept, colour codes and all
    double const sg_visitSeconds = 30;      // a shorter session adds its time, not a visit
    double const sg_halfLifeDays = 21;      // play this long ago counts half
    size_t const sg_rows = 3;               // quick-join rows, and servers asked
    size_t const sg_lookedUp = 6;           // candidates looked up, so that two names for one server are found

    double const sg_askEvery = 45;          // seconds between askings while the main menu is open
    double const sg_askEveryPage = 30;      // and while the Play page is
    double const sg_askBudget = 3;          // an asking gives up on those that have not answered by then
    double const sg_askBudgetMany = 4;      // one of the Play page's many servers
    int const sg_simultaneous = 4;          // questions out at once
    int const sg_simultaneousMany = 12;     // for the Play page, as Play Now counts
    double const sg_settle = .5;            // late answers are still taken for this long
    double const sg_backAfter = 1.5;        // after the main menu comes back, the next asking is this soon
    double const sg_staleAfter = 15;        // unless the last one is younger than this
    double const sg_freshFor = 150;         // counts older than this are not shown
    double const sg_awayGap = 1;            // no main menu frame for this long: it was not on screen

    // ---- the history ----

    struct Entry
    {
        std::string host;           // lower case
        unsigned int port;
        double seconds;
        int visits;
        long long last;
        std::string name;           // colour codes kept; empty when not known

        // learnt while the client runs, never written
        std::string address;        // "192.0.2.7:4534" once looked up
        int live;
        int users, maxUsers;
        double liveWhen;

        Entry()
        : port( 0 ), seconds( 0 ), visits( 0 ), last( 0 ),
          live( gServerHistory::Live_Unknown ), users( 0 ), maxUsers( 0 ), liveWhen( -1E10 )
        {
        }
    };

    std::vector< Entry > sg_entries;
    bool sg_loaded = false;
    int sg_generation = 0;

    // what Play Now heard the server it is joining called
    struct Hint
    {
        std::string host, name;
        unsigned int port;
        Hint(): port( 0 ){}
    };
    Hint sg_hint;

    std::string Std( tString const & text )
    {
        return std::string( static_cast< char const * >( text ) );
    }

    std::string PortText( unsigned int port )
    {
        char text[ 16 ];
        snprintf( text, sizeof( text ), "%u", port );
        return text;
    }

    // A host as a connection names it: letters, digits, dots, dashes and
    // underscores, in lower case. Anything else is no host.
    std::string CleanHost( std::string const & host )
    {
        std::string out;
        for ( size_t i = 0; i < host.size(); ++i )
        {
            unsigned char const c = host[i];
            if ( !isalnum( c ) && c != '.' && c != '-' && c != '_' )
                return std::string();
            out += static_cast< char >( tolower( c ) );
        }
        return out.size() <= 253 ? out : std::string();
    }

    bool Numeric( std::string const & host )
    {
        for ( size_t i = 0; i < host.size(); ++i )
            if ( !isdigit( static_cast< unsigned char >( host[i] ) ) && host[i] != '.' )
                return false;
        return !host.empty();
    }

    // A name as the file can hold it: no tabs, line ends or other control
    // characters, at most sg_nameMost bytes, and never cut in a colour code.
    std::string CleanName( std::string const & name )
    {
        // a colour code is taken whole or not at all, so the cut can never
        // leave half of one (servers often colour every letter)
        std::string out;
        for ( size_t i = 0; i < name.size(); )
        {
            size_t step = 1;
            if ( name[i] == '0' && i + 1 < name.size() && name[i + 1] == 'x' )
                step = std::min( size_t( 8 ), name.size() - i );
            if ( out.size() + step > sg_nameMost )
                break;
            for ( size_t k = 0; k < step; ++k )
            {
                unsigned char const c = name[i + k];
                out += ( c < 0x20 || c == 0x7f ) ? ' ' : static_cast< char >( c );
            }
            i += step;
        }
        while ( !out.empty() && out[ out.size() - 1 ] == ' ' )
            out.erase( out.size() - 1 );
        while ( !out.empty() && out[0] == ' ' )
            out.erase( 0, 1 );
        return out;
    }

    // whether a name says more than where the server is: a connection that
    // knows nothing better gives its address as the name
    bool RealName( std::string const & name, Entry const & entry )
    {
        std::string plain = Std( tColoredString::RemoveColors( name.c_str(), false ) );
        for ( size_t i = 0; i < plain.size(); ++i )
            plain[i] = static_cast< char >( tolower( static_cast< unsigned char >( plain[i] ) ) );
        while ( !plain.empty() && plain[ plain.size() - 1 ] == ' ' )
            plain.erase( plain.size() - 1 );
        if ( plain.empty() || plain == entry.host || plain == entry.host + ":" + PortText( entry.port ) )
            return false;
        for ( size_t i = 0; i < plain.size(); ++i )
            if ( !isdigit( static_cast< unsigned char >( plain[i] ) ) && plain[i] != '.' && plain[i] != ':' )
                return true;
        return false;
    }

    // the player's time there, the last three weeks counting double the three before
    double Points( Entry const & entry, long long now )
    {
        double const days = std::max( 0.0, double( now - entry.last ) / 86400 );
        return ( entry.seconds + entry.visits * 60.0 ) * pow( .5, days / sg_halfLifeDays );
    }

    Entry * Find( std::string const & host, unsigned int port )
    {
        for ( size_t i = 0; i < sg_entries.size(); ++i )
            if ( sg_entries[i].port == port && sg_entries[i].host == host )
                return &sg_entries[i];
        return NULL;
    }

    // best first; past sg_most, the least played go
    void Trim()
    {
        long long const now = time( NULL );
        std::stable_sort( sg_entries.begin(), sg_entries.end(),
                          [now]( Entry const & a, Entry const & b ){ return Points( a, now ) > Points( b, now ); } );
        if ( sg_entries.size() > sg_most )
            sg_entries.resize( sg_most );
    }

    // adds what other knows to keep
    void Absorb( Entry & keep, Entry const & other )
    {
        keep.seconds += other.seconds;
        keep.visits += other.visits;
        if ( other.last > keep.last )
        {
            keep.last = other.last;
            if ( RealName( other.name, other ) )
                keep.name = other.name;
        }
        if ( !RealName( keep.name, keep ) && RealName( other.name, other ) )
            keep.name = other.name;
        if ( other.liveWhen > keep.liveWhen )
        {
            keep.live = other.live;
            keep.users = other.users;
            keep.maxUsers = other.maxUsers;
            keep.liveWhen = other.liveWhen;
        }
    }

    // One server reached by two names (Play Now joins by host name, the
    // server list gives numbers) is one server: once both are known to lead
    // to the same address and port, they are kept as one, under the name.
    bool Merge()
    {
        bool merged = false;
        for ( size_t i = 0; i < sg_entries.size(); ++i )
        {
            if ( sg_entries[i].address.empty() )
                continue;
            for ( size_t j = i + 1; j < sg_entries.size(); )
            {
                if ( sg_entries[j].address != sg_entries[i].address )
                {
                    ++j;
                    continue;
                }
                Entry other = sg_entries[j];
                sg_entries.erase( sg_entries.begin() + j );
                Entry & keep = sg_entries[i];
                if ( Numeric( keep.host ) && !Numeric( other.host ) )
                    std::swap( keep.host, other.host );
                Absorb( keep, other );
                merged = true;
            }
        }
        return merged;
    }

    void Save()
    {
        std::ofstream out( static_cast< char const * >( tDirectories::Var().GetWritePath( sg_fileName ) ),
                           std::ios::binary );
        if ( !out )
            return;
        out << sg_fileTag << "\n";
        out << "# host, port, seconds played, visits, last played (unix time), name; tab separated. Kept on this PC only.\n";
        for ( size_t i = 0; i < sg_entries.size(); ++i )
        {
            Entry const & e = sg_entries[i];
            out << e.host << '\t' << e.port << '\t' << static_cast< long long >( e.seconds + .5 ) << '\t'
                << e.visits << '\t' << e.last << '\t' << e.name << '\n';
        }
    }

    bool Number( std::string const & text, long long most, long long & value )
    {
        if ( text.empty() || text.size() > 18 )
            return false;
        for ( size_t i = 0; i < text.size(); ++i )
            if ( !isdigit( static_cast< unsigned char >( text[i] ) ) )
                return false;
        value = atoll( text.c_str() );
        return value <= most;
    }

    // Reads the file once. A line that is not what this writes is skipped; a
    // file of another version is started over.
    void Load()
    {
        if ( sg_loaded )
            return;
        sg_loaded = true;
        sg_entries.clear();

        std::ifstream in( static_cast< char const * >( tDirectories::Var().GetReadPath( sg_fileName ) ),
                          std::ios::binary );
        std::string line;
        if ( !in || !std::getline( in, line ) )
            return;
        if ( !line.empty() && line[ line.size() - 1 ] == '\r' )
            line.erase( line.size() - 1 );
        // a file saved by hand in an editor may start with a UTF-8 mark
        if ( line.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 )
            line.erase( 0, 3 );
        if ( line != sg_fileTag )
            return;

        while ( std::getline( in, line ) )
        {
            if ( !line.empty() && line[ line.size() - 1 ] == '\r' )
                line.erase( line.size() - 1 );
            if ( line.empty() || line[0] == '#' )
                continue;

            std::vector< std::string > field;
            std::string::size_type start = 0;
            for ( std::string::size_type tab; ( tab = line.find( '\t', start ) ) != std::string::npos; start = tab + 1 )
                field.push_back( line.substr( start, tab - start ) );
            field.push_back( line.substr( start ) );
            if ( field.size() != 6 )
                continue;

            Entry e;
            long long port = 0, seconds = 0, visits = 0, last = 0;
            e.host = CleanHost( field[0] );
            if ( e.host.empty() || !Number( field[1], 65535, port ) || port <= 0 ||
                 !Number( field[2], 1000000000LL, seconds ) || !Number( field[3], 10000000LL, visits ) ||
                 !Number( field[4], 100000000000LL, last ) )
                continue;
            e.port = static_cast< unsigned int >( port );
            e.seconds = double( seconds );
            e.visits = int( visits );
            e.last = last;
            e.name = CleanName( field[5] );

            Entry * const twin = Find( e.host, e.port );
            if ( twin )
                Absorb( *twin, e );
            else
                sg_entries.push_back( e );
        }
        Trim();
    }

    // the servers the rows are for, best first: played on for real (one visit
    // at least), one per address where the addresses are known
    std::vector< size_t > Candidates( size_t most )
    {
        long long const now = time( NULL );
        std::vector< size_t > order;
        for ( size_t i = 0; i < sg_entries.size(); ++i )
            if ( sg_entries[i].visits > 0 )
                order.push_back( i );
        std::stable_sort( order.begin(), order.end(),
                          [now]( size_t a, size_t b ){ return Points( sg_entries[a], now ) > Points( sg_entries[b], now ); } );

        std::vector< size_t > picks;
        for ( size_t i = 0; i < order.size() && picks.size() < most; ++i )
        {
            Entry const & e = sg_entries[ order[i] ];
            bool twin = false;
            for ( size_t j = 0; j < picks.size() && !twin; ++j )
                twin = !e.address.empty() && sg_entries[ picks[j] ].address == e.address;
            if ( !twin )
                picks.push_back( order[i] );
        }
        return picks;
    }

    // ---- asking the favourites who is on ----

    struct Lookup
    {
        std::vector< std::string > hosts, numbers;
        std::atomic< bool > done;
        SDL_Thread * thread;

        Lookup(): done( false ), thread( NULL ){}
    };

    int sg_LookupThread( void * data )
    {
        Lookup * const lookup = static_cast< Lookup * >( data );
        for ( size_t i = 0; i < lookup->hosts.size(); ++i )
            lookup->numbers[i] = st_LookupHost( lookup->hosts[i] );
        lookup->done.store( true, std::memory_order_release );
        return 0;
    }

    Lookup * sg_lookup = NULL;
    bool sg_askWhenLookedUp = false;    // the lookup out is for an asking, not one that was given up

    enum Stage
    {
        Stage_Idle,
        Stage_Asking,       // the servers are in the shared list and being asked
        Stage_Settling      // asked; late answers are still taken
    };
    Stage sg_stage = Stage_Idle;
    bool sg_wasAlone = false;       // the network was standalone when the asking began
    double sg_next = 0;             // tSysTimeFloat() of the next asking
    double sg_until = 0;            // and of the end of the current stage
    double sg_lastFrame = -1E10;    // of the asking page's last frame

    // which page asks (a gServerHistory::Asker), and when an asking of each
    // last ran to its end
    int sg_asker = -1;
    double sg_lastAsked[ gServerHistory::Asker_Count ] = { -1E10, -1E10 };

    // the servers the asking page wants asked besides the favourites: host
    // (lower case) and port
    std::vector< std::pair< std::string, unsigned int > > sg_others;

    // every name looked up and the number it led to; empty where it led nowhere
    std::map< std::string, std::string > sg_numbers;
    double sg_lookedUpWhen = -1E10;

    // what each server said, by number and port ("192.0.2.7:4534")
    std::map< std::string, gServerHistory::Heard > sg_heard;

    // what was put in the shared list: number and port
    std::vector< std::pair< std::string, unsigned int > > sg_asked;

    bool Allowed()
    {
        // a recording must play back the same way, and threads answer when they like
        return !tRecorder::IsRunning();
    }

    // When what a page shows was last asked for. The favourites go along
    // with every asking, so for the main menu any page's asking counts.
    double LastAsked( int asker )
    {
        if ( asker != gServerHistory::Asker_MainMenu )
            return sg_lastAsked[ asker ];
        double last = -1E10;
        for ( int i = 0; i < gServerHistory::Asker_Count; ++i )
            last = std::max( last, sg_lastAsked[i] );
        return last;
    }

    void StartLookup()
    {
        Lookup * const lookup = new Lookup;
        std::vector< size_t > picks;
        if ( sg_historyOn )
            picks = Candidates( sg_lookedUp );
        for ( size_t i = 0; i < picks.size(); ++i )
        {
            std::string const & host = sg_entries[ picks[i] ].host;
            if ( std::find( lookup->hosts.begin(), lookup->hosts.end(), host ) == lookup->hosts.end() )
                lookup->hosts.push_back( host );
        }
        for ( size_t i = 0; i < sg_others.size(); ++i )
        {
            std::string const & host = sg_others[i].first;
            if ( !Numeric( host ) &&
                 std::find( lookup->hosts.begin(), lookup->hosts.end(), host ) == lookup->hosts.end() )
                lookup->hosts.push_back( host );
        }
        if ( lookup->hosts.empty() && sg_others.empty() )
        {
            delete lookup;
            return;     // nobody to ask, so nothing is sent
        }
        lookup->numbers.resize( lookup->hosts.size() );
        lookup->thread = SDL_CreateThread( &sg_LookupThread, lookup );
        if ( !lookup->thread )
        {
            delete lookup;
            return;
        }
        sg_lookup = lookup;
        sg_askWhenLookedUp = true;
    }

    // the numbers the thread found, onto every entry under those names; a
    // lookup that found nothing keeps what was known
    void TakeLookup()
    {
        SDL_WaitThread( sg_lookup->thread, NULL );
        for ( size_t i = 0; i < sg_lookup->hosts.size(); ++i )
        {
            std::string const & host = sg_lookup->hosts[i];
            std::string const & number = sg_lookup->numbers[i];
            if ( !number.empty() )
                sg_numbers[ host ] = number;
            else if ( sg_numbers.find( host ) == sg_numbers.end() )
                sg_numbers[ host ] = std::string();
            for ( size_t j = 0; j < sg_entries.size(); ++j )
                if ( sg_entries[j].host == host && !number.empty() )
                    sg_entries[j].address = number + ":" + PortText( sg_entries[j].port );
        }
        sg_lookedUpWhen = tSysTimeFloat();
        delete sg_lookup;
        sg_lookup = NULL;

        if ( Merge() )
        {
            Trim();
            Save();
            ++sg_generation;
        }
    }

    // whether the shared list still holds what this put there, and only that
    bool StillOurs()
    {
        size_t count = 0;
        for ( nServerInfo * server = nServerInfo::GetFirstServer(); server; server = server->Next() )
        {
            ++count;
            std::pair< std::string, unsigned int > const which( Std( server->GetConnectionName() ), server->GetPort() );
            if ( std::find( sg_asked.begin(), sg_asked.end(), which ) == sg_asked.end() )
                return false;
        }
        return count == sg_asked.size();
    }

    // What has come in, onto the entries it is about and into what was
    // heard. Complete: the asking ran to its end, and those that said
    // nothing are taken to be silent. Before that it may be read every frame,
    // so counts fill in as the answers arrive.
    void ReadAnswers( bool complete )
    {
        double const now = tSysTimeFloat();
        bool named = false, changed = complete;
        for ( nServerInfo * server = nServerInfo::GetFirstServer(); server; server = server->Next() )
        {
            std::string const address = Std( server->GetConnectionName() ) + ":" + PortText( server->GetPort() );
            gServerHistory::Heard & heard = sg_heard[ address ];
            if ( server->Reachable() )
            {
                changed = changed || heard.live != gServerHistory::Live_Answered ||
                          heard.users != server->Users() || heard.maxUsers != server->MaxUsers();
                heard.live = gServerHistory::Live_Answered;
                heard.users = server->Users();
                heard.maxUsers = server->MaxUsers();
                heard.name = server->GetName();
                heard.address = address.c_str();
                heard.when = now;
            }
            else if ( complete )
            {
                heard.live = gServerHistory::Live_Silent;
                heard.users = 0;
                heard.address = address.c_str();
                heard.when = now;
            }

            for ( size_t i = 0; i < sg_entries.size(); ++i )
            {
                Entry & e = sg_entries[i];
                if ( e.address != address )
                    continue;
                if ( server->Reachable() )
                {
                    e.live = gServerHistory::Live_Answered;
                    e.users = server->Users();
                    e.maxUsers = server->MaxUsers();
                    e.liveWhen = now;

                    // a server that was renamed is shown by its new name
                    std::string const name = CleanName( Std( server->GetName() ) );
                    if ( RealName( name, e ) && name != e.name )
                    {
                        e.name = name;
                        named = true;
                    }
                }
                else if ( complete )
                {
                    e.live = gServerHistory::Live_Silent;
                    e.liveWhen = now;
                }
            }
        }
        if ( named )
            Save();
        if ( changed || named )
            ++sg_generation;
    }

    // Ends an asking and leaves the shared list empty. With take, answers that
    // are waiting are read first and kept; without, they may be old by now
    // (the main menu was away) and are dropped.
    void Finish( bool take )
    {
        if ( sg_stage != Stage_Idle && StillOurs() )
        {
            if ( take )
            {
                sn_Receive();
                ReadAnswers( false );
            }
            nServerInfo::DeleteAll( false );

            // Asking put the network client-side (sn_Bend). A menu out of a
            // game must not stay there: the next player object made, as when
            // the player page closes, would wait on a server for an id that
            // never comes, and the game ends with "Not enough nNetObject IDs".
            if ( sg_wasAlone && sn_GetNetState() == nCLIENT )
                sn_SetNetState( nSTANDALONE );
        }
        sg_stage = Stage_Idle;
        sg_asked.clear();
    }

    void StartAsking()
    {
        double const now = tSysTimeFloat();
        sg_asked.clear();
        std::vector< size_t > picks;
        if ( sg_historyOn )
            picks = Candidates( sg_rows );
        for ( size_t i = 0; i < picks.size(); ++i )
        {
            Entry & e = sg_entries[ picks[i] ];
            std::string::size_type const colon = e.address.rfind( ':' );
            if ( colon == std::string::npos )
            {
                // its name leads nowhere just now
                e.live = gServerHistory::Live_Silent;
                e.liveWhen = now;
                continue;
            }
            std::pair< std::string, unsigned int > const which( e.address.substr( 0, colon ), e.port );
            if ( std::find( sg_asked.begin(), sg_asked.end(), which ) == sg_asked.end() )
                sg_asked.push_back( which );
        }
        for ( size_t i = 0; i < sg_others.size(); ++i )
        {
            // a name that leads nowhere just now is not asked; Counted() says so
            std::string number = sg_others[i].first;
            if ( !Numeric( number ) )
            {
                std::map< std::string, std::string >::const_iterator const found = sg_numbers.find( number );
                if ( found == sg_numbers.end() || found->second.empty() )
                    continue;
                number = found->second;
            }
            std::pair< std::string, unsigned int > const which( number, sg_others[i].second );
            if ( std::find( sg_asked.begin(), sg_asked.end(), which ) == sg_asked.end() )
                sg_asked.push_back( which );
        }
        ++sg_generation;
        if ( sg_asked.empty() )
            return;

        // The list is the browser's and Play Now's too. Each of them starts
        // by emptying it, and so does this, only ever from the asking page's
        // own frame (the main menu's or the Play page's), when neither of
        // them is running. The servers go in by number, so asking them needs
        // no lookup here.
        nServerInfo::DeleteAll( false );
        for ( size_t i = 0; i < sg_asked.size(); ++i )
        {
            nServerInfo * const server = tNEW( nServerInfo );
            server->SetConnectionName( tString( sg_asked[i].first.c_str() ) );
            server->SetPort( sg_asked[i].second );
        }
        sg_wasAlone = sn_GetNetState() == nSTANDALONE;
        nServerInfo::StartQueryAll( nServerInfo::QUERY_ALL );
        sg_stage = Stage_Asking;
        sg_until = now + ( sg_asked.size() > sg_rows ? sg_askBudgetMany : sg_askBudget );
    }
}

bool gServerHistory::Server::Fresh() const
{
    return live != Live_Unknown && tSysTimeFloat() - liveWhen < sg_freshFor;
}

void gServerHistory::Played( tString const & host, unsigned int port, tString const & name,
                             tString const & address, double seconds )
{
    Hint const hint = sg_hint;
    sg_hint = Hint();

    if ( !sg_historyOn || tRecorder::IsRunning() )
        return;

    std::string const key = CleanHost( Std( host ) );
    if ( key.empty() || port == 0 || port > 65535 || !( seconds >= 0 ) )
        return;
    seconds = std::min( seconds, 7 * 86400.0 );
    Load();

    Entry * e = Find( key, port );
    if ( !e )
    {
        sg_entries.push_back( Entry() );
        e = &sg_entries.back();
        e->host = key;
        e->port = port;
    }
    e->seconds += seconds;
    if ( seconds >= sg_visitSeconds )
        ++e->visits;
    e->last = time( NULL );

    // what to call it: what the connection called it, else what Play Now heard
    std::string called = CleanName( Std( name ) );
    if ( !RealName( called, *e ) && hint.host == key && hint.port == port )
        called = hint.name;
    if ( RealName( called, *e ) )
        e->name = called;
    if ( address.Len() > 1 )
        e->address = Std( address );

    Merge();
    Trim();
    Save();
    ++sg_generation;
}

void gServerHistory::NameHint( tString const & host, unsigned int port, tString const & name )
{
    sg_hint.host = CleanHost( Std( host ) );
    sg_hint.port = port;
    sg_hint.name = CleanName( Std( name ) );
}

std::vector< gServerHistory::Server > gServerHistory::Favourites()
{
    std::vector< Server > rows;
    if ( !sg_historyOn )
        return rows;
    Load();

    std::vector< size_t > const picks = Candidates( sg_rows );
    for ( size_t i = 0; i < picks.size(); ++i )
    {
        Entry const & e = sg_entries[ picks[i] ];
        Server row;
        row.host = e.host.c_str();
        row.port = e.port;
        row.name = e.name.c_str();
        row.seconds = e.seconds;
        row.visits = e.visits;
        row.last = e.last;
        row.live = e.live;
        row.users = e.users;
        row.maxUsers = e.maxUsers;
        row.liveWhen = e.liveWhen;
        rows.push_back( row );
    }

    // those with players first, the fullest first; the rest as they were
    std::stable_sort( rows.begin(), rows.end(), []( Server const & a, Server const & b )
    {
        int const ua = ( a.Fresh() && a.live == Live_Answered ) ? a.users : 0;
        int const ub = ( b.Fresh() && b.live == Live_Answered ) ? b.users : 0;
        return ua > ub;
    } );
    return rows;
}

int gServerHistory::Generation()
{
    // switching the history off or on changes what the rows show, too
    return sg_generation * 2 + ( sg_historyOn ? 1 : 0 );
}

bool gServerHistory::Heard::Fresh() const
{
    return live != Live_Unknown && tSysTimeFloat() - when < sg_freshFor;
}

void gServerHistory::CountFrame( int asker, std::vector< Target > const & others )
{
    if ( asker < 0 || asker >= Asker_Count )
        return;

    double const now = tSysTimeFloat();
    bool const switched = asker != sg_asker;
    bool const away = switched || now - sg_lastFrame > sg_awayGap;
    sg_lastFrame = now;
    sg_asker = asker;

    // what this page wants asked besides the favourites, for its next asking
    sg_others.clear();
    for ( size_t i = 0; i < others.size(); ++i )
    {
        std::string const host = CleanHost( Std( others[i].host ) );
        if ( !host.empty() && others[i].port > 0 && others[i].port <= 65535 )
            sg_others.push_back( std::make_pair( host, others[i].port ) );
    }

    bool const lookedUp = sg_lookup && sg_lookup->done.load( std::memory_order_acquire );
    if ( lookedUp )
        TakeLookup();

    if ( !Allowed() )
    {
        Finish( false );
        sg_askWhenLookedUp = false;
        return;
    }

    // Back from somewhere else (a game, a page), or another page asked last:
    // whatever was out is given up, and counts that are old by now are asked
    // for again soon; at once on a page that is there for its counts.
    if ( away )
    {
        Finish( false );
        // a lookup still out was for the other page's servers
        if ( switched )
            sg_askWhenLookedUp = false;
        if ( now - LastAsked( asker ) > sg_staleAfter )
            sg_next = now + ( asker == Asker_MainMenu ? sg_backAfter : 0 );
    }

    if ( lookedUp )
    {
        if ( sg_askWhenLookedUp && sg_stage == Stage_Idle )
            StartAsking();
        sg_askWhenLookedUp = false;
    }

    double const every = asker == Asker_MainMenu ? sg_askEvery : sg_askEveryPage;
    switch ( sg_stage )
    {
    case Stage_Idle:
        if ( !sg_lookup && now >= sg_next )
        {
            sg_next = now + every;
            Load();
            StartLookup();
        }
        break;

    case Stage_Asking:
    case Stage_Settling:
        if ( !StillOurs() )
        {
            // someone else has the list now; it is theirs
            sg_stage = Stage_Idle;
            sg_asked.clear();
            break;
        }

        // As the browser does each frame: answers are read as they come in.
        // DoQueryAll reads only on the frames it is its turn to ask (one
        // question per BROWSER_QUERY_DELAY_GLOBAL), and sends at most one.
        sn_Receive();
        sn_SendPlanned();

        if ( sg_stage == Stage_Asking )
        {
            bool const going = nServerInfo::DoQueryAll( sg_asked.size() > sg_rows ? sg_simultaneousMany : sg_simultaneous );
            if ( !going || now > sg_until )
            {
                ReadAnswers( true );
                sg_stage = Stage_Settling;
                sg_until = now + sg_settle;
                sg_lastAsked[ asker ] = now;
            }
            else
                ReadAnswers( false );
        }
        else if ( now > sg_until )
        {
            // late answers went to the servers they are about, not to
            // whoever uses the list next
            Finish( true );
            sg_next = now + every;
        }
        break;
    }
}

void gServerHistory::CountLeaves()
{
    bool const cut = sg_stage == Stage_Asking;
    Finish( true );

    // if the page stays after all, the asking is done again soon
    if ( cut )
        sg_next = tSysTimeFloat() + sg_backAfter;
}

gServerHistory::Heard gServerHistory::Counted( tString const & host, unsigned int port )
{
    std::string number = CleanHost( Std( host ) );
    if ( number.empty() )
        return Heard();
    if ( !Numeric( number ) )
    {
        std::map< std::string, std::string >::const_iterator const found = sg_numbers.find( number );
        if ( found == sg_numbers.end() )
            return Heard();     // not looked up yet
        if ( found->second.empty() )
        {
            // the name led nowhere
            Heard nowhere;
            nowhere.live = Live_Silent;
            nowhere.when = sg_lookedUpWhen;
            return nowhere;
        }
        number = found->second;
    }
    std::map< std::string, Heard >::const_iterator const heard = sg_heard.find( number + ":" + PortText( port ) );
    return heard != sg_heard.end() ? heard->second : Heard();
}

void gServerHistory::MainMenuFrame()
{
    CountFrame( Asker_MainMenu, std::vector< Target >() );
}

void gServerHistory::MainMenuLeaves()
{
    CountLeaves();
}

double gServerHistory::Score( tString const & host, unsigned int port )
{
    if ( !sg_historyOn )
        return -1;
    Load();

    std::string const key = CleanHost( Std( host ) );
    std::string const address = key + ":" + PortText( port );
    long long const now = time( NULL );
    for ( size_t i = 0; i < sg_entries.size(); ++i )
    {
        Entry const & e = sg_entries[i];
        if ( ( e.port == port && e.host == key ) || ( Numeric( key ) && e.address == address ) )
            return Points( e, now );
    }
    return -1;
}

bool gServerHistory::On()
{
    return sg_historyOn;
}

void gServerHistory::Clear()
{
    Finish( false );
    sg_entries.clear();
    sg_loaded = true;
    std::remove( static_cast< char const * >( tDirectories::Var().GetWritePath( sg_fileName ) ) );
    ++sg_generation;
    con << tOutput( "$server_history_cleared" );
}

#else

bool gServerHistory::Server::Fresh() const { return false; }
void gServerHistory::Played( tString const &, unsigned int, tString const &, tString const &, double ){}
void gServerHistory::NameHint( tString const &, unsigned int, tString const & ){}
std::vector< gServerHistory::Server > gServerHistory::Favourites(){ return std::vector< Server >(); }
int gServerHistory::Generation(){ return 0; }
bool gServerHistory::Heard::Fresh() const { return false; }
void gServerHistory::CountFrame( int, std::vector< Target > const & ){}
void gServerHistory::CountLeaves(){}
gServerHistory::Heard gServerHistory::Counted( tString const &, unsigned int ){ return Heard(); }
void gServerHistory::MainMenuFrame(){}
void gServerHistory::MainMenuLeaves(){}
double gServerHistory::Score( tString const &, unsigned int ){ return -1; }
bool gServerHistory::On(){ return false; }
void gServerHistory::Clear(){}

#endif
