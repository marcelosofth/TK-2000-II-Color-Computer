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
#include "Disk.h"
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

namespace {
	// Standard Apple II 6-and-2 GCR translate table: 64 six-bit values
	// (0-63) -> the "disk byte" that represents them. Every entry has bit
	// 7 set (so RWTS's "is data ready yet" poll never has to wait) and
	// never has more than one pair of consecutive zero bits, which is
	// what actually makes these safe to store as flux transitions on a
	// real disk -- irrelevant here, but it's why the table looks the way
	// it does. This exact table is universal across Disk ][-compatible
	// controllers (every Apple II emulator ships the same 64 values).
	constexpr byte kGcrTable[64] = {
		0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,
		0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
		0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
		0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
		0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,
		0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
		0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,
		0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
	};

	// .dsk images are in DOS 3.3 logical order, but the address field of each
	// nibblized sector carries the PHYSICAL sector number (that is what RWTS
	// searches for, after running its logical->physical skew table). So
	// physical sector P of a track has to hold file (logical) sector
	// kPhysToDosSector[P]. Identity order here made every RWTS read after the
	// boot sectors return the wrong data.
	constexpr byte kPhysToDosSector[16] = {
		0x00,0x07,0x0E,0x06,0x0D,0x05,0x0C,0x04,
		0x0B,0x03,0x0A,0x02,0x09,0x01,0x08,0x0F
	};

	inline byte code44A(byte a) { return (byte)(((a >> 1) & 0x55) | 0xAA); }
	inline byte code44B(byte a) { return (byte)((a & 0x55) | 0xAA); }

	// fileName arrives here as UTF-8 (that's what the frontend/libretro
	// hand us for accented paths like "Amazônia (Nova Versão).dsk"). Plain
	// fopen(const char*) on Windows runs those bytes through the ANSI
	// codepage instead of treating them as UTF-8, so a path with any
	// non-ASCII character silently fails to open (or opens the wrong
	// file) there -- this doesn't happen on Linux/macOS, where fopen
	// passes the bytes straight to the OS and UTF-8 just works. On
	// Windows we convert to UTF-16 ourselves and use _wfopen instead;
	// everywhere else this is exactly the fopen call it replaces.
	FILE* openForRead(const char* fileNameUtf8) {
#ifdef _WIN32
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, fileNameUtf8, -1, nullptr, 0);
		if (wlen <= 0) {
			return nullptr;
		}
		std::wstring wPath((size_t)wlen - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, fileNameUtf8, -1, wPath.data(), wlen);
		return _wfopen(wPath.c_str(), L"rb");
#else
		return fopen(fileNameUtf8, "rb");
#endif
	}
}

/*************************************************************************************************/
CDisk::CDisk(CBus& bus) {
	bus.addDevice(EDevices::DISK, this);
	bus.registerAddr(EDevices::DISK, 0xC0E0, 0xC0EF);
}

/*************************************************************************************************/
void CDisk::reset() {
	// Power-on/reset state. Deliberately does NOT touch mLoaded/mNibbles
	// -- a reset doesn't eject the disk, same as real hardware.
	mQuarterTrack = 0;
	mPhaseOn[0] = mPhaseOn[1] = mPhaseOn[2] = mPhaseOn[3] = false;
	mMotorOn = false;
	mMotorOffValid = false;
	mMotorOffCycles = 0;
	mDriveSelected = 0;
	mWriteMode = false;
	mBytePos = 0;
	mLatchValid = false;
	mLastByteCycles = 0;
	mLatch = 0;
}

/*************************************************************************************************/
int CDisk::currentTrack() const {
	int t = mQuarterTrack / 4;
	return (t >= kTracks) ? (kTracks - 1) : t;
}

/*************************************************************************************************/
void CDisk::stepPhase(int phase) {
	// Only an OFF->ON transition on a phase adjacent to the one the head
	// is already "resting" on moves it -- one quarter-track towards
	// whichever neighbour just energized. The opposite phase, or the one
	// already on, doesn't move anything. This is the actual real-hardware
	// stepper-motor protocol (not a simplification): DOS 3.3's RWTS,
	// unmodified, on the disk image itself, drives $C0E0-$C0E7 this exact
	// way whenever it seeks to a different track after boot.
	// One phase step = one HALF-track (two phases per track), i.e. 2
	// quarter-tracks. The phase the head is resting on is the half-track
	// number mod 4. (Stepping by one quarter-track per phase made RWTS seek
	// to the wrong tracks: it needs 2 phase steps per track, not 4.)
	const int cur = (mQuarterTrack / 2) % 4;
	if (phase == (cur + 1) % 4) {
		if (mQuarterTrack < kQuarterTracksMax - 2) {
			mQuarterTrack += 2;
		}
	} else if (phase == (cur + 3) % 4) {
		if (mQuarterTrack >= 2) {
			mQuarterTrack -= 2;
		}
	}
	// phase == cur, or the opposite phase: no movement.
}

/*************************************************************************************************/
void CDisk::touchSoftSwitch(const word addr, const uint64_t cycles) {
	if (addr <= 0xC0E7) {
		const int phase = (addr - 0xC0E0) / 2;
		const bool turnOn = ((addr - 0xC0E0) & 1) != 0;
		if (turnOn && !mPhaseOn[phase]) {
			stepPhase(phase);
		}
		mPhaseOn[phase] = turnOn;
	} else if (addr == 0xC0E8) {
		if (mMotorOn) {
			mMotorOn = false;
			mMotorOffCycles = cycles;
			mMotorOffValid = true;	// keeps "spinning" for kSpinDownCycles
		}
	} else if (addr == 0xC0E9) {
		mMotorOn = true;
		mMotorOffValid = false;
	} else if (addr == 0xC0EA) {
		mDriveSelected = 0;
	} else if (addr == 0xC0EB) {
		mDriveSelected = 1;
	} else if (addr == 0xC0EE) {
		mWriteMode = false;
	} else if (addr == 0xC0EF) {
		mWriteMode = true;
	}
}

/*************************************************************************************************/
byte CDisk::read(const word addr, const uint64_t cycles) {
	if (addr == 0xC0EC) {
		// Q6L / data register. Real hardware only presents a byte once
		// the shift register has assembled one (bit 7 set = ready); every
		// byte in mNibbles already has bit 7 set (see kGcrTable / the
		// prologue/epilogue/self-sync constants below), so it's always
		// "ready" the instant it's asked for -- RWTS's own "BPL"-style
		// wait loops fall straight through, same as they'd normally do
		// after however many real polls it took.
		if (mWriteMode || !spinning(cycles) || !mLoaded || mDriveSelected != 0) {
			return 0xFF;
		}
		// The real latch is the shift register itself, so a read that comes less than one byte
		// time after the previous byte returns the register mid-shift: MSB clear (= "not ready
		// yet", which every poll loop -- LDA $C08C,X / BPL -- simply retries) and a value that
		// differs from the byte before it.
		//
		// This matters because DOS 3.3's RWTS starts every call with a "is the disk spinning?"
		// test ($BD3A): it reads $C08C twice, ~22 cycles apart, and takes "two identical reads"
		// to mean the motor is stopped -- then it sits in a ~1 s spin-up wait (the $CA/$CB loop
		// at $BD9E) before EVERY sector. Handing out a brand-new byte on every read made both
		// reads equal inside the $FF self-sync gaps, so a plain DOS 3.3 disk (Amazonia) crawled
		// at ~61 frames per sector while disks with their own loader (Furia Galactica) did not.
		if (mLatchValid && (cycles - mLastByteCycles) < kByteCycles) {
			return (byte)(mLatch & 0x7F);
		}
		mLatch = mNibbles[currentTrack()][mBytePos];
		mBytePos = (mBytePos + 1) % kNibblesPerTrack;
		mLastByteCycles = cycles;
		mLatchValid = true;
		return mLatch;
	}
	if (addr == 0xC0ED) {
		// Q6H: write-protect sense on some access patterns. This core
		// doesn't support writing, but "not write-protected" is the
		// harmless answer either way (nothing here checks it before a
		// read, and there's no write path to gate).
		return 0x00;
	}
	touchSoftSwitch(addr, cycles);
	return 0xFF;
}

/*************************************************************************************************/
void CDisk::write(const word addr, const byte data, const uint64_t cycles) {
	if (addr == 0xC0EC) {
		// Writing a sector back to disk isn't supported -- nothing this
		// core needs to run (Karateka included) writes to its own disk
		// during play. Silently ignored rather than corrupting mNibbles.
		return;
	}
	if (addr == 0xC0ED) {
		return;
	}
	touchSoftSwitch(addr, cycles);
}

/*************************************************************************************************/
void CDisk::encode62(const byte* data256, byte out343[343]) const {
	// Standard Apple "6-and-2" nibblization: 256 data bytes -> 342 six-bit
	// values -> a 343rd running-XOR checksum byte -> GCR-translated disk
	// bytes. The first 86 six-bit values each pack the low 2 bits of three
	// source bytes (indices cycling through the 256-byte buffer via the
	// same -0x56/-0x56/-0x53 byte-wrapping steps real Disk ][ firmware
	// uses); the remaining 256 six-bit values are the top 6 bits of each
	// source byte, taken directly (the GCR translate step below only ever
	// looks at byte>>2, so the untouched low 2 bits there don't matter).
	byte sixBit[342];
	byte offset = 0xAC;
	int out = 0;
	while (offset != 0x02) {
		byte value = 0;
		auto addValue = [&](byte a) {
			value = (byte)((value << 2) | ((a & 0x01) << 1) | ((a & 0x02) >> 1));
		};
		addValue(data256[offset]); offset = (byte)(offset - 0x56);
		addValue(data256[offset]); offset = (byte)(offset - 0x56);
		addValue(data256[offset]); offset = (byte)(offset - 0x53);
		sixBit[out++] = (byte)(value << 2);
	}
	sixBit[out - 2] &= 0x3F;
	sixBit[out - 1] &= 0x3F;
	for (int i = 0; i < 256; i++) {
		sixBit[out++] = data256[i];
	}
	// out == 342 here.

	byte xored[343];
	byte savedVal = 0;
	for (int i = 0; i < 342; i++) {
		xored[i] = (byte)(savedVal ^ sixBit[i]);
		savedVal = sixBit[i];
	}
	xored[342] = savedVal;	// checksum byte

	for (int i = 0; i < 343; i++) {
		out343[i] = kGcrTable[xored[i] >> 2];
	}
}

/*************************************************************************************************/
void CDisk::buildTrack(int track, const byte* rawSectors) {
	byte* p = mNibbles[track];
	int n = 0;
	auto put = [&](byte b) { p[n++] = b; };

	for (int i = 0; i < 48; i++) {
		put(0xFF);	// gap1: self-sync bytes before the track's first sector
	}

	for (int sector = 0; sector < kSectorsPerTrack; sector++) {
		// Address field: prologue, volume/track/sector/checksum (each
		// "4-and-4" encoded as two bytes), epilogue.
		put(0xD5); put(0xAA); put(0x96);
		const byte t = (byte)track;
		const byte s = (byte)sector;
		const byte chk = (byte)(kVolumeNumber ^ t ^ s);
		put(code44A(kVolumeNumber)); put(code44B(kVolumeNumber));
		put(code44A(t));             put(code44B(t));
		put(code44A(s));             put(code44B(s));
		put(code44A(chk));           put(code44B(chk));
		put(0xDE); put(0xAA); put(0xEB);

		for (int i = 0; i < 6; i++) {
			put(0xFF);	// gap2: self-sync bytes between address and data fields
		}

		// Data field: prologue, 343 GCR-encoded bytes (256 data + checksum), epilogue.
		put(0xD5); put(0xAA); put(0xAD);
		byte encoded[343];
		encode62(rawSectors + kPhysToDosSector[sector] * kSectorSize, encoded);
		for (int i = 0; i < 343; i++) {
			put(encoded[i]);
		}
		put(0xDE); put(0xAA); put(0xEB);

		for (int i = 0; i < 27; i++) {
			put(0xFF);	// gap3: self-sync bytes before the next sector
		}
	}
	assert(n == kNibblesPerTrack);
}

/*************************************************************************************************/
bool CDisk::insertDisk(const char* fileName) {
	constexpr long kExpectedSize = (long)kTracks * kSectorsPerTrack * kSectorSize;	// 143,360
	FILE* f = openForRead(fileName);
	if (!f) {
		return false;
	}
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	// Only plain, unheadered "DOS order" .dsk/.do images are supported --
	// no .po (ProDOS order), .nib, .woz, or zip/gz wrapping.
	if (size != kExpectedSize) {
		fclose(f);
		return false;
	}
	std::vector<byte> raw((size_t)kExpectedSize);
	const size_t rlen = fread(raw.data(), 1, raw.size(), f);
	fclose(f);
	if (rlen != raw.size()) {
		return false;
	}

	for (int t = 0; t < kTracks; t++) {
		buildTrack(t, &raw[(size_t)t * kSectorsPerTrack * kSectorSize]);
	}
	memcpy(mRawTrack0, raw.data(), sizeof(mRawTrack0));

	mLoaded = true;
	reset();
	return true;
}
