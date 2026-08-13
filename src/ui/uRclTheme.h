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
    REAL MenuTop();
    REAL MenuBottom();
    REAL MenuLeft();
    REAL MenuRight();
    REAL LabelX();
    REAL ValueOffset();
    REAL RowPitch();
    REAL RowHalfHeight();
    REAL TextWidth();
    REAL TextHeight();
    REAL PromptTop();
    REAL PromptBottom();
    REAL PromptRowY();

    REAL EaseIn(REAL progress);
    tString FirstLine(tString const &text);

    void DrawBackground(bool full, REAL alpha);
    void DrawChrome(bool full, tString const &title, REAL alpha);
    void DrawSelection(REAL y, REAL alpha);
    void DrawHelp(tString const &help, REAL alpha);
    void DrawPromptBackground(REAL alpha);
    void DrawPromptChrome(tString const &title, REAL alpha);
    void DrawPromptSelection(REAL y, REAL alpha);
    void DrawDialog(tString const &title, REAL alpha);

    void SetLabelColor(bool selected, REAL alpha);
    void SetValueColor(bool selected, REAL alpha);
}

#endif
