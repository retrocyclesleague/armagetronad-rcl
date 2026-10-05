/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

***************************************************************************

*/

#include "eSound.h"
#include "config.h"
#include "tMemManager.h"
#include "tDirectories.h"
#include "tRandom.h"
#include "tError.h"
#include <string>
#include "tConfiguration.h"
#include "uMenu.h"
#include "eCamera.h"
//#include "tList.h"
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include "eGrid.h"
#include "tException.h"

//eGrid* eSoundPlayer::S_Grid = NULL;

// Legacy Windows IDE builds link SDL_mixer without an autoconf feature check.
// Autoconf builds must honor HAVE_LIBSDL_MIXER from their generated config.h.
#if defined(WIN32) && !defined(HAVE_CONFIG_H)
#define HAVE_LIBSDL_MIXER 1
#endif

#ifndef DEDICATED
#ifdef  HAVE_LIBSDL_MIXER
#include <SDL_mixer.h>
static Mix_Music* music = NULL;
#endif

static SDL_AudioSpec audio;
static bool sound_is_there=false;
static bool uses_sdl_mixer=false;
#endif

// sound quality

#define SOUND_OFF 0
#define SOUND_LOW 1
#define SOUND_MED 2
#define SOUND_HIGH 3

// How much the mixer fills at a time: a sound waits for the piece after the
// one being filled, so this is how late it can start, and how unevenly. On
// Windows that used to be 46 ms, from the days of DirectSound drivers that
// crackled with less; SDL2 feeds the device in its own time whatever is asked
// for here, so 12 ms costs nothing.
#ifdef WIN32
static int buffer_shift=-1;
#else
static int buffer_shift=0;
#endif

static tConfItem<int> bs("SOUND_BUFFER_SHIFT",buffer_shift);

// Profiles written by earlier builds have the old size saved as if it had
// been chosen. Bring it down once (settings_client.cfg asks for it after
// user.cfg has loaded); the persisted marker keeps whatever is chosen in the
// sound menu afterwards.
static bool se_soundLatencyApplied = false;
static tConfItem<bool> se_soundLatencyAppliedConf(
    "RCL_SOUND_LATENCY_APPLIED", se_soundLatencyApplied);

static void se_ApplySoundLatency(std::istream &)
{
    if (se_soundLatencyApplied)
        return;
    se_soundLatencyApplied = true;

#ifdef WIN32
    if (buffer_shift > -1)
        buffer_shift = -1;
#endif
}

static tConfItemFunc se_applySoundLatencyConf(
    "RCL_APPLY_SOUND_LATENCY", &se_ApplySoundLatency);

static int sound_quality=SOUND_MED;
static tConfItem<int> sq("SOUND_QUALITY",sound_quality);

static int sound_sources=10;
static tConfItem<int> ss("SOUND_SOURCES",sound_sources);

// how loud the whole mix is; 1 is the level the sounds were balanced for
static REAL sound_volume=1;
static tConfItem<REAL> sv("SOUND_VOLUME",sound_volume);
static REAL loudness_thresh=0;
static int real_sound_sources=0;

static tList<eSoundPlayer> se_globalPlayers;

#ifndef DEDICATED
// ---- sounds played once, for the whole machine ----

namespace
{
struct eOneShot
{
    eWavData * wav;     // what plays; none if the voice is free
    eAudioPos  pos;
    REAL       volume, speed;
};

enum { se_oneShotVoices = 12 };
eOneShot se_oneShots[se_oneShotVoices];

// The game asks for them here and the mixer picks them up when it next runs,
// so asking never waits for the mixer: a ring the game writes and the mixer
// reads.
struct eOneShotRequest
{
    eWavData * wav;
    REAL       volume, speed;
};

enum { se_oneShotRequests = 16 };
eOneShotRequest se_oneShotRequest[se_oneShotRequests];
std::atomic< unsigned int > se_oneShotsAsked( 0 );  // counted up by the game
unsigned int se_oneShotsTaken = 0;                  // followed by the mixer

// starts what was asked for since the mixer last ran
void se_TakeOneShots()
{
    unsigned int const asked = se_oneShotsAsked.load( std::memory_order_acquire );

    // more than the ring holds: the oldest are gone
    if ( asked - se_oneShotsTaken > se_oneShotRequests )
        se_oneShotsTaken = asked - se_oneShotRequests;

    for ( ; se_oneShotsTaken != asked; ++se_oneShotsTaken )
    {
        eOneShotRequest const & request = se_oneShotRequest[ se_oneShotsTaken % se_oneShotRequests ];

        // take a free voice; if there is none, the one that is furthest along
        eOneShot * voice = &se_oneShots[0];
        for ( int i = 0; i < se_oneShotVoices; ++i )
        {
            eOneShot & candidate = se_oneShots[i];
            if ( !candidate.wav )
            {
                voice = &candidate;
                break;
            }
            if ( candidate.pos.pos > voice->pos.pos )
                voice = &candidate;
        }

        voice->wav = request.wav;
        voice->pos.Reset();
        voice->volume = request.volume;
        voice->speed = request.speed;
    }
}

// sources the mixer has been told not to leave out or count (eSoundAlways)
int se_mixingAlways = 0;

// how long eSoundPlayer::Play() requests waited for the mixer, in
// milliseconds; reported next to an RCL_AUDIO_DUMP
unsigned int se_playWaits = 0, se_playWaitTotal = 0, se_playWaitMost = 0;

// Everything is mixed here before it reaches the device, with room over full
// scale, so a busy moment can be rounded off instead of clipped.
std::vector< int > se_mixBuffer;

inline int se_SoftClip( int value )
{
    // straight up to three fifths of full scale, bending towards it above
    static const int knee = 19660, room = 13107;
    int const size = value < 0 ? -value : value;
    if ( size <= knee )
        return value;
    int const shaped = knee + int( ( room - 1 ) * tanhf( ( size - knee ) * ( 1.0f / room ) ) );
    return value < 0 ? -shaped : shaped;
}
}
#endif

void se_PlaySound( eWavData & wav, REAL volume, REAL speed )
{
#ifndef DEDICATED
    if ( !sound_is_there || !( volume > 0 ) )
        return;

    // read the file here, not on the audio thread
    wav.Load();

    unsigned int const asked = se_oneShotsAsked.load( std::memory_order_relaxed );
    eOneShotRequest & request = se_oneShotRequest[ asked % se_oneShotRequests ];
    request.wav = &wav;
    // a full voice is what the mixer lets any one sound have
    request.volume = volume * .25f;
    request.speed = speed;
    se_oneShotsAsked.store( asked + 1, std::memory_order_release );
#endif
}

bool se_SoundAudible( REAL rvol, REAL lvol )
{
#ifndef DEDICATED
    if ( se_mixingAlways > 0 )
        return true;
    if ( rvol + lvol > loudness_thresh )
    {
        real_sound_sources++;
        return true;
    }
#endif
    return false;
}

eSoundAlways::eSoundAlways( bool on )
        :on_( on )
{
#ifndef DEDICATED
    if ( on_ )
        ++se_mixingAlways;
#endif
}

eSoundAlways::~eSoundAlways()
{
#ifndef DEDICATED
    if ( on_ )
        --se_mixingAlways;
#endif
}

REAL se_SoundSeconds( unsigned int len )
{
#ifndef DEDICATED
    // two channels of 16 bits
    if ( audio.freq > 0 )
        return len / ( 4.0f * audio.freq );
#endif
    return 0;
}

void fill_audio(void *udata, Uint8 *stream, int len)
{
#ifndef DEDICATED
    real_sound_sources=0;
    int i;

    // 16 bit samples for both channels go out; mix them with room to spare
    int const count = len / 2;
    se_mixBuffer.assign( count, 0 );
    if ( count <= 0 )
        return;
    Uint8 * const mix = reinterpret_cast< Uint8 * >( &se_mixBuffer[0] );

    if (eGrid::CurrentGrid())
        for(i=eGrid::CurrentGrid()->Cameras().Len()-1;i>=0;i--)
        {
            eCamera *pCam = eGrid::CurrentGrid()->Cameras()(i);
            if(pCam)
                pCam->SoundMix(mix,len);
        }

    for(i=se_globalPlayers.Len()-1;i>=0;i--)
        se_globalPlayers(i)->Mix(mix,len,0,1,1);

    se_TakeOneShots();
    for ( i = 0; i < se_oneShotVoices; ++i )
    {
        eOneShot & voice = se_oneShots[i];
        if ( voice.wav && voice.wav->Mix( mix, len, voice.pos, voice.volume, voice.volume, voice.speed ) )
            voice.wav = NULL;
    }

    // The sources are mixed quietly, as they always were: each is held to a
    // quarter of full scale and most sit far below that. Bring the sum up to
    // a level like other programs', times what the player asked for.
    REAL volume = sound_volume;
    if ( !( volume > 0 ) ) volume = 0;
    if ( volume > 2 ) volume = 2;
    float const gain = 3.0f * volume;

    // onto whatever is in the stream already (music, where there is any)
    short * const out = reinterpret_cast< short * >( stream );
    for ( i = 0; i < count; ++i )
    {
        int value = out[i] + se_SoftClip( int( se_mixBuffer[i] * gain ) );
        if ( value > 32767 ) value = 32767;
        if ( value < -32768 ) value = -32768;
        out[i] = value;
    }

    // A way to check the mix without a speaker: with RCL_AUDIO_DUMP set to a
    // file name, what would be played is written there (16 bit stereo at the
    // device's rate, no header) and the device gets silence instead.
    static FILE * dump = NULL;
    static bool dumpChecked = false;
    if ( !dumpChecked )
    {
        dumpChecked = true;
        char const * const name = getenv( "RCL_AUDIO_DUMP" );
        if ( name && *name )
            dump = fopen( name, "wb" );
    }
    if ( dump )
    {
        fwrite( stream, 1, len, dump );
        fflush( dump );
        memset( stream, 0, len );

        // And now and then, next to it, what it is being made with, and how
        // long the mixer was kept waiting between two pieces: the game holds
        // it up while it moves the world, and a wait much longer than a
        // piece is a gap the device may have had nothing to play in.
        static int pieces = 0;
        static Uint32 lastPiece = 0;
        static unsigned int longestWait = 0, longWaits = 0;
        // how far apart the pieces come: under 3 ms, 3-7, 8-12, 13-17, 18-25, 26-40, more
        static unsigned int apart[7] = { 0, 0, 0, 0, 0, 0, 0 };
        Uint32 const now = SDL_GetTicks();
        unsigned int const pieceTime = audio.freq > 0 ? ( len * 250u ) / audio.freq : 0;
        if ( pieces > 8 )
        {
            unsigned int const waited = now - lastPiece;
            if ( waited > longestWait )
                longestWait = waited;
            if ( waited > 2 * pieceTime + 10 )
                ++longWaits;
            ++apart[ waited < 3 ? 0 : waited < 8 ? 1 : waited < 13 ? 2 : waited < 18 ? 3 :
                     waited < 26 ? 4 : waited < 41 ? 5 : 6 ];
        }
        lastPiece = now;

        if ( ( pieces++ & 63 ) == 0 )
        {
            std::string const about = std::string( getenv( "RCL_AUDIO_DUMP" ) ) + ".txt";
            if ( FILE * const said = fopen( about.c_str(), "w" ) )
            {
                fprintf( said, "rate %d\nbuffer %d\npiece_ms %u\npieces %d\n"
                         "longest_wait_between_pieces_ms %u\nwaits_over_two_pieces_and_10ms %u\n"
                         "pieces_apart_ms_under3_3to7_8to12_13to17_18to25_26to40_more %u %u %u %u %u %u %u\n"
                         "plays %u\nplay_wait_mean_ms %.2f\nplay_wait_most_ms %u\n",
                         int( audio.freq ), int( audio.samples ), pieceTime, pieces,
                         longestWait, longWaits,
                         apart[0], apart[1], apart[2], apart[3], apart[4], apart[5], apart[6], se_playWaits,
                         se_playWaits ? float( se_playWaitTotal ) / se_playWaits : 0.0f, se_playWaitMost );
                fclose( said );
            }
        }
    }

    if (real_sound_sources>sound_sources+4)
        loudness_thresh+=.01;
    if (real_sound_sources>sound_sources+1)
        loudness_thresh+=.001;
    if (real_sound_sources<sound_sources-4)
        loudness_thresh-=.001;
    if (real_sound_sources<sound_sources-1)
        loudness_thresh-=.0001;
    if (loudness_thresh<0)
        loudness_thresh=0;
#endif
}

#ifndef DEDICATED
#ifdef DEFAULT_SDL_AUDIODRIVER

// stringification, yep, two levels required
#define XSTRING(s) #s
#define STRING(s) XSTRING(s)

// call once to initialize SDL sound subsystem
static bool se_SoundInitPrepare()
{
    // initialize audio subsystem with predefined, hopefully good, driver
    if ( ! getenv("SDL_AUDIODRIVER") ) {
        char * arg = "SDL_AUDIODRIVER=" STRING(DEFAULT_SDL_AUDIODRIVER);
        putenv(arg);

        if ( SDL_InitSubSystem(SDL_INIT_AUDIO) >= 0 )
            return true;

        putenv("SDL_AUDIODRIVER=");
    }

    // if that fails, try what the user wanted
    return ( SDL_InitSubSystem(SDL_INIT_AUDIO) >= 0 );
}
#endif
#endif

void se_SoundInit()
{
#ifndef DEDICATED
    // save configuration file with sound disabled on first use so we don't try again
    bool needSave = false;
    static bool firstRun = true;
    if ( st_FirstUse )
    {
        needSave = true;
        int sound_quality_back = sound_quality;
        sound_quality = SOUND_OFF;
        st_SaveConfig();
        if ( firstRun )
            con << tOutput("$sound_firstinit");
        sound_quality=sound_quality_back;
    }

    if ( sound_quality != SOUND_OFF )
    {
#ifdef DEFAULT_SDL_AUDIODRIVER
        static bool init = se_SoundInitPrepare();
        if ( !init )
            return;
#endif
        if ( firstRun && !SDL_WasInit( SDL_INIT_AUDIO ) )
            return;
        firstRun = false;
    }

    if (!sound_is_there && sound_quality!=SOUND_OFF)
    {
        SDL_AudioSpec desired;
        memset( &desired, 0, sizeof( SDL_AudioSpec ) );

        switch (sound_quality)
        {
        case SOUND_LOW:
            desired.freq=11025; break;
        case SOUND_MED:
            desired.freq=22050; break;
        case SOUND_HIGH:
            desired.freq=44100; break;
        default:
            desired.freq=22050;
        }

        desired.format=AUDIO_S16SYS;
        desired.samples=128;
        while (desired.samples <= desired.freq >> (6-buffer_shift))
            desired.samples <<= 1;
        desired.channels = 2;
        desired.callback = fill_audio;
        desired.userdata = NULL;

#ifdef HAVE_LIBSDL_MIXER
        uses_sdl_mixer=true;

        // init using SDL_Mixer
        sound_is_there=(Mix_OpenAudio(desired.freq, desired.format, desired.channels, desired.samples)>=0);

        if ( sound_is_there )
        {
            // query actual sound info
            audio = desired;
            int channels;
            Mix_QuerySpec( &audio.freq, &audio.format, &channels );
            audio.channels = channels;

            // register callback
            Mix_SetPostMix( &fill_audio, NULL );

            const tPath& vpath = tDirectories::Data();
            tString musFile = vpath.GetReadPath( "music/fire.xm" );

            music = Mix_LoadMUS( musFile );

            if ( music )
                Mix_FadeInMusic( music, -1, 2000 );

        }
#else
        // just use SDL to init sound
        uses_sdl_mixer=false;
        sound_is_there=(SDL_OpenAudio(&desired,&audio)>=0);
#endif
        if (sound_is_there && (audio.format!=AUDIO_S16SYS || audio.channels!=2))
        {
            uses_sdl_mixer=false;
            se_SoundExit();
            // force emulation of 16 bit stereo; sadly, this cannot use SDL_Mixer :-(
            audio.format=AUDIO_S16SYS;
            audio.channels=2;
            sound_is_there=(SDL_OpenAudio(&audio,NULL)>=0);
            con << tOutput("$sound_error_no16bit");
        }
        if (!sound_is_there)
            con << tOutput("$sound_error_initfailed");
        else
        {
            //for(int i=wavs.Len()-1;i>=0;i--)
            //wavs(i)->Init();
#ifdef DEBUG
            tOutput o;
            o.SetTemplateParameter(1,audio.freq);
            o.SetTemplateParameter(2,audio.samples);
            o << "$sound_inited";
            con << o;
#endif
            se_SoundPause(false);
        }
    }

    // save sound settings, they appear to work
    if ( needSave )
    {
        st_SaveConfig();
    }
#endif
}

void se_SoundExit(){
#ifndef DEDICATED
    {
        // the mixer must not run while the sounds it plays are unloaded
        eSoundLocker locker;

        for ( int i = 0; i < se_oneShotVoices; ++i )
            se_oneShots[i].wav = NULL;
        se_oneShotsTaken = se_oneShotsAsked.load( std::memory_order_acquire );

        eWavData::UnloadAll();
        se_SoundPause(true);
    }

    // Closing the device waits for the audio thread to end. That thread takes
    // the audio lock on every pass, so closing with the lock held (as this
    // function used to) hangs whenever the thread is waiting for it: at exit,
    // and in optimised builds at startup, where sound is initialised twice.
    if (sound_is_there){
#ifdef DEBUG
        con << tOutput("$sound_disabling");
#endif
        //		se_SoundPause(false);
        //    for(int i=wavs.Len()-1;i>=0;i--)
        //wavs(i)->Exit();

#ifdef HAVE_LIBSDL_MIXER
        if ( music )
        {
            if( Mix_PlayingMusic() )
            {
                Mix_FadeOutMusic(100);
                SDL_Delay(100);
            }
            Mix_FreeMusic( music );
            music = NULL;
        }

        se_SoundPause(true);

        if ( uses_sdl_mixer )
            Mix_CloseAudio();
        else
#endif
            SDL_CloseAudio();

#ifdef DEBUG
        con << tOutput("$sound_disabling_done");
#endif
    }
    sound_is_there=false;
#endif
}

#ifndef DEDICATED
static unsigned int locks;
#endif

void se_SoundLock(){
#ifndef DEDICATED
    if (!locks)
        SDL_LockAudio();
    locks++;
#endif
}

void se_SoundUnlock(){
#ifndef DEDICATED
    locks--;
    if (!locks)
        SDL_UnlockAudio();
#endif
}

void se_SoundPause(bool p){
#ifndef DEDICATED
    SDL_PauseAudio(p);
#endif
}

// ***********************************************************

eWavData* eWavData::s_anchor = NULL;

eWavData::eWavData(const char * fileName,const char *alternative)
        :tListItem<eWavData>(s_anchor),data(NULL),len(0),freeData(false), loadError(false), alt(false){
    //wavs.Add(this,id);
    filename     = fileName;
    filename_alt = alternative;

}

#ifndef DEDICATED

#ifdef SDL_LoadWAV
#undef SDL_LoadWAV
#endif

static SDL_AudioSpec * SDLCALL SDL_LoadWAV(char const *file, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    auto *rw = SDL_RWFromFile(file, "rb");
    if(!rw)
        return nullptr;

    return SDL_LoadWAV_RW(rw,1, spec,audio_buf,audio_len);
}

#endif

void eWavData::Load(){
    //wavs.Add(this,id);

    if (data)
    {
        loadError = false;
        return;
    }

    //return;

#ifndef DEDICATED

    static char const * errorName = "Sound Error";

    freeData = false;

    loadError = true;

    alt=false;

    const tPath& path = tDirectories::Data();

    SDL_AudioSpec *result=SDL_LoadWAV( path.GetReadPath( filename ) ,&spec,&data,&len);
    if (result!=&spec || !data){
        if (filename_alt.Len()>1){
            result=SDL_LoadWAV( path.GetReadPath( filename_alt ),&spec,&data,&len);
            if (result!=&spec || !data)
            {
                tOutput err;
                err.SetTemplateParameter(1, filename);
                err << "$sound_error_filenotfound";
                throw tGenericException(err, errorName);
            }
            else
                alt=true;
        }
        else{
            result=SDL_LoadWAV( path.GetReadPath( "sound/expl.wav" ) ,&spec,&data,&len);
            if (result!=&spec || !data)
            {
                tOutput err;
                err.SetTemplateParameter(1, "sound/expl.waw");
                err << "$sound_error_filenotfount";
                throw tGenericException(err, errorName);
            }
            else
                len=0;
        }
        /*
          tERR_ERROR("Sound file " << fileName << " not found. Have you called "
          "Armagetron from the right directory?"); */
    }

    if (spec.format==AUDIO_S16SYS)
        samples=len>>1;
    else if(spec.format==AUDIO_U8)
        samples=len;
    else
    {
        // prepare error message
        tOutput err;
        err.SetTemplateParameter(1, filename);
        err << "$sound_error_unsupported";

        // convert to 16 bit system format
        SDL_AudioCVT cvt;
        if ( -1 == SDL_BuildAudioCVT( &cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, spec.channels, spec.freq ) )
        {
            throw tGenericException(err, errorName);
        }

        cvt.buf=reinterpret_cast<Uint8 *>( malloc( len * cvt.len_mult ) );
        cvt.len=len;
        memcpy(cvt.buf, data, len);
        freeData = true;


        if ( -1 == SDL_ConvertAudio( &cvt ) )
        {
            throw tGenericException(err, errorName);
        }

        SDL_FreeWAV( data );
        data = cvt.buf;
        spec.format = AUDIO_S16SYS;
        len    = len * cvt.len_ratio;

        samples = len >> 1;
    }

    samples/=spec.channels;

#ifdef DEBUG
#ifdef LINUX
    con << "Sound file " << filename << " loaded: ";
    switch (spec.format){
    case AUDIO_S16SYS: con << "16 bit "; break;
    case AUDIO_U8: con << "8 bit "; break;
    default: con << "unknown "; break;
    }
    if (spec.channels==2)
        con << "stereo ";
    else
        con << "mono ";

    con << "at " << spec.freq << " Hz,\n";

    con << samples << " samples in " << len << " bytes.\n";

    loadError = false;
#endif
#endif
#endif
}

void eWavData::Unload(){
#ifndef DEDICATED
    loadError = false;

    //wavs.Add(this,id);
    if (data){
        eSoundLocker locker;
        if ( freeData )
        {

            free(data);

        }

        else

        {

            SDL_FreeWAV(data);

        }



        data=NULL;
        len=0;
    }
#endif
}

void eWavData::UnloadAll(){
    //wavs.Add(this,id);
    eWavData* wav = s_anchor;
    while ( wav )
    {
        wav->Unload();
        wav = wav->Next();
    }

}

eWavData::~eWavData(){
#ifndef DEDICATED
    Unload();
#endif
}

bool eWavData::Mix( Uint8* dest_u8, Uint32 playlen, eAudioPos& pos,
                    REAL Rvol, REAL Lvol, REAL Speed, bool loop )
{
#ifndef DEDICATED
    // The destination is the mixer's own buffer: one int for every 16 bit
    // sample that goes out, so sounds add up without clipping here.
    int* dest_s = reinterpret_cast<int*>( dest_u8 );

    if ( !data )
    {
        if( !loadError )
        {
            Load();
        }
        if ( !data )
        {
            return false;
        }
    }

    playlen/=4;

    //	Rvol *= 4;
    //	Lvol *= 4;

    const REAL thresh = .25;

    if ( Rvol > thresh )
    {
        Rvol = thresh;
    }

    if ( Lvol > thresh )
    {
        Lvol = thresh;
    }

#define SPEED_SHIFT 20
#define SPEED_FRACTION (1<<SPEED_SHIFT)

#define VOL_SHIFT 16
#define VOL_FRACTION (1<<VOL_SHIFT)

// far above full scale: fill_audio rounds the sum off afterwards
#define MAX_VAL ((1<<28)-1)
#define MIN_VAL (-(1<<28))

    // first, split the speed into the part before and after the decimal:
    if (Speed<0) Speed=0;

    // adjust for different sample rates:
    Speed*=spec.freq;
    Speed/=audio.freq;

    int speed=int(floor(Speed));
    int speed_fraction=int(SPEED_FRACTION*(Speed-speed));

    // secondly, make integers out of the volumes:
    int rvol=int(Rvol*VOL_FRACTION);
    int lvol=int(Lvol*VOL_FRACTION);


    bool goon=true;

    while (goon){
        if (spec.channels==2){
            if (spec.format==AUDIO_U8)
                while (playlen>0 && pos.pos<samples){
                    // fix endian problems for the Mac port, as well as support for other
                    // formats than  stereo...
                    int l = dest_s[0];
                    int r = dest_s[1];
                    r += (rvol*(data[(pos.pos<<1)  ]-128)) >> (VOL_SHIFT-8);
                    l += (lvol*(data[(pos.pos<<1)+1]-128)) >> (VOL_SHIFT-8);
                    if (r>MAX_VAL) r=MAX_VAL;
                    if (l>MAX_VAL) l=MAX_VAL;
                    if (r<MIN_VAL) r=MIN_VAL;
                    if (l<MIN_VAL) l=MIN_VAL;

                    dest_s[0] = l;
                    dest_s[1] = r;

                    dest_s += 2;

                    pos.pos+=speed;

                    pos.fraction+=speed_fraction;
                    while (pos.fraction>=SPEED_FRACTION){
                        pos.fraction-=SPEED_FRACTION;
                        pos.pos++;
                    }

                    playlen--;
                }
            else{
                auto data_s = reinterpret_cast<short const*>( data );
                while (playlen>0 && pos.pos<samples){
                    int l = dest_s[0];
                    int r = dest_s[1];
                    r += ( rvol * ( data_s[( pos.pos << 1 )] ) ) >> VOL_SHIFT;
                    l += ( lvol * ( data_s[( pos.pos << 1 ) + 1] ) ) >> VOL_SHIFT;
                    if (r>MAX_VAL) r=MAX_VAL;
                    if (l>MAX_VAL) l=MAX_VAL;
                    if (r<MIN_VAL) r=MIN_VAL;
                    if (l<MIN_VAL) l=MIN_VAL;

                    dest_s[0] = l;
                    dest_s[1] = r;

                    dest_s += 2;

                    pos.pos+=speed;

                    pos.fraction+=speed_fraction;
                    while (pos.fraction>=SPEED_FRACTION){
                        pos.fraction-=SPEED_FRACTION;
                        pos.pos++;
                    }
                    playlen--;
                }
            }
        }
        else{
            if (spec.format==AUDIO_U8){
                while (playlen>0 && pos.pos<samples){
                    // fix endian problems for the Mac port, as well as support for other
                    // formats than  stereo...
                    int l = dest_s[0];
                    int r = dest_s[1];
                    int d=data[pos.pos]-128;
                    l += (lvol*d) >> (VOL_SHIFT-8);
                    r += (rvol*d) >> (VOL_SHIFT-8);
                    if (r>MAX_VAL) r=MAX_VAL;
                    if (l>MAX_VAL) l=MAX_VAL;
                    if (r<MIN_VAL) r=MIN_VAL;
                    if (l<MIN_VAL) l=MIN_VAL;

                    dest_s[0] = l;
                    dest_s[1] = r;

                    dest_s += 2;

                    pos.pos+=speed;

                    pos.fraction+=speed_fraction;
                    while (pos.fraction>=SPEED_FRACTION){
                        pos.fraction-=SPEED_FRACTION;
                        pos.pos++;
                    }

                    playlen--;
                }
            }
            else
            {
                auto data_s = reinterpret_cast<short const*>( data );
                while (playlen>0 && pos.pos<samples){
                    int l = dest_s[0];
                    int r = dest_s[1];

                    // between this sample and the next, by how far the
                    // position has got into it; without that, a sound played
                    // slower or faster than recorded turns gritty
                    Uint32 const next = pos.pos+1 < samples ? pos.pos+1 : ( loop ? 0 : pos.pos );
                    int d = data_s[pos.pos];
                    d += ( ( data_s[next] - d ) * int( pos.fraction >> (SPEED_SHIFT-8) ) ) >> 8;

                    l += (lvol*d) >> VOL_SHIFT;
                    r += (rvol*d) >> VOL_SHIFT;
                    if (r>MAX_VAL) r=MAX_VAL;
                    if (l>MAX_VAL) l=MAX_VAL;
                    if (r<MIN_VAL) r=MIN_VAL;
                    if (l<MIN_VAL) l=MIN_VAL;

                    dest_s[0] = l;
                    dest_s[1] = r;

                    dest_s += 2;

                    pos.pos+=speed;

                    pos.fraction+=speed_fraction;
                    while (pos.fraction>=SPEED_FRACTION){
                        pos.fraction-=SPEED_FRACTION;
                        pos.pos++;
                    }
                    playlen--;
                }
            }
        }

        if (loop && pos.pos>=samples)
            pos.pos-=samples;
        else
            goon=false;
    }
#endif
    return ( playlen > 0 );
}

void eWavData::Loop(){
#ifndef DEDICATED
    Uint8 *buff2=tNEW(Uint8) [len];

    if (buff2){
        memcpy(buff2,data,len);
        Uint32 samples;

        if (spec.format==AUDIO_U8){
            samples=len;
            for(int i=samples-1;i>=0;i--){
                Uint32 j=i+((len>>2)<<1);
                if (j>=len) j-=len;

                REAL a=fabs(100*(j/REAL(samples)-.5));
                if (a>1) a=1;
                REAL b=1-a;

                data[i]=int(a*buff2[i]+b*buff2[j]);
            }
        }
        else if (spec.format==AUDIO_S16SYS){
            samples=len>>1;
            auto data_s = reinterpret_cast<short*>( data );
            auto buff2_s = reinterpret_cast<short*>( buff2 );
            for(int i=samples-1;i>=0;i--){

                /*
                  REAL a=2*i/REAL(samples);
                  if (a>1) a=2-a;
                  REAL b=1-a;
                */


                Uint32 j=i+((samples>>2)<<1);
                while (j>=samples) j-=samples;

                REAL a=fabs(100*(j/REAL(samples)-.5));
                if (a>1) a=1;
                REAL b=1-a;

                data_s[i] = int( a * buff2_s[i] + b * buff2_s[j] );
            }
        }
        delete[] buff2;
    }

#endif
}


// ******************************************************************

void eAudioPos::Reset(int randomize){
#ifndef DEDICATED
    if (randomize){
        tRandomizer & randomizer = tRandomizer::GetInstance();
        fraction = randomizer.Get( SPEED_FRACTION );
        // fraction=int(SPEED_FRACTION*(rand()/float(RAND_MAX)));
        pos=randomizer.Get( randomize );
        // pos=int(randomize*(rand()/float(RAND_MAX)));
    }
    else
        fraction=pos=0;
#endif
}



eSoundPlayer::eSoundPlayer(eWavData &w,bool l)
        :id(-1),wav(&w),loop(l),played_(0),playedAt_(0){
    if (l)
        wav->Load();

    for(int i=MAX_VIEWERS-1;i>=0;i--){
        goon[i]=true;
        followed_[i]=0;
    }
}

eSoundPlayer::~eSoundPlayer()
{
    eSoundLocker locker;
    se_globalPlayers.Remove(this,id);
}

bool eSoundPlayer::Mix(Uint8 *dest,
                       Uint32 len,
                       int viewer,
                       REAL rvol,
                       REAL lvol,
                       REAL speed){

#ifndef DEDICATED
    // Play() was called since this viewer last heard the sound. One that
    // could not be followed in time (nothing was mixed, or the source was
    // too quiet for a voice) is let go: late, it would say something else.
    unsigned int const played = played_.load( std::memory_order_acquire );
    if ( played != followed_[viewer] )
    {
        followed_[viewer] = played;
        unsigned int const waited = SDL_GetTicks() - playedAt_.load( std::memory_order_relaxed );
        if ( waited < 200 )
        {
            pos[viewer].Reset();
            goon[viewer] = true;

            ++se_playWaits;
            se_playWaitTotal += waited;
            if ( waited > se_playWaitMost )
                se_playWaitMost = waited;
        }
    }

    if (goon[viewer]){
        if ( se_mixingAlways > 0 )
            return goon[viewer]=!wav->Mix(dest,len,pos[viewer],rvol,lvol,speed,loop);

        if (rvol+lvol>loudness_thresh){
            real_sound_sources++;
            return goon[viewer]=!wav->Mix(dest,len,pos[viewer],rvol,lvol,speed,loop);
        }

        // Too quiet for a voice. A loop waits where it is. A sound that
        // plays once is over: picked up later, it would come out of nowhere.
        if ( !loop )
            goon[viewer] = false;
        return loop;
    }
#endif
    return false;
}

void eSoundPlayer::Reset(int randomize){
    wav->Load();

    for(int i=MAX_VIEWERS-1;i>=0;i--){
        pos[i].Reset(randomize);
        goon[i]=true;
    }
}

void eSoundPlayer::Play(){
#ifndef DEDICATED
    // read the file here, not on the audio thread
    wav->Load();

    playedAt_.store( SDL_GetTicks(), std::memory_order_relaxed );
    played_.fetch_add( 1, std::memory_order_release );
#endif
}

void eSoundPlayer::End(){
    for(int i=MAX_VIEWERS-1;i>=0;i--){
        goon[i]=false;
    }
}


void eSoundPlayer::MakeGlobal(){
    wav->Load();

    eSoundLocker locker;
    se_globalPlayers.Add(this,id);
}

// ***************************************************************

// The menus know nothing of audio; they say what happened and this plays it.
// The same thing always sounds the same.
static eWavData se_menuMove("sound/ui_hover.wav");
static eWavData se_menuActivate("sound/ui_activate.wav");
static eWavData se_menuBack("sound/ui_back.wav");
static eWavData se_menuAdjust("sound/ui_adjust.wav");

static void se_MenuSound( uMenu::Sound sound )
{
    switch ( sound )
    {
    case uMenu::Sound_Move:
        se_PlaySound( se_menuMove, .45f );
        break;
    case uMenu::Sound_Activate:
        se_PlaySound( se_menuActivate, .6f );
        break;
    case uMenu::Sound_Back:
        se_PlaySound( se_menuBack, .55f );
        break;
    case uMenu::Sound_Adjust:
        se_PlaySound( se_menuAdjust, .45f );
        break;
    }
}

namespace
{
struct eMenuSoundHook
{
    eMenuSoundHook(){ uMenu::SetSoundFunc( &se_MenuSound ); }
};
eMenuSoundHook se_menuSoundHook;
}


// ***************************************************************

uMenu Sound_menu("$sound_menu_text");

static uMenuItemInt sources_men
(&Sound_menu,"$sound_menu_sources_text",
 "$sound_menu_sources_help",
 sound_sources,2,20,2);

static uMenuItemSelection<int> sq_men
(&Sound_menu,"$sound_menu_quality_text",
 "$sound_menu_quality_help",
 sound_quality);


static uSelectEntry<int> a(sq_men,
                           "$sound_menu_quality_off_text",
                           "$sound_menu_quality_off_help",
                           SOUND_OFF);
static uSelectEntry<int> b(sq_men,
                           "$sound_menu_quality_low_text",
                           "$sound_menu_quality_low_help",
                           SOUND_LOW);
static uSelectEntry<int> c(sq_men,
                           "$sound_menu_quality_medium_text",
                           "$sound_menu_quality_medium_help",
                           SOUND_MED);
static uSelectEntry<int> d(sq_men,
                           "$sound_menu_quality_high_text",
                           "$sound_menu_quality_high_help",
                           SOUND_HIGH);

static uMenuItemSelection<int> bm_men
(&Sound_menu,
 "$sound_menu_buffer_text",
 "$sound_menu_buffer_help",
 buffer_shift);

static uSelectEntry<int> ba(bm_men,
                            "$sound_menu_buffer_vsmall_text",
                            "$sound_menu_buffer_vsmall_help",
                            -2);

static uSelectEntry<int> bb(bm_men,
                            "$sound_menu_buffer_small_text",
                            "$sound_menu_buffer_small_help",
                            -1);

static uSelectEntry<int> bc(bm_men,
                            "$sound_menu_buffer_med_text",
                            "$sound_menu_buffer_med_help",
                            0);

static uSelectEntry<int> bd(bm_men,
                            "$sound_menu_buffer_high_text",
                            "$sound_menu_buffer_high_help",
                            1);

static uSelectEntry<int> be(bm_men,
                            "$sound_menu_buffer_vhigh_text",
                            "$sound_menu_buffer_vhigh_help",
                            2);

// the last item added is the menu's first row
static uMenuItemReal volume_men
(&Sound_menu,"$sound_menu_volume_text",
 "$sound_menu_volume_help",
 sound_volume,0,1.5f,.05f);


void se_SoundMenu(){
    //	se_SoundPause(true);
    //	se_SoundLock();
    int oldsettings=sound_quality;
    int oldshift=buffer_shift;
    Sound_menu.Enter();
    if (oldsettings!=sound_quality || oldshift!=buffer_shift){
        se_SoundExit();
        se_SoundInit();
    }
    //	se_SoundUnlock();
    //  se_SoundPause(false);
}

eSoundLocker::eSoundLocker()
{
    se_SoundLock();
}

eSoundLocker::~eSoundLocker()
{
    se_SoundUnlock();
}

