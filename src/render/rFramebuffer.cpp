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

#include "rGLExt.h"
#include "rFramebuffer.h"

#ifndef DEDICATED

#include <cstdio>

rFramebuffer::rFramebuffer()
: framebuffer_( 0 ), texture_( 0 ), colour_( 0 ), depth_( 0 ), width_( 0 ), height_( 0 ), samples_( 0 ),
  depthBits_( 0 ), float_( false )
{}

rFramebuffer::~rFramebuffer()
{
    // the context may be gone by now; the driver frees what is left with it
}

bool rFramebuffer::Create( int width, int height, bool floatColour, int samples, int depthBits, std::string & error )
{
    Release();
#ifndef RGL_EXT_AVAILABLE
    (void)width; (void)height; (void)floatColour; (void)samples; (void)depthBits;
    error = "built without GLEW";
    return false;
#else
    if ( !rGLExt::Framebuffers() )
    {
        error = "no framebuffer objects";
        return false;
    }
    if ( width < 1 || height < 1 )
    {
        error = "no size";
        return false;
    }
    if ( samples > 1 && !( rGLExt::Multisample() && rGLExt::Blit() ) )
    {
        error = "no multisampled framebuffers";
        return false;
    }
    if ( samples > rGLExt::MaxSamples() && samples > 1 )
        samples = rGLExt::MaxSamples();
    if ( samples < 2 )
        samples = 0;

    while ( glGetError() != GL_NO_ERROR )
        ;

    GLint const previous = static_cast< GLint >( Current() );
    GLint texture = 0;
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &texture );

    GLenum const format = floatColour ? RGL_RGBA16F : RGL_RGBA8;
    GLuint id = 0;
    rGLExt::GenFramebuffers( 1, &id );
    framebuffer_ = id;
    rGLExt::BindFramebuffer( RGL_FRAMEBUFFER, framebuffer_ );

    if ( samples )
    {
        rGLExt::GenRenderbuffers( 1, &id );
        colour_ = id;
        rGLExt::BindRenderbuffer( RGL_RENDERBUFFER, colour_ );
        rGLExt::RenderbufferStorageMultisample( RGL_RENDERBUFFER, samples, format, width, height );
        rGLExt::FramebufferRenderbuffer( RGL_FRAMEBUFFER, RGL_COLOR_ATTACHMENT0, RGL_RENDERBUFFER, colour_ );
    }
    else
    {
        glGenTextures( 1, &id );
        texture_ = id;
        glBindTexture( GL_TEXTURE_2D, texture_ );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA,
                      floatColour ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL );
        rGLExt::FramebufferTexture2D( RGL_FRAMEBUFFER, RGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0 );
    }

    if ( depthBits > 0 )
    {
        GLenum const depthFormat = depthBits <= 16 ? RGL_DEPTH_COMPONENT16 :
                                   depthBits <= 24 ? RGL_DEPTH_COMPONENT24 : RGL_DEPTH_COMPONENT32;
        rGLExt::GenRenderbuffers( 1, &id );
        depth_ = id;
        rGLExt::BindRenderbuffer( RGL_RENDERBUFFER, depth_ );
        if ( samples )
            rGLExt::RenderbufferStorageMultisample( RGL_RENDERBUFFER, samples, depthFormat, width, height );
        else
            rGLExt::RenderbufferStorage( RGL_RENDERBUFFER, depthFormat, width, height );
        rGLExt::FramebufferRenderbuffer( RGL_FRAMEBUFFER, RGL_DEPTH_ATTACHMENT, RGL_RENDERBUFFER, depth_ );
    }
    rGLExt::BindRenderbuffer( RGL_RENDERBUFFER, 0 );

    GLenum const status = rGLExt::CheckFramebufferStatus( RGL_FRAMEBUFFER );
    GLenum const glError = glGetError();
    while ( glGetError() != GL_NO_ERROR )
        ;

    rGLExt::BindFramebuffer( RGL_FRAMEBUFFER, static_cast< GLuint >( previous ) );
    glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( texture ) );

    if ( status != RGL_FRAMEBUFFER_COMPLETE || glError != GL_NO_ERROR )
    {
        char why[120];
        std::snprintf( why, sizeof( why ), "%s %dx%d%s%s not complete (status 0x%x, error 0x%x)",
                       floatColour ? "RGBA16F" : "RGBA8", width, height, samples ? ", multisampled" : "",
                       depthBits > 0 ? ", with depth" : "", static_cast< unsigned >( status ),
                       static_cast< unsigned >( glError ) );
        error = why;
        Release();
        return false;
    }

    width_ = width;
    height_ = height;
    samples_ = samples;
    depthBits_ = depthBits;
    float_ = floatColour;
    return true;
#endif
}

void rFramebuffer::Release()
{
#ifdef RGL_EXT_AVAILABLE
    if ( framebuffer_ )
        rGLExt::DeleteFramebuffers( 1, &framebuffer_ );
    if ( colour_ )
        rGLExt::DeleteRenderbuffers( 1, &colour_ );
    if ( depth_ )
        rGLExt::DeleteRenderbuffers( 1, &depth_ );
    if ( texture_ )
        glDeleteTextures( 1, &texture_ );
#endif
    Forget();
}

void rFramebuffer::Forget()
{
    framebuffer_ = texture_ = colour_ = depth_ = 0;
    width_ = height_ = samples_ = depthBits_ = 0;
    float_ = false;
}

void rFramebuffer::Bind() const
{
#ifdef RGL_EXT_AVAILABLE
    rGLExt::BindFramebuffer( RGL_FRAMEBUFFER, framebuffer_ );
#endif
}

void rFramebuffer::BindRead() const
{
#ifdef RGL_EXT_AVAILABLE
    rGLExt::BindFramebuffer( RGL_READ_FRAMEBUFFER, framebuffer_ );
#endif
}

void rFramebuffer::BindDraw() const
{
#ifdef RGL_EXT_AVAILABLE
    rGLExt::BindFramebuffer( RGL_DRAW_FRAMEBUFFER, framebuffer_ );
#endif
}

void rFramebuffer::BindScreen()
{
    Bind( 0 );
}

unsigned int rFramebuffer::Current()
{
    GLint current = 0;
#ifdef RGL_EXT_AVAILABLE
    if ( rGLExt::Framebuffers() )
        glGetIntegerv( RGL_FRAMEBUFFER_BINDING, &current );
#endif
    return static_cast< unsigned int >( current );
}

void rFramebuffer::Bind( unsigned int framebuffer )
{
#ifdef RGL_EXT_AVAILABLE
    if ( rGLExt::Framebuffers() )
        rGLExt::BindFramebuffer( RGL_FRAMEBUFFER, framebuffer );
#else
    (void)framebuffer;
#endif
}

#endif // DEDICATED
