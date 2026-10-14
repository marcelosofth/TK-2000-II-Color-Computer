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
#include "Bus.h"
#include <cstdio>
#include <cstdarg>

/* Typedefs */

// --- Temporary debug hook: shape-pointer trace (karateka-remaster) -------
// Self-contained on purpose (own file, own FILE*) rather than reusing
// libretro.cpp's tk2000_log, which has internal (anonymous-namespace)
// linkage and isn't visible from this translation unit. `inline` here
// means the function (and its function-local static FILE*) is merged
// across every .cpp that includes this header, so all callers share the
// same open log file instead of each getting their own. Writes
// karateka_shapes.log next to the core, flushing every line so the file
// is readable even if the process is killed instead of closed cleanly.
inline void tk2000_shapelog(const char* fmt, ...) {
	static FILE* f = fopen("karateka_shapes.log", "a");
	if (!f) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fputc('\n', f);
	fflush(f);
}

union UReg16bit {
	struct {
		byte low;
		byte hi;
	};
	word value;
};

/*************************************************************************************************/
class CCpu6502 final : public CDevice {
public:
	CCpu6502(CBus& bus);
	byte read(const word addr, const uint64_t cycles) override {
		return 0xFF;
	};
	void write(const word addr, const byte data, const uint64_t cycles) override {};
	void update(const uint64_t cycles) override {};
	void reset() override;
public:
	uint64_t getCumulativeCycles() const;
	void setClock(const unsigned long clock);
	unsigned long getClock() const;
	void setFullSpeed(const bool val);
	bool getFullSpeed() const;
	double getClockRate() const;
	void executeOpcode();
	// Used once by CMachine::bootFromDisk() right after a hard reset, to
	// jump straight to $0801 the same way a real Disk ][ boot ROM would
	// hand off to whatever it just loaded into RAM -- see Machine.cpp.
	void setPC(const word pc);
	// Diagnostico (apply_tape_end_dump.py): leitura de memoria sem
	// efeitos colaterais de timing, para dump fora do loop de execucao.
	byte dbgPeek(const word addr) const { return mBus.readByte(addr, mCumulativeCycles); }
	// Same one-off boot handoff as setPC(), for A/X/Y: a real Disk ][
	// boot0 ROM leaves A=X=slot*16, Y=$00 on entry to $0801 (DOS 3.3's
	// boot1, baked into every disk, reads X/A back into zero page $2B
	// itself to know which slot it booted from for later RWTS calls).
	// Skipping this leaves $2B at whatever CRam::reset() left it at,
	// which is what was sending boot1 off into the weeds. See
	// CMachine::bootFromDisk() in Machine.cpp.
	void setBootRegs(const byte a, const byte x, const byte y);
	// Savestate support (libretro port)
	struct SState {
		byte a, x, y, s, flags;
		word pc;
		unsigned long clock;
		bool fullSpeed;
		uint64_t cumulativeCycles;
		int interruptFlags;
	};
	SState saveState() const;
	void loadState(const SState& s);
	void dbgArm();	// diagnostico (apply_diag_c0write): chamado por CTape::play()
private:
	CBus& mBus;
	unsigned long mClock{ CPU_CLOCK };
	bool mFullSpeed{ false };
	uint64_t mCumulativeCycles{ 0 };
	int interruptFlags{ 0 };
	// Diagnostic: periodic "where is the CPU right now" heartbeat, so a
	// silent stall (no illegal opcode, no BRK -- just spinning in a
	// legitimate wait loop) still leaves a trail in the log. Logged at
	// most once per ~1,000,000 cycles; harmless to leave in permanently.
	uint64_t mLastHeartbeatCycle{ 0 };
	// Diagnostico (apply_diag_c0write.py)
	mutable bool mDbgArmed{ false };
	mutable bool mDbgTripped{ false };
	mutable bool mDbgBrkDumped{ false };
	mutable word mDbgPC{ 0 };
	mutable word mDbgRing[64]{};
	mutable unsigned mDbgRingHead{ 0 };
	mutable word mDbgStackWriter[256]{};
	mutable unsigned mDbgCalls{ 0 };
	mutable byte mDbgSeen[65536 / 8]{};
	void dbgStep() const;
	void dbgOnWrite(word address, byte value) const;
	void dbgDumpCommon(const char* why) const;
	void dbgDumpStack() const;
private:
	inline byte readByte(word address) const {
		return mBus.readByte(address, mCumulativeCycles);
	}

	inline word readWord(word address) const {
		return mBus.readWord(address, mCumulativeCycles);
	}

	inline void writeByte(word address, byte value) const {
		if ((address & 0xFF00) == 0x0100) {
			mDbgStackWriter[address & 0xFF] = mDbgPC;
		} else if (mDbgArmed && !mDbgTripped && address >= 0xC001 && address <= 0xC00F) {
			dbgOnWrite(address, value);
		}
		// --- Temporary debug hook: shape-pointer trace (karateka-remaster) ---
		// Every draw call sets up zero page $03/$04 (shape data pointer) just
		// before jumping into the blit dispatch table, always writing $03
		// (low byte) first and $04 (high byte) second -- confirmed by static
		// disassembly of every call site found so far. So catching the
		// write to $04 here, after $03 is already in RAM, gives the
		// complete pointer with no risk of catching it half-written.
		// Logs cycle count (for ordering/correlating with on-screen
		// action), the approximate PC of the STA $04 that triggered this
		// (STA zp is a 2-byte instruction, so the instruction itself
		// started 2 bytes before the current PC), the resulting pointer,
		// and the other zero-page fields the draw routines are known to
		// read ($05 row, $06/$07 page base, $0E column, $0F flip, $10
		// sub-pixel offset). Safe to leave in: read-only peeks plus one
		// buffered log line, no behavior change.
		if (address == 0x04) {
			dbgLogShapePointer(value);
		}
		mBus.writeByte(address, value, mCumulativeCycles);
	}

	void dbgLogShapePointer(byte hiByte) const {
		byte lo = dbgPeek(0x03);
		word ptr = (word)((hiByte << 8) | lo);
		byte r05 = dbgPeek(0x05), r06 = dbgPeek(0x06), r07 = dbgPeek(0x07);
		byte r0E = dbgPeek(0x0E), r0F = dbgPeek(0x0F), r10 = dbgPeek(0x10);
		word approxPC = (word)(mRegPC.value - 2);
		tk2000_shapelog("cyc=%llu pc=$%04X ptr=$%04X row=$%02X base=$%02X,$%02X col=$%02X flip=$%02X sub=$%02X",
			(unsigned long long)mCumulativeCycles, approxPC, ptr, r05, r06, r07, r0E, r0F, r10);
	}

	inline void writeWord(word address, word value) const {
		mBus.writeWord(address, value, mCumulativeCycles);
	}
private:
	// Flags Interrupts
	const byte INT_NMI{ (1 << 0) };
	const byte INT_IRQ{ (1 << 1) };

	// ALU look up tables
	word m_BCDTableAdd[512];			// addition
	word m_BCDTableSub[512];			// subtraction

	// Flags bitmask
	const byte FLAG_C{ (1 << 0) };
	const byte FLAG_Z{ (1 << 1) };
	const byte FLAG_I{ (1 << 2) };
	const byte FLAG_D{ (1 << 3) };
	const byte FLAG_B{ (1 << 4) };
	const byte FLAG_V{ (1 << 6) };
	const byte FLAG_N{ (1 << 7) };

	// Registers
	byte mRegA{ 0 };
	byte mRegX{ 0 };
	byte mRegY{ 0 };
	byte mRegS{ 0xFF };
	byte mRegFlags{ 0x20 };	// On an 6502, bit 5 always must be 1
	UReg16bit mRegPC{ 0 };

	// Stack macros

	inline byte stackPop() {
		return readByte(0x100 | ++mRegS);
	}

	inline void stackPush(byte v) {
		writeByte(0x100 | mRegS--, v);
	}

	// Flags utils

	inline void setN(bool b) {
		if (b) {
			mRegFlags |= FLAG_N;
		} else {
			mRegFlags &= ~FLAG_N;
		}
	}

	inline void setV(bool b) {
		if (b) {
			mRegFlags |= FLAG_V;
		} else {
			mRegFlags &= ~FLAG_V;
		}
	}

	inline void setB(bool b) {
		if (b) {
			mRegFlags |= FLAG_B;
		} else {
			mRegFlags &= ~FLAG_B;
		}
	}

	inline void setD(bool b) {
		if (b) {
			mRegFlags |= FLAG_D;
		} else {
			mRegFlags &= ~FLAG_D;
		}
	}

	inline void setI(bool b) {
		if (b) {
			mRegFlags |= FLAG_I;
		} else {
			mRegFlags &= ~FLAG_I;
		}
	}

	inline void setZ(bool b) {
		if (b) {
			mRegFlags |= FLAG_Z;
		} else {
			mRegFlags &= ~FLAG_Z;
		}
	}

	inline void setC(bool b) {
		if (b) {
			mRegFlags |= FLAG_C;
		} else {
			mRegFlags &= ~FLAG_C;
		}
	}

	inline void setNZ(byte val) {
		setN((val & 0x80) != 0);
		setZ(val == 0);
	}

	inline bool getN() const noexcept {
		return ((mRegFlags & FLAG_N) != 0);
	}

	inline bool getV() const noexcept {
		return ((mRegFlags & FLAG_V) != 0);
	}

	inline bool getD() const noexcept {
		return ((mRegFlags & FLAG_D) != 0);
	}

	inline bool getI() const noexcept {
		return ((mRegFlags & FLAG_I) != 0);
	}

	inline bool getZ() const noexcept {
		return ((mRegFlags & FLAG_Z) != 0);
	}

	inline bool getC() const noexcept {
		return ((mRegFlags & FLAG_C) != 0);
	}

	// Inline for effective address calculation

	inline byte eaimm() {
		return readByte(mRegPC.value++);
	}

	inline byte eazp() {
		return readByte(mRegPC.value++);
	}

	inline byte eazpx() {
		return readByte(mRegPC.value++) + mRegX;
	}

	inline byte eazpy() {
		return readByte(mRegPC.value++) + mRegY;
	}

	inline word eaabs() {
		word result = readWord(mRegPC.value);
		mRegPC.value += 2;
		return result;
	}

	inline char earel() {
		return (char)readByte(mRegPC.value++);
	}

	inline word eaabsx() {
		return eaabs() + mRegX;
	}

	inline word eaabsy() {
		return eaabs() + mRegY;
	}

	// Indirect addressing

	inline word eaabsind() {
		return readWord(eaabs());
	}

	inline word eazpxind() {
		return readWord(eazpx());
	}

	inline word eazpindy() {
		return readWord(eaimm()) + mRegY;
	}

	// Misc. macros
	inline void accCycles(int cycles) noexcept {
		mCumulativeCycles += cycles;
	}
	inline void branch(signed char operand) noexcept {
		mRegPC.value += (operand);
		accCycles(1);
	}
};

