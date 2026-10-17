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

#include "config.h"
#include "tHttps.h"

#include <algorithm>
#include <vector>

// The league's site only answers over TLS. Rather than carry a TLS library,
// the client asks the system's own: WinHTTP on Windows, which also uses the
// system's certificate store and proxy settings, and libcurl where it is.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

namespace
{
    std::wstring st_Wide( std::string const & text )
    {
        if ( text.empty() )
            return std::wstring();
        int const n = MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast< int >( text.size() ), NULL, 0 );
        std::wstring wide( n, L'\0' );
        MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast< int >( text.size() ), &wide[0], n );
        return wide;
    }

    // closes a WinHTTP handle when it goes out of scope
    struct st_HttpHandle
    {
        HINTERNET handle;
        explicit st_HttpHandle( HINTERNET h ): handle( h ){}
        ~st_HttpHandle(){ if ( handle ) WinHttpCloseHandle( handle ); }
        operator HINTERNET() const { return handle; }
    };
}

bool st_HttpsAvailable()
{
    return true;
}

void st_HttpsInit()
{
}

int st_HttpsGet( std::string const & host, std::string const & path, std::string & body,
                 int maxlen, double budget )
{
    body.clear();
    if ( budget <= 0 )
        return -1;
    int const ms = static_cast< int >( budget * 1000 );

    st_HttpHandle session( WinHttpOpen( L"Retrocycles RCL", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
    if ( !session )
        return -1;
    WinHttpSetTimeouts( session, ms, ms, ms, ms );

    st_HttpHandle connection( WinHttpConnect( session, st_Wide( host ).c_str(),
                                              INTERNET_DEFAULT_HTTPS_PORT, 0 ) );
    if ( !connection )
        return -1;

    st_HttpHandle request( WinHttpOpenRequest( connection, L"GET", st_Wide( path ).c_str(), NULL,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE ) );
    if ( !request )
        return -1;

    if ( !WinHttpSendRequest( request, L"Accept: application/json\r\n", static_cast< DWORD >( -1L ),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ||
         !WinHttpReceiveResponse( request, NULL ) )
        return -1;

    DWORD status = 0, size = sizeof( status );
    if ( !WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX ) )
        return -1;

    std::vector< char > chunk( 16384 );
    while ( static_cast< int >( body.size() ) < maxlen )
    {
        DWORD got = 0;
        if ( !WinHttpReadData( request, &chunk[0], static_cast< DWORD >( chunk.size() ), &got ) )
            return -1;
        if ( got == 0 )
            break;
        int const room = maxlen - static_cast< int >( body.size() );
        body.append( &chunk[0], std::min( static_cast< int >( got ), room ) );
    }
    return static_cast< int >( status );
}

#elif defined( HAVE_LIBCURL )

#include <curl/curl.h>

namespace
{
    struct st_CurlTarget
    {
        std::string * body;
        size_t maxlen;
    };

    size_t st_CurlWrite( char * data, size_t size, size_t count, void * user )
    {
        st_CurlTarget * const target = static_cast< st_CurlTarget * >( user );
        size_t const n = size * count;
        size_t const room = target->maxlen > target->body->size() ? target->maxlen - target->body->size() : 0;
        target->body->append( data, std::min( n, room ) );
        return n;
    }
}

bool st_HttpsAvailable()
{
    return true;
}

void st_HttpsInit()
{
    static bool done = false;
    if ( !done )
    {
        curl_global_init( CURL_GLOBAL_DEFAULT );
        done = true;
    }
}

int st_HttpsGet( std::string const & host, std::string const & path, std::string & body,
                 int maxlen, double budget )
{
    body.clear();
    if ( budget <= 0 )
        return -1;

    CURL * curl = curl_easy_init();
    if ( !curl )
        return -1;

    std::string const url = "https://" + host + path;
    st_CurlTarget target = { &body, static_cast< size_t >( maxlen ) };
    curl_slist * headers = curl_slist_append( NULL, "Accept: application/json" );
    curl_easy_setopt( curl, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, "Retrocycles RCL" );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( curl, CURLOPT_MAXREDIRS, 3L );
    curl_easy_setopt( curl, CURLOPT_PROTOCOLS, static_cast< long >( CURLPROTO_HTTPS ) );
    curl_easy_setopt( curl, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT_MS, static_cast< long >( budget * 1000 ) );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, &st_CurlWrite );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &target );

    long status = -1;
    if ( curl_easy_perform( curl ) == CURLE_OK )
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &status );
    else
        status = -1;

    curl_slist_free_all( headers );
    curl_easy_cleanup( curl );
    return static_cast< int >( status );
}

#else

bool st_HttpsAvailable()
{
    return false;
}

void st_HttpsInit()
{
}

int st_HttpsGet( std::string const &, std::string const &, std::string & body, int, double )
{
    body.clear();
    return -1;
}

#endif
