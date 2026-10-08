// Replacement for the original pch.h (precompiled header).
// The original included <SDL.h>; the libretro core has no SDL dependency,
// so this trimmed header keeps only the standard-library includes the
// core emulation logic (CPU/Bus/RAM/ROM/Video/Audio/Tape) actually needs.
#pragma once

#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <cctype>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
