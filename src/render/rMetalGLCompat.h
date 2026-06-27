#ifndef AT_METAL_GL_COMPAT_H
#define AT_METAL_GL_COMPAT_H

#ifndef DEDICATED

#define NO_SDL_GLEXT
#define GL_SILENCE_DEPRECATION
#include <SDL3/SDL_opengl.h>

struct rMetalGLState
{
    bool depthTest;
    bool blend;
    bool texture2D;
    bool alphaTest;
    bool texWrapSRepeat;
    GLuint boundTexture;
    int viewportX, viewportY, viewportW, viewportH;
};

void sr_metal_glMatrixMode( GLenum mode );
void sr_metal_glLoadIdentity();
void sr_metal_glMultMatrixf( const GLfloat *m );
void sr_metal_glPushMatrix();
void sr_metal_glPopMatrix();
void sr_metal_glTranslatef( GLfloat x, GLfloat y, GLfloat z );
void sr_metal_glScalef( GLfloat x, GLfloat y, GLfloat z );
void sr_metal_glFrustum( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal );
void sr_metal_glOrtho( GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal );
void sr_metal_glViewport( GLint x, GLint y, GLsizei width, GLsizei height );
void sr_metal_glEnable( GLenum cap );
void sr_metal_glDisable( GLenum cap );
void sr_metal_glBindTexture( GLenum target, GLuint texture );
void sr_metal_glDepthFunc( GLenum func );
void sr_metal_glAlphaFunc( GLenum func, GLclampf ref );
void sr_metal_glBlendFunc( GLenum sfactor, GLenum dfactor );
void sr_metal_glPolygonOffset( GLfloat factor, GLfloat units );
void sr_metal_glTexParameteri( GLenum target, GLenum pname, GLint param );

const rMetalGLState *sr_GetMetalGLState();

#endif

#endif
