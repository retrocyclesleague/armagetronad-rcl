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
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// ---- settings ----

// 0: today's fixed-function drawing straight onto the screen. 1: the new
// renderer, when everything it needs could be made; when not, 0 for the rest
// of the session, and the console says why.
static int sr_render = 1;
static tConfItem< int > sr_renderConf( "RCL_RENDER", sr_render );

// 1: act as if the new renderer could not be made, to see the way back work
static int sr_renderFail = 0;
static tSettingItem< int > sr_renderFailConf( "RCL_RENDER_FAIL", sr_renderFail );

// 1: scenes in half floats, where light can go over 1 and glow. 0: in 8 bits,
// put on the screen as they are: the classic renderer's picture, through the
// new one's framebuffer (to compare the two pixel by pixel).
static int sr_hdr = 1;
static tConfItem< int > sr_hdrConf( "RCL_HDR", sr_hdr );

// The float colour: 0 picks, 1 half floats with alpha (RGBA16F) throughout, 2
// floats packed in 32 bits (R11G11B10F: half the bandwidth, no alpha) for the
// scenes too. Picking takes the packed one for the glow's chain only. The
// scenes keep alpha: the clean arena gathers its shadows there
// (sg_ShadowsBegin in gCycle.cpp; the menu's replay and the colour studio do
// as well, whichever arena the player has), and without it they are drawn
// wall by wall and double up where walls overlap. 2 shows that.
static int sr_hdrFormat = 0;
static tConfItem< int > sr_hdrFormatConf( "RCL_HDR_FORMAT", sr_hdrFormat );

// The view transform: 0 none (clipped at white), 1 AgX as Blender has it, 2
// the game's own colours with only what goes over white rolled off. 2 by
// default: AgX turns a #ffff88 trail tan and the pale arena grey, and a
// trail's colour says whose it is.
static int sr_tonemap = 2;
static tConfItem< int > sr_tonemapConf( "RCL_TONEMAP", sr_tonemap );

// light let in before the view transform, in stops like Blender's exposure
static REAL sr_exposure = 0;
static tConfItem< REAL > sr_exposureConf( "RCL_EXPOSURE", sr_exposure );

// The glow: what is brighter than the threshold (in linear light, 1 being
// white) spreads around itself, by this strength.
static int sr_bloom = 1;
static tConfItem< int > sr_bloomConf( "RCL_BLOOM", sr_bloom );
static REAL sr_bloomStrength = 1;
static tConfItem< REAL > sr_bloomStrengthConf( "RCL_BLOOM_STRENGTH", sr_bloomStrength );
static REAL sr_bloomThreshold = 1;
static tConfItem< REAL > sr_bloomThresholdConf( "RCL_BLOOM_THRESHOLD", sr_bloomThreshold );

// 1: the shader files are looked at about once a second and read again when
// one has changed, so they can be worked on while the game runs
static int sr_shaderReload = 0;
static tSettingItem< int > sr_shaderReloadConf( "RCL_SHADER_RELOAD", sr_shaderReload );

namespace
{
    // the glow is gathered at half the screen's size and four more halvings
    int const sr_levels = 5;

    // the scene is drawn in display values, as the fixed-function drawing
    // always has; light is that to this power
    float const sr_gamma = 2.2f;

    struct gPost
    {
        bool failed;            // the new renderer is off for this session
        std::string why;
        bool shadersTried;      // for this context
        int screenDepthBits;    // of the screen's own framebuffer, -1 not asked yet
        double reloadCheckedAt;

        rFramebuffer scene;     // drawn into; multisampled with RCL_ANTIALIAS
        rFramebuffer resolved;  // the scene with one sample, when it has more
        rFramebuffer bloom[ sr_levels ];
        bool bloomReady;
        bool bloomFailed;       // could not be made: the session goes without glow
        int formatWanted;       // what the targets were made for
        int samplesWanted;
        std::string notes;      // what had to be given up, for the overlay

        // the scene being drawn
        bool open, redirected;
        GLint viewport[4];
        unsigned int previous;  // the framebuffer that was bound before
        int path;

        std::string status;

        gPost()
        : failed( false ), shadersTried( false ), screenDepthBits( -1 ), reloadCheckedAt( 0 ),
          bloomReady( false ), bloomFailed( false ), formatWanted( -1 ), samplesWanted( 0 ),
          open( false ), redirected( false ), previous( 0 ), path( 0 )
        {
            viewport[0] = viewport[1] = viewport[2] = viewport[3] = 0;
        }
    };

    gPost sr_post;

    rShader sr_tonemapShader( "post.vert", "tonemap.frag" );
    rShader sr_downShader( "post.vert", "bloom_down.frag" );
    rShader sr_upShader( "post.vert", "bloom_up.frag" );
    rShader sr_emissiveShader( "emissive.vert", "emissive.frag" );

    rShader * const sr_shaders[] = { &sr_tonemapShader, &sr_downShader, &sr_upShader, &sr_emissiveShader };
    int const sr_shaderCount = sizeof( sr_shaders ) / sizeof( sr_shaders[0] );

    void sr_ReleaseTargets()
    {
        sr_post.scene.Release();
        sr_post.resolved.Release();
        for ( int i = 0; i < sr_levels; ++i )
            sr_post.bloom[i].Release();
        sr_post.bloomReady = false;
    }

    // ---- Blender's AgX, as a table ----

    // shaders/agx.cube, baked from Blender's own view transform by
    // scripts/bake-agx-lut.py. Without it the tone map falls back to a
    // polynomial approximation that is further from Blender's look,
    // saturated colours most of all, and the console says so.
    struct gTable
    {
        GLuint texture;
        int size;
        long stamp;
        gTable(): texture( 0 ), size( 0 ), stamp( 0 ){}
    };
    gTable sr_agx;

    void sr_ReleaseTable()
    {
        if ( sr_agx.texture )
            glDeleteTextures( 1, &sr_agx.texture );
        sr_agx = gTable();
    }

    void sr_LoadTable()
    {
        sr_ReleaseTable();
        sr_agx.stamp = rShader::Stamp( "agx.cube" );
        std::string text, error;
        if ( !rShader::Read( "agx.cube", text, error ) )
        {
            con << "render: " << error << "; AgX is approximated\n";
            return;
        }

        // the .cube layout: a size, then red fastest, green, blue
        std::istringstream in( text );
        std::string line;
        int size = 0;
        std::vector< float > values;
        while ( std::getline( in, line ) )
        {
            if ( line.compare( 0, 11, "LUT_3D_SIZE" ) == 0 )
            {
                size = std::atoi( line.c_str() + 11 );
                continue;
            }
            if ( line.empty() || !( line[0] == '-' || line[0] == '.' || ( line[0] >= '0' && line[0] <= '9' ) ) )
                continue;
            float rgb[3];
            if ( std::sscanf( line.c_str(), "%f %f %f", &rgb[0], &rgb[1], &rgb[2] ) == 3 )
                values.insert( values.end(), rgb, rgb + 3 );
        }
        if ( size < 2 || size > 128 || values.size() != size_t( size ) * size * size * 3 )
        {
            con << "render: shaders/agx.cube is not a 3D table this can read; AgX is approximated\n";
            return;
        }

        // the blue slices side by side: x is blue * size + red, y is green
        int const width = size * size;
        std::vector< GLushort > pixels( size_t( width ) * size * 3 );
        for ( int b = 0; b < size; ++b )
            for ( int g = 0; g < size; ++g )
                for ( int r = 0; r < size; ++r )
                    for ( int k = 0; k < 3; ++k )
                    {
                        float v = values[ ( ( size_t( b ) * size + g ) * size + r ) * 3 + k ];
                        v = v < 0 ? 0 : ( v > 1 ? 1 : v );
                        pixels[ ( size_t( g ) * width + b * size + r ) * 3 + k ] = static_cast< GLushort >( v * 65535 + .5f );
                    }

        while ( glGetError() != GL_NO_ERROR )
            ;
        GLint previous = 0, alignment = 4;
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &previous );
        glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        GLuint texture = 0;
        glGenTextures( 1, &texture );
        glBindTexture( GL_TEXTURE_2D, texture );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB16, width, size, 0, GL_RGB, GL_UNSIGNED_SHORT, &pixels[0] );
        glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
        glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previous ) );
        if ( glGetError() != GL_NO_ERROR )
        {
            glDeleteTextures( 1, &texture );
            con << "render: the AgX table could not be made; AgX is approximated\n";
            return;
        }
        sr_agx.texture = texture;
        sr_agx.size = size;
    }

    void sr_ReleaseAll()
    {
        sr_ReleaseTargets();
        for ( int i = 0; i < sr_shaderCount; ++i )
            sr_shaders[i]->Release();
        sr_ReleaseTable();
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
            for ( int i = 0; i < sr_levels; ++i )
                sr_post.bloom[i].Forget();
            for ( int i = 0; i < sr_shaderCount; ++i )
                sr_shaders[i]->Forget();
            sr_agx = gTable();
            sr_post.bloomReady = false;
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
        sr_LoadTable();
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
        if ( rShader::Stamp( "agx.cube" ) != sr_agx.stamp )
        {
            sr_LoadTable();
            if ( sr_agx.texture )
                con << "render: read agx.cube again\n";
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
    // when first needed and again when the size or the settings it depends
    // on change. What cannot be had is given up in this order: float colour
    // (no glow), then multisampling; without any picture at all the new
    // renderer is off.
    bool sr_Targets()
    {
        int const width = sr_renderWidth, height = sr_renderHeight;
        bool const hdr = sr_hdr > 0 && rGLExt::FloatColour();
        int const samples = sr_Samples();

        // the float colour of the scene and of the glow's chain (RCL_HDR_FORMAT)
        bool const packed = rGLExt::PackedFloat() && sr_hdrFormat != 1;
        int const sceneFloat = packed && sr_hdrFormat == 2 ? rColour_R11G11B10F : rColour_RGBA16F;
        int const format = hdr ? sceneFloat : rColour_RGBA8;
        int const bloomFormat = packed ? rColour_R11G11B10F : rColour_RGBA16F;

        if ( sr_post.scene.Ready() && sr_post.scene.Width() == width && sr_post.scene.Height() == height &&
             sr_post.formatWanted == format && sr_post.samplesWanted == samples )
            return true;

        sr_ReleaseTargets();
        sr_post.formatWanted = format;
        sr_post.samplesWanted = samples;
        sr_post.notes.clear();
        if ( sr_hdr > 0 && !hdr )
            sr_post.notes = "no float colour: 8 bits, no glow";

        // as deep as the screen's own, so what is drawn meets in depth as it did there
        if ( sr_post.screenDepthBits < 0 )
        {
            GLint bits = 24;
            if ( rFramebuffer::Current() == 0 )
                glGetIntegerv( GL_DEPTH_BITS, &bits );
            sr_post.screenDepthBits = bits > 0 ? bits : 24;
        }
        int const depth = sr_post.screenDepthBits;

        // the packed floats give way to half floats, those to 8 bits, then multisampling goes
        struct gTry { int format; int samples; } const tries[6] =
        {
            { format, samples }, { hdr ? rColour_RGBA16F : format, samples }, { rColour_RGBA8, samples },
            { format, 0 }, { hdr ? rColour_RGBA16F : format, 0 }, { rColour_RGBA8, 0 }
        };
        std::string error, firstError;
        bool made = false;
        for ( int i = 0; i < 6 && !made; ++i )
        {
            bool tried = false;
            for ( int j = 0; j < i; ++j )
                tried = tried || ( tries[j].format == tries[i].format && tries[j].samples == tries[i].samples );
            if ( tried )
                continue;
            if ( sr_post.scene.Create( width, height, tries[i].format, tries[i].samples, depth, error ) )
            {
                made = true;
                if ( tries[i].format != format || tries[i].samples != samples )
                {
                    std::string given;
                    if ( tries[i].format != format )
                        given = tries[i].format == rColour_RGBA8 ? "float colour (no glow)" : "packed floats";
                    if ( tries[i].samples != samples )
                        given += given.empty() ? "multisampling" : " and multisampling";
                    if ( !sr_post.notes.empty() )
                        sr_post.notes += "; ";
                    sr_post.notes += "gave up " + given + ": " + firstError;
                    con << "render: " << sr_post.notes << "\n";
                }
            }
            else if ( firstError.empty() )
                firstError = error;
        }
        if ( !made )
        {
            sr_Fail( "no scene framebuffer: " + firstError );
            return false;
        }

        if ( sr_post.scene.Samples() > 0 &&
             !sr_post.resolved.Create( width, height, sr_post.scene.Format(), 0, 0, error ) )
        {
            sr_Fail( "no framebuffer to resolve the multisampled scene into: " + error );
            return false;
        }

        // the glow's chain: half the size, and halved again for every level;
        // it needs no alpha
        sr_post.bloomReady = false;
        if ( sr_post.scene.Float() && !sr_post.bloomFailed )
        {
            bool ok = true;
            for ( int i = 0; i < sr_levels && ok; ++i )
            {
                int const w = width >> ( i + 1 ), h = height >> ( i + 1 );
                ok = sr_post.bloom[i].Create( w > 0 ? w : 1, h > 0 ? h : 1, bloomFormat, 0, 0, error );
                if ( !ok && bloomFormat != rColour_RGBA16F )
                    ok = sr_post.bloom[i].Create( w > 0 ? w : 1, h > 0 ? h : 1, rColour_RGBA16F, 0, 0, error );
            }
            if ( ok )
                sr_post.bloomReady = true;
            else
            {
                sr_post.bloomFailed = true;
                for ( int i = 0; i < sr_levels; ++i )
                    sr_post.bloom[i].Release();
                con << "render: no glow for this session: " << error << "\n";
            }
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

    // a part of a picture as what is drawn into
    void sr_Target( gRect const & part )
    {
        glViewport( part.x, part.y, part.w, part.h );
    }

    // where a shader may read a picture: inside the part, half a texel in, so
    // nothing from beside the viewport (another viewport, an old frame) leaks in
    void sr_Bounds( rShader & shader, gRect const & part )
    {
        shader.Set( "u_bounds", ( part.x + .5f ) / part.width, ( part.y + .5f ) / part.height,
                    ( part.x + part.w - .5f ) / part.width, ( part.y + part.h - .5f ) / part.height );
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

    // The glow: the scene's light over the threshold, made smaller and
    // softer step by step down to a thirty-second of the screen, then
    // brought back up, each level adding itself to the one above (Marius
    // Bjørge's dual filter, SIGGRAPH 2015). What is left at half size is
    // laid over the scene by the tone map.
    void sr_Bloom( GLuint sceneTexture, gRect const & scenePart )
    {
        sr_downShader.Use();
        sr_downShader.SetSampler( "u_source", 0 );
        float const threshold = sr_bloomThreshold > 0 ? float( sr_bloomThreshold ) : 0.f;
        sr_downShader.Set( "u_threshold", threshold );
        sr_downShader.Set( "u_knee", threshold * .5f + 1E-3f );
        sr_downShader.Set( "u_gamma", sr_gamma );

        gRect source = scenePart;
        GLuint sourceTexture = sceneTexture;
        for ( int i = 0; i < sr_levels; ++i )
        {
            rFramebuffer & level = sr_post.bloom[i];
            gRect const part = sr_Part( level.Width(), level.Height() );
            level.Bind();
            sr_Target( part );
            sr_Texture( 0, sourceTexture );
            sr_downShader.Set( "u_prefilter", i == 0 ? 1.f : 0.f );
            sr_downShader.Set( "u_texel", 1.f / source.width, 1.f / source.height );
            sr_Bounds( sr_downShader, source );
            sr_Quad( part );
            source = part;
            sourceTexture = level.Texture();
        }

        sr_upShader.Use();
        sr_upShader.SetSampler( "u_source", 0 );
        glEnable( GL_BLEND );
        glBlendFunc( GL_ONE, GL_ONE );
        for ( int i = sr_levels - 2; i >= 0; --i )
        {
            rFramebuffer & level = sr_post.bloom[i];
            rFramebuffer & smaller = sr_post.bloom[i + 1];
            gRect const part = sr_Part( level.Width(), level.Height() );
            gRect const from = sr_Part( smaller.Width(), smaller.Height() );
            level.Bind();
            sr_Target( part );
            sr_Texture( 0, smaller.Texture() );
            sr_upShader.Set( "u_texel", 1.f / from.width, 1.f / from.height );
            sr_Bounds( sr_upShader, from );
            sr_Quad( part );
        }
        glDisable( GL_BLEND );
    }

    // the scene, resolved, glowing and through the view transform, onto the
    // framebuffer it would have been drawn into, in its viewport
    void sr_Composite()
    {
        // Only what is changed here is saved: all of GL's state is costly to
        // keep, every texture unit's most of all. The textures bound here are
        // unbound at the end; whatever draws next binds its own.
        glPushAttrib( GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_VIEWPORT_BIT | GL_POLYGON_BIT );
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

        // RCL_HDR 0 shows the scene as it is; the 8-bit fallback for
        // drivers without float colour keeps the view transform but cannot glow
        bool const copy = sr_hdr <= 0;
        bool const bloom = !copy && sr_post.bloomReady && sr_bloom > 0 && sr_bloomStrength > 0;
        if ( bloom )
        {
            rGPUTimer::Begin( rGPUTimer::Pass_Bloom );
            sr_Bloom( sceneTexture, scenePart );
            rGPUTimer::End( rGPUTimer::Pass_Bloom );
        }

        rGPUTimer::Begin( rGPUTimer::Pass_Tonemap );
        rFramebuffer::Bind( sr_post.previous );
        glViewport( sr_post.viewport[0], sr_post.viewport[1], sr_post.viewport[2], sr_post.viewport[3] );
        sr_tonemapShader.Use();
        sr_tonemapShader.SetSampler( "u_scene", 0 );
        sr_tonemapShader.SetSampler( "u_bloom", 1 );
        sr_tonemapShader.SetSampler( "u_agx", 2 );
        sr_tonemapShader.Set( "u_agxSize", sr_agx.texture ? float( sr_agx.size ) : 0.f );
        sr_Texture( 2, sr_agx.texture );
        sr_Texture( 1, bloom ? sr_post.bloom[0].Texture() : 0 );
        sr_Texture( 0, sceneTexture );
        int const mode = copy ? 0 : ( sr_tonemap == 1 ? 2 : sr_tonemap == 2 ? 3 : 1 );
        sr_tonemapShader.Set( "u_mode", float( mode ) );
        sr_tonemapShader.Set( "u_exposure", float( std::pow( 2.0, double( sr_exposure ) ) ) );
        sr_tonemapShader.Set( "u_gamma", sr_gamma );
        // each level of the chain has added its share once
        sr_tonemapShader.Set( "u_bloomStrength", bloom ? float( sr_bloomStrength ) / sr_levels : 0.f );
        if ( bloom )
            sr_Bounds( sr_tonemapShader, sr_Part( sr_post.bloom[0].Width(), sr_post.bloom[0].Height() ) );
        sr_tonemapShader.Set( "u_dither", copy ? 0.f : 1.f / 255 );
        sr_Quad( scenePart );
        rGPUTimer::End( rGPUTimer::Pass_Tonemap );

        rShader::UseNone();
        sr_Texture( 2, 0 );
        sr_Texture( 1, 0 );
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
            char const * const transforms[] = { "no view transform",
                                                sr_agx.texture ? "AgX (Blender's, table)" : "AgX (approximated)", "soft" };
            s << "new renderer: " << sr_post.scene.Width() << 'x' << sr_post.scene.Height() << ' '
              << rFramebuffer::FormatName( sr_post.scene.Format() );
            if ( sr_post.scene.Samples() > 0 )
                s << " msaa " << sr_post.scene.Samples();
            if ( sr_hdr <= 0 )
                s << ", copied as it is (RCL_HDR 0)";
            else
            {
                s << ", " << transforms[ sr_tonemap >= 0 && sr_tonemap <= 2 ? sr_tonemap : 0 ];
                if ( sr_post.bloomReady && sr_bloom > 0 )
                    s << ", glow " << rFramebuffer::FormatName( sr_post.bloom[0].Format() );
            }
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

    // ---- emissive ----

    int sr_emissiveDepth = 0;
    float sr_emissiveScale[8];

    // what is to be drawn over the scene once it is on the screen
    std::vector< void ( * )() > sr_afterScene;

    unsigned sr_sceneSerial = 0;
}

bool rPost::BeginScene()
{
    if ( !sr_glOut || sr_post.open )
        return false;
    sr_post.open = true;
    sr_post.redirected = false;
    sr_post.path = 0;
    ++sr_sceneSerial;
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
    while ( sr_emissiveDepth > 0 )
    {
        // an rEmissive outlived its scene; what it bound goes with it
        --sr_emissiveDepth;
        rShader::UseNone();
    }
    rGPUTimer::End( rGPUTimer::Pass_World );
    if ( !sr_post.redirected )
        return;
    sr_post.redirected = false;
    RenderEnd();
    sr_Composite();

    std::vector< void ( * )() > after;
    after.swap( sr_afterScene );
    for ( size_t i = 0; i < after.size(); ++i )
        ( *after[i] )();
}

bool rPost::InScene()
{
    return sr_post.open && sr_post.redirected;
}

unsigned rPost::SceneSerial()
{
    return sr_sceneSerial;
}

void rPost::AfterScene( void ( *f )() )
{
    if ( !InScene() || !f )
        return;
    for ( size_t i = 0; i < sr_afterScene.size(); ++i )
        if ( sr_afterScene[i] == f )
            return;
    sr_afterScene.push_back( f );
}

bool rPost::Glows()
{
    return sr_post.open && sr_post.redirected && sr_post.scene.Float() && sr_hdr > 0 && sr_emissiveShader.Ready();
}

bool rPost::Glows( REAL intensity )
{
    return intensity > 1 && Glows();
}

int rPost::Path()
{
    return sr_post.path;
}

bool rPost::ReloadWanted()
{
    return sr_shaderReload > 0;
}

char const * rPost::Status()
{
    sr_UpdateStatus();
    return sr_post.status.c_str();
}

rEmissive::rEmissive( REAL intensity )
: on_( false )
{
    if ( !( intensity > 0 ) || intensity == 1 || !rPost::Glows() || rDisplayList::IsRecording() ||
         sr_emissiveDepth >= 8 )
        return;
    RenderEnd();
    // The scene holds display values; the shader scales them so that their
    // light, display value to the power 2.2, comes out times intensity.
    float const scale = static_cast< float >( std::pow( double( intensity ), 1 / double( sr_gamma ) ) );
    sr_emissiveShader.Use();
    sr_emissiveShader.Set( "u_scale", scale );
    sr_emissiveScale[ sr_emissiveDepth++ ] = scale;
    on_ = true;
}

rEmissive::~rEmissive()
{
    if ( !on_ || sr_emissiveDepth <= 0 )
        return;
    RenderEnd();
    --sr_emissiveDepth;
    if ( sr_emissiveDepth > 0 )
        sr_emissiveShader.Set( "u_scale", sr_emissiveScale[ sr_emissiveDepth - 1 ] );
    else
        rShader::UseNone();
}

#endif // DEDICATED
