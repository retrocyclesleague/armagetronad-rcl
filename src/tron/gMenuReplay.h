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

#ifndef ArmageTron_MENU_REPLAY_H
#define ArmageTron_MENU_REPLAY_H

//! The anonymous match replay that plays behind the out-of-game menus.
namespace gMenuReplay
{
    //! Advances and draws the replay full screen. Returns false, drawing
    //! nothing, when it is switched off or its data file is missing.
    bool Render();
}

#endif
