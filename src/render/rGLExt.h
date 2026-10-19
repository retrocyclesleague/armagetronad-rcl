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

#ifndef ArmageTron_GLEXT_H
#define ArmageTron_GLEXT_H

// What the new renderer needs past OpenGL 1.1: shaders, framebuffers and
// timer queries, and which of them the driver has. Windows and Linux reach
// them through GLEW. macOS has them in its OpenGL framework, for the legacy
// 2.1 context sdl12-compat gives there: GLSL 1.20 and the EXT framebuffer
// calls, so that is what everything here is written against.
//
// Include this before anything else that includes GL headers: GLEW must come
// before gl.h.

#include "config.h"

#ifndef DEDICATED

#if defined( HAVE_GLEW )
#define RGL_EXT_AVAILABLE 1
#include <GL/glew.h>
#include "rGL.h"
typedef GLuint64 rGLtime;
#elif defined( MACOSX ) || defined( __APPLE__ )
#define RGL_EXT_AVAILABLE 1
#include "rGL.h"
#include <OpenGL/glext.h>
typedef GLuint64EXT rGLtime;
#else
#include "rGL.h"
#endif

// The enumerants used, by value: they are the same for the core, ARB and EXT
// forms, and older headers (Windows' gl.h is 1.1) lack most of them.
#define RGL_FRAMEBUFFER                0x8D40
#define RGL_READ_FRAMEBUFFER           0x8CA8
#define RGL_DRAW_FRAMEBUFFER           0x8CA9
#define RGL_FRAMEBUFFER_BINDING        0x8CA6
#define RGL_READ_FRAMEBUFFER_BINDING   0x8CAA
#define RGL_RENDERBUFFER               0x8D41
#define RGL_COLOR_ATTACHMENT0          0x8CE0
#define RGL_DEPTH_ATTACHMENT           0x8D00
#define RGL_FRAMEBUFFER_COMPLETE       0x8CD5
#define RGL_MAX_SAMPLES                0x8D57
#define RGL_RGBA16F                    0x881A
#define RGL_RGBA8                      0x8058
#define RGL_DEPTH_COMPONENT16          0x81A5
#define RGL_DEPTH_COMPONENT24          0x81A6
#define RGL_DEPTH_COMPONENT32          0x81A7
#define RGL_CLAMP_TO_EDGE              0x812F
#define RGL_TEXTURE0                   0x84C0
#define RGL_TEXTURE1                   0x84C1
#define RGL_VERTEX_SHADER              0x8B31
#define RGL_FRAGMENT_SHADER            0x8B30
#define RGL_COMPILE_STATUS             0x8B81
#define RGL_LINK_STATUS                0x8B82
#define RGL_INFO_LOG_LENGTH            0x8B84
#define RGL_SHADING_LANGUAGE_VERSION   0x8B8C
#define RGL_TIME_ELAPSED               0x88BF
#define RGL_QUERY_RESULT               0x8866
#define RGL_QUERY_RESULT_AVAILABLE     0x8867
#define RGL_RGB16F                     0x881B
#define RGL_TEXTURE_CUBE_MAP           0x8513
#define RGL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define RGL_TEXTURE_CUBE_MAP_SEAMLESS  0x884F
#define RGL_TEXTURE_BASE_LEVEL         0x813C
#define RGL_TEXTURE_MAX_LEVEL          0x813D
#define RGL_GENERATE_MIPMAP            0x8191
#define RGL_ARRAY_BUFFER               0x8892
#define RGL_ELEMENT_ARRAY_BUFFER       0x8893
#define RGL_STATIC_DRAW                0x88E4
#define RGL_TEXTURE2                   0x84C2

namespace rGLExt
{
    //! Loads the entry points for the context that is current and finds out
    //! what it has. Done after every new context (a new screen mode can
    //! bring one); nothing else here works before.
    void Init();

    bool Loaded();                  //!< the entry points are there for this context
    bool Shaders();                 //!< GLSL 1.20 programs
    bool Framebuffers();            //!< framebuffer objects, core or EXT
    bool Blit();                    //!< copies between framebuffers (resolving multisampling)
    bool Multisample();             //!< multisampled renderbuffers
    int MaxSamples();               //!< the most samples a renderbuffer can have
    bool FloatColour();             //!< half float colour targets that keep values over 1
    bool FloatTextures();           //!< half float textures (light over 1 in a cubemap)
    bool SeamlessCube();            //!< cubemaps filtered across their faces' edges
    bool TimerQueries();            //!< GPU time spent between two points
    char const * Missing();         //!< what is missing of the above, for the console; empty if nothing
    char const * Describe();        //!< the GL and GLSL versions, for the stats overlay

#ifdef RGL_EXT_AVAILABLE
    // framebuffers, by whichever form the driver has
    void GenFramebuffers( GLsizei n, GLuint * ids );
    void DeleteFramebuffers( GLsizei n, GLuint const * ids );
    void BindFramebuffer( GLenum target, GLuint id );
    void FramebufferTexture2D( GLenum target, GLenum attachment, GLenum textureTarget, GLuint texture, GLint level );
    void GenRenderbuffers( GLsizei n, GLuint * ids );
    void DeleteRenderbuffers( GLsizei n, GLuint const * ids );
    void BindRenderbuffer( GLenum target, GLuint id );
    void RenderbufferStorage( GLenum target, GLenum format, GLsizei width, GLsizei height );
    void RenderbufferStorageMultisample( GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height );
    void FramebufferRenderbuffer( GLenum target, GLenum attachment, GLenum renderbufferTarget, GLuint renderbuffer );
    GLenum CheckFramebufferStatus( GLenum target );
    void BlitFramebuffer( GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                          GLbitfield mask, GLenum filter );

    //! the GPU time a finished query measured, in nanoseconds
    rGLtime QueryTime( GLuint query );
#endif
}

#endif // DEDICATED

#endif
