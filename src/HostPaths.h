// Where is the host program / this core located on disk? Used to find the
// "roms/tk2000" folder relative to the frontend. Kept in its own file so the
// Win32 headers never meet the emulator's own typedefs (Common.h).
#pragma once
#include <string>

// Folder that contains the frontend executable ("" if unknown).
std::string hostExeDir();
// Folder that contains this core's .dll/.so ("" if unknown).
std::string hostCoreDir();
