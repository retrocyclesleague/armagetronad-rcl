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

#ifndef ArmageTron_JSON_H
#define ArmageTron_JSON_H

#include <string>
#include <utility>
#include <vector>

//! A JSON value, read whole from text: enough to take what the league's site
//! answers apart. Asking for a member or an item that is not there gives a
//! null, so a path through a document needs no checks on the way.
class tJson
{
public:
    enum Kind { Kind_Null, Kind_Bool, Kind_Number, Kind_String, Kind_Array, Kind_Object };

    tJson(): kind_( Kind_Null ), bool_( false ), number_( 0 ){}

    //! Reads text into out. Returns false (and leaves out null) for anything
    //! that is not one well-formed JSON value, or nests deeper than 64.
    static bool Parse( std::string const & text, tJson & out );

    Kind Type() const { return kind_; }
    bool IsNull() const { return kind_ == Kind_Null; }

    //! the value, or fallback when it is of another kind
    bool Bool( bool fallback = false ) const { return kind_ == Kind_Bool ? bool_ : fallback; }
    double Number( double fallback = 0 ) const { return kind_ == Kind_Number ? number_ : fallback; }
    int Int( int fallback = 0 ) const { return kind_ == Kind_Number ? static_cast< int >( number_ ) : fallback; }
    std::string const & Str() const;    //!< UTF-8; empty unless a string

    //! items of an array, members of an object
    size_t Size() const;
    tJson const & operator[]( size_t index ) const;
    tJson const & operator[]( int index ) const;
    tJson const & operator[]( char const * key ) const;
    std::vector< std::pair< std::string, tJson > > const & Members() const { return members_; }

private:
    friend class tJsonReader;

    Kind kind_;
    bool bool_;
    double number_;
    std::string string_;
    std::vector< tJson > items_;
    std::vector< std::pair< std::string, tJson > > members_;
};

#endif
