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

#include "gLeague.h"

#ifndef DEDICATED

#include "ePlayer.h"
#include "rScreen.h"
#include "rSDL.h"
#include "tConfiguration.h"
#include "tDirectories.h"
#include "tHttps.h"
#include "tJson.h"
#include "tSysTime.h"
#include "uMenu.h"
#include "uRclTheme.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// What the league's site says, as the main menu shows it. The site is asked
// on a thread of its own, which is handed five paths and gives back five
// answers; nothing else crosses over. The answers are read, kept on disk and
// turned into what is drawn here, on the main thread.

namespace
{
    char const * const sg_leagueHost = "retrocyclesleague.com";

    // what is asked for, in this order
    enum Source
    {
        Source_Events,
        Source_Tst,
        Source_Sumobar,
        Source_Twos,
        Source_Fortress,
        Source_Count
    };

    char const * const sg_sourceName[ Source_Count ] = { "events", "tst", "sumobar", "2s", "fortress" };

    // the ladders, as the tabs name them; Source_Tst + index is where each comes from
    int const sg_ladderCount = 4;

    double const sg_freshFor = 300;     // seconds an answer is good for
    double const sg_retryAfter = 60;    // after no answer at all
    double const sg_staleShown = 900;   // older than this, the menu says how old

    // ---- what was read ----

    struct Row
    {
        int rank;
        std::string name, badge, score;
        int tier;       // index into sg_tierColor, -1 for none

        Row(): rank( 0 ), tier( -1 ){}
    };

    struct Ladder
    {
        bool loaded;
        int players;
        std::vector< Row > top;
        bool hasMine;
        Row mine;

        Ladder(): loaded( false ), players( 0 ), hasMine( false ){}
    };

    struct Event
    {
        bool loaded;
        std::string title, slug, winner;
        time_t starts;
        int signups;
        bool open;

        Event(): loaded( false ), starts( 0 ), signups( 0 ), open( false ){}
    };

    std::string sg_body[ Source_Count ];        // the last good answer from each
    time_t sg_bodyWhen[ Source_Count ];         // and when it came
    bool sg_cacheRead = false;

    Ladder sg_ladder[ sg_ladderCount ];
    Event sg_next, sg_last;
    std::string sg_builtFor;                    // whom the ladders were read for
    bool sg_rebuild = true;

    int sg_ladderShown = 0;
    tConfItem< int > sg_ladderShownConf( "RCL_HOME_LADDER", sg_ladderShown );

    // ---- the thread that asks ----

    struct Fetch
    {
        std::string path[ Source_Count ];
        int status[ Source_Count ];
        std::string body[ Source_Count ];
        std::atomic< bool > done;
        SDL_Thread * thread;

        Fetch(): done( false ), thread( NULL )
        {
            for ( int i = 0; i < Source_Count; ++i )
                status[i] = -1;
        }
    };

    Fetch * sg_fetch = NULL;
    double sg_lastAsked = -1E10;    // tSysTimeFloat() of the last request
    bool sg_lastAnswered = false;

    int sg_FetchThread( void * data )
    {
        Fetch * const fetch = static_cast< Fetch * >( data );
        for ( int i = 0; i < Source_Count; ++i )
            fetch->status[i] = st_HttpsGet( sg_leagueHost, fetch->path[i], fetch->body[i], 2 << 20, 8 );
        fetch->done.store( true, std::memory_order_release );
        return 0;
    }

    // ---- reading ----

    std::string Lower( std::string text )
    {
        for ( size_t i = 0; i < text.size(); ++i )
            text[i] = static_cast< char >( tolower( static_cast< unsigned char >( text[i] ) ) );
        return text;
    }

    // Text from the site, made safe to draw: the game's font is Latin-1, so
    // what lies beyond it becomes '?', control characters go, and "0x" can
    // never start a colour code. A name that is mostly '?' gives way to the
    // fallback (its profile's address, which is plain).
    std::string Plain( std::string const & utf8, std::string const & fallback = std::string(), size_t most = 28 )
    {
        std::string out;
        int unknown = 0, known = 0;
        for ( size_t i = 0; i < utf8.size() && out.size() < most; )
        {
            unsigned char const c = utf8[i];
            unsigned long code = c;
            int extra = 0;
            if ( c >= 0xF0 )      { code = c & 0x07; extra = 3; }
            else if ( c >= 0xE0 ) { code = c & 0x0F; extra = 2; }
            else if ( c >= 0xC0 ) { code = c & 0x1F; extra = 1; }
            else if ( c >= 0x80 ) { code = '?'; }
            ++i;
            for ( ; extra > 0 && i < utf8.size(); --extra, ++i )
                code = ( code << 6 ) | ( static_cast< unsigned char >( utf8[i] ) & 0x3F );

            if ( code < 0x20 || ( code >= 0x7F && code < 0xA0 ) )
                continue;
            if ( code > 0xFF )
            {
                out += '?';
                ++unknown;
                continue;
            }
            if ( code == 'x' && !out.empty() && out[ out.size() - 1 ] == '0' )
                code = 'X';
            out += static_cast< char >( code );
            if ( code != ' ' )
                ++known;
        }
        while ( !out.empty() && out[ out.size() - 1 ] == ' ' )
            out.erase( out.size() - 1 );
        while ( !out.empty() && out[0] == ' ' )
            out.erase( 0, 1 );
        if ( ( out.empty() || unknown * 2 > known ) && !fallback.empty() )
            return Plain( fallback, std::string(), most );
        return out;
    }

    int Tier( std::string const & name )
    {
        static char const * const names[] = { "legend", "master", "diamond", "platinum", "gold", "silver", "bronze" };
        std::string const lower = Lower( name );
        for ( int i = 0; i < 7; ++i )
            if ( lower == names[i] )
                return i;
        return -1;
    }

    std::string Badge( tJson const & rankName, tJson const & division )
    {
        std::string badge = Lower( Plain( rankName.Str() ) );
        if ( !badge.empty() && !division.Str().empty() )
            badge += " " + Lower( Plain( division.Str() ) );
        return badge;
    }

    std::string Number( double value, char const * unit = "" )
    {
        char buffer[ 32 ];
        snprintf( buffer, sizeof( buffer ), "%d%s", static_cast< int >( value + ( value < 0 ? -.5 : .5 ) ), unit );
        return buffer;
    }

    // whether any of a row's names is one of the player's
    bool IsMine( std::vector< std::string > const & me, tJson const & row, char const * const * fields )
    {
        for ( ; *fields; ++fields )
        {
            std::string const name = Lower( row[ *fields ].Str() );
            if ( name.empty() )
                continue;
            for ( size_t i = 0; i < me.size(); ++i )
                if ( name == me[i] )
                    return true;
        }
        return false;
    }

    void ReadLadder( int index, std::vector< std::string > const & me )
    {
        Ladder ladder;
        tJson doc;
        if ( !tJson::Parse( sg_body[ Source_Tst + index ], doc ) )
        {
            sg_ladder[ index ] = ladder;
            return;
        }

        tJson const * rows = NULL;
        static char const * const tstNames[] = { "username", "profileSlug", NULL };
        static char const * const sumoNames[] = { "player_name", NULL };
        static char const * const twosNames[] = { "player_name", "profile_slug", "player_auth", NULL };
        static char const * const fortNames[] = { "nickname", "profileSlug", NULL };
        char const * const * names = tstNames;
        switch ( Source_Tst + index )
        {
        case Source_Tst:      rows = &doc["data"];    names = tstNames;  break;
        case Source_Sumobar:  rows = &doc["rows"];    names = sumoNames; break;
        case Source_Twos:     rows = &doc["rows"];    names = twosNames; break;
        case Source_Fortress: rows = &doc["entries"]; names = fortNames; break;
        }
        if ( !rows || rows->Type() != tJson::Kind_Array )
        {
            sg_ladder[ index ] = ladder;
            return;
        }

        ladder.loaded = true;
        ladder.players = static_cast< int >( rows->Size() );
        for ( size_t i = 0; i < rows->Size(); ++i )
        {
            tJson const & r = ( *rows )[ i ];
            Row row;
            switch ( Source_Tst + index )
            {
            case Source_Tst:
                row.rank = r["leaderboardRank"].Int( static_cast< int >( i ) + 1 );
                row.name = Plain( r["username"].Str(), r["profileSlug"].Str() );
                row.badge = Badge( r["rankName"], r["rankDivision"] );
                row.tier = Tier( r["tier"].Str() );
                row.score = Number( r["elo"].Number() );
                break;
            case Source_Sumobar:
                row.rank = r["rank"].Int( static_cast< int >( i ) + 1 );
                row.name = Plain( r["player_name"].Str() );
                row.score = Number( r["rating"].Number() );
                break;
            case Source_Twos:
                row.rank = r["rank"].Int( static_cast< int >( i ) + 1 );
                row.name = Plain( r["player_name"].Str(), r["profile_slug"].Str() );
                row.score = Number( r["rating"].Number() );
                break;
            case Source_Fortress:
                row.rank = r["rank"].Int( static_cast< int >( i ) + 1 );
                row.name = Plain( r["nickname"].Str(), r["profileSlug"].Str() );
                row.badge = Badge( r["rankName"], r["division"] );
                row.tier = Tier( r["tier"].Str() );
                row.score = Number( r["lp"].Number(), " lp" );
                break;
            }
            if ( ladder.top.size() < 5 )
                ladder.top.push_back( row );
            if ( !ladder.hasMine && IsMine( me, r, names ) )
            {
                ladder.hasMine = true;
                ladder.mine = row;
            }
        }
        sg_ladder[ index ] = ladder;
    }

    // "2026-11-01T18:00:00.000Z" as seconds since 1970 (UTC)
    time_t When( std::string const & iso )
    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if ( sscanf( iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s ) < 3 || mo < 1 || mo > 12 )
            return 0;
        y -= mo <= 2;
        long const era = ( y >= 0 ? y : y - 399 ) / 400;
        long const yoe = y - era * 400;
        long const doy = ( 153L * ( mo + ( mo > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
        long const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        long const days = era * 146097 + doe - 719468;
        return static_cast< time_t >( days ) * 86400 + h * 3600 + mi * 60 + s;
    }

    // keeps letters, digits, '-' and '_': all an event's address may hold
    std::string Slug( std::string const & slug )
    {
        std::string out;
        for ( size_t i = 0; i < slug.size() && out.size() < 64; ++i )
        {
            char const c = slug[i];
            if ( isalnum( static_cast< unsigned char >( c ) ) || c == '-' || c == '_' )
                out += c;
        }
        return out;
    }

    void ReadEvents()
    {
        sg_next = Event();
        sg_last = Event();
        tJson doc;
        if ( !tJson::Parse( sg_body[ Source_Events ], doc ) )
            return;

        tJson const & up = doc["upcoming"][ 0 ];
        if ( up.Type() == tJson::Kind_Object )
        {
            sg_next.loaded = true;
            sg_next.title = Lower( Plain( up["title"].Str(), std::string(), 40 ) );
            sg_next.slug = Slug( up["slug"].Str() );
            sg_next.starts = When( up["startsAt"].Str() );
            sg_next.signups = up["signupCount"].Int();
            sg_next.open = up["signupOpen"].Bool();
        }

        tJson const & past = doc["past"][ 0 ];
        if ( past.Type() == tJson::Kind_Object )
        {
            sg_last.loaded = true;
            sg_last.title = Lower( Plain( past["title"].Str(), std::string(), 40 ) );
            sg_last.slug = Slug( past["slug"].Str() );
            // a team's winners come a line each
            std::string winners, line;
            std::istringstream lines( past["winnerText"].Str() );
            while ( std::getline( lines, line ) )
            {
                line = Plain( line );
                if ( line.empty() )
                    continue;
                if ( !winners.empty() )
                    winners += " & ";
                winners += line;
            }
            sg_last.winner = Lower( winners );
        }
    }

    // who the player is on the ladders: their sign-in name and their public name
    std::vector< std::string > Me()
    {
        std::vector< std::string > me;
        if ( !ePlayer::RclAuthenticated() )
            return me;
        tString const identity = ePlayer::RclIdentity();
        int const at = identity.StrPos( "@" );
        tString const name = at > 0 ? identity.SubStr( 0, at ) : identity;
        std::string const user = Lower( std::string( static_cast< char const * >( name ) ) );
        if ( user.empty() )
            return me;
        me.push_back( user );
        me.push_back( user + "@rcl" );
        tString const display = ePlayer::RclDisplayName();
        std::string const shown = Lower( std::string( static_cast< char const * >( display ) ) );
        if ( !shown.empty() && shown != user )
            me.push_back( shown );
        return me;
    }

    void Rebuild()
    {
        std::vector< std::string > const me = Me();
        std::string key;
        for ( size_t i = 0; i < me.size(); ++i )
            key += me[i] + "\n";
        if ( !sg_rebuild && key == sg_builtFor )
            return;
        sg_builtFor = key;
        sg_rebuild = false;

        ReadEvents();
        for ( int i = 0; i < sg_ladderCount; ++i )
            ReadLadder( i, me );
    }

    // ---- kept on disk: one file per answer, its first line the time it came ----

    tString CacheName( int source )
    {
        tString name( "league-" );
        name << sg_sourceName[ source ] << ".json";
        return name;
    }

    void ReadCache()
    {
        sg_cacheRead = true;
        for ( int i = 0; i < Source_Count; ++i )
        {
            sg_bodyWhen[i] = 0;
            std::ifstream in( static_cast< char const * >( tDirectories::Var().GetReadPath( CacheName( i ) ) ),
                              std::ios::binary );
            if ( !in )
                continue;
            long long when = 0;
            std::string first;
            if ( !std::getline( in, first ) || sscanf( first.c_str(), "%lld", &when ) != 1 )
                continue;
            std::ostringstream body;
            body << in.rdbuf();
            sg_body[i] = body.str();
            sg_bodyWhen[i] = static_cast< time_t >( when );
        }
        sg_rebuild = true;
    }

    void WriteCache( int source )
    {
        std::ofstream out( static_cast< char const * >( tDirectories::Var().GetWritePath( CacheName( source ) ) ),
                           std::ios::binary );
        if ( out )
            out << static_cast< long long >( sg_bodyWhen[ source ] ) << "\n" << sg_body[ source ];
    }

    void Ask()
    {
        Fetch * const fetch = new Fetch;
        time_t const now = time( NULL );
        tm const * const utc = gmtime( &now );
        int const season = utc ? utc->tm_year + 1900 : 2026;
        char fortress[ 96 ];
        snprintf( fortress, sizeof( fortress ), "/api/hub/leaderboard/fortress?season=%d", season );

        fetch->path[ Source_Events ] = "/api/events";
        fetch->path[ Source_Tst ] = "/api/v1/leaderboard";
        fetch->path[ Source_Sumobar ] = "/api/v1/sumobar/leaderboard?limit=200";
        fetch->path[ Source_Twos ] = "/api/v1/two-s/leaderboard?limit=200";
        fetch->path[ Source_Fortress ] = fortress;

        sg_lastAsked = tSysTimeFloat();
        fetch->thread = SDL_CreateThread( &sg_FetchThread, fetch );
        if ( !fetch->thread )
        {
            delete fetch;
            return;
        }
        sg_fetch = fetch;
    }

    void TakeAnswer()
    {
        SDL_WaitThread( sg_fetch->thread, NULL );
        sg_lastAnswered = false;
        time_t const now = time( NULL );
        for ( int i = 0; i < Source_Count; ++i )
        {
            // an answer that cannot be read keeps the last one that could
            tJson check;
            if ( sg_fetch->status[i] != 200 || !tJson::Parse( sg_fetch->body[i], check ) )
                continue;
            sg_body[i].swap( sg_fetch->body[i] );
            sg_bodyWhen[i] = now;
            sg_lastAnswered = true;
            WriteCache( i );
        }
        delete sg_fetch;
        sg_fetch = NULL;
        sg_rebuild = true;
    }

    // ---- drawing ----

    using namespace uRclTheme;

    uRclTheme::Color const sg_tierColor[] =
    {
        { .933f, 1.0f, .255f },     // legend: the accent
        { .73f, .58f, 1.0f },       // master
        { .50f, .78f, 1.0f },       // diamond
        { .50f, .88f, .80f },       // platinum
        { .93f, .79f, .38f },       // gold
        { .76f, .78f, .81f },       // silver
        { .80f, .58f, .40f }        // bronze
    };

    uRclTheme::Color const sg_cardColor = { .094f, .098f, .102f };

    char const * const sg_weekday[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    char const * const sg_month[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

    // where things are this frame, in logical pixels, for the mouse
    struct Box
    {
        REAL left, top, right, bottom;
        bool Holds( REAL x, REAL y ) const { return x >= left && x <= right && y >= top && y <= bottom; }
    };
    Box sg_tabBox[ sg_ladderCount ];
    Box sg_signupBox, sg_boardBox, sg_ladderBox;
    bool sg_drawn = false;

    // the start of an event in the player's own time
    std::string StartText( time_t starts )
    {
        tm const * const local = localtime( &starts );
        if ( !local )
            return std::string();
        char buffer[ 64 ];
        snprintf( buffer, sizeof( buffer ), "%s %d %s, %02d:%02d", sg_weekday[ local->tm_wday % 7 ], local->tm_mday,
                  sg_month[ local->tm_mon % 12 ], local->tm_hour, local->tm_min );
        return buffer;
    }

    std::string UntilText( time_t starts )
    {
        long const left = static_cast< long >( starts - time( NULL ) );
        char buffer[ 32 ];
        if ( left <= 0 )
            return "started";
        if ( left < 3600 )
            snprintf( buffer, sizeof( buffer ), "in %ld min", ( left + 59 ) / 60 );
        else if ( left < 2 * 86400 )
            snprintf( buffer, sizeof( buffer ), "in %ld h", left / 3600 );
        else
            snprintf( buffer, sizeof( buffer ), "in %ld days", left / 86400 );
        return buffer;
    }

    std::string AgeText( time_t when )
    {
        long const age = static_cast< long >( time( NULL ) - when );
        char buffer[ 48 ];
        if ( age < 2 * 3600 )
            snprintf( buffer, sizeof( buffer ), "league data from %ld min ago", age / 60 );
        else if ( age < 2 * 86400 )
            snprintf( buffer, sizeof( buffer ), "league data from %ld h ago", age / 3600 );
        else
            snprintf( buffer, sizeof( buffer ), "league data from %ld days ago", age / 86400 );
        return buffer;
    }

    // a chip: a filled or outlined small box with a word in it; returns its width
    REAL Chip( REAL leftPx, REAL midPx, char const * text, REAL sizePx, bool filled,
               uRclTheme::Color const & color, uRclTheme::Color const & textColor, REAL alpha, REAL padPx, REAL halfPx )
    {
        REAL const widthPx = TextWidth( text, sizePx, 500 ) / W( 1 ) + 2 * padPx;
        if ( filled )
            FillRect( X( leftPx ), Y( midPx - halfPx ), X( leftPx + widthPx ), Y( midPx + halfPx ), color, alpha, 3 );
        else
            FrameRect( X( leftPx ), Y( midPx - halfPx ), X( leftPx + widthPx ), Y( midPx + halfPx ), 1, color, alpha );
        Text( X( leftPx + padPx ), Y( midPx ), sizePx, 500, textColor, alpha, text );
        return widthPx;
    }

    void LadderRow( Row const & row, REAL leftPx, REAL rightPx, REAL midPx, bool mine, REAL alpha )
    {
        char rank[ 16 ];
        snprintf( rank, sizeof( rank ), "%d", row.rank );
        Text( X( leftPx ), Y( midPx ), 15, 500, mine ? accent : textSecondary, mine ? alpha : .7f * alpha, rank );
        Text( X( leftPx + 46 ), Y( midPx ), 15, 500, textPrimary, alpha, row.name.c_str(), -1, W( 170 ) );
        if ( !row.badge.empty() )
            Chip( leftPx + 226, midPx, row.badge.c_str(), 11, false,
                  row.tier >= 0 ? sg_tierColor[ row.tier ] : textSecondary,
                  row.tier >= 0 ? sg_tierColor[ row.tier ] : textSecondary, alpha, 7, 9 );
        Text( X( rightPx ), Y( midPx ), 15, 500, textPrimary, alpha, row.score.c_str(), 1 );
    }

    void OpenPage( std::string const & path )
    {
        std::string const url = std::string( "https://" ) + sg_leagueHost + path;
#ifdef _WIN32
        ShellExecuteA( NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL );
#else
#ifdef MACOSX
        char const * const opener = "open";
#else
        char const * const opener = "xdg-open";
#endif
        // the opener runs in a grandchild, so nothing is left to wait for
        pid_t const child = fork();
        if ( child == 0 )
        {
            if ( fork() == 0 )
            {
                execlp( opener, opener, url.c_str(), static_cast< char * >( NULL ) );
            }
            _exit( 0 );
        }
        if ( child > 0 )
            waitpid( child, NULL, 0 );
#endif
    }
}

void gLeague::Poll()
{
    if ( !st_HttpsAvailable() )
        return;

    if ( sg_ladderShown < 0 || sg_ladderShown >= sg_ladderCount )
        sg_ladderShown = 0;

    if ( !sg_cacheRead )
    {
        st_HttpsInit();
        ReadCache();
    }

    if ( sg_fetch && sg_fetch->done.load( std::memory_order_acquire ) )
        TakeAnswer();

    if ( !sg_fetch )
    {
        time_t newest = 0;
        for ( int i = 0; i < Source_Count; ++i )
            if ( sg_bodyWhen[i] > newest )
                newest = sg_bodyWhen[i];
        double const age = static_cast< double >( time( NULL ) - newest );
        double const sinceAsked = tSysTimeFloat() - sg_lastAsked;
        if ( age > sg_freshFor && sinceAsked > ( sg_lastAnswered ? sg_freshFor : sg_retryAfter ) )
            Ask();
    }

    Rebuild();
}

void gLeague::DrawHome( REAL alpha )
{
    sg_drawn = false;
    if ( sg_ladderShown < 0 || sg_ladderShown >= sg_ladderCount )
        sg_ladderShown = 0;
    if ( !st_HttpsAvailable() || ( !sg_next.loaded && !sg_last.loaded && !sg_ladder[ sg_ladderShown ].loaded ) )
        return;

    // beside the rail, on the right; a window too small for both keeps the rail
    REAL const margin = MarginPx();
    REAL const railRight = margin + ColumnPx() + margin * .5f;
    REAL const widthPx = 420;
    REAL const right = LogicalWidth() - margin;
    REAL const left = right - widthPx;
    if ( left < railRight + 40 || LogicalHeight() < margin * .75f + 182 + 16 + 330 + 24 )
        return;
    sg_drawn = true;

    REAL const pad = 22;
    REAL top = margin * .75f;

    // ---- events ----
    {
        REAL const height = sg_last.loaded ? 182 : 140;
        FillRect( X( left ), Y( top ), X( right ), Y( top + height ), sg_cardColor, .88f * alpha, 8 );
        Text( X( left + pad ), Y( top + 22 ), 12, 500, textSecondary, .7f * alpha, "events" );

        sg_signupBox = Box();
        if ( sg_next.loaded )
        {
            Text( X( left + pad ), Y( top + 58 ), 26, 500, textPrimary, alpha, sg_next.title.c_str(), -1, W( widthPx - 2 * pad ) );
            std::string const when = StartText( sg_next.starts ) + "  \xb7  " + UntilText( sg_next.starts );
            Text( X( left + pad ), Y( top + 92 ), 14, 400, textSecondary, alpha, when.c_str() );

            char signups[ 64 ];
            snprintf( signups, sizeof( signups ), "%d signed up", sg_next.signups );
            std::string line = signups;
            if ( sg_next.open )
                line += "  \xb7  sign-ups open";
            Text( X( left + pad ), Y( top + 116 ), 14, 400, textSecondary, alpha, line.c_str() );

            if ( !sg_next.slug.empty() )
            {
                char const * const label = sg_next.open ? "sign up" : "details";
                REAL const chipW = TextWidth( label, 13, 500 ) / W( 1 ) + 24;
                REAL const chipLeft = right - pad - chipW;
                Chip( chipLeft, top + 114, label, 13, true, accent, onAccent, alpha, 12, 13 );
                sg_signupBox.left = chipLeft;
                sg_signupBox.right = chipLeft + chipW;
                sg_signupBox.top = top + 101;
                sg_signupBox.bottom = top + 127;
            }
        }
        else
        {
            Text( X( left + pad ), Y( top + 62 ), 18, 500, textSecondary, alpha, "no event announced yet" );
        }

        if ( sg_last.loaded )
        {
            REAL const lineY = top + ( sg_next.loaded ? 142 : 100 );
            FillRect( X( left + pad ), Y( lineY ), X( right - pad ), Y( lineY + 1 ), borderSubtle, alpha );
            std::string const last = "last  " + sg_last.title;
            Text( X( left + pad ), Y( lineY + 20 ), 13, 400, textSecondary, .7f * alpha, last.c_str(), -1, W( 190 ) );
            Text( X( right - pad ), Y( lineY + 20 ), 13, 500, textSecondary, alpha, sg_last.winner.c_str(), 1, W( 180 ) );
        }
        top += height + 16;
    }

    // ---- a ladder ----
    {
        Ladder const & ladder = sg_ladder[ sg_ladderShown ];
        REAL const height = 330;
        sg_boardBox = Box();
        sg_ladderBox.left = left;
        sg_ladderBox.right = right;
        sg_ladderBox.top = top;
        sg_ladderBox.bottom = top + height;
        FillRect( X( left ), Y( top ), X( right ), Y( top + height ), sg_cardColor, .88f * alpha, 8 );
        Text( X( left + pad ), Y( top + 22 ), 12, 500, textSecondary, .7f * alpha, "leaderboards" );

        REAL x = left + pad;
        for ( int i = 0; i < sg_ladderCount; ++i )
        {
            bool const on = i == sg_ladderShown;
            REAL const w = Chip( x, top + 54, sg_sourceName[ Source_Tst + i ], 13, true,
                                 on ? accent : surfaceRaised, on ? onAccent : textSecondary, alpha, 10, 12 );
            sg_tabBox[i].left = x;
            sg_tabBox[i].right = x + w;
            sg_tabBox[i].top = top + 40;
            sg_tabBox[i].bottom = top + 68;
            x += w + 6;
        }

        REAL y = top + 98;
        if ( !ladder.loaded )
        {
            Text( X( left + pad ), Y( y ), 14, 400, textSecondary, alpha, "not loaded yet" );
        }
        else
        {
            for ( size_t i = 0; i < ladder.top.size(); ++i, y += 34 )
                LadderRow( ladder.top[i], left + pad, right - pad, y, false, alpha );

            REAL const lineY = top + 98 + 5 * 34 - 14;
            FillRect( X( left + pad ), Y( lineY ), X( right - pad ), Y( lineY + 1 ), borderSubtle, alpha );
            REAL const mineY = lineY + 28;
            if ( ladder.hasMine )
            {
                FrameRect( X( left + 12 ), Y( mineY - 16 ), X( right - 12 ), Y( mineY + 16 ), 2, accent, alpha );
                Row mine = ladder.mine;
                mine.name += "  (you)";
                LadderRow( mine, left + pad, right - pad, mineY, true, alpha );
            }
            else
            {
                Text( X( left + pad ), Y( mineY ), 14, 400, textSecondary, .8f * alpha,
                      ePlayer::RclAuthenticated() ? "you are not on this ladder yet" : "sign in to see where you stand" );
            }

            char footer[ 96 ];
            snprintf( footer, sizeof( footer ), "%d players  \xb7  left and right change ladder", ladder.players );
            Text( X( left + pad ), Y( top + height - 20 ), 12, 400, textSecondary, .6f * alpha, footer );

            // the whole ladder is on the league's site
            char const * const more = "full board";
            REAL const moreW = TextWidth( more, 12, 500 ) / W( 1 );
            Text( X( right - pad ), Y( top + height - 20 ), 12, 500, accent, .9f * alpha, more, 1 );
            sg_boardBox.left = right - pad - moreW - 6;
            sg_boardBox.right = right - pad + 6;
            sg_boardBox.top = top + height - 32;
            sg_boardBox.bottom = top + height - 8;
        }
        top += height + 12;
    }

    // how old this is, when it is not fresh (no answer for a while: offline, or the site is down)
    time_t newest = 0;
    for ( int i = 0; i < Source_Count; ++i )
        if ( sg_bodyWhen[i] > newest )
            newest = sg_bodyWhen[i];
    if ( newest > 0 && time( NULL ) - newest > sg_staleShown )
        Text( X( right ), Y( top + 8 ), 12, 400, textSecondary, .6f * alpha, AgeText( newest ).c_str(), 1 );
}

bool gLeague::HomeKey( int key )
{
    if ( !sg_drawn )
        return false;
    if ( key == SDLK_LEFT || key == SDLK_RIGHT )
    {
        sg_ladderShown = ( sg_ladderShown + ( key == SDLK_RIGHT ? 1 : sg_ladderCount - 1 ) ) % sg_ladderCount;
        uMenu::MenuSound( uMenu::Sound_Adjust );
        return true;
    }
    return false;
}

bool gLeague::HomeClick( REAL x, REAL y )
{
    if ( !sg_drawn )
        return false;

    // field coordinates to logical pixels from the top left
    REAL const px = ( x + 1 ) / W( 1 );
    REAL const py = ( 1 - y ) / H( 1 );

    for ( int i = 0; i < sg_ladderCount; ++i )
        if ( sg_tabBox[i].Holds( px, py ) )
        {
            if ( i != sg_ladderShown )
            {
                sg_ladderShown = i;
                uMenu::MenuSound( uMenu::Sound_Adjust );
            }
            return true;
        }

    if ( sg_signupBox.right > sg_signupBox.left && sg_signupBox.Holds( px, py ) && !sg_next.slug.empty() )
    {
        uMenu::MenuSound( uMenu::Sound_Activate );
        OpenPage( "/event/" + sg_next.slug );
        return true;
    }

    if ( sg_boardBox.right > sg_boardBox.left && sg_boardBox.Holds( px, py ) )
    {
        uMenu::MenuSound( uMenu::Sound_Activate );
        OpenPage( "/leaderboard" );
        return true;
    }

    // the rest of the ladder's card is part of it, not a click through to the rail
    return sg_ladderBox.Holds( px, py );
}

void gLeague::OpenLeaderboard()
{
    OpenPage( "/leaderboard" );
}

void gLeague::OpenEvents()
{
    OpenPage( "/events" );
}

#else

void gLeague::Poll(){}
void gLeague::DrawHome( REAL ){}
bool gLeague::HomeKey( int ){ return false; }
bool gLeague::HomeClick( REAL, REAL ){ return false; }
void gLeague::OpenLeaderboard(){}
void gLeague::OpenEvents(){}

#endif
