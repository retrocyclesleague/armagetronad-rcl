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

#ifndef ArmageTron_CYCLEMESH_H
#define ArmageTron_CYCLEMESH_H

#include "defs.h"

#ifndef DEDICATED
class eCoord;

//! Where a cycle stands, for the mesh: the frame the light cycle is drawn in
//! (gLightCycle: at the cycle's place, half size, x ahead, leaning as it
//! leans), as a matrix to the arena, column by column; and how far it has
//! come, which turns its wheels.
struct gCycleMeshPose
{
    float frame[16];
    REAL distance;
};

//! the pose of a cycle at pos facing dir, leaning by the angle whose cosine
//! and sine are given (1, 0 upright), distance travelled so far
void sg_CycleMeshPose( gCycleMeshPose & pose, eCoord const & pos, eCoord const & dir,
                       REAL leanCos, REAL leanSin, REAL distance );

//! Draws a cycle as the light cycle mesh (RCL_CYCLE_MODEL 2) in the colour
//! its bike shows (sg_BikeColor), with GL's modelview at the pose's frame,
//! as for sg_RenderLightCycle. It stands where the light cycle stands: the
//! same nose, the same tail the wall leaves, the same length. Returns false
//! and draws nothing where it cannot: the new renderer is not drawing this
//! scene, the mesh or its shader could not be had, or the cycle is too small
//! on the screen for it; the light cycle is drawn then.
bool sg_RenderCycleMesh( gCycleMeshPose const & pose, REAL r, REAL g, REAL b );

//! whether the mesh is drawn in the scene being drawn now (the new renderer
//! draws it, the mesh and its shader are there)
bool sg_CycleMeshDraws();

//! how far behind the cycle, in the arena, the mesh's tail is where it is placed
REAL sg_CycleMeshTailGap();
#endif

#endif
