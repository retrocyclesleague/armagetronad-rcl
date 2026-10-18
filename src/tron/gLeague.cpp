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
#include "tHttp.h"
#include "tHttps.h"
#include "tJson.h"
#include "tSysTime.h"
#include "uMenu.h"
#include "uRclTheme.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

// What the league's site says, as the menus show it. The site is asked on a
// thread of its own, which is handed the paths to ask and gives back their
// answers; nothing else crosses over. The answers are read, kept on disk and
// turned into what is drawn here, on the main thread.

namespace
{
    char const * const sg_leagueHost = "retrocyclesleague.com";

    // RCL_LEAGUE_PLAIN in the environment names a host:port that every request
    // of the league side goes to instead, over plain HTTP, and that its pages
    // open on: a test hook, for a stand-in on this machine. It is not a
    // setting, so that no configuration file a server can send has a say in it.
    std::string const & PlainHost()
    {
        static std::string const host( getenv( "RCL_LEAGUE_PLAIN" ) ? getenv( "RCL_LEAGUE_PLAIN" ) : "" );
        return host;
    }

    bool Available()
    {
        return st_HttpsAvailable() || !PlainHost().empty();
    }

    // what is asked for, in this order; the first five are the league's, the
    // last is the signed-in player's own
    enum Source
    {
        Source_Events,
        Source_Tst,
        Source_Sumobar,
        Source_Twos,
        Source_Fortress,
        Source_Progression,
        Source_Count
    };

    char const * const sg_sourceName[ Source_Count ] = { "events", "tst", "sumobar", "2s", "fortress", "progression" };

    // the ladders, in the order of the tabs; Source_Tst + index is where each comes from
    int const sg_ladderCount = 4;

    double const sg_freshFor = 300;     // seconds an answer is good for
    double const sg_retryAfter = 60;    // after no answer at all
    double const sg_staleShown = 900;   // older than this, the menus say how old

    // ---- what was read ----

    struct Row
    {
        int rank;
        std::string name, slug, badge, score, seen;
        int tier;           // index into sg_tierColor, -1 for none
        double value;       // the score as a number, for the gaps between rows
        int played;         // -1: not known
        double win;         // per cent; negative: not known
        bool hasDelta;
        int delta;

        Row(): rank( 0 ), tier( -1 ), value( 0 ), played( -1 ), win( -1 ), hasDelta( false ), delta( 0 ){}
    };

    struct Ladder
    {
        bool loaded;
        std::vector< Row > rows;
        int mine;           // index of the player's row, -1 for none

        Ladder(): loaded( false ), mine( -1 ){}
    };

    struct LeagueEvent
    {
        std::string title, slug, winner, kind, date;
        time_t starts;
        int signups;
        bool open;

        LeagueEvent(): starts( 0 ), signups( 0 ), open( false ){}
    };

    // the player's level and how far they are to the next
    struct Progress
    {
        bool loaded;
        std::string login;          // whose it is: the sign-in it was asked for, as the address carries it
        std::string name, slug, award;
        int level, xp, levelXp, nextXp, awardAmount;
        time_t awardWhen;

        Progress(): loaded( false ), level( 0 ), xp( 0 ), levelXp( 0 ), nextXp( 0 ), awardAmount( 0 ), awardWhen( 0 ){}
    };

    std::string sg_body[ Source_Count ];        // the last good answer from each
    time_t sg_bodyWhen[ Source_Count ];         // and when it came
    bool sg_cacheRead = false;

    // a progression is one player's: the sign-in its answer is for, and the
    // last one it was asked for, so that someone else's level is never shown
    std::string sg_progressFor;
    std::string sg_progressAskedFor;
    Progress sg_progress;

    Ladder sg_ladder[ sg_ladderCount ];
    bool sg_hasNext = false;
    LeagueEvent sg_next;
    std::vector< LeagueEvent > sg_past;
    int sg_pastTotal = 0;
    std::string sg_builtFor;                    // whom the ladders were read for
    bool sg_rebuild = true;
    int sg_generation = 0;                      // counts the times what is shown changed

    int sg_ladderShown = 0;
    tConfItem< int > sg_ladderShownConf( "RCL_HOME_LADDER", sg_ladderShown );

    // ---- the thread that asks ----

    struct Fetch
    {
        std::string path[ Source_Count ];       // empty: not asked this time
        int status[ Source_Count ];
        std::string body[ Source_Count ];
        std::string plain;                      // PlainHost(), for the thread
        std::string login;                      // whose progression is asked for
        bool everything;                        // the league's five are asked too
        std::atomic< bool > done;
        SDL_Thread * thread;

        Fetch(): everything( false ), done( false ), thread( NULL )
        {
            for ( int i = 0; i < Source_Count; ++i )
                status[i] = -1;
        }
    };

    Fetch * sg_fetch = NULL;
    double sg_lastAsked = -1E10;    // tSysTimeFloat() of the last request for the league's five
    bool sg_lastAnswered = false;   // and whether any of them answered

    int sg_FetchThread( void * data )
    {
        Fetch * const fetch = static_cast< Fetch * >( data );
        for ( int i = 0; i < Source_Count; ++i )
        {
            if ( fetch->path[i].empty() )
                continue;
            if ( fetch->plain.empty() )
                fetch->status[i] = st_HttpsGet( sg_leagueHost, fetch->path[i], fetch->body[i], 2 << 20, 8 );
            else
            {
                std::ostringstream body;
                fetch->status[i] = st_PlainHttpGet( fetch->plain, fetch->path[i], body, 2 << 20, 8 );
                fetch->body[i] = body.str();
            }
        }
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

    // keeps letters, digits, '-', '_' and '.': all a page's address may hold here
    std::string Slug( std::string const & slug )
    {
        std::string out;
        for ( size_t i = 0; i < slug.size() && out.size() < 64; ++i )
        {
            char const c = slug[i];
            if ( isalnum( static_cast< unsigned char >( c ) ) || c == '-' || c == '_' || c == '.' )
                out += c;
        }
        return out;
    }

    // "2026-11-01T18:00:00.000Z" or "2026-10-17 16:04:13+01" as seconds since 1970 (UTC; an offset is ignored)
    time_t When( std::string const & iso )
    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if ( sscanf( iso.c_str(), "%d-%d-%d", &y, &mo, &d ) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31 )
            return 0;
        if ( iso.size() > 11 )
            sscanf( iso.c_str() + 11, "%d:%d:%d", &h, &mi, &s );
        y -= mo <= 2;
        long const era = ( y >= 0 ? y : y - 399 ) / 400;
        long const yoe = y - era * 400;
        long const doy = ( 153L * ( mo + ( mo > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
        long const doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        long const days = era * 146097 + doe - 719468;
        return static_cast< time_t >( days ) * 86400 + h * 3600 + mi * 60 + s;
    }

    std::string SeenText( time_t when )
    {
        if ( when <= 0 )
            return std::string();
        long const days = static_cast< long >( ( time( NULL ) - when ) / 86400 );
        if ( days <= 0 )
            return "today";
        if ( days == 1 )
            return "yesterday";
        char buffer[ 32 ];
        snprintf( buffer, sizeof( buffer ), "%ldd ago", days );
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
        ladder.rows.reserve( rows->Size() );
        for ( size_t i = 0; i < rows->Size(); ++i )
        {
            tJson const & r = ( *rows )[ i ];
            Row row;
            int const place = static_cast< int >( i ) + 1;
            switch ( Source_Tst + index )
            {
            case Source_Tst:
                row.rank = r["leaderboardRank"].Int( place );
                row.name = Plain( r["username"].Str(), r["profileSlug"].Str() );
                row.slug = Slug( r["profileSlug"].Str().empty() ? r["username"].Str() : r["profileSlug"].Str() );
                row.badge = Badge( r["rankName"], r["rankDivision"] );
                row.tier = Tier( r["tier"].Str() );
                row.value = r["elo"].Number();
                row.score = Number( row.value );
                row.played = r["matches"].Int( -1 );
                row.win = r["winRate"].Number( -1 );
                row.hasDelta = r["ratingDelta"].Type() == tJson::Kind_Number;
                row.delta = r["ratingDelta"].Int();
                row.seen = Lower( Plain( r["lastActive"].Str() ) );
                break;
            case Source_Sumobar:
                row.rank = r["rank"].Int( place );
                row.name = Plain( r["player_name"].Str() );
                row.slug = Slug( r["player_name"].Str() );
                row.badge = Badge( r["rank_name"], r["division"] );
                row.tier = Tier( r["rank_name"].Str() );
                row.value = r["rating"].Number();
                row.score = Number( row.value );
                row.played = r["matches_played"].Int( -1 );
                row.win = r["win_rate_pct"].Number( -1 );
                row.hasDelta = r["rating_change"].Type() == tJson::Kind_Number;
                row.delta = r["rating_change"].Int();
                row.seen = SeenText( When( r["last_active"].Str() ) );
                break;
            case Source_Twos:
                row.rank = r["rank"].Int( place );
                row.name = Plain( r["player_name"].Str(), r["profile_slug"].Str() );
                row.slug = Slug( r["profile_slug"].Str().empty() ? r["player_name"].Str() : r["profile_slug"].Str() );
                row.value = r["rating"].Number();
                row.score = Number( row.value );
                row.played = r["matches_played"].Int( -1 );
                row.win = r["win_rate_pct"].Number( -1 );
                row.hasDelta = r["rating_change"].Type() == tJson::Kind_Number;
                row.delta = r["rating_change"].Int();
                row.seen = SeenText( When( r["last_active"].Str() ) );
                break;
            case Source_Fortress:
            {
                row.rank = r["rank"].Int( place );
                row.name = Plain( r["nickname"].Str(), r["profileSlug"].Str() );
                row.slug = Slug( r["profileSlug"].Str().empty() ? r["nickname"].Str() : r["profileSlug"].Str() );
                row.badge = Badge( r["rankName"], r["division"] );
                row.tier = Tier( r["tier"].Str() );
                row.value = r["lp"].Number();
                row.score = Number( row.value, " lp" );
                int const wins = r["wins"].Int(), losses = r["losses"].Int();
                row.played = wins + losses;
                row.win = row.played > 0 ? 100.0 * wins / row.played : -1;
                // the points of the last few games, added up
                tJson const & recent = r["recentLpDeltas"];
                row.hasDelta = recent.Size() > 0;
                for ( size_t k = 0; k < recent.Size(); ++k )
                    row.delta += recent[ k ].Int();
                row.seen = SeenText( When( r["lastMatchAt"].Str() ) );
                break;
            }
            }
            if ( ladder.mine < 0 && IsMine( me, r, names ) )
                ladder.mine = static_cast< int >( ladder.rows.size() );
            ladder.rows.push_back( row );
        }
        sg_ladder[ index ] = ladder;
    }

    std::string Kind( std::string const & type )
    {
        if ( type == "sbt" )
            return "sumobar";
        return Lower( Plain( type, std::string(), 16 ) );
    }

    std::string DateText( time_t when )
    {
        static char const * const month[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
        tm const * const utc = gmtime( &when );
        if ( !utc )
            return std::string();
        char buffer[ 32 ];
        snprintf( buffer, sizeof( buffer ), "%d %s %d", utc->tm_mday, month[ utc->tm_mon % 12 ], utc->tm_year + 1900 );
        return buffer;
    }

    LeagueEvent ReadEvent( tJson const & e )
    {
        LeagueEvent event;
        event.title = Lower( Plain( e["title"].Str(), std::string(), 60 ) );
        event.slug = Slug( e["slug"].Str() );
        event.kind = Kind( e["eventType"].Str() );
        event.starts = When( e["startsAt"].Str() );
        event.date = event.starts > 0 ? DateText( event.starts ) : Lower( Plain( e["dateText"].Str(), std::string(), 24 ) );
        event.signups = e["signupCount"].Int();
        event.open = e["signupOpen"].Bool();
        // a team's winners come a line each
        std::string line;
        std::istringstream lines( e["winnerText"].Str() );
        while ( std::getline( lines, line ) )
        {
            line = Plain( line, std::string(), 40 );
            if ( line.empty() )
                continue;
            if ( !event.winner.empty() )
                event.winner += " & ";
            event.winner += line;
        }
        event.winner = Lower( event.winner );
        return event;
    }

    void ReadEvents()
    {
        sg_hasNext = false;
        sg_past.clear();
        sg_pastTotal = 0;
        tJson doc;
        if ( !tJson::Parse( sg_body[ Source_Events ], doc ) )
            return;

        tJson const & up = doc["upcoming"][ 0 ];
        if ( up.Type() == tJson::Kind_Object )
        {
            sg_hasNext = true;
            sg_next = ReadEvent( up );
        }
        tJson const & past = doc["past"];
        for ( size_t i = 0; i < past.Size() && i < 100; ++i )
            sg_past.push_back( ReadEvent( past[ i ] ) );
        sg_pastTotal = doc["pastTotal"].Int( static_cast< int >( sg_past.size() ) );
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

    // the signed-in player's sign-in ("syn@rcl") as an address carries it
    // ("syn%40rcl"); empty when nobody is signed in
    std::string Login()
    {
        if ( !ePlayer::RclAuthenticated() )
            return std::string();
        tString const identity = ePlayer::RclIdentity();
        std::string const raw( static_cast< char const * >( identity ) );
        static char const * const hex = "0123456789ABCDEF";
        std::string out;
        for ( size_t i = 0; i < raw.size(); ++i )
        {
            unsigned char const c = raw[i];
            if ( ( c < 0x80 && isalnum( c ) ) || c == '-' || c == '_' || c == '.' || c == '~' )
                out += static_cast< char >( c );
            else
            {
                out += '%';
                out += hex[ c >> 4 ];
                out += hex[ c & 15 ];
            }
        }
        return out;
    }

    // a number from the site as a whole number that cannot overflow
    int Whole( tJson const & value )
    {
        double const n = value.Number();
        return n > 1E9 ? 1000000000 : n < -1E9 ? -1000000000 : static_cast< int >( n );
    }

    void ReadProgress( std::string const & login )
    {
        sg_progress = Progress();
        tJson doc;
        // only an answer for whoever is signed in now, and only one with a level
        if ( login.empty() || login != sg_progressFor || !tJson::Parse( sg_body[ Source_Progression ], doc ) ||
             doc["level"].Type() != tJson::Kind_Number )
            return;

        Progress & p = sg_progress;
        p.loaded = true;
        p.login = login;
        p.name = doc["player"]["name"].Str();      // as the site wrote it: drawn only through Plain()
        p.slug = Slug( doc["player"]["slug"].Str() );
        p.level = Whole( doc["level"] );
        p.xp = Whole( doc["xp"] );
        p.levelXp = Whole( doc["levelXp"] );
        p.nextXp = Whole( doc["nextLevelXp"] );

        // the newest award: the time says which, whatever the order
        tJson const & recent = doc["recent"];
        if ( recent.Type() != tJson::Kind_Array )
            return;
        int newest = -1;
        for ( size_t i = 0; i < recent.Size() && i < 100; ++i )
        {
            time_t const when = When( recent[ i ]["earnedAt"].Str() );
            if ( newest < 0 || when > p.awardWhen )
            {
                newest = static_cast< int >( i );
                p.awardWhen = when;
            }
        }
        if ( newest >= 0 )
        {
            tJson const & award = recent[ newest ];
            p.award = Lower( Plain( award["label"].Str(), award["source"].Str(), 40 ) );
            p.awardAmount = Whole( award["amount"] );
        }
    }

    void Rebuild()
    {
        std::vector< std::string > const me = Me();
        std::string const login = Login();
        std::string key = login + "\n";
        for ( size_t i = 0; i < me.size(); ++i )
            key += me[i] + "\n";
        if ( !sg_rebuild && key == sg_builtFor )
            return;
        sg_builtFor = key;
        sg_rebuild = false;

        ReadEvents();
        for ( int i = 0; i < sg_ladderCount; ++i )
            ReadLadder( i, me );
        ReadProgress( login );
        ++sg_generation;
    }

    // the player's progression, when it is theirs
    bool HasProgress()
    {
        return sg_progress.loaded && sg_progress.login == Login();
    }

    bool AnyLoaded()
    {
        if ( sg_hasNext || !sg_past.empty() )
            return true;
        for ( int i = 0; i < sg_ladderCount; ++i )
            if ( sg_ladder[i].loaded )
                return true;
        return false;
    }

    // how old the league's data is; the player's own progression does not count
    time_t Newest()
    {
        time_t newest = 0;
        for ( int i = 0; i < Source_Progression; ++i )
            if ( sg_bodyWhen[i] > newest )
                newest = sg_bodyWhen[i];
        return newest;
    }

    // ---- kept on disk: one file per answer, its first line the time it came ----

    tString CacheName( int source )
    {
        // what a stand-in answered is never taken for the site's
        tString name( PlainHost().empty() ? "league-" : "league-test-" );
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
            if ( i == Source_Progression )
            {
                // its first line also says whose it is
                std::string::size_type const space = first.find( ' ' );
                if ( space == std::string::npos )
                    continue;
                std::string & whose = sg_progressFor;
                whose = first.substr( space + 1 );
                while ( !whose.empty() && isspace( static_cast< unsigned char >( whose[ whose.size() - 1 ] ) ) )
                    whose.erase( whose.size() - 1 );
            }
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
        if ( !out )
            return;
        out << static_cast< long long >( sg_bodyWhen[ source ] );
        if ( source == Source_Progression )
            out << " " << sg_progressFor;
        out << "\n" << sg_body[ source ];
    }

    // asks for the league's five when everything, and for the progression of
    // login when it is not empty
    void Ask( bool everything, std::string const & login )
    {
        Fetch * const fetch = new Fetch;
        fetch->plain = PlainHost();
        fetch->everything = everything;
        fetch->login = login;
        if ( everything )
        {
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
        }
        if ( !login.empty() )
        {
            fetch->path[ Source_Progression ] = "/api/v1/progression?login=" + login;
            sg_progressAskedFor = login;
        }

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
        if ( sg_fetch->everything )
            sg_lastAnswered = false;
        time_t const now = time( NULL );
        for ( int i = 0; i < Source_Count; ++i )
        {
            if ( i == Source_Progression && sg_fetch->status[i] == 404 )
            {
                // the site does not know this player: nothing of theirs is kept
                sg_body[i].clear();
                sg_bodyWhen[i] = now;
                sg_progressFor = sg_fetch->login;
                WriteCache( i );
                continue;
            }

            // an answer that cannot be read keeps the last one that could
            tJson check;
            if ( sg_fetch->status[i] != 200 || !tJson::Parse( sg_fetch->body[i], check ) )
                continue;
            if ( i == Source_Progression )
            {
                // and a progression without a level is no progression
                if ( check["level"].Type() != tJson::Kind_Number )
                    continue;
                sg_progressFor = sg_fetch->login;
            }
            else
                sg_lastAnswered = true;
            sg_body[i].swap( sg_fetch->body[i] );
            sg_bodyWhen[i] = now;
            WriteCache( i );
        }
        delete sg_fetch;
        sg_fetch = NULL;
        sg_rebuild = true;
    }

    void OpenPage( std::string const & path )
    {
        std::string const url = PlainHost().empty() ? std::string( "https://" ) + sg_leagueHost + path
                                                    : "http://" + PlainHost() + path;
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
    uRclTheme::Color const sg_quiet = { .478f, .486f, .494f };
    uRclTheme::Color const sg_up = { .47f, .86f, .55f };
    uRclTheme::Color const sg_down = { .94f, .47f, .43f };

    char const * const sg_weekday[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    char const * const sg_month[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

    // where things are this frame, in logical pixels, for the mouse
    struct Box
    {
        REAL left, top, right, bottom;
        Box(): left( 0 ), top( 0 ), right( 0 ), bottom( 0 ){}
        bool Holds( REAL x, REAL y ) const { return right > left && x >= left && x <= right && y >= top && y <= bottom; }
    };
    Box sg_tileBox[ sg_ladderCount ];
    Box sg_signupBox;
    Box sg_progressBox;
    bool sg_drawn = false;

    // field coordinates to logical pixels from the top left
    REAL PxX( REAL x ) { return ( x + 1 ) / W( 1 ); }
    REAL PxY( REAL y ) { return ( 1 - y ) / H( 1 ); }

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

    std::string DeltaText( Row const & row, char const * unit = "" )
    {
        if ( !row.hasDelta )
            return std::string();
        char buffer[ 32 ];
        if ( row.delta == 0 )
            snprintf( buffer, sizeof( buffer ), "\xb1" "0" );
        else
            snprintf( buffer, sizeof( buffer ), "%+d%s", row.delta, unit );
        return buffer;
    }

    uRclTheme::Color const & DeltaColor( Row const & row )
    {
        return row.delta > 0 ? sg_up : row.delta < 0 ? sg_down : sg_quiet;
    }

    uRclTheme::Color const & TierColor( Row const & row )
    {
        return row.tier >= 0 ? sg_tierColor[ row.tier ] : textSecondary;
    }

    REAL WidthPx( char const * text, REAL sizePx, int weight )
    {
        return TextWidth( text, sizePx, weight ) / W( 1 );
    }

    // a chip: a filled or outlined small box with a word in it; returns its width
    REAL Chip( REAL leftPx, REAL midPx, char const * text, REAL sizePx, bool filled,
               uRclTheme::Color const & color, uRclTheme::Color const & textColor, REAL alpha, REAL padPx, REAL halfPx )
    {
        REAL const widthPx = WidthPx( text, sizePx, 500 ) + 2 * padPx;
        if ( filled )
            FillRect( X( leftPx ), Y( midPx - halfPx ), X( leftPx + widthPx ), Y( midPx + halfPx ), color, alpha, 3 );
        else
            FrameRect( X( leftPx ), Y( midPx - halfPx ), X( leftPx + widthPx ), Y( midPx + halfPx ), 1, color, alpha );
        Text( X( leftPx + padPx ), Y( midPx ), sizePx, 500, textColor, alpha, text );
        return widthPx;
    }

    // one ladder's tile on the main menu: where the player stands, and who is next to them
    void DrawTile( int index, REAL left, REAL top, REAL width, REAL height, REAL alpha )
    {
        Ladder const & ladder = sg_ladder[ index ];
        REAL const pad = 20, right = left + width;
        FillRect( X( left ), Y( top ), X( right ), Y( top + height ), sg_cardColor, .88f * alpha, 8 );
        Text( X( left + pad ), Y( top + 22 ), 13, 500, textSecondary, alpha, sg_sourceName[ Source_Tst + index ] );
        sg_tileBox[ index ].left = left;
        sg_tileBox[ index ].right = right;
        sg_tileBox[ index ].top = top;
        sg_tileBox[ index ].bottom = top + height;

        if ( !ladder.loaded )
        {
            Text( X( left + pad ), Y( top + 70 ), 16, 500, sg_quiet, alpha, "not loaded yet" );
            return;
        }
        char players[ 32 ];
        snprintf( players, sizeof( players ), "%d players", static_cast< int >( ladder.rows.size() ) );
        Text( X( right - pad ), Y( top + 22 ), 12, 400, sg_quiet, alpha, players, 1 );

        int first = 0;
        if ( ladder.mine >= 0 )
        {
            Row const & me = ladder.rows[ ladder.mine ];
            char rank[ 16 ];
            snprintf( rank, sizeof( rank ), "#%d", me.rank );
            Text( X( left + pad ), Y( top + 66 ), 40, 600, accent, alpha, rank );
            REAL const at = left + pad + WidthPx( rank, 40, 600 ) + 12;
            Text( X( at ), Y( top + 58 ), 18, 500, textPrimary, alpha, me.score.c_str() );
            if ( !me.badge.empty() )
                Chip( at, top + 84, me.badge.c_str(), 11, false, TierColor( me ), TierColor( me ), alpha, 7, 9 );
            bool const fortress = Source_Tst + index == Source_Fortress;
            std::string const delta = DeltaText( me, fortress ? " lp" : "" );
            if ( !delta.empty() )
            {
                Text( X( right - pad ), Y( top + 58 ), 18, 600, DeltaColor( me ), alpha, delta.c_str(), 1 );
                Text( X( right - pad ), Y( top + 82 ), 11, 400, sg_quiet, alpha, fortress ? "last 5 games" : "change", 1 );
            }
            first = std::max( 0, std::min( ladder.mine - 1, static_cast< int >( ladder.rows.size() ) - 3 ) );
        }
        else
        {
            Text( X( left + pad ), Y( top + 66 ), 16, 500, sg_quiet, alpha,
                  ePlayer::RclAuthenticated() ? "not ranked here yet" : "sign in to see your place" );
        }

        // the rows around the player, or the top of the ladder
        FillRect( X( left + pad ), Y( top + 110 ), X( right - pad ), Y( top + 111 ), borderSubtle, alpha );
        REAL y = top + 130;
        Row const * mine = ladder.mine >= 0 ? &ladder.rows[ ladder.mine ] : NULL;
        for ( int i = first; i < first + 3 && i < static_cast< int >( ladder.rows.size() ); ++i, y += 22 )
        {
            Row const & row = ladder.rows[ i ];
            bool const you = i == ladder.mine;
            char rank[ 16 ];
            snprintf( rank, sizeof( rank ), "%d", row.rank );
            Text( X( left + pad ), Y( y ), 13, 500, you ? accent : sg_quiet, alpha, rank );
            std::string name = row.name;
            if ( you )
                name += "  (you)";
            Text( X( left + pad + 36 ), Y( y ), 13, you ? 500 : 400, you ? textPrimary : textSecondary, alpha,
                  name.c_str(), -1, W( width - 2 * pad - 36 - 110 ) );
            Text( X( right - pad ), Y( y ), 13, you ? 500 : 400, you ? textPrimary : textSecondary, alpha, row.score.c_str(), 1 );
            if ( mine && !you )
            {
                // how far the player is from them
                int const difference = static_cast< int >( row.value - mine->value + ( row.value >= mine->value ? .5 : -.5 ) );
                if ( difference != 0 )
                {
                    char gap[ 24 ];
                    snprintf( gap, sizeof( gap ), "%+d", difference );
                    Text( X( right - pad - 62 ), Y( y ), 13, 400, sg_quiet, alpha, gap, 1 );
                }
            }
        }
    }

    // how long ago, shortly: "2 h ago"
    std::string AgoText( time_t when )
    {
        if ( when <= 0 )
            return std::string();
        long const age = static_cast< long >( time( NULL ) - when );
        char buffer[ 32 ];
        if ( age < 60 )
            return "just now";
        if ( age < 3600 )
            snprintf( buffer, sizeof( buffer ), "%ld min ago", age / 60 );
        else if ( age < 2 * 86400 )
            snprintf( buffer, sizeof( buffer ), "%ld h ago", age / 3600 );
        else
            snprintf( buffer, sizeof( buffer ), "%ld days ago", age / 86400 );
        return buffer;
    }

    // the player's level, in a strip of its own: the number, the way to the
    // next one, and the last thing that earned them some
    void DrawProgress( REAL left, REAL top, REAL width, REAL height, REAL alpha )
    {
        Progress const & p = sg_progress;
        REAL const pad = 22, right = left + width, mid = top + height * .5f;
        FillRect( X( left ), Y( top ), X( right ), Y( top + height ), sg_cardColor, .88f * alpha, 8 );
        if ( !p.slug.empty() )
        {
            sg_progressBox.left = left;
            sg_progressBox.right = right;
            sg_progressBox.top = top;
            sg_progressBox.bottom = top + height;
        }

        // "level 23", the number large, the two on one baseline (a text's lies
        // about a third of its size below the middle it is drawn at)
        Text( X( left + pad ), Y( mid + 6 ), 13, 500, textSecondary, alpha, "level" );
        REAL x = left + pad + WidthPx( "level", 13, 500 ) + 8;
        char level[ 16 ];
        snprintf( level, sizeof( level ), "%d", p.level );
        Text( X( x ), Y( mid ), 30, 600, accent, alpha, level );
        x += WidthPx( level, 30, 600 ) + 20;

        // who, over the bar from this level to the next: the name the client
        // knows them by, else the site's
        tString const display = ePlayer::RclDisplayName();
        std::string name = Plain( std::string( static_cast< char const * >( display ) ), p.name.empty() ? p.slug : p.name );
        if ( name.empty() )
        {
            tString const identity = ePlayer::RclIdentity();
            int const at = identity.StrPos( "@" );
            name = Plain( std::string( static_cast< char const * >( at > 0 ? identity.SubStr( 0, at ) : identity ) ) );
        }
        REAL const barWidth = 130, barTop = top + 37;
        int const span = p.nextXp - p.levelXp;
        char xp[ 48 ];
        if ( span > 0 )
            snprintf( xp, sizeof( xp ), "%d / %d xp", std::max( 0, std::min( p.xp - p.levelXp, span ) ), span );
        else
            snprintf( xp, sizeof( xp ), "%d xp", p.xp );
        REAL const xpRight = x + barWidth + 10 + WidthPx( xp, 12, 400 );
        Text( X( x ), Y( top + 20 ), 15, 500, textPrimary, alpha, name.c_str(), -1, W( xpRight - x ) );
        FillRect( X( x ), Y( barTop ), X( x + barWidth ), Y( barTop + 4 ), surfaceRaised, alpha );
        if ( span > 0 && p.xp > p.levelXp )
            FillRect( X( x ), Y( barTop ), X( x + barWidth * std::min( REAL( 1 ), REAL( p.xp - p.levelXp ) / span ) ),
                      Y( barTop + 4 ), accent, alpha );
        Text( X( x + barWidth + 10 ), Y( barTop + 2 ), 12, 400, textSecondary, alpha, xp );

        // the newest award, quietly, on the right, where there is room
        if ( p.awardAmount != 0 || !p.award.empty() )
        {
            char amount[ 24 ];
            snprintf( amount, sizeof( amount ), "%+d", p.awardAmount );
            std::string const award = std::string( amount ) + " " + ( p.award.empty() ? std::string( "xp" ) : p.award );
            REAL const room = right - pad - ( xpRight + 24 );
            if ( room > 40 )
            {
                Text( X( right - pad ), Y( top + 21 ), 12, 400, sg_quiet, alpha, award.c_str(), 1, W( room ) );
                Text( X( right - pad ), Y( barTop + 2 ), 11, 400, sg_quiet, alpha, AgoText( p.awardWhen ).c_str(), 1 );
            }
        }
    }

    // ---- the pages: rows of a table ----

    // the columns of a page, as parts of the table's width, from its left text edge
    REAL Column( REAL part )
    {
        REAL const left = PxX( LabelX() );
        REAL const right = PxX( RowRight() ) - 16;
        return left + ( right - left ) * part;
    }

    void ColumnText( REAL part, REAL y, char const * text, REAL size, int weight, uRclTheme::Color const & color,
                     REAL alpha, int align = -1, REAL widthPart = 0 )
    {
        REAL const maxWidth = widthPart > 0 ? W( Column( widthPart ) - Column( 0 ) ) : 0;
        Text( X( Column( part ) ), y, size, weight, color, alpha, text, align, maxWidth );
    }

    void StaleNote( REAL alpha )
    {
        time_t const newest = Newest();
        if ( newest > 0 && time( NULL ) - newest > sg_staleShown )
            Text( X( LogicalWidth() - MarginPx() ), Y( 54 ), 12, 400, sg_quiet, alpha, AgeText( newest ).c_str(), 1 );
    }

    void PageTabs( char const * title, REAL alpha, Box * boxes )
    {
        REAL x = MarginPx() + WidthPx( title, 22, 500 ) + 32;
        for ( int i = 0; i < sg_ladderCount; ++i )
        {
            bool const on = i == sg_ladderShown;
            REAL const w = Chip( x, 54, sg_sourceName[ Source_Tst + i ], 14, true,
                                 on ? accent : surfaceRaised, on ? onAccent : textSecondary, alpha, 14, 15 );
            if ( boxes )
            {
                boxes[i].left = x;
                boxes[i].right = x + w;
                boxes[i].top = 39;
                boxes[i].bottom = 69;
            }
            x += w + 8;
        }
    }

    // column parts of the leaderboard
    REAL const sg_cRank = 0, sg_cName = .045f, sg_cBadge = .30f, sg_cScore = .50f, sg_cDelta = .60f,
               sg_cPlayed = .72f, sg_cWin = .84f, sg_cSeen = 1.0f;

    void BoardRow( Row const & row, bool you, REAL y, REAL alpha, bool selected )
    {
        uRclTheme::Color const & text = selected || you ? textPrimary : textSecondary;
        char buffer[ 64 ];
        snprintf( buffer, sizeof( buffer ), "%d", row.rank );
        ColumnText( sg_cRank, y, buffer, 15, 500, you ? accent : sg_quiet, alpha );
        std::string name = row.name;
        if ( you )
            name += "  (you)";
        ColumnText( sg_cName, y, name.c_str(), 15, 500, textPrimary, alpha, -1, sg_cBadge - sg_cName - .01f );
        if ( !row.badge.empty() )
            Chip( Column( sg_cBadge ), PxY( y ), row.badge.c_str(), 11, false, TierColor( row ), TierColor( row ), alpha, 7, 9 );
        ColumnText( sg_cScore, y, row.score.c_str(), 15, 500, textPrimary, alpha, 1 );
        std::string const delta = DeltaText( row );
        if ( !delta.empty() )
            ColumnText( sg_cDelta, y, delta.c_str(), 15, 500, DeltaColor( row ), alpha, 1 );
        if ( row.played >= 0 )
        {
            snprintf( buffer, sizeof( buffer ), "%d", row.played );
            ColumnText( sg_cPlayed, y, buffer, 15, 400, text, alpha, 1 );
        }
        if ( row.win >= 0 )
        {
            snprintf( buffer, sizeof( buffer ), "%.0f%%", row.win );
            ColumnText( sg_cWin, y, buffer, 15, 400, text, alpha, 1 );
        }
        if ( !row.seen.empty() )
            ColumnText( sg_cSeen, y, row.seen.c_str(), 15, 400, text, alpha, 1 );
    }

    // a row of a page that only says something
    class gLeagueNoteItem: public uMenuItem
    {
        tString text_;
    public:
        gLeagueNoteItem( uMenu * menu, char const * text ): uMenuItem( menu, tOutput() ), text_( text ){}
        virtual void Render( REAL, REAL y, REAL alpha, bool )
        {
            Text( LabelX(), y, 15, 400, sg_quiet, alpha, text_ );
        }
    };

    class gLeagueBoardMenu;

    class gLeagueRowItem: public uMenuItem
    {
        int index_;
    public:
        gLeagueRowItem( uMenu * menu, int index ): uMenuItem( menu, tOutput() ), index_( index ){}
        int Index() const { return index_; }

        virtual void Render( REAL, REAL y, REAL alpha, bool selected )
        {
            Ladder const & ladder = sg_ladder[ sg_ladderShown ];
            if ( index_ < static_cast< int >( ladder.rows.size() ) )
                BoardRow( ladder.rows[ index_ ], index_ == ladder.mine, y, alpha, selected );
        }

        virtual void Enter()
        {
            Ladder const & ladder = sg_ladder[ sg_ladderShown ];
            if ( index_ < static_cast< int >( ladder.rows.size() ) && !ladder.rows[ index_ ].slug.empty() )
            {
                uMenu::MenuSound( uMenu::Sound_Activate );
                OpenPage( "/player/" + ladder.rows[ index_ ].slug );
            }
        }
    };

    class gLeagueBoardMenu: public uMenu
    {
        std::vector< uMenuItem * > rows_;
        int generation_;
        int filledFor_;
        Box tabs_[ sg_ladderCount ];

        void Clear()
        {
            for ( size_t i = 0; i < rows_.size(); ++i )
                delete rows_[i];
            rows_.clear();
        }

        // the row index a menu position stands for: rows are kept bottom first
        int RowAt( int position ) const
        {
            return items.Len() - 1 - position;
        }

    public:
        gLeagueBoardMenu(): uMenu( tOutput( "$rcl_leaderboards_text" ), false ), generation_( -1 ), filledFor_( -1 )
        {
            SetStyle( uMenuStyle_RclFull );
            SetKeyHint( tString( "left right  ladder  \xb7  up down  move  \xb7  enter  their page on the site  \xb7  tab  you  \xb7  esc  back" ) );
            Fill( 0 );
        }

        ~gLeagueBoardMenu()
        {
            Clear();
        }

        //! makes the rows of the ladder shown anew, with the row at top first in view
        void Fill( int top )
        {
            Clear();
            Ladder const & ladder = sg_ladder[ sg_ladderShown ];
            if ( !ladder.loaded || ladder.rows.empty() )
                rows_.push_back( new gLeagueNoteItem( this, ladder.loaded ? "nobody on this ladder yet" :
                                                      "not loaded yet: the league's site has not answered" ) );
            else
                for ( int i = static_cast< int >( ladder.rows.size() ) - 1; i >= 0; --i )
                    rows_.push_back( new gLeagueRowItem( this, i ) );
            generation_ = sg_generation;
            filledFor_ = sg_ladderShown;
            SetSelected( std::max( 0, items.Len() - 1 - std::max( 0, std::min( top, items.Len() - 1 ) ) ) );
        }

        void ShowLadder( int ladder )
        {
            sg_ladderShown = ( ladder + sg_ladderCount ) % sg_ladderCount;
            uMenu::MenuSound( uMenu::Sound_Adjust );
            Fill( 0 );
        }

    protected:
        virtual void OnRender()
        {
            uMenu::OnRender();
            gLeague::Poll();
            if ( generation_ != sg_generation || filledFor_ != sg_ladderShown )
            {
                // new answers: the same row stays selected
                int const keep = selected >= 0 && selected < items.Len() ? RowAt( selected ) : 0;
                Fill( keep );
            }
        }

        virtual void RenderExtras( REAL alpha )
        {
            PageTabs( "leaderboards", alpha, tabs_ );
            StaleNote( alpha );

            REAL const header = MenuTop() + H( 12 );
            ColumnText( sg_cRank, header, "#", 12, 500, sg_quiet, alpha );
            ColumnText( sg_cName, header, "player", 12, 500, sg_quiet, alpha );
            ColumnText( sg_cBadge, header, "rank", 12, 500, sg_quiet, alpha );
            ColumnText( sg_cScore, header, "rating", 12, 500, sg_quiet, alpha, 1 );
            ColumnText( sg_cDelta, header, sg_ladderShown == 3 ? "last 5" : "change", 12, 500, sg_quiet, alpha, 1 );
            ColumnText( sg_cPlayed, header, "matches", 12, 500, sg_quiet, alpha, 1 );
            ColumnText( sg_cWin, header, "win rate", 12, 500, sg_quiet, alpha, 1 );
            ColumnText( sg_cSeen, header, "seen", 12, 500, sg_quiet, alpha, 1 );

            // the foot: the selected player, and the player's own place
            Ladder const & ladder = sg_ladder[ sg_ladderShown ];
            REAL const foot = LogicalHeight() - 150;
            int const row = selected >= 0 && selected < items.Len() ? RowAt( selected ) : -1;
            if ( ladder.loaded && row >= 0 && row < static_cast< int >( ladder.rows.size() ) )
            {
                Row const & r = ladder.rows[ row ];
                REAL const left = PxX( LabelX() );
                Text( X( left ), Y( foot ), 20, 500, textPrimary, alpha, r.name.c_str(), -1, W( 420 ) );
                if ( !r.badge.empty() )
                    Chip( left + std::min( WidthPx( r.name.c_str(), 20, 500 ), REAL( 420 ) ) + 14, foot, r.badge.c_str(), 12,
                          false, TierColor( r ), TierColor( r ), alpha, 8, 10 );
                std::ostringstream line;
                line << "#" << r.rank << "  \xb7  " << r.score;
                if ( r.played >= 0 )
                    line << "  \xb7  " << r.played << " matches";
                if ( r.win >= 0 )
                    line << "  \xb7  " << static_cast< int >( r.win + .5 ) << "% won";
                if ( !r.seen.empty() )
                    line << "  \xb7  seen " << r.seen;
                Text( X( left ), Y( foot + 32 ), 14, 400, textSecondary, alpha, line.str().c_str() );
                if ( !r.slug.empty() )
                    Text( X( left ), Y( foot + 56 ), 13, 400, sg_quiet, alpha, "enter opens their page on the league's site" );
            }

            REAL const right = PxX( RowRight() ) - 16;
            char summary[ 96 ];
            snprintf( summary, sizeof( summary ), "%d players", static_cast< int >( ladder.rows.size() ) );
            Text( X( right ), Y( foot ), 13, 400, textSecondary, alpha, summary, 1 );
            if ( ladder.mine >= 0 )
            {
                Row const & me = ladder.rows[ ladder.mine ];
                std::string const you = "you  #" + Number( me.rank ) + "  \xb7  " + me.score + "  \xb7  tab goes there";
                Text( X( right ), Y( foot + 32 ), 14, 500, accent, alpha, you.c_str(), 1 );
            }
            else if ( !ePlayer::RclAuthenticated() )
                Text( X( right ), Y( foot + 32 ), 13, 400, sg_quiet, alpha, "sign in to see your place", 1 );
        }

        virtual void HandleEvent( SDL_Event event )
        {
            if ( event.type == SDL_KEYDOWN )
            {
                switch ( event.key.keysym.sym )
                {
                case SDLK_LEFT:
                    ShowLadder( sg_ladderShown - 1 );
                    return;
                case SDLK_RIGHT:
                    ShowLadder( sg_ladderShown + 1 );
                    return;
                case SDLK_TAB:
                {
                    int const mine = sg_ladder[ sg_ladderShown ].mine;
                    if ( mine >= 0 && rows_.size() > 1 )
                    {
                        SetSelected( items.Len() - 1 - mine );
                        uMenu::MenuSound( uMenu::Sound_Move );
                    }
                    return;
                }
                default:
                    break;
                }
            }
            if ( event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
                 sr_screenWidth > 0 && sr_screenHeight > 0 )
            {
                REAL const px = PxX( 2.0f * event.button.x / sr_screenWidth - 1.0f );
                REAL const py = PxY( 1.0f - 2.0f * event.button.y / sr_screenHeight );
                for ( int i = 0; i < sg_ladderCount; ++i )
                    if ( tabs_[i].Holds( px, py ) )
                    {
                        if ( i != sg_ladderShown )
                            ShowLadder( i );
                        return;
                    }
            }
            uMenu::HandleEvent( event );
        }
    };

    // ---- the events page ----

    REAL const sg_eDate = 0, sg_eTitle = .12f, sg_eKind = .52f, sg_ePlayers = .66f, sg_eWinner = .70f;

    class gLeagueEventItem: public uMenuItem
    {
        int index_;     // into sg_past; -1 for the next event
    public:
        gLeagueEventItem( uMenu * menu, int index ): uMenuItem( menu, tOutput() ), index_( index ){}
        int Index() const { return index_; }

        LeagueEvent const * Shown() const
        {
            if ( index_ < 0 )
                return sg_hasNext ? &sg_next : NULL;
            return index_ < static_cast< int >( sg_past.size() ) ? &sg_past[ index_ ] : NULL;
        }

        virtual void Render( REAL, REAL y, REAL alpha, bool selected )
        {
            LeagueEvent const * const e = Shown();
            if ( !e )
                return;
            bool const next = index_ < 0;
            uRclTheme::Color const & text = selected || next ? textPrimary : textSecondary;
            ColumnText( sg_eDate, y, next ? "next" : e->date.c_str(), 15, next ? 600 : 400, next ? accent : text, alpha, -1,
                        sg_eTitle - sg_eDate - .01f );
            ColumnText( sg_eTitle, y, e->title.c_str(), 15, 500, textPrimary, alpha, -1, sg_eKind - sg_eTitle - .02f );
            ColumnText( sg_eKind, y, e->kind.c_str(), 15, 400, text, alpha );
            if ( e->signups > 0 )
            {
                char buffer[ 16 ];
                snprintf( buffer, sizeof( buffer ), "%d", e->signups );
                ColumnText( sg_ePlayers, y, buffer, 15, 400, text, alpha, 1 );
            }
            if ( next )
            {
                std::string const when = StartText( e->starts ) + "  \xb7  " + UntilText( e->starts ) +
                                         ( e->open ? "  \xb7  sign-ups open" : "" );
                ColumnText( sg_eWinner, y, when.c_str(), 15, 500, accent, alpha, -1, 1 - sg_eWinner );
            }
            else
                ColumnText( sg_eWinner, y, e->winner.c_str(), 15, 500, e->winner.empty() ? sg_quiet : textPrimary, alpha, -1,
                            1 - sg_eWinner );
        }

        virtual void Enter()
        {
            LeagueEvent const * const e = Shown();
            if ( e && !e->slug.empty() )
            {
                uMenu::MenuSound( uMenu::Sound_Activate );
                OpenPage( "/event/" + e->slug );
            }
        }
    };

    class gLeagueEventsMenu: public uMenu
    {
        std::vector< uMenuItem * > rows_;
        int generation_;

        void Clear()
        {
            for ( size_t i = 0; i < rows_.size(); ++i )
                delete rows_[i];
            rows_.clear();
        }

    public:
        gLeagueEventsMenu(): uMenu( tOutput( "$rcl_events_text" ), false ), generation_( -1 )
        {
            SetStyle( uMenuStyle_RclFull );
            SetKeyHint( tString( "up down  move  \xb7  enter  the event's page on the site  \xb7  esc  back" ) );
            Fill();
        }

        ~gLeagueEventsMenu()
        {
            Clear();
        }

        void Fill()
        {
            int const keep = items.Len() > 0 ? items.Len() - 1 - selected : 0;
            Clear();
            for ( int i = static_cast< int >( sg_past.size() ) - 1; i >= 0; --i )
                rows_.push_back( new gLeagueEventItem( this, i ) );
            if ( sg_hasNext )
                rows_.push_back( new gLeagueEventItem( this, -1 ) );
            if ( rows_.empty() )
                rows_.push_back( new gLeagueNoteItem( this, "not loaded yet: the league's site has not answered" ) );
            generation_ = sg_generation;
            SetSelected( std::max( 0, items.Len() - 1 - std::max( 0, std::min( keep, items.Len() - 1 ) ) ) );
        }

    protected:
        virtual void OnRender()
        {
            uMenu::OnRender();
            gLeague::Poll();
            if ( generation_ != sg_generation )
                Fill();
        }

        virtual void RenderExtras( REAL alpha )
        {
            StaleNote( alpha );
            REAL const header = MenuTop() + H( 12 );
            ColumnText( sg_eDate, header, "date", 12, 500, sg_quiet, alpha );
            ColumnText( sg_eTitle, header, "event", 12, 500, sg_quiet, alpha );
            ColumnText( sg_eKind, header, "kind", 12, 500, sg_quiet, alpha );
            ColumnText( sg_ePlayers, header, "players", 12, 500, sg_quiet, alpha, 1 );
            ColumnText( sg_eWinner, header, "won by", 12, 500, sg_quiet, alpha );

            // the foot: the selected event
            REAL const foot = LogicalHeight() - 150;
            REAL const left = PxX( LabelX() );
            gLeagueEventItem * const item = selected >= 0 && selected < items.Len() ?
                                            dynamic_cast< gLeagueEventItem * >( items[ selected ] ) : NULL;
            LeagueEvent const * const e = item ? item->Shown() : NULL;
            if ( e )
            {
                Text( X( left ), Y( foot ), 20, 500, textPrimary, alpha, e->title.c_str(), -1, W( 900 ) );
                std::ostringstream line;
                if ( item->Index() < 0 )
                {
                    line << StartText( e->starts ) << "  \xb7  " << UntilText( e->starts ) << "  \xb7  " << e->signups << " signed up";
                    if ( e->open )
                        line << "  \xb7  sign-ups open";
                }
                else
                {
                    line << e->date;
                    if ( e->signups > 0 )
                        line << "  \xb7  " << e->signups << " players";
                    if ( !e->winner.empty() )
                        line << "  \xb7  won by " << e->winner;
                }
                Text( X( left ), Y( foot + 32 ), 14, 400, textSecondary, alpha, line.str().c_str(), -1, W( 1100 ) );
                if ( !e->slug.empty() )
                    Text( X( left ), Y( foot + 56 ), 13, 400, sg_quiet, alpha,
                          item->Index() < 0 && e->open ? "enter opens its page on the league's site, to sign up there"
                                                       : "enter opens its page on the league's site" );
            }
            char summary[ 64 ];
            snprintf( summary, sizeof( summary ), "%d past events, the last %d here", sg_pastTotal, static_cast< int >( sg_past.size() ) );
            Text( X( PxX( RowRight() ) - 16 ), Y( foot ), 13, 400, textSecondary, alpha, summary, 1 );
        }
    };
}

void gLeague::Poll()
{
    if ( !Available() )
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
        double const age = static_cast< double >( time( NULL ) - Newest() );
        double const sinceAsked = tSysTimeFloat() - sg_lastAsked;
        bool const due = age > sg_freshFor && sinceAsked > ( sg_lastAnswered ? sg_freshFor : sg_retryAfter );

        // The player's own progression goes along with the rest, and is asked
        // for at once for someone just signed in (that often ends a few seconds
        // after the menu is up), unless what is kept is theirs and new enough.
        std::string const login = Login();
        bool mine = false;
        if ( !login.empty() && login != sg_progressAskedFor )
        {
            if ( login == sg_progressFor && time( NULL ) - sg_bodyWhen[ Source_Progression ] <= sg_freshFor )
                sg_progressAskedFor = login;
            else
                mine = true;
        }
        if ( due || mine )
            Ask( due, login );
    }

    Rebuild();
}

void gLeague::DrawHome( REAL alpha )
{
    sg_drawn = false;
    if ( !Available() || !AnyLoaded() )
        return;

    // beside the rail, on the right; a window too small for all of it keeps the rail
    REAL const margin = MarginPx();
    REAL const railRight = margin + ColumnPx() + margin * .5f;
    REAL const widthPx = 560;
    REAL const right = LogicalWidth() - margin * .6f;
    REAL const left = right - widthPx;
    bool const progress = HasProgress();
    REAL const eventHeight = 86, progressHeight = 58, tileHeight = 204, gap = 12;
    REAL top = margin * .6f;
    REAL const needed = top + eventHeight + gap + ( progress ? progressHeight + gap : 0 ) + 2 * tileHeight + gap + 40;
    if ( left < railRight + 40 || LogicalHeight() < needed )
        return;
    sg_drawn = true;

    // ---- the next event, on one line ----
    sg_signupBox = Box();
    sg_progressBox = Box();
    FillRect( X( left ), Y( top ), X( right ), Y( top + eventHeight ), sg_cardColor, .88f * alpha, 8 );
    Text( X( left + 22 ), Y( top + 20 ), 12, 500, sg_quiet, alpha, "next event" );
    if ( sg_hasNext )
    {
        REAL chipRight = right - 22;
        if ( !sg_next.slug.empty() )
        {
            char const * const label = sg_next.open ? "sign up" : "details";
            REAL const chipW = WidthPx( label, 13, 500 ) + 24;
            Chip( chipRight - chipW, top + 50, label, 13, true, accent, onAccent, alpha, 12, 14 );
            sg_signupBox.left = chipRight - chipW;
            sg_signupBox.right = chipRight;
            sg_signupBox.top = top + 36;
            sg_signupBox.bottom = top + 64;
            chipRight -= chipW + 16;
        }
        Text( X( left + 22 ), Y( top + 50 ), 24, 500, textPrimary, alpha, sg_next.title.c_str(), -1, W( 200 ) );
        REAL const titleW = std::min( WidthPx( sg_next.title.c_str(), 24, 500 ), REAL( 200 ) );
        char signups[ 32 ];
        snprintf( signups, sizeof( signups ), "%d signed up", sg_next.signups );
        std::string const line = StartText( sg_next.starts ) + "  \xb7  " + UntilText( sg_next.starts ) + "  \xb7  " + signups;
        Text( X( left + 22 + titleW + 16 ), Y( top + 52 ), 13, 400, textSecondary, alpha, line.c_str(), -1,
              W( chipRight - ( left + 22 + titleW + 16 ) ) );
    }
    else
    {
        Text( X( left + 22 ), Y( top + 52 ), 16, 500, sg_quiet, alpha, "no event announced yet" );
    }
    top += eventHeight + gap;

    // ---- the player's level, when the site has one for them ----
    if ( progress )
    {
        DrawProgress( left, top, widthPx, progressHeight, alpha );
        top += progressHeight + gap;
    }

    // ---- the four ladders, two by two ----
    REAL const tileWidth = ( widthPx - gap ) * .5f;
    for ( int i = 0; i < sg_ladderCount; ++i )
        DrawTile( i, left + ( i % 2 ) * ( tileWidth + gap ), top + ( i / 2 ) * ( tileHeight + gap ), tileWidth, tileHeight, alpha );
    top += 2 * tileHeight + gap + 22;

    // how old this is, when it is not fresh (no answer for a while: offline, or the site is down)
    time_t const newest = Newest();
    std::string note = "click a ladder for the whole board";
    if ( newest > 0 && time( NULL ) - newest > sg_staleShown )
        note = AgeText( newest ) + "  \xb7  " + note;
    Text( X( right ), Y( top ), 12, 400, textSecondary, .75f * alpha, note.c_str(), 1 );
}

bool gLeague::HomeClick( REAL x, REAL y )
{
    if ( !sg_drawn )
        return false;

    REAL const px = PxX( x ), py = PxY( y );
    if ( sg_signupBox.Holds( px, py ) && !sg_next.slug.empty() )
    {
        uMenu::MenuSound( uMenu::Sound_Activate );
        OpenPage( "/event/" + sg_next.slug );
        return true;
    }
    if ( sg_progressBox.Holds( px, py ) && HasProgress() && !sg_progress.slug.empty() )
    {
        uMenu::MenuSound( uMenu::Sound_Activate );
        OpenPage( "/player/" + sg_progress.slug );
        return true;
    }
    for ( int i = 0; i < sg_ladderCount; ++i )
        if ( sg_tileBox[i].Holds( px, py ) )
        {
            uMenu::MenuSound( uMenu::Sound_Activate );
            sg_ladderShown = i;
            LeaderboardMenu();
            return true;
        }
    return false;
}

void gLeague::LeaderboardMenu()
{
    Poll();
    gLeagueBoardMenu menu;
    menu.Enter();
}

void gLeague::EventsMenu()
{
    Poll();
    gLeagueEventsMenu menu;
    menu.Enter();
}

tString gLeague::NextEventLine()
{
    if ( !Available() || !sg_hasNext || sg_next.title.empty() )
        return tString();
    std::string line = sg_next.title;
    if ( sg_next.starts > 0 )
        line += " " + UntilText( sg_next.starts );
    else if ( !sg_next.date.empty() )
        line += ", " + sg_next.date;
    return tString( line.c_str() );
}

namespace
{
    // The signed-in player's page on the site: the address the site gave
    // with their level, else their sign-in name, which is what the site's
    // addresses are made of. Empty when nobody is signed in.
    std::string PlayerSlug()
    {
        if ( !ePlayer::RclAuthenticated() )
            return std::string();
        if ( HasProgress() && !sg_progress.slug.empty() )
            return sg_progress.slug;
        tString const identity = ePlayer::RclIdentity();
        int const at = identity.StrPos( "@" );
        tString const name = at > 0 ? identity.SubStr( 0, at ) : identity;
        return Slug( Lower( std::string( static_cast< char const * >( name ) ) ) );
    }
}

bool gLeague::HasPlayerPage()
{
    return !PlayerSlug().empty();
}

void gLeague::OpenPlayerPage()
{
    std::string const slug = PlayerSlug();
    if ( !slug.empty() )
        OpenPage( "/player/" + slug );
}

#else

void gLeague::Poll(){}
void gLeague::DrawHome( REAL ){}
bool gLeague::HomeClick( REAL, REAL ){ return false; }
void gLeague::LeaderboardMenu(){}
void gLeague::EventsMenu(){}
tString gLeague::NextEventLine(){ return tString(); }
bool gLeague::HasPlayerPage(){ return false; }
void gLeague::OpenPlayerPage(){}

#endif
