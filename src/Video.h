// Copyright (c) 2024 FBLabs
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include "Common.h"
#include "Device.h"
#include "Bus.h"

struct SRGB {
	byte red;
	byte green;
	byte blue;
};

class CVideo final : public CDevice {
public:
	CVideo(CBus& bus, byte* ramPtr);
	byte read(const word addr, const uint64_t cycles) override;
	void write(const word addr, const byte data, const uint64_t cycles) override;
	void update(const uint64_t cycles) override {};
	void reset() override;
	//
	SRGB* getFrameBuffer();
	// Savestate support (libretro port)
	struct SState {
		bool videoMono;
		bool secondPage;
	};
	SState saveState() const { return { mVideoMono, mSecondPage }; }
	void loadState(const SState& s) { mVideoMono = s.videoMono; mSecondPage = s.secondPage; }
private:
	byte* mRamPtr = nullptr;
	bool mVideoMono{ false };
	bool mSecondPage{ false };
	// TEMP INSTRUMENTATION (2026-09-27): AN0-AN2 ($C058-$C05D) were
	// completely unregistered (fell through Bus to a no-op default) --
	// unlike AN3 ($C05E/$C05F), which Keyboard.cpp already repurposes for
	// TK2000's SHIFT/CTRL sensing. Tracked here as plain on/off flags
	// (standard Apple II annunciator semantics: even addr = off, odd =
	// on) purely so accesses are no longer silently dropped and so they
	// show up in the debug log. Not yet wired to anything else -- if the
	// Karateka "RETIRE SUA INTERFACE" hang turns out to depend on one of
	// these being sensed elsewhere (e.g. echoed onto the cassette input
	// or a paddle line), that's the next step once the log confirms it.
	bool mAn0{ false };
	bool mAn1{ false };
	bool mAn2{ false };
	SRGB mFrameBuffer[VIDEOWIDTH * VIDEOHEIGHT]{ 0 };
	void drawMono();
	void drawColor();
};
