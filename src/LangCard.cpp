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
#include "LangCard.h"
#include "CoreLog.h"

/*************************************************************************************************/
CLangCard::CLangCard(CBus& bus, CRom& rom) : mRom(rom) {
	bus.addDevice(EDevices::LANGCARD, this);
	bus.registerAddr(EDevices::LANGCARD, 0xC080, 0xC08F);
	// Takes over $D000-$FFFF from CRom: CRom must only register $C100-$CFFF
	// now (see Rom.cpp) so this is the sole device for the banked range.
	bus.registerAddr(EDevices::LANGCARD, 0xD000, 0xFFFF);
	// Seed all three RAM banks with a mirror of the ROM they shadow. The
	// ROM's own cold-boot code briefly banks RAM in over itself (e.g. a
	// soft-switch access at $EC7B while control flow is still running out
	// of $D000-$FFFF), and on real hardware the very next opcode fetch
	// already comes from whatever is now banked in. That only stays
	// harmless if the banked-in RAM happens to still read back as the
	// same code -- so, same as the real card's behavior of coming up
	// mirroring ROM until something actually writes to it, mirror the ROM
	// here instead of leaving these banks zeroed (zeroed RAM turns that
	// incidental fetch into a stray $00/BRK and hangs the CPU before
	// anything -- tape, game, monitor -- ever gets a chance to run).
	mirrorRom();
}

/*************************************************************************************************/
void CLangCard::mirrorRom() {
	for (int i = 0; i < 0x1000; i++) {
		mBank1[i] = mRom.read(0xD000 + i, 0);
		mBank2Ram[i] = mRom.read(0xD000 + i, 0);
	}
	for (int i = 0; i < 0x2000; i++) {
		mUpper[i] = mRom.read(0xE000 + i, 0);
	}
}

/*************************************************************************************************/
byte* CLangCard::ramPtr(const word addr) {
	if (addr >= 0xE000) {
		return &mUpper[addr - 0xE000];
	}
	// 0xD000-0xDFFF: whichever of the two 4KB banks is currently selected
	// Real TK2000: a single RAM image under $D000-$DFFF (no bank switching).
	return &mBank2Ram[addr - 0xD000];
}

/*************************************************************************************************/
void CLangCard::handleSwitch(const word addr, const bool isWrite) {
	const int off = addr & 0x0F;
	mBank2 = (off & 0x08) == 0;
	const int low2 = off & 0x03;
	mReadRAM = (low2 == 0 || low2 == 3);
	// TK2000's expansion board does NOT replicate the Apple II Language
	// Card's write-enable flip-flop (the "read the same odd offset twice
	// in a row" dance that real Apple II software, and the comment this
	// replaces, assumed applied here too). Traced cycle-by-cycle against
	// the real Karateka tape: its own memory-size check only ever
	// switches between $C080 and $C088 -- the two *even* offsets that
	// just pick "read RAM" -- and never once touches $C081/$C083/$C089/
	// $C08B (the odd, write-unlock offsets), yet still expects a
	// straight `STA $D000` to stick. On this board, selecting "read RAM"
	// already implies "write RAM"; there's no separate unlock step, so
	// mWriteRAM simply tracks mReadRAM. (mPrewrite is kept only for
	// savestate binary compatibility; it's unused now.)
	if ((off & 1) == 0) {
		// even offset: this board's behaviour (write follows read)
		mWriteRAM = mReadRAM;
		mPrewrite = false;
	} else if (isWrite) {
		// write to an odd offset resets the pre-write latch
		mPrewrite = false;
	} else {
		// odd offset read: 2nd consecutive read enables RAM writes
		// (real Apple II protocol); never disables an earlier enable
		if (mPrewrite) {
			mWriteRAM = true;
		}
		mPrewrite = true;
	}

	// TEMP INSTRUMENTATION (2026-09-26): log only when the resulting state
	// actually changes -- handleSwitch() gets called on every soft-switch
	// access, including re-reads of the same offset, which would otherwise
	// flood the log during a busy-polling loop.
	static bool first = true;
	static word lastAddr = 0;
	static bool lastReadRAM = false, lastWriteRAM = false, lastBank2 = false;
	if (first || addr != lastAddr || mReadRAM != lastReadRAM || mWriteRAM != lastWriteRAM || mBank2 != lastBank2) {
		tk2000_log("langcard switch addr=$%04X readRAM=%d writeRAM=%d bank2=%d",
			addr, mReadRAM ? 1 : 0, mWriteRAM ? 1 : 0, mBank2 ? 1 : 0);
		first = false;
		lastAddr = addr;
		lastReadRAM = mReadRAM;
		lastWriteRAM = mWriteRAM;
		lastBank2 = mBank2;
	}
}

/*************************************************************************************************/
byte CLangCard::read(const word addr, const uint64_t cycles) {
	if (addr >= 0xC080 && addr <= 0xC08F) {
		// The real TK2000 has no language card: nothing happens here.
		return 0xFF;
	}
	// 0xD000-0xFFFF: RAM only while AN1 is on ($C05B), ROM otherwise.
	if (mRom.isAn1()) {
		return *ramPtr(addr);
	}
	return mRom.read(addr, cycles);
}

/*************************************************************************************************/
void CLangCard::write(const word addr, const byte data, const uint64_t cycles) {
	if (addr >= 0xC080 && addr <= 0xC08F) {
		return;	// no language card on the real TK2000
	}
	// 0xD000-0xFFFF: writes only land in RAM when write-enabled (which,
	// on this hardware, just means "RAM is currently selected for
	// reading" -- see handleSwitch()); the ROM image itself is never
	// modified (real hardware: writes just vanish when ROM is selected).
	if (mRom.isAn1()) {
		*ramPtr(addr) = data;
	}
}

/*************************************************************************************************/
void CLangCard::reset() {
	// Power-on/reset default, matching real hardware: ROM readable,
	// RAM write-protected, bank 2 selected. RAM contents themselves are
	// left untouched (soft reset does not clear RAM on real hardware).
	mReadRAM = false;
	mWriteRAM = false;
	mBank2 = true;
	mPrewrite = false;
}

/*************************************************************************************************/
void CLangCard::clearRam() {
	// See the constructor: mirror ROM rather than zero-filling, so a stray
	// fetch through freshly-banked-in RAM during the ROM's own cold-boot
	// sequence still finds the same bytes ROM would have given it.
	mirrorRom();
}

/*************************************************************************************************/
CLangCard::SState CLangCard::saveState() const {
	SState s{};
	s.readRAM = mReadRAM;
	s.writeRAM = mWriteRAM;
	s.bank2 = mBank2;
	s.prewrite = mPrewrite;
	memcpy(s.bank1, mBank1, sizeof(mBank1));
	memcpy(s.bank2Ram, mBank2Ram, sizeof(mBank2Ram));
	memcpy(s.upper, mUpper, sizeof(mUpper));
	return s;
}

/*************************************************************************************************/
void CLangCard::loadState(const SState& s) {
	mReadRAM = s.readRAM;
	mWriteRAM = s.writeRAM;
	mBank2 = s.bank2;
	mPrewrite = s.prewrite;
	memcpy(mBank1, s.bank1, sizeof(mBank1));
	memcpy(mBank2Ram, s.bank2Ram, sizeof(mBank2Ram));
	memcpy(mUpper, s.upper, sizeof(mUpper));
}
