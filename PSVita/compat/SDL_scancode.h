#pragma once

// Roblox only stores SDL scancodes in InputObject and compares the handful of
// movement keys below.  Pulling the bundled desktop SDL2 header into the Vita
// build also pulls SDL_stdinc.h, whose compiler ABI assertions are not valid
// for Vita GCC.  Keep the original SDL2 numeric ABI without importing the
// desktop SDL platform layer.  When the Vita input backend is wired up it can
// translate Vita controls/keyboard events directly to these canonical values.
typedef int SDL_Scancode;

enum
{
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,
    SDL_SCANCODE_D = 7,
    SDL_SCANCODE_S = 22,
    SDL_SCANCODE_W = 26,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82
};
