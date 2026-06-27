#include "config.h"

#ifndef DEDICATED

#include "rMatrixState.h"
#include <cmath>

GLenum rMatrixState::activeMode_ = GL_MODELVIEW;
int rMatrixState::projSP_ = 0;
int rMatrixState::mvSP_ = 0;
int rMatrixState::texSP_ = 0;
float rMatrixState::proj_[MATRIX_STACK_DEPTH][16];
float rMatrixState::modelview_[MATRIX_STACK_DEPTH][16];
float rMatrixState::texture_[MATRIX_STACK_DEPTH][16];

static void Identity( float m[16] )
{
    for ( int i = 0; i < 16; ++i )
        m[i] = 0.f;
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void rMatrixState::LoadIdentityStack( float m[16] )
{
    Identity( m );
}

void rMatrixState::Multiply( float m[16], const float *a, const float *b )
{
    float out[16];
    for ( int col = 0; col < 4; ++col )
    {
        for ( int row = 0; row < 4; ++row )
        {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    for ( int i = 0; i < 16; ++i )
        m[i] = out[i];
}

float *rMatrixState::Current()
{
    switch ( activeMode_ )
    {
    case GL_PROJECTION:
        return proj_[projSP_];
    case GL_TEXTURE:
        return texture_[texSP_];
    default:
        return modelview_[mvSP_];
    }
}

void rMatrixState::ResetAll()
{
    projSP_ = mvSP_ = texSP_ = 0;
    activeMode_ = GL_MODELVIEW;
    LoadIdentityStack( proj_[0] );
    LoadIdentityStack( modelview_[0] );
    LoadIdentityStack( texture_[0] );
}

void rMatrixState::MatrixMode( GLenum mode )
{
    activeMode_ = mode;
}

void rMatrixState::LoadIdentity()
{
    LoadIdentityStack( Current() );
}

void rMatrixState::MultMatrixf( const float *m )
{
    float cur[16];
    for ( int i = 0; i < 16; ++i )
        cur[i] = Current()[i];
    Multiply( Current(), cur, m );
}

void rMatrixState::PushMatrix()
{
    switch ( activeMode_ )
    {
    case GL_PROJECTION:
        if ( projSP_ + 1 < MATRIX_STACK_DEPTH )
        {
            ++projSP_;
            for ( int i = 0; i < 16; ++i )
                proj_[projSP_][i] = proj_[projSP_ - 1][i];
        }
        break;
    case GL_TEXTURE:
        if ( texSP_ + 1 < MATRIX_STACK_DEPTH )
        {
            ++texSP_;
            for ( int i = 0; i < 16; ++i )
                texture_[texSP_][i] = texture_[texSP_ - 1][i];
        }
        break;
    default:
        if ( mvSP_ + 1 < MATRIX_STACK_DEPTH )
        {
            ++mvSP_;
            for ( int i = 0; i < 16; ++i )
                modelview_[mvSP_][i] = modelview_[mvSP_ - 1][i];
        }
        break;
    }
}

void rMatrixState::PopMatrix()
{
    switch ( activeMode_ )
    {
    case GL_PROJECTION:
        if ( projSP_ > 0 )
            --projSP_;
        break;
    case GL_TEXTURE:
        if ( texSP_ > 0 )
            --texSP_;
        break;
    default:
        if ( mvSP_ > 0 )
            --mvSP_;
        break;
    }
}

void rMatrixState::Translatef( float x, float y, float z )
{
    float t[16];
    Identity( t );
    t[12] = x;
    t[13] = y;
    t[14] = z;
    MultMatrixf( t );
}

void rMatrixState::Scalef( float x, float y, float z )
{
    float s[16];
    Identity( s );
    s[0] = x;
    s[5] = y;
    s[10] = z;
    MultMatrixf( s );
}

void rMatrixState::Frustum( float left, float right, float bottom, float top, float nearVal, float farVal )
{
    float m[16];
    Identity( m );
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = farVal - nearVal;
    m[0] = 2.f * nearVal / rl;
    m[5] = 2.f * nearVal / tb;
    m[8] = ( right + left ) / rl;
    m[9] = ( top + bottom ) / tb;
    m[10] = -( farVal + nearVal ) / fn;
    m[11] = -1.f;
    m[14] = -( 2.f * farVal * nearVal ) / fn;
    m[15] = 0.f;
    MultMatrixf( m );
}

void rMatrixState::Ortho( float left, float right, float bottom, float top, float nearVal, float farVal )
{
    float m[16];
    Identity( m );
    m[0] = 2.f / ( right - left );
    m[5] = 2.f / ( top - bottom );
    m[10] = -2.f / ( farVal - nearVal );
    m[12] = -( right + left ) / ( right - left );
    m[13] = -( top + bottom ) / ( top - bottom );
    m[14] = -( farVal + nearVal ) / ( farVal - nearVal );
    MultMatrixf( m );
}

void rMatrixState::GetModelViewProjection( float out[16] )
{
    Multiply( out, proj_[projSP_], modelview_[mvSP_] );
}

void rMatrixState::GetTextureMatrix( float out[16] )
{
    for ( int i = 0; i < 16; ++i )
        out[i] = texture_[texSP_][i];
}

#endif
