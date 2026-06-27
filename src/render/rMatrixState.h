#ifndef AT_MATRIX_STATE_H
#define AT_MATRIX_STATE_H

#ifndef DEDICATED

#include "rGL.h"

class rMatrixState
{
public:
    static void ResetAll();
    static void MatrixMode( GLenum mode );
    static void LoadIdentity();
    static void MultMatrixf( const float *m );
    static void PushMatrix();
    static void PopMatrix();
    static void Translatef( float x, float y, float z );
    static void Scalef( float x, float y, float z );
    static void Frustum( float left, float right, float bottom, float top, float nearVal, float farVal );
    static void Ortho( float left, float right, float bottom, float top, float nearVal, float farVal );

    static void GetModelViewProjection( float out[16] );
    static void GetTextureMatrix( float out[16] );

private:
    enum { MATRIX_STACK_DEPTH = 32 };

    static GLenum activeMode_;
    static int projSP_, mvSP_, texSP_;
    static float proj_[MATRIX_STACK_DEPTH][16];
    static float modelview_[MATRIX_STACK_DEPTH][16];
    static float texture_[MATRIX_STACK_DEPTH][16];

    static float *Current();
    static void LoadIdentityStack( float m[16] );
    static void Multiply( float m[16], const float *a, const float *b );
};

#endif

#endif
