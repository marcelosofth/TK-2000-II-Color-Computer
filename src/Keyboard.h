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
// --- libretro port note ---
// The original keyEvent() took an SDL_KeyboardEvent&. That dependency is
// gone: this version takes a libretro-style (down, keycode, key_modifiers)
// triple straight from retro_keyboard_callback, using the RETROK_* codes
// defined in libretro.h (which mirror the classic SDL1.2 SDLK_* values the
// original switch statement was written against, symbol-for-symbol).

#pragma once

#include "Device.h"
#include "Bus.h"

class CKeyboard final : public CDevice {
public:
	CKeyboard(CBus& bus);
	// CDevice
	byte read(const word addr, const uint64_t cycles) override;
	void write(const word addr, const byte data, const uint64_t cycles) override;
	void update(const uint64_t cycles) override {};
	void reset() override;
	// libretro: called from the retro_keyboard_callback. The libretro
	// keyboard callback is event-based (no auto-repeat is delivered by
	// RetroArch), so unlike the original SDL version there is no
	// e.repeat flag to filter.
	void keyEvent(bool down, unsigned keycode, uint16_t mod);
	// libretro: real TK2000 hardware wires its two FIRE keys in parallel
	// to the joystick fire buttons -- pressing either one both asserts
	// PB0/PB1 (see CJoystick::padEvent) AND types "." on the keyboard
	// matrix (official manual: "as teclas FIRE emitem o caracter '.'").
	// Some games (e.g. Bombardeiro.ct2) poll the keyboard for this
	// instead of reading $C061/$C062 directly, so CMachine::padEvent()
	// drives this alongside CJoystick::padEvent() to match real hardware.
	// Sets/clears the matrix bit directly (same position as RETROK_PERIOD)
	// without touching mShift/mCtrl, so it can't stomp a real keypress.
	void fireKeyEvent(bool down);
	// libretro: same idea as fireKeyEvent(), for the D-pad. The TK2000's
	// dedicated LFT/RGT/DWN/UP cursor keys (matrix rows 3-6, bit 0 --
	// same cells RETROK_LEFT/RIGHT/UP/DOWN hit above) are what games like
	// Bombardeiro.ct2 actually poll for movement, not the joystick
	// paddles ($C064-$C067). CMachine::padEvent() drives this alongside
	// CJoystick::padEvent() so the RetroPad D-pad works for both kinds
	// of game.
	void dirKeyEvent(bool left, bool right, bool up, bool down);
	// libretro: RetroPad button -> keyboard key (used by the per-game
	// Karateka mapping in libretro.cpp). Same matrix cell as a real key
	// (keyEvent()), but keeps the current Shift state instead of
	// overwriting it with "no modifiers".
	void padKeyEvent(bool down, unsigned keycode);
	// Savestate support
	struct SState {
		byte matrix[8];
		bool ctrl;
		bool shift;
		byte kbOut;
		bool kbOutCtrl;
	};
	SState saveState() const;
	void loadState(const SState& s);
private:
	byte mMatrix[8]{ 0 };
	bool mCtrl{ false };
	bool mShift{ false };
	byte mKbOut{ 0 };
	bool mKbOutCtrl{ 0 };
};
