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

#include "gLogo.h"

#include "gStuff.h"
#include "rTexture.h"
#include "rRender.h"
#include "rScreen.h"
#include "eCoord.h"
#include "uMenu.h"
#include "tSysTime.h"

#include <algorithm>

// static rFileTexture sg_LogoTexture(rTextureGroups::TEX_FONT, "textures/KGN_logo.png",0,0,1);
static rISurfaceTexture* sg_LogoMPTitle = NULL;

static gLogo logo;

static bool sg_Displayed = true;
static bool sg_Spinning  = false;
static bool sg_Big       = true;

static eCoord sg_SpinStatus(1,0);    // current spinning position
static REAL   sg_SizeStatus(1);    // 1 -> big      , 0 -> small
static REAL   sg_DisplayStatus(-1); // 1 -> displayed, 0->invisible

void gLogo::SetDisplayed(bool d, bool immediately)
{
    if (sg_Displayed == false && d == true && sg_DisplayStatus < .01)
        sg_SpinStatus = eCoord(0, 1);

    sg_Displayed = d;
    if (immediately)
        sg_DisplayStatus = d ? 1 : 0;
}

void gLogo::SetSpinning(bool s)
{
    sg_Spinning = s;
    if (!s)
        sg_SpinStatus = eCoord(1, 0);
}
void gLogo::SetBig(bool b, bool immediately)
{
    sg_Big = b;
    if (immediately)
        sg_SizeStatus = b ? 1 : 0;

}

/*
static tString sg_title("Anonymous/original/textures/title.jpg");
static nSettingItem<tString> gg_title("TEXTURE_TITLE", sg_title);

static tString sg_mp_title("Anonymous/original/moviepack/title.jpg");
static nSettingItem<tString> gg_mp_title("TEXTURE_MP_TITLE", sg_mp_title);
*/

void gLogo::Display()
{
#ifndef DEDICATED
    if (!sr_glOut) return;
    static REAL lastTime = 0;
    REAL const now = tSysTimeFloat();
    REAL const delta = std::min(REAL(.1), std::max(REAL(0), now-lastTime));
    lastTime = now;
    bool const visible = sg_Displayed && sg_Big;
    sg_DisplayStatus = std::max(REAL(0), std::min(REAL(1),
        sg_DisplayStatus + (visible ? delta*3 : -delta*3)));
    if (sg_DisplayStatus <= .001) return;

    renderer->SetFlag(rRenderer::DEPTH_TEST, false);
    if (!sg_LogoMPTitle)
        sg_LogoMPTitle = tNEW(rFileTexture)(rTextureGroups::TEX_FONT,
            sg_MoviePack() ? "moviepack/title.jpg" : "textures/title.png", 0, 0, 1);

    // Fill the viewport in the kit's graphite, then contain the artwork.
    // The old splash stretched a 4:3 image across every display shape.
    RenderEnd(true);
    glDisable(GL_TEXTURE_2D);
    BeginQuads();
    Color(32/255.f, 33/255.f, 34/255.f, sg_DisplayStatus);
    Vertex(-1,-1); Vertex(1,-1); Vertex(1,1); Vertex(-1,1);
    RenderEnd();
    sg_LogoMPTitle->Select();
    if (!sg_LogoMPTitle->Loaded()) return;
    REAL const aspect = REAL(sr_screenWidth)/std::max(1, sr_screenHeight);
    REAL const artworkAspect = sg_MoviePack() ? REAL(4)/3 : REAL(2);
    REAL const halfWidth = std::min(REAL(1), artworkAspect/aspect);
    REAL const halfHeight = std::min(REAL(1), aspect/artworkAspect);
    Color(1,1,1,sg_DisplayStatus);
    BeginQuads();
    TexCoord(0,0); Vertex(-halfWidth, halfHeight);
    TexCoord(0,1); Vertex(-halfWidth,-halfHeight);
    TexCoord(1,1); Vertex( halfWidth,-halfHeight);
    TexCoord(1,0); Vertex( halfWidth, halfHeight);
    RenderEnd();
#endif
}

gLogo::~gLogo()
{
    if (sg_LogoMPTitle)
    {
        tDESTROY(sg_LogoMPTitle);
    }
}
