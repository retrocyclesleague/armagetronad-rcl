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

#include "tJson.h"

#include <cstdlib>
#include <cstring>

namespace
{
    tJson const st_jsonNull;
    std::string const st_jsonEmpty;
}

// reads one value at a time from the text, by recursive descent
class tJsonReader
{
public:
    explicit tJsonReader( std::string const & text )
    : at_( text.c_str() ), end_( text.c_str() + text.size() ){}

    bool Document( tJson & out )
    {
        if ( !Value( out, 0 ) )
            return false;
        Space();
        return at_ == end_;
    }

private:
    char const * at_;
    char const * end_;

    void Space()
    {
        while ( at_ < end_ && ( *at_ == ' ' || *at_ == '\t' || *at_ == '\n' || *at_ == '\r' ) )
            ++at_;
    }

    bool Word( char const * word )
    {
        size_t const n = strlen( word );
        if ( static_cast< size_t >( end_ - at_ ) < n || strncmp( at_, word, n ) != 0 )
            return false;
        at_ += n;
        return true;
    }

    bool Value( tJson & out, int depth )
    {
        if ( depth > 64 )
            return false;
        Space();
        if ( at_ >= end_ )
            return false;

        switch ( *at_ )
        {
        case '{':
            return Object( out, depth );
        case '[':
            return Array( out, depth );
        case '"':
            out.kind_ = tJson::Kind_String;
            return Text( out.string_ );
        case 't':
            out.kind_ = tJson::Kind_Bool;
            out.bool_ = true;
            return Word( "true" );
        case 'f':
            out.kind_ = tJson::Kind_Bool;
            out.bool_ = false;
            return Word( "false" );
        case 'n':
            out.kind_ = tJson::Kind_Null;
            return Word( "null" );
        default:
            return Number( out );
        }
    }

    bool Object( tJson & out, int depth )
    {
        out.kind_ = tJson::Kind_Object;
        ++at_;
        Space();
        if ( at_ < end_ && *at_ == '}' )
        {
            ++at_;
            return true;
        }
        while ( true )
        {
            Space();
            if ( at_ >= end_ || *at_ != '"' )
                return false;
            out.members_.push_back( std::make_pair( std::string(), tJson() ) );
            if ( !Text( out.members_.back().first ) )
                return false;
            Space();
            if ( at_ >= end_ || *at_ != ':' )
                return false;
            ++at_;
            if ( !Value( out.members_.back().second, depth + 1 ) )
                return false;
            Space();
            if ( at_ >= end_ )
                return false;
            if ( *at_ == '}' )
            {
                ++at_;
                return true;
            }
            if ( *at_ != ',' )
                return false;
            ++at_;
        }
    }

    bool Array( tJson & out, int depth )
    {
        out.kind_ = tJson::Kind_Array;
        ++at_;
        Space();
        if ( at_ < end_ && *at_ == ']' )
        {
            ++at_;
            return true;
        }
        while ( true )
        {
            out.items_.push_back( tJson() );
            if ( !Value( out.items_.back(), depth + 1 ) )
                return false;
            Space();
            if ( at_ >= end_ )
                return false;
            if ( *at_ == ']' )
            {
                ++at_;
                return true;
            }
            if ( *at_ != ',' )
                return false;
            ++at_;
        }
    }

    bool Number( tJson & out )
    {
        char const * const start = at_;
        if ( at_ < end_ && *at_ == '-' )
            ++at_;
        if ( at_ >= end_ || *at_ < '0' || *at_ > '9' )
            return false;
        while ( at_ < end_ && ( ( *at_ >= '0' && *at_ <= '9' ) || *at_ == '.' || *at_ == 'e' ||
                                *at_ == 'E' || *at_ == '+' || *at_ == '-' ) )
            ++at_;
        std::string const digits( start, at_ );
        char * stop = NULL;
        out.kind_ = tJson::Kind_Number;
        out.number_ = strtod( digits.c_str(), &stop );
        return stop && *stop == '\0';
    }

    static void Utf8( std::string & to, unsigned long code )
    {
        if ( code < 0x80 )
            to += static_cast< char >( code );
        else if ( code < 0x800 )
        {
            to += static_cast< char >( 0xC0 | ( code >> 6 ) );
            to += static_cast< char >( 0x80 | ( code & 0x3F ) );
        }
        else if ( code < 0x10000 )
        {
            to += static_cast< char >( 0xE0 | ( code >> 12 ) );
            to += static_cast< char >( 0x80 | ( ( code >> 6 ) & 0x3F ) );
            to += static_cast< char >( 0x80 | ( code & 0x3F ) );
        }
        else
        {
            to += static_cast< char >( 0xF0 | ( code >> 18 ) );
            to += static_cast< char >( 0x80 | ( ( code >> 12 ) & 0x3F ) );
            to += static_cast< char >( 0x80 | ( ( code >> 6 ) & 0x3F ) );
            to += static_cast< char >( 0x80 | ( code & 0x3F ) );
        }
    }

    bool Hex4( unsigned long & code )
    {
        if ( end_ - at_ < 4 )
            return false;
        code = 0;
        for ( int i = 0; i < 4; ++i, ++at_ )
        {
            char const c = *at_;
            code <<= 4;
            if ( c >= '0' && c <= '9' )
                code |= c - '0';
            else if ( c >= 'a' && c <= 'f' )
                code |= c - 'a' + 10;
            else if ( c >= 'A' && c <= 'F' )
                code |= c - 'A' + 10;
            else
                return false;
        }
        return true;
    }

    bool Text( std::string & to )
    {
        ++at_;
        while ( at_ < end_ )
        {
            char const c = *at_++;
            if ( c == '"' )
                return true;
            if ( static_cast< unsigned char >( c ) < 0x20 )
                return false;
            if ( c != '\\' )
            {
                to += c;
                continue;
            }
            if ( at_ >= end_ )
                return false;
            switch ( *at_++ )
            {
            case '"':  to += '"';  break;
            case '\\': to += '\\'; break;
            case '/':  to += '/';  break;
            case 'b':  to += '\b'; break;
            case 'f':  to += '\f'; break;
            case 'n':  to += '\n'; break;
            case 'r':  to += '\r'; break;
            case 't':  to += '\t'; break;
            case 'u':
            {
                unsigned long code;
                if ( !Hex4( code ) )
                    return false;
                // a character outside the first plane comes as two halves
                if ( code >= 0xD800 && code < 0xDC00 && end_ - at_ >= 6 && at_[0] == '\\' && at_[1] == 'u' )
                {
                    char const * const back = at_;
                    at_ += 2;
                    unsigned long low;
                    if ( Hex4( low ) && low >= 0xDC00 && low < 0xE000 )
                        code = 0x10000 + ( ( code - 0xD800 ) << 10 ) + ( low - 0xDC00 );
                    else
                        at_ = back;
                }
                if ( code >= 0xD800 && code < 0xE000 )
                    code = 0xFFFD;
                Utf8( to, code );
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
};

bool tJson::Parse( std::string const & text, tJson & out )
{
    tJson read;
    tJsonReader reader( text );
    if ( !reader.Document( read ) )
    {
        out = tJson();
        return false;
    }
    out.kind_ = read.kind_;
    out.bool_ = read.bool_;
    out.number_ = read.number_;
    out.string_.swap( read.string_ );
    out.items_.swap( read.items_ );
    out.members_.swap( read.members_ );
    return true;
}

std::string const & tJson::Str() const
{
    return kind_ == Kind_String ? string_ : st_jsonEmpty;
}

size_t tJson::Size() const
{
    return kind_ == Kind_Array ? items_.size() : kind_ == Kind_Object ? members_.size() : 0;
}

tJson const & tJson::operator[]( size_t index ) const
{
    return kind_ == Kind_Array && index < items_.size() ? items_[index] : st_jsonNull;
}

tJson const & tJson::operator[]( int index ) const
{
    return index < 0 ? st_jsonNull : (*this)[ static_cast< size_t >( index ) ];
}

tJson const & tJson::operator[]( char const * key ) const
{
    if ( kind_ == Kind_Object )
        for ( size_t i = 0; i < members_.size(); ++i )
            if ( members_[i].first == key )
                return members_[i].second;
    return st_jsonNull;
}
