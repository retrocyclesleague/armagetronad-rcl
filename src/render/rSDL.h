#ifndef AT_SDL_H
#define AT_SDL_H

#include "config.h"

#ifndef DEDICATED
#include <SDL3/SDL.h>
#else
#define SDLK_UNKNOWN       0
#define SDL_SCANCODE_UNKNOWN 0
#define SDL_SCANCODE_COUNT 512
#define SDLK_LAST          0
typedef int SDL_Keycode;
typedef int SDL_Scancode;
struct SDL_KeyboardEvent { SDL_Keycode key; SDL_Scancode scancode; };
typedef int SDL_Event;
typedef unsigned char Uint8;
typedef unsigned int Uint32;
typedef int SDL_AudioSpec;
#endif

#endif
