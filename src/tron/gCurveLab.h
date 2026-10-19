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

#ifndef ArmageTron_CURVE_LAB_H
#define ArmageTron_CURVE_LAB_H

//! The curve lab: curved trails tried out on a floor of the lab's own, with
//! the light cycle mesh riding them. No server, no game objects, nothing
//! that hits and nothing recorded; the game itself is untouched. Escape
//! leaves.
void sg_CurveLab();

//! Whether the lab is to open as the main menu first shows
//! (RCL_CURVE_LAB_START 1, for the test harness): true once, then never again.
bool sg_CurveLabAtStart();

#endif
