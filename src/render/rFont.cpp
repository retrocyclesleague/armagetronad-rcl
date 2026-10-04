/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

***************************************************************************

*/

#include "rFont.h"
#include "rScreen.h"
#include "rUiFont.h"
#include "tConfiguration.h"
#include "tColor.h"
#include <ctype.h>
#include <math.h>

#ifndef DEDICATED
#include "rRender.h"
//#include <GL/gl>
//#include <SDL>
#endif

/*
#include "nConfig.h"

tString lala_font_extra("Anonymous/original/textures/font_extra.png");
static nSettingItem<tString> lalala_font_extra("TEXTURE_FONT_EXTRA", lala_font_extra);
static rFont sr_lowerPartFont(lala_font_extra);
static rFont sr_lowerPartFont("Anonymous/original/textures/font_extra.png");

tString lala_defaultFont("Anonymous/original/textures/font.png");
static nSettingItem<tString> lalala_defaultFont("TEXTURE_DEFAULT_FONT", lala_defaultFont);
rFont rFont::s_defaultFont(lala_defaultFont, &sr_lowerPartFont);
rFont rFont::s_defaultFont("Anonymous/original/textures/font.png", &sr_lowerPartFont);

tString lala_defaultFontSmall("Anonymous/original/textures/font_s.png");
static nSettingItem<tString> lalala_defaultFontSmall("TEXTURE_DEFAULT_FONT_SMALL", lala_defaultFontSmall);
rFont rFont::s_defaultFontSmall(lala_defaultFontSmall, 32,5/128.0,9/128.0,1/128.0);
rFont rFont::s_defaultFontSmall("Anonymous/original/textures/font_s.png", 32,5/128.0,9/128.0,1/128.0);
*/

#ifndef DEDICATED
//! like strnlen, but that's nonstandard :-(
static size_t my_strnlen(char const *c, size_t i) {
	char const *begin = c;
	char const *end = c + i;
	for(; *c && c != end; ++c) ;
	return c - begin;
}
#endif

// RCL keeps the original Armagetronad typeface and atlas addressing, but
// rasterizes it at 1024x1024.  The legacy atlases only provided 32x64 pixels
// per regular glyph (and 20x36 for the small font), which becomes visibly
// soft on modern high-density displays.
static REAL const sr_rclFontPixel = 1.0f / 1024.0f;
static rFont sr_lowerPartFont("textures/font_extra_rcl.png", 0,
                             1/16.0f, 1/8.0f, sr_rclFontPixel);
rFont rFont::s_defaultFont("textures/font_rcl.png", 0,
                           1/16.0f, 1/8.0f, sr_rclFontPixel,
                           1, &sr_lowerPartFont);
rFont rFont::s_defaultFontSmall("textures/font_rcl.png", 0,
                                1/16.0f, 1/8.0f, sr_rclFontPixel,
                                1, &sr_lowerPartFont);
//rFont rFont::s_defaultFontSmall("textures/Font.png",0,16/256.0,32/256.0);
//rFont rFont::s_defaultFontSmall("textures/Font.png",0,1/16.0,1/8.0);

rFont::rFont(const char *fileName,int Offset,REAL CWidth,REAL CHeight,REAL op, int border, rFont *lower):
        rFileTexture(rTextureGroups::TEX_FONT,fileName,0,0),
        offset(Offset),cwidth(CWidth),cheight(CHeight),
        onepixel(op),borderExtension(border), lowerPart(lower)
{
    StoreAlpha();
}

rFont::rFont(const char *fileName, rFont *lower):
        rFileTexture(rTextureGroups::TEX_FONT,fileName,0,0),
        offset(0),cwidth(1/16.0),cheight(1/8.0),
        onepixel(1/256.0),lowerPart(lower)
{
    StoreAlpha();
}

rFont::~rFont(){}

// ******************************************************************************************
// *
// *	ProcessImage
// *
// ******************************************************************************************
//!
//!		@param	surface the surface to process
//!
// ******************************************************************************************

void rFont::ProcessImage( SDL_Surface * surface )
{
#ifndef DEDICATED
    if ( sr_alphaBlend )
        return;

    // pre-blend alpha values
    GLubyte *pixels =reinterpret_cast<GLubyte *>(surface->pixels);

    if (surface->format->BytesPerPixel == 4)
    {
        for (int i=surface->w*surface->h-1;i>=0;i--){
            GLubyte alpha=pixels[4*i+3];
            pixels[4*i  ] = (alpha * pixels[4*i  ]) >> 8;
            pixels[4*i+1] = (alpha * pixels[4*i+1]) >> 8;
            pixels[4*i+2] = (alpha * pixels[4*i+2]) >> 8;
        }
    }
    else if (surface->format->BytesPerPixel == 2)
    {
        for (int i=surface->w*surface->h-1;i>=0;i--){
            GLubyte alpha=pixels[2*i+1];
            pixels[2*i  ] = (alpha * pixels[2*i  ]) >> 8;
        }
    }
#endif
}

void rFont::OnSelect( bool enforce )
{
    rISurfaceTexture::OnSelect( enforce );
    if ( !Loaded() && sr_glOut )
    {
        // abort. It makes no sense to continue without a font.
        tERR_ERROR( "Font file " << this->GetFileName() << " could not be loaded.");
    }

#ifndef DEDICATED
    // wrap around so we can use the transparent pixel on the right side on the left
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
#endif
}

// displays c
#ifndef DEDICATED
static rFont * sr_lastSelected = 0;
void rFont::Render(unsigned char c,REAL left,REAL top,REAL right,REAL bot){
    //  if (c > 128 && this == &rFont::s_defaultFont)
    //rFont::s_defaultFontSmall.Render(c, left, top, right, bot);
    //  else
    // if(31<c && 256>c && sr_glOut)
    {
        c-=offset;

        int x=c%16;
        int y=c/16;

        REAL pix = onepixel *.1;
        if (rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] != GL_NEAREST && rTextureGroups::TextureMode[rTextureGroups::TEX_FONT] != GL_NEAREST_MIPMAP_NEAREST)
            pix = onepixel * .5;


        REAL ttop=y*cheight+pix;
        REAL tbot=(y+1)*cheight-pix;
        REAL tleft=x*cwidth+borderExtension*pix;
        REAL tright=(x+1)*cwidth-pix;

        rFont* select = this;
        while (ttop > .999 && select->lowerPart)
        {
            tbot -= 1;
            ttop -= 1;
            select = select->lowerPart;
        }
        if ( sr_lastSelected != select )
        {
            RenderEnd(true);
            select->Select(true);
            sr_lastSelected = select;
        }

        BeginQuads();

        glTexCoord2f(tright,tbot);
        glVertex2f(   right, bot);

        glTexCoord2f(tright,ttop);
        glVertex2f(   right ,top);

        glTexCoord2f(tleft,ttop);
        glVertex2f(   left, top);

        glTexCoord2f(tleft,tbot);
        glVertex2f(   left, bot);
    }
}
#endif

// **************************************************

static REAL sr_bigFontThresholdWidth  = 10;
static REAL sr_bigFontThresholdHeight = 20;

static tSettingItem< REAL > sr_bigFontThresholdWidthConf(  "FONT_BIG_THRESHOLD_WIDTH", sr_bigFontThresholdWidth );
static tSettingItem< REAL > sr_bigFontThresholdHeightConf( "FONT_BIG_THRESHOLD_HEIGHT", sr_bigFontThresholdHeight );

static REAL sr_smallFontThresholdWidth  = 5;
static REAL sr_smallFontThresholdHeight = 8;

static tSettingItem< REAL > sr_smallFontThresholdWidthConf(  "FONT_SMALL_THRESHOLD_WIDTH", sr_smallFontThresholdWidth );
static tSettingItem< REAL > sr_smallFontThresholdHeightConf( "FONT_SMALL_THRESHOLD_HEIGHT", sr_smallFontThresholdHeight );

// ---------------------------------------------------------------------------
// the proportional interface font
// ---------------------------------------------------------------------------

static int  sr_styleWeight   = rUiFont::Regular;
static REAL sr_styleTracking = 0;
static bool sr_styleShadow   = true;
static bool sr_fixedWidth    = false;

void rTextField::SetStyle( int weight, REAL tracking )
{
    sr_styleWeight = weight;
    sr_styleTracking = tracking;
}

int rTextField::GetStyleWeight(){ return sr_styleWeight; }
REAL rTextField::GetStyleTracking(){ return sr_styleTracking; }

void rTextField::SetShadow( bool shadow ){ sr_styleShadow = shadow; }
bool rTextField::GetShadow(){ return sr_styleShadow; }

void rTextField::SetFixedWidth( bool fixedWidth ){ sr_fixedWidth = fixedWidth; }
bool rTextField::GetFixedWidth(){ return sr_fixedWidth; }

#ifndef DEDICATED
namespace
{
    struct rUiPlacement
    {
        rUiFont::Face const * face;
        REAL pixelW, pixelH;    // one font pixel in field units
        REAL tracking;          // field units
        REAL baseline;          // from the top of a line down to the baseline
        bool snap;
    };
}

// Picks the interface face for a field with the given cells. Returns false
// when the field has to use the legacy font.
static bool sr_PlaceUiFont( REAL cwidth, REAL cheight, rUiPlacement & place )
{
    if ( sr_fixedWidth || !rUiFont::Available() )
        return false;

    // field units span -1..1 over the viewport
    REAL const unitW = sr_viewportPixelWidth * .5f;
    REAL const unitH = sr_viewportPixelHeight * .5f;
    if ( !( unitW > 0 && unitH > 0 ) )
        return false;

    REAL const cellW = cwidth * unitW;
    REAL const cellH = cheight * unitH;
    REAL em = cellH * rUiFont::emPerCell;

    // The legacy cell is half as wide as it is tall, and callers narrow it to
    // make text fit. Glyphs are never stretched: a narrowed cell makes the
    // text smaller instead. Proportional text is a little narrower than the
    // grid anyway, so only a clear squeeze has an effect.
    REAL const narrowest = cellH * .39f;
    if ( cellW < narrowest )
        em *= cellW / narrowest;

    REAL scale = 1;
    place.face = rUiFont::Pick( sr_styleWeight, em, scale );
    if ( !place.face )
        return false;

    place.pixelW = scale / unitW;
    place.pixelH = scale / unitH;
    place.tracking = sr_styleTracking * em / unitW;
    place.snap = ( scale == 1 );

    // centre the font's line box in the cell
    REAL const box = ( rUiFont::Ascent( place.face ) + rUiFont::Descent( place.face ) ) * scale;
    place.baseline = ( ( cellH - box ) * .5f + rUiFont::Ascent( place.face ) * scale ) / unitH;
    return true;
}

// moves a coordinate to the nearest screen pixel edge
static inline REAL sr_SnapToPixel( REAL xy, int viewportPixels )
{
    REAL const unit = viewportPixels * .5f;
    return floorf( ( xy + 1 ) * unit + .5f ) / unit - 1;
}
#endif

REAL rTextField::CharAdvance(unsigned char c) const
{
    return rUiFont::Advance( static_cast< rUiFont::Face const * >( ui_ ), c ) * uiPixelW_ + uiTracking_;
}

REAL rTextField::TextWidth( const char * text, REAL cwidth, REAL cheight, ColorMode colorMode )
{
#ifndef DEDICATED
    rUiPlacement place;
    if ( sr_PlaceUiFont( cwidth, cheight, place ) )
    {
        REAL width = 0;
        for ( char const * c = text; *c != '\0' && *c != '\n'; )
        {
            if ( colorMode == COLOR_USE && c[0] == '0' && c[1] == 'x' && my_strnlen( c, 8 ) >= 8 )
            {
                c += 8;
                continue;
            }
            width += rUiFont::Advance( place.face, static_cast< unsigned char >( *c ) ) * place.pixelW
                     + place.tracking;
            ++c;
        }
        return width;
    }
#endif

    // the cell grid: every character that is not part of a colour code
    int length = strlen( text );
    if ( colorMode == COLOR_USE )
    {
        for ( char const * c = text; *c != 0; ++c )
        {
            if ( *c == '0' && c[1] == 'x' )
                length -= 8;
        }
    }
    return length * cwidth;
}

rTextField::rTextField(REAL Left,REAL Top,
                       REAL Cwidth,REAL Cheight,
                       rFont *f)
        :parIndent(0),
        left(Left),top(Top),cwidth(Cwidth),cheight(Cheight),
        F(f),x(0),y(0),realx(0),cursor(0),cursorPos(0),
        ui_(0),uiPixelW_(0),uiPixelH_(0),uiTracking_(0),uiBaseline_(0),uiSnap_(false),
        lineAdvance_(0),blankRun_(0),table_(false){
    if ( cwidth*sr_screenWidth < sr_bigFontThresholdWidth*2 || cheight*sr_screenHeight < sr_bigFontThresholdHeight*2 )
        F=&rFont::s_defaultFontSmall;
    if (cwidth*sr_screenWidth <= sr_smallFontThresholdWidth*2 + 1E-4)
    {
        cwidth=sr_smallFontThresholdWidth*2/REAL(sr_screenWidth);

        // try to place font at exact pixels
        left = Pixelize(left, sr_screenWidth);
    }
    if (cheight*sr_screenHeight <= sr_smallFontThresholdHeight*2 + 1E-4)
    {
        cheight=sr_smallFontThresholdHeight*2/REAL(sr_screenHeight);

        // try to place font at exact pixels
        top = Pixelize(top, sr_screenHeight);
    }

    color_ = defaultColor_;

    width = int((1-Left)/cwidth);

#ifndef DEDICATED
    {
        rUiPlacement place;
        if ( sr_PlaceUiFont( cwidth, cheight, place ) )
        {
            ui_ = place.face;
            uiPixelW_ = place.pixelW;
            uiPixelH_ = place.pixelH;
            uiTracking_ = place.tracking;
            uiBaseline_ = place.baseline;
            uiSnap_ = place.snap;
        }
    }
#endif

    buffer.SetLen(0);
    /*
    top=(int(top*sr_screenHeight)+.5)/REAL(sr_screenHeight);
    left=(int(left*sr_screenWidth)+.5)/REAL(sr_screenWidth);
    */

    cursor_x = -100;
    cursor_y = -100;
}

REAL rTextField::AspectWidthMultiplier()
{
    return std::min(((4.0f/3.0f)*sr_screenHeight)/sr_screenWidth,1.0f);
}

REAL rTextField::AspectHeightMultiplier()
{
    return std::min(((3.0f/4.0f)*sr_screenWidth)/sr_screenHeight,1.0f);
}

REAL rTextField::Pixelize(REAL xy, int WidthHeight)
{
    auto pixelIn = static_cast<int>(.5f * xy * WidthHeight);
    return (2.0f*(pixelIn+.5f))/WidthHeight;
}


rTextField::~rTextField(){
#ifndef DEDICATED
    FlushLine();
    RenderEnd();

    if (cursor && sr_glOut){
        if (cursor==2)
            glColor4f(1,1,1,.5);
        else
            glColor3f(1,1,0);

        //    glDisable(GL_TEXTURE);
        glDisable(GL_TEXTURE_2D);

        BeginLines();
        glVertex2f(cursor_x,cursor_y);
        glVertex2f(cursor_x,cursor_y-cheight);
        RenderEnd();
    }
#endif
}

static bool sr_renderBrightBackground = false;
static tConfItem<bool> rbb("TEXT_BRIGHT_BACKGROUND",sr_renderBrightBackground);

static int sr_textShadow = 1;
static tConfItem<int> textShadowConf("TEXT_SHADOW", sr_textShadow);

void rTextField::FlushLine(int len,bool newline){
#ifndef DEDICATED
    // reload textures if alpha blending changed
    {
        static bool alphaBlendBefore = sr_alphaBlend;
        if ( alphaBlendBefore != sr_alphaBlend )
        {
            alphaBlendBefore = sr_alphaBlend;
            sr_lowerPartFont.Unload();
            rFont::s_defaultFont.Unload();
            rFont::s_defaultFontSmall.Unload();
            rUiFont::Unload();
        }
    }

    int i;

    REAL r = color_.r_;
    REAL g = color_.g_;
    REAL b = color_.b_;
    REAL a = color_.a_;

    if (sr_glOut && ui_)
    {
        // The interface font: every character sits where its predecessors'
        // advances put it, on whole pixels when the face is drawn unscaled.
        rUiFont::Face const * face = static_cast< rUiFont::Face const * >( ui_ );
        REAL const lineTop = top - y*cheight;
        REAL baseline = lineTop - uiBaseline_;
        if ( uiSnap_ )
            baseline = sr_SnapToPixel( baseline, sr_viewportPixelHeight );

        REAL const begin = left + ( realx < x ? starts_[realx] : lineAdvance_ );
        REAL const end   = left + ( realx + len < x ? starts_[realx + len] : lineAdvance_ );

        if ( color_.IsDark() && sr_renderBrightBackground )
        {
            RenderEnd(true);
            glDisable(GL_TEXTURE_2D);
            if ( sr_alphaBlend )
            {
                glColor4f( blendColor_.r_, blendColor_.g_, blendColor_.b_, a * blendColor_.a_ );
                BeginQuads();
                glVertex2f( begin, lineTop - cheight );
                glVertex2f( end,   lineTop - cheight );
                glVertex2f( end,   lineTop );
                glVertex2f( begin, lineTop );
            }
            else
            {
                if ( r < .5 ) r = .5;
                if ( g < .5 ) g = .5;
                if ( b < .5 ) b = .5;
            }
            RenderEnd(true);
        }

        if ( len > 0 )
        {
            RenderEnd(true);
            rUiFont::Select( face );
            sr_lastSelected = 0;

            if( sr_textShadow && sr_styleShadow && (
                ( color_.IsDark() && (sr_textShadow&1) ) ||
                (!color_.IsDark() && ( (sr_textShadow&2) || sr_cleanArena ) )
            ))
            {
                // one pixel down and to the right
                if( color_.IsDark() )
                    glColor4f( 1, 1, 1, blendColor_.a_*a );
                else
                    glColor4f( 0, 0, 0, blendColor_.a_*a );

                BeginQuads();
                for ( i = 0; i < len; ++i )
                {
                    REAL l = left + starts_[realx + i];
                    if ( uiSnap_ )
                        l = sr_SnapToPixel( l, sr_viewportPixelWidth );
                    rUiFont::Quad( face, buffer[realx + i], l + uiPixelW_, baseline - uiPixelH_,
                                   uiPixelW_, uiPixelH_ );
                }
                RenderEnd(true);
            }

            glColor4f(r * blendColor_.r_,g * blendColor_.g_,b * blendColor_.b_,a * blendColor_.a_);
            BeginQuads();
        }

        for (i=0;i<=len;i++){
            REAL l = left + ( realx < x ? starts_[realx] : lineAdvance_ );

            if (0 <= cursorPos--){
                cursor_x=l;
                cursor_y=lineTop;
            }
            if (i<len){
                if ( uiSnap_ )
                    l = sr_SnapToPixel( l, sr_viewportPixelWidth );
                rUiFont::Quad( face, buffer[realx], l, baseline, uiPixelW_, uiPixelH_ );
                realx++;
            }
        }
    }
    else if (sr_glOut)
    {
        // render bright background
        //this is kind of ugly
        if ( color_.IsDark() && sr_renderBrightBackground)
        {
            RenderEnd(true);
            glDisable(GL_TEXTURE_2D);
            if ( sr_alphaBlend )
            {
                glColor4f( blendColor_.r_, blendColor_.g_, blendColor_.b_, a * blendColor_.a_ );

                REAL l=left+realx*cwidth;
                REAL t=top-y*cheight;
                REAL r=l + cwidth * len;
                REAL b=t - cheight;

                BeginQuads();

                glVertex2f(   l, b);

                glVertex2f(   r, b);

                glVertex2f(   r ,t);

                glVertex2f(   l, t);
            }
            else
            {
                if ( r < .5 ) r = .5;
                if ( g < .5 ) g = .5;
                if ( b < .5 ) b = .5;
            }
            RenderEnd(true);
            glEnable(GL_TEXTURE_2D);
        }
        
        if( sr_textShadow && sr_styleShadow && (
            ( color_.IsDark() && (sr_textShadow&1) ) ||
            (!color_.IsDark() && ( (sr_textShadow&2) || sr_cleanArena ) )
        ))
        {
            RenderEnd(true);
            glEnable(GL_TEXTURE_2D);
            sr_lastSelected = 0;
            
            int fakex = realx;
            REAL l,t;
            
            if( color_.IsDark() )
            {
                glColor4f( 1, 1, 1, blendColor_.a_*a );
            }
            else
            {
                glColor4f( 0, 0, 0, blendColor_.a_*a );
            }
            
            #if 0
                {
                    static REAL offset[8][2] = {{0,1},{1,1},{1,0},{0,-1},{-1,-1},{-1,0},{-1,1},{1,-1}};
                    for(i=0;i<len;++i)
                    {
                        for(int z=0;z<8;++z)
                        {
                            l=(left+(0.002*offset[z][0]))+fakex*cwidth;
                            t=(top+(0.002*offset[z][0]))-y*cheight;
                            F->Render(buffer[fakex],l,t,l+cwidth,t-cheight);
                        }
                        fakex++;
                    }
                }
            #else
                    for(i=0;i<len;i++)
                    {
                        l=(left+0.0025)+fakex*cwidth;
                        t=(top-0.0025)-y*cheight;
                        F->Render(buffer[fakex],l,t,l+cwidth,t-cheight);
                        fakex++;
                    }
            #endif
        }


        if ( len > 0 )
        {
            RenderEnd(true);
            glColor4f(r * blendColor_.r_,g * blendColor_.g_,b * blendColor_.b_,a * blendColor_.a_);
            sr_lastSelected = 0;
        }
        for (i=0;i<=len;i++){
            REAL l=left+realx*cwidth;
            REAL t=top-y*cheight;

            if (0 <= cursorPos--){
                cursor_x=l;
                cursor_y=t;
            }
            if (i<len){
                F->Render(buffer[realx],l,t,l+cwidth,t-cheight);
                realx++;
            }
        }
    }

#endif
    /*
    for(i=0;i<buffer.Len()-len;i++)
      buffer[i]=buffer[i+len];

    buffer.SetLen(buffer.Len()-len);
    */

    if (newline){
        y++;
        realx=x=0;
        lineAdvance_=0;
        blankRun_=0;
        table_=false;
    }
    else
    {
        //      realx = 0;
        //      buffer.SetLen(0);
    }
    //    x+=len;
}

void rTextField::FlushLine(bool newline){
    FlushLine(buffer.Len()-realx,newline);
}

inline void rTextField::WriteChar(unsigned char c)
{
    switch (c){
    case('\n'):
                    FlushLine();
        buffer.SetLen(0);
        break;
    default:
        if ( ui_ )
        {
            // Padded columns: once a line is known to be a table, text that
            // follows a run of blanks starts on the cell grid, where the
            // fixed-width font would have put it. Everything else is placed
            // by the advances of what precedes it.
            bool const blank = ( c == ' ' );
            if ( !blank && table_ && blankRun_ >= 2 )
            {
                REAL const grid = x * cwidth;
                if ( grid > lineAdvance_ )
                    lineAdvance_ = grid;
            }

            if ( static_cast< int >( starts_.size() ) <= x )
                starts_.resize( x + 32 );
            starts_[x] = lineAdvance_;
            lineAdvance_ += CharAdvance( c );

            blankRun_ = blank ? blankRun_ + 1 : 0;
            if ( blankRun_ >= 3 )
                table_ = true;
        }
        buffer[x++]=c;
        break;
    }
}

/*
rTextField & rTextField::operator<<(unsigned char c){
    WriteChar( c );

    if (x>=width)
    {
        // overflow! insert newline
        int i=x-1;
        while (!isspace(buffer(i)) && i>0) i--;

        bool force=false;
        if (x-i>=width-parIndent){
            i=x;
            force=true;
        }


        FlushLine(i-realx);

        if (force)
            cursorPos++;

        for(int j=0;j<parIndent;j++){
            buffer[x++]=' ';
        }
        i++;
        while (i<width)
            buffer[x++]=buffer[i++];
        buffer.SetLen(x);
        buffer[x]='\0';
        if (cursorPos>=0)
            cursorPos+=parIndent;
    }
    return *this;
}
*/

rTextField & rTextField::StringOutput(const char * c, ColorMode colorMode )
{
#ifndef DEDICATED
    // run through string
    while (*c!='\0')
    {
        // A line with a run of three blanks has padded columns; lay it out as
        // a table from its start (see WriteChar).
        if ( ui_ && x == 0 && !table_ )
        {
            for ( char const * s = c; *s != '\0' && *s != '\n'; ++s )
            {
                if ( s[0] == ' ' && s[1] == ' ' && s[2] == ' ' )
                {
                    table_ = true;
                    break;
                }
            }
        }

        // break line if next space character is too far away
        if ( isblank(*c) )
        {
            // count number of nonblank characters following
            char const * nextSpace = c+1;
            int wordLen = 0;
            REAL wordAdvance = 0;
            while ( *nextSpace != '\0' && *nextSpace != '\n' && !isblank(*nextSpace) )
            {
                if (*nextSpace=='0' && my_strnlen(nextSpace, 8)>=8 && nextSpace[1]=='x' && colorMode != COLOR_IGNORE )
                {
                    // skip color code
                    nextSpace += 8;
                }
                else
                {
                    // count letter
                    if ( ui_ )
                        wordAdvance += CharAdvance( *nextSpace );
                    nextSpace++;
                    wordLen++;
                }
            }

            // see if the word plus the space fit into the current line
            bool const overflow = ui_
                ? ( x > 0 && lineAdvance_ + CharAdvance(' ') + wordAdvance > width * cwidth )
                : ( wordLen + x + 1 >= width );
            if ( overflow )
            {
                // no. Skip to the next line
                WriteChar('\n');
                c++;
                for ( int i = parIndent-1; i >= 0; --i )
                {
                    WriteChar(' ');
                    cursorPos++;
                }
                continue;
            }
        }

        // linebreak if line has gotten too long anyway
        if ( ui_ ? ( x > 0 && *c != '\n' && lineAdvance_ + CharAdvance(*c) > width * cwidth )
                 : ( x >= width ) )
        {
            WriteChar('\n');
            cursorPos += 1;
        }

        // detect presence of color code
        if (*c == '0' && my_strnlen(c, 8) >= 8 && c[1] == 'x' && colorMode != COLOR_IGNORE && (strncmp(c,"0xRESETT",8) == 0 || tColor::VerifyColorCode(c)))
        {
            tColor color;

            if ( 0 == strncmp(c,"0xRESETT",8) )
            {
                // color reset to default requested
                color = defaultColor_;
            }
            else
            {
                // found! extract colors
                tString colorStr(c);
                color = tColor(colorStr.ToLower());
            }

            // advance
            if ( colorMode == COLOR_USE )
            {
                c += 8;
                cursorPos -= 8;
            }
            else
            {
                // write color code out
                for(int i = 7; i >= 0; --i)
                    WriteChar(*(c++));
            }

            FlushLine(false);
            cursorPos++;
            color_ = color;
        }
        else
        {
            // normal operation: add char
            WriteChar(*(c++));
        }
    }

    RenderEnd( true );
#endif

    return *this;
}

void DisplayText(REAL x,REAL y,REAL w,REAL h,const char *text,int center,int cursor,int cursorPos, rTextField::ColorMode colorMode ){
    int colorlen = strlen(text);

    if ( colorMode == rTextField::COLOR_USE )
    {
        for(char const *c = text; *c != 0; ++c)
        {
            if(*c == '0' && c[1] == 'x')
                colorlen -= 8;
        }
    }

    // calculate top position so that does not move when we shrink the font
    REAL top = y + h*.5;

    // the width the text really takes; with the fixed-width font that is the
    // number of characters times the cell width
    REAL textWidth = rTextField::TextWidth( text, w, h, colorMode );

    // shrink fields that don't fit the screen
    REAL availw = 1.9f;
    if (center < 0) availw = (.9f-x);
    if (center > 0) availw = (x + .9f);
    if ( textWidth > availw && textWidth > 0 )
    {
        REAL const shrink = availw/textWidth;
        h *= shrink;
        w *= shrink;
        textWidth = rTextField::TextWidth( text, w, h, colorMode );
    }

    rTextField c(x-(center+1)*.5*textWidth,y+h*.5,w,h);
    if (center==-1)
        c.SetWidth(int((.95-x)/c.GetCWidth()));
    else
        c.SetWidth(10000);

    // did the text field enlarge the font? If yes, there will be wrapping; better make some more room
    if ( c.GetWidth() < colorlen )
    {
        c.SetTop( top );
    }

    c.SetIndent(5);
    if (cursor)
        c.SetCursor(cursor,cursorPos);
    c.StringOutput(text, colorMode );
}

void DisplayTextAutoWidth(REAL x, REAL y, const char *text, REAL h, int center, int cursor, int cursorPos, rTextField::ColorMode colorMode)
{
    DisplayText(x, y, h*(rCWIDTH_NORMAL/rCHEIGHT_NORMAL)*rTextField::AspectWidthMultiplier(), h, text, center, cursor, cursorPos, colorMode);
}

void DisplayTextAutoHeight(REAL x, REAL y, const char *text, REAL w, int center, int cursor, int cursorPos, rTextField::ColorMode colorMode)
{
    DisplayText(x, y, w, w*(rCHEIGHT_NORMAL/rCWIDTH_NORMAL)*rTextField::AspectHeightMultiplier(), text, center, cursor, cursorPos, colorMode);
}

// *******************************************************************************************
// *
// *	GetDefaultColor
// *
// *******************************************************************************************
//!
//!		@return		default color
//!
// *******************************************************************************************

tColor const & rTextField::GetDefaultColor( void )
{
    return defaultColor_;
}

// *******************************************************************************************
// *
// *	GetDefaultColor
// *
// *******************************************************************************************
//!
//!		@param	defaultColor	default color to fill
//!
// *******************************************************************************************

void rTextField::GetDefaultColor( tColor & defaultColor )
{
    defaultColor = defaultColor_;
}

// *******************************************************************************************
// *
// *	SetDefaultColor
// *
// *******************************************************************************************
//!
//!		@param	defaultColor	default color to set
//!
// *******************************************************************************************

void rTextField::SetDefaultColor( tColor const & defaultColor )
{
    defaultColor_ = defaultColor;
    if ( !sr_alphaBlend )
    {
        defaultColor_.r_ *= defaultColor_.a_;
        defaultColor_.g_ *= defaultColor_.a_;
        defaultColor_.b_ *= defaultColor_.a_;
        defaultColor_.a_ = 1;
    }
    blendColor_ = tColor();
}

// *******************************************************************************************
// *
// *	GetBlendColor
// *
// *******************************************************************************************
//!
//!		@return		color all other colors are multiplied with
//!
// *******************************************************************************************

tColor const & rTextField::GetBlendColor( void )
{
    return blendColor_;
}

// *******************************************************************************************
// *
// *	GetBlendColor
// *
// *******************************************************************************************
//!
//!		@param	blendColor	color all other colors are multiplied with to fill
//!
// *******************************************************************************************

void rTextField::GetBlendColor( tColor & blendColor )
{
    blendColor = blendColor_;
}

// *******************************************************************************************
// *
// *	SetBlendColor
// *
// *******************************************************************************************
//!
//!		@param	blendColor	color all other colors are multiplied with to set
//!
// *******************************************************************************************

void rTextField::SetBlendColor( tColor const & blendColor )
{
    blendColor_ = blendColor;
    if ( !sr_alphaBlend )
    {
        blendColor_.r_ *= blendColor_.a_;
        blendColor_.g_ *= blendColor_.a_;
        blendColor_.b_ *= blendColor_.a_;
        blendColor_.a_ = 1;
    }
}

tColor rTextField::defaultColor_;
tColor rTextField::blendColor_;
