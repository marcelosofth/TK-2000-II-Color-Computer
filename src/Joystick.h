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
//
// --- libretro port addition ---
// Native Apple II-style analog joystick/paddle ports, present on real
// TK2000 hardware at $C061-$C07F. Games that read the joystick directly
// (e.g. arcade-style titles like Bombardeiro.ct2) use this, not
// CKeyboard.
//
// Real hardware: writing (or reading) any address in $C070-$C07F starts
// an RC-timer countdown per paddle, proportional to that paddle's
// position (0-255); games busy-loop reading $C064-$C067 until bit 7
// clears to measure elapsed time == paddle value. We reproduce that
// timing so existing paddle-reading game code works unmodified, driven
// here by digital D-pad input mapped to the paddle extremes (0/128/255)
// instead of a real analog stick.

#pragma once

#include "Device.h"
#include "Bus.h"

class CJoystick final : public CDevice {
public:
	CJoystick(CBus& bus);
	// CDevice
	byte read(const word addr, const uint64_t cycles) override;
	void write(const word addr, const byte data, const uint64_t cycles) override;
	void update(const uint64_t cycles) override {};
	void reset() override;

	// libretro: called once per frame from CMachine with the current
	// digital D-pad/fire-button state (only while the on-screen keyboard
	// overlay is closed -- see CMachine::padEvent()).
	void padEvent(bool left, bool right, bool up, bool down, bool button0, bool button1);

	// Savestate support
	struct SState {
		byte paddleValue[4];
		bool button[3];
		unsigned long long triggerCycle[4];
	};
	SState saveState() const;
	void loadState(const SState& s);

private:
	void triggerAll(const uint64_t cycles);

	byte mPaddleValue[4]{ 128, 128, 128, 128 };	// 0=X (this port), 1=Y, 2/3=second port (unused)
	bool mButton[3]{ false, false, false };
	unsigned long long mTriggerCycle[4]{ 0, 0, 0, 0 };

	// Roughly matches real Apple II/TK2000 paddle timing at 1.02MHz
	// (~11 CPU cycles per paddle unit, plus a small fixed offset).
	static constexpr unsigned long kCyclesPerUnit = 11;
	static constexpr unsigned long kFixedOffset = 8;
};
