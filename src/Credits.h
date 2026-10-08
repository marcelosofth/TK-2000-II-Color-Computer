// Credits window: the scrolling "movie credits" panel, ported from the gme2
// core (libretro.c:658-1573) to the TK2000.
//
// A libretro core has no windows of its own -- the frontend owns the window and
// the event loop -- so, like the on-screen keyboard (Machine.cpp) and the tape
// picker (FilePicker.cpp), this "window" is painted over the 280x192 frame and
// driven from the RetroPad. It is the same box as the SELECT tape picker, with
// text rolling from the bottom to the top continuously and `=====` rules
// between sections.
//
// The music is the gme2 credits track, played as ordinary PCM to the frontend.
// NOT as a 1-bit square wave: the TK2000's beeper limits what the EMULATED
// MACHINE can produce, not what the core can hand to the frontend, and the audio
// of this window is core output, not game output. The ADPCM is decoded in real
// time by fillAudio(), block by block.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Common.h"
#include "Video.h"

class CCredits {
public:
	// Raw (level, not edge) pad state for one frame. Edge detection is done by
	// the caller, which already keeps a g_prev* flag for every button.
	//
	// The button that opened the window toggles the pause; any other button
	// closes it and slows the scroll down for next time. That split is why
	// there is no `close` flag any more: the caller decides which is which, and
	// it can, because it already knows which button was pressed to open.
	struct SInput {
		bool togglePause{ false };	// same button that opened: pause / resume
		bool close{ false };			// any other button: close
	};

	// Restarts the scroll from the top and the music from the beginning.
	void open();
	void close();
	bool isOpen() const { return mOpen; }
	bool isPaused() const { return mPaused; }

	// Advances the scroll by one frame. The music does not move here: it
	// advances in fillAudio(), by however many samples are actually sent.
	void update(const SInput& in);

	// Paints the window over the frame. No-op while closed.
	void draw(SRGB* fb) const;

	// Fills `frames` stereo frames of the credits music into `out`, which must
	// hold frames * CREDITS_TK_CHANNELS samples and is already interleaved the
	// way retro_audio_sample_batch wants. Call once per retro_run() instead of
	// the machine's own audio while the window is open. Loops at the end.
	void fillAudio(int16_t* out, size_t frames);

	// Which colour a row is drawn in. Chosen by the letter at the start of the
	// line in kCreditsLines[] ('G' green, 'V' red, 'P' plain), not by the row's
	// position, so a blank line after a title stays blank.
	enum class EColor { WHITE, GREEN, RED };

	struct SRow {
		std::string text;
		int y;			// offset in pixels from the top of the text block
		EColor color{ EColor::WHITE };
		bool rule{ false };	// draw as one solid bar instead of glyphs
	};

	// Splits kCreditsLines[] into SRow, wrapping long lines to the box width,
	// and caches the resulting height. Done by open().
	void buildRows();

	// Base64-decodes the ADPCM payload into mMusic. Done once, on the first
	// fillAudio() after open() -- about 30 ms, invisible at window open.
	void loadMusic();
	// Decodes mMusic's current block into mPcm and advances to the next.
	void decodeBlock();

	// The karateka sprite that scrolls up with the text. Base64-decoded from
	// credits_karateka.h (generated from karateka.gif) into a flat RLE stream,
	// then expanded a frame at a time in drawSprite().
	void loadSprite();
	// Draws frame mSpriteFrame with its top-left at (x, y), fading out above
	// y == fadeTop and cut at y == clipBottom, so it obeys exactly the same
	// limits as the text rows. No-op once the RLE stream has run out.
	void drawSprite(SRGB* fb, int x, int y, int fadeTop, int clipBottom) const;
	// Base64-decodes the photo. Done once, on the first open().
	void loadPhoto();
	// Draws the photo centred in the content box, with its top-left at (x, y),
	// under the same fade and clip as the text rows.
	void drawPhoto(SRGB* fb, int x, int y, int fadeTop, int clipBottom) const;

	bool mOpen{ false };
	bool mPaused{ false };	// set by the button that opened the window
	int mY{ 0 };			// scroll position, 8.8 fixed point (256 = 1 px)
	// Frames counted since the last 1-pixel step. The text moves exactly 1 px
	// every kFramesPerPx frames (Credits.cpp), so every step is the same length.
	int mScrollTick{ 0 };

	std::vector<SRow> mRows;
	int mContentH{ 0 };	// height of the whole block, in pixels
	int mTravel{ 0 };		// pixels before looping: the block plus the window

	// --- karateka sprite ---
	std::vector<unsigned char> mSpriteRle;	// the decoded RLE stream
	mutable std::vector<unsigned char> mSpritePix;	// the frame, decompressed
	int mSpriteFrame{ 0 };		// current frame, 0..KARATEKA_FRAMES-1
	int mSpriteTimer{ 0 };		// frames left before the next one
	// Y of the row the sprite rides with, in pixels from the top of the text
	// block. Set by buildRows() to the row whose text mentions KARATEKA, so the
	// sprite only shows up during that section -- which is also why it does not
	// collide with the long lines of the history paragraph further down. -1 if
	// no such row exists.
	int mSpriteRowY{ -1 };

	// --- the photo that closes the credits ---
	std::vector<unsigned char> mPhoto;	// RGB, 3 bytes per pixel
	// Y of the photo's top edge, in pixels from the top of the text block. It
	// sits past the last row, so it rises after everything is read.
	int mPhotoY{ 0 };

	// --- music playback state ---
	std::vector<unsigned char> mMusic;	// ADPCM payload, decoded from base64
	std::vector<int16_t> mPcm;			// one decoded block, interleaved stereo
	int mBlock{ 0 };
	int mPos{ 0 };
	bool mMusicLoaded{ false };
};
