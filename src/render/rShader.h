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

#ifndef ArmageTron_SHADER_H
#define ArmageTron_SHADER_H

#include "config.h"

#ifndef DEDICATED

#include <string>
#include <utility>
#include <vector>

// A GLSL program made of one vertex and one fragment shader, read from files
// in the data folder's shaders/ (so they can be changed without a rebuild,
// see RCL_SHADER_RELOAD). What fails to compile or link is written to the
// console with the driver's log.
class rShader
{
public:
    rShader( char const * vertexFile, char const * fragmentFile );
    ~rShader();

    //! reads, compiles and links; on failure says why in error and keeps what it had
    bool Load( std::string & error );
    //! deletes the program; the context must still be there
    void Release();
    //! drops the program without deleting it: its context is gone
    void Forget();

    bool Ready() const { return program_ != 0; }
    //! makes it the program things are drawn with
    void Use();
    //! back to fixed function
    static void UseNone();

    //! uniforms by name, for the program in use
    void Set( char const * name, float value );
    void Set( char const * name, float a, float b );
    void Set( char const * name, float a, float b, float c );
    void Set( char const * name, float a, float b, float c, float d );
    void SetSampler( char const * name, int unit );
    //! a 4x4 matrix, column by column as GL keeps them
    void SetMatrix4( char const * name, float const * matrix );

    //! whether either file changed on disk since it was read
    bool FilesChanged() const;

    //! when a file under shaders/ was last changed, 0 if it is not there
    static long Stamp( char const * file );
    //! reads a file under shaders/ whole; says why not in error
    static bool Read( char const * file, std::string & content, std::string & error );

    char const * Name() const { return fragment_.c_str(); }

private:
    rShader( rShader const & );
    rShader & operator=( rShader const & );

    int Location( char const * name );

    std::string vertex_, fragment_;     // file names under shaders/
    unsigned int program_;
    long stamp_[2];                     // when the files were last changed, as read
    std::vector< std::pair< std::string, int > > locations_;
};

#endif // DEDICATED

#endif
