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
// the screen through the post chain: the glow of what gives off light
// (bloom) and the view transform (tone mapping), as Blender's renders have
// them. HUD, console, scores and menus are drawn after it, onto the screen,
// unchanged. With RCL_RENDER 0, or for the rest of the session once anything
// it needs could not be made, scenes are drawn straight onto the screen as
// they always were.
namespace rPost
{
    //! Starts a 3D scene in the viewport GL has now. Returns whether it is
    //! drawn into the scene picture; either way EndScene() must follow.
    bool BeginScene();
    //! Puts the scene on the screen in the same viewport, through the post chain.
    void EndScene();

    //! whether what is drawn now goes into a scene picture, and so through
    //! the post chain
    bool InScene();
    //! counts the scenes begun, so what holds for one scene can be kept for it
    unsigned SceneSerial();
    //! For what is drawn over a scene rather than in it (the names over the
    //! cycles): f is called once, right after the scene being drawn is on the
    //! screen, in its viewport. Only while InScene().
    void AfterScene( void ( *f )() );

    //! whether the scene being drawn can hold light over 1, so that what
    //! rEmissive draws glows
    bool Glows();
    //! whether drawing at this intensity would add anything: Glows(), and over 1
    bool Glows( REAL intensity );

    //! 1 if the last scene went through the new renderer, else 0
    int Path();
    //! whether files the renderer reads are to be read again when they change (RCL_SHADER_RELOAD)
    bool ReloadWanted();
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

//! While one of these lives, what is drawn gives off light: its colour times
//! intensity, in linear light the way Blender's emission strength is, so that
//! over 1 it goes into the glow. Only in a scene that can hold such light
//! (rPost::Glows()), and never into a display list being recorded: there it
//! does nothing and things are drawn in their plain colour.
class rEmissive
{
public:
    explicit rEmissive( REAL intensity );
    ~rEmissive();
    bool On() const { return on_; }
private:
    rEmissive( rEmissive const & );
    rEmissive & operator=( rEmissive const & );
    bool on_;
};

#endif // DEDICATED

#endif
