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

#include "pch.h"
#include "Joystick.h"
#include "CoreLog.h"

/*************************************************************************************************/
CJoystick::CJoystick(CBus& bus) {
	bus.addDevice(EDevices::JOYSTICK, this);
	bus.registerAddr(EDevices::JOYSTICK, 0xC061, 0xC067);
	bus.registerAddr(EDevices::JOYSTICK, 0xC070, 0xC07F);
}

/*************************************************************************************************/
void CJoystick::triggerAll(const uint64_t cycles) {
	for (int i = 0; i < 4; i++) {
		mTriggerCycle[i] = cycles;
	}
}

/*************************************************************************************************/
byte CJoystick::read(const word addr, const uint64_t cycles) {
	const word a = addr & 0x00FF;
	byte result;
	bool retriggered = false;

	if (a >= 0x70) {
		// Reading $C070-$C07F re-triggers the paddle RC timers, same as
		// real hardware (also true of writing -- see write() below).
		triggerAll(cycles);
		result = 0xFF;
		retriggered = true;
	} else {
		switch (a) {
		case 0x61:	// PB0 (joystick button 0)
			result = mButton[0] ? 0x80 : 0x00;
			break;
		case 0x62:	// PB1 (joystick button 1)
			result = mButton[1] ? 0x80 : 0x00;
			break;
		case 0x63:	// PB2 -- not wired up on this port, always released
			result = 0x00;
			break;
		case 0x64:	// PDL0 (paddle 0 / X axis)
		case 0x65:	// PDL1 (paddle 1 / Y axis)
		case 0x66:	// PDL2 (second port -- unused, always "done")
		case 0x67:	// PDL3 (second port -- unused, always "done")
		{
			const int idx = a - 0x64;
			const unsigned long long needed = kFixedOffset + (unsigned long long)mPaddleValue[idx] * kCyclesPerUnit;
			result = ((cycles - mTriggerCycle[idx]) < needed) ? 0x80 : 0x00;
			break;
		}
		default:
			result = 0xFF;
			break;
		}
	}

	// TEMP INSTRUMENTATION (2026-09-26): same change-only throttling as
	// Keyboard.cpp/LangCard.cpp -- log addr+result only when either changes,
	// plus always log $C070-$C07F retriggers (rare enough not to flood).
	static byte lastAddr = 0xFF;
	static byte lastResult = 0xFF;
	static bool first = true;
	if (first || retriggered || a != lastAddr || result != lastResult) {
		tk2000_log("joy read addr=$%04X btn0=%d btn1=%d pdl=%d,%d,%d,%d -> $%02X cycles=%llu",
			addr, mButton[0] ? 1 : 0, mButton[1] ? 1 : 0,
			mPaddleValue[0], mPaddleValue[1], mPaddleValue[2], mPaddleValue[3],
			result, (unsigned long long)cycles);
		first = false;
		lastAddr = (byte)a;
		lastResult = result;
	}
	return result;
}

/*************************************************************************************************/
void CJoystick::write(const word addr, const byte data, const uint64_t cycles) {
	if ((addr & 0x00FF) >= 0x70) {
		triggerAll(cycles);
		// TEMP INSTRUMENTATION (2026-09-26): writes to $C070-$C07F are rare
		// (only the retrigger case is wired to this address range), so log
		// unconditionally.
		tk2000_log("joy write addr=$%04X data=$%02X cycles=%llu", addr, data, (unsigned long long)cycles);
	}
}

/*************************************************************************************************/
void CJoystick::reset() {
	for (int i = 0; i < 4; i++) {
		mPaddleValue[i] = 128;
		mTriggerCycle[i] = 0;
	}
	for (int i = 0; i < 3; i++) {
		mButton[i] = false;
	}
}

/*************************************************************************************************/
void CJoystick::padEvent(bool left, bool right, bool up, bool down, bool button0, bool button1) {
	// Digital D-pad -> paddle extremes. A real paddle rests anywhere in
	// 0-255; games reading a digital joystick this way only care about
	// "pushed all the way" vs "centered", so use the extremes and let
	// left/right (or up/down) held together just cancel back to center.
	if (left && !right) {
		mPaddleValue[0] = 0;
	} else if (right && !left) {
		mPaddleValue[0] = 255;
	} else {
		mPaddleValue[0] = 128;
	}

	if (up && !down) {
		mPaddleValue[1] = 0;
	} else if (down && !up) {
		mPaddleValue[1] = 255;
	} else {
		mPaddleValue[1] = 128;
	}

	mButton[0] = button0;
	mButton[1] = button1;
}

/*************************************************************************************************/
CJoystick::SState CJoystick::saveState() const {
	SState s{};
	for (int i = 0; i < 4; i++) {
		s.paddleValue[i] = mPaddleValue[i];
		s.triggerCycle[i] = mTriggerCycle[i];
	}
	for (int i = 0; i < 3; i++) {
		s.button[i] = mButton[i];
	}
	return s;
}

/*************************************************************************************************/
void CJoystick::loadState(const SState& s) {
	for (int i = 0; i < 4; i++) {
		mPaddleValue[i] = s.paddleValue[i];
		mTriggerCycle[i] = s.triggerCycle[i];
	}
	for (int i = 0; i < 3; i++) {
		mButton[i] = s.button[i];
	}
}
