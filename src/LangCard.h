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
// --- Language Card ---
// Standard Apple II/TK2000 16KB memory expansion: two 4KB RAM banks that
// shadow $D000-$DFFF (only one visible at a time) plus 8KB of RAM that
// shadows $E000-$FFFF, bringing total RAM to the 48KB motherboard (CRam,
// $0000-$BFFF) + 16KB here = 64KB some software (e.g. Karateka) insists on
// seeing before it will run ("NAO HA 64K DE MEMORIA").
//
// Bank/read state is controlled by soft switches at $C080-$C08F (16
// mirrored addresses), decoded from the low nibble of the address the
// same way a real Apple II Language Card decodes it:
//   bit3 (0x08) clear -> bank 2 group ($C080-$C087), set -> bank 1 group ($C088-$C08F)
//   low 2 bits of the group: 0 = read RAM
//                              1 = read ROM
//                              2 = read ROM
//                              3 = read RAM
// Unlike the real Apple II card, this board has no separate write-enable
// flip-flop (no "read the same odd offset twice in a row" unlock dance):
// writing to $D000-$FFFF simply lands in RAM whenever RAM is currently
// selected for reading, and vanishes otherwise. Confirmed by tracing the
// real Karateka tape's own memory-size check cycle-by-cycle: it only ever
// switches between $C080 and $C088 (the *even*, read-RAM offsets) and
// never touches $C081/$C083/$C089/$C08B (the odd, write-unlock offsets a
// real Apple II Language Card would require), yet still expects a plain
// `STA $D000` to stick immediately.

#pragma once

#include "Common.h"
#include "Device.h"
#include "Bus.h"
#include "Rom.h"

class CLangCard final : public CDevice {
public:
	CLangCard(CBus& bus, CRom& rom);
	byte read(const word addr, const uint64_t cycles) override;
	void write(const word addr, const byte data, const uint64_t cycles) override;
	void reset() override;
	void update(const uint64_t cycles) override {}

	// Hard-reset only: real RAM contents are undefined/garbage at power-on,
	// same treatment CRam::init() gives the motherboard RAM.
	void clearRam();

#pragma pack(push, 1)
	struct SState {
		bool readRAM;
		bool writeRAM;
		bool bank2;
		bool prewrite;
		byte bank1[0x1000];
		byte bank2Ram[0x1000];
		byte upper[0x2000];
	};
#pragma pack(pop)
	SState saveState() const;
	void loadState(const SState& s);

private:
	void handleSwitch(const word addr, const bool isWrite);
	byte* ramPtr(const word addr);
	void mirrorRom();

	CRom& mRom;
	bool mReadRAM{ false };
	bool mWriteRAM{ false };
	bool mBank2{ true };
	bool mPrewrite{ false };
	byte mBank1[0x1000]{};
	byte mBank2Ram[0x1000]{};
	byte mUpper[0x2000]{};
};
