// Copyright (c) 2024 FBLabs
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSe->  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "pch.h"
#include "Keyboard.h"
#include "libretro.h"
#include "CoreLog.h"

/*
 *   TK2000 Keyboard Matrix
 *   ======================
 *
 *                         KBIN
 *             0   1   2   3   4   5
 *         0       B   V   C   X   Z
 *     K   1  SHFT G   F   D   S   A
 *     B   2  " "  T   R   E   W   Q
 *     O   3  LFT  5   4   3   2   1
 *     U   4  RGT  6   7   8   9   0
 *     T   5  DWN  Y   U   I   O   P
 *         6  UP   H   J   K   L   :
 *         7  RTN  N   M   ,   .   ?
*/


/*************************************************************************************************/
CKeyboard::CKeyboard(CBus& bus) {
	bus.addDevice(EDevices::KEYBOARD, this);
	bus.registerAddr(EDevices::KEYBOARD, 0xC000, 0xC01F);
	bus.registerAddr(EDevices::KEYBOARD, 0xC05E, 0xC05F);
}

/*************************************************************************************************/
byte CKeyboard::read(const word addr, const uint64_t cycles) {
	byte result;
	if (mKbOut == 1 && mShift) {
		result = 1;
	} else {
		result = ((mKbOutCtrl && mCtrl) ? 1 : 0);
		for (int l = 0; l < 8; l++) {
			if (mKbOut & (1 << l)) {
				result |= mMatrix[l];
			}
		}
	}
	// TEMP INSTRUMENTATION (2026-09-26): log only on change, so a scan loop
	// that reads this every few cycles doesn't flood the log -- but a stuck
	// "waiting for keypress" loop that never sees mMatrix flip still shows
	// up as a single line, and any change (ours or a stray bus glitch) is
	// captured. Remove once the Karateka keyboard-wait hang is understood.
	static byte lastAddr = 0xFF;
	static byte lastResult = 0xFF;
	static byte lastKbOut = 0xFF;
	if (result != lastResult || mKbOut != lastKbOut || (addr & 0xFF) != lastAddr) {
		tk2000_log("kbd read addr=$%04X kbOut=$%02X matrix=%02X %02X %02X %02X %02X %02X %02X %02X -> $%02X cycles=%llu",
			addr, mKbOut,
			mMatrix[0], mMatrix[1], mMatrix[2], mMatrix[3], mMatrix[4], mMatrix[5], mMatrix[6], mMatrix[7],
			result, (unsigned long long)cycles);
		lastAddr = (byte)addr;
		lastResult = result;
		lastKbOut = mKbOut;
	}
	return result;
}

/*************************************************************************************************/
void CKeyboard::write(const word addr, byte data, const uint64_t cycles) {
	// TEMP INSTRUMENTATION (2026-09-26): see read() above -- log column-select
	// writes only when the value actually changes.
	static byte lastWritten = 0xFF;
	static bool first = true;
	if (first || data != lastWritten) {
		tk2000_log("kbd write addr=$%04X data=$%02X cycles=%llu", addr, data, (unsigned long long)cycles);
		lastWritten = data;
		first = false;
	}
	if (addr == 0xC05E) {
		mKbOutCtrl = false;
		return;
	} else if (addr == 0xC05F) {
		mKbOutCtrl = true;
		return;
	}
	if ((addr & 0xF0) == 0x00) {
		mKbOut = data;
	}
}

/*************************************************************************************************/
void CKeyboard::reset() {
	for (int i = 0; i < 8; i++) {
		mMatrix[i] = 0;
	}
	mCtrl = false;
	mShift = false;
}

/*************************************************************************************************/
// Receiving keyboard notification from the libretro keyboard callback
void CKeyboard::keyEvent(bool down, unsigned keycode, uint16_t mod) {
	// TEMP INSTRUMENTATION (2026-09-26): every call here is a real
	// physical/virtual keypress reaching the keyboard matrix (rare enough
	// to log unconditionally) -- confirms input is arriving at all,
	// independent of whether the ROM's polling loop ever notices it.
	tk2000_log("keyEvent down=%d keycode=%u mod=$%04X", down ? 1 : 0, keycode, mod);

	mShift = (mod & RETROKMOD_SHIFT) != 0;

	switch (keycode){

	case RETROK_LCTRL:
	case RETROK_RCTRL:
		mCtrl = down;
		break;

	case RETROK_a:
		if (down) {
			mMatrix[1] |= 1 << 5;
		} else {
			mMatrix[1] &= ~(1 << 5);
		}
		return;

	case RETROK_b:
		if (down) {
			mMatrix[0] |= 1 << 1;
		} else {
			mMatrix[0] &= ~(1 << 1);
		}
		return;

	case RETROK_c:
		if (down) {
			mMatrix[0] |= 1 << 3;
		} else {
			mMatrix[0] &= ~(1 << 3);
		}
		return;

	case RETROK_d:
		if (down) {
			mMatrix[1] |= 1 << 3;
		} else {
			mMatrix[1] &= ~(1 << 3);
		}
		return;

	case RETROK_e:
		if (down) {
			mMatrix[2] |= 1 << 3;
		} else {
			mMatrix[2] &= ~(1 << 3);
		}
		return;

	case RETROK_f:
		if (down) {
			mMatrix[1] |= 1 << 2;
		} else {
			mMatrix[1] &= ~(1 << 2);
		}
		return;

	case RETROK_g:
		if (down) {
			mMatrix[1] |= 1 << 1;
		} else {
			mMatrix[1] &= ~(1 << 1);
		}
		return;

	case RETROK_h:
		if (down) {
			mMatrix[6] |= 1 << 1;
		} else {
			mMatrix[6] &= ~(1 << 1);
		}
		return;

	case RETROK_i:
		if (down) {
			mMatrix[5] |= 1 << 3;
		} else {
			mMatrix[5] &= ~(1 << 3);
		}
		return;

	case RETROK_j:
		if (down) {
			mMatrix[6] |= 1 << 2;
		} else {
			mMatrix[6] &= ~(1 << 2);
		}
		return;

	case RETROK_k:
		if (down) {
			mMatrix[6] |= 1 << 3;
		} else {
			mMatrix[6] &= ~(1 << 3);
		}
		return;

	case RETROK_l:
		if (down) {
			mMatrix[6] |= 1 << 4;
		} else {
			mMatrix[6] &= ~(1 << 4);
		}
		return;

	case RETROK_m:
		if (down) {
			mMatrix[7] |= 1 << 2;
		} else {
			mMatrix[7] &= ~(1 << 2);
		}
		return;

	case RETROK_n:
		if (down) {
			mMatrix[7] |= 1 << 1;
		} else {
			mMatrix[7] &= ~(1 << 1);
		}
		return;

	case RETROK_o:
		if (down) {
			mMatrix[5] |= 1 << 4;
		} else {
			mMatrix[5] &= ~(1 << 4);
		}
		return;

	case RETROK_p:
		if (down) {
			mMatrix[5] |= 1 << 5;
		} else {
			mMatrix[5] &= ~(1 << 5);
		}
		return;

	case RETROK_q:
		if (down) {
			mMatrix[2] |= 1 << 5;
		} else {
			mMatrix[2] &= ~(1 << 5);
		}
		return;

	case RETROK_r:
		if (down) {
			mMatrix[2] |= 1 << 2;
		} else {
			mMatrix[2] &= ~(1 << 2);
		}
		return;

	case RETROK_s:
		if (down) {
			mMatrix[1] |= 1 << 4;
		} else {
			mMatrix[1] &= ~(1 << 4);
		}
		return;

	case RETROK_t:
		if (down) {
			mMatrix[2] |= 1 << 1;
		} else {
			mMatrix[2] &= ~(1 << 1);
		}
		return;

	case RETROK_u:
		if (down) {
			mMatrix[5] |= 1 << 2;
		} else {
			mMatrix[5] &= ~(1 << 2);
		}
		return;

	case RETROK_v:
		if (down) {
			mMatrix[0] |= 1 << 2;
		} else {
			mMatrix[0] &= ~(1 << 2);
		}
		return;

	case RETROK_w:
		if (down) {
			mMatrix[2] |= 1 << 4;
		} else {
			mMatrix[2] &= ~(1 << 4);
		}
		return;

	case RETROK_x:
		if (down) {
			mMatrix[0] |= 1 << 4;
		} else {
			mMatrix[0] &= ~(1 << 4);
		}
		return;

	case RETROK_y:
		if (down) {
			mMatrix[5] |= 1 << 1;
		} else {
			mMatrix[5] &= ~(1 << 1);
		}
		return;

	case RETROK_z:
		if (down) {
			mMatrix[0] |= 1 << 5;
		} else {
			mMatrix[0] &= ~(1 << 5);
		}
		return;

	case RETROK_1:
	case RETROK_KP1:
		if (down) {
			mMatrix[3] |= 1 << 5;
		} else {
			mMatrix[3] &= ~(1 << 5);
		}
		return;

	case RETROK_2:
	case RETROK_KP2:
		if (down) {
			mMatrix[3] |= 1 << 4;
		} else {
			mMatrix[3] &= ~(1 << 4);
		}
		return;

	case RETROK_3:
	case RETROK_KP3:
		if (down) {
			mMatrix[3] |= 1 << 3;
		} else {
			mMatrix[3] &= ~(1 << 3);
		}
		return;

	case RETROK_4:
	case RETROK_KP4:
		if (down) {
			mMatrix[3] |= 1 << 2;
		} else {
			mMatrix[3] &= ~(1 << 2);
		}
		return;

	case RETROK_5:
	case RETROK_KP5:
		if (down) {
			mMatrix[3] |= 1 << 1;
		} else {
			mMatrix[3] &= ~(1 << 1);
		}
		return;

	case RETROK_6:
	case RETROK_KP6:
		if (down) {
			mMatrix[4] |= 1 << 1;
		} else {
			mMatrix[4] &= ~(1 << 1);
		}
		return;

	case RETROK_7:
	case RETROK_KP7:
		if (down) {
			mMatrix[4] |= 1 << 2;
		} else {
			mMatrix[4] &= ~(1 << 2);
		}
		return;

	case RETROK_8:
	case RETROK_KP8:
		if (down) {
			mMatrix[4] |= 1 << 3;
		} else {
			mMatrix[4] &= ~(1 << 3);
		}
		return;

	case RETROK_9:
	case RETROK_KP9:
		if (down) {
			mMatrix[4] |= 1 << 4;
		} else {
			mMatrix[4] &= ~(1 << 4);
		}
		return;

	case RETROK_0:
	case RETROK_KP0:
		if (down) {
			mMatrix[4] |= 1 << 5;
		} else {
			mMatrix[4] &= ~(1 << 5);
		}
		return;

	case RETROK_COMMA:
		if (down) {
			mMatrix[7] |= 1 << 3;
		} else {
			mMatrix[7] &= ~(1 << 3);
		}
		return;

	case RETROK_PERIOD:
	case RETROK_KP_PERIOD:
		if (down) {
			mMatrix[7] |= 1 << 4;
		} else {
			mMatrix[7] &= ~(1 << 4);
		}
		return;

	case RETROK_COLON:
		if (down) {
			mMatrix[6] |= 1 << 5;
		} else {
			mMatrix[6] &= ~(1 << 5);
		}
		return;

	case RETROK_QUESTION:
		if (down) {
			mMatrix[7] |= 1 << 5;
		} else {
			mMatrix[7] &= ~(1 << 5);
		}
		return;

	case RETROK_EXCLAIM:
		if (down) {
			mMatrix[3] |= 1 << 5;
		} else {
			mMatrix[3] &= ~(1 << 5);
		}
		mShift = true;
		return;

	case RETROK_SEMICOLON:
		if (down) {
			mMatrix[6] |= 1 << 5;
		} else {
			mMatrix[6] &= ~(1 << 5);
		}
		mShift = !mShift;
		break;

	case RETROK_QUOTE:
		if (down) {
			mMatrix[4] |= 1 << 2;
		} else {
			mMatrix[4] &= ~(1 << 2);
		}
		mShift = true;
		return;

	case RETROK_QUOTEDBL:
		if (down) {
			mMatrix[3] |= 1 << 4;
		} else {
			mMatrix[3] &= ~(1 << 4);
		}
		mShift = true;
		return;

	case RETROK_HASH:
		if (down) {
			mMatrix[3] |= 1 << 3;
		} else {
			mMatrix[3] &= ~(1 << 3);
		}
		mShift = true;
		return;

	case RETROK_DOLLAR:
		if (down) {
			mMatrix[3] |= 1 << 2;
		} else {
			mMatrix[3] &= ~(1 << 2);
		}
		mShift = true;
		return;

	case RETROK_PERCENT:
		if (down) {
			mMatrix[3] |= 1 << 1;
		} else {
			mMatrix[3] &= ~(1 << 1);
		}
		mShift = true;
		return;

	case RETROK_AMPERSAND:
		if (down) {
			mMatrix[4] |= 1 << 1;
		} else {
			mMatrix[4] &= ~(1 << 1);
		}
		mShift = true;
		return;

	case RETROK_LEFTPAREN:
		if (down) {
			mMatrix[4] |= 1 << 3;
		} else {
			mMatrix[4] &= ~(1 << 3);
		}
		mShift = true;
		return;

	case RETROK_RIGHTPAREN:
		if (down) {
			mMatrix[4] |= 1 << 4;
		} else {
			mMatrix[4] &= ~(1 << 4);
		}
		mShift = true;
		return;

	case RETROK_SLASH:
	case RETROK_KP_DIVIDE:
		if (down) {
			mMatrix[7] |= 1 << 5;
		} else {
			mMatrix[7] &= ~(1 << 5);
		}
		mShift = !mShift;
		return;

	case RETROK_EQUALS:
		if (down) {
			mMatrix[5] |= 1 << 4;
		} else {
			mMatrix[5] &= ~(1 << 4);
		}
		mShift = true;
		return;

	case RETROK_MINUS:
	case RETROK_KP_MINUS:
		if (down) {
			mMatrix[5] |= 1 << 3;
		} else {
			mMatrix[5] &= ~(1 << 3);
		}
		mShift = true;
		return;

	case RETROK_PLUS:
	case RETROK_KP_PLUS:
		if (down) {
			mMatrix[5] |= 1 << 5;
		} else {
			mMatrix[5] &= ~(1 << 5);
		}
		mShift = true;
		return;

	case RETROK_ASTERISK:
	case RETROK_KP_MULTIPLY:
		if (down) {
			mMatrix[4] |= 1 << 5;
		} else {
			mMatrix[4] &= ~(1 << 5);
		}
		mShift = true;
		return;

	case RETROK_CARET:
		if (down) {
			mMatrix[6] |= 1 << 3;
		} else {
			mMatrix[6] &= ~(1 << 3);
		}
		mShift = true;
		return;

	case RETROK_AT:
		if (down) {
			mMatrix[6] |= 1 << 4;
		} else {
			mMatrix[6] &= ~(1 << 4);
		}
		mShift = true;
		return;

	case RETROK_UP:
		if (down) {
			mMatrix[6] |= 1 << 0;
		} else {
			mMatrix[6] &= ~(1 << 0);
		}
		return;

	case RETROK_DOWN:
		if (down) {
			mMatrix[5] |= 1 << 0;
		} else {
			mMatrix[5] &= ~(1 << 0);
		}
		return;

	case RETROK_LEFT:
	case RETROK_BACKSPACE:
		if (down) {
			mMatrix[3] |= 1 << 0;
		} else {
			mMatrix[3] &= ~(1 << 0);
		}
		return;

	case RETROK_RIGHT:
		if (down) {
			mMatrix[4] |= 1 << 0;
		} else {
			mMatrix[4] &= ~(1 << 0);
		}
		return;

	case RETROK_RETURN:
	case RETROK_KP_ENTER:
		if (down) {
			mMatrix[7] |= 1 << 0;
		} else {
			mMatrix[7] &= ~(1 << 0);
		}
		return;

	case RETROK_SPACE:
		if (down) {
			mMatrix[2] |= 1 << 0;
		} else {
			mMatrix[2] &= ~(1 << 0);
		}
		return;
	}
}

/*************************************************************************************************/
void CKeyboard::fireKeyEvent(bool down) {
	// Row 7, bit 4 -- the "." key position, same matrix cell RETROK_PERIOD
	// hits above. Real FIRE keys share this cell (see comment in Keyboard.h).
	if (down) {
		mMatrix[7] |= 1 << 4;
	} else {
		mMatrix[7] &= ~(1 << 4);
	}
}

/*************************************************************************************************/
void CKeyboard::padKeyEvent(bool down, unsigned keycode) {
	keyEvent(down, keycode, mShift ? RETROKMOD_SHIFT : 0);
}

/*************************************************************************************************/
void CKeyboard::dirKeyEvent(bool left, bool right, bool up, bool down) {
	// Same matrix cells as RETROK_LEFT/RIGHT/UP/DOWN above (row bit 0),
	// set directly so this can't stomp mShift/mCtrl or a real keypress.
	if (left) {
		mMatrix[3] |= 1 << 0;
	} else {
		mMatrix[3] &= ~(1 << 0);
	}
	if (right) {
		mMatrix[4] |= 1 << 0;
	} else {
		mMatrix[4] &= ~(1 << 0);
	}
	if (down) {
		mMatrix[5] |= 1 << 0;
	} else {
		mMatrix[5] &= ~(1 << 0);
	}
	if (up) {
		mMatrix[6] |= 1 << 0;
	} else {
		mMatrix[6] &= ~(1 << 0);
	}
}

/*************************************************************************************************/
CKeyboard::SState CKeyboard::saveState() const {
	SState s{};
	for (int i = 0; i < 8; i++) {
		s.matrix[i] = mMatrix[i];
	}
	s.ctrl = mCtrl;
	s.shift = mShift;
	s.kbOut = mKbOut;
	s.kbOutCtrl = mKbOutCtrl;
	return s;
}

/*************************************************************************************************/
void CKeyboard::loadState(const SState& s) {
	for (int i = 0; i < 8; i++) {
		mMatrix[i] = s.matrix[i];
	}
	mCtrl = s.ctrl;
	mShift = s.shift;
	mKbOut = s.kbOut;
	mKbOutCtrl = s.kbOutCtrl;
}
