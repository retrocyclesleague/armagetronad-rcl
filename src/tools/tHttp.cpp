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

#include "tHttp.h"

#include <ostream>
#include <chrono>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <winsock.h>
typedef SOCKET st_HttpSocket;
typedef int st_HttpLength;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
typedef int st_HttpSocket;
typedef socklen_t st_HttpLength;
#endif

namespace
{
    // tSysTimeFloat() only moves when the main loop advances a frame, which
    // does not happen while a request blocks; budgets need a real clock.
    double st_HttpTime()
    {
        return std::chrono::duration< double >(
                   std::chrono::steady_clock::now().time_since_epoch() ).count();
    }

    void st_HttpClose( st_HttpSocket s )
    {
#ifdef _WIN32
        closesocket( s );
#else
        close( s );
#endif
    }

    // wait until the socket is readable/writable, has failed, time is up or
    // the idle callback gives up. With a callback, the wait is cut into short
    // slices so the caller can draw a frame and read input in between.
    bool st_HttpWait( st_HttpSocket s, bool write, double deadline, tHttpIdle * idle )
    {
        while ( true )
        {
            double left = deadline - st_HttpTime();
            if ( left <= 0 )
                return false;
            if ( idle && left > .02 )
                left = .02;

            fd_set ready, failed;
            FD_ZERO( &ready );
            FD_SET( s, &ready );
            failed = ready;
            timeval timeout;
            timeout.tv_sec = static_cast< long >( left );
            timeout.tv_usec = static_cast< long >( ( left - timeout.tv_sec ) * 1000000 );
            int const selected = select( static_cast< int >( s ) + 1, write ? NULL : &ready,
                                         write ? &ready : NULL, &failed, &timeout );
            if ( selected != 0 )
                return selected > 0;
            if ( idle && !(*idle)() )
                return false;
        }
    }
}

int st_PlainHttpGet( std::string host, std::string const & path, std::ostream & target, int maxlen,
                 double budget, tHttpIdle * idle )
{
    if ( budget <= 0 )
        return -1;

#ifdef _WIN32
    static bool started = false;
    if ( !started )
    {
        WSADATA data;
        WSAStartup( MAKEWORD( 1, 1 ), &data );
        started = true;
    }
#endif

    int port = 80;
    std::string::size_type const colon = host.find( ':' );
    if ( colon != std::string::npos )
    {
        port = atoi( host.c_str() + colon + 1 );
        host.erase( colon );
    }

    hostent const * entry = gethostbyname( host.c_str() );
    if ( !entry || entry->h_addrtype != AF_INET || !entry->h_addr_list[0] )
        return -1;

    sockaddr_in address;
    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( static_cast< unsigned short >( port ) );
    memcpy( &address.sin_addr, entry->h_addr_list[0], sizeof( address.sin_addr ) );

    st_HttpSocket s = socket( AF_INET, SOCK_STREAM, 0 );
#ifdef _WIN32
    if ( s == INVALID_SOCKET )
        return -1;
    unsigned long nonblocking = 1;
    ioctlsocket( s, FIONBIO, &nonblocking );
#else
    if ( s < 0 )
        return -1;
    fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
#endif

    double const deadline = st_HttpTime() + budget;
    connect( s, reinterpret_cast< sockaddr * >( &address ), sizeof( address ) );

    int error = 0;
    st_HttpLength errorLength = sizeof( error );
    if ( !st_HttpWait( s, true, deadline, idle ) ||
         getsockopt( s, SOL_SOCKET, SO_ERROR, reinterpret_cast< char * >( &error ), &errorLength ) != 0 ||
         error != 0 )
    {
        st_HttpClose( s );
        return -1;
    }

    std::string const request = "GET " + path + " HTTP/1.0\r\nHost: " + host +
        "\r\nUser-Agent: armagetronad-rcl\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    std::string::size_type sent = 0;
    while ( sent < request.size() )
    {
        int const count = st_HttpWait( s, true, deadline, idle )
            ? send( s, request.data() + sent, static_cast< int >( request.size() - sent ), 0 )
            : -1;
        if ( count <= 0 )
        {
            st_HttpClose( s );
            return -1;
        }
        sent += count;
    }

    std::string response;
    std::string::size_type const cap = static_cast< std::string::size_type >( maxlen > 0 ? maxlen : 0 ) + 16384;
    char buffer[2048];
    while ( response.size() < cap && st_HttpWait( s, false, deadline, idle ) )
    {
        int const count = recv( s, buffer, sizeof( buffer ), 0 );
        if ( count <= 0 )
            break;
        response.append( buffer, count );
    }
    st_HttpClose( s );

    // "HTTP/1.1 200 OK", headers, blank line, body
    std::string::size_type const space = response.find( ' ' );
    std::string::size_type body = response.find( "\r\n\r\n" );
    if ( response.compare( 0, 5, "HTTP/" ) != 0 || space == std::string::npos ||
         body == std::string::npos || space > body )
        return -1;
    body += 4;

    std::string::size_type length = response.size() - body;
    if ( maxlen < 0 )
        maxlen = 0;
    if ( length > static_cast< std::string::size_type >( maxlen ) )
        length = maxlen;
    target.write( response.data() + body, length );
    return atoi( response.c_str() + space + 1 );
}
