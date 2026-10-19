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

#ifndef ArmageTron_FRAMEBUFFER_H
#define ArmageTron_FRAMEBUFFER_H

#include "config.h"

#ifndef DEDICATED

#include <string>

// A picture drawn into instead of the screen. Single-sampled, its colour is a
// texture the post chain reads; multisampled, colour and depth are
// renderbuffers, resolved into a single-sampled one before anything reads
// them. The colour is half float where values over 1 are wanted, else 8 bits
// a channel; both have alpha, which the clean arena gathers its shadows in.
class rFramebuffer
{
public:
    rFramebuffer();
    ~rFramebuffer();

    //! makes it anew; depthBits 0 for none. On failure says why and holds nothing.
    bool Create( int width, int height, bool floatColour, int samples, int depthBits, std::string & error );
    //! deletes what it holds; the context must still be there
    void Release();
    //! drops what it holds without deleting it: its context is gone
    void Forget();

    bool Ready() const { return framebuffer_ != 0; }
    //! as the target of drawing and of reading
    void Bind() const;
    //! as what a blit reads from, or writes to
    void BindRead() const;
    void BindDraw() const;
    //! the screen's own framebuffer again
    static void BindScreen();
    //! the framebuffer bound for drawing now, to go back to
    static unsigned int Current();
    static void Bind( unsigned int framebuffer );

    unsigned int Texture() const { return texture_; }   //!< the colour, if single-sampled
    int Width() const { return width_; }
    int Height() const { return height_; }
    int Samples() const { return samples_; }
    bool Float() const { return float_; }
    int DepthBits() const { return depthBits_; }

private:
    rFramebuffer( rFramebuffer const & );
    rFramebuffer & operator=( rFramebuffer const & );

    unsigned int framebuffer_, texture_, colour_, depth_;
    int width_, height_, samples_, depthBits_;
    bool float_;
};

#endif // DEDICATED

#endif
