#include "config.h"

#ifndef DEDICATED

#define DONTDOIT
#include "rRender.h"
#include "rMetalBackend.h"
#include "rMetalGLCompat.h"
#include "rMatrixState.h"
#include "tMemManager.h"

#include <vector>

enum MetalPrimitive
{
    MP_None = 0,
    MP_Lines,
    MP_Triangles,
    MP_Quads,
    MP_LineStrip,
    MP_LineLoop,
    MP_TriangleStrip,
    MP_QuadStrip,
    MP_TriangleFan
};

class metalRenderer : public rRenderer
{
    MetalPrimitive lastPrimitive_;
    bool forceEnd_;

    float curR_, curG_, curB_, curA_;
    float curU_, curV_;

    std::vector<float> batch_;

    void FlushBatch()
    {
        if (batch_.empty())
            return;

        std::vector<float> expanded;
        const int stride = 9;
        const int n = (int)batch_.size() / stride;

        auto emit = [&]( int srcVert )
        {
            const int src = srcVert * stride;
            for ( int f = 0; f < stride; ++f )
                expanded.push_back( batch_[src + f] );
        };

        switch ( lastPrimitive_ )
        {
        case MP_Lines:
        case MP_Triangles:
            // already a flat list in the layout Metal wants
            expanded = batch_;
            break;
        case MP_TriangleStrip:
            // GL_TRIANGLE_STRIP: alternate winding so faces stay consistent
            for ( int i = 0; i + 2 < n; ++i )
            {
                if ( i & 1 )
                {
                    emit( i + 1 );
                    emit( i );
                }
                else
                {
                    emit( i );
                    emit( i + 1 );
                }
                emit( i + 2 );
            }
            break;
        case MP_TriangleFan:
            // GL_TRIANGLE_FAN: ( 0, i, i+1 )
            for ( int i = 1; i + 1 < n; ++i )
            {
                emit( 0 );
                emit( i );
                emit( i + 1 );
            }
            break;
        case MP_Quads:
            // GL_QUADS: independent quads, corners in order i..i+3
            for ( int i = 0; i + 3 < n; i += 4 )
            {
                emit( i );     emit( i + 1 ); emit( i + 2 );
                emit( i );     emit( i + 2 ); emit( i + 3 );
            }
            break;
        case MP_QuadStrip:
            // GL_QUAD_STRIP: each quad is i, i+1, i+3, i+2
            for ( int i = 0; i + 3 < n; i += 2 )
            {
                emit( i );     emit( i + 1 ); emit( i + 3 );
                emit( i );     emit( i + 3 ); emit( i + 2 );
            }
            break;
        case MP_LineStrip:
            for ( int i = 1; i < n; ++i )
            {
                emit( i - 1 );
                emit( i );
            }
            break;
        case MP_LineLoop:
            for ( int i = 1; i <= n; ++i )
            {
                emit( ( i - 1 ) % n );
                emit( i % n );
            }
            break;
        default:
            batch_.clear();
            lastPrimitive_ = MP_None;
            return;
        }

        float mvp[16], texM[16];
        rMatrixState::GetModelViewProjection( mvp );
        rMatrixState::GetTextureMatrix( texM );

        const rMetalGLState *gs = sr_GetMetalGLState();
        const bool useTex = gs->texture2D && gs->boundTexture > 0;
        const int outVerts = (int)expanded.size() / stride;

        if ( lastPrimitive_ == MP_Lines || lastPrimitive_ == MP_LineStrip || lastPrimitive_ == MP_LineLoop )
            sr_MetalDrawLines( expanded.data(), outVerts, mvp );
        else
            sr_MetalDrawTriangles( expanded.data(), outVerts, mvp, texM, useTex, gs->boundTexture );

        batch_.clear();
        lastPrimitive_ = MP_None;
    }

    void PushVertex( float x, float y, float z )
    {
        batch_.push_back( x );
        batch_.push_back( y );
        batch_.push_back( z );
        batch_.push_back( curR_ );
        batch_.push_back( curG_ );
        batch_.push_back( curB_ );
        batch_.push_back( curA_ );
        batch_.push_back( curU_ );
        batch_.push_back( curV_ );
    }

    void BeginPrimitive( MetalPrimitive p, bool forceEnd = false )
    {
        if ( lastPrimitive_ != MP_None && lastPrimitive_ != p )
            FlushBatch();
        lastPrimitive_ = p;
        forceEnd_ = forceEnd;
    }

public:
    metalRenderer()
        : lastPrimitive_( MP_None ), forceEnd_( false ),
          curR_( 1.f ), curG_( 1.f ), curB_( 1.f ), curA_( 1.f ), curU_( 0.f ), curV_( 0.f )
    {
        ChangeFlags( 0xffffffff, 0 );
    }

    virtual ~metalRenderer() {}

    virtual void Vertex( REAL x, REAL y ) { PushVertex( x, y, 0.f ); }
    virtual void Vertex( REAL x, REAL y, REAL z ) { PushVertex( x, y, z ); }
    virtual void Vertex3( REAL *x ) { PushVertex( x[0], x[1], x[2] ); }
    virtual void Vertex( REAL x, REAL y, REAL z, REAL w ) { PushVertex( x, y, z ); }

    virtual void TexCoord( REAL u, REAL v ) { curU_ = u; curV_ = v; }
    virtual void TexCoord( REAL u, REAL v, REAL w ) { curU_ = u; curV_ = v; }
    virtual void TexCoord( REAL u, REAL v, REAL w, REAL t ) { curU_ = u; curV_ = v; }

    virtual void Color( REAL r, REAL g, REAL b ) { curR_ = r; curG_ = g; curB_ = b; curA_ = 1.f; }
    virtual void Color( REAL r, REAL g, REAL b, REAL a ) { curR_ = r; curG_ = g; curB_ = b; curA_ = a; }

    virtual void End( bool force = true )
    {
        if ( ( forceEnd_ || force ) && lastPrimitive_ != MP_None )
        {
            forceEnd_ = false;
            FlushBatch();
        }
    }

    virtual void BeginLines() { BeginPrimitive( MP_Lines ); }
    virtual void BeginTriangles() { BeginPrimitive( MP_Triangles ); }
    virtual void BeginQuads() { BeginPrimitive( MP_Quads ); }
    virtual void BeginLineStrip() { BeginPrimitive( MP_LineStrip, true ); }
    virtual void BeginLineLoop() { BeginPrimitive( MP_LineLoop, true ); }
    virtual void BeginTriangleStrip() { BeginPrimitive( MP_TriangleStrip, true ); }
    virtual void BeginQuadStrip() { BeginPrimitive( MP_QuadStrip, true ); }
    virtual void BeginTriangleFan() { BeginPrimitive( MP_TriangleFan, true ); }

    virtual void Line( REAL x1, REAL y1, REAL z1, REAL x2, REAL y2, REAL z2 )
    {
        BeginPrimitive( MP_Lines );
        PushVertex( x1, y1, z1 );
        PushVertex( x2, y2, z2 );
        End();
    }

    virtual void ProjMatrix() { End( true ); rMatrixState::MatrixMode( GL_PROJECTION ); }
    virtual void ModelMatrix() { End( true ); rMatrixState::MatrixMode( GL_MODELVIEW ); }
    virtual void TexMatrix() { End( true ); rMatrixState::MatrixMode( GL_TEXTURE ); }

    virtual void PushMatrix() { rMatrixState::PushMatrix(); }

    virtual void PopMatrix()
    {
        End( true );
        rMatrixState::PopMatrix();
    }

    virtual void MultMatrix( REAL mdata[4][4] )
    {
        End( true );
        rMatrixState::MultMatrixf( &mdata[0][0] );
    }

    virtual void IdentityMatrix()
    {
        End( true );
        rMatrixState::LoadIdentity();
    }

    virtual void ScaleMatrix( REAL f ) { End( true ); rMatrixState::Scalef( f, f, f ); }
    virtual void ScaleMatrix( REAL f1, REAL f2, REAL f3 ) { End( true ); rMatrixState::Scalef( f1, f2, f3 ); }
    virtual void TranslateMatrix( REAL x1, REAL x2, REAL x3 ) { End( true ); rMatrixState::Translatef( x1, x2, x3 ); }

    virtual void ReallySetFlag( flag f, bool c )
    {
        switch ( f )
        {
        case ALPHA_BLEND:
            if ( c )
                sr_metal_glEnable( GL_BLEND );
            else
                sr_metal_glDisable( GL_BLEND );
            break;
        case DEPTH_TEST:
            if ( c )
                sr_metal_glEnable( GL_DEPTH_TEST );
            else
                sr_metal_glDisable( GL_DEPTH_TEST );
            break;
        default:
            break;
        }
    }
};

void sr_metalRendererInit()
{
    tNEW( metalRenderer );
}

#endif
