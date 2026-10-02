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

        // retrocyclesleague.com tokens: neutral greys and a single lime
        // accent (--accent #e8ff47). Keep every theme colour here.
        struct Rgb { REAL r, g, b; };
        static Rgb const accent    = { .910f, 1.0f, .278f };
        static Rgb const backdrop  = { .020f, .020f, .020f };
        static Rgb const panel     = { .039f, .039f, .039f };
        static Rgb const line      = { .26f, .26f, .26f };
        static Rgb const text      = { .98f, .98f, .98f };
        static Rgb const textSoft  = { .86f, .86f, .86f };
        static Rgb const textMuted = { .63f, .63f, .63f };
        static Rgb const textDim   = { .45f, .45f, .45f };

#ifndef DEDICATED
        void DisableTexture()
        {
            RenderEnd(true);
            glDisable(GL_TEXTURE_2D);
        }

        void DrawQuad(REAL left, REAL right, REAL bottom, REAL top,
                      Rgb const &c, REAL alpha)
        {
            // Without blending a tint would render as a solid block.
            if (!sr_alphaBlend && alpha < .5f)
                return;
            DisableTexture();
            BeginQuads();
            Color(c.r, c.g, c.b, alpha);
            Vertex(left, bottom);
            Vertex(right, bottom);
            Vertex(right, top);
            Vertex(left, top);
            RenderEnd();
        }

        void DrawLine(REAL x1, REAL y1, REAL x2, REAL y2,
                      Rgb const &c, REAL alpha)
        {
            DisableTexture();
            BeginLines();
            Color(c.r, c.g, c.b, alpha);
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

        void SetTextColor(Rgb const &c, REAL alpha)
        {
            rTextField::SetDefaultColor(tColor(c.r, c.g, c.b, alpha));
            rTextField::SetBlendColor(tColor(1, 1, 1, alpha));
        }

        void DrawGrid(REAL alpha)
        {
            DisableTexture();
            BeginLines();
            Color(line.r, line.g, line.b, alpha);
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
            Color(accent.r, accent.g, accent.b, alpha);

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

    namespace
    {
        // Set while a frame is drawn, read by the next frame's chrome.
        static bool valueColumnNoted = false;
        static bool valueColumnShown = false;
        static bool sceneNoted = false;
    }

    void NoteValueColumn() { valueColumnNoted = true; }
    void NoteSceneBehind() { sceneNoted = true; }

    tString Lower(char const *text)
    {
        tString out;
        if (!text)
            return out;
        for (int i = 0; text[i] != '\0'; ++i)
        {
            if (text[i] == '0' && text[i + 1] == 'x')
            {
                // keep the colour code, including its hex digits, verbatim
                for (int n = 0; n < 8 && text[i] != '\0'; ++n, ++i)
                    out << text[i];
                --i;
                continue;
            }
            char ch = text[i];
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
            out << ch;
        }
        return out;
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
        // One panel everywhere. Over a live scene (a game, or the menu
        // replay) it stays translucent; with nothing behind it, the backdrop
        // closes up and carries the grid instead.
        bool const scene = !full || sceneNoted;
        sceneNoted = false;
        if (scene)
        {
            DrawQuad(-1, 1, -1, 1, backdrop, .30f * alpha);
        }
        else
        {
            DrawQuad(-1, 1, -1, 1, backdrop, .90f * alpha);
            DrawGrid(.16f * alpha);
        }

        DrawQuad(menuLeft, menuRight, -.88f, .66f,
                 panel, (scene ? .66f : .82f) * alpha);
        DrawCornerMarks(.75f * alpha);
#else
        (void)full;
        (void)alpha;
#endif
    }

    namespace
    {
        static char const *footerHint = "mouse // arrows // enter // esc";
    }

    void DrawChrome(bool full, tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        SetTextColor(accent, alpha);
        DrawText(labelX, .80f, .022f, .050f, "retrocycles league");

        SetTextColor(text, alpha);
        tString const heading = Lower(title);
        if (heading.Len() <= 34)
        {
            DrawTextFitted(labelX, menuRight - .03f, .70f, .050f, .105f,
                           heading);
        }
        else
        {
            // A sentence, not a name (a sign-in request, for instance): set
            // it as wrapped copy rather than squeezing it into one line.
            REAL const width = .024f * rTextField::AspectWidthMultiplier();
            rTextField field(labelX, .748f, width, .050f);
            field.SetWidth(std::max(8, static_cast<int>(
                (menuRight - .03f - labelX) / field.GetCWidth())));
            field << heading;
        }

        DrawLine(menuLeft, .615f, menuRight, .615f,
                 accent, .55f * alpha);

        // The column rule belongs to label/value rows only.
        valueColumnShown = valueColumnNoted;
        valueColumnNoted = false;
        if (valueColumnShown)
            DrawLine(labelX + valueOffset - .065f, menuBottom + .04f,
                     labelX + valueOffset - .065f, .54f,
                     line, .7f * alpha);

        DrawLine(menuLeft, -.705f, menuRight, -.705f,
                 line, .7f * alpha);
        SetTextColor(textDim, alpha);
        DrawText(labelX, -.845f, .018f, .042f, footerHint);
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

        DrawQuad(left, right, bottom, top, accent, .055f * alpha);

        DisableTexture();
        BeginLines();
        Color(accent.r, accent.g, accent.b, alpha);
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
        if (FirstLine(help).Len() <= 0)
            return;

        SetTextColor(accent, alpha);
        DrawText(labelX, -.752f, .021f, .050f, "//");

        // Word-wrap into the two rows of the footer band; cut what does
        // not fit rather than letting it run over the key hints.
        REAL const left = labelX + .075f;
        REAL const width = .020f * rTextField::AspectWidthMultiplier();
        SetTextColor(textMuted, alpha);
        rTextField field(left, -.725f, width, .046f);
        int const columns = std::max(8, static_cast<int>(
            (menuRight - .03f - left) / field.GetCWidth()));
        field.SetWidth(columns);

        tString const text = Lower(help);
        tString shown;
        int column = 0, row = 0;
        for (int i = 0; i < text.Len() && text[i] != '\0'; ++i)
        {
            bool const newline = text[i] == '\n' || text[i] == '\r';
            if (newline || column >= columns)
            {
                if (++row >= 2)
                    break;
                column = 0;
            }
            if (!newline)
                ++column;
            shown << text[i];
        }
        field << shown;
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
        DrawQuad(-1, 1, -.94f, -.62f, backdrop, .72f * alpha);
        DrawQuad(menuLeft, menuRight, -.91f, -.66f,
                 panel, .90f * alpha);
        DrawLine(menuLeft, -.66f, menuRight, -.66f,
                 accent, .46f * alpha);
        DrawLine(menuLeft, -.91f, menuRight, -.91f,
                 line, .7f * alpha);
#else
        (void)alpha;
#endif
    }

    void DrawPromptChrome(tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        SetTextColor(accent, alpha);
        DrawText(labelX, -.705f, .018f, .042f, "rcl // input");

        if (title.Len() > 1)
        {
            SetTextColor(textMuted, alpha);
            DrawTextFitted(.38f, menuRight - .03f, -.705f,
                           .018f, .042f, Lower(title));
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
                 accent, .055f * alpha);
        DrawLine(left, y + rowHalfHeight, right, y + rowHalfHeight,
                 accent, .60f * alpha);
#else
        (void)y;
        (void)alpha;
#endif
    }

    void DrawDialog(tString const &title, REAL alpha)
    {
#ifndef DEDICATED
        valueColumnNoted = false;
        DrawBackground(true, alpha);
        char const *menuHint = footerHint;
        footerHint = "any key to continue // up // down // esc";
        DrawChrome(true, title, alpha);
        footerHint = menuHint;
#else
        (void)title;
        (void)alpha;
#endif
    }

    void SetLabelColor(bool selected, REAL alpha)
    {
#ifndef DEDICATED
        if (selected)
            SetTextColor(accent, alpha);
        else
            SetTextColor(textSoft, alpha);
#else
        (void)selected;
        (void)alpha;
#endif
    }

    void SetBodyColor(REAL alpha)
    {
#ifndef DEDICATED
        SetTextColor(textSoft, alpha);
#else
        (void)alpha;
#endif
    }

    void SetScrollMarkColor(REAL alpha)
    {
#ifndef DEDICATED
        Color(accent.r, accent.g, accent.b, alpha);
#else
        (void)alpha;
#endif
    }

    void SetValueColor(bool selected, REAL alpha)
    {
#ifndef DEDICATED
        if (selected)
            SetTextColor(text, alpha);
        else
            SetTextColor(textMuted, alpha);
#else
        (void)selected;
        (void)alpha;
#endif
    }
}
