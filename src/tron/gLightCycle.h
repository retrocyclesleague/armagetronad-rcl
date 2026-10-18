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

#ifndef ArmageTron_LIGHTCYCLE_H
#define ArmageTron_LIGHTCYCLE_H

#include "defs.h"

#ifndef DEDICATED
class eCoord;

//! The clean arena's light cycle, drawn in a cycle's own frame (x ahead, half
//! size, where the classic body and wheels would be: see sg_CleanCycleFrame in
//! gCycle.cpp), facing dir in the arena, with the colour its bike shows. It
//! has the classic cycle's place and size: 3.02 long, 1.30 high and .68 wide
//! in that frame, its wheels on the floor where the classic cycle's stand.
void sg_RenderLightCycle( eCoord const & dir, REAL r, REAL g, REAL b );
#endif

#endif
