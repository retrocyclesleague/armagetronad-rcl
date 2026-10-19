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

#ifndef DEDICATED

#include "rScreen.h"
#include "tString.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    struct gFeatures
    {
        bool loaded, shaders, framebuffers, core, blit, multisample, floatColour, timer, timerCore;
        int maxSamples;
        std::string missing, describe;

        gFeatures()
        : loaded( false ), shaders( false ), framebuffers( false ), core( false ), blit( false ),
          multisample( false ), floatColour( false ), timer( false ), timerCore( false ), maxSamples( 0 )
        {}
    };

    gFeatures sr_ext;

    char const * String( GLenum name )
    {
        char const * const s = reinterpret_cast< char const * >( glGetString( name ) );
        return s ? s : "";
    }

    // the first two numbers of a version string ("4.6.0 NVIDIA ...", "1.20")
    void Version( char const * s, int & major, int & minor )
    {
        major = minor = 0;
        if ( s )
            std::sscanf( s, "%d.%d", &major, &minor );
    }

    bool AtLeast( int major, int minor, int wantMajor, int wantMinor )
    {
        return major > wantMajor || ( major == wantMajor && minor >= wantMinor );
    }

#if defined( RGL_EXT_AVAILABLE ) && !defined( HAVE_GLEW )
    // a whole word of the extension string
    bool HasExtension( char const * name )
    {
        char const * const all = String( GL_EXTENSIONS );
        size_t const length = std::strlen( name );
        for ( char const * at = std::strstr( all, name ); at; at = std::strstr( at + 1, name ) )
        {
            bool const starts = at == all || at[-1] == ' ';
            bool const ends = at[length] == ' ' || at[length] == '\0';
            if ( starts && ends )
                return true;
        }
        return false;
    }
#endif

    void Note( char const * what )
    {
        if ( !sr_ext.missing.empty() )
            sr_ext.missing += ", ";
        sr_ext.missing += what;
    }

    // a new screen mode can mean a new context, with entry points of its own
    void sr_GLExtInit()
    {
        rGLExt::Init();
    }
    rCallbackAfterScreenModeChange sr_glExtInit( &sr_GLExtInit );
}

void rGLExt::Init()
{
    sr_ext = gFeatures();
    if ( !sr_glOut )
    {
        Note( "no GL output" );
        return;
    }

    int major, minor, glslMajor = 0, glslMinor = 0;
    Version( String( GL_VERSION ), major, minor );

#ifndef RGL_EXT_AVAILABLE
    Note( "built without GLEW" );
    (void)major;
    (void)minor;
#else
#ifdef HAVE_GLEW
    GLenum const result = glewInit();
    bool loaded = result == GLEW_OK;
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
    // GLEW built for GLX, on a context that is not one (Wayland, EGL): it
    // says so only after the GL entry points, all that is used here, are in
    if ( result == GLEW_ERROR_NO_GLX_DISPLAY )
        loaded = true;
#endif
    if ( !loaded )
    {
        std::string why = "GLEW: ";
        why += reinterpret_cast< char const * >( glewGetErrorString( result ) );
        Note( why.c_str() );
        return;
    }
    sr_ext.loaded = true;

    if ( AtLeast( major, minor, 2, 0 ) )
        Version( String( RGL_SHADING_LANGUAGE_VERSION ), glslMajor, glslMinor );
    sr_ext.shaders = AtLeast( major, minor, 2, 0 ) && AtLeast( glslMajor, glslMinor, 1, 20 ) &&
                     glCreateShader && glShaderSource && glCompileShader && glCreateProgram && glLinkProgram &&
                     glUseProgram && glGetUniformLocation && glUniform1f && glUniform1i && glActiveTexture;

    sr_ext.core = ( GLEW_VERSION_3_0 || GLEW_ARB_framebuffer_object ) &&
                  glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D && glCheckFramebufferStatus &&
                  glGenRenderbuffers && glRenderbufferStorage && glFramebufferRenderbuffer;
    if ( sr_ext.core )
    {
        sr_ext.framebuffers = true;
        sr_ext.blit = glBlitFramebuffer != 0;
        sr_ext.multisample = glRenderbufferStorageMultisample != 0;
    }
    else
    {
        sr_ext.framebuffers = GLEW_EXT_framebuffer_object && glGenFramebuffersEXT && glBindFramebufferEXT &&
                              glFramebufferTexture2DEXT && glCheckFramebufferStatusEXT && glGenRenderbuffersEXT;
        sr_ext.blit = GLEW_EXT_framebuffer_blit && glBlitFramebufferEXT;
        sr_ext.multisample = GLEW_EXT_framebuffer_multisample && glRenderbufferStorageMultisampleEXT;
    }

    // Values over 1 survive only where colours are not clamped on the way
    // into a float target: core in 3.0, an extension before that.
    sr_ext.floatColour = GLEW_VERSION_3_0 || ( GLEW_ARB_texture_float && GLEW_ARB_color_buffer_float );

    sr_ext.timerCore = ( GLEW_VERSION_3_3 || GLEW_ARB_timer_query ) && glGetQueryObjectui64v;
    sr_ext.timer = glGenQueries && glBeginQuery && glEndQuery && glGetQueryObjectiv &&
                   ( sr_ext.timerCore || ( GLEW_EXT_timer_query && glGetQueryObjectui64vEXT ) );
#else
    // macOS: the legacy 2.1 context, everything linked from the framework
    sr_ext.loaded = true;
    if ( AtLeast( major, minor, 2, 0 ) )
        Version( String( RGL_SHADING_LANGUAGE_VERSION ), glslMajor, glslMinor );
    sr_ext.shaders = AtLeast( major, minor, 2, 0 ) && AtLeast( glslMajor, glslMinor, 1, 20 );
    sr_ext.core = false;
    sr_ext.framebuffers = HasExtension( "GL_EXT_framebuffer_object" );
    sr_ext.blit = HasExtension( "GL_EXT_framebuffer_blit" );
    sr_ext.multisample = HasExtension( "GL_EXT_framebuffer_multisample" );
    sr_ext.floatColour = AtLeast( major, minor, 3, 0 ) ||
                         ( HasExtension( "GL_ARB_texture_float" ) && HasExtension( "GL_ARB_color_buffer_float" ) );
    sr_ext.timerCore = false;
    sr_ext.timer = HasExtension( "GL_EXT_timer_query" ) || HasExtension( "GL_ARB_timer_query" );
#endif

    if ( sr_ext.multisample )
    {
        GLint samples = 0;
        glGetIntegerv( RGL_MAX_SAMPLES, &samples );
        sr_ext.maxSamples = samples;
    }
    while ( glGetError() != GL_NO_ERROR )
        ;

    if ( !sr_ext.shaders )
        Note( "GLSL 1.20 shaders" );
    if ( !sr_ext.framebuffers )
        Note( "framebuffer objects" );
    if ( !sr_ext.blit )
        Note( "framebuffer blit" );
#endif

    char line[160];
    std::snprintf( line, sizeof( line ), "GL %d.%d, GLSL %d.%02d", major, minor, glslMajor, glslMinor );
    sr_ext.describe = line;
}

bool rGLExt::Loaded(){ return sr_ext.loaded; }
bool rGLExt::Shaders(){ return sr_ext.shaders; }
bool rGLExt::Framebuffers(){ return sr_ext.framebuffers; }
bool rGLExt::Blit(){ return sr_ext.blit; }
bool rGLExt::Multisample(){ return sr_ext.multisample; }
int rGLExt::MaxSamples(){ return sr_ext.maxSamples; }
bool rGLExt::FloatColour(){ return sr_ext.floatColour; }
bool rGLExt::TimerQueries(){ return sr_ext.timer; }
char const * rGLExt::Missing(){ return sr_ext.missing.c_str(); }
char const * rGLExt::Describe(){ return sr_ext.describe.c_str(); }

#ifdef RGL_EXT_AVAILABLE

// Core and EXT framebuffer calls take the same arguments and the same
// enumerants; only the names differ. macOS has only the EXT ones on its 2.1
// context.
#ifdef HAVE_GLEW
#define RGL_FBO_CALL( coreCall, extCall, args ) ( sr_ext.core ? coreCall args : extCall args )
#else
#define RGL_FBO_CALL( coreCall, extCall, args ) ( extCall args )
#endif

void rGLExt::GenFramebuffers( GLsizei n, GLuint * ids )
{
    RGL_FBO_CALL( glGenFramebuffers, glGenFramebuffersEXT, ( n, ids ) );
}

void rGLExt::DeleteFramebuffers( GLsizei n, GLuint const * ids )
{
    RGL_FBO_CALL( glDeleteFramebuffers, glDeleteFramebuffersEXT, ( n, ids ) );
}

void rGLExt::BindFramebuffer( GLenum target, GLuint id )
{
    RGL_FBO_CALL( glBindFramebuffer, glBindFramebufferEXT, ( target, id ) );
}

void rGLExt::FramebufferTexture2D( GLenum target, GLenum attachment, GLenum textureTarget, GLuint texture, GLint level )
{
    RGL_FBO_CALL( glFramebufferTexture2D, glFramebufferTexture2DEXT, ( target, attachment, textureTarget, texture, level ) );
}

void rGLExt::GenRenderbuffers( GLsizei n, GLuint * ids )
{
    RGL_FBO_CALL( glGenRenderbuffers, glGenRenderbuffersEXT, ( n, ids ) );
}

void rGLExt::DeleteRenderbuffers( GLsizei n, GLuint const * ids )
{
    RGL_FBO_CALL( glDeleteRenderbuffers, glDeleteRenderbuffersEXT, ( n, ids ) );
}

void rGLExt::BindRenderbuffer( GLenum target, GLuint id )
{
    RGL_FBO_CALL( glBindRenderbuffer, glBindRenderbufferEXT, ( target, id ) );
}

void rGLExt::RenderbufferStorage( GLenum target, GLenum format, GLsizei width, GLsizei height )
{
    RGL_FBO_CALL( glRenderbufferStorage, glRenderbufferStorageEXT, ( target, format, width, height ) );
}

void rGLExt::RenderbufferStorageMultisample( GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height )
{
    RGL_FBO_CALL( glRenderbufferStorageMultisample, glRenderbufferStorageMultisampleEXT,
                  ( target, samples, format, width, height ) );
}

void rGLExt::FramebufferRenderbuffer( GLenum target, GLenum attachment, GLenum renderbufferTarget, GLuint renderbuffer )
{
    RGL_FBO_CALL( glFramebufferRenderbuffer, glFramebufferRenderbufferEXT,
                  ( target, attachment, renderbufferTarget, renderbuffer ) );
}

GLenum rGLExt::CheckFramebufferStatus( GLenum target )
{
    return RGL_FBO_CALL( glCheckFramebufferStatus, glCheckFramebufferStatusEXT, ( target ) );
}

void rGLExt::BlitFramebuffer( GLint x0, GLint y0, GLint x1, GLint y1, GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                              GLbitfield mask, GLenum filter )
{
    RGL_FBO_CALL( glBlitFramebuffer, glBlitFramebufferEXT, ( x0, y0, x1, y1, dx0, dy0, dx1, dy1, mask, filter ) );
}

rGLtime rGLExt::QueryTime( GLuint query )
{
    rGLtime time = 0;
#ifdef HAVE_GLEW
    if ( sr_ext.timerCore )
        glGetQueryObjectui64v( query, RGL_QUERY_RESULT, &time );
    else
        glGetQueryObjectui64vEXT( query, RGL_QUERY_RESULT, &time );
#else
    glGetQueryObjectui64vEXT( query, RGL_QUERY_RESULT, &time );
#endif
    return time;
}

#endif // RGL_EXT_AVAILABLE

#endif // DEDICATED
