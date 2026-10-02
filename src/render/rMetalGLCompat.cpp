#include "config.h"

#ifndef DEDICATED

#include "rMetalGLCompat.h"
#include "rGraphicsBackend.h"
#include "rMatrixState.h"

// ponytail: do not include rGL.h here — its gl* macros would recurse into these wrappers.
#define NO_SDL_GLEXT
#define GL_SILENCE_DEPRECATION
#include <SDL3/SDL_opengl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

// Undef rGL.h macros if any prior include pulled them in (PCH / transitive headers).
#ifdef glMatrixMode
#undef glMatrixMode
#undef glLoadIdentity
#undef glMultMatrixf
#undef glPushMatrix
#undef glPopMatrix
#undef glTranslatef
#undef glScalef
#undef glFrustum
#undef glOrtho
#undef glViewport
#undef glEnable
#undef glDisable
#undef glBindTexture
#undef glDepthFunc
#undef glAlphaFunc
#undef glBlendFunc
#undef glPolygonOffset
#undef glTexParameteri
#endif

namespace {
// Real GL entry points — never macro-expanded.
void real_glMatrixMode( GLenum mode ) { ::glMatrixMode( mode ); }
void real_glLoadIdentity() { ::glLoadIdentity(); }
void real_glMultMatrixf( const GLfloat *m ) { ::glMultMatrixf( m ); }
void real_glPushMatrix() { ::glPushMatrix(); }
void real_glPopMatrix() { ::glPopMatrix(); }
void real_glTranslatef( GLfloat x, GLfloat y, GLfloat z ) { ::glTranslatef( x, y, z ); }
void real_glScalef( GLfloat x, GLfloat y, GLfloat z ) { ::glScalef( x, y, z ); }
void real_glFrustum( GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f )
    { ::glFrustum( a, b, c, d, e, f ); }
void real_glOrtho( GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f )
    { ::glOrtho( a, b, c, d, e, f ); }
void real_glViewport( GLint x, GLint y, GLsizei w, GLsizei h ) { ::glViewport( x, y, w, h ); }
void real_glEnable( GLenum cap ) { ::glEnable( cap ); }
void real_glDisable( GLenum cap ) { ::glDisable( cap ); }
void real_glBindTexture( GLenum target, GLuint texture ) { ::glBindTexture( target, texture ); }
void real_glDepthFunc( GLenum func ) { ::glDepthFunc( func ); }
void real_glAlphaFunc( GLenum func, GLclampf ref ) { ::glAlphaFunc( func, ref ); }
void real_glBlendFunc( GLenum sf, GLenum df ) { ::glBlendFunc( sf, df ); }
void real_glPolygonOffset( GLfloat f, GLfloat u ) { ::glPolygonOffset( f, u ); }
void real_glTexParameteri( GLenum target, GLenum pname, GLint param ) { ::glTexParameteri( target, pname, param ); }
}

static rMetalGLState sr_metalGLState = {
    true, true, false, false, false, 0, 0, 0, 0, 0
};

const rMetalGLState *sr_GetMetalGLState()
{
    return &sr_metalGLState;
}

static bool MetalActive()
{
    return sr_UsingMetalBackend();
}

void sr_metal_glMatrixMode( GLenum mode )
{
    if ( MetalActive() )
        rMatrixState::MatrixMode( mode );
    else
        real_glMatrixMode( mode );
}

void sr_metal_glLoadIdentity()
{
    if ( MetalActive() )
        rMatrixState::LoadIdentity();
    else
        real_glLoadIdentity();
}

void sr_metal_glMultMatrixf( const GLfloat *m )
{
    if ( MetalActive() )
        rMatrixState::MultMatrixf( m );
    else
        real_glMultMatrixf( m );
}

void sr_metal_glPushMatrix()
{
    if ( MetalActive() )
        rMatrixState::PushMatrix();
    else
        real_glPushMatrix();
}

void sr_metal_glPopMatrix()
{
    if ( MetalActive() )
        rMatrixState::PopMatrix();
    else
        real_glPopMatrix();
}

void sr_metal_glTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
    if ( MetalActive() )
        rMatrixState::Translatef( x, y, z );
    else
        real_glTranslatef( x, y, z );
}

void sr_metal_glScalef( GLfloat x, GLfloat y, GLfloat z )
{
    if ( MetalActive() )
        rMatrixState::Scalef( x, y, z );
    else
        real_glScalef( x, y, z );
}

void sr_metal_glFrustum( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal )
{
    if ( MetalActive() )
        rMatrixState::Frustum( (float)left, (float)right, (float)bottom, (float)top, (float)nearVal, (float)farVal );
    else
        real_glFrustum( left, right, bottom, top, nearVal, farVal );
}

void sr_metal_glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal )
{
    if ( MetalActive() )
        rMatrixState::Ortho( (float)left, (float)right, (float)bottom, (float)top, (float)nearVal, (float)farVal );
    else
        real_glOrtho( left, right, bottom, top, nearVal, farVal );
}

void sr_metal_glViewport( GLint x, GLint y, GLsizei width, GLsizei height )
{
    if ( MetalActive() )
    {
        sr_metalGLState.viewportX = x;
        sr_metalGLState.viewportY = y;
        sr_metalGLState.viewportW = width;
        sr_metalGLState.viewportH = height;
    }
    else
        real_glViewport( x, y, width, height );
}

void sr_metal_glEnable( GLenum cap )
{
    if ( MetalActive() )
    {
        switch ( cap )
        {
        case GL_DEPTH_TEST:
            sr_metalGLState.depthTest = true;
            break;
        case GL_BLEND:
            sr_metalGLState.blend = true;
            break;
        case GL_TEXTURE_2D:
            sr_metalGLState.texture2D = true;
            break;
        case GL_ALPHA_TEST:
            sr_metalGLState.alphaTest = true;
            break;
        default:
            break;
        }
    }
    else
        real_glEnable( cap );
}

void sr_metal_glDisable( GLenum cap )
{
    if ( MetalActive() )
    {
        switch ( cap )
        {
        case GL_DEPTH_TEST:
            sr_metalGLState.depthTest = false;
            break;
        case GL_BLEND:
            sr_metalGLState.blend = false;
            break;
        case GL_TEXTURE_2D:
            sr_metalGLState.texture2D = false;
            break;
        case GL_ALPHA_TEST:
            sr_metalGLState.alphaTest = false;
            break;
        default:
            break;
        }
    }
    else
        real_glDisable( cap );
}

void sr_metal_glBindTexture( GLenum target, GLuint texture )
{
    if ( MetalActive() && target == GL_TEXTURE_2D )
    {
        sr_metalGLState.boundTexture = texture;
        sr_metalGLState.texWrapSRepeat = false;
    }
    else
        real_glBindTexture( target, texture );
}

void sr_metal_glDepthFunc( GLenum func )
{
    if ( !MetalActive() )
        real_glDepthFunc( func );
}

void sr_metal_glAlphaFunc( GLenum func, GLclampf ref )
{
    if ( !MetalActive() )
        real_glAlphaFunc( func, ref );
}

void sr_metal_glBlendFunc( GLenum sfactor, GLenum dfactor )
{
    if ( !MetalActive() )
        real_glBlendFunc( sfactor, dfactor );
}

void sr_metal_glPolygonOffset( GLfloat factor, GLfloat units )
{
    if ( !MetalActive() )
        real_glPolygonOffset( factor, units );
}

void sr_metal_glTexParameteri( GLenum target, GLenum pname, GLint param )
{
    if ( MetalActive() && target == GL_TEXTURE_2D )
    {
        if ( pname == GL_TEXTURE_WRAP_S )
            sr_metalGLState.texWrapSRepeat = param == GL_REPEAT;
    }
    else
        real_glTexParameteri( target, pname, param );
}

#endif
