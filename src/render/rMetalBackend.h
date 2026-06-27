#ifndef AT_METAL_BACKEND_H
#define AT_METAL_BACKEND_H

#ifndef DEDICATED

#define NO_SDL_GLEXT
#include <SDL3/SDL_opengl.h>

struct rMetalGLState;

bool sr_CreateMetalContext();
void sr_DestroyMetalContext();

void sr_MetalBeginFrame();
void sr_MetalPresent();

bool sr_MetalUploadTexture( GLuint id, const void *pixels, int width, int height, bool rgba );
void sr_MetalDeleteTexture( GLuint id );

void sr_MetalDrawTriangles(
    const float *vertices, int vertexCount,
    const float *mvp, const float *texMatrix,
    bool useTexture, GLuint textureId );

void sr_MetalDrawLines(
    const float *vertices, int vertexCount,
    const float *mvp );

#endif

#endif
