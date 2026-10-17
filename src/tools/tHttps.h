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

#ifndef ArmageTron_HTTPS_H
#define ArmageTron_HTTPS_H

#include <string>

//! Whether this build can make HTTPS requests at all: always on Windows
//! (WinHTTP), elsewhere when it was built with libcurl.
bool st_HttpsAvailable();

//! Call once on the main thread before the first request from any thread.
void st_HttpsInit();

//! GET https://host path. The system checks the certificate; redirects are
//! followed. Writes at most maxlen body bytes to body and returns the HTTP
//! status, or -1 when there was no answer within budget seconds. Blocks, so
//! it is for threads of their own; it shares nothing between calls.
int st_HttpsGet( std::string const & host, std::string const & path, std::string & body,
                 int maxlen, double budget );

#endif
