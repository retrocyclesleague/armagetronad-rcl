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
#include "eCoord.h"
#include "eFloor.h"
#include "gCycle.h"
#include "gWall.h"
#include "gZone.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

static bool sg_menuReplay = true;
static tConfItem<bool> sg_menuReplayConf("MENU_REPLAY", sg_menuReplay);

namespace
{
    // The replay is a real league match, played back from data only: each
    // cycle's path as timed points, the two zones and when each fell, written
    // by scripts/generate-menu-replay.py (which says how the motion is
    // rebuilt from the match log). It carries no names.
    //
    // It is drawn the way the game draws a match: the same arena, walls,
    // arrows, shadows and painted zones, through the clean arena's own
    // functions, in the look the player has chosen. What happens in it
    // follows the servers' rules: walls as long as theirs, standing as long
    // after a death, a zone that falls when it was conquered.
    char const * const replayFile = "replays/menu_fort.rclreplay";

    float const wallSink     = .5f;   // seconds a dead cycle's walls take to go down once their time is up
    float const tipLength    = 3.6f;  // the stretch behind a cycle over which its wall rises out of it
    float const zoneCollapse = 2;     // seconds a conquered zone takes to close
    float const deathRing    = .7f;   // seconds the ring a cycle leaves behind is seen
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
        int team;
        bool died;
        std::vector<Point> points;
    };

    struct Fall
    {
        float t;
        int team;   // whose zone it is
    };

    struct Round
    {
        float duration;
        std::vector<Life> lives;
        std::vector<Fall> falls;
    };

    struct Zone
    {
        bool there;
        float x, y, radius, r, g, b;
        float turn;     // how far it has turned, in radians
    };

    struct Replay
    {
        bool tried, ok;
        float minX, minY, maxX, maxY;
        float wallLength, wallStay;
        std::vector<Zone> zones;
        std::vector<Round> rounds;
    };

    static Replay replay = { false, false, 0, 0, 0, 0, 400, 8,
                             std::vector<Zone>(), std::vector<Round>() };

    bool Load()
    {
        std::ifstream in;
        if (!tDirectories::Data().Open(in, replayFile))
            return false;

        std::string word;
        int version = 0;
        if (!(in >> word >> version) || word != "RCLREPLAY" || version != 2)
            return false;

        while (in >> word)
        {
            if (word == "arena")
            {
                in >> replay.minX >> replay.minY >> replay.maxX >> replay.maxY;
            }
            else if (word == "walls")
            {
                in >> replay.wallLength >> replay.wallStay;
            }
            else if (word == "zone")
            {
                int team = 0;
                Zone zone;
                zone.there = true;
                zone.turn = 0;
                in >> team >> zone.x >> zone.y >> zone.radius
                   >> zone.r >> zone.g >> zone.b;
                if (!in || team < 0 || team > 15)
                    return false;
                zone.r /= 15; zone.g /= 15; zone.b /= 15;
                if (static_cast<int>(replay.zones.size()) <= team)
                {
                    Zone none = zone;
                    none.there = false;
                    replay.zones.resize(team + 1, none);
                }
                replay.zones[team] = zone;
            }
            else if (word == "round")
            {
                Round round;
                int lives = 0;
                in >> round.duration >> lives;
                replay.rounds.push_back(round);
            }
            else if (word == "fall")
            {
                Fall fall;
                in >> fall.t >> fall.team;
                if (!in || replay.rounds.empty())
                    return false;
                replay.rounds.back().falls.push_back(fall);
            }
            else if (word == "life")
            {
                int player = 0, team = 0, died = 0, count = 0;
                float r = 0, g = 0, b = 0;
                in >> player >> team >> r >> g >> b >> died >> count;
                if (!in || replay.rounds.empty() || count < 2 || count > 200000)
                    return false;

                Life life;
                life.team = team;
                life.died = died != 0;
                life.points.resize(count);
                for (int i = 0; i < count; ++i)
                    in >> life.points[i].t >> life.points[i].x >> life.points[i].y;

                // Team colour leads, as it does in a team game; the player's
                // own colour only tints it.
                float const peak = std::max(1.0f, std::max(r, std::max(g, b)));
                life.r = r / peak; life.g = g / peak; life.b = b / peak;
                if (team >= 0 && team < static_cast<int>(replay.zones.size()) &&
                    replay.zones[team].there)
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
    // on the grid. index is the last point behind the cycle.
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
        position = Mix(from, to, f);

        // A cycle held by a wall has two points in one place: it still faces
        // the way it came.
        Vec const forward = { 0, 1, 0 };
        heading = forward;
        for (int i = next; i >= 1; --i)
        {
            Vec const p = { points[i].x, points[i].y, 0 };
            Vec const q = { points[i - 1].x, points[i - 1].y, 0 };
            if (Length(p - q) > 1E-3f)
            {
                heading = Normalized(p - q, forward);
                break;
            }
        }
        index = next - 1;
        return true;
    }

    // When the zone of a team falls in a round; far in the future if it does not.
    float FallTime(Round const & round, int team)
    {
        float when = 1E+30f;
        for (size_t i = 0; i < round.falls.size(); ++i)
            if (round.falls[i].team == team && round.falls[i].t < when)
                when = round.falls[i].t;
        return when;
    }

    // ---------------------------------------------------------------- camera

    struct Pose
    {
        Vec eye, target, up;
    };

    enum ShotKind
    {
        Shot_Eagle,   // high over the middle, turning slowly
        Shot_Orbit,   // low circle around the fight
        Shot_Chase,   // behind one cycle
        Shot_High,    // wide three-quarter view
        Shot_Sweep,   // low pass from one zone to the other
        Shot_Zone     // around a zone that is about to fall
    };

    struct Shot
    {
        ShotKind kind;
        float start, length, phase;
        int life;       // the life followed, or the zone circled
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

    // Where the fight is: the middle of the largest knot of cycles. (The
    // middle of all of them is the empty ground between the two zones.)
    Vec ActionCentre(Round const & round, float t)
    {
        std::vector<Vec> at;
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            Vec position, heading;
            int index;
            if (Sample(round.lives[i], t, position, heading, index))
                at.push_back(position);
        }
        if (at.empty())
            return Centre();

        float const reach = 70;
        size_t best = 0;
        int most = -1;
        for (size_t i = 0; i < at.size(); ++i)
        {
            int around = 0;
            for (size_t j = 0; j < at.size(); ++j)
                if (Length(at[j] - at[i]) < reach)
                    ++around;
            if (around > most)
            {
                most = around;
                best = i;
            }
        }

        Vec sum = { 0, 0, 0 };
        int count = 0;
        for (size_t j = 0; j < at.size(); ++j)
        {
            if (Length(at[j] - at[best]) < reach)
            {
                sum = sum + at[j];
                ++count;
            }
        }
        return sum * (1.0f / count);
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
            Vec const lift = { 0, 0, size * .56f };
            pose.eye = centre + lift;
            pose.target = centre;
            pose.up = up;
            break;
        }
        case Shot_Orbit:
        {
            float const angle = shot.phase + .12f * local;
            Vec const offset = { cosf(angle) * 78, sinf(angle) * 78, 34 };
            Vec const rise = { 0, 0, 1 };
            pose.eye = state.focus + offset;
            pose.target = state.focus + rise;
            pose.up = sky;
            break;
        }
        case Shot_High:
        {
            float const angle = shot.phase + .05f * local;
            Vec const offset = { cosf(angle) * size * .50f,
                                 sinf(angle) * size * .50f, size * .28f };
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
            Vec const lift = { 0, 0, 24 };
            float const f = Smooth(local / std::max(1.0f, shot.length));
            pose.eye = Mix(from - along * 70, to, f) + side * 52 + lift;
            pose.target = state.focus;
            pose.up = sky;
            break;
        }
        case Shot_Zone:
        {
            if (shot.life >= 0 && shot.life < static_cast<int>(replay.zones.size()))
            {
                Zone const & zone = replay.zones[shot.life];
                float const angle = shot.phase + .10f * local;
                Vec const middle = { zone.x, zone.y, 0 };
                Vec const offset = { cosf(angle) * 88, sinf(angle) * 88, 32 };
                Vec const rise = { 0, 0, 1 };
                pose.eye = middle + offset;
                pose.target = middle + rise;
                pose.up = sky;
            }
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
                Vec const back = { 0, 0, 14 };
                Vec const ahead = { 0, 0, 1 };
                pose.eye = position - shot.heading * 26 + back;
                pose.target = position + shot.heading * 16 + ahead;
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

    // The zone that falls within the next few seconds, or -1.
    int ZoneAboutToFall(Round const & round, float t)
    {
        for (size_t i = 0; i < round.falls.size(); ++i)
        {
            Fall const & fall = round.falls[i];
            if (fall.t > t + 3 && fall.t < t + 13 && fall.team >= 0 &&
                fall.team < static_cast<int>(replay.zones.size()) &&
                replay.zones[fall.team].there)
                return fall.team;
        }
        return -1;
    }

    void StartShot(ShotKind kind, Round const & round, float t)
    {
        static ShotKind const order[] = { Shot_Orbit, Shot_Chase, Shot_High,
                                          Shot_Chase, Shot_Sweep, Shot_Chase };
        Vec const forward = { 0, 1, 0 };

        state.previous = state.shot;
        Shot & shot = state.shot;
        shot.kind = kind;
        if (kind != Shot_Eagle && kind != Shot_Zone)
        {
            shot.kind = order[state.shotCount % 6];
            ++state.shotCount;
        }
        shot.start = t;
        shot.phase = 1.7f * state.shotCount + .9f * state.round;
        shot.length = shot.kind == Shot_Eagle ? 8.0f : 11.0f;
        shot.life = -1;
        shot.heading = forward;

        if (shot.kind == Shot_Zone)
        {
            shot.life = ZoneAboutToFall(round, t);
            if (shot.life < 0)
                shot.kind = Shot_Orbit;
            else
                // to the fall and through the collapse
                shot.length = FallTime(round, shot.life) - t + zoneCollapse + 1.5f;
        }

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
        // Rounds cut in, so there is nothing to swoop from.
        state.previous = state.shot;
        state.shot.start = -blendTime;
        state.shot.length += blendTime;
    }

    void ApplyCamera(Pose const & pose)
    {
        float const aspect = sr_screenHeight > 0
            ? static_cast<float>(sr_screenWidth) / sr_screenHeight : 4.0f / 3.0f;
        // as near and as far as the game itself looks
        float const nearPlane = .5f;
        double const farPlane = 1E+20;
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

    // Walks a life's wall back from its head and hands each straight piece to
    // the caller, with how far behind the head its two ends lie. Returns
    // false if there is nothing to draw at time t. The rules are the
    // servers': a wall is so long and no longer, and when its cycle is gone
    // it stands for a while and then goes down.
    template <class Piece>
    bool WalkWall(Life const & life, float t, bool & alive, float & standing, Piece & piece)
    {
        std::vector<Point> const & points = life.points;
        if (t < points.front().t)
            return false;

        Vec head, heading;
        int index;
        standing = 1;
        alive = Sample(life, t, head, heading, index);
        if (!alive)
        {
            float const since = t - points.back().t;
            if (since > replay.wallStay + wallSink)
                return false;
            if (since > replay.wallStay)
                standing = 1 - (since - replay.wallStay) / wallSink;
            index = static_cast<int>(points.size()) - 2;
            Vec const end = { points.back().x, points.back().y, 0 };
            head = end;
        }

        float behind = 0;
        Vec from = head;
        for (int i = index; i >= 0 && behind < replay.wallLength; --i)
        {
            Vec to = { points[i].x, points[i].y, 0 };
            float length = Length(to - from);
            if (behind + length > replay.wallLength)
            {
                to = Mix(from, to, (replay.wallLength - behind) / length);
                length = replay.wallLength - behind;
            }
            if (length > 1E-3f)
                piece(to, from, behind + length, behind);
            behind += length;
            from = to;
        }
        return true;
    }

    // one pass of the clean arena's wall drawing over a life's wall
    struct WallPass
    {
        Life const & life;
        int pass;
        bool alive;
        float standing;

        WallPass(Life const & l, int p): life(l), pass(p), alive(false), standing(1) {}

        // A wall rises out of its cycle: low at the arrow's tail, full height
        // a few units back.
        float Height(float behind) const
        {
            float rise = 1;
            if (alive && behind < tipLength)
                rise = .22f + .78f * behind / tipLength;
            return rise * standing;
        }

        void Draw(Vec const & a, Vec const & b, float behindA, float behindB) const
        {
            sg_CleanWallPiece(eCoord(a.x, a.y), eCoord(b.x, b.y), Height(behindA), Height(behindB),
                              life.r, life.g, life.b, 1, pass);
        }

        // a is the older end, further behind the head
        void operator()(Vec const & a, Vec const & b, float behindA, float behindB) const
        {
            if (alive && behindB < tipLength && behindA > tipLength)
            {
                // level from where it has its height; the rise is a piece of its own
                Vec const full = Mix(b, a, (tipLength - behindB) / (behindA - behindB));
                Draw(a, full, behindA, tipLength);
                Draw(full, b, tipLength, behindB);
            }
            else
            {
                Draw(a, b, behindA, behindB);
            }
        }
    };

    void DrawWalls(Round const & round, float t, int pass)
    {
        BeginQuads();
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            WallPass piece(round.lives[i], pass);
            // the pass is told whether the cycle lives before any piece is handed over
            Vec head, heading;
            int index;
            piece.alive = Sample(round.lives[i], t, head, heading, index);
            if (!piece.alive)
            {
                float const since = t - round.lives[i].points.back().t;
                piece.standing = since > replay.wallStay
                    ? std::max(0.0f, 1 - (since - replay.wallStay) / wallSink) : 1;
            }
            bool alive;
            float standing;
            WalkWall(round.lives[i], t, alive, standing, piece);
        }
        RenderEnd();
    }

    // A line along the top of every wall. From high up a wall is thinner than
    // a pixel; the line is what is then seen of it.
    struct WallLine
    {
        bool alive;
        float standing;

        // as the wall itself: rising out of its cycle
        float Height(float behind) const
        {
            float rise = 1;
            if (alive && behind < tipLength)
                rise = .22f + .78f * behind / tipLength;
            return rise * standing;
        }

        void operator()(Vec const & a, Vec const & b, float behindA, float behindB) const
        {
            if (alive && behindB < tipLength && behindA > tipLength)
            {
                Vec const full = Mix(b, a, (tipLength - behindB) / (behindA - behindB));
                glVertex3f(a.x, a.y, Height(behindA));
                glVertex3f(full.x, full.y, Height(tipLength));
                glVertex3f(full.x, full.y, Height(tipLength));
                glVertex3f(b.x, b.y, Height(behindB));
                return;
            }
            glVertex3f(a.x, a.y, Height(behindA));
            glVertex3f(b.x, b.y, Height(behindB));
        }
    };

    void DrawWallLines(Round const & round, float t)
    {
        sr_DepthOffset(true);
        glLineWidth(1);
        BeginLines();
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            Life const & life = round.lives[i];
            REAL r = life.r, g = life.g, b = life.b;
            sr_CleanPaint(r, g, b);
            glColor4f(r + (1 - r) * .2f, g + (1 - g) * .2f, b + (1 - b) * .2f, 1);
            Vec head, heading;
            int index;
            WallLine line = { Sample(life, t, head, heading, index), 1 };
            float const since = t - life.points.back().t;
            if (since > replay.wallStay)
                line.standing = std::max(0.0f, 1 - (since - replay.wallStay) / wallSink);
            bool alive;
            float standing;
            WalkWall(life, t, alive, standing, line);
        }
        RenderEnd();
        sr_DepthOffset(false);
    }

    // How a zone stands at time t of the round: its radius, and how many of
    // the other team are in it.
    float ZoneRadius(Round const & round, int team, float t)
    {
        Zone const & zone = replay.zones[team];
        float const fall = FallTime(round, team);
        if (t <= fall)
            return zone.radius;
        return zone.radius * std::max(0.0f, 1 - (t - fall) / zoneCollapse);
    }

    int Attackers(Round const & round, int team, float t)
    {
        Zone const & zone = replay.zones[team];
        int inside = 0;
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            if (round.lives[i].team == team)
                continue;
            Vec position, heading;
            int index;
            if (Sample(round.lives[i], t, position, heading, index))
            {
                Vec const middle = { zone.x, zone.y, 0 };
                if (Length(position - middle) < zone.radius)
                    ++inside;
            }
        }
        return inside;
    }

    void DrawScene(Round const & round, float t, Vec const & eye)
    {
        // the backdrop: sky, floor and glint, as far as the eye goes
        se_CleanArenaBackdrop(eCoord(eye.x, eye.y), eye.z);

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        // the arena's edge, marked on the floor
        eCoord const corner[5] = { eCoord(replay.minX, replay.minY), eCoord(replay.minX, replay.maxY),
                                   eCoord(replay.maxX, replay.maxY), eCoord(replay.maxX, replay.minY),
                                   eCoord(replay.minX, replay.minY) };
        for (int i = 0; i < 4; ++i)
            sg_CleanRimMarking(corner[i], corner[i + 1]);

        if (sr_alphaBlend)
        {
            // what the floor mirrors of the walls: the nearest face, once
            for (int pass = 0; pass < 2; ++pass)
            {
                sg_CleanSheenBegin(pass);
                DrawWalls(round, t, 1);
                sg_CleanSheenEnd();
            }

            // every wall's shadow, laid once; where that cannot be done, one by one
            float const margin = 60;
            if (sg_CleanShadowsBegin())
            {
                DrawWalls(round, t, 0);
                sg_CleanShadowsEnd(replay.minX - margin, replay.minY - margin,
                                   replay.maxX + margin, replay.maxY + margin);
            }
            else
            {
                glDepthMask(GL_FALSE);
                DrawWalls(round, t, 0);
                glDepthMask(GL_TRUE);
            }

            // and the cycles'
            for (size_t i = 0; i < round.lives.size(); ++i)
            {
                Vec position, heading;
                int index;
                if (Sample(round.lives[i], t, position, heading, index))
                    sg_RenderCleanCycleShadowAt(eCoord(position.x, position.y), eCoord(heading.x, heading.y));
            }
        }

        // the walls and the cycles themselves
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        DrawWalls(round, t, 2);
        DrawWallLines(round, t);
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            Life const & life = round.lives[i];
            Vec position, heading;
            int index;
            if (Sample(life, t, position, heading, index))
                sg_RenderCleanCycleAt(eCoord(position.x, position.y), eCoord(heading.x, heading.y),
                                      life.r, life.g, life.b);
        }

        // where a cycle has just gone: a ring on the floor that opens and fades
        glDepthMask(GL_FALSE);
        BeginQuads();
        for (size_t i = 0; i < round.lives.size(); ++i)
        {
            Life const & life = round.lives[i];
            float const since = t - life.points.back().t;
            if (!life.died || since < 0 || since > deathRing)
                continue;
            float const f = since / deathRing;
            float const outer = 1 + 7 * sqrtf(f), inner = outer * (.55f + .35f * f);
            REAL r = life.r, g = life.g, b = life.b;
            sr_CleanPaint(r, g, b);
            float const light = .5f;
            r += (1 - r) * light; g += (1 - g) * light; b += (1 - b) * light;
            float const x = life.points.back().x, y = life.points.back().y;
            int const segments = 28;
            for (int k = 0; k < segments; ++k)
            {
                float const a0 = 2 * pi * k / segments, a1 = 2 * pi * (k + 1) / segments;
                glColor4f(r, g, b, 0);
                glVertex3f(x + cosf(a0) * inner, y + sinf(a0) * inner, .04f);
                glVertex3f(x + cosf(a1) * inner, y + sinf(a1) * inner, .04f);
                glColor4f(r, g, b, .75f * (1 - f));
                glVertex3f(x + cosf(a1) * outer, y + sinf(a1) * outer, .04f);
                glVertex3f(x + cosf(a0) * outer, y + sinf(a0) * outer, .04f);
            }
        }
        RenderEnd();
        glDepthMask(GL_TRUE);

        // the zones, painted on the floor; the veil over them is see-through
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        for (size_t z = 0; z < replay.zones.size(); ++z)
        {
            Zone const & zone = replay.zones[z];
            if (!zone.there)
                continue;
            float const radius = ZoneRadius(round, static_cast<int>(z), t);
            if (radius > .5f)
                sg_RenderCleanZoneAt(eCoord(zone.x, zone.y), radius, zone.turn,
                                     zone.r, zone.g, zone.b, std::min(1.0f, t / 1.5f));
        }
        RenderEnd();
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

    // A zone turns, and faster while it is under attack.
    for (size_t z = 0; z < replay.zones.size(); ++z)
        if (replay.zones[z].there)
            replay.zones[z].turn += dt * (.3f + .5f * std::min(3, Attackers(round, static_cast<int>(z), t)));

    Shot const & current = state.shot;
    bool const chaseLost = current.kind == Shot_Chase &&
        ( current.life < 0 ||
          t > round.lives[current.life].points.back().t - .3f );
    if (t - current.start > current.length || chaseLost)
        StartShot(Shot_Orbit, round, t);
    else if (current.kind != Shot_Zone && current.kind != Shot_Eagle &&
             ZoneAboutToFall(round, t) >= 0 && t - current.start > 2)
        // a zone is about to go: be there
        StartShot(Shot_Zone, round, t);

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
    if (pose.eye.z < 2)
        pose.eye.z = 2;

    RenderEnd();
    ProjMatrix();
    PushMatrix();
    ModelMatrix();
    PushMatrix();
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    // In the look the player plays in; those on the classic grid get the
    // night arena here.
    bool const wasDark = sr_cleanDark;
    if (!sr_cleanArena)
        sr_cleanDark = true;

    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    ApplyCamera(pose);
    DrawScene(round, t, pose.eye);

    // Cut between rounds through the floor's own colour.
    float const fade = std::max(1 - t / 1.2f,
                                (t - round.duration) / roundLeadOut);
    ProjMatrix();
    IdentityMatrix();
    ModelMatrix();
    IdentityMatrix();
    if (fade > 0)
    {
        REAL const * const floor = sr_CleanFloorColor();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        Color(floor[0], floor[1], floor[2], std::min(1.0f, fade));
        BeginQuads();
        Vertex(-1, -1);
        Vertex(1, -1);
        Vertex(1, 1);
        Vertex(-1, 1);
        RenderEnd();
    }

    sr_cleanDark = wasDark;

    glPopAttrib();
    ProjMatrix();
    PopMatrix();
    ModelMatrix();
    PopMatrix();
    Color(1, 1, 1, 1);

    return true;
}

#endif
