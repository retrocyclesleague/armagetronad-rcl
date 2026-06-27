#ifndef AT_GRAPHICS_BACKEND_H
#define AT_GRAPHICS_BACKEND_H

#ifndef DEDICATED

enum rGraphicsBackend
{
    rGraphicsBackend_OpenGL = 0,
    rGraphicsBackend_Metal  = 1
};

rGraphicsBackend sr_GetGraphicsBackend();
bool sr_UsingMetalBackend();

//! Read config and clamp to supported backends on this platform.
void sr_ApplyGraphicsBackendSetting();

//! Create GL or Metal context after SDL window exists. Returns false on failure.
bool sr_CreateGraphicsContext();

void sr_DestroyGraphicsContext();
void sr_MakeGraphicsCurrent();

//! End of frame — GL swap or Metal present.
void sr_PresentFrame();

#ifdef __APPLE__
bool sr_CreateMetalContext();
void sr_MetalPresent();
void sr_DestroyMetalContext();
#endif

#endif

#endif
