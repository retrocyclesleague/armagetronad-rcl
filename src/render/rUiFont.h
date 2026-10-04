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

#ifndef ArmageTron_UI_FONT_H
#define ArmageTron_UI_FONT_H

#include "defs.h"

//! The interface typeface, Space Grotesk, drawn from prebuilt atlases with the
//! font's real glyph advances. There is one atlas per weight and pixel size
//! (see scripts/generate-rcl-ui-font.py); text is drawn from the size nearest
//! to the one asked for, texel for pixel, so it stays sharp.
namespace rUiFont
{
    enum Weight
    {
        Regular  = 400,
        Medium   = 500,
        SemiBold = 600,
        Bold     = 700      //!< only holds the letters of the wordmark
    };

    //! part of a cell's height the em square takes, so that the font's
    //! ascender-to-descender box fills a text cell
    extern REAL const emPerCell;

    class Face;             //!< one weight at one pixel size

    //! true when the atlases are installed and RCL_UI_FONT is on
    bool Available();

    //! The face to draw text of the given em size (in pixels) with. scale is
    //! the factor to draw it at; it is exactly 1 unless the size lies outside
    //! the range the atlases cover. Returns NULL when the font is unavailable.
    Face const * Pick( int weight, REAL emPixels, REAL & scale );

    int  Ascent( Face const * face );       //!< baseline to top of the line box, in face pixels
    int  Descent( Face const * face );      //!< baseline to bottom of the line box, in face pixels
    bool Has( Face const * face, unsigned char c );
    REAL Advance( Face const * face, unsigned char c );  //!< pen advance in face pixels

#ifndef DEDICATED
    //! binds the face's atlas for the quads that follow
    void Select( Face const * face );

    //! Emits one glyph as a textured quad inside a BeginQuads() block. x is
    //! the pen position and y the baseline; pixelW and pixelH are the size of
    //! one face pixel in the caller's units (y grows upwards).
    void Quad( Face const * face, unsigned char c, REAL x, REAL y, REAL pixelW, REAL pixelH );
#endif

    //! drops the atlas textures; they reload on demand
    void Unload();
}

#endif
