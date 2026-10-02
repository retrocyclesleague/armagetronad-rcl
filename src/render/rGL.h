#ifndef AT_GL_H
#define AT_GL_H

#ifndef DEDICATED

#ifdef WIN32
#include <windows.h>
#undef GetUserName
#endif

#define NO_SDL_GLEXT
#define GL_SILENCE_DEPRECATION
#include <SDL3/SDL_opengl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#ifdef __APPLE__
#include "rMetalGLCompat.h"
#define glMatrixMode sr_metal_glMatrixMode
#define glLoadIdentity sr_metal_glLoadIdentity
#define glMultMatrixf sr_metal_glMultMatrixf
#define glPushMatrix sr_metal_glPushMatrix
#define glPopMatrix sr_metal_glPopMatrix
#define glTranslatef sr_metal_glTranslatef
#define glScalef sr_metal_glScalef
#define glFrustum sr_metal_glFrustum
#define glOrtho sr_metal_glOrtho
#define glViewport sr_metal_glViewport
#define glEnable sr_metal_glEnable
#define glDisable sr_metal_glDisable
#define glBindTexture sr_metal_glBindTexture
#define glDepthFunc sr_metal_glDepthFunc
#define glAlphaFunc sr_metal_glAlphaFunc
#define glBlendFunc sr_metal_glBlendFunc
#define glPolygonOffset sr_metal_glPolygonOffset
#define glTexParameteri sr_metal_glTexParameteri
#endif

/*
// include OpenGL header
#ifdef HAVE_SDL_OPENGL_H
#include <SDL_opengl.h>
#else
#ifdef HAVE_SDL_SDL_OPENGL_H
#include <SDL/SDL_opengl.h>
#else
#ifdef HAVE_OPENGL_GL_H
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#ifdef HAVE_GL_GL_H
#include <GL/gl.h>
#include <GL/glu.h>
#else
#error No suitable OpenGL header found by configure!
#endif
#endif
#endif
#endif
*/

#else

typedef float GLfloat;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
#endif


#ifdef DEBUG
#ifndef DEDICATED
#define AA_GL_ERROR_CHECKING
#endif
#endif

#ifdef AA_GL_ERROR_CHECKING
//! for debugging purposes: checks for OpenGL errors and prints them to the console.
void sr_CheckGLError();
#else
inline void sr_CheckGLError(){}
#endif

#endif
