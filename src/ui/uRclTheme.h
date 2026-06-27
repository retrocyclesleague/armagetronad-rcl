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

#ifndef ArmageTron_RCL_THEME_H
#define ArmageTron_RCL_THEME_H

#include "defs.h"
#include "tString.h"

namespace uRclTheme
{
    REAL MenuLeft();
    REAL MenuRight();
    REAL MenuLabelX();
    REAL MenuDescX();
    REAL MenuDescRight();
    REAL TextW();
    REAL TextH();
    REAL RowHalfH();

    inline REAL FullMenuTop() { return 0.28f; }
    inline REAL FullMenuBot() { return -0.82f; }
    inline REAL MenuTop() { return 0.48f; }
    inline REAL MenuBot() { return -0.78f; }
    inline bool ShowInlineDesc( bool selected ) { return selected; }

    tString FirstLine( tString const & text );

    void DrawBackground();
    void DrawGridOverlay( REAL alpha );
    void DrawFullChrome();
    void DrawBracketSelection( REAL left, REAL right, REAL y );
    void DrawCornerPluses( REAL left, REAL right, REAL bot, REAL top );

    void SetLabelColor( bool selected, REAL alpha );
    void SetDescColor( bool selected, REAL alpha );
    void DrawLabel( REAL x, REAL y, char const * text, bool selected, REAL alpha );
    void DrawDesc( REAL x, REAL y, char const * text, bool selected, REAL alpha );
    void DrawHeaderTitle( tString const & title );
}

#endif
