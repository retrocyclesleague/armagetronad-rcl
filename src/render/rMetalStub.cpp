#include "config.h"

#ifndef DEDICATED

// ponytail: autotools/Linux builds link this stub; Xcode links rMetalBackend.mm + rMetalRender.mm instead.

#include "rMetalBackend.h"
#include "rRender.h"

bool sr_CreateMetalContext()
{
    return false;
}

void sr_DestroyMetalContext()
{
}

void sr_MetalBeginFrame()
{
}

void sr_MetalPresent()
{
}

bool sr_MetalUploadTexture( GLuint, const void *, int, int, bool )
{
    return false;
}

void sr_MetalDeleteTexture( GLuint )
{
}

void sr_MetalDrawTriangles(
    const float *, int, const float *, const float *, bool, GLuint )
{
}

void sr_MetalDrawLines( const float *, int, const float * )
{
}

void sr_metalRendererInit()
{
    sr_glRendererInit();
}

#endif
