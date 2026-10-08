// File picker window drawn directly into the emulated video frame.
//
// A libretro core has no windows of its own (the frontend owns the window
// and the event loop), so -- like the on-screen keyboard in Machine.cpp --
// the "window" is painted over the 280x192 frame and navigated with the
// RetroPad. It lists the folders and .ct2 tapes of a directory so the
// player can pick the next cassette side (e.g. Karateka disco 2).
//
// Pure UI + directory listing: it knows nothing about the emulation. The
// caller (libretro.cpp) opens it, feeds it the pad state once per frame,
// and acts on the result.

#pragma once

#include <string>
#include <vector>
#include "Common.h"
#include "Video.h"

class CFilePicker {
public:
	// Raw (level, not edge) pad state for one frame. Edge detection and
	// hold-to-repeat are handled inside update().
	struct SInput {
		bool up{ false };
		bool down{ false };
		bool left{ false };		// page up
		bool right{ false };	// page down
		bool ok{ false };		// RetroPad A: enter folder / choose tape
		bool cancel{ false };	// RetroPad B: close without choosing
	};
	enum class EResult { None, Chosen, Cancelled };

	// Finds the "roms/tk2000" folder by looking in each of `bases` and their
	// parent folders. Returns "" if there is none.
	static std::string findRomsDir(const std::vector<std::string>& bases);

	// Opens the window in `preferredDir` (normally roms/tk2000); if that is
	// empty/missing, in the folder of `currentImage`, else the working
	// directory. The cursor starts on the tape that comes right after
	// `currentImage` (matched by name), since the usual reason to open the
	// picker is "give me the next side".
	void open(const std::string& currentImage, const std::string& preferredDir);
	void close() { mOpen = false; }
	bool isOpen() const { return mOpen; }

	// Call once per frame while open. Returns Chosen when a tape was
	// picked (then chosenPath() is valid) and Cancelled when B was pressed.
	// The window is left open on Chosen/Cancelled -- the caller closes it.
	EResult update(const SInput& in);
	const std::string& chosenPath() const { return mChosen; }

	// Paints the window over the frame.
	void draw(SRGB* fb) const;

private:
	struct SEntry {
		std::string name;
		bool isDir;
	};

	// Returns true if `selectName` was found and the cursor is on it.
	bool loadDir(const std::string& dir, const std::string& selectName);
	void move(int delta);
	// Enter a folder, or pick a tape (returns true and fills mChosen).
	bool activate();
	void keepCursorVisible();

	bool mOpen{ false };
	std::string mDir;
	std::string mCurrentImage;
	std::string mCurrentStem;	// lower-case name without extension; marked with '*'
	std::vector<SEntry> mEntries;
	bool mReadError{ false };
	int mCursor{ 0 };
	int mScroll{ 0 };
	std::string mChosen;

	// Edge / auto-repeat bookkeeping.
	bool mPrevOk{ false };
	bool mPrevCancel{ false };
	int mHoldUp{ 0 };
	int mHoldDown{ 0 };
	int mHoldLeft{ 0 };
	int mHoldRight{ 0 };
};
