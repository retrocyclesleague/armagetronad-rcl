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

#include "uRclTheme.h"

#include <algorithm>

#ifndef DEDICATED
#include "rFont.h"
#include "rRender.h"
#include "rScreen.h"
#endif

namespace uRclTheme
{
    namespace
    {
        static REAL const menuTop = .58f;
        static REAL const menuBottom = -.66f;
        static REAL const menuLeft = -.86f;
        static REAL const menuRight = .86f;
        static REAL const labelX = -.78f;
        static REAL const valueOffset = .80f;
        static REAL const rowPitch = .105f;
        static REAL const rowHalfHeight = .048f;
        static REAL const textWidth = .039f;
        static REAL const textHeight = .084f;
        static REAL const promptTop = -.70f;
        static REAL const promptBottom = -.90f;
        static REAL const promptRowY = -.80f;

#ifndef DEDICATED
        void DisableTexture()
        {
            RenderEnd(true);
            glDisable(GL_TEXTURE_2D);
        }

        void DrawQuad(REAL left, REAL right, REAL bottom, REAL top,
                      REAL r, REAL g, REAL b, REAL alpha)
        {
            DisableTexture();
            BeginQuads();
            Color(r, g, b, alpha);
            Vertex(left, bottom);
            Vertex(right, bottom);
            Vertex(right, top);
            Vertex(left, top);
            RenderEnd();
        }

        void DrawLine(REAL x1, REAL y1, REAL x2, REAL y2,
                      REAL r, REAL g, REAL b, REAL alpha)
        {
            DisableTexture();
            BeginLines();
            Color(r, g, b, alpha);
            Vertex(x1, y1);
            Vertex(x2, y2);
            RenderEnd();
        }

        void DrawText(REAL x, REAL y, REAL width, REAL height,
                      char const *text)
        {
            ::DisplayText(x, y,
                          width * rTextField::AspectWidthMultiplier(),
                          height, text, -1, 0, 0,
                          rTextField::COLOR_USE);
        }

        void DrawTextFitted(REAL x, REAL right, REAL y,
                            REAL width, REAL height, char const *text)
        {
            int const length = std::max(1,
                tColoredString::RemoveColors(text).Len() - 1);
            REAL const available = right - x;
            if (available > 0 && length * width > available)
                width = available / length;
            DrawText(x, y, width, height, text);
        }

        void SetTextColor(REAL r, REAL g, REAL b, REAL alpha)
        {
            rTextField::SetDefaultColor(tColor(r, g, b, alpha));
            rTextField::SetBlendColor(tColor(1, 1, 1, alpha));
        }

        void DrawGrid(REAL alpha)
        {
            DisableTexture();
            BeginLines();
            Color(.20f, .24f, .27f, alpha);
            for (REAL x = -1.0f; x <= 1.001f; x += .10f)
            {
                Vertex(x, -1.0f);
                Vertex(x, 1.0f);
            }
            for (REAL y = -1.0f; y <= 1.001f; y += .10f)
            {
                Vertex(-1.0f, y);
                Vertex(1.0f, y);
            }
            RenderEnd();
        }

        void DrawCornerMarks(REAL alpha)
        {
            static REAL const length = .025f;
            DisableTexture();
            BeginLines();
            Color(1, 1, 0, alpha);

            Vertex(menuLeft, .66f); Vertex(menuLeft + length, .66f);
            Vertex(menuLeft, .66f); Vertex(menuLeft, .66f - length);
            Vertex(menuRight, .66f); Vertex(menuRight - length, .66f);
            Vertex(menuRight, .66f); Vertex(menuRight, .66f - length);
            Vertex(menuLeft, -.88f); Vertex(menuLeft + length, -.88f);
            Vertex(menuLeft, -.88f); Vertex(menuLeft, -.88f + length);
            Vertex(menuRight, -.88f); Vertex(menuRight - length, -.88f);
            Vertex(menuRight, -.88f); Vertex(menuRight, -.88f + length);
            RenderEnd();
        }
#endif
    }

    REAL MenuTop() { return menuTop; }
    REAL MenuBottom() { return menuBottom; }
    REAL MenuLeft() { return menuLeft; }
    REAL MenuRight() { return menuRight; }
    REAL LabelX() { return labelX; }
    REAL ValueOffset() { return valueOffset; }
    REAL RowPitch() { return rowPitch; }
    REAL RowHalfHeight() { return rowHalfHeight; }
    REAL TextWidth() { return textWidth; }
    REAL TextHeight() { return textHeight; }
    REAL PromptTop() { return promptTop; }
    REAL PromptBottom() { return promptBottom; }
    REAL PromptRowY() { return promptRowY; }

    REAL EaseIn(REAL progress)
    {
        if (progress <= 0)
            return 0;
        if (progress >= 1)
            return 1;
        REAL inverse = 1 - progress;
        return 1 - inverse * inverse * inverse;
    }

    tString FirstLine(tString const &text)
    {
        tString line;
        for (int i = 0; i < text.Len() && text[i] != '\0'; ++i)
        {
            if (text[i] == '\n' || text[i] == '\r')
                break;
            line << text[i];
        }
        return line;
    }

    void DrawBackground(bool full, REAL alpha)
    {
#ifndef DEDICATED
        if (full)
        {
            DrawQuad(-1, 1, -1, 1, .008f, .012f, .016f, .90f * alpha);
            DrawGrid(.16f * alpha);
        }
        else
        {
            DrawQuad(-1, 1, -1, 1, 0, 0, 0, .28f * alpha);
            DrawGrid(.07f * alpha);
        }

        DrawQuad(menuLeft, menuRight, -.88f, .66f,
                 .015f, .022f, .028f, (full ? .82f : .76f) * alpha);
        DrawCornerMarks(.75f * alpha);
#else
        (void)full;
        (void)alpha;
#endif
    }

    void DrawChrome(bool full, tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        SetTextColor(1, 1, 0, alpha);
        DrawText(labelX, .80f, .022f, .050f, "RETROCYCLES LEAGUE");

        SetTextColor(.95f, .97f, 1, alpha);
        DrawTextFitted(labelX, .43f, .70f, .050f, .105f, title);

        SetTextColor(.42f, .48f, .52f, alpha);
        DrawText(.48f, .79f, .018f, .042f,
                 full ? "RCL CLIENT" : "MENU OVERLAY");

        DrawLine(menuLeft, .615f, menuRight, .615f,
                 1, 1, 0, .55f * alpha);
        DrawLine(labelX + valueOffset - .065f, menuBottom + .04f,
                 labelX + valueOffset - .065f, .54f,
                 0, .78f, .86f, .28f * alpha);

        DrawLine(menuLeft, -.705f, menuRight, -.705f,
                 .28f, .34f, .38f, .7f * alpha);
        SetTextColor(.40f, .48f, .52f, alpha);
        DrawText(labelX, -.845f, .018f, .042f,
                 "MOUSE // ARROWS // ENTER // ESC");
#else
        (void)full;
        (void)title;
        (void)alpha;
#endif
    }

    void DrawSelection(REAL y, REAL alpha)
    {
#ifndef DEDICATED
        REAL const left = menuLeft + .025f;
        REAL const right = menuRight - .025f;
        REAL const bottom = y - rowHalfHeight;
        REAL const top = y + rowHalfHeight;
        REAL const corner = .018f;

        DrawQuad(left, right, bottom, top, 1, 1, 0, .055f * alpha);

        DisableTexture();
        BeginLines();
        Color(1, 1, 0, alpha);
        Vertex(left, top); Vertex(left + corner, top);
        Vertex(left, top); Vertex(left, top - corner);
        Vertex(right, top); Vertex(right - corner, top);
        Vertex(right, top); Vertex(right, top - corner);
        Vertex(left, bottom); Vertex(left + corner, bottom);
        Vertex(left, bottom); Vertex(left, bottom + corner);
        Vertex(right, bottom); Vertex(right - corner, bottom);
        Vertex(right, bottom); Vertex(right, bottom + corner);
        RenderEnd();
#else
        (void)y;
        (void)alpha;
#endif
    }

    void DrawHelp(tString const &help, REAL alpha)
    {
#ifndef DEDICATED
        tString first = FirstLine(help);
        if (first.Len() <= 0)
            return;

        tString second;
        bool afterBreak = false;
        for (int i = 0; i < help.Len() && help[i] != '\0'; ++i)
        {
            if (!afterBreak)
            {
                if (help[i] == '\n' || help[i] == '\r')
                    afterBreak = true;
                continue;
            }
            if (help[i] == '\n' || help[i] == '\r')
                break;
            second << help[i];
        }

        SetTextColor(0, .82f, .88f, alpha);
        DrawText(labelX, -.752f, .021f, .050f, "//");
        SetTextColor(.72f, .76f, .79f, alpha);
        DrawTextFitted(labelX + .075f, menuRight - .03f, -.752f,
                       .024f, .050f, first);
        if (second.Len() > 1)
        {
            SetTextColor(.48f, .57f, .62f, alpha);
            DrawTextFitted(labelX + .075f, menuRight - .03f, -.815f,
                           .019f, .040f, second);
        }
#else
        (void)help;
        (void)alpha;
#endif
    }

    void DrawPromptBackground(REAL alpha)
    {
#ifndef DEDICATED
        // Chat and console entry stay anchored at the bottom of gameplay, but
        // now read as a compact member of the same RCL interface family.
        DrawQuad(-1, 1, -.94f, -.62f, 0, 0, 0, .72f * alpha);
        DrawQuad(menuLeft, menuRight, -.91f, -.66f,
                 .015f, .022f, .028f, .90f * alpha);
        DrawLine(menuLeft, -.66f, menuRight, -.66f,
                 1, 1, 0, .46f * alpha);
        DrawLine(menuLeft, -.91f, menuRight, -.91f,
                 0, .78f, .86f, .34f * alpha);
#else
        (void)alpha;
#endif
    }

    void DrawPromptChrome(tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        SetTextColor(1, 1, 0, alpha);
        DrawText(labelX, -.705f, .018f, .042f, "RCL // INPUT");

        if (title.Len() > 1)
        {
            SetTextColor(.72f, .76f, .79f, alpha);
            DrawTextFitted(.38f, menuRight - .03f, -.705f,
                           .018f, .042f, title);
        }
#else
        (void)title;
        (void)alpha;
#endif
    }

    void DrawPromptSelection(REAL y, REAL alpha)
    {
#ifndef DEDICATED
        REAL const left = menuLeft + .025f;
        REAL const right = menuRight - .025f;
        DrawQuad(left, right, y - rowHalfHeight, y + rowHalfHeight,
                 0, .72f, .82f, .075f * alpha);
        DrawLine(left, y + rowHalfHeight, right, y + rowHalfHeight,
                 0, .84f, .92f, .60f * alpha);
#else
        (void)y;
        (void)alpha;
#endif
    }

    void DrawDialog(tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        DrawBackground(true, alpha);
        DrawChrome(true, title, alpha);
        SetTextColor(.40f, .48f, .52f, alpha);
        DrawText(.49f, -.845f, .018f, .042f,
                 "UP // DOWN // ESC");
#else
        (void)title;
        (void)alpha;
#endif
    }

    void SetLabelColor(bool selected, REAL alpha)
    {
#ifndef DEDICATED
        if (selected)
            SetTextColor(1, 1, 0, alpha);
        else
            SetTextColor(.88f, .91f, .93f, alpha);
#else
        (void)selected;
        (void)alpha;
#endif
    }

    void SetValueColor(bool selected, REAL alpha)
    {
#ifndef DEDICATED
        if (selected)
            SetTextColor(0, .92f, 1, alpha);
        else
            SetTextColor(.50f, .62f, .67f, alpha);
#else
        (void)selected;
        (void)alpha;
#endif
    }
}
