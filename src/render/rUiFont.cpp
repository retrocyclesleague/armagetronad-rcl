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

#include "rUiFont.h"

#include "rSDL.h"

#include "rScreen.h"
#include "rTexture.h"
#include "tConfiguration.h"
#include "tDirectories.h"

#ifndef DEDICATED
#include "rRender.h"
#endif

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

// Space Grotesk's ascender-to-descender box is 1.3 em tall.
REAL const rUiFont::emPerCell = .78f;

static bool sr_uiFont = true;
static tConfItem<bool> sr_uiFontConf( "RCL_UI_FONT", sr_uiFont );

namespace
{
    char const * const metricsFile = "textures/ui/space-grotesk.fnt";

    struct Glyph
    {
        short x, y, w, h;   // box in the atlas
        short bx, by;       // from the pen to the box's left edge, from the baseline up to its top
        float advance;
        bool present;
    };

#ifndef DEDICATED
    // Coverage lives in the alpha channel. Without alpha blending the colour
    // has to carry it instead, as it does for the legacy font.
    class Atlas: public rFileTexture
    {
    public:
        explicit Atlas( char const * fileName )
        : rFileTexture( rTextureGroups::TEX_FONT, fileName, 0, 0 )
        {
            StoreAlpha();
        }

    protected:
        virtual void ProcessImage( SDL_Surface * surface )
        {
            if ( sr_alphaBlend )
                return;

            GLubyte * pixels = reinterpret_cast< GLubyte * >( surface->pixels );
            if ( surface->format->BytesPerPixel == 4 )
            {
                for ( int i = surface->w * surface->h - 1; i >= 0; --i )
                {
                    GLubyte const alpha = pixels[4*i+3];
                    pixels[4*i  ] = ( alpha * pixels[4*i  ] ) >> 8;
                    pixels[4*i+1] = ( alpha * pixels[4*i+1] ) >> 8;
                    pixels[4*i+2] = ( alpha * pixels[4*i+2] ) >> 8;
                }
            }
            else if ( surface->format->BytesPerPixel == 2 )
            {
                for ( int i = surface->w * surface->h - 1; i >= 0; --i )
                    pixels[2*i] = ( pixels[2*i+1] * pixels[2*i] ) >> 8;
            }
        }
    };
#endif
}

class rUiFont::Face
{
public:
    int weight, size, ascent, descent, width, height;
    std::string file;
    Glyph glyphs[256];
#ifndef DEDICATED
    mutable Atlas * atlas;
#endif

    Face(): weight(0), size(0), ascent(0), descent(0), width(1), height(1)
    {
        for ( int i = 0; i < 256; ++i )
        {
            Glyph const none = { 0, 0, 0, 0, 0, 0, 0, false };
            glyphs[i] = none;
        }
#ifndef DEDICATED
        atlas = 0;
#endif
    }
};

namespace
{
    // Faces stay at fixed addresses once loaded: text fields keep pointers.
    std::vector< rUiFont::Face * > & Faces()
    {
        static std::vector< rUiFont::Face * > faces;
        return faces;
    }

    bool LoadMetrics()
    {
        std::ifstream in;
        if ( !tDirectories::Data().Open( in, metricsFile ) )
            return false;

        std::string word;
        int version = 0;
        if ( !( in >> word >> version ) || word != "RCLFONT" || version != 1 )
            return false;

        rUiFont::Face * face = 0;
        while ( in >> word )
        {
            if ( word == "face" )
            {
                int glyphCount = 0;
                face = new rUiFont::Face;
                in >> face->weight >> face->size >> face->ascent >> face->descent
                   >> face->width >> face->height >> glyphCount >> face->file;
                if ( !in || face->size <= 0 || face->width <= 0 || face->height <= 0 )
                {
                    delete face;
                    return false;
                }
                Faces().push_back( face );
            }
            else if ( word == "g" && face )
            {
                int code = 0, x = 0, y = 0, w = 0, h = 0, bx = 0, by = 0;
                float advance = 0;
                in >> code >> x >> y >> w >> h >> bx >> by >> advance;
                if ( !in || code < 0 || code > 255 )
                    return false;
                Glyph const glyph = { short(x), short(y), short(w), short(h),
                                      short(bx), short(by), advance, true };
                face->glyphs[code] = glyph;
            }
            else
            {
                return false;
            }
        }

        return !Faces().empty();
    }

    bool Loaded()
    {
        static bool tried = false, ok = false;
        if ( !tried )
        {
            tried = true;
            ok = LoadMetrics();
        }
        return ok;
    }
}

bool rUiFont::Available()
{
    return sr_uiFont && Loaded();
}

rUiFont::Face const * rUiFont::Pick( int weight, REAL emPixels, REAL & scale )
{
    scale = 1;
    if ( !Available() || !( emPixels > 0 ) )
        return 0;

    std::vector< Face * > const & faces = Faces();

    // the nearest weight the atlases hold
    int bestWeight = -1;
    for ( size_t i = 0; i < faces.size(); ++i )
    {
        if ( bestWeight < 0 ||
             abs( faces[i]->weight - weight ) < abs( bestWeight - weight ) )
            bestWeight = faces[i]->weight;
    }

    // the size nearest in proportion, and the smallest one at least as large
    Face const * nearest = 0;
    Face const * larger = 0;
    Face const * largest = 0;
    REAL nearestError = 1E+30f;
    for ( size_t i = 0; i < faces.size(); ++i )
    {
        Face const * face = faces[i];
        if ( face->weight != bestWeight )
            continue;

        REAL const error = fabsf( logf( face->size / emPixels ) );
        if ( error < nearestError )
        {
            nearestError = error;
            nearest = face;
        }
        if ( face->size >= emPixels && ( !larger || face->size < larger->size ) )
            larger = face;
        if ( !largest || face->size > largest->size )
            largest = face;
    }
    if ( !nearest )
        return 0;

    // Close enough: draw that size as it is, texel for pixel. The ladder of
    // sizes is dense enough that this is the usual case.
    if ( nearestError <= logf( 1.085f ) )
        return nearest;

    // Out of range: scale the next larger size down, or the largest one up.
    Face const * face = larger ? larger : largest;
    scale = emPixels / face->size;
    return face;
}

int rUiFont::Ascent( Face const * face )
{
    return face->ascent;
}

int rUiFont::Descent( Face const * face )
{
    return face->descent;
}

bool rUiFont::Has( Face const * face, unsigned char c )
{
    return face->glyphs[c].present;
}

REAL rUiFont::Advance( Face const * face, unsigned char c )
{
    Glyph const & glyph = face->glyphs[c];
    if ( glyph.present )
        return glyph.advance;

    // no glyph: non-breaking space reads as a space, the rest as '?'
    if ( c == 0xA0 || c == '\t' )
        return face->glyphs[' '].advance;
    return face->glyphs['?'].present ? face->glyphs['?'].advance : 0;
}

#ifndef DEDICATED
void rUiFont::Select( Face const * face )
{
    if ( !face->atlas )
    {
        std::string const path = std::string( "textures/ui/" ) + face->file;
        face->atlas = new Atlas( path.c_str() );
    }
    face->atlas->Select( true );
}

void rUiFont::Quad( Face const * face, unsigned char c, REAL x, REAL y, REAL pixelW, REAL pixelH )
{
    Glyph const * glyph = &face->glyphs[c];
    if ( !glyph->present )
    {
        if ( c == 0xA0 || c == '\t' || c == ' ' )
            return;
        glyph = &face->glyphs['?'];
    }
    if ( glyph->w <= 0 || glyph->h <= 0 )
        return;

    REAL const left   = x + glyph->bx * pixelW;
    REAL const right  = left + glyph->w * pixelW;
    REAL const top    = y + glyph->by * pixelH;
    REAL const bottom = top - glyph->h * pixelH;

    REAL const u0 = REAL( glyph->x ) / face->width;
    REAL const u1 = REAL( glyph->x + glyph->w ) / face->width;
    REAL const v0 = REAL( glyph->y ) / face->height;
    REAL const v1 = REAL( glyph->y + glyph->h ) / face->height;

    glTexCoord2f( u1, v1 );
    glVertex2f( right, bottom );

    glTexCoord2f( u1, v0 );
    glVertex2f( right, top );

    glTexCoord2f( u0, v0 );
    glVertex2f( left, top );

    glTexCoord2f( u0, v1 );
    glVertex2f( left, bottom );
}
#endif

void rUiFont::Unload()
{
#ifndef DEDICATED
    std::vector< Face * > const & faces = Faces();
    for ( size_t i = 0; i < faces.size(); ++i )
    {
        if ( faces[i]->atlas )
            faces[i]->atlas->Unload();
    }
#endif
}
