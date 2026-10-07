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

#include "eRclScoreboard.h"

#include "tConfiguration.h"

#ifndef DEDICATED
#include "eNetGameObject.h"
#include "ePlayer.h"
#include "eRclBoard.h"
#include "eTeam.h"
#include "eTimer.h"
#include "nNetwork.h"
#include "rFont.h"
#include "rRender.h"
#include "rScreen.h"
#include "uRclTheme.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#endif

namespace eRclScoreboard
{
    bool enabled = true;

#ifndef DEDICATED

    static tConfItem<bool> se_enabledConf( "RCL_SCOREBOARD", enabled );

    namespace
    {
        // named one by one: the renderer has a Color of its own
        using uRclTheme::Color;
        using uRclTheme::FillRect;
        using uRclTheme::FrameRect;
        using uRclTheme::Text;
        using uRclTheme::TextWidth;
        using uRclTheme::ReadableColors;
        using uRclTheme::Scale;
        using uRclTheme::LogicalWidth;
        using uRclTheme::LogicalHeight;
        using uRclTheme::X;
        using uRclTheme::Y;
        using uRclTheme::W;
        using uRclTheme::canvas;
        using uRclTheme::surface;
        using uRclTheme::surfaceRaised;
        using uRclTheme::surfaceSelected;
        using uRclTheme::borderSubtle;
        using uRclTheme::textPrimary;
        using uRclTheme::textSecondary;
        using uRclTheme::accent;
        using uRclTheme::danger;

        tString server_;    // as the server names itself; empty in a local game
        tString mapfile_;
        tString map_;       // the map's own name, taken from its path

        // ---- measures, in logical pixels ----
        // Without columns of the server's own, the panel is no wider than
        // the part of the window a portrait capture keeps, so a clip shows
        // all of it. It has room for two such columns; more widen it.
        REAL const baseWidth   = 600;
        REAL const pad         = 16;    // from the panel's edge to its content
        REAL const headHeight  = 64;    // what is played, where, and who is here
        REAL const infoHeight  = 30;    // the server's facts about the match
        REAL const columnsHeight = 22;  // the column heads
        REAL const bandHeight  = 36;    // a team
        REAL const rowPitch    = 28;    // a player
        REAL const rowTight    = 23;    // a player, when there are many
        REAL const column      = 62;    // a column of numbers
        REAL const chip        = 8;     // the square in a player's colour
        REAL const noteHeight  = 28;
        int const mostColumns  = 5;     // of the server's own

        // the interface font's bold only has the wordmark's letters
        int const regular = 400, medium = 500;

        // the panel while it is drawn: its top left corner and its width
        REAL left_ = 0, top_ = 0, width_ = baseWidth;

        void Fill( REAL x, REAL y, REAL w, REAL h, Color const & c, REAL alpha, REAL radius = 0 )
        {
            FillRect( X( left_ + x ), Y( top_ + y ), X( left_ + x + w ), Y( top_ + y + h ), c, alpha, radius );
        }

        void Outline( REAL x, REAL y, REAL w, REAL h, Color const & c, REAL alpha )
        {
            int const pixels = std::max( 1, int( Scale() + .5f ) );
            FrameRect( X( left_ + x ), Y( top_ + y ), X( left_ + x + w ), Y( top_ + y + h ), pixels, c, alpha );
        }

        // one line of text; y is its centre, a width in pixels cuts it short
        void Write( REAL x, REAL y, REAL size, int weight, Color const & c, REAL alpha,
                    char const * text, int align = -1, REAL width = 0 )
        {
            Text( X( left_ + x ), Y( top_ + y ), size, weight, c, alpha, text, align,
                  width > 0 ? W( width ) : 0 );
        }

        // how wide a text is, in logical pixels
        REAL Width( char const * text, REAL size, int weight )
        {
            return TextWidth( text, size, weight ) / W( 1 );
        }

        // A player's or a team's colour as it shows on the panel: never so
        // dark that it is lost there.
        Color Paint( unsigned short r, unsigned short g, unsigned short b )
        {
            Color c = { std::min( r / 15.0f, 1.0f ), std::min( g / 15.0f, 1.0f ), std::min( b / 15.0f, 1.0f ) };
            REAL const least = .25f;
            REAL const seen = .299f * c.r + .587f * c.g + .114f * c.b;
            if ( seen < least )
            {
                REAL const lift = ( least - seen ) / ( 1 - seen );
                c.r += ( 1 - c.r ) * lift;
                c.g += ( 1 - c.g ) * lift;
                c.b += ( 1 - c.b ) * lift;
            }
            return c;
        }

        // "Anonymous/polygon/regular/square-1.0.1.aamap.xml" is the map "square"
        tString MapName( tString const & path )
        {
            std::string name( static_cast< char const * >( path ) );
            std::string::size_type const slash = name.find_last_of( "/\\" );
            if ( slash != std::string::npos )
                name.erase( 0, slash + 1 );
            std::string::size_type const type = name.find( ".aamap" );
            if ( type != std::string::npos )
                name.erase( type );
            else if ( name.size() > 4 && name.compare( name.size() - 4, 4, ".xml" ) == 0 )
                name.erase( name.size() - 4 );

            // the version: everything from the last dash on, if that is digits and dots
            std::string::size_type const dash = name.find_last_of( '-' );
            if ( dash != std::string::npos && dash > 0 && dash + 1 < name.size() &&
                 name.find_first_not_of( "0123456789.", dash + 1 ) == std::string::npos )
                name.erase( dash );
            return tString( name.c_str() );
        }

        // A server reached by its address alone is named by that address. It
        // says nothing a player wants on a scoreboard, or in a recording.
        bool IsAddress( tString const & name )
        {
            char const * c = name;
            for ( ; *c != '\0'; ++c )
                if ( ( *c < '0' || *c > '9' ) && *c != '.' && *c != ':' && *c != '[' && *c != ']' && *c != ' ' )
                    return false;
            return true;
        }

        bool Alive( ePlayerNetID const * p )
        {
            return p->Object() && p->Object()->Alive();
        }

        // what one line of the list is
        enum Kind { Kind_Team, Kind_Player, Kind_More };
        struct Line
        {
            Kind kind;
            eTeam * team;
            ePlayerNetID * player;
            int number;     // a player's place where everyone plays alone; how many are left out
        };

        // where the numbers end: the score, the server's columns after it, the ping last
        REAL PingEnd()                       { return width_ - pad; }
        REAL ColumnEnd( int k, int columns ) { return PingEnd() - column * ( columns - k ); }
        REAL ScoreEnd( int columns )         { return PingEnd() - column * ( columns + 1 ); }

        void DrawTeam( REAL y, eTeam * team )
        {
            Color const paint = Paint( team->R(), team->G(), team->B() );
            Fill( 0, y, width_, bandHeight - 2, surfaceRaised, .92f );
            Fill( 0, y, 4, bandHeight - 2, paint, 1 );

            REAL const centre = y + ( bandHeight - 2 ) * .5f;

            // the score first and large, as the thing the board is opened for
            tString score;
            score << team->Score();
            Write( pad, centre, 22, medium, textPrimary, 1, score );
            REAL const scoreWidth = std::max( REAL( 44 ), Width( score, 22, medium ) + 14 );

            int alive = 0;
            for ( int i = team->NumPlayers() - 1; i >= 0; --i )
                if ( Alive( team->Player( i ) ) )
                    ++alive;
            tString count;
            count << alive << " of " << team->NumPlayers() << " alive";
            Write( width_ - pad, centre, 13, regular, alive ? textSecondary : danger, 1, count, 1 );

            tString const name = ReadableColors( team->Name(), .30f );
            Write( pad + scoreWidth, centre, 16, medium, paint, 1, name, -1,
                   width_ - 2 * pad - scoreWidth - 110 );
        }

        void DrawPlayer( REAL y, REAL pitch, ePlayerNetID * p, int place, int columns )
        {
            bool const mine = p->IsHuman() && p->Owner() == sn_myNetID;
            bool const alive = Alive( p );
            bool const here = p->IsActive();
            REAL const alpha = !here ? .35f : alive ? 1 : .5f;
            REAL const centre = y + pitch * .5f;

            if ( mine )
            {
                // the one row a player looks for
                Fill( 4, y, width_ - 8, pitch, surfaceSelected, .30f );
                Outline( 4, y, width_ - 8, pitch, accent, 1 );
            }

            REAL x = pad + 2;
            if ( place > 0 )
            {
                tString number;
                number << place;
                Write( x + 14, centre, 13, regular, textSecondary, alpha, number, 1 );
                x += 24;
            }

            // filled while alive, an outline once out
            Color const paint = Paint( p->r, p->g, p->b );
            if ( alive )
                Fill( x, centre - chip * .5f, chip, chip, paint, 1 );
            else
                Outline( x, centre - chip * .5f, chip, chip, paint, .7f );
            x += chip + 10;

            REAL const scoreEnd = ScoreEnd( columns );
            REAL room = scoreEnd - 60 - x;
            if ( p->IsChatting() && here )
            {
                Write( scoreEnd - 60, centre, 12, regular, textSecondary, 1, "typing", 1 );
                room -= 52;
            }

            tString const name = ReadableColors( p->GetColoredName(), .30f );
            Write( x, centre, 16, mine ? medium : regular, textPrimary, alpha, name, -1, room );

            tString score;
            score << p->Score();
            Write( scoreEnd, centre, 16, medium, textPrimary, alpha, score, 1 );

            // what the server says about this player, column by column
            std::vector< tString > values;
            if ( columns > 0 && eRclBoard::PlayerValues( p, values ) )
                for ( int k = 0; k < columns && k < int( values.size() ); ++k )
                    Write( ColumnEnd( k, columns ), centre, 15, regular, textPrimary, alpha,
                           ReadableColors( values[k], .30f ), 1, column - 8 );

            if ( here )
            {
                tString ping;
                ping << int( p->ping * 1000 );
                Write( PingEnd(), centre, 13, regular, textSecondary, alpha, ping, 1 );
            }
            else
                Write( PingEnd(), centre, 13, regular, textSecondary, 1, "left", 1 );
        }
    }

    void SetContext( tString const & server, tString const & mapfile )
    {
        if ( server_ != server )
            server_ = server;
        if ( mapfile_ != mapfile )
        {
            mapfile_ = mapfile;
            map_ = MapName( mapfile );
        }
    }

    bool Render()
    {
        if ( !enabled || !sr_glOut )
            return false;

        ePlayerNetID::SortByScore();
        eTeam::SortByScore();

        // what the server adds, if it adds anything
        std::vector< tString > heads, facts;
        eRclBoard::Split( eRclBoard::Columns(), heads );
        eRclBoard::Split( eRclBoard::Info(), facts );
        int const columns = std::min( int( heads.size() ), mostColumns );
        tString const & title = eRclBoard::Title();
        tString const & note = eRclBoard::Note();
        bool const titled = title.Len() > 1;
        bool const noted = note.Len() > 1;

        // Teams count once one of them is more than a player on their own.
        bool teamPlay = false;
        for ( int i = eTeam::teams.Len() - 1; i >= 0 && !teamPlay; --i )
        {
            eTeam const * t = eTeam::teams( i );
            teamPlay = t->NumPlayers() > 1 ||
                       ( t->NumPlayers() == 1 && t->Player( 0 )->Score() != t->Score() );
        }

        int playing = 0, teams = 0;
        tString watching;
        int watchers = 0;
        for ( int i = 0; i < se_PlayerNetIDs.Len(); ++i )
        {
            ePlayerNetID const * p = se_PlayerNetIDs( i );
            if ( p->CurrentTeam() )
                ++playing;
            else
            {
                if ( watchers++ )
                    watching << "0xRESETT, ";
                watching << ReadableColors( p->GetColoredName(), .30f );
            }
        }
        for ( int i = 0; i < eTeam::teams.Len(); ++i )
            if ( eTeam::teams( i )->NumPlayers() > 0 )
                ++teams;

        // How much fits. Rows close up before any are left out; if some
        // must be, every team keeps its best.
        REAL const logicalHeight = LogicalHeight();
        REAL const head = headHeight + ( facts.empty() ? 0 : infoHeight );
        REAL const foot = ( watchers ? rowPitch : 0 ) + ( noted ? noteHeight : 0 ) + 10;
        REAL const fixed = head + columnsHeight + ( teamPlay ? teams * bandHeight : 0 ) + foot;
        REAL const room = logicalHeight - 48 - fixed;
        REAL pitch = rowPitch;
        if ( playing * pitch > room )
            pitch = rowTight;
        int const groups = teamPlay ? std::max( teams, 1 ) : 1;
        int perGroup = playing;
        if ( playing * pitch > room )
            perGroup = std::max( 1, int( room / pitch ) / groups - 1 );

        std::vector< Line > lines;
        lines.reserve( playing + 2 * teams + 1 );
        if ( teamPlay )
        {
            for ( int i = 0; i < eTeam::teams.Len(); ++i )
            {
                eTeam * t = eTeam::teams( i );
                if ( t->NumPlayers() <= 0 )
                    continue;
                Line const band = { Kind_Team, t, 0, 0 };
                lines.push_back( band );

                // in the order of their scores, which is the order of the list
                int shown = 0;
                for ( int j = 0; j < se_PlayerNetIDs.Len(); ++j )
                {
                    ePlayerNetID * p = se_PlayerNetIDs( j );
                    if ( p->CurrentTeam() != t )
                        continue;
                    if ( shown < perGroup )
                    {
                        Line const row = { Kind_Player, t, p, 0 };
                        lines.push_back( row );
                    }
                    ++shown;
                }
                if ( shown > perGroup )
                {
                    Line const more = { Kind_More, t, 0, shown - perGroup };
                    lines.push_back( more );
                }
            }
        }
        else
        {
            int shown = 0;
            for ( int j = 0; j < se_PlayerNetIDs.Len(); ++j )
            {
                ePlayerNetID * p = se_PlayerNetIDs( j );
                if ( !p->CurrentTeam() )
                    continue;
                if ( shown < perGroup )
                {
                    Line const row = { Kind_Player, 0, p, shown + 1 };
                    lines.push_back( row );
                }
                ++shown;
            }
            if ( shown > perGroup )
            {
                Line const more = { Kind_More, 0, 0, shown - perGroup };
                lines.push_back( more );
            }
        }

        REAL listHeight = 0;
        for ( size_t i = 0; i < lines.size(); ++i )
            listHeight += lines[i].kind == Kind_Team ? bandHeight : pitch;
        if ( lines.empty() )
            listHeight = rowPitch;      // "nobody is playing"
        REAL const height = head + columnsHeight + listHeight + foot;

        // Below the messages in the middle of the upper third where there is
        // room for that, further up where there is not.
        REAL const logicalWidth = LogicalWidth();
        width_ = std::min( baseWidth + column * std::max( 0, columns - 2 ), logicalWidth - 16 );
        left_ = ( logicalWidth - width_ ) * .5f;
        top_ = std::max( REAL( 24 ), std::min( logicalHeight * .30f, logicalHeight - 24 - height ) );

        // The board lies on its own panel, not on the arena: nothing on it
        // is to be written in the pale arena's ink.
        bool const ink = sr_cleanInk;
        sr_cleanInk = false;

        Fill( 0, 0, width_, height, canvas, .96f );
        Fill( 0, 0, width_, head, surface, 1 );
        Outline( 0, 0, width_, height, borderSubtle, 1 );

        // ---- what is played and where, and how long the round has run ----
        bool const online = sn_GetNetState() == nCLIENT;
        tString where;
        if ( !online )
            where = "Local game";
        else if ( server_.Len() > 1 && !IsAddress( server_ ) )
            where = ReadableColors( server_, .30f );
        else
            where = "Online game";
        tString const headline = titled ? ReadableColors( title, .30f ) : where;
        Write( pad, 24, 20, medium, textPrimary, 1, headline, -1, width_ - 2 * pad - 70 );

        if ( se_mainGameTimer )
        {
            int const seconds = int( se_GameTime() );
            if ( seconds >= 0 )
            {
                char clock[16];
                snprintf( clock, sizeof( clock ), "%d:%02d", seconds / 60, seconds % 60 );
                Write( width_ - pad, 24, 15, medium, textSecondary, 1, clock, 1 );
            }
        }

        tString count;
        count << playing << " playing";
        if ( watchers )
            count << ", " << watchers << " watching";
        Write( width_ - pad, 48, 13, regular, textSecondary, 1, count, 1 );

        // under a title of the server's, its name moves down to the map
        REAL x = pad;
        REAL const second = width_ - 2 * pad - Width( count, 13, regular ) - 16;
        if ( titled )
        {
            Write( x, 48, 13, regular, textSecondary, 1, where, -1, second * .6f );
            x += std::min( Width( where, 13, regular ), second * .6f ) + 14;
        }
        if ( map_.Len() > 1 )
            Write( x, 48, 13, regular, textSecondary, titled ? .7f : 1, map_, -1, pad + second - x );

        // the server's facts, each on a small plate of its own
        if ( !facts.empty() )
        {
            REAL at = pad;
            for ( size_t i = 0; i < facts.size(); ++i )
            {
                tString const fact = ReadableColors( facts[i], .30f );
                REAL const plate = Width( fact, 13, regular ) + 16;
                if ( at + plate > width_ - pad )
                    break;
                Fill( at, headHeight - 2, plate, 22, surfaceRaised, 1, 3 );
                Write( at + 8, headHeight + 9, 13, regular, textPrimary, 1, fact );
                at += plate + 6;
            }
        }

        // ---- the column heads ----
        REAL y = head;
        REAL const heading = y + columnsHeight * .5f + 1;
        Write( ScoreEnd( columns ), heading, 12, regular, textSecondary, 1, "score", 1 );
        for ( int k = 0; k < columns; ++k )
            Write( ColumnEnd( k, columns ), heading, 12, regular, textSecondary, 1, heads[k], 1, column - 8 );
        Write( PingEnd(), heading, 12, regular, textSecondary, 1, "ping", 1 );
        y += columnsHeight;

        // ---- teams and players ----
        if ( lines.empty() )
        {
            Write( pad, y + rowPitch * .5f, 15, regular, textSecondary, 1, "Nobody is playing." );
            y += rowPitch;
        }
        for ( size_t i = 0; i < lines.size(); ++i )
        {
            Line const & line = lines[i];
            switch ( line.kind )
            {
            case Kind_Team:
                DrawTeam( y, line.team );
                y += bandHeight;
                break;
            case Kind_Player:
                DrawPlayer( y, pitch, line.player, line.number, columns );
                y += pitch;
                break;
            case Kind_More:
                {
                    tString more;
                    more << "and " << line.number << " more";
                    Write( pad + 20, y + pitch * .5f, 13, regular, textSecondary, 1, more );
                    y += pitch;
                }
                break;
            }
        }

        // ---- who watches, and the server's last word ----
        if ( watchers )
        {
            Fill( pad, y + 2, width_ - 2 * pad, 1, borderSubtle, 1 );
            REAL const centre = y + 3 + rowPitch * .5f;
            Write( pad, centre, 12, regular, textSecondary, 1, "watching" );
            Write( pad + 66, centre, 13, regular, textSecondary, 1, watching, -1,
                   width_ - 2 * pad - 66 );
            y += rowPitch;
        }
        if ( noted )
        {
            Fill( pad, y + 2, width_ - 2 * pad, 1, borderSubtle, 1 );
            Write( pad, y + 3 + noteHeight * .5f, 13, regular, textSecondary, 1,
                   ReadableColors( note, .30f ), -1, width_ - 2 * pad );
        }

        RenderEnd();
        rTextField::SetDefaultColor( tColor( 1, 1, 1, 1 ) );
        rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
        sr_cleanInk = ink;
        return true;
    }

#else   // DEDICATED: there is nothing to draw on

    void SetContext( tString const &, tString const & ) {}
    bool Render() { return false; }

#endif
}
