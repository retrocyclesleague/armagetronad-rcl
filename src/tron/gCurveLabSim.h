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

#ifndef ArmageTron_CURVE_LAB_SIM_H
#define ArmageTron_CURVE_LAB_SIM_H

// The curve lab's riders (gCurveLab.cpp): a bike on an open floor, ridden by
// the turning rules being tried out, with nothing to hit, no server and no
// recording. Plain C++ with no part of the game in it, so that it can be
// built on its own and checked against the reference model it follows step
// for step, rcl-cfg/curves/curves_ref.py (simulate(): the same order of
// things within a step, the same short step where an arc ends).

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace gCurveLab
{
    double const quarter = 1.5707963267948966;

    //! how a rider turns
    enum Rule
    {
        //! Corners at least the delay distance D = speed * delay apart, as
        //! the classic game spaces queued turns, each rounded to a quarter
        //! circle of radius roundness * D. A turn takes effect that radius
        //! further on, so the arc can start where the key was pressed.
        //! Roundness 0 is the classic game: a corner at the press.
        Rule_Rounded,
        //! a turn is a quarter circle as long as D (radius 2D/pi); queued
        //! turns follow on from each other with no straight between them
        Rule_Arc
    };

    //! a point the trail passes through, and how far its rider had come there
    struct Vertex
    {
        double x, y, along;
    };

    //! a key: when (on the rider's clock), and which way, +1 right, -1 left
    struct Press
    {
        double time;
        int direction;
    };

    class Rider
    {
    public:
        // what it rides by, read every step; a turn under way keeps what it began with
        double speed, delay, roundness;
        Rule rule;
        //! how long the drawn heading takes to follow a corner, in seconds
        //! (the time constant; it is all but there after three); 0 follows at once
        double yawEase;

        // where it is
        double x, y, heading, time, along;
        //! the heading it is drawn with, how fast that turns, and its lean (right positive)
        double yaw, yawRate, lean, leanRate;
        //! turns made since the reset, and how far it had come when each was done
        std::vector< double > turnEnds;
        //! Its trail, oldest first: where it was when it turned, and points
        //! along its arcs no more than a piece apart. The head is (x, y, along).
        std::deque< Vertex > trail;

        // the trail's pieces along an arc, and how much of it is kept
        static double MaxPiece(){ return .5; }
        static double MaxPieceTurn(){ return .2618; }   // 15 degrees
        static double MaxTrail(){ return 600; }
        static size_t MaxVertices(){ return 1600; }

        // the lean: like a rider's, by speed times how fast it turns, up to a limit, with a spring's ease
        static double LeanPerAcceleration(){ return .0011; }
        static double MaxLean(){ return .4363; }        // 25 degrees
        static double LeanSpring(){ return 25; }

        Rider()
        : speed( 30 ), delay( .25 ), roundness( 0 ), rule( Rule_Rounded ), yawEase( 0 )
        {
            Reset( 0, 0, quarter );
        }

        void Reset( double x0, double y0, double heading0 )
        {
            x = x0;
            y = y0;
            heading = yaw = heading0;
            time = along = 0;
            yawRate = lean = leanRate = 0;
            turnEnds.clear();
            trail.clear();
            Vertex const start = { x, y, 0 };
            trail.push_back( start );
            pending_.clear();
            queue_.clear();
            s_ = 0;
            firstLine_ = true;
            planned_ = false;
            planDirection_ = 0;
            planCorner_ = 0;
            turning_ = 0;
            arcLeft_ = arcRate_ = arcSpeed_ = arcRadius_ = 0;
            vertexHeading_ = heading;
        }

        //! a key pressed at time (the rider's clock); presses are taken in time order
        void Press( double at, int direction )
        {
            gCurveLab::Press const press = { at, direction > 0 ? 1 : -1 };
            std::deque< gCurveLab::Press >::iterator i = pending_.end();
            while ( i != pending_.begin() && ( i - 1 )->time > at )
                --i;
            pending_.insert( i, press );
        }

        //! nothing pressed, planned or under way
        bool Idle() const
        {
            return pending_.empty() && queue_.empty() && !planned_ && !turning_;
        }

        //! the radius of the turn under way, 0 on a straight
        double ArcRadius() const { return turning_ ? arcRadius_ : 0; }

        //! the delay distance, and the corners' radius under the rounded rule
        double DelayDistance() const { return speed * delay; }
        double CornerRadius() const { return roundness * DelayDistance(); }

        //! One step of the given length (or less, where an arc ends inside it).
        void Step( double step )
        {
            while ( !pending_.empty() && pending_.front().time <= time )
            {
                queue_.push_back( pending_.front().direction );
                pending_.pop_front();
            }

            double const D = DelayDistance();
            bool arcStarts = false, corner = false, arcEnds = false;
            if ( rule == Rule_Arc )
            {
                if ( !turning_ && !queue_.empty() )
                {
                    StartArc( queue_.front(), D / quarter );
                    queue_.pop_front();
                    arcStarts = true;
                }
            }
            else
            {
                double const r = roundness * D;
                // where the corner goes is settled when the press is taken:
                // the radius further on, and not nearer the last one than D
                if ( !turning_ && !planned_ && !queue_.empty() )
                {
                    planCorner_ = firstLine_ ? s_ + r : std::max( s_ + r, D );
                    planDirection_ = queue_.front();
                    queue_.pop_front();
                    planned_ = true;
                }
                if ( !turning_ && planned_ && s_ >= planCorner_ - r - 1E-9 )
                {
                    if ( r < 1E-9 )
                    {
                        // the classic corner, at once
                        AddVertex();
                        heading -= planDirection_ * quarter;
                        s_ = 0;
                        firstLine_ = false;
                        planned_ = false;
                        corner = true;
                    }
                    else
                    {
                        StartArc( planDirection_, r );
                        arcStarts = true;
                    }
                }
            }
            // the trail turns where the arc starts
            if ( arcStarts )
                AddVertex();

            double dt = step, v = speed;
            if ( turning_ )
            {
                dt = std::min( step, arcLeft_ );
                v = arcSpeed_;
                heading -= turning_ * arcRate_ * dt;
                arcLeft_ -= dt;
                if ( arcLeft_ <= 1E-12 )
                {
                    if ( rule == Rule_Rounded )
                    {
                        heading = std::floor( heading / quarter + .5 ) * quarter;
                        planned_ = false;
                        s_ = arcRadius_;
                        firstLine_ = false;
                    }
                    turning_ = 0;
                    arcEnds = true;
                }
            }
            else
                s_ += v * dt;

            double const before = along;
            x += std::cos( heading ) * v * dt;
            y += std::sin( heading ) * v * dt;
            time += dt;
            along += v * dt;

            if ( corner )
                turnEnds.push_back( before );
            if ( arcEnds )
            {
                // and goes straight again where it ends
                AddVertex();
                turnEnds.push_back( along );
            }
            else if ( turning_ && ( along - trail.back().along >= MaxPiece() - 1E-9 ||
                                    std::fabs( heading - vertexHeading_ ) >= MaxPieceTurn() ) )
                AddVertex();

            Wrap();
            Pose( dt, v );
            Trim();
        }

    private:
        std::deque< gCurveLab::Press > pending_;
        std::deque< int > queue_;
        double s_;                  // how far along the current line from its corner
        bool firstLine_;            // the line it started on, which has no corner behind it
        bool planned_;              // a turn whose corner is settled and not yet reached
        int planDirection_;
        double planCorner_;
        int turning_;               // the arc under way: +1 right, -1 left, 0 none
        double arcLeft_, arcRate_, arcSpeed_, arcRadius_;
        double vertexHeading_;      // the heading at the trail's last point

        void StartArc( int direction, double radius )
        {
            turning_ = direction;
            arcRadius_ = radius;
            arcSpeed_ = speed;
            arcRate_ = speed / radius;
            arcLeft_ = quarter * radius / speed;
        }

        void AddVertex()
        {
            Vertex const v = { x, y, along };
            if ( !trail.empty() && along - trail.back().along < 1E-9 )
                trail.back() = v;
            else
                trail.push_back( v );
            vertexHeading_ = heading;
        }

        // the heading kept near the range of an angle, a whole turn at a time
        void Wrap()
        {
            double const whole = 4 * quarter;
            while ( heading > 2 * whole )
            {
                heading -= whole;
                yaw -= whole;
                vertexHeading_ -= whole;
            }
            while ( heading < -2 * whole )
            {
                heading += whole;
                yaw += whole;
                vertexHeading_ += whole;
            }
        }

        // the drawn heading follows the heading, and the lean follows how fast that turns
        void Pose( double dt, double v )
        {
            if ( !( dt > 1E-7 ) )
                return;
            double const before = yaw;
            if ( yawEase > 0 )
                yaw += ( heading - yaw ) * ( 1 - std::exp( -dt / yawEase ) );
            else
                yaw = heading;
            yawRate = ( yaw - before ) / dt;

            double target = -LeanPerAcceleration() * v * yawRate;
            target = std::max( -MaxLean(), std::min( MaxLean(), target ) );
            double const w = LeanSpring();
            leanRate += ( w * w * ( target - lean ) - 2 * w * leanRate ) * dt;
            lean += leanRate * dt;
        }

        // the oldest part goes where the trail is longer than it is kept
        void Trim()
        {
            double const cut = along - MaxTrail();
            while ( trail.size() >= 2 && ( trail[1].along <= cut || trail.size() > MaxVertices() ) )
                trail.pop_front();
            Vertex & first = trail.front();
            if ( first.along < cut )
            {
                Vertex const next = trail.size() >= 2 ? trail[1] : Vertex();
                double const nx = trail.size() >= 2 ? next.x : x, ny = trail.size() >= 2 ? next.y : y;
                double const na = trail.size() >= 2 ? next.along : along;
                double const f = na > first.along ? ( cut - first.along ) / ( na - first.along ) : 1;
                first.x += ( nx - first.x ) * f;
                first.y += ( ny - first.y ) * f;
                first.along = cut;
            }
        }
    };

    // ---- Jamie's sequence ----

    //! "delays can make big circles and then you can reverse triple left
    //! right right right to do a U turn and go back into the big circle
    //! regular triple right right right left", as rcl-cfg/curves/big_circle.py
    //! (sequence(), one lap) presses it: a lap of the circle, right once a
    //! delay; the reverse triple L R R R, `quick` apart; `gap` of riding after
    //! its four turns; the regular triple R R R L; `gap` again; a lap of the
    //! circle again. From t0 on; mirror -1 swaps every key. marks: where the
    //! four parts start, and where it is over (the last circle press and .6 s).
    inline void BigCircle( double delay, double gap, double t0, double quick, int mirror,
                           std::vector< Press > & presses, double marks[5] )
    {
        static const int lap = 4;
        static const int reverse[4] = { -1, 1, 1, 1 }, regular[4] = { 1, 1, 1, -1 };
        presses.clear();
        double t = t0;
        marks[0] = t0;
        for ( int i = 0; i < lap; ++i )
        {
            Press const p = { t, mirror };
            presses.push_back( p );
            t += delay;
        }
        double const rev = t;
        marks[1] = rev;
        for ( int i = 0; i < 4; ++i )
        {
            Press const p = { rev + i * quick, reverse[i] * mirror };
            presses.push_back( p );
        }
        double const reg = rev + 4 * delay + gap;
        marks[2] = reg;
        for ( int i = 0; i < 4; ++i )
        {
            Press const p = { reg + i * quick, regular[i] * mirror };
            presses.push_back( p );
        }
        double const again = reg + 4 * delay + gap;
        marks[3] = again;
        for ( int i = 0; i < lap; ++i )
        {
            Press const p = { again + i * delay, mirror };
            presses.push_back( p );
        }
        marks[4] = again + lap * delay + .6;
    }

    // ---- measures ----

    inline double SegmentDistance( double px, double py, double ax, double ay, double bx, double by )
    {
        double const dx = bx - ax, dy = by - ay;
        double const length = dx * dx + dy * dy;
        double f = length > 0 ? ( ( px - ax ) * dx + ( py - ay ) * dy ) / length : 0;
        f = std::max( 0.0, std::min( 1.0, f ) );
        double const ex = ax + dx * f - px, ey = ay + dy * f - py;
        return std::sqrt( ex * ex + ey * ey );
    }

    //! the rider's trail with its head on the end
    inline Vertex TrailPoint( Rider const & rider, size_t i )
    {
        if ( i < rider.trail.size() )
            return rider.trail[i];
        Vertex const head = { rider.x, rider.y, rider.along };
        return head;
    }

    //! The nearest the rider's head is to its own trail, leaving out the last
    //! `gap` behind it; a large number where there is nothing older.
    inline double NearestOwnTrail( Rider const & rider, double gap )
    {
        double const until = rider.along - gap;
        double best = 1E30;
        size_t const count = rider.trail.size() + 1;
        for ( size_t i = 0; i + 1 < count; ++i )
        {
            Vertex const a = TrailPoint( rider, i );
            if ( a.along >= until )
                break;
            Vertex b = TrailPoint( rider, i + 1 );
            if ( b.along > until )
            {
                double const f = ( until - a.along ) / ( b.along - a.along );
                b.x = a.x + ( b.x - a.x ) * f;
                b.y = a.y + ( b.y - a.y ) * f;
                b.along = until;
            }
            // a quick look first: the head cannot be nearer than the box round the piece
            double const lowX = std::min( a.x, b.x ), highX = std::max( a.x, b.x );
            double const lowY = std::min( a.y, b.y ), highY = std::max( a.y, b.y );
            double const outX = std::max( 0.0, std::max( lowX - rider.x, rider.x - highX ) );
            double const outY = std::max( 0.0, std::max( lowY - rider.y, rider.y - highY ) );
            if ( outX >= best || outY >= best )
                continue;
            best = std::min( best, SegmentDistance( rider.x, rider.y, a.x, a.y, b.x, b.y ) );
        }
        return best;
    }

    //! where on the rider's trail it had come `at` along
    inline Vertex TrailAt( Rider const & rider, double at )
    {
        size_t const count = rider.trail.size() + 1;
        Vertex a = TrailPoint( rider, 0 );
        if ( at <= a.along )
            return a;
        for ( size_t i = 1; i < count; ++i )
        {
            Vertex const b = TrailPoint( rider, i );
            if ( b.along >= at )
            {
                double const f = b.along > a.along ? ( at - a.along ) / ( b.along - a.along ) : 1;
                Vertex const v = { a.x + ( b.x - a.x ) * f, a.y + ( b.y - a.y ) * f, at };
                return v;
            }
            a = b;
        }
        return a;
    }

    //! How far the path's trail between `from` and `to` (in how far it had
    //! come) gets from the line's trail: the largest of the nearest
    //! distances, looked at every `every` along the way.
    inline double FarthestFrom( Rider const & path, double from, double to, Rider const & line, double every )
    {
        double worst = 0;
        size_t const count = line.trail.size() + 1;
        for ( double at = from; at <= to + 1E-9; at += every )
        {
            Vertex const p = TrailAt( path, at );
            double nearest = 1E30;
            for ( size_t i = 0; i + 1 < count; ++i )
            {
                Vertex const a = TrailPoint( line, i ), b = TrailPoint( line, i + 1 );
                nearest = std::min( nearest, SegmentDistance( p.x, p.y, a.x, a.y, b.x, b.y ) );
            }
            worst = std::max( worst, nearest );
        }
        return worst;
    }
}

#endif
