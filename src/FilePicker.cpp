// See FilePicker.h. Directory listing uses <dirent.h>/stat() (available on
// Linux, macOS and MinGW), so no extra libraries are needed.

#include "pch.h"
#include "FilePicker.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#define TK_GETCWD _getcwd
#else
#include <unistd.h>
#define TK_GETCWD getcwd
#endif

namespace {

// --- Geometry (280x192 frame) ---
constexpr int kPanelX = 8;
constexpr int kPanelY = 8;
constexpr int kPanelW = VIDEOWIDTH - 16;	// 264
constexpr int kPanelH = VIDEOHEIGHT - 16;	// 176
constexpr int kTitleH = 11;
constexpr int kPathY = kPanelY + kTitleH + 2;
constexpr int kListY = kPanelY + kTitleH + 14;
constexpr int kRowH = 9;
constexpr int kFooterH = 12;
constexpr int kRows = (kPanelH - (kListY - kPanelY) - kFooterH) / kRowH;	// 15
constexpr int kCharW = 6;	// 5px glyph + 1px gap
constexpr int kListX = kPanelX + 5;
constexpr int kListW = kPanelW - 10 - 5;	// leaves room for the scrollbar
constexpr int kMaxChars = kListW / kCharW;
constexpr int kPage = kRows - 1;

// --- Colors ---
constexpr SRGB kBg{ 0, 0, 70 };
constexpr SRGB kBorder{ 255, 255, 255 };
constexpr SRGB kTitleBg{ 0, 80, 200 };
constexpr SRGB kText{ 255, 255, 255 };
constexpr SRGB kPathText{ 120, 220, 255 };
constexpr SRGB kDirText{ 255, 220, 80 };
constexpr SRGB kDimText{ 170, 170, 190 };
constexpr SRGB kSelBg{ 255, 255, 255 };
constexpr SRGB kSelText{ 0, 0, 70 };
constexpr SRGB kTrack{ 40, 40, 120 };
constexpr SRGB kThumb{ 200, 200, 200 };

// --- 5x7 font, 5 bits/row (bit4 = left), top row first ---
struct SGlyph { char ch; byte rows[7]; };
const SGlyph kFont[] = {
	{ '0', {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110} },
	{ '1', {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110} },
	{ '2', {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111} },
	{ '3', {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110} },
	{ '4', {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010} },
	{ '5', {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110} },
	{ '6', {0b00110,0b01000,0b10000,0b11110,0b10001,0b10001,0b01110} },
	{ '7', {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000} },
	{ '8', {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110} },
	{ '9', {0b01110,0b10001,0b10001,0b01111,0b00001,0b00010,0b01100} },
	{ 'A', {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001} },
	{ 'B', {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110} },
	{ 'C', {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110} },
	{ 'D', {0b11100,0b10010,0b10001,0b10001,0b10001,0b10010,0b11100} },
	{ 'E', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111} },
	{ 'F', {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000} },
	{ 'G', {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01111} },
	{ 'H', {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001} },
	{ 'I', {0b01110,0b00100,0b00100,0b00100,0b00100,0b00100,0b01110} },
	{ 'J', {0b00111,0b00010,0b00010,0b00010,0b00010,0b10010,0b01100} },
	{ 'K', {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001} },
	{ 'L', {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111} },
	{ 'M', {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001} },
	{ 'N', {0b10001,0b10001,0b11001,0b10101,0b10011,0b10001,0b10001} },
	{ 'O', {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110} },
	{ 'P', {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000} },
	{ 'Q', {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101} },
	{ 'R', {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001} },
	{ 'S', {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110} },
	{ 'T', {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100} },
	{ 'U', {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110} },
	{ 'V', {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100} },
	{ 'W', {0b10001,0b10001,0b10001,0b10101,0b10101,0b10101,0b01010} },
	{ 'X', {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001} },
	{ 'Y', {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100} },
	{ 'Z', {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111} },
	// Mantido em sincronia com Credits.cpp. Ver o comentario de la.
	{ (char)0xD5, {0b01110,0b00000,0b01110,0b10001,0b10001,0b10001,0b01110} },
	{ (char)0xC7, {0b01110,0b10001,0b10000,0b10000,0b01110,0b00100,0b01000} },
	// As quatro setas do D-pad. Mantido em sincronia com Credits.cpp. O seletor
	// de fitas nunca emite esses bytes (toDisplay so devolve ASCII ou '?'), mas
	// as duas tabelas precisam continuar identicas.
	{ (char)0x80, {0b00000,0b00100,0b01000,0b11111,0b01000,0b00100,0b00000} },
	{ (char)0x81, {0b00000,0b00100,0b00010,0b11111,0b00010,0b00100,0b00000} },
	{ (char)0x82, {0b00000,0b00100,0b00100,0b00100,0b01110,0b01000,0b00000} },
	{ (char)0x83, {0b00000,0b01000,0b11110,0b00100,0b00100,0b00100,0b00000} },
	{ ':', {0b00000,0b00100,0b00100,0b00000,0b00100,0b00100,0b00000} },
	{ ',', {0b00000,0b00000,0b00000,0b00000,0b00110,0b00100,0b01000} },
	{ '.', {0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100} },
	{ '?', {0b01110,0b10001,0b00001,0b00010,0b00100,0b00000,0b00100} },
	{ '!', {0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100} },
	{ '_', {0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11111} },
	{ '-', {0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000} },
	{ '+', {0b00000,0b00100,0b00100,0b11111,0b00100,0b00100,0b00000} },
	{ '=', {0b00000,0b00000,0b11111,0b00000,0b11111,0b00000,0b00000} },
	{ '*', {0b00000,0b00100,0b10101,0b01110,0b10101,0b00100,0b00000} },
	{ '/', {0b00001,0b00001,0b00010,0b00100,0b01000,0b10000,0b10000} },
	{ '(', {0b00010,0b00100,0b01000,0b01000,0b01000,0b00100,0b00010} },
	{ ')', {0b01000,0b00100,0b00010,0b00010,0b00010,0b00100,0b01000} },
	{ '[', {0b01110,0b01000,0b01000,0b01000,0b01000,0b01000,0b01110} },
	{ ']', {0b01110,0b00010,0b00010,0b00010,0b00010,0b00010,0b01110} },
	{ '<', {0b00010,0b00100,0b01000,0b10000,0b01000,0b00100,0b00010} },
	{ '>', {0b01000,0b00100,0b00010,0b00001,0b00010,0b00100,0b01000} },
	{ '\'', {0b00100,0b00100,0b01000,0b00000,0b00000,0b00000,0b00000} },
	{ '&', {0b01100,0b10010,0b10100,0b01000,0b10101,0b10010,0b01101} },
	{ '#', {0b01010,0b01010,0b11111,0b01010,0b11111,0b01010,0b01010} },
	{ '\\', {0b10000,0b10000,0b01000,0b00100,0b00010,0b00001,0b00001} },
	{ '{', {0b00110,0b00100,0b00100,0b01000,0b00100,0b00100,0b00110} },
	{ '}', {0b01100,0b00100,0b00100,0b00010,0b00100,0b00100,0b01100} },
	{ '~', {0b00000,0b00000,0b01000,0b10101,0b00010,0b00000,0b00000} },
	{ '%', {0b11001,0b11010,0b00010,0b00100,0b01000,0b01011,0b10011} },
	{ ';', {0b00000,0b01100,0b01100,0b00000,0b01100,0b00100,0b01000} },
	{ '@', {0b01110,0b10001,0b10111,0b10101,0b10111,0b10000,0b01110} },
	{ '$', {0b00100,0b01111,0b10100,0b01110,0b00101,0b11110,0b00100} },
	{ '|', {0b00100,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100} },
	{ '^', {0b00100,0b01010,0b10001,0b00000,0b00000,0b00000,0b00000} },
	{ '`', {0b01000,0b00100,0b00010,0b00000,0b00000,0b00000,0b00000} },
	{ '"', {0b01010,0b01010,0b01010,0b00000,0b00000,0b00000,0b00000} },
};
constexpr int kFontCount = sizeof(kFont) / sizeof(kFont[0]);

const byte* findGlyph(char ch) {
	for (int i = 0; i < kFontCount; i++) {
		if (kFont[i].ch == ch) {
			return kFont[i].rows;
		}
	}
	return nullptr;
}

inline void setPixel(SRGB* fb, int x, int y, SRGB c) {
	if (x < 0 || x >= VIDEOWIDTH || y < 0 || y >= VIDEOHEIGHT) {
		return;
	}
	fb[y * VIDEOWIDTH + x] = c;
}

void fillRect(SRGB* fb, int x0, int y0, int w, int h, SRGB c) {
	for (int y = y0; y < y0 + h; y++) {
		for (int x = x0; x < x0 + w; x++) {
			setPixel(fb, x, y, c);
		}
	}
}

// Converts a file name to plain ASCII for display: accented Latin letters
// lose their accent (e.g. A-acute -> A) so Portuguese names stay readable;
// anything else unknown becomes '?'. Accepts both UTF-8 (Linux/macOS) and
// single-byte Latin-1/ANSI (what Windows' readdir() returns).
std::string toDisplay(const std::string& in) {
	// Base letters for U+00C0..U+00FF.
	static const char kFold[] =
		"AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
		"aaaaaaaceeeeiiiidnooooo/ouuuuyty";
	auto isCont = [&](size_t i) { return i < in.size() && ((unsigned char)in[i] & 0xC0) == 0x80; };
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		const unsigned char c = (unsigned char)in[i];
		if (c < 0x80) {
			out.push_back((char)c);
		} else if (c == 0xC3 && isCont(i + 1)) {
			out.push_back(kFold[(unsigned char)in[i + 1] - 0x80]);
			i++;
		} else if (c >= 0xC2 && c <= 0xF4 && isCont(i + 1)) {
			out.push_back('?');	// some other UTF-8 character: one '?' for all its bytes
			while (isCont(i + 1)) i++;
		} else if (c >= 0xC0) {
			out.push_back(kFold[c - 0xC0]);	// Latin-1 letter
		} else {
			out.push_back('?');
		}
	}
	return out;
}

// Upper-cases everything (the font has no lowercase). Bytes >= 0x80 (UTF-8
// accents etc.) and other unknown characters are drawn as '?'.
void drawText(SRGB* fb, int x, int y, const std::string& text, SRGB color) {
	for (const char raw : text) {
		const char ch = (char)std::toupper((unsigned char)raw);
		if (ch != ' ') {
			const byte* rows = findGlyph(ch);
			if (!rows) {
				rows = findGlyph('?');
			}
			for (int gy = 0; gy < 7; gy++) {
				for (int gx = 0; gx < 5; gx++) {
					if (rows[gy] & (1 << (4 - gx))) {
						setPixel(fb, x + gx, y + gy, color);
					}
				}
			}
		}
		x += kCharW;
	}
}

int textWidth(const std::string& text) {
	return text.empty() ? 0 : (int)text.size() * kCharW - 1;
}

// Keeps the END of a long string (the interesting part of a path).
std::string fitLeft(const std::string& s, int maxChars) {
	if ((int)s.size() <= maxChars) {
		return s;
	}
	return ".." + s.substr(s.size() - (size_t)(maxChars - 2));
}

// Keeps the START of a long string (a file name).
std::string fitRight(const std::string& s, int maxChars) {
	if ((int)s.size() <= maxChars) {
		return s;
	}
	return s.substr(0, (size_t)(maxChars - 2)) + "..";
}

inline bool isSep(char c) { return c == '/' || c == '\\'; }

std::string joinPath(const std::string& dir, const std::string& name) {
	if (dir.empty()) {
		return name;
	}
	if (isSep(dir.back())) {
		return dir + name;
	}
	const char sep = (dir.find('\\') != std::string::npos && dir.find('/') == std::string::npos) ? '\\' : '/';
	return dir + sep + name;
}

// Parent of a directory path; returns the input unchanged at a root.
std::string parentOf(std::string p) {
	while (p.size() > 1 && isSep(p.back()) && !(p.size() == 3 && p[1] == ':')) {
		p.pop_back();
	}
	const size_t pos = p.find_last_of("/\\");
	if (pos == std::string::npos) {
		return p;
	}
	if (pos == 0) {
		return p.substr(0, 1);
	}
	if (pos == 2 && p[1] == ':') {
		return p.substr(0, 3);	// "C:\"
	}
	if (pos == p.size() - 1) {
		return p;	// already a root such as "C:\"
	}
	return p.substr(0, pos);
}

std::string lastComponent(std::string p) {
	while (p.size() > 1 && isSep(p.back())) {
		p.pop_back();
	}
	const size_t pos = p.find_last_of("/\\");
	return pos == std::string::npos ? p : p.substr(pos + 1);
}

bool hasExt(const std::string& name, const char* ext3) {
	if (name.size() < 5) {
		return false;
	}
	const char* s = name.c_str() + name.size() - 4;
	return s[0] == '.' && std::tolower((unsigned char)s[1]) == ext3[0] &&
		std::tolower((unsigned char)s[2]) == ext3[1] && std::tolower((unsigned char)s[3]) == ext3[2];
}

// Tapes come as raw .ct2 or zipped (.zip holding a .ct2).
bool isTapeFile(const std::string& name) {
	return hasExt(name, "ct2") || hasExt(name, "zip");
}

// File name without directory and without the extension, lower-cased --
// used to recognise "the same game" even when one copy is the .zip in
// roms/tk2000 and the other is the .ct2 the frontend extracted to %TEMP%.
std::string stemOf(const std::string& path) {
	std::string n = lastComponent(path);
	const size_t dot = n.find_last_of('.');
	if (dot != std::string::npos) {
		n.erase(dot);
	}
	for (char& c : n) {
		c = (char)std::tolower((unsigned char)c);
	}
	return n;
}

// Case-insensitive "natural" ordering: "disco 2" < "disco 10".
bool naturalLess(const std::string& a, const std::string& b) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		if (std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
			size_t ie = i, je = j;
			while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ie++;
			while (je < b.size() && std::isdigit((unsigned char)b[je])) je++;
			size_t is = i, js = j;
			while (is + 1 < ie && a[is] == '0') is++;
			while (js + 1 < je && b[js] == '0') js++;
			const size_t la = ie - is, lb = je - js;
			if (la != lb) {
				return la < lb;
			}
			const int c = a.compare(is, la, b, js, lb);
			if (c != 0) {
				return c < 0;
			}
			i = ie;
			j = je;
		} else {
			const int ca = std::tolower((unsigned char)a[i]);
			const int cb = std::tolower((unsigned char)b[j]);
			if (ca != cb) {
				return ca < cb;
			}
			i++;
			j++;
		}
	}
	return (a.size() - i) < (b.size() - j);
}

bool isDirectory(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return false;
	}
	return (st.st_mode & S_IFMT) == S_IFDIR;
}

// Hold-to-repeat: fires on the first frame, then after a delay every few frames.
bool repeatFire(bool held, int& counter) {
	if (!held) {
		counter = 0;
		return false;
	}
	counter++;
	if (counter == 1) {
		return true;
	}
	constexpr int kDelay = 18, kInterval = 4;
	return counter > kDelay && ((counter - kDelay) % kInterval) == 0;
}

}	// namespace

/*************************************************************************************************/
// Looks for "<base>/roms/tk2000" in each base and in up to 6 of its parents
// (so it works whether the core sits in <app>/cores, the exe in <app>, ...).
std::string CFilePicker::findRomsDir(const std::vector<std::string>& bases) {
	static const char* kSubdirs[] = { "roms/tk2000", "roms/TK2000", "ROMS/TK2000", "Roms/TK2000", "Roms/tk2000" };
	for (const std::string& start : bases) {
		std::string base = start;
		for (int level = 0; level < 7 && !base.empty(); level++) {
			for (const char* sub : kSubdirs) {
				const std::string cand = joinPath(base, sub);
				if (isDirectory(cand)) {
					return cand;
				}
			}
			const std::string up = parentOf(base);
			if (up == base) {
				break;
			}
			base = up;
		}
	}
	return std::string();
}

/*************************************************************************************************/
void CFilePicker::open(const std::string& currentImage, const std::string& preferredDir) {
	mOpen = true;
	mChosen.clear();
	mCurrentImage = currentImage;
	mCurrentStem = currentImage.empty() ? std::string() : stemOf(currentImage);

	// Start folder: the ROMs folder if it was found, else the folder of the
	// loaded tape, else the working directory.
	std::string dir = preferredDir;
	if (dir.empty() || !isDirectory(dir)) {
		dir.clear();
		const size_t pos = currentImage.find_last_of("/\\");
		if (!currentImage.empty() && pos != std::string::npos) {
			dir = (pos == 0) ? currentImage.substr(0, 1)
				: (pos == 2 && currentImage[1] == ':') ? currentImage.substr(0, 3)
				: currentImage.substr(0, pos);
		} else {
			char buf[1024];
			dir = TK_GETCWD(buf, sizeof(buf)) ? buf : "/";
		}
	}
	loadDir(dir, "");

	// Start on the tape AFTER the one that is currently loaded: opening the
	// picker almost always means "I need the next side". Matching is by name
	// without extension, so "Karateka (disco 1).ct2" (extracted to %TEMP%)
	// finds "Karateka (disco 1).zip" here.
	if (!mCurrentStem.empty()) {
		for (int i = 0; i < (int)mEntries.size(); i++) {
			if (!mEntries[i].isDir && stemOf(mEntries[i].name) == mCurrentStem) {
				if (i + 1 < (int)mEntries.size() && !mEntries[i + 1].isDir) {
					mCursor = i + 1;
				} else {
					mCursor = i;
				}
				mScroll = std::max(0, mCursor - kRows / 2);
				keepCursorVisible();
				break;
			}
		}
	}

	// Buttons that are still held from opening the window must be released
	// before they count as a new press.
	mPrevOk = true;
	mPrevCancel = true;
	mHoldUp = mHoldDown = mHoldLeft = mHoldRight = 1;
}

/*************************************************************************************************/
bool CFilePicker::loadDir(const std::string& dir, const std::string& selectName) {
	mDir = dir;
	mEntries.clear();
	mReadError = false;

	if (parentOf(dir) != dir) {
		mEntries.push_back({ "..", true });
	}
	const size_t firstReal = mEntries.size();

	if (DIR* d = opendir(dir.c_str())) {
		std::vector<SEntry> dirs, files;
		while (const dirent* e = readdir(d)) {
			const std::string n = e->d_name;
			if (n.empty() || n[0] == '.') {
				continue;	// ".", ".." and hidden entries
			}
			if (isDirectory(joinPath(dir, n))) {
				dirs.push_back({ n, true });
			} else if (isTapeFile(n)) {
				files.push_back({ n, false });
			}
		}
		closedir(d);
		auto cmp = [](const SEntry& a, const SEntry& b) { return naturalLess(a.name, b.name); };
		std::sort(dirs.begin(), dirs.end(), cmp);
		std::sort(files.begin(), files.end(), cmp);
		mEntries.insert(mEntries.end(), dirs.begin(), dirs.end());
		mEntries.insert(mEntries.end(), files.begin(), files.end());
	} else {
		mReadError = true;
	}

	// Cursor: the requested name if present, else the first tape, else the top.
	bool matched = false;
	mCursor = 0;
	for (size_t i = 0; i < mEntries.size(); i++) {
		if (!selectName.empty() && mEntries[i].name == selectName) {
			mCursor = (int)i;
			matched = true;
			break;
		}
	}
	if (!matched) {
		for (size_t i = firstReal; i < mEntries.size(); i++) {
			if (!mEntries[i].isDir) {
				mCursor = (int)i;
				break;
			}
		}
	}
	mScroll = std::max(0, mCursor - kRows / 2);
	keepCursorVisible();
	return matched;
}

/*************************************************************************************************/
void CFilePicker::keepCursorVisible() {
	const int n = (int)mEntries.size();
	if (mCursor < mScroll) {
		mScroll = mCursor;
	}
	if (mCursor >= mScroll + kRows) {
		mScroll = mCursor - kRows + 1;
	}
	mScroll = std::max(0, std::min(mScroll, std::max(0, n - kRows)));
}

/*************************************************************************************************/
void CFilePicker::move(int delta) {
	const int n = (int)mEntries.size();
	if (n == 0) {
		return;
	}
	if (delta == 1 || delta == -1) {
		mCursor = (mCursor + delta + n) % n;	// wraps around
	} else {
		mCursor = std::max(0, std::min(n - 1, mCursor + delta));
	}
	keepCursorVisible();
}

/*************************************************************************************************/
bool CFilePicker::activate() {
	if (mCursor < 0 || mCursor >= (int)mEntries.size()) {
		return false;
	}
	const SEntry e = mEntries[mCursor];
	if (!e.isDir) {
		mChosen = joinPath(mDir, e.name);
		return true;
	}
	if (e.name == "..") {
		const std::string cameFrom = lastComponent(mDir);
		loadDir(parentOf(mDir), cameFrom);
	} else {
		loadDir(joinPath(mDir, e.name), "");
	}
	return false;
}

/*************************************************************************************************/
CFilePicker::EResult CFilePicker::update(const SInput& in) {
	if (!mOpen) {
		return EResult::None;
	}
	if (repeatFire(in.up, mHoldUp)) move(-1);
	if (repeatFire(in.down, mHoldDown)) move(1);
	if (repeatFire(in.left, mHoldLeft)) move(-kPage);
	if (repeatFire(in.right, mHoldRight)) move(kPage);

	const bool okEdge = in.ok && !mPrevOk;
	const bool cancelEdge = in.cancel && !mPrevCancel;
	mPrevOk = in.ok;
	mPrevCancel = in.cancel;

	if (cancelEdge) {
		return EResult::Cancelled;
	}
	if (okEdge && activate()) {
		return EResult::Chosen;
	}
	return EResult::None;
}

/*************************************************************************************************/
void CFilePicker::draw(SRGB* fb) const {
	if (!mOpen) {
		return;
	}
	// Frame + background + title bar.
	fillRect(fb, kPanelX - 1, kPanelY - 1, kPanelW + 2, kPanelH + 2, kBorder);
	fillRect(fb, kPanelX, kPanelY, kPanelW, kPanelH, kBg);
	fillRect(fb, kPanelX, kPanelY, kPanelW, kTitleH, kTitleBg);
	const std::string title = "ESCOLHA O DISCO 2 (.CT2)";
	drawText(fb, kPanelX + (kPanelW - textWidth(title)) / 2, kPanelY + 2, title, kText);

	// Current folder (the end of the path is what matters, so cut the front).
	drawText(fb, kListX, kPathY, fitLeft(toDisplay(mDir), (kPanelW - 10) / kCharW), kPathText);
	fillRect(fb, kPanelX + 3, kListY - 4, kPanelW - 6, 1, kTrack);

	// List.
	const int n = (int)mEntries.size();
	bool anyTape = false;
	for (int i = 0; i < kRows && mScroll + i < n; i++) {
		const int idx = mScroll + i;
		const SEntry& e = mEntries[idx];
		const int y = kListY + i * kRowH;
		const bool selected = (idx == mCursor);
		const bool isCurrent = !e.isDir && !mCurrentStem.empty() && stemOf(e.name) == mCurrentStem;

		std::string label = toDisplay(e.isDir ? e.name + "/" : e.name);
		label = std::string(isCurrent ? "*" : " ") + " " + label;
		label = fitRight(label, kMaxChars);

		if (selected) {
			fillRect(fb, kListX - 2, y - 1, kListW + 2, kRowH, kSelBg);
		}
		drawText(fb, kListX, y, label, selected ? kSelText : (e.isDir ? kDirText : kText));
	}
	for (const SEntry& e : mEntries) {
		anyTape = anyTape || !e.isDir;
	}
	if (!anyTape) {
		const int row = std::min(n - mScroll, kRows - 1);
		drawText(fb, kListX, kListY + std::max(0, row) * kRowH + 3,
			mReadError ? "ERRO AO LER A PASTA" : "NENHUMA FITA (.CT2/.ZIP) AQUI", kDimText);
	}

	// Scrollbar (only when the list doesn't fit).
	if (n > kRows) {
		const int trackX = kPanelX + kPanelW - 6;
		const int trackH = kRows * kRowH;
		fillRect(fb, trackX, kListY - 1, 3, trackH, kTrack);
		const int thumbH = std::max(6, trackH * kRows / n);
		const int thumbY = kListY - 1 + (trackH - thumbH) * mScroll / std::max(1, n - kRows);
		fillRect(fb, trackX, thumbY, 3, thumbH, kThumb);
	}

	// Footer.
	const int footY = kPanelY + kPanelH - kFooterH;
	fillRect(fb, kPanelX + 3, footY, kPanelW - 6, 1, kTrack);
	const std::string help = "A:OK  B/SEL:SAIR  L/R:PAG";
	drawText(fb, kPanelX + (kPanelW - textWidth(help)) / 2, footY + 3, help, kDimText);
}
