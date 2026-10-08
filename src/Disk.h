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
// --- Disk ][-compatible floppy controller (.dsk, 35x16x256 DOS order) ---
// The TK2000 ROM this core carries doesn't include any disk controller
// firmware (checked: $C600-$C6FF in Rom.cpp is TK2000 BASIC/monitor data,
// not boot code), so there's nothing at $C600 to jump to and nothing at
// $C0E0-$C0EF driving real disk hardware. Both are added here.
//
// A real Disk ][ boot ROM's only job is: recalibrate to track 0, read
// track 0 / sector 0 into RAM at $0800 (its own first byte says how many
// more sectors of track 0 to load the same way), then jump to $0801.
// CMachine::bootFromDisk() does exactly that directly in C++ once, using
// the raw sector bytes handed back by rawTrack0Sector() below -- no 6502
// boot ROM needed for that one-time handoff.
//
// Everything AFTER that handoff (DOS 3.3's own RWTS loading the rest of
// DOS, then the game) runs from the disk image itself, unmodified, and
// drives the real $C0E0-$C0EF soft switches directly -- stepper-motor
// phase, drive select, read/write mode, data register -- so that part
// has to behave like genuine Disk ][ hardware. That's what read()/write()
// below emulate, and insertDisk() nibblizes (6-and-2 GCR-encodes, complete
// with address/data field prologues and a self-sync gap) the raw 256-byte
// sectors into the same per-track byte layout RWTS expects to see arrive
// off the "spinning" disk, so it can decode them with its own unmodified
// code exactly as it would on real hardware.
//
// $C0E0-$C0EF layout (slot 6; matches the real Disk ][ card and is what
// DOS 3.3's RWTS -- baked into every DOS 3.3 disk, this one included --
// assumes without asking):
//   $C0E0/E1  phase 0 off/on      $C0E2/E3  phase 1 off/on
//   $C0E4/E5  phase 2 off/on      $C0E6/E7  phase 3 off/on
//   $C0E8     motor off           $C0E9     motor on
//   $C0EA     select drive 1      $C0EB     select drive 2 (not emulated)
//   $C0EC     data register (Q6L) $C0ED     Q6H (write-protect sense)
//   $C0EE     read mode (Q7L)     $C0EF     write mode (Q7H)
// Any of these except $C0EC/$C0ED fire on a plain read too, not just a
// write -- real boot/RWTS code hits most of them with LDA, not STA.

#pragma once

#include "Common.h"
#include "Device.h"
#include "Bus.h"

class CDisk final : public CDevice {
public:
	explicit CDisk(CBus& bus);
	// CDevice
	byte read(const word addr, const uint64_t cycles) override;
	void write(const word addr, const byte data, const uint64_t cycles) override;
	void reset() override;
	void update(const uint64_t cycles) override {}

	// Loads a 143,360-byte (35 tracks x 16 sectors x 256 bytes, "DOS
	// order") .dsk image and builds the nibblized GCR track buffers used
	// by read()/write() above. Returns false (nothing changed) if the
	// file can't be opened or isn't exactly that size -- this core only
	// supports plain, unheadered .dsk/.do images, not .po/.nib/.woz/zips.
	bool insertDisk(const char* fileName);
	bool isLoaded() const { return mLoaded; }
	// What a real Disk ][ boot ROM leaves behind when it hands over to boot1:
	// drive 1 selected and the motor already spinning. Some boot1/RWTS
	// variants (e.g. Bombardeio.dsk) never touch $C0E9 themselves and read
	// garbage ($FF) forever if the motor is still off.
	void powerOnDrive() { mDriveSelected = 0; mMotorOn = true; }

	static constexpr int kSectorSize = 256;
	static constexpr int kSectorsPerTrack = 16;

	// Raw (still GCR-free) bytes of track 0, sector `sector` (0-15) of
	// whatever disk was last successfully inserted -- used once, right
	// after insertDisk() succeeds, by CMachine::bootFromDisk(). Only
	// track 0 is kept in raw form; every other track only exists in its
	// nibblized form in mNibbles, same as real disk hardware never hands
	// back "raw" sector bytes either -- RWTS decodes them itself.
	const byte* rawTrack0Sector(int sector) const { return &mRawTrack0[sector * kSectorSize]; }

private:
	static constexpr int kTracks = 35;
	// 48 (gap1) + 16 sectors * (14 addr-field + 6 gap2 + 349 data-field + 27 gap3)
	// = 48 + 16*396 = 6384 -- matches the standard Apple II .nib track size.
	static constexpr int kNibblesPerTrack = 6384;
	static constexpr int kQuarterTracksMax = kTracks * 4;
	// Arbitrary but fixed volume number baked into every address-field
	// checksum; DOS 3.3's RWTS doesn't reject a mismatched volume on a
	// normal LOAD/BLOAD, so any fixed value works. 254 matches what this
	// disk image itself reports (VTOC volume #254).
	static constexpr byte kVolumeNumber = 254;

	void buildTrack(int track, const byte* rawSectors);	// rawSectors: 16*256 bytes, logical order
	void encode62(const byte* data256, byte out343[343]) const;
	void touchSoftSwitch(const word addr, const uint64_t cycles);
	// True while the platter is turning: motor on, or switched off less than kSpinDownCycles ago.
	bool spinning(const uint64_t cycles) const {
		return mMotorOn || (mMotorOffValid && (cycles - mMotorOffCycles) < kSpinDownCycles);
	}
	void stepPhase(int phase);
	int currentTrack() const;

	byte mNibbles[kTracks][kNibblesPerTrack]{};
	byte mRawTrack0[kSectorsPerTrack * kSectorSize]{};
	bool mLoaded{ false };

	// Stepper-motor head position, in quarter-tracks (0..kQuarterTracksMax-1);
	// currentTrack() = mQuarterTrack / 4, same convention real Disk ][
	// firmware and RWTS use.
	int mQuarterTrack{ 0 };
	bool mPhaseOn[4]{ false, false, false, false };
	bool mMotorOn{ false };
	// A real drive does not stop the instant $C0E8 is touched: the disk coasts for about a second.
	// DOS 3.3's RWTS switches the motor off at the end of EVERY call ($BE4D) and, at the start of the
	// next one, checks whether the data register still changes to decide if it must wait ~1 s for
	// the motor to spin up. If the emulated disk stops dead, that check fails every time.
	static constexpr uint64_t kSpinDownCycles = 1000000;
	uint64_t mMotorOffCycles{ 0 };
	bool mMotorOffValid{ false };
	int mDriveSelected{ 0 };	// 0 = drive 1 (the only drive this core emulates), 1 = drive 2
	bool mWriteMode{ false };	// Q7: false = read (Q7L), true = write (Q7H, unsupported -- see write())
	int mBytePos{ 0 };			// read position within mNibbles[currentTrack()]

	// Data latch timing (see read() for why). A byte only "completes" every kByteCycles CPU
	// cycles (8 bits * 4 us at 1.02 MHz = ~32); a read that arrives sooner than that after the
	// last completed byte catches the shift register mid-byte -- MSB still clear, value
	// different from the previous byte -- exactly like a real Disk ][.
	static constexpr uint64_t kByteCycles = 28;	// a bit under 32: margin for the core's cycle accounting
	uint64_t mLastByteCycles{ 0 };	// CPU cycle count when the last byte was handed out
	byte mLatch{ 0 };				// that byte
	bool mLatchValid{ false };
};
