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

#include "gMenuReplay.h"

// DEDICATED comes from config.h
#include "defs.h"

#ifdef DEDICATED

bool gMenuReplay::Render() { return false; }

#else

#include "rSDL.h"

#include "defs.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "tDirectories.h"
#include "tSysTime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

static bool sg_menuReplay = true;
static tConfItem<bool> sg_menuReplayConf("MENU_REPLAY", sg_menuReplay);

namespace
{
    // The replay is drawn from data only: timed corner points per cycle life,
    // written by scripts/generate-menu-replay.py. It carries no names.
    char const * const replayFile = "replays/menu_fort.rclreplay";

    float const wallLength   = 400;   // trail length kept behind a cycle
    float const wallHeight   = 5;
    float const wallFade     = 3;     // seconds a dead cycle's trail lingers
    float const roundLeadOut = 1.5f;
    float const blendTime    = 3.5f;  // length of a swoop between two shots
    float const pi           = 3.14159265f;

    struct Vec
    {
        float x, y, z;
    };

    Vec operator+(Vec a, Vec b) { Vec r = { a.x + b.x, a.y + b.y, a.z + b.z }; return r; }
    Vec operator-(Vec a, Vec b) { Vec r = { a.x - b.x, a.y - b.y, a.z - b.z }; return r; }
    Vec operator*(Vec a, float f) { Vec r = { a.x * f, a.y * f, a.z * f }; return r; }

    Vec Cross(Vec a, Vec b)
    {
        Vec r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
        return r;
    }

    float Length(Vec a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

    Vec Normalized(Vec a, Vec fallback)
    {
        float const length = Length(a);
        return length > 1E-4f ? a * (1 / length) : fallback;
    }

    Vec Mix(Vec a, Vec b, float f) { return a + (b - a) * f; }

    float Smooth(float f)
    {
        f = std::max(0.0f, std::min(1.0f, f));
        return f * f * (3 - 2 * f);
    }

    struct Point
    {
        float t, x, y;
    };

    struct Life
    {
        float r, g, b;
        std::vector<Point> points;
    };

    struct Round
    {
        float duration;
        std::vector<Life> lives;
    };

    struct Zone
    {
        float x, y, radius, r, g, b;
    };

    struct Replay
    {
        bool tried, ok;
        float minX, minY, maxX, maxY;
        std::vector<Zone> zones;
        std::vector<Round> rounds;
    };

    static Replay replay = { false, false, 0, 0, 0, 0,
                             std::vector<Zone>(), std::vector<Round>() };

    bool Load()
    {
        std::ifstream in;
        if (!tDirectories::Data().Open(in, replayFile))
            return false;

        std::string word;
        int version = 0;
        if (!(in >> word >> version) || word != "RCLREPLAY" || version != 1)
            return false;

        while (in >> word)
        {
            if (word == "arena")
            {
                in >> replay.minX >> replay.minY >> replay.maxX >> replay.maxY;
            }
            else if (word == "zone")
            {
                int team = 0;
                Zone zone;
                in >> team >> zone.x >> zone.y >> zone.radius
                   >> zone.r >> zone.g >> zone.b;
                if (!in || team < 0 || team > 15)
                    return false;
                zone.r /= 15; zone.g /= 15; zone.b /= 15;
                if (static_cast<int>(replay.zones.size()) <= team)
                    replay.zones.resize(team + 1, zone);
                replay.zones[team] = zone;
            }
            else if (word == "round")
            {
                Round round;
                int lives = 0;
                in >> round.duration >> lives;
                replay.rounds.push_back(round);
            }
            else if (word == "life")
            {
                int player = 0, team = 0, count = 0;
                float r = 0, g = 0, b = 0;
                in >> player >> team >> r >> g >> b >> count;
                if (!in || replay.rounds.empty() || count < 2 || count > 200000)
                    return false;

                Life life;
                life.points.resize(count);
                for (int i = 0; i < count; ++i)
                    in >> life.points[i].t >> life.points[i].x >> life.points[i].y;

                // Team colour leads, as it does in a team game; the player's
                // own colour only tints it.
                float const peak = std::max(1.0f, std::max(r, std::max(g, b)));
                life.r = r / peak; life.g = g / peak; life.b = b / peak;
                if (team >= 0 && team < static_cast<int>(replay.zones.size()))
                {
                    Zone const & zone = replay.zones[team];
                    life.r = .7f * zone.r + .3f * life.r;
                    life.g = .7f * zone.g + .3f * life.g;
                    life.b = .7f * zone.b + .3f * life.b;
                }
                replay.rounds.back().lives.push_back(life);
            }
            else
            {
                return false;
            }

            if (!in)
                return false;
        }

        return !replay.rounds.empty() &&
               replay.maxX > replay.minX && replay.maxY > replay.minY;
    }

    bool TimeBefore(float t, Point const & point) { return t < point.t; }

    // Position and heading of a life at round time t; false when it is not
    // on the grid. index is the last corner behind the cycle.
    bool Sample(Life const & life, float t, Vec & position, Vec & heading,
                int & index)
    {
        std::vector<Point> const & points = life.points;
        if (t < points.front().t || t > points.back().t)
            return false;

        int next = static_cast<int>(
            std::upper_bound(points.begin(), points.end(), t, TimeBefore)
            - points.begin());
        next = std::max(1, std::min(next, static_cast<int>(points.size()) - 1));

        Point const & a = points[next - 1];
        Point const & b = points[next];
        float const span = b.t - a.t;
        float const f = span > 1E-4f ? (t - a.t) / span : 1;
        Vec const from = { a.x, a.y, 0 };
        Vec const to = { b.x, b.y, 0 };
        Vec const forward = { 0, 1, 0 };
        position = Mix(from, to, f);
        heading = Normalized(to - from, forward);
        index = next - 1;
        return true;
    }

    // ---------------------------------------------------------------- camera

    struct Pose
    {
        Vec eye, target, up;
    };

    enum ShotKind
    {
        Shot_Eagle,   // straight down on the whole grid
        Shot_Orbit,   // low circle around the fight
        Shot_Chase,   // behind one cycle
        Shot_High,    // wide three-quarter view
        Shot_Sweep    // low pass from one zone to the other
    };

    struct Shot
    {
        ShotKind kind;
        float start, length, phase;
        int life;
        Vec heading;
        Pose last;
    };

    struct State
    {
        int round;
        float time;
        double lastReal;
        int shotCount;
        Shot shot, previous;
        Vec focus;
    };

    static State state;

    Vec Centre()
    {
        Vec c = { (replay.minX + replay.maxX) / 2, (replay.minY + replay.maxY) / 2, 0 };
        return c;
    }

    float Size()
    {
        return std::max(replay.maxX - replay.minX, replay.maxY - replay.minY);
    }

    // Middle of the cycles currently alive: where the fight is.
    Vec ActionCentre(Round const & round, float t)
    {
        Vec sum = { 0, 0, 0 };
        int count = 0;
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            Vec position, heading;
            int index;
            if (Sample(round.lives[i], t, position, heading, index))
            {
                sum = sum + position;
                ++count;
            }
        }
        return count > 0 ? sum * (1.0f / count) : Centre();
    }

    Pose Evaluate(Shot & shot, Round const & round, float t, float dt)
    {
        float const local = t - shot.start;
        float const size = Size();
        Vec const centre = Centre();
        Vec const sky = { 0, 0, 1 };
        Pose pose = shot.last;

        switch (shot.kind)
        {
        case Shot_Eagle:
        {
            float const angle = shot.phase + .05f * local;
            Vec const up = { sinf(angle), cosf(angle), 0 };
            Vec const lift = { 0, 0, size * 1.08f };
            pose.eye = centre + lift;
            pose.target = centre;
            pose.up = up;
            break;
        }
        case Shot_Orbit:
        {
            float const angle = shot.phase + .12f * local;
            Vec const offset = { cosf(angle) * size * .32f,
                                 sinf(angle) * size * .32f, size * .15f };
            Vec const rise = { 0, 0, 4 };
            pose.eye = state.focus + offset;
            pose.target = state.focus + rise;
            pose.up = sky;
            break;
        }
        case Shot_High:
        {
            float const angle = shot.phase + .05f * local;
            Vec const offset = { cosf(angle) * size * .72f,
                                 sinf(angle) * size * .72f, size * .40f };
            pose.eye = centre + offset;
            pose.target = centre;
            pose.up = sky;
            break;
        }
        case Shot_Sweep:
        {
            Vec from = { centre.x, replay.minY, 0 };
            Vec to = { centre.x, replay.maxY, 0 };
            if (replay.zones.size() >= 2)
            {
                Vec const a = { replay.zones[0].x, replay.zones[0].y, 0 };
                Vec const b = { replay.zones[1].x, replay.zones[1].y, 0 };
                from = a; to = b;
            }
            Vec const forward = { 0, 1, 0 };
            Vec const along = Normalized(to - from, forward);
            Vec const side = Cross(along, sky);
            Vec const lift = { 0, 0, size * .09f };
            float const f = Smooth(local / std::max(1.0f, shot.length));
            pose.eye = Mix(from - along * (size * .2f), to, f)
                       + side * (size * .14f) + lift;
            pose.target = state.focus;
            pose.up = sky;
            break;
        }
        case Shot_Chase:
        {
            Vec position, heading;
            int index;
            if (shot.life >= 0 && shot.life < static_cast<int>(round.lives.size()) &&
                Sample(round.lives[shot.life], t, position, heading, index))
            {
                // Ease through the cycle's right-angle turns.
                shot.heading = Normalized(
                    Mix(shot.heading, heading, std::min(1.0f, dt * 3)), heading);
                Vec const back = { 0, 0, 16 };
                Vec const ahead = { 0, 0, 2 };
                pose.eye = position - shot.heading * 42 + back;
                pose.target = position + shot.heading * 26 + ahead;
                pose.up = sky;
            }
            break;
        }
        }

        shot.last = pose;
        return pose;
    }

    // A cycle that stays alive long enough to be worth following.
    int PickChaseLife(Round const & round, float t, int skip)
    {
        int best = -1, seen = 0;
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            std::vector<Point> const & points = round.lives[i].points;
            if (points.front().t <= t && points.back().t >= t + 7)
            {
                best = static_cast<int>(i);
                if (seen++ >= skip)
                    break;
            }
        }
        return best;
    }

    void StartShot(ShotKind kind, Round const & round, float t)
    {
        static ShotKind const order[] = { Shot_Orbit, Shot_Chase, Shot_High,
                                          Shot_Chase, Shot_Sweep, Shot_Chase };
        Vec const forward = { 0, 1, 0 };

        state.previous = state.shot;
        Shot & shot = state.shot;
        shot.kind = kind;
        if (kind != Shot_Eagle)
        {
            shot.kind = order[state.shotCount % 6];
            ++state.shotCount;
        }
        shot.start = t;
        shot.phase = 1.7f * state.shotCount + .9f * state.round;
        shot.length = shot.kind == Shot_Eagle ? 8.0f : 11.0f;
        shot.life = -1;
        shot.heading = forward;

        if (shot.kind == Shot_Chase)
        {
            shot.life = PickChaseLife(round, t, state.shotCount % 5);
            if (shot.life < 0)
                shot.kind = Shot_Orbit;
            else
            {
                Vec position;
                int index;
                Sample(round.lives[shot.life], t, position, shot.heading, index);
                shot.length = 9;
            }
        }
    }

    void StartRound(int index)
    {
        state.round = index % static_cast<int>(replay.rounds.size());
        state.time = 0;
        state.focus = Centre();
        StartShot(Shot_Eagle, replay.rounds[state.round], 0);
        // Rounds cut in from black, so there is nothing to swoop from.
        state.previous = state.shot;
        state.shot.start = -blendTime;
        state.shot.length += blendTime;
    }

    void ApplyCamera(Pose const & pose)
    {
        float const aspect = sr_screenHeight > 0
            ? static_cast<float>(sr_screenWidth) / sr_screenHeight : 4.0f / 3.0f;
        float const nearPlane = 2, farPlane = 6000;
        float const top = nearPlane * tanf(55.0f * pi / 360.0f);
        float const right = top * aspect;

        ProjMatrix();
        IdentityMatrix();
        glFrustum(-right, right, -top, top, nearPlane, farPlane);

        Vec const sky = { 0, 0, 1 };
        Vec const forward = Normalized(pose.target - pose.eye, sky * -1);
        Vec side = Cross(forward, pose.up);
        if (Length(side) < 1E-3f)
        {
            Vec const alternative = { 0, 1, 0 };
            side = Cross(forward, alternative);
        }
        side = Normalized(side, sky);
        Vec const up = Cross(side, forward);

        GLfloat const matrix[16] = {
            side.x, up.x, -forward.x, 0,
            side.y, up.y, -forward.y, 0,
            side.z, up.z, -forward.z, 0,
            0, 0, 0, 1 };
        ModelMatrix();
        IdentityMatrix();
        glMultMatrixf(matrix);
        glTranslatef(-pose.eye.x, -pose.eye.y, -pose.eye.z);
    }

    // ------------------------------------------------------------------ scene
    // Drawn in the clean arena look: a pale open floor lit from one point,
    // solid trails and their soft shadows. Colours come from rScreen.

    float const wallHalfWidth = .55f;

    Vec LightPosition()
    {
        Vec light = { replay.minX + (replay.maxX - replay.minX) * .30f,
                      replay.minY + (replay.maxY - replay.minY) * .68f,
                      Size() * .28f };
        return light;
    }

    // where the top of a wall at p throws its shadow
    Vec ShadowPoint(Vec const & p, float height)
    {
        Vec const light = LightPosition();
        Vec offset = { p.x - light.x, p.y - light.y, 0 };
        offset = offset * (height / std::max(1.0f, light.z - height));
        float const length = Length(offset);
        float const shortest = height * .7f, longest = height * 1.7f;
        if (length > longest)
            offset = offset * (longest / length);
        else if (length < shortest && length > 1E-4f)
            offset = offset * (shortest / length);
        Vec const result = { p.x + offset.x, p.y + offset.y, .05f };
        return result;
    }

    void DrawFloor()
    {
        float const size = Size();
        Vec const centre = Centre();
        int const segments = 48;

        // the plane, fading into the sky colour with distance
        static float const radius[6] = { 0, .7f, 1.2f, 2.5f, 5, 14 };
        static float const haze[6]   = { 0, 0, .08f, .40f, .80f, 1 };
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int ring = 0; ring < 5; ++ring)
        {
            BeginQuadStrip();
            for (int i = 0; i <= segments; ++i)
            {
                float const a = 2 * pi * i / segments;
                for (int edge = 0; edge < 2; ++edge)
                {
                    float const h = haze[ring + edge];
                    Color(sr_cleanFloorColor[0] + (sr_cleanSkyColor[0] - sr_cleanFloorColor[0]) * h,
                          sr_cleanFloorColor[1] + (sr_cleanSkyColor[1] - sr_cleanFloorColor[1]) * h,
                          sr_cleanFloorColor[2] + (sr_cleanSkyColor[2] - sr_cleanFloorColor[2]) * h, 1);
                    Vertex(centre.x + cosf(a) * radius[ring + edge] * size,
                           centre.y + sinf(a) * radius[ring + edge] * size, -.05f);
                }
            }
            RenderEnd();
        }

        // the pool of light under the lamp
        Vec const light = LightPosition();
        static float const poolRadius[6] = { 0, .12f, .30f, .55f, .90f, 1.6f };
        static float const poolGlow[6]   = { .30f, .25f, .15f, .07f, .02f, 0 };
        for (int ring = 0; ring < 5; ++ring)
        {
            BeginQuadStrip();
            for (int i = 0; i <= segments; ++i)
            {
                float const a = 2 * pi * i / segments;
                Color(1, 1, 1, poolGlow[ring]);
                Vertex(light.x + cosf(a) * poolRadius[ring] * size,
                       light.y + sinf(a) * poolRadius[ring] * size, 0);
                Color(1, 1, 1, poolGlow[ring + 1]);
                Vertex(light.x + cosf(a) * poolRadius[ring + 1] * size,
                       light.y + sinf(a) * poolRadius[ring + 1] * size, 0);
            }
            RenderEnd();
        }

        // the arena's edge, as a line on the floor
        glLineWidth(1.5f);
        Color(sr_cleanFloorColor[0] * .72f, sr_cleanFloorColor[1] * .72f,
              sr_cleanFloorColor[2] * .72f, 1);
        BeginLineLoop();
        Vertex(replay.minX, replay.minY, .05f);
        Vertex(replay.maxX, replay.minY, .05f);
        Vertex(replay.maxX, replay.maxY, .05f);
        Vertex(replay.minX, replay.maxY, .05f);
        RenderEnd();
    }

    void DrawZones(float t)
    {
        int const segments = 48;
        float const height = wallHeight * .5f;
        for (size_t z = 0; z < replay.zones.size(); ++z)
        {
            Zone const & zone = replay.zones[z];
            float const spin = t * .35f * (z % 2 ? -1 : 1);
            BeginQuads();
            Color(zone.r, zone.g, zone.b, .55f);
            for (int i = 0; i < segments; i += 2)
            {
                float const a = spin + 2 * pi * i / segments;
                float const b = spin + 2 * pi * (i + 1) / segments;
                Vec const from = { zone.x + cosf(a) * zone.radius,
                                   zone.y + sinf(a) * zone.radius, 0 };
                Vec const to = { zone.x + cosf(b) * zone.radius,
                                 zone.y + sinf(b) * zone.radius, 0 };
                Vertex(from.x, from.y, 0);
                Vertex(to.x, to.y, 0);
                Vertex(to.x, to.y, height);
                Vertex(from.x, from.y, height);
            }
            RenderEnd();

            Color(zone.r * .8f, zone.g * .8f, zone.b * .8f, .9f);
            BeginLineLoop();
            for (int i = 0; i < segments; ++i)
            {
                float const a = 2 * pi * i / segments;
                Vertex(zone.x + cosf(a) * zone.radius,
                       zone.y + sinf(a) * zone.radius, .1f);
            }
            RenderEnd();
        }
    }

    // one straight piece of trail: its shadow, or the box itself
    void TrailPiece(Vec const & p1, Vec const & p2, Life const & life,
                    float alpha, bool shadow)
    {
        if (shadow)
        {
            Vec const s1 = ShadowPoint(p1, wallHeight);
            Vec const s2 = ShadowPoint(p2, wallHeight);
            Color(sr_cleanShadowColor[0], sr_cleanShadowColor[1], sr_cleanShadowColor[2], .34f * alpha);
            Vertex(p1.x, p1.y, .05f);
            Vertex(p2.x, p2.y, .05f);
            Color(sr_cleanShadowColor[0], sr_cleanShadowColor[1], sr_cleanShadowColor[2], 0);
            Vertex(s2.x, s2.y, .05f);
            Vertex(s1.x, s1.y, .05f);
            return;
        }

        Vec const forward = { 0, 1, 0 };
        Vec const along = Normalized(p2 - p1, forward) * wallHalfWidth;
        Vec const side = { -along.y, along.x, 0 };
        Vec const a = p1 - along;
        Vec const c = p2 + along;
        Vec const light = LightPosition();
        Vec const toLight = { light.x - (p1.x + p2.x) * .5f, light.y - (p1.y + p2.y) * .5f, 0 };
        bool const lit = side.x * toLight.x + side.y * toLight.y > 0;

        for (int sign = -1; sign <= 1; sign += 2)
        {
            Vec const o = side * static_cast<float>(sign);
            float const shade = ((sign > 0) == lit) ? .96f : .74f;
            Color(life.r * shade, life.g * shade, life.b * shade, alpha);
            Vertex(a.x + o.x, a.y + o.y, 0);
            Vertex(a.x + o.x, a.y + o.y, wallHeight);
            Vertex(c.x + o.x, c.y + o.y, wallHeight);
            Vertex(c.x + o.x, c.y + o.y, 0);
        }

        Color(life.r + (1 - life.r) * .30f, life.g + (1 - life.g) * .30f,
              life.b + (1 - life.b) * .30f, alpha);
        Vertex(a.x - side.x, a.y - side.y, wallHeight);
        Vertex(a.x + side.x, a.y + side.y, wallHeight);
        Vertex(c.x + side.x, c.y + side.y, wallHeight);
        Vertex(c.x - side.x, c.y - side.y, wallHeight);
    }

    void DrawLife(Life const & life, float t, bool shadow)
    {
        std::vector<Point> const & points = life.points;
        if (t < points.front().t)
            return;

        Vec head, heading;
        int index;
        float alpha = 1;
        bool const alive = Sample(life, t, head, heading, index);
        if (!alive)
        {
            float const since = t - points.back().t;
            if (since > wallFade)
                return;
            alpha = 1 - since / wallFade;
            index = static_cast<int>(points.size()) - 2;
            Vec const end = { points.back().x, points.back().y, 0 };
            head = end;
        }

        // walk the trail back from the head
        BeginQuads();
        float remaining = wallLength;
        Vec from = head;
        for (int i = index; i >= 0 && remaining > 0; --i)
        {
            Vec to = { points[i].x, points[i].y, 0 };
            float length = Length(to - from);
            if (length > remaining)
            {
                to = Mix(from, to, remaining / length);
                length = remaining;
            }
            if (length > 1E-3f)
                TrailPiece(to, from, life, alpha, shadow);
            remaining -= length;
            from = to;
        }
        RenderEnd();

        if (alive && !shadow)
        {
            // the cycle: a pale wedge ahead of its trail
            Vec const sky = { 0, 0, 1 };
            Vec const side = Cross(heading, sky);
            Vec const tip = head + heading * 6;
            Vec const left = head - heading * 1 + side * 2.2f;
            Vec const right = head - heading * 1 - side * 2.2f;
            Color(.90f + .10f * life.r, .90f + .10f * life.g, .90f + .10f * life.b, 1);
            BeginTriangles();
            Vertex(tip.x, tip.y, wallHeight * .8f);
            Vertex(left.x, left.y, wallHeight * .8f);
            Vertex(right.x, right.y, wallHeight * .8f);
            RenderEnd();
        }
    }
}

bool gMenuReplay::Render()
{
    if (!sg_menuReplay || !sr_glOut)
        return false;

    if (!replay.tried)
    {
        replay.tried = true;
        replay.ok = Load();
        if (replay.ok)
        {
            state.lastReal = tSysTimeFloat();
            state.shotCount = 0;
            StartRound(0);
        }
    }
    if (!replay.ok)
        return false;

    // Advance only while a menu is showing it; pick up where it left off.
    double const now = tSysTimeFloat();
    float dt = static_cast<float>(now - state.lastReal);
    state.lastReal = now;
    if (dt < 0 || dt > .25f)
        dt = .02f;
    state.time += dt;

    if (state.time > replay.rounds[state.round].duration + roundLeadOut)
        StartRound(state.round + 1);
    Round const & round = replay.rounds[state.round];
    float const t = state.time;

    state.focus = Mix(state.focus, ActionCentre(round, t), std::min(1.0f, dt * 1.5f));

    Shot const & current = state.shot;
    bool const chaseLost = current.kind == Shot_Chase &&
        ( current.life < 0 ||
          t > round.lives[current.life].points.back().t - .3f );
    if (t - current.start > current.length || chaseLost)
        StartShot(Shot_Orbit, round, t);

    // Both shots keep moving while one hands over to the other; that is the
    // swoop.
    Pose pose = Evaluate(state.shot, round, t, dt);
    float const blend = Smooth((t - state.shot.start) / blendTime);
    if (blend < 1)
    {
        Pose const from = Evaluate(state.previous, round, t, dt);
        Vec const sky = { 0, 0, 1 };
        pose.eye = Mix(from.eye, pose.eye, blend);
        pose.target = Mix(from.target, pose.target, blend);
        pose.up = Normalized(Mix(from.up, pose.up, blend), sky);
    }

    RenderEnd();
    ProjMatrix();
    PushMatrix();
    ModelMatrix();
    PushMatrix();
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT |
                 GL_DEPTH_BUFFER_BIT);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);

    ApplyCamera(pose);
    DrawFloor();
    DrawZones(t);
    // shadows lie on the floor and must not hide each other or the trails
    glDepthMask(GL_FALSE);
    for (size_t i = 0; i < round.lives.size(); ++i)
        DrawLife(round.lives[i], t, true);
    glDepthMask(GL_TRUE);
    for (size_t i = 0; i < round.lives.size(); ++i)
        DrawLife(round.lives[i], t, false);
    glDisable(GL_DEPTH_TEST);

    // Cut between rounds through the sky colour.
    float const fade = std::max(1 - t / 1.2f,
                                (t - round.duration) / roundLeadOut);
    ProjMatrix();
    IdentityMatrix();
    ModelMatrix();
    IdentityMatrix();
    if (fade > 0)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Color(sr_cleanSkyColor[0], sr_cleanSkyColor[1], sr_cleanSkyColor[2], std::min(1.0f, fade));
        BeginQuads();
        Vertex(-1, -1);
        Vertex(1, -1);
        Vertex(1, 1);
        Vertex(-1, 1);
        RenderEnd();
    }

    glPopAttrib();
    ProjMatrix();
    PopMatrix();
    ModelMatrix();
    PopMatrix();
    Color(1, 1, 1, 1);

    return true;
}

#endif
