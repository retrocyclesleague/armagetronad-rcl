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

#include "gLightCycle.h"

// DEDICATED comes from config.h
#include "defs.h"

#ifndef DEDICATED

#include "eCoord.h"
#include "eFloor.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// The clean arena's light cycle: the classic cycle's shape, cut in the clean
// arena's way. A long, low body of flat facets with straight edges on two
// wheels, the big one behind, both where the classic cycle has them, all in
// dark graphite, and a few things in the bike's colour that light up: the
// rims of the wheels, a line along each tread, a chevron on each side, a
// spine along the top and the tail. Every face is lit flat by the arena's sun
// or moon, the way the walls and the arrow are.
//
// The shape is built once, about six hundred triangles; only its colours are
// worked out for every cycle drawn. Far away, where the cycle is a few pixels
// long, the body takes on the bike's colour, so a cycle seen from high above
// still reads as its colour.

namespace
{
    float const sg_pi = 3.14159265f;

    // what a vertex belongs to; Shade() says how each is coloured
    enum Part
    {
        Part_Body,      // dark graphite with a little of the bike's colour in it
        Part_Canopy,    // the rider's canopy: darker still
        Part_Tyre,      // the wheels' treads and outer faces
        Part_Light,     // in the bike's colour: the rims, the tread lines, the chevrons, the spine
        Part_Ember      // the same, dimmer: the tail
    };

    struct Point
    {
        float x, y, z;
    };

    Point P( float x, float y, float z )
    {
        Point p = { x, y, z };
        return p;
    }

    Point operator+( Point a, Point b ) { return P( a.x + b.x, a.y + b.y, a.z + b.z ); }
    Point operator-( Point a, Point b ) { return P( a.x - b.x, a.y - b.y, a.z - b.z ); }
    Point operator*( Point a, float f ) { return P( a.x * f, a.y * f, a.z * f ); }
    float Dot( Point a, Point b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Point Cross( Point a, Point b ) { return P( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x ); }

    Point Unit( Point a )
    {
        float const length = sqrtf( Dot( a, a ) );
        return length > 1E-6f ? a * ( 1 / length ) : P( 0, 0, 1 );
    }

    struct ShapeVertex
    {
        Point at, normal;
        int part;

        bool operator<( ShapeVertex const & other ) const
        {
            float const a[7] = { at.x, at.y, at.z, normal.x, normal.y, normal.z, float( part ) };
            float const b[7] = { other.at.x, other.at.y, other.at.z, other.normal.x, other.normal.y, other.normal.z,
                                 float( other.part ) };
            return std::lexicographical_compare( a, a + 7, b, b + 7 );
        }
    };

    // The shape: each vertex once, the triangles as indices into them. The
    // indices from decals on are the lights that lie on the body.
    struct Mesh
    {
        bool built;
        std::vector< ShapeVertex > vertices;
        std::vector< GLfloat > places;      // three to a vertex
        std::vector< GLfloat > colours;     // four to a vertex, anew for every cycle drawn
        std::vector< GLushort > indices;
        size_t decals;
        std::map< ShapeVertex, GLushort > known; // while it is built

        Mesh(): built( false ), decals( 0 ){}
    };

    Mesh sg_mesh;

    GLushort Index( Point const & at, Point const & normal, int part )
    {
        ShapeVertex const vertex = { at, normal, part };
        std::map< ShapeVertex, GLushort >::const_iterator const found = sg_mesh.known.find( vertex );
        if ( found != sg_mesh.known.end() )
            return found->second;
        GLushort const index = static_cast< GLushort >( sg_mesh.vertices.size() );
        sg_mesh.vertices.push_back( vertex );
        sg_mesh.known[ vertex ] = index;
        return index;
    }

    void Triangle( Point a, Point b, Point c, Point normal, int part )
    {
        GLushort const corners[3] = { Index( a, normal, part ), Index( b, normal, part ), Index( c, normal, part ) };
        sg_mesh.indices.insert( sg_mesh.indices.end(), corners, corners + 3 );
    }

    // A flat four-cornered face, one normal for all of it: facets stay
    // facets. Which way round the corners go does not matter, nothing of the
    // cycle is culled (the floor's mirror image turns it inside out).
    void Quad( Point a, Point b, Point c, Point d, Point normal, int part )
    {
        GLushort const ia = Index( a, normal, part ), ib = Index( b, normal, part );
        GLushort const ic = Index( c, normal, part ), id = Index( d, normal, part );
        GLushort const corners[6] = { ia, ib, ic, ia, ic, id };
        sg_mesh.indices.insert( sg_mesh.indices.end(), corners, corners + 6 );
    }

    // the same, with its normal worked out from the corners, facing away from inside
    void Facet( Point a, Point b, Point c, Point d, Point inside, int part )
    {
        Point normal = Unit( Cross( c - a, d - b ) );
        if ( Dot( normal, a - inside ) < 0 )
            normal = normal * -1;
        Quad( a, b, c, d, normal, part );
    }

    // ---- the body ----

    // Its outline seen from the side, in the cycle's frame (x ahead, z up),
    // round from the nose over the top and back along the belly. It follows
    // the classic body's, lower and longer: over the big rear wheel at the
    // back, over the small front one at the nose. The outline is convex.
    struct Corner
    {
        float x, z;
    };

    Corner const sg_outline[] =
    {
        {  2.32f,  .66f },  // 0 the nose
        {  2.10f,  .92f },
        {  1.60f, 1.10f },  // 2 the canopy
        {   .98f, 1.26f },
        {   .20f, 1.30f },  // 4 the top
        {  -.42f, 1.18f },
        {  -.66f,  .98f },  // 6 the tail
        {  -.70f,  .78f },
        {  -.20f,  .40f },  // 8 down through the rear wheel
        {   .70f,  .30f },
        {  1.45f,  .30f },  // 10 the belly
        {  2.05f,  .48f }
    };
    int const sg_corners = sizeof( sg_outline ) / sizeof( sg_outline[0] );

    // the edges along the outline, from corner i to the next, that are something of their own
    int const sg_canopyFirst = 1, sg_canopyLast = 2;    // the canopy, darker
    int const sg_spineFirst = 3, sg_spineLast = 5;      // a line of light along the top
    int const sg_tailEdge = 6;                          // the back of the tail, which glows

    // Each side is two flat faces meeting in a crease along the body, like a
    // hull: the upper one leans in towards the top, the lower one towards the
    // belly, so one catches the light and the other does not. The chevron's
    // point sits on the crease.
    float const sg_crease = .70f;       // the crease's height
    float const sg_creaseHalf = .30f;   // how far out it stands
    float const sg_sideHalf = .18f;     // how far out the sides are where they lean in the most
    float const sg_bandHalf = .11f;     // the band round the outline, across the top and the belly
    float const sg_bevel = .08f;        // how far in from the outline the sides begin
    float const sg_lift = .008f;        // how far the lights on the body stand off it

    Point const sg_inside = { .80f, 0, .80f };     // a point inside the body, to turn its normals out by

    // The outline and the sides' outline (the outline moved in by the
    // bevel), each with a corner of its own where the crease meets it, and
    // for each corner the outline's edge it begins.
    std::vector< Corner > sg_out, sg_in;
    std::vector< int > sg_edgeOf;
    float sg_lean = 0;                  // how far in the sides come for each unit above or below the crease

    // how far out a side is at height z
    float SideHalf( float z )
    {
        return sg_creaseHalf - sg_lean * fabsf( z - sg_crease );
    }

    // the outline's edge from corner i on: its direction, and outwards across it
    void Edge( int i, float & ex, float & ez, float & ox, float & oz )
    {
        Corner const & a = sg_outline[i];
        Corner const & b = sg_outline[ ( i + 1 ) % sg_corners ];
        float const length = sqrtf( ( b.x - a.x ) * ( b.x - a.x ) + ( b.z - a.z ) * ( b.z - a.z ) );
        ex = ( b.x - a.x ) / length;
        ez = ( b.z - a.z ) / length;
        // the outline runs round anticlockwise, so outwards is to the right of it
        ox = ez;
        oz = -ex;
    }

    void MakeOutlines()
    {
        // where the edges either side of each corner meet, each moved in by the bevel
        std::vector< Corner > inset;
        for ( int i = 0; i < sg_corners; ++i )
        {
            int const before = ( i + sg_corners - 1 ) % sg_corners;
            float e1x, e1z, o1x, o1z, e2x, e2z, o2x, o2z;
            Edge( before, e1x, e1z, o1x, o1z );
            Edge( i, e2x, e2z, o2x, o2z );
            Corner const & c = sg_outline[i];
            float const p1x = c.x - o1x * sg_bevel, p1z = c.z - o1z * sg_bevel;
            float const p2x = c.x - o2x * sg_bevel, p2z = c.z - o2z * sg_bevel;
            float const det = e1x * ( -e2z ) - e1z * ( -e2x );
            float const t = fabsf( det ) > 1E-6f ? ( ( p2x - p1x ) * ( -e2z ) - ( p2z - p1z ) * ( -e2x ) ) / det : 0;
            Corner const in = { p1x + e1x * t, p1z + e1z * t };
            inset.push_back( in );
        }

        // the corners, and where an edge of the sides' outline crosses the crease, one more
        sg_out.clear();
        sg_in.clear();
        sg_edgeOf.clear();
        float reach = 0;
        for ( int i = 0; i < sg_corners; ++i )
        {
            int const j = ( i + 1 ) % sg_corners;
            reach = std::max( reach, fabsf( inset[i].z - sg_crease ) );
            sg_out.push_back( sg_outline[i] );
            sg_in.push_back( inset[i] );
            sg_edgeOf.push_back( i );

            float const a = inset[i].z - sg_crease, b = inset[j].z - sg_crease;
            if ( ( a < 0 ) != ( b < 0 ) && fabsf( a - b ) > 1E-6f )
            {
                float const t = a / ( a - b );
                Corner const in = { inset[i].x + ( inset[j].x - inset[i].x ) * t, sg_crease };
                Corner const out = { sg_outline[i].x + ( sg_outline[j].x - sg_outline[i].x ) * t,
                                     sg_outline[i].z + ( sg_outline[j].z - sg_outline[i].z ) * t };
                sg_out.push_back( out );
                sg_in.push_back( in );
                sg_edgeOf.push_back( i );
            }
        }
        sg_lean = reach > 0 ? ( sg_creaseHalf - sg_sideHalf ) / reach : 0;
    }

    Point Out( int k, float y ) { return P( sg_out[k].x, y, sg_out[k].z ); }
    Point In( int k, int side ) { return P( sg_in[k].x, side * SideHalf( sg_in[k].z ), sg_in[k].z ); }

    void BuildBody()
    {
        MakeOutlines();
        int const n = static_cast< int >( sg_out.size() );
        for ( int k = 0; k < n; ++k )
        {
            int const next = ( k + 1 ) % n;
            int const edge = sg_edgeOf[k];
            float ex, ez, ox, oz;
            Edge( edge, ex, ez, ox, oz );
            bool const canopy = edge >= sg_canopyFirst && edge <= sg_canopyLast;

            // the band across, from one bevel to the other
            int const band = canopy ? Part_Canopy : edge == sg_tailEdge ? Part_Ember : Part_Body;
            Quad( Out( k, sg_bandHalf ), Out( next, sg_bandHalf ), Out( next, -sg_bandHalf ), Out( k, -sg_bandHalf ),
                  P( ox, 0, oz ), band );

            // the bevels, from the band down to each side
            for ( int side = -1; side <= 1; side += 2 )
                Facet( Out( k, side * sg_bandHalf ), Out( next, side * sg_bandHalf ), In( next, side ), In( k, side ),
                       sg_inside, canopy ? Part_Canopy : Part_Body );
        }

        // The sides, each two flat faces: above the crease and below it. The
        // corners on the crease belong to both; the outline is convex, so the
        // corners of each, in the outline's order, go round it.
        for ( int side = -1; side <= 1; side += 2 )
            for ( int upper = 0; upper < 2; ++upper )
            {
                std::vector< int > corners;
                Point centre = P( 0, 0, 0 );
                for ( int k = 0; k < n; ++k )
                {
                    float const z = sg_in[k].z;
                    if ( upper ? z >= sg_crease : z <= sg_crease )
                    {
                        corners.push_back( k );
                        centre = centre + In( k, side );
                    }
                }
                if ( corners.size() < 3 )
                    continue;
                centre = centre * ( 1.0f / corners.size() );
                Point const normal = Unit( P( 0, float( side ), upper ? sg_lean : -sg_lean ) );
                for ( size_t m = 0; m < corners.size(); ++m )
                    Triangle( centre, In( corners[m], side ), In( corners[ ( m + 1 ) % corners.size() ], side ), normal,
                              Part_Body );
            }
    }

    // ---- the wheels ----

    Point Rim( float x, float z, float radius, float y, float angle )
    {
        return P( x + radius * cosf( angle ), y, z + radius * sinf( angle ) );
    }

    // A wheel round an axis across the cycle, at x and z, with flat facets
    // round its tread, one corner standing on the floor. A line of light runs
    // along the tread; each face is dark with a ring of light, its rim, where
    // the tyre meets the wheel.
    void BuildWheel( float x, float z, float radius, float halfWidth, int segments )
    {
        float const rimIn = radius * .64f, rimOut = radius * .76f;
        for ( int k = 0; k < segments; ++k )
        {
            float const a0 = -sg_pi * .5f + k * ( 2 * sg_pi / segments );
            float const a1 = a0 + 2 * sg_pi / segments;
            float const middle = ( a0 + a1 ) * .5f;
            Point const out = P( cosf( middle ), 0, sinf( middle ) );

            // the tread: tyre, the line of light down its middle, tyre
            static const float across[4] = { -1, -.22f, .22f, 1 };
            for ( int band = 0; band < 3; ++band )
            {
                float const y0 = across[ band ] * halfWidth, y1 = across[ band + 1 ] * halfWidth;
                Quad( Rim( x, z, radius, y0, a0 ), Rim( x, z, radius, y1, a0 ), Rim( x, z, radius, y1, a1 ),
                      Rim( x, z, radius, y0, a1 ), out, band == 1 ? Part_Light : Part_Tyre );
            }

            // the faces: the wheel, its rim of light, the tyre
            for ( int side = -1; side <= 1; side += 2 )
            {
                float const y = side * halfWidth;
                Point const face = P( 0, float( side ), 0 );
                Triangle( P( x, y, z ), Rim( x, z, rimIn, y, a0 ), Rim( x, z, rimIn, y, a1 ), face, Part_Body );
                Quad( Rim( x, z, rimIn, y, a0 ), Rim( x, z, rimOut, y, a0 ), Rim( x, z, rimOut, y, a1 ),
                      Rim( x, z, rimIn, y, a1 ), face, Part_Light );
                Quad( Rim( x, z, rimOut, y, a0 ), Rim( x, z, radius, y, a0 ), Rim( x, z, radius, y, a1 ),
                      Rim( x, z, rimOut, y, a1 ), face, Part_Tyre );
            }
        }
    }

    // ---- the lights on the body ----

    // a point on one side (1 the left, -1 the right) at x and z, standing off it a little
    Point OnSide( float x, float z, int side )
    {
        return P( x, side * ( SideHalf( z ) + sg_lift ), z );
    }

    // A bar of light on one side from (x0, z0) towards (x1, z1) on the
    // crease. It lies on the face its start is on and is cut off along the
    // crease (a straight bar across it would sink into the ridge), so two
    // bars meeting there make a clean point, mitred along the crease.
    void Bar( float x0, float z0, float x1, float z1, int side )
    {
        static const float halfWidth = .032f, past = .10f;
        float const length = sqrtf( ( x1 - x0 ) * ( x1 - x0 ) + ( z1 - z0 ) * ( z1 - z0 ) );
        float const ux = ( x1 - x0 ) / length, uz = ( z1 - z0 ) / length;
        x1 += ux * past;
        z1 += uz * past;
        float const ax = -uz * halfWidth, az = ux * halfWidth;
        Corner const corners[4] = { { x0 - ax, z0 - az }, { x0 + ax, z0 + az }, { x1 + ax, z1 + az }, { x1 - ax, z1 - az } };

        bool const upper = z0 > sg_crease;
        std::vector< Corner > kept;
        for ( int i = 0; i < 4; ++i )
        {
            Corner const & a = corners[i];
            Corner const & b = corners[ ( i + 1 ) % 4 ];
            bool const aIn = upper ? a.z >= sg_crease : a.z <= sg_crease;
            bool const bIn = upper ? b.z >= sg_crease : b.z <= sg_crease;
            if ( aIn )
                kept.push_back( a );
            if ( aIn != bIn )
            {
                Corner const cut = { a.x + ( b.x - a.x ) * ( sg_crease - a.z ) / ( b.z - a.z ), sg_crease };
                kept.push_back( cut );
            }
        }
        for ( size_t m = 1; m + 1 < kept.size(); ++m )
            Triangle( OnSide( kept[0].x, kept[0].z, side ), OnSide( kept[m].x, kept[m].z, side ),
                      OnSide( kept[m + 1].x, kept[m + 1].z, side ), P( 0, float( side ), 0 ), Part_Light );
    }

    void BuildLights()
    {
        // the spine: a line down the middle of the band along the top, from
        // the canopy to the tail
        static const float spineHalf = .035f;
        for ( int i = sg_spineFirst; i <= sg_spineLast; ++i )
        {
            int const j = ( i + 1 ) % sg_corners;
            float ex, ez, ox, oz;
            Edge( i, ex, ez, ox, oz );
            Point const lift = P( ox, 0, oz ) * sg_lift;
            Quad( Out( i, spineHalf ) + lift, Out( j, spineHalf ) + lift, Out( j, -spineHalf ) + lift,
                  Out( i, -spineHalf ) + lift, P( ox, 0, oz ), Part_Light );
        }

        // a chevron on each side between the wheels, pointing the way the cycle goes, its point on the crease
        for ( int side = -1; side <= 1; side += 2 )
        {
            Bar( .80f, .96f, 1.20f, sg_crease, side );
            Bar( .80f, .44f, 1.20f, sg_crease, side );
        }
    }

    void Build()
    {
        sg_mesh.built = true;
        BuildBody();
        // the wheels stand out a little past the body's crease on either side
        BuildWheel( -.02f, .64f, .64f, .34f, 16 );  // the big rear wheel, where the classic cycle has its own
        BuildWheel( 1.84f, .44f, .44f, .32f, 16 );  // and the front one
        sg_mesh.decals = sg_mesh.indices.size();
        BuildLights();
        sg_mesh.known.clear();

        sg_mesh.places.reserve( sg_mesh.vertices.size() * 3 );
        for ( size_t i = 0; i < sg_mesh.vertices.size(); ++i )
        {
            sg_mesh.places.push_back( sg_mesh.vertices[i].at.x );
            sg_mesh.places.push_back( sg_mesh.vertices[i].at.y );
            sg_mesh.places.push_back( sg_mesh.vertices[i].at.z );
        }
        sg_mesh.colours.resize( sg_mesh.vertices.size() * 4 );
    }

    // ---- its colours ----

    struct Look
    {
        float base[ Part_Light ][3];        // the lit parts
        float sky;                          // how much of the sky the faces looking up catch
        float light[3], ember[3];           // the lights
        float paint[3];                     // the bike's colour as the arena paints it
    };

    void Set( float * out, float r, float g, float b )
    {
        out[0] = r;
        out[1] = g;
        out[2] = b;
    }

    void Mix( float * out, float const * a, float const * b, float share )
    {
        for ( int i = 0; i < 3; ++i )
            out[i] = a[i] + ( b[i] - a[i] ) * share;
    }

    Look MakeLook( REAL r, REAL g, REAL b )
    {
        Look look;
        bool const dark = sr_cleanDark;

        REAL pr = r, pg = g, pb = b;
        sr_CleanPaint( pr, pg, pb );
        Set( look.paint, pr, pg, pb );

        // The lights are lights in either arena: the colour as it is,
        // brought up where so little of it would show that it could be
        // missed (as the dark arena does with walls). At night they burn a
        // little whiter at the core.
        float glow[3] = { std::max( 0.0f, std::min( 1.0f, float( r ) ) ), std::max( 0.0f, std::min( 1.0f, float( g ) ) ),
                          std::max( 0.0f, std::min( 1.0f, float( b ) ) ) };
        static const float least = .22f;
        float const seen = .299f * glow[0] + .587f * glow[1] + .114f * glow[2];
        if ( seen < least )
            for ( int i = 0; i < 3; ++i )
                glow[i] += ( 1 - glow[i] ) * ( least - seen ) / ( 1 - seen );
        if ( dark )
            for ( int i = 0; i < 3; ++i )
                glow[i] += ( 1 - glow[i] ) * .12f;
        for ( int i = 0; i < 3; ++i )
        {
            look.light[i] = glow[i];
            look.ember[i] = glow[i] * .6f;
        }

        static const float bodyPale[3] = { .27f, .29f, .32f }, bodyDark[3] = { .21f, .23f, .27f };
        static const float canopyPale[3] = { .10f, .11f, .13f }, canopyDark[3] = { .07f, .08f, .10f };
        Mix( look.base[ Part_Body ], dark ? bodyDark : bodyPale, look.paint, .14f );
        Mix( look.base[ Part_Canopy ], dark ? canopyDark : canopyPale, look.paint, .08f );
        if ( dark )
            Set( look.base[ Part_Tyre ], .08f, .08f, .09f );
        else
            Set( look.base[ Part_Tyre ], .12f, .12f, .13f );
        look.sky = dark ? .06f : .08f;
        return look;
    }

    // how many pixels long the cycle is on the screen, from what GL has been given to draw it with
    float Pixels()
    {
        GLfloat m[16], p[16];
        GLint viewport[4];
        glGetFloatv( GL_MODELVIEW_MATRIX, m );
        glGetFloatv( GL_PROJECTION_MATRIX, p );
        glGetIntegerv( GL_VIEWPORT, viewport );

        // the cycle's middle, and how far ahead of the eye it is
        float const depth = -( m[2] * .8f + m[10] * .65f + m[14] );
        float const scale = sqrtf( m[0] * m[0] + m[1] * m[1] + m[2] * m[2] );
        if ( !( depth > 1E-3f ) )
            return 1E+6f;
        return 3.0f * scale * p[5] * viewport[3] * .5f / depth;
    }

    // remote: how far the body has given way to the bike's colour, 0 to 1
    void Shade( Look const & look, Point const & sun, float remote )
    {
        std::vector< ShapeVertex > const & vertices = sg_mesh.vertices;
        GLfloat * out = &sg_mesh.colours[0];
        for ( size_t v = 0; v < vertices.size(); ++v, out += 4 )
        {
            ShapeVertex const & vertex = vertices[v];
            float colour[3];
            if ( vertex.part == Part_Light )
                Set( colour, look.light[0], look.light[1], look.light[2] );
            else if ( vertex.part == Part_Ember )
                Set( colour, look.ember[0], look.ember[1], look.ember[2] );
            else
            {
                // the sun, as the arrow's faces take it: a face either
                // catches it or not, with a short blend between
                Point const & n = vertex.normal;
                float lit = ( Dot( n, sun ) + .15f ) / .75f;
                lit = std::max( 0.0f, std::min( 1.0f, lit ) );
                float const light = .64f + .36f * lit;
                // and faces that look up catch some of the sky, as the walls' tops do
                float const sky = std::max( 0.0f, n.z ) * look.sky;
                for ( int k = 0; k < 3; ++k )
                {
                    float const base = look.base[ vertex.part ][k] * light;
                    colour[k] = base + ( 1 - base ) * sky;
                    // far away, the bike's colour, so the cycle still reads as it
                    colour[k] += ( look.paint[k] * light - colour[k] ) * remote;
                }
            }
            for ( int k = 0; k < 3; ++k )
                out[k] = std::max( 0.0f, std::min( 1.0f, colour[k] ) );
            out[3] = 1;
        }
    }
}

void sg_RenderLightCycle( eCoord const & dir, REAL r, REAL g, REAL b )
{
    if ( !sg_mesh.built )
        Build();
    if ( sg_mesh.vertices.empty() )
        return;

    // from this many pixels long on the screen down, the body gives way to the bike's colour
    float const remote = std::max( 0.0f, std::min( 1.0f, ( 60 - Pixels() ) / 40 ) );

    // the sun, in the cycle's frame
    eCoord toSun;
    REAL reach;
    se_CleanArenaSun( toSun, reach );
    float const norm = 1 / sqrtf( reach * reach + 1 );
    Point const sun = P( eCoord::F( toSun, dir ) * reach * norm, eCoord::F( toSun, eCoord( -dir.y, dir.x ) ) * reach * norm, norm );

    Shade( MakeLook( r, g, b ), sun, remote );

    RenderEnd();
    glPushAttrib( GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LIGHTING_BIT | GL_POLYGON_BIT );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_CULL_FACE );
    glShadeModel( GL_SMOOTH );

    glDisableClientState( GL_NORMAL_ARRAY );
    glDisableClientState( GL_TEXTURE_COORD_ARRAY );
    glDisableClientState( GL_INDEX_ARRAY );
    glDisableClientState( GL_EDGE_FLAG_ARRAY );
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_COLOR_ARRAY );
    glVertexPointer( 3, GL_FLOAT, 0, &sg_mesh.places[0] );
    glColorPointer( 4, GL_FLOAT, 0, &sg_mesh.colours[0] );

    glDrawElements( GL_TRIANGLES, static_cast< GLsizei >( sg_mesh.decals ), GL_UNSIGNED_SHORT, &sg_mesh.indices[0] );

    // the lights lie a hair off the body; they are drawn nearer still, so
    // they never sink into it
    sr_DepthOffset( true );
    glDrawElements( GL_TRIANGLES, static_cast< GLsizei >( sg_mesh.indices.size() - sg_mesh.decals ), GL_UNSIGNED_SHORT,
                    &sg_mesh.indices[ sg_mesh.decals ] );
    sr_DepthOffset( false );

    glPopClientAttrib();
    glPopAttrib();
}

#endif
