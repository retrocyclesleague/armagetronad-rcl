#include "config.h"

#ifndef DEDICATED

#include "rGraphicsBackend.h"
#include "rMetalBackend.h"
#include "rMatrixState.h"
#include "rMetalGLCompat.h"
#include "rRender.h"
#include "rScreen.h"
#include "rSDL.h"
#include "tConfiguration.h"

#include <iostream>

static rGraphicsBackend sr_graphicsBackend = rGraphicsBackend_OpenGL;

static int sr_graphicsBackendSetting = 0;
static tConfItem<int> sr_graphicsBackend_ci(
    "ARMAGETRON_GRAPHICS_BACKEND", sr_graphicsBackendSetting);

rGraphicsBackend sr_GetGraphicsBackend()
{
    return sr_graphicsBackend;
}

bool sr_UsingMetalBackend()
{
    return sr_graphicsBackend == rGraphicsBackend_Metal;
}

void sr_ApplyGraphicsBackendSetting()
{
#if defined(__APPLE__) && defined(RCL_XCODE_METAL)
    if (sr_graphicsBackendSetting == 1)
        sr_graphicsBackend = rGraphicsBackend_Metal;
    else
        sr_graphicsBackend = rGraphicsBackend_OpenGL;
#elif defined(__APPLE__)
    sr_graphicsBackend = rGraphicsBackend_OpenGL;
    if (sr_graphicsBackendSetting != 0)
        std::cerr << "ARMAGETRON_GRAPHICS_BACKEND=1 needs an Xcode build (./build-macos.sh); using OpenGL.\n";
#else
    sr_graphicsBackend = rGraphicsBackend_OpenGL;
    if (sr_graphicsBackendSetting != 0)
        std::cerr << "ARMAGETRON_GRAPHICS_BACKEND: Metal is macOS-only; using OpenGL.\n";
#endif
}

bool sr_CreateGraphicsContext()
{
    if (!sr_window)
        return false;

    if (sr_UsingMetalBackend())
    {
#ifdef __APPLE__
        if (!sr_CreateMetalContext())
            return false;
        return true;
#else
        return false;
#endif
    }

    sr_glcontext = SDL_GL_CreateContext(sr_window);
    if (!sr_glcontext)
        return false;

    SDL_GL_MakeCurrent(sr_window, sr_glcontext);
    return true;
}

void sr_DestroyGraphicsContext()
{
    if (sr_UsingMetalBackend())
    {
#ifdef __APPLE__
        sr_DestroyMetalContext();
#endif
    }
    else if (sr_glcontext)
    {
        SDL_GL_DestroyContext(sr_glcontext);
        sr_glcontext = NULL;
    }
}

void sr_MakeGraphicsCurrent()
{
    if (!sr_UsingMetalBackend() && sr_window && sr_glcontext)
        SDL_GL_MakeCurrent(sr_window, sr_glcontext);
}

void sr_PresentFrame()
{
    if (!sr_window)
        return;

    if (sr_UsingMetalBackend())
    {
#ifdef __APPLE__
        sr_rendererEndFrame();
        sr_MetalPresent();
#endif
    }
    else if (lastSuccess.useSDL)
        SDL_GL_SwapWindow(sr_window);
}

#endif
