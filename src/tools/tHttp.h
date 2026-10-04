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

#ifndef ArmageTron_HTTP_H
#define ArmageTron_HTTP_H

#include <iosfwd>
#include <string>

//! called between short waits of st_PlainHttpGet; return false to give up
typedef bool tHttpIdle();

//! Plain HTTP/1.0 GET of http://host[:port]path with a time budget in
//! seconds. Writes at most maxlen body bytes to target and returns the HTTP
//! status, or -1 when the request could not be completed. No TLS, no
//! redirects. This exists because libxml2 2.15 dropped its HTTP client.
int st_PlainHttpGet( std::string host, std::string const & path, std::ostream & target, int maxlen,
                     double budget, tHttpIdle * idle = 0 );

#endif
