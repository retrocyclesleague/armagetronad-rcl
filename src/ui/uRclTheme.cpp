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

#include <algorithm>
#include <math.h>
#include <vector>

#ifndef DEDICATED
#include "rFont.h"
#include "rRender.h"
#include "rScreen.h"
#include "rUiFont.h"
#endif

namespace uRclTheme
{
    // The component kit's inverse surfaces and its one lime, on production's
    // dark canvas. Every interface colour comes from here.
    Color const canvas          = { .078f, .086f, .090f };  // #141617
    Color const surface         = { .125f, .129f, .133f };  // #202122
    Color const surfaceRaised   = { .231f, .239f, .247f };  // #3B3D3F
    Color const surfaceSelected = { .357f, .365f, .376f };  // #5B5D60
    Color const borderSubtle    = { .239f, .243f, .247f };  // #3D3E3F
    Color const borderStrong    = { .392f, .400f, .408f };  // #646668
    Color const textPrimary     = { .980f, .980f, .980f };  // #FAFAFA
    Color const textSecondary   = { .698f, .706f, .710f };  // #B2B4B5
    Color const accent          = { .933f, 1.0f,  .255f };  // #EEFF41
    Color const onAccent        = { .125f, .129f, .133f };  // #202122
    Color const danger          = { .941f, .459f, .494f };  // the kit's #B23440, lifted

    namespace
    {
        // ---- logical pixel measures ----
        REAL const referenceHeight = 1080;  // lines at which a logical pixel is a real one
        REAL const minimumScale    = .6f;   // below this, text would get too small: scroll instead

        REAL const rowPadPx      = 16;      // from a row's edge to its text
        REAL const labelColumnPx = 300;     // room for a setting's label
        REAL const columnGapPx   = 24;      // between label and control
        REAL const sliderPx      = 200;     // slider track
        REAL const textBoxPx     = 420;     // text and key binding boxes, at most
        REAL const togglePx      = 38;      // toggle track
        REAL const promptBarPx   = 60;      // chat and console input bar
        REAL const footerPx      = 150;     // below the rows: help and key hints

        REAL const sizeWordmark  = 34;      // the same mark on every screen
        REAL const wordmarkY     = 72;
        REAL const sizeTitle     = 30;
        REAL const sizeAction    = 22;      // navigation rows
        REAL const sizeSetting   = 18;      // setting labels and values
        REAL const sizeTable     = 17;      // table rows
        REAL const sizeHelp      = 15;
        REAL const sizeHint      = 14;

        REAL const controlRadius = 3;       // the kit's one small radius

        int const weightRegular  = 400;
        int const weightMedium   = 500;
        int const weightBold     = 700;

        REAL const emPerCell     = .78f;    // rUiFont::emPerCell, also valid without it

        // ---- frame state ----
        Layout layout_ = Layout_Page;
        bool   values_ = false;

        REAL rowY_ = 0;
        bool rowSelected_ = false;
        bool rowPrimary_ = false;
        int  rowControl_ = Control_None;
        REAL promptValueX_ = 0;

        bool sceneNoted_ = false;   // set while a frame is drawn
        bool sceneBehind_ = false;  // what the frame being drawn has behind it

#ifndef DEDICATED
        REAL PixelW() { return sr_renderWidth > 0 ? REAL(sr_renderWidth) : 1920.0f; }
        REAL PixelH() { return sr_renderHeight > 0 ? REAL(sr_renderHeight) : 1080.0f; }
#else
        REAL PixelW() { return 1920; }
        REAL PixelH() { return 1080; }
#endif

        REAL RowPitchPx()  { return layout_ == Layout_Wide ? 40.0f : 48.0f; }
        REAL RowHeightPx() { return layout_ == Layout_Wide ? 38.0f : 44.0f; }
        REAL ListTopPx()
        {
            switch ( layout_ )
            {
            case Layout_Home: return 142;
            case Layout_Wide: return 236;
            default:          return 204;
            }
        }
    }

    // ------------------------------------------------------------- units

    void Configure( Layout layout, bool values )
    {
        layout_ = layout;
        values_ = values;
    }

    Layout CurrentLayout() { return layout_; }

    REAL Scale() { return std::max( PixelH() / referenceHeight, minimumScale ); }
    REAL LogicalWidth()  { return PixelW() / Scale(); }
    REAL LogicalHeight() { return PixelH() / Scale(); }

    REAL W( REAL px ) { return 2 * px * Scale() / PixelW(); }
    REAL H( REAL px ) { return 2 * px * Scale() / PixelH(); }
    REAL X( REAL px ) { return -1 + W( px ); }
    REAL Y( REAL px ) { return 1 - H( px ); }

    REAL MarginPx()
    {
        return std::max( 32.0f, std::min( 96.0f, LogicalWidth() * .05f ) );
    }

    REAL ColumnPx()
    {
        // navigation keeps one column width from the main menu down; pages
        // with controls need room for them
        REAL wanted = 440;
        switch ( layout_ )
        {
        case Layout_Home:   break;
        case Layout_Wide:   wanted = 1600; break;
        case Layout_Prompt: wanted = 4000; break;
        default:            if ( values_ ) wanted = 760; break;
        }
        return std::max( 240.0f, std::min( wanted, LogicalWidth() - 2 * MarginPx() ) );
    }

    // ---------------------------------------------------------- geometry

    REAL MenuTop()    { return Y( ListTopPx() ); }
    REAL MenuBottom()
    {
        return Y( LogicalHeight() - ( layout_ == Layout_Wide ? footerPx + 60 : footerPx ) );
    }
    REAL RowLeft()    { return X( MarginPx() ); }
    REAL RowRight()   { return X( MarginPx() + ColumnPx() ); }
    REAL LabelX()     { return X( MarginPx() + rowPadPx ); }
    REAL ValueOffset()
    {
        // the control column gives way on narrow windows before labels do
        REAL const room = ColumnPx() - 2 * rowPadPx;
        return W( std::min( labelColumnPx + columnGapPx, room * .5f ) );
    }
    REAL ValueX()        { return LabelX() + ValueOffset(); }
    REAL RowPitch()      { return H( RowPitchPx() ); }
    REAL RowHalfHeight() { return H( RowHeightPx() * .5f ); }
    REAL ScrollEdge()    { return H( RowPitchPx() * .5f + 4 ); }
    REAL ScrollMargin()  { return H( RowPitchPx() * 1.5f ); }
    REAL SliderLeft()    { return ValueX(); }
    REAL SliderRight()   { return ValueX() + W( sliderPx ); }
    REAL ControlRight()
    {
        return std::min( RowRight() - W( rowPadPx ), ValueX() + W( textBoxPx ) );
    }

    REAL PromptTop()     { return Y( LogicalHeight() - promptBarPx ); }
    REAL PromptBottom()  { return Y( LogicalHeight() + 8 ); }
    REAL PromptRowY()    { return Y( LogicalHeight() - promptBarPx * .5f ); }

    REAL EaseIn( REAL progress )
    {
        if ( progress <= 0 )
            return 0;
        if ( progress >= 1 )
            return 1;
        REAL const inverse = 1 - progress;
        return 1 - inverse * inverse * inverse;
    }

    namespace
    {
        // relative luminance of an sRGB colour
        REAL Luminance( REAL r, REAL g, REAL b )
        {
            return .2126f * powf( r, 2.2f ) + .7152f * powf( g, 2.2f )
                 + .0722f * powf( b, 2.2f );
        }

        int HexDigit( char c )
        {
            if ( c >= '0' && c <= '9' ) return c - '0';
            if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
            if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
            return -1;
        }
    }

    tString ReadableColors( tString const & text, REAL minLuminance )
    {
        tString out;
        int const length = text.Len() - 1;
        for ( int i = 0; i < length; )
        {
            // a colour code is 0x and six hex digits
            int digits[6];
            bool code = i + 8 <= length && text[i] == '0' && text[i+1] == 'x';
            for ( int d = 0; code && d < 6; ++d )
                code = ( digits[d] = HexDigit( text[i+2+d] ) ) >= 0;
            if ( !code )
            {
                out << text[i++];
                continue;
            }

            REAL c[3];
            for ( int k = 0; k < 3; ++k )
                c[k] = ( digits[2*k] * 16 + digits[2*k+1] ) / 255.0f;

            // towards white, in small steps, until it is bright enough
            for ( int step = 0; step < 20 && Luminance( c[0], c[1], c[2] ) < minLuminance; ++step )
                for ( int k = 0; k < 3; ++k )
                    c[k] += ( 1 - c[k] ) * .12f;

            static char const hex[] = "0123456789abcdef";
            out << "0x";
            for ( int k = 0; k < 3; ++k )
            {
                int const value = static_cast< int >( c[k] * 255 + .5f );
                out << hex[ ( value >> 4 ) & 15 ] << hex[ value & 15 ];
            }
            i += 8;
        }
        return out;
    }

    tString FirstLine( tString const & text )
    {
        tString line;
        for ( int i = 0; i < text.Len() && text[i] != '\0'; ++i )
        {
            if ( text[i] == '\n' || text[i] == '\r' )
                break;
            line << text[i];
        }
        return line;
    }

    void NoteSceneBehind() { sceneNoted_ = true; }

#ifndef DEDICATED

    namespace
    {
        // ------------------------------------------------- primitives

        REAL SnapX( REAL x )
        {
            REAL const unit = PixelW() * .5f;
            return floorf( ( x + 1 ) * unit + .5f ) / unit - 1;
        }

        REAL SnapY( REAL y )
        {
            REAL const unit = PixelH() * .5f;
            return floorf( ( y + 1 ) * unit + .5f ) / unit - 1;
        }

        // A filled rectangle on whole pixels. A radius takes its corners off:
        // at these sizes a small cut reads as the kit's rounded corner.
        void Rect( REAL left, REAL top, REAL right, REAL bottom, Color const & c, REAL alpha,
                   REAL radiusPx = 0 )
        {
            if ( !sr_alphaBlend && alpha < .5f )
                return;     // a tint would come out as a solid block

            left = SnapX( left ); right = SnapX( right );
            top = SnapY( top ); bottom = SnapY( bottom );

            REAL const cut = radiusPx > 0
                ? std::max( 1.0f, floorf( radiusPx * .6f * Scale() + .5f ) ) : 0;
            REAL const cutW = 2 * cut / PixelW(), cutH = 2 * cut / PixelH();

            RenderEnd();
            glDisable( GL_TEXTURE_2D );
            BeginQuads();
            ::Color( c.r, c.g, c.b, alpha );
            if ( cut <= 0 || right - left < 3 * cutW || top - bottom < 3 * cutH )
            {
                Vertex( left, bottom );
                Vertex( right, bottom );
                Vertex( right, top );
                Vertex( left, top );
            }
            else
            {
                Vertex( left, bottom + cutH );
                Vertex( right, bottom + cutH );
                Vertex( right, top - cutH );
                Vertex( left, top - cutH );

                Vertex( left, top - cutH );
                Vertex( right, top - cutH );
                Vertex( right - cutW, top );
                Vertex( left + cutW, top );

                Vertex( left + cutW, bottom );
                Vertex( right - cutW, bottom );
                Vertex( right, bottom + cutH );
                Vertex( left, bottom + cutH );
            }
            RenderEnd();
        }

        // a control's body: a fill inside a one pixel outline
        void Box( REAL left, REAL top, REAL right, REAL bottom, Color const & fill,
                  Color const & outline, REAL alpha )
        {
            left = SnapX( left ); right = SnapX( right );
            top = SnapY( top ); bottom = SnapY( bottom );
            REAL const w = 2 / PixelW(), h = 2 / PixelH();
            Rect( left, top, right, bottom, outline, alpha, controlRadius );
            Rect( left + w, top - h, right - w, bottom + h, fill, alpha, controlRadius - 1 );
        }

        // a rectangle's outline, drawn inside it, in real pixels thick
        void Frame( REAL left, REAL top, REAL right, REAL bottom, int pixels,
                    Color const & c, REAL alpha )
        {
            REAL const w = 2 * pixels / PixelW();
            REAL const h = 2 * pixels / PixelH();
            Rect( left, top, right, top - h, c, alpha );
            Rect( left, bottom + h, right, bottom, c, alpha );
            Rect( left, top - h, left + w, bottom + h, c, alpha );
            Rect( right - w, top - h, right, bottom + h, c, alpha );
        }

        // cell size that gives text of the given em size
        void Cell( REAL sizePx, REAL & cellW, REAL & cellH )
        {
            REAL const cellPixels = sizePx * Scale() / emPerCell;
            cellH = 2 * cellPixels / PixelH();
            cellW = cellPixels / PixelW();     // half as wide as tall
        }

        char const * Ellipsis()
        {
            return rUiFont::Available() ? "\x85" : "...";
        }

        // the longest beginning of text that fits, with an ellipsis
        tString Shorten( char const * text, REAL cellW, REAL cellH, REAL maxWidth )
        {
            tString best( Ellipsis() );
            tString beginning;
            for ( char const * c = text; *c != '\0'; )
            {
                // never cut a colour code in half
                int step = 1;
                if ( c[0] == '0' && c[1] == 'x' )
                {
                    step = 0;
                    while ( step < 8 && c[step] != '\0' )
                        ++step;
                }
                for ( int i = 0; i < step; ++i )
                    beginning << c[i];
                c += step;

                tString candidate( beginning );
                candidate << Ellipsis();
                if ( rTextField::TextWidth( candidate, cellW, cellH ) > maxWidth )
                    break;
                best = candidate;
            }
            return best;
        }

        void SetTextColor( Color const & c, REAL alpha )
        {
            rTextField::SetDefaultColor( tColor( c.r, c.g, c.b, 1 ) );
            rTextField::SetBlendColor( tColor( 1, 1, 1, alpha ) );
        }
    }

    // ---------------------------------------------------------------- text

    REAL TextWidth( char const * text, REAL sizePx, int weight, REAL tracking )
    {
        REAL cellW, cellH;
        Cell( sizePx, cellW, cellH );
        rTextStyle style( weight, tracking, false );
        return rTextField::TextWidth( text, cellW, cellH );
    }

    void Text( REAL x, REAL y, REAL sizePx, int weight, Color const & color, REAL alpha,
               char const * text, int align, REAL maxWidth, REAL tracking )
    {
        if ( !text || text[0] == '\0' || alpha <= 0 )
            return;

        REAL cellW, cellH;
        Cell( sizePx, cellW, cellH );
        rTextStyle style( weight, tracking, false );

        tString shortened;
        REAL width = rTextField::TextWidth( text, cellW, cellH );
        if ( maxWidth > 0 && width > maxWidth )
        {
            shortened = Shorten( text, cellW, cellH, maxWidth );
            text = shortened;
            width = rTextField::TextWidth( text, cellW, cellH );
        }

        REAL left = x;
        if ( align == 0 )
            left -= width * .5f;
        else if ( align > 0 )
            left -= width;

        SetTextColor( color, alpha );
        rTextField field( left, y + cellH * .5f, cellW, cellH );
        field.SetWidth( 1000000 );
        field.StringOutput( text );
    }

    int Paragraph( REAL x, REAL top, REAL width, REAL sizePx, int weight, Color const & color,
                   REAL alpha, char const * text, int maxLines, int firstLine )
    {
        if ( !text )
            return 0;

        REAL cellW, cellH;
        Cell( sizePx, cellW, cellH );
        rTextStyle style( weight, 0, false );

        // break into lines by measured width
        std::vector< tString > lines;
        tString line, word;
        char const * c = text;
        while ( true )
        {
            bool const end = ( *c == '\0' );
            bool const newline = ( *c == '\n' || *c == '\r' );
            if ( end || newline || *c == ' ' )
            {
                if ( word.Len() > 1 &&
                     rTextField::TextWidth( word, cellW, cellH ) > width )
                {
                    // A word that is wider than a line by itself (a path, an
                    // address) is cut where it has to be, not shortened. It
                    // starts on the line it follows.
                    tString piece( line );
                    if ( line.Len() > 1 )
                        piece << " ";
                    for ( int i = 0; i < word.Len() - 1; )
                    {
                        // a colour code stays in one piece
                        int step = 1;
                        if ( word[i] == '0' && word[i+1] == 'x' && i + 8 <= word.Len() - 1 )
                            step = 8;
                        tString grown( piece );
                        for ( int k = 0; k < step; ++k )
                            grown << word[i+k];
                        if ( piece.Len() > 1 &&
                             rTextField::TextWidth( grown, cellW, cellH ) > width )
                        {
                            lines.push_back( piece );
                            piece = "";
                            continue;   // the same characters start the next line
                        }
                        piece = grown;
                        i += step;
                    }
                    line = piece;
                    word = "";
                }
                else if ( word.Len() > 1 )
                {
                    tString candidate( line );
                    if ( line.Len() > 1 )
                        candidate << " ";
                    candidate << word;
                    if ( line.Len() > 1 &&
                         rTextField::TextWidth( candidate, cellW, cellH ) > width )
                    {
                        lines.push_back( line );
                        line = word;
                    }
                    else
                    {
                        line = candidate;
                    }
                    word = "";
                }
                if ( newline || end )
                {
                    if ( line.Len() > 1 || newline )
                        lines.push_back( line );
                    line = "";
                }
                if ( end )
                    break;
            }
            else
            {
                word << *c;
            }
            ++c;
        }

        int const total = static_cast< int >( lines.size() );
        if ( firstLine < 0 )
            firstLine = 0;
        int last = total;
        if ( maxLines > 0 && firstLine + maxLines < last )
            last = firstLine + maxLines;

        for ( int i = firstLine; i < last; ++i )
        {
            tString shown( lines[i] );
            // more text than room: say so on the last line shown
            if ( i == last - 1 && last < total )
                shown << " " << Ellipsis();
            if ( shown.Len() > 1 )
                Text( x, top - ( i - firstLine + .5f ) * cellH, sizePx, weight, color, alpha,
                      shown, -1, width );
        }
        return total;
    }

    // ------------------------------------------------------------- frame

    void DrawBackground( REAL alpha )
    {
        sceneBehind_ = sceneNoted_;
        sceneNoted_ = false;

        if ( layout_ == Layout_Prompt )
        {
            // the input bar along the bottom; the game stays in view above it
            REAL const top = Y( LogicalHeight() - promptBarPx );
            Rect( -1, top, 1, -1, surface, .94f * alpha );
            Rect( -1, top, 1, top - 2 / PixelH(), borderSubtle, alpha );
            return;
        }

        // The scene behind is held back so it cannot compete with the text.
        if ( sceneBehind_ )
            Rect( -1, 1, 1, -1, canvas, .62f * alpha );
        else
            Rect( -1, 1, 1, -1, canvas, 1 );

        if ( layout_ == Layout_Wide )
        {
            // a table needs the whole width
            Rect( -1, 1, 1, -1, surface, 1 );
            return;
        }

        // The navigation column: a rail down the left side. It is solid, so
        // nothing behind it can get into the text.
        REAL const right = X( MarginPx() + ColumnPx() + 32 );
        Rect( -1, 1, right, -1, surface, 1 );
        Rect( right, 1, right + 2 / PixelW(), -1, borderSubtle, 1 );
    }

    namespace
    {
        // The wordmark: lowercase, bold, tight, over its short lime underline.
        // It is the same on every screen.
        void DrawWordmark( REAL alpha )
        {
            REAL const left = X( MarginPx() );
            REAL const width = TextWidth( "rcl", sizeWordmark, weightBold, -.02f );
            Text( left, Y( wordmarkY ), sizeWordmark, weightBold, textPrimary, alpha,
                  "rcl", -1, 0, -.02f );
            REAL const barTop = wordmarkY + sizeWordmark * .5f + 12;
            Rect( left, Y( barTop ), left + width, Y( barTop + 5 ), accent, alpha );
        }
    }

    void DrawChrome( tString const & title, tString const & footnote, REAL alpha )
    {
        if ( layout_ == Layout_Prompt )
            return;

        REAL const left = X( MarginPx() );

        DrawWordmark( alpha );

        if ( layout_ != Layout_Home )
        {
            // a name is a heading; a sentence (a sign-in request) is copy
            tString const heading = FirstLine( title );
            if ( heading.Len() <= 44 && heading.Len() == title.Len() )
            {
                Text( left, Y( 160 ), sizeTitle, weightMedium, textPrimary, alpha,
                      heading, -1, W( ColumnPx() ), -.03f );
            }
            else
            {
                Paragraph( left, Y( 126 ), W( ColumnPx() ), 17, weightRegular,
                           textSecondary, alpha, title, 3 );
            }
        }

        REAL const hintY = Y( LogicalHeight() - 44 );
        Text( left, hintY, sizeHint, weightRegular, textSecondary, .8f * alpha,
              layout_ == Layout_Wide
                  ? "arrows move  \xb7  enter join  \xb7  left/right sort  \xb7  r refresh  \xb7  b bookmark  \xb7  esc back"
                  : "arrows move  \xb7  enter select  \xb7  esc back",
              -1, W( LogicalWidth() - 2 * MarginPx() - 260 ) );

        if ( footnote.Len() > 1 )
            Text( X( LogicalWidth() - MarginPx() ), hintY, sizeHint, weightRegular,
                  textSecondary, .8f * alpha, footnote, 1 );
    }

    // -------------------------------------------------------------- rows

    void BeginRow( REAL y, bool selected, bool primary, int control )
    {
        rowY_ = y;
        rowSelected_ = selected;
        rowPrimary_ = primary;
        rowControl_ = control;
    }

    void DrawRow( REAL alpha, bool on, REAL fraction )
    {
        if ( layout_ == Layout_Prompt )
            return;

        REAL const left = RowLeft(), right = RowRight();
        REAL const top = rowY_ + RowHalfHeight(), bottom = rowY_ - RowHalfHeight();

        if ( rowPrimary_ )
        {
            // the one primary action: lime, always; focus adds an outline
            Rect( left, top, right, bottom, accent, alpha, controlRadius );
            if ( rowSelected_ )
            {
                REAL const gapW = W( 4 ), gapH = H( 4 );
                Frame( left - gapW, top + gapH, right + gapW, bottom - gapH, 2,
                       textPrimary, alpha );
            }
        }
        else if ( rowSelected_ )
        {
            // selection and keyboard focus: graphite with a lime edge
            Rect( left, top, right, bottom, surfaceSelected, alpha );
            Rect( left, top, left + W( 3 ), bottom, accent, alpha );
        }

        REAL const x = ValueX();
        REAL const y = rowY_;
        switch ( rowControl_ )
        {
        case Control_Toggle:
        {
            REAL const halfH = H( 10 );
            Box( x, y + halfH, x + W( togglePx ), y - halfH, canvas,
                 on ? accent : borderStrong, alpha );
            REAL const thumb = on ? x + W( togglePx - 17 ) : x + W( 3 );
            Rect( thumb, y + H( 7 ), thumb + W( 14 ), y - H( 7 ),
                  on ? accent : textSecondary, alpha, controlRadius - 1 );
            break;
        }
        case Control_Slider:
        {
            if ( fraction < 0 )
                break;
            REAL const f = std::min( 1.0f, fraction );
            REAL const end = x + W( sliderPx );
            REAL const at = x + ( end - x ) * f;
            Rect( x, y + H( 2 ), end, y - H( 2 ), borderStrong, alpha );
            Rect( x, y + H( 2 ), at, y - H( 2 ),
                  rowSelected_ ? accent : textSecondary, alpha );
            Rect( at - W( 3 ), y + H( 9 ), at + W( 3 ), y - H( 9 ), textPrimary, alpha,
                  controlRadius - 1 );
            break;
        }
        case Control_Text:
        case Control_Binding:
        {
            REAL const halfH = H( 16 );
            Box( x, y + halfH, ControlRight(), y - halfH, canvas,
                 rowSelected_ ? accent : borderStrong, alpha );
            break;
        }
        default:
            break;
        }
    }

    void RowText( int kind, REAL x, REAL y, char const * text, bool selected, REAL alpha,
                  int cursor, int cursorPos, int colorMode )
    {
        if ( !text )
            return;

        if ( layout_ == Layout_Prompt )
        {
            // chat and console: a quiet label, then what is being typed
            REAL const left = X( MarginPx() );
            if ( kind > 0 )
            {
                Text( left, y, 16, weightRegular, textSecondary, alpha, text );
                promptValueX_ = left + TextWidth( text, 16, weightRegular ) + W( 14 );
                return;
            }

            REAL cellW, cellH;
            Cell( sizeSetting, cellW, cellH );
            rTextStyle style( weightRegular, 0, false );
            REAL const start = kind < 0 ? promptValueX_ : left;
            SetTextColor( textPrimary, alpha );
            rTextField field( start, y + cellH * .5f, cellW, cellH );
            field.SetWidth( 1000000 );
            if ( cursor )
                field.SetCursor( cursor, cursorPos );
            field.StringOutput( text, static_cast< rTextField::ColorMode >( colorMode ) );
            return;
        }

        // Authored colours (a colour's name, a player's name) stay, lifted
        // where they would sink into the surface. Text being edited is
        // shown exactly as typed.
        tString readable;
        if ( !cursor )
        {
            readable = ReadableColors( tString( text ), selected ? .45f : .24f );
            text = readable;
        }

        // where the menu slid the row to as it entered
        REAL const base = kind > 0 ? x + .02f : kind < 0 ? x - .02f : x;
        REAL const slide = base - LabelX();
        REAL const rowEnd = RowRight() - W( rowPadPx );

        if ( kind > 0 )
        {
            // A setting's label. The control column separates it from its
            // value, so the colon many labels still end in is left out.
            tString label( text );
            int length = label.Len() - 1;
            while ( length > 0 && ( label[length-1] == ':' || label[length-1] == ' ' ) )
                --length;
            label = label.SubStr( 0, length );
            Text( LabelX() + slide, y, sizeSetting, weightRegular, textPrimary, alpha, label,
                  -1, ValueOffset() - W( columnGapPx ) );
            return;
        }

        if ( kind == 0 )
        {
            // an action: navigation rows are larger than rows among settings
            bool const table = ( layout_ == Layout_Wide );
            REAL const size = table ? sizeTable : values_ ? sizeSetting : sizeAction;
            Text( LabelX() + slide, y, size, weightMedium,
                  rowPrimary_ ? onAccent : textPrimary, alpha, text,
                  -1, rowEnd - ( LabelX() + slide ) );
            return;
        }

        // a value, placed after its control
        REAL start = ValueX() + slide;
        switch ( rowControl_ )
        {
        case Control_Toggle:   start += W( togglePx + 12 ); break;
        case Control_Slider:   start += W( sliderPx + 16 ); break;
        case Control_Text:
        case Control_Binding:  start += W( 12 ); break;
        case Control_Selector: start += W( 20 ); break;
        default: break;
        }
        REAL room = rowEnd - start - ( rowControl_ == Control_Selector ? W( 20 ) : 0 );
        if ( rowControl_ == Control_Text || rowControl_ == Control_Binding )
            room = ControlRight() - W( 12 ) - start;
        Color const & color = selected ? textPrimary : textSecondary;

        if ( cursor || rowControl_ == Control_Text )
        {
            // text being edited keeps its case and is never cut; long input
            // gets smaller rather than leaving its box
            REAL cellW, cellH;
            Cell( sizeSetting, cellW, cellH );
            rTextStyle style( weightRegular, 0, false );
            rTextField::ColorMode const mode = static_cast< rTextField::ColorMode >( colorMode );
            REAL const width = rTextField::TextWidth( text, cellW, cellH, mode );
            if ( width > room && width > 0 )
            {
                REAL const shrink = std::max( .5f, room / width );
                cellW *= shrink;
                cellH *= shrink;
            }
            SetTextColor( color, alpha );
            rTextField field( start, y + cellH * .5f, cellW, cellH );
            field.SetWidth( 1000000 );
            if ( cursor )
                field.SetCursor( cursor, cursorPos );
            field.StringOutput( text, mode );
            return;
        }

        Text( start, y, sizeSetting, weightRegular, color, alpha, text, -1, room );

        if ( rowControl_ == Control_Selector && selected )
        {
            // the keys that change it
            REAL const width = std::min( room, TextWidth( text, sizeSetting, weightRegular ) );
            Text( start - W( 8 ), y, sizeSetting, weightRegular, accent, alpha, "\x8b", 1 );
            Text( start + width + W( 8 ), y, sizeSetting, weightRegular, accent, alpha, "\x9b" );
        }
    }

    void DrawHelp( tString const & help, REAL alpha )
    {
        // a table's footer shows the selected row's details instead
        if ( layout_ == Layout_Prompt || layout_ == Layout_Wide || help.Len() <= 1 )
            return;

        Paragraph( X( MarginPx() ), Y( LogicalHeight() - footerPx + 18 ), W( ColumnPx() ),
                   sizeHelp, weightRegular, textSecondary, alpha, help, 3 );
    }

    void DrawScrollMarks( bool above, bool below, REAL alpha )
    {
        // small arrowheads at the list's right end
        REAL const x = RowRight() - W( 10 );
        REAL const w = W( 7 ), h = H( 7 );
        RenderEnd();
        glDisable( GL_TEXTURE_2D );
        if ( above )
        {
            REAL const y = MenuTop() + H( 14 );
            BeginTriangles();
            ::Color( textSecondary.r, textSecondary.g, textSecondary.b, alpha );
            Vertex( x - w, y - h );
            Vertex( x + w, y - h );
            Vertex( x, y );
            RenderEnd();
        }
        if ( below )
        {
            REAL const y = MenuBottom() - H( 14 );
            BeginTriangles();
            ::Color( textSecondary.r, textSecondary.g, textSecondary.b, alpha );
            Vertex( x - w, y + h );
            Vertex( x + w, y + h );
            Vertex( x, y );
            RenderEnd();
        }
    }

    int DrawDialog( tString const & title, tString const & body, int firstLine, REAL alpha )
    {
        Configure( Layout_Page, true );
        DrawBackground( alpha );

        REAL const left = X( MarginPx() );
        DrawWordmark( alpha );

        // the title may be long (an error sentence): let it wrap
        REAL const titleLine = 26 / emPerCell;
        int const titleLines = Paragraph( left, Y( 138 ), W( ColumnPx() ), 26, weightMedium,
                                          textPrimary, alpha, title, 3 );
        REAL const bodyTop = 138 + std::min( 3, std::max( 1, titleLines ) ) * titleLine + 20;
        int const room = std::max( 1, static_cast< int >(
            ( LogicalHeight() - 90 - bodyTop ) / ( sizeSetting / emPerCell ) ) );

        // measure first: scrolling stops when the last line is in view
        int const total = Paragraph( left, Y( bodyTop ), W( ColumnPx() ), sizeSetting,
                                     weightRegular, textSecondary, 0, body );
        int const lastStart = std::max( 0, total - room );
        firstLine = std::max( 0, std::min( firstLine, lastStart ) );
        Paragraph( left, Y( bodyTop ), W( ColumnPx() ), sizeSetting, weightRegular,
                   textSecondary, alpha, body, room, firstLine );

        Text( left, Y( LogicalHeight() - 44 ), sizeHint, weightRegular, textSecondary,
              .8f * alpha, lastStart > 0
                  ? "any key continue  \xb7  arrows scroll  \xb7  esc back"
                  : "any key continue  \xb7  esc back" );
        return lastStart;
    }

    // -------------------------------------------------- colours for others

    void SetLabelColor( bool selected, REAL alpha )
    {
        (void)selected;
        SetTextColor( textPrimary, alpha );
    }

    void SetValueColor( bool selected, REAL alpha )
    {
        SetTextColor( selected ? textPrimary : textSecondary, alpha );
    }

    void SetBodyColor( REAL alpha )
    {
        SetTextColor( textSecondary, alpha );
    }

#else   // DEDICATED: there is no interface to draw

    void Text( REAL, REAL, REAL, int, Color const &, REAL, char const *, int, REAL, REAL ) {}
    REAL TextWidth( char const *, REAL, int, REAL ) { return 0; }
    int Paragraph( REAL, REAL, REAL, REAL, int, Color const &, REAL, char const *, int, int ) { return 0; }
    void DrawBackground( REAL ) {}
    void DrawChrome( tString const &, tString const &, REAL ) {}
    void BeginRow( REAL, bool, bool, int ) {}
    void DrawRow( REAL, bool, REAL ) {}
    void RowText( int, REAL, REAL, char const *, bool, REAL, int, int, int ) {}
    void DrawHelp( tString const &, REAL ) {}
    void DrawScrollMarks( bool, bool, REAL ) {}
    int DrawDialog( tString const &, tString const &, int, REAL ) { return 0; }
    void SetLabelColor( bool, REAL ) {}
    void SetValueColor( bool, REAL ) {}
    void SetBodyColor( REAL ) {}

#endif
}
