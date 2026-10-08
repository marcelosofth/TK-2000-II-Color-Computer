// Copyright (c) 2024 FBLabs
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

// Minimal diagnostic logging hook. CCpu6502 and CTape don't (and shouldn't)
// know about libretro at all; libretro.cpp implements this using whatever
// retro_log_printf_t the frontend gave it (or drops it silently if none
// was provided). Use sparingly -- this is for pinpointing emulation bugs
// (traps, tape end-of-data, etc.), not routine per-frame logging.
void tk2000_log(const char* fmt, ...);

// Forces any buffered log output to disk. Call this on shutdown -- the
// logger no longer flushes after every call (that was stalling hot,
// high-frequency codepaths like AN0-AN2 access), so without this the last
// partial buffer would otherwise be lost.
void tk2000_log_flush();
