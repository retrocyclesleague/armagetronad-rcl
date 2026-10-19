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

// GLEW first, before anything brings in gl.h
#include "rGLExt.h"
#include "gCycleMesh.h"

#ifndef DEDICATED

#include "eCoord.h"
#include "eFloor.h"
#include "gLightCycle.h"
#include "rDisplayList.h"
#include "rGPUTimer.h"
#include "rPost.h"
#include "rScreen.h"
#include "rShader.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tDirectories.h"
#include "tString.h"
#include "tSysTime.h"
#include "rRender.h"

#include <png.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The light cycle from the Blender model (RCL_CYCLE_MODEL 2), drawn with
// physically based shading. What it reads, all from the data folder, made by
// scripts/install-cycle-assets.py from the Blender asset pack:
//
//   models/rcl/lightcycle.rclmesh        the levels of detail (scripts/convert-cycle-mesh.py
//                                        says the format)
//   textures/rcl/lightcycle_lod<N>_*.png each level's baked maps on its own UVs: normal
//                                        (tangent space, OpenGL), basecolor (sRGB), ao,
//                                        roughness, mask (R emissive, G metallic, B clear gloss)
//   textures/rcl/env_<look>_*.hdr        the pale and the night arena as RGBE cubemaps, six GL
//                                        faces side by side: spec_m0.. GGX-prefiltered by mip
//                                        (roughness = mip / 5), irradiance
//   textures/rcl/brdf_lut_ggx.png        the split-sum table: x NoV, y roughness -> (A, B)
//
// Everything but the mesh may be missing: without maps each material has its
// own roughness, metal and colour, a flat normal and no occlusion; without a
// look's cubemaps a sky and floor worked out from the arena's colours, and
// the sun as a light; without the table an analytic fit of it. Without the
// mesh, or without the new renderer, the light cycle is drawn instead.

// the levels of detail by the cycle's length on the screen, in pixels: under
// the first the second level, under the second the third, under the last the
// light cycle, whose far-away look holds the bike's colour
static REAL sg_cycleLod1 = 240;
static tConfItem< REAL > sg_cycleLod1Conf( "RCL_CYCLE_LOD1", sg_cycleLod1 );
static REAL sg_cycleLod2 = 80;
static tConfItem< REAL > sg_cycleLod2Conf( "RCL_CYCLE_LOD2", sg_cycleLod2 );
static REAL sg_cycleFar = 24;
static tConfItem< REAL > sg_cycleFarConf( "RCL_CYCLE_FAR", sg_cycleFar );

// The sun as a light of its own, by this share of its strength, on top of the
// cubemaps, which hold the sun already; without them it is always there.
static REAL sg_cycleSun = 0;
static tConfItem< REAL > sg_cycleSunConf( "RCL_CYCLE_SUN", sg_cycleSun );

// 0: leave the cubemaps or the baked maps out, to see what they bring
static int sg_cycleEnv = 1;
static tSettingItem< int > sg_cycleEnvConf( "RCL_CYCLE_ENV", sg_cycleEnv );
static int sg_cycleMaps = 1;
static tSettingItem< int > sg_cycleMapsConf( "RCL_CYCLE_MAPS", sg_cycleMaps );

namespace
{
    enum { sg_objects = 3, sg_materials = 6, sg_maxLods = 4 };
    char const * const sg_objectNames[ sg_objects ] = { "body", "front_wheel", "rear_wheel" };

    // the light cycle's frame (gLightCycle.cpp): its nose, its tail where the
    // wall leaves, its length
    float const sg_nose = 2.32f, sg_length = 3.02f;
    // its middle, for the size on the screen
    float const sg_middle[3] = { .80f, 0, .65f };

    // What each material is where there are no maps, from the Blender model's
    // materials (rcl-cfg/art/lightcycle/build_lightcycle.py): colour in
    // light, roughness, metal, clear gloss, emissive.
    struct gMaterial
    {
        float base[3], rough, metal, gloss, emit;
    };
    gMaterial const sg_material[ sg_materials ] =
    {
        { { .58f, .60f, .62f }, .28f, 1.0f, .35f, 0 },      // shell: light grey metal, satin
        { { .004f, .005f, .008f }, .04f, 0, .8f, 0 },       // canopy: dark gloss
        { { 1, 1, 1 }, .5f, 0, 0, 1 },                      // light: the bike's colour, emissive
        { { .012f, .013f, .016f }, .22f, .5f, .6f, 0 },     // detail: graphite
        { { .16f, .17f, .18f }, .12f, 1.0f, 1.0f, 0 },      // hub: polished graphite metal
        { { .007f, .007f, .008f }, .72f, 0, 0, 0 }          // tyre: rough black
    };

    // ---- small matrices, column by column as GL keeps them ----

    void Identity( float * m )
    {
        for ( int i = 0; i < 16; ++i )
            m[i] = ( i % 5 ) ? 0.f : 1.f;
    }

    void Multiply( float const * a, float const * b, float * out )
    {
        float r[16];
        for ( int c = 0; c < 4; ++c )
            for ( int row = 0; row < 4; ++row )
            {
                float s = 0;
                for ( int k = 0; k < 4; ++k )
                    s += a[ k * 4 + row ] * b[ c * 4 + k ];
                r[ c * 4 + row ] = s;
            }
        std::memcpy( out, r, sizeof( r ) );
    }

    void Translation( float * m, float x, float y, float z )
    {
        Identity( m );
        m[12] = x;
        m[13] = y;
        m[14] = z;
    }

    bool Invert( float const * m, float * out )
    {
        float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        float const det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if ( !( std::fabs( det ) > 1E-20f ) )
            return false;
        for ( int i = 0; i < 16; ++i )
            out[i] = inv[i] / det;
        return true;
    }

    void Apply( float const * m, float const * p, float w, float * out )
    {
        for ( int row = 0; row < 3; ++row )
            out[row] = m[row] * p[0] + m[4 + row] * p[1] + m[8 + row] * p[2] + m[12 + row] * w;
    }

    // ---- files ----

    std::string sg_Path( char const * name )
    {
        tString const path = tDirectories::Data().GetReadPath( name );
        return path.Len() > 1 ? std::string( static_cast< char const * >( path ) ) : std::string();
    }

    // when a data file was last changed, 0 if it is not there
    long sg_Stamp( char const * name )
    {
        std::string const path = sg_Path( name );
        struct stat info;
        if ( path.empty() || stat( path.c_str(), &info ) != 0 )
            return 0;
        return static_cast< long >( info.st_mtime ) ^ static_cast< long >( info.st_size );
    }

    bool sg_ReadFile( char const * name, std::vector< unsigned char > & data )
    {
        std::string const path = sg_Path( name );
        if ( path.empty() )
            return false;
        FILE * f = std::fopen( path.c_str(), "rb" );
        if ( !f )
            return false;
        std::fseek( f, 0, SEEK_END );
        long const size = std::ftell( f );
        std::fseek( f, 0, SEEK_SET );
        bool ok = size > 0 && size < ( 64L << 20 );
        if ( ok )
        {
            data.resize( static_cast< size_t >( size ) );
            ok = std::fread( &data[0], 1, data.size(), f ) == data.size();
        }
        std::fclose( f );
        return ok;
    }

    // An image as its file has it: rows top first, 8 or 16 bits a channel
    // (16 in the machine's byte order), values untouched (no gamma).
    struct gImage
    {
        int width, height, channels, depth;
        std::vector< unsigned char > data;
        gImage(): width( 0 ), height( 0 ), channels( 0 ), depth( 0 ){}
        unsigned char At( int x, int y, int c ) const
        {
            if ( c >= channels )
                c = channels > 2 ? channels - 1 : 0;
            return data[ ( size_t( y ) * width + x ) * channels + c ];
        }
    };

    struct gPngRead
    {
        png_structp png;
        png_infop info;
        FILE * file;
        gPngRead(): png( 0 ), info( 0 ), file( 0 ){}
        ~gPngRead()
        {
            if ( png )
                png_destroy_read_struct( &png, info ? &info : 0, 0 );
            if ( file )
                std::fclose( file );
        }
    };

    bool sg_ReadPng( char const * name, gImage & image )
    {
        std::string const path = sg_Path( name );
        if ( path.empty() )
            return false;
        gPngRead read;
        read.file = std::fopen( path.c_str(), "rb" );
        if ( !read.file )
            return false;
        png_byte signature[8];
        if ( std::fread( signature, 1, 8, read.file ) != 8 || png_sig_cmp( signature, 0, 8 ) )
            return false;
        read.png = png_create_read_struct( PNG_LIBPNG_VER_STRING, 0, 0, 0 );
        if ( !read.png )
            return false;
        read.info = png_create_info_struct( read.png );
        if ( !read.info )
            return false;
        std::vector< png_bytep > rows;
        if ( setjmp( png_jmpbuf( read.png ) ) )
            return false;
        png_init_io( read.png, read.file );
        png_set_sig_bytes( read.png, 8 );
        png_read_info( read.png, read.info );
        int const type = png_get_color_type( read.png, read.info );
        int const depth = png_get_bit_depth( read.png, read.info );
        if ( type == PNG_COLOR_TYPE_PALETTE )
            png_set_palette_to_rgb( read.png );
        if ( type == PNG_COLOR_TYPE_GRAY && depth < 8 )
            png_set_expand_gray_1_2_4_to_8( read.png );
        if ( depth == 16 )
        {
            // PNG keeps 16 bits big end first
            unsigned short const probe = 1;
            if ( *reinterpret_cast< unsigned char const * >( &probe ) == 1 )
                png_set_swap( read.png );
        }
        png_read_update_info( read.png, read.info );

        image.width = static_cast< int >( png_get_image_width( read.png, read.info ) );
        image.height = static_cast< int >( png_get_image_height( read.png, read.info ) );
        image.channels = png_get_channels( read.png, read.info );
        image.depth = png_get_bit_depth( read.png, read.info );
        size_t const rowBytes = png_get_rowbytes( read.png, read.info );
        if ( image.width < 1 || image.height < 1 || image.width > 8192 || image.height > 8192 ||
             rowBytes != size_t( image.width ) * image.channels * ( image.depth / 8 ) )
            return false;
        image.data.resize( rowBytes * image.height );
        rows.resize( image.height );
        for ( int y = 0; y < image.height; ++y )
            rows[y] = &image.data[ rowBytes * y ];
        png_read_image( read.png, &rows[0] );
        png_read_end( read.png, 0 );
        return true;
    }

    // Radiance RGBE: the header, "-Y <height> +X <width>", then flat or
    // run-length scanlines, top row first; to linear light.
    bool sg_ReadHdr( char const * name, int & width, int & height, std::vector< float > & rgb )
    {
        std::vector< unsigned char > file;
        if ( !sg_ReadFile( name, file ) )
            return false;
        size_t at = 0;
        std::string line;
        bool format = false;
        for ( ;; )
        {
            line.clear();
            while ( at < file.size() && file[at] != '\n' )
                line += static_cast< char >( file[at++] );
            if ( at >= file.size() )
                return false;
            ++at;
            if ( line.empty() )
                break;
            if ( line == "FORMAT=32-bit_rle_rgbe" )
                format = true;
        }
        line.clear();
        while ( at < file.size() && file[at] != '\n' )
            line += static_cast< char >( file[at++] );
        ++at;
        if ( !format || std::sscanf( line.c_str(), "-Y %d +X %d", &height, &width ) != 2 ||
             width < 1 || height < 1 || width > 16384 || height > 16384 )
            return false;

        std::vector< unsigned char > scan( size_t( width ) * 4 );
        rgb.resize( size_t( width ) * height * 3 );
        for ( int y = 0; y < height; ++y )
        {
            if ( at + 4 > file.size() )
                return false;
            unsigned char const * p = &file[at];
            if ( p[0] == 2 && p[1] == 2 && !( p[2] & 0x80 ) && width >= 8 && width < 32768 &&
                 ( ( p[2] << 8 ) | p[3] ) == width )
            {
                // run length, channel by channel
                at += 4;
                for ( int c = 0; c < 4; ++c )
                {
                    int x = 0;
                    while ( x < width )
                    {
                        if ( at >= file.size() )
                            return false;
                        int count = file[at++];
                        if ( count > 128 )
                        {
                            count -= 128;
                            if ( at >= file.size() || x + count > width )
                                return false;
                            unsigned char const value = file[at++];
                            for ( int k = 0; k < count; ++k )
                                scan[ size_t( x++ ) * 4 + c ] = value;
                        }
                        else
                        {
                            if ( count == 0 || x + count > width || at + count > file.size() )
                                return false;
                            for ( int k = 0; k < count; ++k )
                                scan[ size_t( x++ ) * 4 + c ] = file[at++];
                        }
                    }
                }
            }
            else
            {
                if ( at + scan.size() > file.size() )
                    return false;
                std::memcpy( &scan[0], &file[at], scan.size() );
                at += scan.size();
            }
            float * out = &rgb[ size_t( y ) * width * 3 ];
            for ( int x = 0; x < width; ++x )
            {
                unsigned char const * e = &scan[ size_t( x ) * 4 ];
                float const f = e[3] ? static_cast< float >( std::ldexp( 1.0, int( e[3] ) - 136 ) ) : 0.f;
                for ( int c = 0; c < 3; ++c )
                    out[ x * 3 + c ] = e[3] ? ( e[c] + .5f ) * f : 0.f;
            }
        }
        return true;
    }

    // ---- what is held on the GPU ----

    struct gRange
    {
        int object, material;
        unsigned first, count;
    };

    struct gLod
    {
        GLuint vertices, indices;
        unsigned vertexCount, indexCount;
        std::vector< gRange > ranges;
        GLuint normal, base, orm;       // the maps: normal; base colour + emissive; AO, roughness, metal, gloss
        bool hasNormal, hasBase, hasOrm, hasEmit;
        long mapStamp;
        gLod(): vertices( 0 ), indices( 0 ), vertexCount( 0 ), indexCount( 0 ), normal( 0 ), base( 0 ), orm( 0 ),
                hasNormal( false ), hasBase( false ), hasOrm( false ), hasEmit( false ), mapStamp( 0 ){}
    };

    struct gEnv
    {
        GLuint spec, irradiance;
        int levels;
        long stamp;
        bool tried;
        gEnv(): spec( 0 ), irradiance( 0 ), levels( 0 ), stamp( 0 ), tried( false ){}
    };

    struct gMesh
    {
        bool tried, failed;     // tried for this context; failed for good this session
        long stamp;
        float lo[3], hi[3];
        float pivot[ sg_objects ][4];
        std::vector< gLod > lods;
        gEnv env[2];            // pale, night
        GLuint brdf;
        bool brdfTried;
        long brdfStamp;
        double checkedAt;
        gMesh(): tried( false ), failed( false ), stamp( 0 ), brdf( 0 ), brdfTried( false ), brdfStamp( 0 ),
                 checkedAt( 0 ){}
    };

    gMesh sg_mesh;
    rShader sg_shader( "cycle_pbr.vert", "cycle_pbr.frag" );
    bool sg_shaderTried = false;
    int sg_shaderLinks = 0;                     // counts the programs made, which start without uniforms
    int sg_sceneKey[5] = { -1, -1, -1, -1, -1 };  // what the uniforms set once a scene were set for

    void sg_DeleteTexture( GLuint & texture )
    {
        if ( texture )
            glDeleteTextures( 1, &texture );
        texture = 0;
    }

    void sg_ReleaseMaps( gLod & lod )
    {
        sg_DeleteTexture( lod.normal );
        sg_DeleteTexture( lod.base );
        sg_DeleteTexture( lod.orm );
        lod.hasNormal = lod.hasBase = lod.hasOrm = lod.hasEmit = false;
    }

    void sg_ReleaseEnv( gEnv & env )
    {
        sg_DeleteTexture( env.spec );
        sg_DeleteTexture( env.irradiance );
        env.levels = 0;
        env.tried = false;
    }

    void sg_ReleaseMesh()
    {
#ifdef RGL_EXT_AVAILABLE
        for ( size_t i = 0; i < sg_mesh.lods.size(); ++i )
        {
            gLod & lod = sg_mesh.lods[i];
            if ( lod.vertices )
                glDeleteBuffers( 1, &lod.vertices );
            if ( lod.indices )
                glDeleteBuffers( 1, &lod.indices );
            sg_ReleaseMaps( lod );
        }
#endif
        sg_mesh.lods.clear();
        sg_mesh.tried = false;
    }

    // Everything here belongs to the context; before it may go, it goes, and
    // is made again when a cycle is next drawn.
    void sg_CycleMeshRelease()
    {
        if ( rGLExt::Loaded() )
        {
            sg_ReleaseMesh();
            for ( int i = 0; i < 2; ++i )
                sg_ReleaseEnv( sg_mesh.env[i] );
            sg_DeleteTexture( sg_mesh.brdf );
            sg_shader.Release();
        }
        else
        {
            sg_mesh.lods.clear();
            sg_mesh.tried = false;
            for ( int i = 0; i < 2; ++i )
                sg_mesh.env[i] = gEnv();
            sg_mesh.brdf = 0;
            sg_shader.Forget();
        }
        sg_mesh.brdfTried = false;
        sg_shaderTried = false;
    }
    rCallbackBeforeScreenModeChange sg_cycleMeshRelease( &sg_CycleMeshRelease );

#ifdef RGL_EXT_AVAILABLE
    // ---- the mesh ----

    template< class T > T sg_Get( unsigned char const * at )
    {
        T value;
        std::memcpy( &value, at, sizeof( T ) );
        return value;
    }

    bool sg_LoadMesh( std::string & why )
    {
        std::string const path = sg_Path( "models/rcl/lightcycle.rclmesh" );
        sg_mesh.stamp = sg_Stamp( "models/rcl/lightcycle.rclmesh" );
        if ( path.empty() )
        {
            why = "no models/rcl/lightcycle.rclmesh";
            return false;
        }
        FILE * f = std::fopen( path.c_str(), "rb" );
        if ( !f )
        {
            why = "cannot read models/rcl/lightcycle.rclmesh";
            return false;
        }

        // the header: magic, levels, bounds, three pivots with radii
        unsigned char header[ 8 + 4 + 24 + 48 ];
        bool ok = std::fread( header, 1, sizeof( header ), f ) == sizeof( header ) &&
                  std::memcmp( header, "RCLMESH1", 8 ) == 0;
        unsigned const levels = ok ? sg_Get< unsigned >( header + 8 ) : 0;
        ok = ok && levels >= 1 && levels <= sg_maxLods;
        if ( ok )
        {
            for ( int k = 0; k < 3; ++k )
            {
                sg_mesh.lo[k] = sg_Get< float >( header + 12 + 4 * k );
                sg_mesh.hi[k] = sg_Get< float >( header + 24 + 4 * k );
            }
            for ( int o = 0; o < sg_objects; ++o )
                for ( int k = 0; k < 4; ++k )
                    sg_mesh.pivot[o][k] = sg_Get< float >( header + 36 + 16 * o + 4 * k );
            ok = sg_mesh.hi[0] - sg_mesh.lo[0] > .1f && sg_mesh.pivot[1][3] > 0 && sg_mesh.pivot[2][3] > 0;
        }

        std::vector< unsigned char > counts( 12 ), ranges;
        std::vector< float > vertices;
        std::vector< unsigned > indices;
        for ( unsigned level = 0; ok && level < levels; ++level )
        {
            ok = std::fread( &counts[0], 1, 12, f ) == 12;
            unsigned const vertexCount = ok ? sg_Get< unsigned >( &counts[0] ) : 0;
            unsigned const indexCount = ok ? sg_Get< unsigned >( &counts[4] ) : 0;
            unsigned const rangeCount = ok ? sg_Get< unsigned >( &counts[8] ) : 0;
            ok = ok && vertexCount > 0 && vertexCount <= ( 1u << 21 ) && indexCount > 0 &&
                 indexCount <= ( 3u << 21 ) && indexCount % 3 == 0 && rangeCount > 0 && rangeCount <= 64;
            if ( !ok )
                break;

            ranges.resize( rangeCount * 12 );
            vertices.resize( size_t( vertexCount ) * 12 );
            indices.resize( indexCount );
            ok = std::fread( &ranges[0], 1, ranges.size(), f ) == ranges.size() &&
                 std::fread( &vertices[0], sizeof( float ), vertices.size(), f ) == vertices.size() &&
                 std::fread( &indices[0], sizeof( unsigned ), indices.size(), f ) == indices.size();
            if ( !ok )
                break;

            gLod lod;
            lod.vertexCount = vertexCount;
            lod.indexCount = indexCount;
            for ( unsigned r = 0; r < rangeCount && ok; ++r )
            {
                unsigned char const * at = &ranges[ r * 12 ];
                gRange range;
                range.object = at[0];
                range.material = at[1];
                range.first = sg_Get< unsigned >( at + 4 );
                range.count = sg_Get< unsigned >( at + 8 );
                ok = range.object < sg_objects && range.material < sg_materials && range.count % 3 == 0 &&
                     range.first <= indexCount && range.count <= indexCount - range.first;
                lod.ranges.push_back( range );
            }
            for ( unsigned i = 0; i < indexCount && ok; ++i )
                ok = indices[i] < vertexCount;
            for ( size_t i = 0; i < vertices.size() && ok; ++i )
                ok = vertices[i] == vertices[i] && std::fabs( vertices[i] ) < 1E4f;
            if ( !ok )
                break;

            GLuint buffers[2] = { 0, 0 };
            glGenBuffers( 2, buffers );
            lod.vertices = buffers[0];
            lod.indices = buffers[1];
            glBindBuffer( RGL_ARRAY_BUFFER, lod.vertices );
            glBufferData( RGL_ARRAY_BUFFER, vertices.size() * sizeof( float ), &vertices[0], RGL_STATIC_DRAW );
            glBindBuffer( RGL_ELEMENT_ARRAY_BUFFER, lod.indices );
            glBufferData( RGL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof( unsigned ), &indices[0], RGL_STATIC_DRAW );
            glBindBuffer( RGL_ARRAY_BUFFER, 0 );
            glBindBuffer( RGL_ELEMENT_ARRAY_BUFFER, 0 );
            sg_mesh.lods.push_back( lod );
        }
        std::fclose( f );
        if ( !ok )
        {
            why = "models/rcl/lightcycle.rclmesh is not a mesh this can read";
            sg_ReleaseMesh();
            return false;
        }
        return true;
    }

    // ---- maps ----

    // a picture for GL, its bottom row first: the maps' rows are top first
    // and the mesh's v runs up, as Blender has it
    GLuint sg_Upload2D( int width, int height, int channels, std::vector< unsigned char > const & topFirst )
    {
        std::vector< unsigned char > rows( topFirst.size() );
        size_t const rowBytes = size_t( width ) * channels;
        for ( int y = 0; y < height; ++y )
            std::memcpy( &rows[ rowBytes * y ], &topFirst[ rowBytes * ( height - 1 - y ) ], rowBytes );

        GLint previous = 0, alignment = 4;
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &previous );
        glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        GLuint texture = 0;
        glGenTextures( 1, &texture );
        glBindTexture( GL_TEXTURE_2D, texture );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, RGL_GENERATE_MIPMAP, GL_TRUE );
        glTexImage2D( GL_TEXTURE_2D, 0, channels == 4 ? GL_RGBA8 : GL_RGB8, width, height, 0,
                      channels == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, &rows[0] );
        glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
        glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previous ) );
        return texture;
    }

    void sg_LoadMaps( int level )
    {
        gLod & lod = sg_mesh.lods[ level ];
        sg_ReleaseMaps( lod );
        char const * const kinds[5] = { "normal", "basecolor", "ao", "roughness", "mask" };
        gImage image[5];
        char name[96];
        long stamp = 0;
        for ( int k = 0; k < 5; ++k )
        {
            std::snprintf( name, sizeof( name ), "textures/rcl/lightcycle_lod%d_%s.png", level, kinds[k] );
            stamp = stamp * 31 + sg_Stamp( name );
            if ( sg_ReadPng( name, image[k] ) && image[k].depth != 8 )
                image[k] = gImage();
        }
        lod.mapStamp = stamp;
        gImage const & normal = image[0], & base = image[1], & ao = image[2], & rough = image[3], & mask = image[4];

        if ( normal.channels >= 3 )
        {
            std::vector< unsigned char > rgb( size_t( normal.width ) * normal.height * 3 );
            for ( int y = 0; y < normal.height; ++y )
                for ( int x = 0; x < normal.width; ++x )
                    for ( int c = 0; c < 3; ++c )
                        rgb[ ( size_t( y ) * normal.width + x ) * 3 + c ] = normal.At( x, y, c );
            lod.normal = sg_Upload2D( normal.width, normal.height, 3, rgb );
            lod.hasNormal = true;
        }

        // base colour, and in its alpha the emissive share
        gImage const & first = base.width ? base : mask;
        bool const baseOk = base.width > 0 && base.channels >= 3;
        bool const maskOk = mask.width > 0 && mask.channels >= 3 && mask.width == first.width && mask.height == first.height;
        if ( baseOk || maskOk )
        {
            std::vector< unsigned char > rgba( size_t( first.width ) * first.height * 4 );
            for ( int y = 0; y < first.height; ++y )
                for ( int x = 0; x < first.width; ++x )
                {
                    unsigned char * out = &rgba[ ( size_t( y ) * first.width + x ) * 4 ];
                    for ( int c = 0; c < 3; ++c )
                        out[c] = baseOk ? base.At( x, y, c ) : 255;
                    out[3] = maskOk ? mask.At( x, y, 0 ) : 0;
                }
            lod.base = sg_Upload2D( first.width, first.height, 4, rgba );
            lod.hasBase = baseOk;
            lod.hasEmit = maskOk;
        }

        // occlusion, roughness, metal, clear gloss: one texture, where
        // roughness and the mask are both there
        if ( rough.width > 0 && mask.width == rough.width && mask.height == rough.height && mask.channels >= 3 )
        {
            bool const aoOk = ao.width == rough.width && ao.height == rough.height;
            std::vector< unsigned char > orm( size_t( rough.width ) * rough.height * 4 );
            for ( int y = 0; y < rough.height; ++y )
                for ( int x = 0; x < rough.width; ++x )
                {
                    unsigned char * out = &orm[ ( size_t( y ) * rough.width + x ) * 4 ];
                    out[0] = aoOk ? ao.At( x, y, 0 ) : 255;
                    out[1] = rough.At( x, y, 0 );
                    out[2] = mask.At( x, y, 1 );
                    out[3] = mask.At( x, y, 2 );
                }
            lod.orm = sg_Upload2D( rough.width, rough.height, 4, orm );
            lod.hasOrm = true;
        }
        while ( glGetError() != GL_NO_ERROR )
            ;
    }

    // ---- the arena's light ----

    char const * const sg_lookNames[2] = { "pale", "night" };

    // a cubemap level from a strip of six GL faces side by side
    bool sg_UploadCubeLevel( char const * name, GLenum target, int level, int & size )
    {
        int width = 0, height = 0;
        std::vector< float > rgb;
        if ( !sg_ReadHdr( name, width, height, rgb ) || width != height * 6 || ( size && height != size ) )
            return false;
        size = height;
        std::vector< float > face( size_t( size ) * size * 3 );
        for ( int f = 0; f < 6; ++f )
        {
            for ( int y = 0; y < size; ++y )
                std::memcpy( &face[ size_t( y ) * size * 3 ], &rgb[ ( size_t( y ) * width + f * size ) * 3 ],
                             size_t( size ) * 3 * sizeof( float ) );
            glTexImage2D( target + f, level, RGL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, &face[0] );
        }
        return true;
    }

    void sg_LoadEnv( int look )
    {
        gEnv & env = sg_mesh.env[ look ];
        sg_ReleaseEnv( env );
        env.tried = true;
        char name[96];
        std::snprintf( name, sizeof( name ), "textures/rcl/env_%s_spec_m0.hdr", sg_lookNames[ look ] );
        char irradianceName[96];
        std::snprintf( irradianceName, sizeof( irradianceName ), "textures/rcl/env_%s_irradiance.hdr", sg_lookNames[ look ] );
        env.stamp = sg_Stamp( name ) * 31 + sg_Stamp( irradianceName );
        if ( !rGLExt::FloatTextures() )
            return;

        GLint previous = 0;
        glGetIntegerv( 0x8514 /* GL_TEXTURE_BINDING_CUBE_MAP */, &previous );
        while ( glGetError() != GL_NO_ERROR )
            ;

        glGenTextures( 1, &env.spec );
        glBindTexture( RGL_TEXTURE_CUBE_MAP, env.spec );
        int levels = 0, size = 0;
        for ( int level = 0; level < 8; ++level )
        {
            std::snprintf( name, sizeof( name ), "textures/rcl/env_%s_spec_m%d.hdr", sg_lookNames[ look ], level );
            int wanted = level ? size >> level : 0;
            if ( level && wanted < 1 )
                break;
            int got = wanted;
            if ( !sg_UploadCubeLevel( name, RGL_TEXTURE_CUBE_MAP_POSITIVE_X, level, got ) )
                break;
            if ( !level )
                size = got;
            levels = level + 1;
        }
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, 0x8072 /* GL_TEXTURE_WRAP_R */, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, RGL_TEXTURE_BASE_LEVEL, 0 );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, RGL_TEXTURE_MAX_LEVEL, levels > 0 ? levels - 1 : 0 );

        glGenTextures( 1, &env.irradiance );
        glBindTexture( RGL_TEXTURE_CUBE_MAP, env.irradiance );
        int irradianceSize = 0;
        bool const irradiance = sg_UploadCubeLevel( irradianceName, RGL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, irradianceSize );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, 0x8072, RGL_CLAMP_TO_EDGE );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, RGL_TEXTURE_BASE_LEVEL, 0 );
        glTexParameteri( RGL_TEXTURE_CUBE_MAP, RGL_TEXTURE_MAX_LEVEL, 0 );
        glBindTexture( RGL_TEXTURE_CUBE_MAP, static_cast< GLuint >( previous ) );

        // both or neither: half a look's light is worse than the sky worked out
        if ( levels < 1 || !irradiance || glGetError() != GL_NO_ERROR )
        {
            sg_ReleaseEnv( env );
            env.tried = true;
            return;
        }
        env.levels = levels;
    }

    void sg_LoadBrdf()
    {
        sg_DeleteTexture( sg_mesh.brdf );
        sg_mesh.brdfTried = true;
        sg_mesh.brdfStamp = sg_Stamp( "textures/rcl/brdf_lut_ggx.png" );
        gImage image;
        if ( !sg_ReadPng( "textures/rcl/brdf_lut_ggx.png", image ) || image.channels < 2 )
            return;
        // the table's rows are roughness from 0 up and go to GL as they are
        std::vector< unsigned short > rg( size_t( image.width ) * image.height * 3, 0 );
        for ( int y = 0; y < image.height; ++y )
            for ( int x = 0; x < image.width; ++x )
                for ( int c = 0; c < 2; ++c )
                {
                    size_t const at = ( size_t( y ) * image.width + x ) * image.channels + c;
                    rg[ ( size_t( y ) * image.width + x ) * 3 + c ] = image.depth == 16
                        ? reinterpret_cast< unsigned short const * >( &image.data[0] )[ at ]
                        : static_cast< unsigned short >( image.data[ at ] * 257 );
                }
        GLint previous = 0, alignment = 4;
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &previous );
        glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 2 );
        glGenTextures( 1, &sg_mesh.brdf );
        glBindTexture( GL_TEXTURE_2D, sg_mesh.brdf );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, RGL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, RGL_CLAMP_TO_EDGE );
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB16, image.width, image.height, 0, GL_RGB, GL_UNSIGNED_SHORT, &rg[0] );
        glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
        glBindTexture( GL_TEXTURE_2D, static_cast< GLuint >( previous ) );
        if ( glGetError() != GL_NO_ERROR )
            sg_DeleteTexture( sg_mesh.brdf );
    }

    // ---- making it all ----

    // RCL_SHADER_RELOAD: every two seconds, what changed on disk (or came)
    // is read again
    void sg_ReloadChanged()
    {
        double const now = tRealSysTimeFloat();
        if ( now - sg_mesh.checkedAt < 2 )
            return;
        sg_mesh.checkedAt = now;

        if ( sg_shader.FilesChanged() )
        {
            std::string error;
            if ( sg_shader.Load( error ) )
            {
                ++sg_shaderLinks;
                con << "render: read cycle_pbr again\n";
            }
            else
                con << "render: " << error << "\n";
        }
        if ( sg_Stamp( "models/rcl/lightcycle.rclmesh" ) != sg_mesh.stamp )
            sg_ReleaseMesh();
        for ( size_t l = 0; l < sg_mesh.lods.size(); ++l )
        {
            long stamp = 0;
            char name[96];
            char const * const kinds[5] = { "normal", "basecolor", "ao", "roughness", "mask" };
            for ( int k = 0; k < 5; ++k )
            {
                std::snprintf( name, sizeof( name ), "textures/rcl/lightcycle_lod%d_%s.png", int( l ), kinds[k] );
                stamp = stamp * 31 + sg_Stamp( name );
            }
            if ( stamp != sg_mesh.lods[l].mapStamp )
                sg_LoadMaps( static_cast< int >( l ) );
        }
        for ( int look = 0; look < 2; ++look )
        {
            gEnv & env = sg_mesh.env[ look ];
            char name[96], irradianceName[96];
            std::snprintf( name, sizeof( name ), "textures/rcl/env_%s_spec_m0.hdr", sg_lookNames[ look ] );
            std::snprintf( irradianceName, sizeof( irradianceName ), "textures/rcl/env_%s_irradiance.hdr", sg_lookNames[ look ] );
            if ( env.tried && sg_Stamp( name ) * 31 + sg_Stamp( irradianceName ) != env.stamp )
                env.tried = false;
        }
        if ( sg_mesh.brdfTried && sg_Stamp( "textures/rcl/brdf_lut_ggx.png" ) != sg_mesh.brdfStamp )
            sg_mesh.brdfTried = false;
    }

    // whether the mesh can be drawn; makes what is needed
    bool sg_Ready( int look )
    {
        if ( sg_mesh.failed || !rGLExt::Shaders() )
            return false;
        if ( rPost::ReloadWanted() )
            sg_ReloadChanged();

        if ( !sg_shaderTried )
        {
            sg_shaderTried = true;
            std::string error;
            ++sg_shaderLinks;
            if ( !sg_shader.Load( error ) )
            {
                sg_mesh.failed = true;
                con << "render: the light cycle mesh is off for this session, the light cycle draws instead: "
                    << error << "\n";
                return false;
            }
        }
        if ( !sg_shader.Ready() )
            return false;

        if ( !sg_mesh.tried )
        {
            sg_mesh.tried = true;
            std::string why;
            if ( !sg_LoadMesh( why ) )
            {
                con << "render: " << why << "; RCL_CYCLE_MODEL 2 draws the light cycle\n";
                return false;
            }
            for ( size_t l = 0; l < sg_mesh.lods.size(); ++l )
                sg_LoadMaps( static_cast< int >( l ) );
        }
        if ( sg_mesh.lods.empty() )
            return false;

        if ( !sg_mesh.env[ look ].tried )
            sg_LoadEnv( look );
        if ( !sg_mesh.brdfTried )
            sg_LoadBrdf();
        return true;
    }

    // a colour of the game (display values) as light
    void sg_Light( REAL const * display, float scale, float * out )
    {
        for ( int i = 0; i < 3; ++i )
            out[i] = static_cast< float >( std::pow( std::max( 0.0, double( display[i] ) ), 2.2 ) ) * scale;
    }

    void sg_Bind( int unit, GLenum target, GLuint texture )
    {
        glActiveTexture( RGL_TEXTURE0 + unit );
        glBindTexture( target, texture );
    }
#endif // RGL_EXT_AVAILABLE
}

void sg_CycleMeshPose( gCycleMeshPose & pose, eCoord const & pos, eCoord const & dir,
                       REAL leanCos, REAL leanSin, REAL distance )
{
    // as gCycle::Render and sg_CleanCycleFrame place a cycle: at its place,
    // half size, turned its way, back by 1.5 to where its wheels are, leaning
    float place[16], turn[16], back[16], lean[16];
    Translation( place, pos.x, pos.y, 0 );
    for ( int i = 0; i < 3; ++i )
        place[ i * 5 ] = .5f;
    Identity( turn );
    turn[0] = dir.x;
    turn[1] = dir.y;
    turn[4] = -dir.y;
    turn[5] = dir.x;
    Translation( back, -1.5f, 0, 0 );
    Identity( lean );
    lean[5] = leanCos;
    lean[6] = leanSin;
    lean[9] = -leanSin;
    lean[10] = leanCos;
    Multiply( place, turn, pose.frame );
    Multiply( pose.frame, back, pose.frame );
    Multiply( pose.frame, lean, pose.frame );
    pose.distance = distance;
}

bool sg_RenderCycleMesh( gCycleMeshPose const & pose, REAL r, REAL g, REAL b )
{
#ifndef RGL_EXT_AVAILABLE
    (void)pose; (void)r; (void)g; (void)b;
    return false;
#else
    if ( !rPost::InScene() || rDisplayList::IsRecording() )
        return false;
    int const look = sr_cleanDark ? 1 : 0;
    if ( !sg_Ready( look ) )
        return false;

    // How long it is on the screen, from what GL has been given to draw it
    // with. Asking GL makes the driver's own thread catch up; the projection
    // and the viewport stay for a scene, so they are asked once a scene.
    static unsigned scene = 0;
    static GLfloat projection[16];
    static GLint viewport[4];
    if ( scene != rPost::SceneSerial() )
    {
        scene = rPost::SceneSerial();
        glGetFloatv( GL_PROJECTION_MATRIX, projection );
        glGetIntegerv( GL_VIEWPORT, viewport );
    }
    GLfloat view[16];
    glGetFloatv( GL_MODELVIEW_MATRIX, view );
    float const depth = -( view[2] * sg_middle[0] + view[6] * sg_middle[1] + view[10] * sg_middle[2] + view[14] );
    float const scale = std::sqrt( view[0] * view[0] + view[1] * view[1] + view[2] * view[2] );
    float const pixels = depth > 1E-3f ? sg_length * scale * projection[5] * viewport[3] * .5f / depth : 1E6f;
    if ( !( pixels >= sg_cycleFar ) )
        return false;
    int level = pixels < sg_cycleLod2 ? 2 : ( pixels < sg_cycleLod1 ? 1 : 0 );
    if ( level >= int( sg_mesh.lods.size() ) )
        level = int( sg_mesh.lods.size() ) - 1;
    gLod const & lod = sg_mesh.lods[ level ];

    // The model in the light cycle's frame: scaled to its length, its nose at
    // the light cycle's nose, so its tail is where the wall leaves.
    float const fit = sg_length / ( sg_mesh.hi[0] - sg_mesh.lo[0] );
    float place[16], size[16], to[16], local[16];
    Translation( place, sg_nose, 0, 0 );
    Identity( size );
    size[0] = size[5] = size[10] = fit;
    Translation( to, -sg_mesh.hi[0], 0, 0 );
    Multiply( place, size, local );
    Multiply( local, to, local );

    // where the eye is, in the arena: the modelview takes the frame to the
    // eye, the pose the frame to the arena (in the floor's mirror image the
    // eye comes out under the floor, as it should for what it sees there)
    float inverse[16], eyeFrame[3], eye[3] = { 0, 0, 0 };
    float const origin[3] = { 0, 0, 0 };
    if ( Invert( view, inverse ) )
    {
        Apply( inverse, origin, 1, eyeFrame );
        Apply( pose.frame, eyeFrame, 1, eye );
    }

    // the sun, as the arena's shadows have it; the cubemaps were made with
    // the sun at (-.8, .6) across, so they turn with it
    eCoord toSun;
    REAL reach;
    se_CleanArenaSun( toSun, reach );
    float const norm = 1 / std::sqrt( reach * reach + 1 );
    float const sun[3] = { float( toSun.x * reach * norm ), float( toSun.y * reach * norm ), norm };
    float const turn = std::atan2( .6f, -.8f ) - std::atan2( float( toSun.y ), float( toSun.x ) );

    gEnv const & env = sg_mesh.env[ look ];
    bool const cubes = env.levels > 0 && sg_cycleEnv > 0;
    bool const maps = sg_cycleMaps > 0;

    // the bike's colour as light, as the light cycle's lights burn
    float glow[3], light[3];
    sg_LightCycleLightColour( r, g, b, glow );
    REAL const intensity = sg_LightCycleGlow() > 1 ? sg_LightCycleGlow() : 1;
    for ( int i = 0; i < 3; ++i )
        light[i] = static_cast< float >( std::pow( double( glow[i] ), 2.2 ) * intensity );

    // the sky and floor where there are no cubemaps; the sun or moon
    static REAL const palePart[3][3] = { { .651f, .757f, .824f }, { .831f, .827f, .835f }, { 1, .969f, .910f } };
    static REAL const nightPart[3][3] = { { .016f, .020f, .031f }, { .157f, .180f, .227f }, { .80f, .86f, .96f } };
    REAL const ( *part )[3] = look ? nightPart : palePart;
    float sky[3], horizon[3], ground[3], sunColour[3];
    sg_Light( part[0], 1, sky );
    sg_Light( part[1], 1, horizon );
    sg_Light( sr_CleanFloorColor(), 1, ground );
    sg_Light( part[2], look ? 1.2f : 3.0f, sunColour );
    float const sunShare = cubes ? float( sg_cycleSun ) : 1.f + float( sg_cycleSun );

    // Only what is changed is saved, once a cycle: the texture units are
    // unbound at the end instead (what draws next binds its own).
    RenderEnd();
    rGPUTimer::Begin( rGPUTimer::Pass_Cycles );
    glPushAttrib( GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_CULL_FACE );
    glDisable( GL_BLEND );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glDepthMask( GL_TRUE );
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    if ( rGLExt::SeamlessCube() )
        glEnable( RGL_TEXTURE_CUBE_MAP_SEAMLESS );

    sg_shader.Use();

    // What holds for the whole scene goes to the program once a scene; it
    // keeps it (until it is linked anew).
    int const sceneKey[5] = { int( rPost::SceneSerial() ), look, cubes ? env.levels : 0, sg_mesh.brdf ? 1 : 0,
                              sg_shaderLinks };
    if ( std::memcmp( sceneKey, sg_sceneKey, sizeof( sceneKey ) ) != 0 )
    {
        std::memcpy( sg_sceneKey, sceneKey, sizeof( sceneKey ) );
        sg_shader.SetSampler( "u_normalMap", 0 );
        sg_shader.SetSampler( "u_baseMap", 1 );
        sg_shader.SetSampler( "u_ormMap", 2 );
        sg_shader.SetSampler( "u_env", 3 );
        sg_shader.SetSampler( "u_irradiance", 4 );
        sg_shader.SetSampler( "u_brdf", 5 );
        sg_shader.Set( "u_envMaps", cubes ? 1.f : 0.f );
        sg_shader.Set( "u_envLods", 5.f );
        sg_shader.Set( "u_envTop", float( env.levels > 0 ? env.levels - 1 : 0 ) );
        sg_shader.Set( "u_brdfMap", sg_mesh.brdf ? 1.f : 0.f );
        sg_shader.Set( "u_envTurn", std::cos( turn ), std::sin( turn ) );
        sg_shader.Set( "u_sun", sun[0], sun[1], sun[2] );
        sg_shader.Set( "u_sunLight", sunColour[0] * sunShare, sunColour[1] * sunShare, sunColour[2] * sunShare );
        sg_shader.Set( "u_sky", sky[0], sky[1], sky[2] );
        sg_shader.Set( "u_horizon", horizon[0], horizon[1], horizon[2] );
        sg_shader.Set( "u_ground", ground[0], ground[1], ground[2] );
    }
    sg_shader.Set( "u_maps", maps && lod.hasNormal ? 1.f : 0.f, maps && lod.hasBase ? 1.f : 0.f,
                   maps && lod.hasOrm ? 1.f : 0.f, maps && lod.hasEmit ? 1.f : 0.f );
    sg_shader.Set( "u_eye", eye[0], eye[1], eye[2] );
    sg_shader.Set( "u_light", light[0], light[1], light[2] );

    sg_Bind( 5, GL_TEXTURE_2D, sg_mesh.brdf );
    sg_Bind( 4, RGL_TEXTURE_CUBE_MAP, cubes ? env.irradiance : 0 );
    sg_Bind( 3, RGL_TEXTURE_CUBE_MAP, cubes ? env.spec : 0 );
    sg_Bind( 2, GL_TEXTURE_2D, lod.orm );
    sg_Bind( 1, GL_TEXTURE_2D, lod.base );
    sg_Bind( 0, GL_TEXTURE_2D, lod.normal );

    // position, normal, uv on unit 0, tangent with its sign on unit 1
    glBindBuffer( RGL_ARRAY_BUFFER, lod.vertices );
    glBindBuffer( RGL_ELEMENT_ARRAY_BUFFER, lod.indices );
    GLsizei const stride = 12 * sizeof( float );
    char const * const none = 0;
    glDisableClientState( GL_COLOR_ARRAY );
    glDisableClientState( GL_INDEX_ARRAY );
    glDisableClientState( GL_EDGE_FLAG_ARRAY );
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_NORMAL_ARRAY );
    glVertexPointer( 3, GL_FLOAT, stride, none );
    glNormalPointer( GL_FLOAT, stride, none + 3 * sizeof( float ) );
    glClientActiveTexture( RGL_TEXTURE1 );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );
    glTexCoordPointer( 4, GL_FLOAT, stride, none + 6 * sizeof( float ) );
    glClientActiveTexture( RGL_TEXTURE0 );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );
    glTexCoordPointer( 2, GL_FLOAT, stride, none + 10 * sizeof( float ) );

    ModelMatrix();
    for ( int object = 0; object < sg_objects; ++object )
    {
        float part[16];
        std::memcpy( part, local, sizeof( part ) );
        if ( object > 0 )
        {
            // a wheel turns about its axle by the way come over its radius
            // (in the frame, twice the arena's size), as the classic wheels do
            float const * const pivot = sg_mesh.pivot[ object ];
            float const angle = static_cast< float >( 2 * pose.distance / ( pivot[3] * fit ) );
            float axle[16], spin[16];
            Translation( axle, pivot[0], pivot[1], pivot[2] );
            Identity( spin );
            spin[0] = std::cos( angle );
            spin[2] = std::sin( angle );
            spin[8] = -spin[2];
            spin[10] = spin[0];
            Multiply( part, axle, part );
            Multiply( part, spin, part );
        }
        float model[16];
        Multiply( pose.frame, part, model );
        sg_shader.SetMatrix4( "u_model", model );

        glPushMatrix();
        glMultMatrixf( part );
        for ( size_t i = 0; i < lod.ranges.size(); ++i )
        {
            gRange const & range = lod.ranges[i];
            if ( range.object != object )
                continue;
            gMaterial const & m = sg_material[ range.material ];
            sg_shader.Set( "u_base", m.base[0], m.base[1], m.base[2] );
            sg_shader.Set( "u_surface", m.rough, m.metal, m.gloss, m.emit );
            glDrawElements( GL_TRIANGLES, static_cast< GLsizei >( range.count ), GL_UNSIGNED_INT,
                            none + range.first * sizeof( unsigned ) );
        }
        glPopMatrix();
    }

    glBindBuffer( RGL_ARRAY_BUFFER, 0 );
    glBindBuffer( RGL_ELEMENT_ARRAY_BUFFER, 0 );
    for ( int unit = 5; unit >= 0; --unit )
        sg_Bind( unit, ( unit == 3 || unit == 4 ) ? RGL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 0 );
    rShader::UseNone();
    if ( rGLExt::SeamlessCube() )
        glDisable( RGL_TEXTURE_CUBE_MAP_SEAMLESS );
    glPopClientAttrib();
    glClientActiveTexture( RGL_TEXTURE0 );
    glPopAttrib();
    rGPUTimer::End( rGPUTimer::Pass_Cycles );
    return true;
#endif
}

#endif // DEDICATED
