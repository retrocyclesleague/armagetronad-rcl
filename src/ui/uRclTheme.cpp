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

#include "uRclTheme.h"

#ifndef DEDICATED

#include "rRender.h"
#include "rFont.h"
#include "rScreen.h"
#include "ePlayer.h"
#include "nNetwork.h"

namespace uRclTheme
{
    static REAL const text_w = .048f;
    static REAL const text_h = .125f;
    static REAL const row_half_h = .062f;

    static REAL const menu_left = -.95f;
    static REAL const menu_right = -.10f;
    static REAL const menu_label_x = -.93f;
    static REAL const menu_desc_x = -.48f;
    static REAL const menu_desc_right = -.10f;

    static REAL const column_divider_x = -.02f;
    static REAL const sidebar_left = .04f;
    static REAL const sidebar_right = .93f;
    static REAL const sidebar_text_x = .07f;

    static REAL const bracket_ext = .014f;

    REAL MenuLeft() { return menu_left; }
    REAL MenuRight() { return menu_right; }
    REAL MenuLabelX() { return menu_label_x; }
    REAL MenuDescX() { return menu_desc_x; }
    REAL MenuDescRight() { return menu_desc_right; }
    REAL TextW() { return text_w; }
    REAL TextH() { return text_h; }
    REAL RowHalfH() { return row_half_h; }

    static REAL aspectTw()
    {
        return text_w * (REAL( sr_screenHeight ) / sr_screenWidth) * (4.0f / 3.0f);
    }

    static REAL charW( REAL scale )
    {
        return aspectTw() * scale;
    }

    static void setTextColor( REAL r, REAL g, REAL b, REAL a = 1.0f )
    {
        rTextField::SetDefaultColor( tColor( r, g, b, a ) );
        rTextField::SetBlendColor( tColor( 1, 1, 1, 1 ) );
    }

    // x = left edge; DisplayText(center=-1) lays out to the right without wrapping when it fits.
    static void drawTextAt( REAL x, REAL y, REAL w, REAL h, char const * text )
    {
        ::DisplayText( x, y, w, h, text, -1, 0, 0, rTextField::COLOR_IGNORE );
    }

    tString FirstLine( tString const & text )
    {
        tString line;
        for ( int i = 0; i < text.Len(); ++i )
        {
            if ( text[i] == '\n' )
                break;
            line << text[i];
        }
        return line;
    }

    static void drawGrid( REAL r, REAL g, REAL b, REAL a )
    {
        glDisable( GL_TEXTURE_2D );
        Color( r, g, b, a );
        BeginLines();
        for ( REAL x = -1.0f; x <= 1.01f; x += .08f )
        {
            Vertex( x, -1.0f );
            Vertex( x, 1.0f );
        }
        for ( REAL y = -1.0f; y <= 1.01f; y += .08f )
        {
            Vertex( -1.0f, y );
            Vertex( 1.0f, y );
        }
        RenderEnd();

        Color( r, g, b, a * .6f );
        BeginLines();
        Vertex( -1.2f, -1.2f );
        Vertex( 1.2f, 1.2f );
        Vertex( -1.2f, 1.2f );
        Vertex( 1.2f, -1.2f );
        RenderEnd();
    }

    void DrawBackground()
    {
        glDisable( GL_TEXTURE_2D );
        BeginQuads();
        Color( 0, 0, 0, 1 );
        Vertex( -1, -1 );
        Vertex( 1, -1 );
        Vertex( 1, 1 );
        Vertex( -1, 1 );
        RenderEnd();
        drawGrid( .12f, .12f, .12f, .55f );
    }

    void DrawGridOverlay( REAL alpha )
    {
        drawGrid( .15f, .15f, .15f, alpha );
    }

    static void drawBox( REAL left, REAL right, REAL bot, REAL top, REAL r, REAL g, REAL b, REAL a )
    {
        glDisable( GL_TEXTURE_2D );
        BeginLineLoop();
        Color( r, g, b, a );
        Vertex( left, bot );
        Vertex( right, bot );
        Vertex( right, top );
        Vertex( left, top );
        RenderEnd();
    }

    static void drawColumnDivider( REAL bot, REAL top )
    {
        glDisable( GL_TEXTURE_2D );
        Color( 1, 1, 0, .45f );
        BeginLines();
        Vertex( column_divider_x, bot );
        Vertex( column_divider_x, top );
        RenderEnd();
    }

    void DrawCornerPluses( REAL left, REAL right, REAL bot, REAL top )
    {
        REAL const s = .018f;
        glDisable( GL_TEXTURE_2D );
        Color( 1, 1, 1, .7f );
        BeginLines();
        Vertex( left - s, top ); Vertex( left + s, top );
        Vertex( left, top - s ); Vertex( left, top + s );
        Vertex( right - s, top ); Vertex( right + s, top );
        Vertex( right, top - s ); Vertex( right, top + s );
        Vertex( left - s, bot ); Vertex( left + s, bot );
        Vertex( left, bot - s ); Vertex( left, bot + s );
        Vertex( right - s, bot ); Vertex( right + s, bot );
        Vertex( right, bot - s ); Vertex( right, bot + s );
        RenderEnd();
    }

    void DrawFullChrome()
    {
        REAL smallH = text_h * .58f;

        setTextColor( 1, 1, 0 );
        drawTextAt( menu_left, .84f, charW( .75f ), text_h * .55f, "RETROCYCLES LEAGUE" );
        drawTextAt( menu_left, .72f, charW( 2.2f ), text_h * 1.35f, "RCL" );

        setTextColor( .55f, .55f, .55f );
        drawTextAt( -.38f, .74f, charW( .85f ), text_h * .62f, "armagetron advanced" );

        glDisable( GL_TEXTURE_2D );
        Color( .35f, .35f, .35f, 1 );
        BeginLines();
        Vertex( -.38f, .68f );
        Vertex( -.38f, .78f );
        RenderEnd();

        DrawCornerPluses( menu_left, menu_right, -.82f, .32f );
        drawColumnDivider( -.82f, .32f );

        drawBox( sidebar_left, sidebar_right, .50f, .72f, 1, 1, 1, .45f );
        setTextColor( 1, 1, 0 );
        drawTextAt( sidebar_text_x, .68f, charW( .85f ), text_h * .72f, "RCL FEED" );
        drawTextAt( sidebar_text_x, .58f, charW( .95f ), text_h * .62f, "// SEASON 7 LIVE NOW" );
        setTextColor( .65f, .65f, .65f );
        drawTextAt( sidebar_text_x, .545f, charW( .95f ), text_h * .58f, "Ranked queue is open." );
        setTextColor( 0, .85f, .85f );
        drawTextAt( sidebar_text_x, .505f, charW( .85f ), text_h * .58f, "/// READ MORE" );

        tString playerName( "GUEST" );
        ePlayer * lp = ePlayer::PlayerConfig( 0 );
        if ( lp && lp->name.Len() > 0 )
            playerName = lp->name;

        drawBox( sidebar_left, sidebar_right, .24f, .44f, 1, 1, 1, .45f );
        setTextColor( 1, 1, 0 );
        drawTextAt( sidebar_text_x, .40f, charW( .85f ), text_h * .72f, "PLAYER IDENTITY" );
        setTextColor( 1, 1, 1 );
        drawTextAt( sidebar_text_x, .30f, charW( .95f ), text_h * .68f, playerName );
        setTextColor( 0, .85f, .85f );
        drawTextAt( sidebar_text_x, .255f, charW( .85f ), text_h * .58f, "LOCAL PROFILE" );

        char const * status = "OFFLINE";
        char const * detail = "NOT CONNECTED";
        switch ( sn_GetNetState() )
        {
        case nCLIENT:
            status = "ONLINE";
            detail = "CONNECTED TO RCL";
            break;
        case nSERVER:
            status = "HOSTING";
            detail = "SERVER ACTIVE";
            break;
        case nSTANDALONE:
            status = "LOCAL";
            detail = "STANDALONE MODE";
            break;
        default:
            break;
        }

        drawBox( sidebar_left, sidebar_right, -.02f, .18f, 1, 1, 1, .45f );
        setTextColor( 1, 1, 0 );
        drawTextAt( sidebar_text_x, .14f, charW( .85f ), text_h * .72f, "STATUS" );
        setTextColor( .65f, .65f, .65f );
        drawTextAt( sidebar_text_x, .04f, charW( .95f ), text_h * .58f, detail );
        setTextColor( 0, 1, 0 );
        drawTextAt( .72f, .04f, charW( .75f ), text_h * .58f, status );

        glDisable( GL_TEXTURE_2D );
        Color( .35f, .35f, .35f, 1 );
        BeginLines();
        Vertex( -1, -.86f );
        Vertex( 1, -.86f );
        RenderEnd();

        setTextColor( .55f, .55f, .55f );
        tString footer;
        footer << "ARMAGETRON ADVANCED " << sn_programVersion;
        drawTextAt( menu_left, -.905f, charW( .82f ), smallH, footer );

        setTextColor( 1, 1, 0 );
        drawTextAt( .12f, -.905f, charW( .82f ), smallH, "// RCL THEME PACK" );

        setTextColor( .75f, .75f, .75f );
        drawTextAt( .52f, -.905f, charW( .62f ), smallH, "NEWS // RULES // SUPPORT" );
    }

    void DrawHeaderTitle( tString const & title )
    {
        setTextColor( 1, 1, 0 );
        drawTextAt( menu_left, .58f, charW( 1.4f ), text_h, title );
    }

    void DrawBracketSelection( REAL left, REAL right, REAL y )
    {
        REAL bot = y - row_half_h;
        REAL top = y + row_half_h;
        glDisable( GL_TEXTURE_2D );
        Color( 1, 1, 0, 1 );

        BeginLineStrip();
        Vertex( left - bracket_ext, top );
        Vertex( left, top );
        Vertex( left, top + bracket_ext );
        RenderEnd();

        BeginLineStrip();
        Vertex( right + bracket_ext, top );
        Vertex( right, top );
        Vertex( right, top + bracket_ext );
        RenderEnd();

        BeginLineStrip();
        Vertex( left - bracket_ext, bot );
        Vertex( left, bot );
        Vertex( left, bot - bracket_ext );
        RenderEnd();

        BeginLineStrip();
        Vertex( right + bracket_ext, bot );
        Vertex( right, bot );
        Vertex( right, bot - bracket_ext );
        RenderEnd();
    }

    void SetLabelColor( bool selected, REAL alpha )
    {
        if ( selected )
            setTextColor( 1, 1, 0, alpha );
        else
            setTextColor( .92f, .92f, .92f, alpha );
    }

    void SetDescColor( bool selected, REAL alpha )
    {
        if ( selected )
            setTextColor( 1, 1, 0, alpha );
        else
            setTextColor( .55f, .55f, .55f, alpha );
    }

    void DrawLabel( REAL x, REAL y, char const * text, bool selected, REAL alpha )
    {
        SetLabelColor( selected, alpha );
        drawTextAt( x, y, charW( 1.0f ), text_h, text );
    }

    void DrawDesc( REAL x, REAL y, char const * text, bool selected, REAL alpha )
    {
        SetDescColor( selected, alpha );
        drawTextAt( x, y, charW( .85f ), text_h * .82f, text );
    }
}

#endif
