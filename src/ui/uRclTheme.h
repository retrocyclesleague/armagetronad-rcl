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

#ifndef ArmageTron_RCL_THEME_H
#define ArmageTron_RCL_THEME_H

#include "defs.h"
#include "tString.h"

//! The RCL client's interface language: one palette, one type scale and one
//! set of row and control drawings, shared by every menu, prompt and dialog.
//!
//! Layout is written in logical pixels. A logical pixel is one real pixel on
//! a 1080-line display and scales with the window, but never below the size
//! where text stays readable; smaller windows scroll instead.
//!
//! Brand reference: the RCL component kit. The palette is the kit's inverse
//! (dark) treatment with one lime, #EEFF41.
namespace uRclTheme
{
    struct Color
    {
        REAL r, g, b;
    };

    extern Color const canvas;          //!< #141617 outer background
    extern Color const surface;         //!< #202122 principal panels
    extern Color const surfaceRaised;   //!< #3B3D3F secondary controls
    extern Color const surfaceSelected; //!< #5B5D60 the selected row
    extern Color const borderSubtle;    //!< #3D3E3F quiet separation
    extern Color const borderStrong;    //!< #646668 control outlines
    extern Color const textPrimary;     //!< #FAFAFA
    extern Color const textSecondary;   //!< #B2B4B5 supporting text
    extern Color const accent;          //!< #EEFF41 primary action, selection, focus
    extern Color const onAccent;        //!< #202122 text on the accent
    extern Color const danger;          //!< errors, lifted for dark surfaces

    enum Layout
    {
        Layout_Home,    //!< the main menu: wordmark and a navigation column
        Layout_Page,    //!< any other menu: a title and rows in a column
        Layout_Wide,    //!< a table across the window (the server browser)
        Layout_Prompt   //!< one input row along the bottom (chat, console)
    };

    //! what a row shows next to its label
    enum Control
    {
        Control_None,       //!< nothing: a plain action
        Control_Toggle,
        Control_Selector,   //!< one of several named values
        Control_Slider,     //!< a number in a range
        Control_Text,       //!< editable text
        Control_Binding     //!< the keys bound to an action
    };

    //! Sets the layout the functions below work with. Call it at the start
    //! of every frame of a menu; values says the page has rows with a control.
    void Configure( Layout layout, bool values );

    // ---- units: logical pixels to field coordinates (-1..1) ----
    REAL Scale();           //!< real pixels per logical pixel
    REAL X( REAL px );      //!< from the left edge
    REAL Y( REAL px );      //!< from the top edge
    REAL W( REAL px );
    REAL H( REAL px );
    REAL LogicalWidth();
    REAL LogicalHeight();
    REAL MarginPx();        //!< left margin of the content column
    REAL ColumnPx();        //!< width of the content column

    // ---- geometry of the current layout, in field coordinates ----
    REAL MenuTop();         //!< top of the row list
    REAL MenuBottom();      //!< bottom of the row list
    REAL RowLeft();
    REAL RowRight();
    REAL LabelX();          //!< where row labels start
    REAL ValueOffset();     //!< from the label to the control column
    REAL ValueX();          //!< where controls start
    REAL RowPitch();        //!< from one row to the next
    REAL RowHalfHeight();
    REAL ScrollEdge();      //!< first and last row keep this distance from the list's ends
    REAL ScrollMargin();    //!< the keyboard keeps the selected row this far inside the list
    REAL SliderLeft();      //!< the slider track, for the mouse
    REAL SliderRight();
    REAL ControlRight();    //!< where text boxes end
    REAL PromptTop();
    REAL PromptBottom();
    REAL PromptRowY();

    REAL EaseIn( REAL progress );
    tString FirstLine( tString const & text );

    //! Authored colours (in server and player names) stay, but every colour
    //! code in text is lifted until it is at least this bright, so no part of
    //! a name disappears into a dark surface. 0 is black, 1 is white.
    tString ReadableColors( tString const & text, REAL minLuminance );

    // ---- text ----
    //! One line of text. x is its left edge, centre or right edge for align
    //! -1, 0 or 1; y is its vertical centre. With a positive maxWidth, text
    //! that is too long ends in an ellipsis. weight is an rUiFont::Weight,
    //! tracking is in em.
    void Text( REAL x, REAL y, REAL sizePx, int weight, Color const & color, REAL alpha,
               char const * text, int align = -1, REAL maxWidth = 0, REAL tracking = 0 );
    REAL TextWidth( char const * text, REAL sizePx, int weight, REAL tracking = 0 );

    //! Word-wrapped copy starting at the top left corner x, top. Draws at
    //! most maxLines lines (0: all), starting with line firstLine, and
    //! returns how many lines the whole text has.
    int Paragraph( REAL x, REAL top, REAL width, REAL sizePx, int weight, Color const & color,
                   REAL alpha, char const * text, int maxLines = 0, int firstLine = 0 );

    // ---- drawing a menu frame, in this order ----
    void NoteSceneBehind();     //!< a live scene (game or replay) was drawn behind the menu this frame
    void DrawBackground( REAL alpha );
    void DrawChrome( tString const & title, tString const & footnote, REAL alpha );

    //! the row the following row and text calls belong to
    void BeginRow( REAL y, bool selected, bool primary, int control );
    //! the row's surface and its control; on and fraction are the control's state
    void DrawRow( REAL alpha, bool on, REAL fraction );
    //! A row's text. kind is positive for a label, negative for a value and
    //! zero for an action. x is where the menu put it, y its centre.
    void RowText( int kind, REAL x, REAL y, char const * text, bool selected, REAL alpha,
                  int cursor, int cursorPos, int colorMode );

    void DrawHelp( tString const & help, REAL alpha );
    void DrawScrollMarks( bool above, bool below, REAL alpha );

    //! A whole dialog frame: background, title, wrapped body and key hint.
    //! The body is shown from its line firstLine on. Returns the largest
    //! firstLine that still fills the frame, for the caller's scrolling.
    int DrawDialog( tString const & title, tString const & body, int firstLine, REAL alpha );

    //! the label colour, for menu items that draw their own text
    void SetLabelColor( bool selected, REAL alpha );
}

#endif
