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
#include "rShader.h"

#ifndef DEDICATED

#include "tConsole.h"
#include "tDirectories.h"
#include "tString.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace
{
    std::string sr_ShaderPath( std::string const & file )
    {
        return "shaders/" + file;
    }

    bool sr_ReadShader( std::string const & file, std::string & source, std::string & error )
    {
        std::ifstream in;
        if ( !tDirectories::Data().Open( in, sr_ShaderPath( file ).c_str() ) )
        {
            error = "cannot read shaders/" + file;
            return false;
        }
        std::stringstream all;
        all << in.rdbuf();
        source = all.str();
        if ( source.empty() )
        {
            error = "shaders/" + file + " is empty";
            return false;
        }
        return true;
    }

    // when the file was last changed, 0 if it cannot be found
    long sr_ShaderStamp( std::string const & file )
    {
        tString const path = tDirectories::Data().GetReadPath( sr_ShaderPath( file ).c_str() );
        struct stat info;
        if ( path.Len() <= 1 || stat( static_cast< char const * >( path ), &info ) != 0 )
            return 0;
        return static_cast< long >( info.st_mtime );
    }

#ifdef RGL_EXT_AVAILABLE
    std::string sr_ShaderLog( GLuint object, bool program )
    {
        GLint length = 0;
        if ( program )
            glGetProgramiv( object, RGL_INFO_LOG_LENGTH, &length );
        else
            glGetShaderiv( object, RGL_INFO_LOG_LENGTH, &length );
        if ( length <= 1 )
            return std::string();
        std::string log( static_cast< size_t >( length ), '\0' );
        GLsizei written = 0;
        if ( program )
            glGetProgramInfoLog( object, length, &written, &log[0] );
        else
            glGetShaderInfoLog( object, length, &written, &log[0] );
        log.resize( static_cast< size_t >( written > 0 ? written : 0 ) );
        return log;
    }

    GLuint sr_Compile( GLenum kind, std::string const & file, std::string const & source, std::string & error )
    {
        GLuint const shader = glCreateShader( kind );
        if ( !shader )
        {
            error = "no shader object for " + file;
            return 0;
        }
        char const * text = source.c_str();
        GLint const length = static_cast< GLint >( source.size() );
        glShaderSource( shader, 1, &text, &length );
        glCompileShader( shader );
        GLint ok = 0;
        glGetShaderiv( shader, RGL_COMPILE_STATUS, &ok );
        if ( !ok )
        {
            error = "shaders/" + file + " did not compile:\n" + sr_ShaderLog( shader, false );
            glDeleteShader( shader );
            return 0;
        }
        return shader;
    }
#endif
}

rShader::rShader( char const * vertexFile, char const * fragmentFile )
: vertex_( vertexFile ), fragment_( fragmentFile ), program_( 0 )
{
    stamp_[0] = stamp_[1] = 0;
}

rShader::~rShader()
{
    // the context may be gone by now; the driver frees what is left with it
}

bool rShader::Load( std::string & error )
{
    // What was loaded before stays in use until the new one has linked, so an
    // edit that does not compile leaves the last good program running.
#ifndef RGL_EXT_AVAILABLE
    error = "built without GLEW";
    return false;
#else
    if ( !rGLExt::Shaders() )
    {
        error = "no GLSL 1.20";
        return false;
    }

    std::string vertexSource, fragmentSource;
    if ( !sr_ReadShader( vertex_, vertexSource, error ) || !sr_ReadShader( fragment_, fragmentSource, error ) )
        return false;
    stamp_[0] = sr_ShaderStamp( vertex_ );
    stamp_[1] = sr_ShaderStamp( fragment_ );

    GLuint const vertex = sr_Compile( RGL_VERTEX_SHADER, vertex_, vertexSource, error );
    if ( !vertex )
        return false;
    GLuint const fragment = sr_Compile( RGL_FRAGMENT_SHADER, fragment_, fragmentSource, error );
    if ( !fragment )
    {
        glDeleteShader( vertex );
        return false;
    }

    GLuint const program = glCreateProgram();
    glAttachShader( program, vertex );
    glAttachShader( program, fragment );
    glLinkProgram( program );
    // the program keeps them as long as it needs them
    glDeleteShader( vertex );
    glDeleteShader( fragment );

    GLint ok = 0;
    glGetProgramiv( program, RGL_LINK_STATUS, &ok );
    if ( !ok )
    {
        error = "shaders/" + vertex_ + " and " + fragment_ + " did not link:\n" + sr_ShaderLog( program, true );
        glDeleteProgram( program );
        return false;
    }

    Release();
    program_ = program;
    return true;
#endif
}

void rShader::Release()
{
#ifdef RGL_EXT_AVAILABLE
    if ( program_ )
        glDeleteProgram( program_ );
#endif
    Forget();
}

void rShader::Forget()
{
    program_ = 0;
    locations_.clear();
}

void rShader::Use()
{
#ifdef RGL_EXT_AVAILABLE
    if ( program_ )
        glUseProgram( program_ );
#endif
}

void rShader::UseNone()
{
#ifdef RGL_EXT_AVAILABLE
    if ( rGLExt::Shaders() )
        glUseProgram( 0 );
#endif
}

int rShader::Location( char const * name )
{
    for ( size_t i = 0; i < locations_.size(); ++i )
        if ( locations_[i].first == name )
            return locations_[i].second;
    int location = -1;
#ifdef RGL_EXT_AVAILABLE
    if ( program_ )
        location = glGetUniformLocation( program_, name );
#endif
    locations_.push_back( std::make_pair( std::string( name ), location ) );
    return location;
}

void rShader::Set( char const * name, float value )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniform1f( location, value );
#endif
}

void rShader::Set( char const * name, float a, float b )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniform2f( location, a, b );
#endif
}

void rShader::Set( char const * name, float a, float b, float c )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniform3f( location, a, b, c );
#endif
}

void rShader::Set( char const * name, float a, float b, float c, float d )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniform4f( location, a, b, c, d );
#endif
}

void rShader::SetMatrix4( char const * name, float const * matrix )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniformMatrix4fv( location, 1, GL_FALSE, matrix );
#else
    (void)name;
    (void)matrix;
#endif
}

void rShader::SetSampler( char const * name, int unit )
{
#ifdef RGL_EXT_AVAILABLE
    int const location = Location( name );
    if ( location >= 0 )
        glUniform1i( location, unit );
#endif
}

bool rShader::FilesChanged() const
{
    return sr_ShaderStamp( vertex_ ) != stamp_[0] || sr_ShaderStamp( fragment_ ) != stamp_[1];
}

long rShader::Stamp( char const * file )
{
    return sr_ShaderStamp( file );
}

bool rShader::Read( char const * file, std::string & content, std::string & error )
{
    return sr_ReadShader( file, content, error );
}

#endif // DEDICATED
