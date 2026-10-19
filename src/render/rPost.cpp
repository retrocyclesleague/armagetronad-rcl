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
#include "rPost.h"

#ifndef DEDICATED

#include "rDisplayList.h"
#include "rFramebuffer.h"
#include "rGPUTimer.h"
#include "rScreen.h"
#include "rShader.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tSysTime.h"
#include "rRender.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

// ---- settings ----

// 0: today's fixed-function drawing straight onto the screen. 1: the new
// renderer, when everything it needs could be made; when not, 0 for the rest
// of the session, and the console says why.
static int sr_render = 1;
static tConfItem< int > sr_renderConf( "RCL_RENDER", sr_render );

// 1: act as if the new renderer could not be made, to see the way back work
static int sr_renderFail = 0;
static tSettingItem< int > sr_renderFailConf( "RCL_RENDER_FAIL", sr_renderFail );

// 1: the shader files are looked at about once a second and read again when
// one has changed, so they can be worked on while the game runs
static int sr_shaderReload = 0;
static tSettingItem< int > sr_shaderReloadConf( "RCL_SHADER_RELOAD", sr_shaderReload );

namespace
{
    struct gPost
    {
        bool failed;            // the new renderer is off for this session
        std::string why;
        bool shadersTried;      // for this context
        int screenDepthBits;    // of the screen's own framebuffer, -1 not asked yet
        double reloadCheckedAt;

        rFramebuffer scene;     // drawn into; multisampled with RCL_ANTIALIAS
        rFramebuffer resolved;  // the scene with one sample, when it has more
        int samplesWanted;      // what the targets were made for
        std::string notes;      // what had to be given up, for the overlay

        // the scene being drawn
        bool open, redirected;
        GLint viewport[4];
        unsigned int previous;  // the framebuffer that was bound before
        int path;

        std::string status;

        gPost()
        : failed( false ), shadersTried( false ), screenDepthBits( -1 ), reloadCheckedAt( 0 ), samplesWanted( 0 ),
          open( false ), redirected( false ), previous( 0 ), path( 0 )
        {
            viewport[0] = viewport[1] = viewport[2] = viewport[3] = 0;
        }
    };

    gPost sr_post;

    rShader sr_tonemapShader( "post.vert", "tonemap.frag" );

    rShader * const sr_shaders[] = { &sr_tonemapShader };
    int const sr_shaderCount = sizeof( sr_shaders ) / sizeof( sr_shaders[0] );

    void sr_ReleaseTargets()
    {
        sr_post.scene.Release();
        sr_post.resolved.Release();
    }

    void sr_ReleaseAll()
    {
        sr_ReleaseTargets();
        for ( int i = 0; i < sr_shaderCount; ++i )
            sr_shaders[i]->Release();
        sr_post.shadersTried = false;
    }

    // Everything here belongs to the context. Before it may go (a new screen
    // mode, or the window losing the output), all of it is deleted; it is
    // made again when the next scene needs it.
    void sr_PostRelease()
    {
        if ( sr_post.open && sr_post.redirected )
            rFramebuffer::Bind( sr_post.previous );
        sr_post.open = sr_post.redirected = false;
        if ( rGLExt::Loaded() )
            sr_ReleaseAll();
        else
        {
            sr_post.scene.Forget();
            sr_post.resolved.Forget();
            for ( int i = 0; i < sr_shaderCount; ++i )
                sr_shaders[i]->Forget();
            sr_post.shadersTried = false;
        }
    }
    rCallbackBeforeScreenModeChange sr_postRelease( &sr_PostRelease );

    void sr_PostNewContext()
    {
        sr_post.screenDepthBits = -1;
    }
    rCallbackAfterScreenModeChange sr_postNewContext( &sr_PostNewContext );

    void sr_Fail( std::string const & why )
    {
        if ( sr_post.failed )
            return;
        sr_post.failed = true;
        sr_post.why = why;
        con << "render: the new renderer is off for this session, the classic one draws instead: " << why << "\n";
        sr_ReleaseAll();
    }

    bool sr_LoadShaders()
    {
        for ( int i = 0; i < sr_shaderCount; ++i )
        {
            std::string error;
            if ( !sr_shaders[i]->Load( error ) )
            {
                sr_Fail( error );
                return false;
            }
        }
        return true;
    }

    // RCL_SHADER_RELOAD: a shader that was changed is read again; one that
    // does not compile now says so and leaves the last good one in use
    void sr_ReloadChangedShaders()
    {
        double const now = tRealSysTimeFloat();
        if ( now - sr_post.reloadCheckedAt < 1 )
            return;
        sr_post.reloadCheckedAt = now;
        for ( int i = 0; i < sr_shaderCount; ++i )
        {
            if ( !sr_shaders[i]->FilesChanged() )
                continue;
            std::string error;
            if ( sr_shaders[i]->Load( error ) )
                con << "render: read " << sr_shaders[i]->Name() << " again\n";
            else
                con << "render: " << error << "\n";
        }
    }

    int sr_Samples()
    {
        if ( sr_antialias < 2 || !rGLExt::Multisample() || !rGLExt::Blit() )
            return 0;
        int samples = sr_antialias > 16 ? 16 : sr_antialias;
        if ( samples > rGLExt::MaxSamples() )
            samples = rGLExt::MaxSamples();
        return samples > 1 ? samples : 0;
    }

    // The scene picture at the screen's size, and what goes with it. Made
    // when first needed and again when the size or the multisampling change.
    // Multisampling that cannot be had is given up; without any picture at
    // all the new renderer is off.
    bool sr_Targets()
    {
        int const width = sr_renderWidth, height = sr_renderHeight;
        int const samples = sr_Samples();

        if ( sr_post.scene.Ready() && sr_post.scene.Width() == width && sr_post.scene.Height() == height &&
             sr_post.samplesWanted == samples )
            return true;

        sr_ReleaseTargets();
        sr_post.samplesWanted = samples;
        sr_post.notes.clear();

        // as deep as the screen's own, so what is drawn meets in depth as it did there
        if ( sr_post.screenDepthBits < 0 )
        {
            GLint bits = 24;
            if ( rFramebuffer::Current() == 0 )
                glGetIntegerv( GL_DEPTH_BITS, &bits );
            sr_post.screenDepthBits = bits > 0 ? bits : 24;
        }
        int const depth = sr_post.screenDepthBits;

        std::string error, firstError;
        bool made = sr_post.scene.Create( width, height, false, samples, depth, error );
        if ( !made && samples > 0 )
        {
            firstError = error;
            made = sr_post.scene.Create( width, height, false, 0, depth, error );
            if ( made )
            {
                sr_post.notes = "gave up multisampling: " + firstError;
                con << "render: " << sr_post.notes << "\n";
            }
        }
        if ( !made )
        {
            sr_Fail( "no scene framebuffer: " + ( firstError.empty() ? error : firstError ) );
            return false;
        }

        if ( sr_post.scene.Samples() > 0 && !sr_post.resolved.Create( width, height, false, 0, 0, error ) )
        {
            sr_Fail( "no framebuffer to resolve the multisampled scene into: " + error );
            return false;
        }
        return true;
    }

    // whether the new renderer can draw the next scene; makes what it needs
    bool sr_Ready()
    {
        if ( sr_post.failed )
            return false;
        if ( sr_renderFail > 0 )
        {
            sr_Fail( "RCL_RENDER_FAIL is set" );
            return false;
        }
        if ( !rGLExt::Loaded() || !rGLExt::Shaders() || !rGLExt::Framebuffers() )
        {
            std::string why = "the driver lacks ";
            why += *rGLExt::Missing() ? rGLExt::Missing() : "what it needs";
            sr_Fail( why );
            return false;
        }
        if ( !sr_post.shadersTried )
        {
            sr_post.shadersTried = true;
            if ( !sr_LoadShaders() )
                return false;
        }
        else if ( sr_shaderReload > 0 )
            sr_ReloadChangedShaders();
        return sr_Targets();
    }

    // ---- drawing pictures ----

    struct gRect
    {
        int x, y, w, h;     // in pixels of the picture
        int width, height;  // the picture's size
    };

    // The part of a picture of the given size that covers the same share of
    // the screen as the scene's viewport. Every picture covers the whole
    // screen, so one place has the same texture coordinates in all of them.
    gRect sr_Part( int width, int height )
    {
        double const u0 = double( sr_post.viewport[0] ) / sr_post.scene.Width();
        double const v0 = double( sr_post.viewport[1] ) / sr_post.scene.Height();
        double const u1 = double( sr_post.viewport[0] + sr_post.viewport[2] ) / sr_post.scene.Width();
        double const v1 = double( sr_post.viewport[1] + sr_post.viewport[3] ) / sr_post.scene.Height();
        int x0 = static_cast< int >( std::floor( u0 * width ) ), y0 = static_cast< int >( std::floor( v0 * height ) );
        int x1 = static_cast< int >( std::ceil( u1 * width ) ), y1 = static_cast< int >( std::ceil( v1 * height ) );
        if ( x0 < 0 ) x0 = 0;
        if ( y0 < 0 ) y0 = 0;
        if ( x1 > width ) x1 = width;
        if ( y1 > height ) y1 = height;
        if ( x1 <= x0 ) x1 = x0 + 1;
        if ( y1 <= y0 ) y1 = y0 + 1;
        gRect const part = { x0, y0, x1 - x0, y1 - y0, width, height };
        return part;
    }

    // the viewport filled, reading the given part of the source pictures
    void sr_Quad( gRect const & part )
    {
        GLfloat const u0 = GLfloat( part.x ) / part.width, v0 = GLfloat( part.y ) / part.height;
        GLfloat const u1 = GLfloat( part.x + part.w ) / part.width, v1 = GLfloat( part.y + part.h ) / part.height;
        GLfloat const corners[8] = { -1, -1, 1, -1, 1, 1, -1, 1 };
        GLfloat const uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
        glVertexPointer( 2, GL_FLOAT, 0, corners );
        glTexCoordPointer( 2, GL_FLOAT, 0, uv );
        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
    }

    void sr_Texture( int unit, GLuint texture )
    {
#ifdef RGL_EXT_AVAILABLE
        glActiveTexture( RGL_TEXTURE0 + unit );
#endif
        glBindTexture( GL_TEXTURE_2D, texture );
    }

    // the scene, resolved, onto the framebuffer it would have been drawn
    // into, in its viewport, as it is
    void sr_Composite()
    {
        glPushAttrib( GL_ALL_ATTRIB_BITS );
        glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );

        glDisable( GL_DEPTH_TEST );
        glDepthMask( GL_FALSE );
        glDisable( GL_BLEND );
        glDisable( GL_ALPHA_TEST );
        glDisable( GL_CULL_FACE );
        glDisable( GL_LIGHTING );
        glDisable( GL_FOG );
        glDisable( GL_TEXTURE_2D );
        glDisable( GL_SCISSOR_TEST );
        glDisable( GL_STENCIL_TEST );
        glDisable( GL_POLYGON_OFFSET_FILL );
        glDisable( GL_COLOR_LOGIC_OP );
        glDisable( GL_DITHER );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
        glDisableClientState( GL_COLOR_ARRAY );
        glDisableClientState( GL_NORMAL_ARRAY );
        glDisableClientState( GL_INDEX_ARRAY );
        glDisableClientState( GL_EDGE_FLAG_ARRAY );
        glEnableClientState( GL_VERTEX_ARRAY );
        glEnableClientState( GL_TEXTURE_COORD_ARRAY );

        gRect const scenePart = sr_Part( sr_post.scene.Width(), sr_post.scene.Height() );

        GLuint sceneTexture = sr_post.scene.Texture();
#ifdef RGL_EXT_AVAILABLE
        if ( sr_post.scene.Samples() > 0 )
        {
            rGPUTimer::Begin( rGPUTimer::Pass_Resolve );
            sr_post.scene.BindRead();
            sr_post.resolved.BindDraw();
            rGLExt::BlitFramebuffer( scenePart.x, scenePart.y, scenePart.x + scenePart.w, scenePart.y + scenePart.h,
                                     scenePart.x, scenePart.y, scenePart.x + scenePart.w, scenePart.y + scenePart.h,
                                     GL_COLOR_BUFFER_BIT, GL_NEAREST );
            rGPUTimer::End( rGPUTimer::Pass_Resolve );
            sceneTexture = sr_post.resolved.Texture();
        }
#endif

        rGPUTimer::Begin( rGPUTimer::Pass_Tonemap );
        rFramebuffer::Bind( sr_post.previous );
        glViewport( sr_post.viewport[0], sr_post.viewport[1], sr_post.viewport[2], sr_post.viewport[3] );
        sr_tonemapShader.Use();
        sr_tonemapShader.SetSampler( "u_scene", 0 );
        sr_Texture( 0, sceneTexture );
        sr_Quad( scenePart );
        rGPUTimer::End( rGPUTimer::Pass_Tonemap );

        rShader::UseNone();
        sr_Texture( 0, 0 );
        glPopClientAttrib();
        glPopAttrib();
    }

    void sr_UpdateStatus()
    {
        std::ostringstream s;
        if ( sr_render <= 0 )
            s << "classic renderer (RCL_RENDER 0)";
        else if ( sr_post.failed )
            s << "classic renderer, new one off: " << sr_post.why;
        else if ( !sr_post.scene.Ready() )
            s << "new renderer, nothing drawn yet";
        else
        {
            s << "new renderer: " << sr_post.scene.Width() << 'x' << sr_post.scene.Height() << " RGBA8";
            if ( sr_post.scene.Samples() > 0 )
                s << " msaa " << sr_post.scene.Samples();
            s << ", copied as it is";
            if ( !sr_post.notes.empty() )
                s << " (" << sr_post.notes << ")";
        }
        sr_post.status = s.str();
    }

    // the line the stats overlay (rGPUTimer) starts with
    char const * sr_PostStatus()
    {
        return rPost::Status();
    }
    struct gPostStatusSet
    {
        gPostStatusSet(){ rGPUTimer::SetStatus( &sr_PostStatus ); }
    };
    gPostStatusSet sr_postStatusSet;
}

bool rPost::BeginScene()
{
    if ( !sr_glOut || sr_post.open )
        return false;
    sr_post.open = true;
    sr_post.redirected = false;
    sr_post.path = 0;
    rGPUTimer::Begin( rGPUTimer::Pass_World );

    if ( sr_render <= 0 || rDisplayList::IsRecording() )
        return false;
    RenderEnd();
    if ( !sr_Ready() )
        return false;

    glGetIntegerv( GL_VIEWPORT, sr_post.viewport );
    if ( sr_post.viewport[2] <= 0 || sr_post.viewport[3] <= 0 )
        return false;
    sr_post.previous = rFramebuffer::Current();
    sr_post.scene.Bind();

    // What the screen is cleared to between frames: the floor where the
    // clean arena was drawn lately (rSysDep::ClearGL), black elsewhere.
    glPushAttrib( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT );
    glEnable( GL_SCISSOR_TEST );
    glScissor( sr_post.viewport[0], sr_post.viewport[1], sr_post.viewport[2], sr_post.viewport[3] );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glDepthMask( GL_TRUE );
    if ( sr_CleanArenaRecent() )
    {
        REAL const * const floor = sr_CleanFloorColor();
        glClearColor( floor[0], floor[1], floor[2], 1 );
    }
    else
        glClearColor( 0, 0, 0, 1 );
    glClearDepth( 1 );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glPopAttrib();

    sr_post.redirected = true;
    sr_post.path = 1;
    return true;
}

void rPost::EndScene()
{
    if ( !sr_post.open )
        return;
    sr_post.open = false;
    rGPUTimer::End( rGPUTimer::Pass_World );
    if ( !sr_post.redirected )
        return;
    sr_post.redirected = false;
    RenderEnd();
    sr_Composite();
}

int rPost::Path()
{
    return sr_post.path;
}

char const * rPost::Status()
{
    sr_UpdateStatus();
    return sr_post.status.c_str();
}

#endif // DEDICATED
