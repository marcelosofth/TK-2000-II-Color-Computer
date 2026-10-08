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
#include "Cpu6502.h"
#include "CoreLog.h"
#include "Disk.h"

/* Constants */


/*************************************************************************************************/
CCpu6502::CCpu6502(CBus& bus) :
	mBus(bus)
{
	for (int i = 0; i < 512; i++) {
		m_BCDTableAdd[i] = ((i & 0x0F) <= 0x09) ? i : (i + 0x06);
		m_BCDTableAdd[i] += ((m_BCDTableAdd[i] & 0xf0) <= 0x90) ? 0 : 0x60;
		if (m_BCDTableAdd[i] > 0x1ff) {
			m_BCDTableAdd[i] -= 0x100;
		}
		m_BCDTableSub[i] = ((i & 0x0f) <= 0x09) ? i : (i - 0x06);
		m_BCDTableSub[i] -= ((m_BCDTableSub[i] & 0xf0) <= 0x90) ? 0 : 0x60;
	}

	mBus.addDevice(EDevices::CPU, this);
}

/*************************************************************************************************/
void CCpu6502::reset() {
	interruptFlags = 0;	// clear all interrupt flags
	mRegA = mRegX = mRegY = 0;
	mRegFlags = 0x20;
	mRegS = 0xFF;
	mRegPC.value = readWord(0xFFFC);
	mCumulativeCycles = 0;
}

/*************************************************************************************************/
void CCpu6502::setPC(const word pc) {
	mRegPC.value = pc;
}

/*****************************************************************************/
void CCpu6502::setBootRegs(const byte a, const byte x, const byte y) {
	mRegA = a;
	mRegX = x;
	mRegY = y;
}

/*************************************************************************************************/
uint64_t CCpu6502::getCumulativeCycles() const {
	return mCumulativeCycles;
}

/*************************************************************************************************/
void CCpu6502::setClock(const unsigned long clock) {
	assert(clock > 0);
	mClock = clock;
}

/*************************************************************************************************/
unsigned long CCpu6502::getClock() const {
	return mClock;
}

/*************************************************************************************************/
void CCpu6502::setFullSpeed(const bool val) {
	mFullSpeed = val;
}

/*************************************************************************************************/
bool CCpu6502::getFullSpeed() const {
	return mFullSpeed;
}

/*************************************************************************************************/
double CCpu6502::getClockRate() const {
	if (mFullSpeed) {
		return std::numeric_limits<double>::infinity();
	}
	return mClock / CPU_CLOCK;
}

/*************************************************************************************************/
/*************************************************************************************************/
// DIAGNOSTIC (apply_diag_c0write.py)
void CCpu6502::dbgArm() {
	static int plays = 0;
	if (++plays < 2) {
		return;	// 1a fita (LOADT do boot): nao interessa
	}
	mDbgArmed = true;
	mDbgTripped = false;
	mDbgBrkDumped = false;
	mDbgCalls = 0;
	mDbgRingHead = 0;
	memset(mDbgSeen, 0, sizeof(mDbgSeen));
	tk2000_log("DIAG armed (tape play #%d) cycles=%llu", plays, (unsigned long long)mCumulativeCycles);
}

static void dbgHexLine(const char* tag, word base, const byte* b, int n) {
	char s[16 * 3 + 1] = {};
	for (int i = 0; i < n; i++) {
		snprintf(s + i * 3, 4, "%02X ", b[i]);
	}
	tk2000_log("  %s $%04X: %s", tag, base, s);
}

void CCpu6502::dbgDumpCommon(const char* why) const {
	tk2000_log("DIAG %s: PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X P=$%02X cycles=%llu",
		why, mDbgPC, mRegA, mRegX, mRegY, mRegS, mRegFlags, (unsigned long long)mCumulativeCycles);
	const unsigned n = mDbgRingHead < 64 ? mDbgRingHead : 64;
	for (unsigned i = 0; i < n; i++) {
		const word pc = mDbgRing[(mDbgRingHead - n + i) & 63];
		tk2000_log("  ring[%02u] PC=$%04X op=$%02X", i, pc, readByte(pc));
	}
}

void CCpu6502::dbgDumpStack() const {
	byte b[16];
	for (int row = 0; row < 4; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte(0x0100 + ((mRegS + 1 + row * 16 + i) & 0xFF));
		}
		dbgHexLine("stack from S+1+row*16", (word)(0x0100 + ((mRegS + 1 + row * 16) & 0xFF)), b, 16);
	}
	for (int i = 1; i <= 12; i++) {
		const unsigned slot = (mRegS + i) & 0xFF;
		const word w = mDbgStackWriter[slot];
		tk2000_log("  stack[$01%02X]=$%02X written by PC=$%04X", slot, readByte(0x0100 + slot), w);
	}
	// bytes de codigo em torno de cada um dos primeiros 4 escritores distintos
	word seen[4] = { 0, 0, 0, 0 };
	int ns = 0;
	for (int i = 1; i <= 12 && ns < 4; i++) {
		const word w = mDbgStackWriter[(mRegS + i) & 0xFF];
		bool dup = false;
		for (int k = 0; k < ns; k++) {
			dup = dup || (seen[k] == w);
		}
		if (!dup && w != 0) {
			seen[ns++] = w;
			for (int i2 = 0; i2 < 16; i2++) {
				b[i2] = readByte((word)(w - 4 + i2));
			}
			dbgHexLine("code around writer (PC-4..)", (word)(w - 4), b, 16);
		}
	}
}

void CCpu6502::dbgStep() const {
	const word pc = mDbgPC;
	const byte op = readByte(pc);
	mDbgRing[mDbgRingHead++ & 63] = pc;
	if ((op == 0x20 || op == 0x4C) && mDbgCalls < 500) {
		const word target = (word)(readByte((word)(pc + 1)) | (readByte((word)(pc + 2)) << 8));
		if ((mDbgSeen[target >> 3] & (1 << (target & 7))) == 0) {
			mDbgSeen[target >> 3] |= (byte)(1 << (target & 7));
			++mDbgCalls;
			tk2000_log("DIAG %s $%04X from $%04X S=$%02X cycles=%llu",
				op == 0x20 ? "JSR" : "JMP", target, pc, mRegS, (unsigned long long)mCumulativeCycles);
		}
	}
	if (op == 0x00 && !mDbgBrkDumped) {
		mDbgBrkDumped = true;
		dbgDumpCommon("first BRK opcode");
		dbgDumpStack();
		tk2000_log_flush();
		mDbgArmed = false;
	}
}

void CCpu6502::dbgOnWrite(word address, byte value) const {
	mDbgTripped = true;
	tk2000_log("DIAG first write to $%04X data=$%02X", address, value);
	dbgDumpCommon("first write to $C001-$C00F");
	byte b[16];
	for (int row = 0; row < 4; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte((word)(mDbgPC - 16 + row * 16 + i));
		}
		dbgHexLine("code around writer", (word)(mDbgPC - 16 + row * 16), b, 16);
	}
	for (int row = 0; row < 16; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte((word)(row * 16 + i));
		}
		dbgHexLine("zero page", (word)(row * 16), b, 16);
	}
	dbgDumpStack();
	tk2000_log_flush();
}

/*************************************************************************************************/
void CCpu6502::executeOpcode() {
	byte operand;
	word result, operandAddr;

	// Native "ReadSector" trap. DOS 3.3's boot1 -- loaded verbatim from
	// the disk into $0800 by CMachine::bootFromDisk(), unmodified -- is
	// written to call back into a real Disk ][ boot ROM at $C65C (slot
	// 6) with a bare JMP (not JSR: it never expects to RTS back here,
	// it always continues at $081F itself, see Machine.cpp/Disk.h for
	// the rest of this boot chain). This core carries no such ROM code
	// at $C65C, so instead of executing whatever ROM/RAM bytes happen
	// to be there, catch the jump here and do the equivalent work
	// natively: copy the sector boot1 just picked out of the DOS 3.3
	// interleave table (zero page $3D) straight out of the .dsk image
	// to wherever boot1 itself says to put it ($26/$27, low/high --
	// boot1 computes and maintains this destination pointer on its own,
	// same as it would for a real ReadSector call; on this disk it
	// counts DOWN from page $B9 each call, not up from $09 as might be
	// guessed, since DOS 3.3's resident RWTS lives in high memory), then
	// jump back into boot1's own loop.
	// Only when the jump really comes from boot1 ($0800-$08FF, mDbgPC still holds
	// the PC of the instruction just executed). $C65C is also a legitimate address
	// inside the TK2000 BASIC ROM (GTFORPNT: FOR/NEXT, GOSUB/RETURN); trapping there
	// unconditionally derailed every BASIC program (e.g. Snake.ct2) to $081F.
	if (mRegPC.value == 0xC65C && mDbgPC >= 0x0800 && mDbgPC < 0x0900) {
		CDisk* disk = static_cast<CDisk*>(mBus.getDevice(EDevices::DISK));
		if (disk != nullptr && disk->isLoaded()) {
			// $3D holds the PHYSICAL sector boot1 wants (it got it out
			// of the DOS 3.3 logical->physical skew table at $0851),
			// but the .dsk image is stored in logical ("DOS") order,
			// so invert that same table to find the file sector.
			static const byte kLogicalToPhysical[16] = {
				0x00, 0x0D, 0x0B, 0x09, 0x07, 0x05, 0x03, 0x01,
				0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x0F
			};
			const byte physical = readByte(0x003D) & 0x0F;
			byte sector = 0;
			for (byte l = 0; l < 16; l++) {
				if (kLogicalToPhysical[l] == physical) {
					sector = l;
					break;
				}
			}
			const word dest = readWord(0x0026);
			const byte* src = disk->rawTrack0Sector(sector);
			for (int i = 0; i < CDisk::kSectorSize; i++) {
				writeByte(dest + i, src[i]);
			}
		}
		mRegPC.value = 0x081F;
		accCycles(6);	// rough stand-in for a real ROM sector read's cost
		return;
	}

	//fprintf(stderr, "PC = %04X\n", mRegPC.value);
	mDbgPC = mRegPC.value;
	if (mDbgArmed) {
		dbgStep();
	}
	const byte opcode = readByte(this->mRegPC.value++);
	switch (opcode) {

	case 0x69: // ADC #imm
		operand = eaimm();
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x6D: // ADC abs
		operand = readByte(eaabs());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x65: // ADC zp
		operand = readByte(eazp());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0x61: // ADC (zp,X)
		operand = readByte(eazpxind());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x71: // ADC (zp),Y
		operandAddr = eazpindy();
		operand = readByte(operandAddr);
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x75: // ADC zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr);
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x7D: // ADC abs,X
		operand = readByte(eaabsx());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x79: // ADC abs,Y
		operand = readByte(eaabsy());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableAdd[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x29: // AND #imm
		mRegA &= eaimm();
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x2D: // AND abs
		mRegA &= readByte(eaabs());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x25: // AND zp
		mRegA &= readByte(eazp());
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0x21: // AND (zp,X)
		mRegA &= readByte(eazpxind());
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x31: // AND (zp),Y
		mRegA &= readByte(eazpindy());
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x35: // AND zp,X
		mRegA &= readByte(eazpx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x3D: // AND abs,X
		mRegA &= readByte(eaabsx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x39: // AND abs,Y
		mRegA &= readByte(eaabsy());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x0E: // ASL abs
		operandAddr = eaabs();
		result = readByte(operandAddr) << 1;
		setC((result & 0x100) != 0);
		operand = result & 0xFF;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x06: // ASL zp
		operandAddr = eazp();
		result = readByte(operandAddr) << 1;
		setC((result & 0x100) != 0);
		operand = result & 0xFF;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0x0A: // ASL acc
		result = mRegA << 1;
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x16: // ASL zp,X
		operandAddr = eazpx();
		result = readByte(operandAddr) << 1;
		setC((result & 0x100) != 0);
		operand = result & 0xFF;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x1E: // ASL abs,X
		operandAddr = eaabsx();
		result = readByte(operandAddr) << 1;
		setC((result & 0x100) != 0);
		operand = result & 0xFF;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0x90: // BCC rr
		operand = earel();
		accCycles(2);
		if (!getC())
			branch(operand);
		break;

	case 0xB0: // BCS rr
		operand = earel();
		accCycles(2);
		if (getC())
			branch(operand);
		break;

	case 0xF0: // BEQ rr
		operand = earel();
		accCycles(2);
		if (getZ())
			branch(operand);
		break;

	case 0x2C: // BIT abs
		operand = readByte(eaabs());
		setN((operand & 0x80) != 0);
		setV((operand & 0x40) != 0);
		setZ((operand & mRegA) == 0);
		accCycles(4);
		break;

	case 0x24: // BIT zp
		operand = readByte(eazp());
		setN((operand & 0x80) != 0);
		setV((operand & 0x40) != 0);
		setZ((operand & mRegA) == 0);
		accCycles(3);
		break;

	case 0x30: // BMI rr
		operand = earel();
		accCycles(2);
		if (getN())
			branch(operand);
		break;

	case 0xD0: // BNE rr
		operand = earel();
		accCycles(2);
		if (!getZ())
			branch(operand);
		break;

	case 0x10: // BPL rr
		operand = earel();
		accCycles(2);
		if (!getN())
			branch(operand);
		break;

	case 0x00: // BRK
		tk2000_log("BRK trap: PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X cycles=%llu",
			mRegPC.value, mRegA, mRegX, mRegY, mRegS, (unsigned long long)mCumulativeCycles);
		stackPush(mRegPC.hi); // save PCH, PCL & P
		stackPush(mRegPC.low);
		setN(getN());
		setZ(getZ());
		setC(getC());
		setB(true);
		stackPush(mRegFlags);
		setI(true);
		mRegPC.value = readWord(0xFFFE);
		accCycles(7);
		break;

	case 0x50: // BVC rr
		operand = earel();
		accCycles(2);
		if (!getV())
			branch(operand);
		break;

	case 0x70: // BVS rr
		operand = earel();
		accCycles(2);
		if (getV())
			branch(operand);
		break;

	case 0x18: // CLC rr
		setC(false);
		accCycles(2);
		break;

	case 0xD8: // CLD
		setD(false);
		accCycles(2);
		break;

	case 0x58: // CLI
		setI(false);
		accCycles(2);
		break;

	case 0xB8: // CLV
		setV(false);
		accCycles(2);
		break;

	case 0xC9: // CMP #imm
		result = mRegA - eaimm();
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(2);
		break;

	case 0xCD: // CMP abs
		result = mRegA - readByte(eaabs());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xC5: // CMP zp
		result = mRegA - readByte(eazp());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(3);
		break;

	case 0xC1: // CMP (zp,X)
		result = mRegA - readByte(eazpxind());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(6);
		break;

	case 0xD1: // CMP (zp),Y
		result = mRegA - readByte(eazpindy());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(5);
		break;

	case 0xD5: // CMP zp,X
		result = mRegA - readByte(eazpx());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xDD: // CMP abs,X
		result = mRegA - readByte(eaabsx());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xD9: // CMP abs,Y
		result = mRegA - readByte(eaabsy());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xE0: // CPX #imm
		result = mRegX - eaimm();
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(2);
		break;

	case 0xEC: // CPX abs
		result = mRegX - readByte(eaabs());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xE4: // CPX zp
		result = mRegX - readByte(eazp());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(3);
		break;

	case 0xC0: // CPY #imm
		result = mRegY - eaimm();
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(2);
		break;

	case 0xCC: // CPY abs
		result = mRegY - readByte(eaabs());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(4);
		break;

	case 0xC4: // CPY zp
		result = mRegY - readByte(eazp());
		setC((result & 0x100) == 0);
		operand = (byte)result;
		setNZ(operand);
		accCycles(3);
		break;

	case 0xCE: // DEC abs
		operandAddr = eaabs();
		operand = readByte(operandAddr) - 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0xC6: // DEC zp
		operandAddr = eazp();
		operand = readByte(operandAddr) - 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0xD6: // DEC zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr) - 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0xDE: // DEC abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr) - 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0xCA: // DEX
		--mRegX;
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0x88: // DEY
		--mRegY;
		setNZ(mRegY);
		accCycles(2);
		break;

	case 0x49: // EOR #imm
		mRegA ^= eaimm();
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x4D: // EOR abs
		mRegA ^= readByte(eaabs());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x45: // EOR zp
		mRegA ^= readByte(eazp());
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0x41: // EOR (zp,X)
		mRegA ^= readByte(eazpxind());
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x51: // EOR (zp),Y
		mRegA ^= readByte(eazpindy());
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x55: // EOR zp,X
		mRegA ^= readByte(eazpx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x5D: // EOR abs,X
		mRegA ^= readByte(eaabsx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x59: // EOR abs,Y
		mRegA ^= readByte(eaabsy());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xEE: // INC abs
		operandAddr = eaabs();
		operand = readByte(operandAddr) + 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0xE6: // INC zp
		operandAddr = eazp();
		operand = readByte(operandAddr) + 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0xF6: // INC zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr) + 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0xFE: // INC abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr) + 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0xE8: // INX
		++mRegX;
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0xC8: // INY
		++mRegY;
		setNZ(mRegY);
		accCycles(2);
		break;

	case 0x4C: // JMP abs
		mRegPC.value = eaabs();
		accCycles(3);
		break;

	case 0x6C: // JMP (abs)
		mRegPC.value = eaabsind();
		accCycles(5);
		break;

	case 0x20: // JSR abs
		operandAddr = eaabs();
		--mRegPC.value;
		stackPush(mRegPC.hi);
		stackPush(mRegPC.low);
		mRegPC.value = operandAddr;
		accCycles(6);
		break;

	case 0xA9: // LDA #imm
		mRegA = eaimm();
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0xAD: // LDA abs
		mRegA = readByte(eaabs());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xA5: // LDA zp
		mRegA = readByte(eazp());
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0xA1: // LDA (zp,X)
		mRegA = readByte(eazpxind());
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0xB1: // LDA (zp),Y
		mRegA = readByte(eazpindy());
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0xB5: // LDA zp,X
		mRegA = readByte(eazpx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xBD: // LDA abs,X
		mRegA = readByte(eaabsx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xB9: // LDA abs,Y
		mRegA = readByte(eaabsy());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xA2: // LDX #imm
		mRegX = eaimm();
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0xAE: // LDX abs
		mRegX = readByte(eaabs());
		setNZ(mRegX);
		accCycles(4);
		break;

	case 0xA6: // LDX zp
		mRegX = readByte(eazp());
		setNZ(mRegX);
		accCycles(3);
		break;

	case 0xBE: // LDX abs,Y
		mRegX = readByte(eaabsy());
		setNZ(mRegX);
		accCycles(4);
		break;

	case 0xB6: // LDX zp,Y
		mRegX = readByte(eazpy());
		setNZ(mRegX);
		accCycles(4);
		break;

	case 0xA0: // LDY #imm
		mRegY = eaimm();
		setNZ(mRegY);
		accCycles(2);
		break;

	case 0xAC: // LDY abs
		mRegY = readByte(eaabs());
		setNZ(mRegY);
		accCycles(4);
		break;

	case 0xA4: // LDY zp
		mRegY = readByte(eazp());
		setNZ(mRegY);
		accCycles(3);
		break;

	case 0xB4: // LDY zp,X
		mRegY = readByte(eazpx());
		setNZ(mRegY);
		accCycles(4);
		break;

	case 0xBC: // LDY abs,X
		mRegY = readByte(eaabsx());
		setNZ(mRegY);
		accCycles(4);
		break;

	case 0x4E: // LSR abs
		operandAddr = eaabs();
		operand = readByte(operandAddr);
		setC((operand & 0x01) != 0);
		operand >>= 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x46: // LSR zp
		operandAddr = eazp();
		operand = readByte(operandAddr);
		setC((operand & 0x01) != 0);
		operand >>= 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0x4A: // LSR acc
		setC((mRegA & 0x01) != 0);
		mRegA >>= 1;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x56: // LSR zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr);
		setC((operand & 0x01) != 0);
		operand >>= 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x5E: // LSR abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr);
		setC((operand & 0x01) != 0);
		operand >>= 1;
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0xEA: // NOP
		accCycles(2);
		break;

	case 0x09: // ORA #imm
		mRegA |= eaimm();
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x0D: // ORA abs
		mRegA |= readByte(eaabs());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x05: // ORA zp
		mRegA |= readByte(eazp());
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0x01: // ORA (zp,X)
		mRegA |= readByte(eazpxind());
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x11: // ORA (zp),Y
		mRegA |= readByte(eazpindy());
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x15: // ORA zp,X
		mRegA |= readByte(eazpx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x1D: // ORA abs,X
		mRegA |= readByte(eaabsx());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x19: // ORA abs,Y
		mRegA |= readByte(eaabsy());
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x48: // PHA
		stackPush(mRegA);
		accCycles(3);
		break;

	case 0x08: // PHP
		setN(getN());
		setZ(getZ());
		setC(getC());
		stackPush(mRegFlags);
		accCycles(3);
		break;

	case 0x68: // PLA
		mRegA = stackPop();
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x28: // PLP
		mRegFlags = stackPop() | 0x20; // fix bug in bit5 of P
		accCycles(4);
		break;

	case 0x2E: // ROL abs
		operandAddr = eaabs();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((operand & 0x100) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x26: // ROL zp
		operandAddr = eazp();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0x2A: // ROL acc
		result = mRegA << 1;
		mRegA = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x36: // ROL zp,X
		operandAddr = eazpx();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x3E: // ROL abs,X
		operandAddr = eaabsx();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0x6E: // ROR abs
		operandAddr = eaabs();
		result = readByte(operandAddr);
		operand = (result >> 1) | (getC() ? 0x80 : 0);
		setC((result & 0x01) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x66: // ROR zp
		operandAddr = eazp();
		result = readByte(operandAddr);
		operand = (result >> 1) | (getC() ? 0x80 : 0);
		setC((result & 0x01) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(5);
		break;

	case 0x6A: // ROR acc
		result = mRegA;
		mRegA = (result >> 1) | (getC() ? 0x80 : 0);
		setC((result & 0x01) != 0);
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x76: // ROR zp,X
		operandAddr = eazpx();
		result = readByte(operandAddr);
		operand = (result >> 1) | (getC() ? 0x80 : 0);
		setC((result & 0x01) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(6);
		break;

	case 0x7E: // ROR abs,X
		operandAddr = eaabsx();
		result = readByte(operandAddr);
		operand = (result >> 1) | (getC() ? 0x80 : 0);
		setC((result & 0x01) != 0);
		setNZ(operand);
		writeByte(operandAddr, operand);
		accCycles(7);
		break;

	case 0x40: // RTI
		mRegFlags = stackPop() | 0x20; // bit 5 bug of 6502
		mRegPC.low = stackPop();
		mRegPC.hi = stackPop();
		accCycles(6);
		break;

	case 0x60: // RTS
		mRegPC.low = stackPop();
		mRegPC.hi = stackPop();
		++mRegPC.value;
		accCycles(6);
		break;

	case 0xE9: // SBC #imm
		operand = 255 - eaimm();
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0xED: // SBC abs
		operand = 255 - readByte(eaabs());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xE5: // SBC zp
		operand = 255 - readByte(eazp());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0xE1: // SBC (zp,X)
		operand = 255 - readByte(eazpxind());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0xF1: // SBC (zp),Y
		operand = 255 - readByte(eazpindy());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0xF5: // SBC zp,X
		operand = 255 - readByte(eazpx());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xFD: // SBC abs,X
		operand = 255 - readByte(eaabsx());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xF9: // SBC abs,Y
		operand = 255 - readByte(eaabsy());
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x38: // SEC
		setC(true);
		accCycles(2);
		break;

	case 0xF8: // SED
		setD(true);
		accCycles(2);
		break;

	case 0x78: // SEI
		setI(true);
		accCycles(2);
		break;

	case 0x8D: // STA abs
		writeByte(eaabs(), mRegA);
		accCycles(4);
		break;

	case 0x85: // STA zp
		writeByte(eazp(), mRegA);
		accCycles(3);
		break;

	case 0x81: // STA (zp,X)
		writeByte(eazpxind(), mRegA);
		accCycles(6);
		break;

	case 0x91: // STA (zp),Y
		writeByte(eazpindy(), mRegA);
		accCycles(6);
		break;

	case 0x95: // STA zp,X
		writeByte(eazpx(), mRegA);
		accCycles(4);
		break;

	case 0x9D: // STA abs,X
		writeByte(eaabsx(), mRegA);
		accCycles(5);
		break;

	case 0x99: // STA abs,Y
		writeByte(eaabsy(), mRegA);
		accCycles(5);
		break;

	case 0x8E: // STX abs
		writeByte(eaabs(), mRegX);
		accCycles(4);
		break;

	case 0x86: // STX zp
		writeByte(eazp(), mRegX);
		accCycles(3);
		break;

	case 0x96: // STX zp,Y
		writeByte(eazpy(), mRegX);
		accCycles(4);
		break;

	case 0x8C: // STY abs
		writeByte(eaabs(), mRegY);
		accCycles(4);
		break;

	case 0x84: // STY zp
		writeByte(eazp(), mRegY);
		accCycles(3);
		break;

	case 0x94: // STY zp,X
		writeByte(eazpx(), mRegY);
		accCycles(4);
		break;

	case 0xAA: // TAX
		mRegX = mRegA;
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0xA8: // TAY
		mRegY = mRegA;
		setNZ(mRegY);
		accCycles(2);
		break;

	case 0xBA: // TSX
		mRegX = mRegS;
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0x8A: // TXA
		mRegA = mRegX;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x9A: // TXS
		mRegS = mRegX;
		accCycles(2);
		break;

	case 0x98: // TYA
		mRegA = mRegY;
		setNZ(mRegA);
		accCycles(2);
		break;

	// --- Undocumented/"illegal" 6502 opcodes ---
	// Real assemblers of the era (including turbo-loaders and games) used
	// these on purpose -- they're stable, well-documented-by-convention
	// side effects of the 6502's decode logic, not random behavior. The
	// previous default: case treated every one of these as a 1-byte NOP,
	// which never consumes the operand byte(s) a real LAX/SAX/SLO/NOP
	// here would -- silently desyncing instruction decoding from that
	// point on. That's what was happening after the Xadrez tape finished
	// loading: it hit LAX/SAX/SLO in its own code, drifted out of sync,
	// spun through garbage for millions of cycles, and eventually landed
	// on a stray $00 (BRK).

	case 0xA3: // LAX (zp,X)
		operand = readByte(eazpxind());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0xA7: // LAX zp
		operand = readByte(eazp());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(3);
		break;

	case 0xAF: // LAX abs
		operand = readByte(eaabs());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xB3: // LAX (zp),Y
		operand = readByte(eazpindy());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0xB7: // LAX zp,Y
		operand = readByte(eazpy());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0xBF: // LAX abs,Y
		operand = readByte(eaabsy());
		mRegA = mRegX = operand;
		setNZ(mRegA);
		accCycles(4);
		break;

	case 0x83: // SAX (zp,X)
		writeByte(eazpxind(), mRegA & mRegX);
		accCycles(6);
		break;

	case 0x87: // SAX zp
		writeByte(eazp(), mRegA & mRegX);
		accCycles(3);
		break;

	case 0x8F: // SAX abs
		writeByte(eaabs(), mRegA & mRegX);
		accCycles(4);
		break;

	case 0x97: // SAX zp,Y
		writeByte(eazpy(), mRegA & mRegX);
		accCycles(4);
		break;

	case 0x03: // SLO (zp,X)  -- ASL memory, then ORA A with it
		operandAddr = eazpxind();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x07: // SLO zp
		operandAddr = eazp();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x0F: // SLO abs
		operandAddr = eaabs();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x13: // SLO (zp),Y
		operandAddr = eazpindy();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x17: // SLO zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x1B: // SLO abs,Y
		operandAddr = eaabsy();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	case 0x1F: // SLO abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr);
		setC((operand & 0x80) != 0);
		operand <<= 1;
		writeByte(operandAddr, operand);
		mRegA |= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	// DCP (DEC memory, then CMP A with it) -- same family as LAX/SAX/SLO
	// above. Karateka's tape image hits DCP/RLA/RRA/ISC/SRE/LAS/SHA in its
	// own code; none of those were implemented, so they fell into
	// `default` below, which (like the old LAX/SAX/SLO gap) doesn't
	// consume the operand bytes. PC desyncs on the very first one it
	// hits and everything after that is garbage -- explains the
	// "unimplemented opcode" cascade through $2058/$17CE/$42F0/etc. in
	// the log (that's the CPU executing stray video/RAM bytes as code)
	// and, eventually, "RETIRE SUA INTERFACE": not a real hardware check,
	// just where the derailed CPU happened to end up.
	case 0xC3: // DCP (zp,X)
		operandAddr = eazpxind();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(8);
		break;

	case 0xC7: // DCP zp
		operandAddr = eazp();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(5);
		break;

	case 0xCF: // DCP abs
		operandAddr = eaabs();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(6);
		break;

	case 0xD3: // DCP (zp),Y
		operandAddr = eazpindy();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(8);
		break;

	case 0xD7: // DCP zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(6);
		break;

	case 0xDB: // DCP abs,Y
		operandAddr = eaabsy();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(7);
		break;

	case 0xDF: // DCP abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr) - 1;
		writeByte(operandAddr, operand);
		result = mRegA - operand;
		setC((result & 0x100) == 0);
		setNZ((byte)result);
		accCycles(7);
		break;

	// RLA (ROL memory, then AND A with it)
	case 0x23: // RLA (zp,X)
		operandAddr = eazpxind();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x27: // RLA zp
		operandAddr = eazp();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x2F: // RLA abs
		operandAddr = eaabs();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x33: // RLA (zp),Y
		operandAddr = eazpindy();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x37: // RLA zp,X
		operandAddr = eazpx();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x3B: // RLA abs,Y
		operandAddr = eaabsy();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	case 0x3F: // RLA abs,X
		operandAddr = eaabsx();
		result = readByte(operandAddr) << 1;
		operand = (result & 0xFF) | (getC() ? 1 : 0);
		setC((result & 0x100) != 0);
		writeByte(operandAddr, operand);
		mRegA &= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	// SRE (LSR memory, then EOR A with it)
	case 0x43: // SRE (zp,X)
		operandAddr = eazpxind();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x47: // SRE zp
		operandAddr = eazp();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x4F: // SRE abs
		operandAddr = eaabs();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x53: // SRE (zp),Y
		operandAddr = eazpindy();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x57: // SRE zp,X
		operandAddr = eazpx();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x5B: // SRE abs,Y
		operandAddr = eaabsy();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	case 0x5F: // SRE abs,X
		operandAddr = eaabsx();
		result = readByte(operandAddr);
		operand = result >> 1;
		writeByte(operandAddr, operand);
		setC((result & 0x01) != 0);
		mRegA ^= operand;
		setNZ(mRegA);
		accCycles(7);
		break;

	// RRA (ROR memory, then ADC A with it -- using the carry the ROR
	// itself produced, not whatever carry was set going in)
	case 0x63: // RRA (zp,X)
		operandAddr = eazpxind();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x67: // RRA zp
		operandAddr = eazp();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0x6F: // RRA abs
		operandAddr = eaabs();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x73: // RRA (zp),Y
		operandAddr = eazpindy();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0x77: // RRA zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0x7B: // RRA abs,Y
		operandAddr = eaabsy();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(7);
		break;

	case 0x7F: // RRA abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr);
		result = (operand >> 1) | (getC() ? 0x80 : 0);
		setC((operand & 0x01) != 0);
		writeByte(operandAddr, (byte)result);
		operand = (byte)result;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableAdd[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(7);
		break;

	// ISC/ISB (INC memory, then SBC A with it)
	case 0xE3: // ISC (zp,X)
		operandAddr = eazpxind();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0xE7: // ISC zp
		operandAddr = eazp();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(5);
		break;

	case 0xEF: // ISC abs
		operandAddr = eaabs();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0xF3: // ISC (zp),Y
		operandAddr = eazpindy();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(8);
		break;

	case 0xF7: // ISC zp,X
		operandAddr = eazpx();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(6);
		break;

	case 0xFB: // ISC abs,Y
		operandAddr = eaabsy();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(7);
		break;

	case 0xFF: // ISC abs,X
		operandAddr = eaabsx();
		operand = readByte(operandAddr) + 1;
		writeByte(operandAddr, operand);
		operand = 255 - operand;
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0) && (((mRegA ^ result) & 0x80) != 0));
		if (getD()) { result = m_BCDTableSub[result]; }
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(7);
		break;

	// LAS/LAR (AND memory with S, load result into A, X and S)
	case 0xBB: // LAS abs,Y
		operand = readByte(eaabsy()) & mRegS;
		mRegA = mRegX = mRegS = operand;
		setNZ(operand);
		accCycles(4);
		break;

	// SHA/AHX -- unstable on real silicon (result depends on bus
	// capacitance/page-crossing quirks), but this is the commonly
	// documented approximation and beats desyncing the CPU.
	case 0x93: { // SHA (zp),Y
		word base = eazpindy();
		byte hi = (byte)(base >> 8);
		writeByte(base, mRegA & mRegX & (byte)(hi + 1));
		accCycles(6);
		break;
	}

	case 0x9F: { // SHA abs,Y
		word base = eaabs();
		byte hi = (byte)(base >> 8);
		writeByte(base + mRegY, mRegA & mRegX & (byte)(hi + 1));
		accCycles(5);
		break;
	}

	case 0x9C: { // SHY abs,X
		word base = eaabs();
		byte hi = (byte)(base >> 8);
		writeByte(base + mRegX, mRegY & (byte)(hi + 1));
		accCycles(5);
		break;
	}

	case 0x9E: { // SHX abs,Y
		word base = eaabs();
		byte hi = (byte)(base >> 8);
		writeByte(base + mRegY, mRegX & (byte)(hi + 1));
		accCycles(5);
		break;
	}

	case 0x9B: { // TAS/SHS abs,Y
		word base = eaabs();
		byte hi = (byte)(base >> 8);
		mRegS = mRegA & mRegX;
		writeByte(base + mRegY, mRegS & (byte)(hi + 1));
		accCycles(5);
		break;
	}

	// ANC (AND A with imm, then copy N into C -- as if the AND result
	// had been shifted into carry)
	case 0x0B: case 0x2B: // ANC #imm
		mRegA &= eaimm();
		setNZ(mRegA);
		setC((mRegA & 0x80) != 0);
		accCycles(2);
		break;

	case 0x4B: // ALR/ASR #imm -- AND A with imm, then LSR A
		mRegA &= eaimm();
		setC((mRegA & 0x01) != 0);
		mRegA >>= 1;
		setNZ(mRegA);
		accCycles(2);
		break;

	case 0x6B: // ARR #imm -- AND A with imm, then ROR A (binary-mode flag rules)
		mRegA &= eaimm();
		mRegA = (mRegA >> 1) | (getC() ? 0x80 : 0);
		setNZ(mRegA);
		setC((mRegA & 0x40) != 0);
		setV((((mRegA >> 6) ^ (mRegA >> 5)) & 1) != 0);
		accCycles(2);
		break;

	case 0xCB: // AXS/SBX #imm -- X = (A & X) - imm, no borrow in, sets C like CMP
		result = (mRegA & mRegX) - eaimm();
		setC((result & 0x100) == 0);
		mRegX = result & 0xFF;
		setNZ(mRegX);
		accCycles(2);
		break;

	case 0xEB: // SBC #imm (undocumented duplicate of $E9)
		operand = 255 - eaimm();
		result = operand + mRegA + (getC() ? 1 : 0);
		setV(!(((operand ^ mRegA) & 0x80) != 0)
			&& (((mRegA ^ result) & 0x80) != 0));
		if (getD()) {
			result = m_BCDTableSub[result];
		}
		setC((result & 0x100) != 0);
		mRegA = result & 0xFF;
		setNZ(mRegA);
		accCycles(2);
		break;

	// Multi-byte NOPs: read and discard the operand, do nothing else.
	case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: // NOP #imm
		eaimm();
		accCycles(2);
		break;

	case 0x04: case 0x44: case 0x64: // NOP zp
		eazp();
		accCycles(3);
		break;

	case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: // NOP zp,X
		eazpx();
		accCycles(4);
		break;

	case 0x0C: // NOP abs
		eaabs();
		accCycles(4);
		break;

	case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: // NOP abs,X
		eaabsx();
		accCycles(4);
		break;

	case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: // NOP (1 byte)
		accCycles(2);
		break;

	case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
	case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
		// JAM/KIL/HLT -- locks up a real 6502 until a hardware reset.
		// Keep re-fetching this same byte forever instead of silently
		// moving past it, which would just mask a desync somewhere else.
		tk2000_log("JAM/KIL opcode $%02X at PC=$%04X -- CPU halted",
			opcode, (word)(mRegPC.value - 1));
		mRegPC.value--;
		accCycles(2);
		break;

	default: // truly unknown instructions (all documented 6502 illegal
		// opcodes -- LAX/SAX/SLO/DCP/RLA/RRA/SRE/ISC/LAS/SHA/SHX/SHY/TAS/
		// ANC/ALR/ARR/AXS/SBC-dup -- are handled above; this default is
		// now only hit by something that shouldn't happen on a real 6502
		// at all)
		tk2000_log("unimplemented opcode $%02X at PC=$%04X cycles=%llu",
			opcode, (word)(mRegPC.value - 1), (unsigned long long)mCumulativeCycles);
		accCycles(2);
	}
	// Check interrupts
	if ((interruptFlags & INT_NMI) != 0) {
		interruptFlags &= ~INT_NMI;
		stackPush(mRegPC.hi);
		stackPush(mRegPC.low);
		stackPush(mRegFlags);
		mRegPC.value = readWord(0xFFFA);
		accCycles(6);
	}
	if ((interruptFlags & INT_IRQ) != 0 && !getI()) {
		interruptFlags &= ~INT_IRQ;
		stackPush(mRegPC.hi);
		stackPush(mRegPC.low);
		stackPush(mRegFlags);
		setI(false);
		mRegPC.value = readWord(0xFFFE);
		accCycles(6);
	}

	// Heartbeat: with no illegal-opcode/BRK/JAM to catch it, a legitimate
	// wait loop (polling a key, a soft switch, anything that never flips)
	// hangs silently. This leaves a periodic breadcrumb of where PC/A/X/Y/S
	// are, so a stall shows up as the same PC printed over and over instead
	// of no log at all.
	if (mCumulativeCycles - mLastHeartbeatCycle >= 1000000) {
		mLastHeartbeatCycle = mCumulativeCycles;
		tk2000_log("heartbeat PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X cycles=%llu",
			mRegPC.value, mRegA, mRegX, mRegY, mRegS, (unsigned long long)mCumulativeCycles);
	}
}

/*************************************************************************************************/
CCpu6502::SState CCpu6502::saveState() const {
	SState s{};
	s.a = mRegA;
	s.x = mRegX;
	s.y = mRegY;
	s.s = mRegS;
	s.flags = mRegFlags;
	s.pc = mRegPC.value;
	s.clock = mClock;
	s.fullSpeed = mFullSpeed;
	s.cumulativeCycles = mCumulativeCycles;
	s.interruptFlags = interruptFlags;
	return s;
}

/*************************************************************************************************/
void CCpu6502::loadState(const SState& s) {
	mRegA = s.a;
	mRegX = s.x;
	mRegY = s.y;
	mRegS = s.s;
	mRegFlags = s.flags;
	mRegPC.value = s.pc;
	mClock = s.clock;
	mFullSpeed = s.fullSpeed;
	mCumulativeCycles = s.cumulativeCycles;
	interruptFlags = s.interruptFlags;
}
