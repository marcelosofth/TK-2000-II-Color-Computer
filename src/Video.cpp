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
#include "Video.h"
#include "CoreLog.h"

static bool pixels[VIDEOWIDTH][VIDEOHEIGHT];
static bool colorMod[VIDEOWIDTH / 7][VIDEOHEIGHT];

/*****************************************************************************/
void CVideo::drawMono() {
	word memAddr = (mSecondPage ? 0xA000 : 0x2000);
	byte *pnt;
	for (int y = 0; y < VIDEOHEIGHT; y++) {
		int offset = ((y & 7) << 10) + ((y & 0x38) << 4) + (y >> 6) * 40;
		pnt = mRamPtr + memAddr + offset;
		for (int x = 0; x < VIDEOWIDTH; x += 7) {
			byte v = *pnt++;
			for (int b = 0; b < 7; b++) {
				const int index = y * VIDEOWIDTH + x + b;
				if (v & (1 << b)) {
					mFrameBuffer[index].red = 255;
					mFrameBuffer[index].green = 255;
					mFrameBuffer[index].blue = 255;
				} else {
					mFrameBuffer[index].red = 0;
					mFrameBuffer[index].green = 0;
					mFrameBuffer[index].blue = 0;
				}
			}
		}
	}

}

/*****************************************************************************/
void CVideo::drawColor() {
	word memAddr = (mSecondPage ? 0xA000 : 0x2000);
	byte *ptr;
	for (int y = 0; y < VIDEOHEIGHT; y++) {
		int offset = ((y & 7) << 10) + ((y & 0x38) << 4) + (y >> 6) * 40;
		ptr = mRamPtr + memAddr + offset;
		for (int x = 0; x < VIDEOWIDTH; x += 7) {
			byte v = *ptr++;
			colorMod[x/7][y] = (v & 0x80) != 0;
			for (int b = 0; b < 7; b++) {
				pixels[x + b][y] = (v & (1 << b)) != 0;
			}
		}
	}
	// Per-pixel colouring (instead of one colour per 2-pixel block).
	// The old block method could only show white when BOTH pixels of an
	// even/odd pair were lit, so 1-pixel-wide details (like the small
	// letters on Karateka's mountain) got turned into cyan/red blocks.
	// Rules, close to what a real TV shows:
	//   lit pixel with a lit neighbour        -> white
	//   lone lit pixel                        -> colour of its column parity
	//   unlit pixel between two lone lit ones -> same colour (solid fill,
	//                                            keeps flat colour areas solid)
	//   anything else                         -> black
	for (int y = 0; y < VIDEOHEIGHT; y++) {
		auto lit = [y](int x) -> bool {
			return x >= 0 && x < VIDEOWIDTH && pixels[x][y];
		};
		auto lone = [&lit](int x) -> bool {
			return lit(x) && !lit(x - 1) && !lit(x + 1);
		};
		for (int x = 0; x < VIDEOWIDTH; x++) {
			SRGB& out = mFrameBuffer[y * VIDEOWIDTH + x];
			const bool cm = colorMod[x / 7][y];
			int colour = -1;	// -1 black, 0 = even-column colour, 1 = odd-column colour, 2 white
			if (lit(x)) {
				colour = (lit(x - 1) || lit(x + 1)) ? 2 : (x & 1);
			} else if (lone(x - 1) && lone(x + 1)) {
				colour = (x - 1) & 1;
			}
			switch (colour) {
			case 0:	// even column: cyan / green
				out.red = 32;
				out.green = cm ? 176 : 192;
				out.blue = cm ? 176 : 0;
				break;
			case 1:	// odd column: red / blue
				out.red = cm ? 250 : 0;
				out.green = cm ? 16 : 128;
				out.blue = cm ? 0 : 255;
				break;
			case 2:	// white
				out.red = 255;
				out.green = 255;
				out.blue = 255;
				break;
			default:	// black
				out.red = 0;
				out.green = 0;
				out.blue = 0;
				break;
			}
		}
	}
}

/*****************************************************************************/
CVideo::CVideo(CBus& bus, byte* ramPtr) :
	mRamPtr(ramPtr)
{
	assert(ramPtr != nullptr);
	bus.addDevice(EDevices::VIDEO, this);
	bus.registerAddr(EDevices::VIDEO, 0xC050, 0xC051);
	bus.registerAddr(EDevices::VIDEO, 0xC054, 0xC055);
	// TEMP INSTRUMENTATION (2026-09-27): see mAn0/mAn1/mAn2 in Video.h.
	bus.registerAddr(EDevices::VIDEO, 0xC058, 0xC05D);
}

/*****************************************************************************/
byte CVideo::read(const word addr, const uint64_t cycles) {
	switch (addr & 0x00FF) {
	case 0x50:
		mVideoMono = false;
		break;

	case 0x51:
		mVideoMono = true;
		break;

	case 0x54:
		mSecondPage = false;
		break;

	case 0x55:
		mSecondPage = true;
		break;

	// TEMP INSTRUMENTATION (2026-09-27): AN0-AN2, see Video.h. Logged
	// unconditionally (like Joystick's $C070-$C07F writes) -- these are
	// expected to be rare, and knowing exactly when each is hit (versus
	// $C05A specifically, seen once very early in the Karateka tape
	// bootstrap) is the whole point right now.
	case 0x58:
		mAn0 = false;
		tk2000_log("an0 off ($C058) cycles=%llu", (unsigned long long)cycles);
		break;
	case 0x59:
		mAn0 = true;
		tk2000_log("an0 on ($C059) cycles=%llu", (unsigned long long)cycles);
		break;
	case 0x5A:
		mAn1 = false;
		tk2000_log("an1 off ($C05A) cycles=%llu", (unsigned long long)cycles);
		break;
	case 0x5B:
		mAn1 = true;
		tk2000_log("an1 on ($C05B) cycles=%llu", (unsigned long long)cycles);
		break;
	case 0x5C:
		mAn2 = false;
		tk2000_log("an2 off ($C05C) cycles=%llu", (unsigned long long)cycles);
		break;
	case 0x5D:
		mAn2 = true;
		tk2000_log("an2 on ($C05D) cycles=%llu", (unsigned long long)cycles);
		break;

	}
	return 0xFF;
}

/*****************************************************************************/
void CVideo::write(const word addr, const byte data, const uint64_t cycles) {
	read(addr, cycles);
}

/*****************************************************************************/
void CVideo::reset() {
	mVideoMono = false;
	mSecondPage = false;
	mAn0 = false;
	mAn1 = false;
	mAn2 = false;
}

/*****************************************************************************/
SRGB* CVideo::getFrameBuffer() {
	memset(mFrameBuffer, 0, sizeof(mFrameBuffer));
	mVideoMono ? drawMono() : drawColor();
	return mFrameBuffer;
}
