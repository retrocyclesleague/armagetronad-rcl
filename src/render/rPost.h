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

#ifndef ArmageTron_POST_H
#define ArmageTron_POST_H

#include "defs.h"

#ifndef DEDICATED

// The new renderer (RCL_RENDER 1). Each 3D scene, the world of a viewport,
// the menu's replay or the colour studio's preview, is drawn into a picture
// of its own with the same fixed-function drawing as before, and then put on
// the screen. HUD, console, scores and menus are drawn after it, onto the
// screen, unchanged. With RCL_RENDER 0, or for the rest of the session once
// anything it needs could not be made, scenes are drawn straight onto the
// screen as they always were.
namespace rPost
{
    //! Starts a 3D scene in the viewport GL has now. Returns whether it is
    //! drawn into the scene picture; either way EndScene() must follow.
    bool BeginScene();
    //! Puts the scene on the screen in the same viewport.
    void EndScene();

    //! 1 if the last scene went through the new renderer, else 0
    int Path();
    //! a line saying what the renderer does, for the overlay
    char const * Status();
}

//! A 3D scene for the length of a block: see rPost::BeginScene().
class rScene
{
public:
    rScene(){ rPost::BeginScene(); }
    ~rScene(){ rPost::EndScene(); }
private:
    rScene( rScene const & );
    rScene & operator=( rScene const & );
};

#endif // DEDICATED

#endif
