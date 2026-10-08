// Deflate decoder follows the structure of zlib's public-domain "puff.c"
// (Mark Adler): canonical Huffman decoding, one bit at a time.

#include "ZipTape.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {

typedef unsigned char u8;

struct SInflate {
	const u8* in;
	size_t inLen;
	size_t inPos;
	unsigned bitBuf;
	int bitCnt;
	bool overrun;
	std::vector<u8>* out;
	size_t maxOut;
};

struct SHuff {
	short count[16];
	short symbol[288];
};

int getBits(SInflate& s, int need) {
	long val = s.bitBuf;
	while (s.bitCnt < need) {
		if (s.inPos >= s.inLen) {
			s.overrun = true;
			return 0;
		}
		val |= (long)s.in[s.inPos++] << s.bitCnt;
		s.bitCnt += 8;
	}
	s.bitBuf = (unsigned)(val >> need);
	s.bitCnt -= need;
	return (int)(val & ((1L << need) - 1));
}

// Returns 0 for a complete code, >0 incomplete, <0 over-subscribed.
int construct(SHuff& h, const short* length, int n) {
	for (int len = 0; len <= 15; len++) h.count[len] = 0;
	for (int sym = 0; sym < n; sym++) h.count[length[sym]]++;
	if (h.count[0] == n) return 0;
	int left = 1;
	for (int len = 1; len <= 15; len++) {
		left <<= 1;
		left -= h.count[len];
		if (left < 0) return left;
	}
	short offs[16];
	offs[1] = 0;
	for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
	for (int sym = 0; sym < n; sym++) {
		if (length[sym] != 0) h.symbol[offs[length[sym]]++] = (short)sym;
	}
	return left;
}

int decodeSym(SInflate& s, const SHuff& h) {
	int code = 0, first = 0, index = 0;
	for (int len = 1; len <= 15; len++) {
		code |= getBits(s, 1);
		if (s.overrun) return -1;
		const int count = h.count[len];
		if (code - count < first) return h.symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

const short kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const short kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const short kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const short kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

bool codes(SInflate& s, const SHuff& lencode, const SHuff& distcode) {
	for (;;) {
		int sym = decodeSym(s, lencode);
		if (sym < 0) return false;
		if (sym < 256) {
			if (s.out->size() >= s.maxOut) return false;
			s.out->push_back((u8)sym);
		} else if (sym == 256) {
			return true;
		} else {
			sym -= 257;
			if (sym >= 29) return false;
			const int len = kLenBase[sym] + getBits(s, kLenExtra[sym]);
			const int ds = decodeSym(s, distcode);
			if (ds < 0 || ds >= 30) return false;
			const size_t dist = (size_t)kDistBase[ds] + getBits(s, kDistExtra[ds]);
			if (s.overrun || dist > s.out->size() || s.out->size() + len > s.maxOut) return false;
			for (int i = 0; i < len; i++) {
				s.out->push_back((*s.out)[s.out->size() - dist]);
			}
		}
	}
}

bool inflateData(const u8* in, size_t inLen, std::vector<u8>& out, size_t maxOut) {
	SInflate s{ in, inLen, 0, 0, 0, false, &out, maxOut };
	int last;
	do {
		last = getBits(s, 1);
		const int type = getBits(s, 2);
		if (s.overrun) return false;
		if (type == 0) {
			s.bitBuf = 0;
			s.bitCnt = 0;
			if (s.inPos + 4 > s.inLen) return false;
			const unsigned len = in[s.inPos] | (in[s.inPos + 1] << 8);
			const unsigned nlen = in[s.inPos + 2] | (in[s.inPos + 3] << 8);
			s.inPos += 4;
			if (len != (~nlen & 0xFFFF) || s.inPos + len > s.inLen || out.size() + len > maxOut) return false;
			out.insert(out.end(), in + s.inPos, in + s.inPos + len);
			s.inPos += len;
		} else if (type == 1) {
			SHuff lencode, distcode;
			short lengths[288];
			int sym = 0;
			for (; sym < 144; sym++) lengths[sym] = 8;
			for (; sym < 256; sym++) lengths[sym] = 9;
			for (; sym < 280; sym++) lengths[sym] = 7;
			for (; sym < 288; sym++) lengths[sym] = 8;
			construct(lencode, lengths, 288);
			for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
			construct(distcode, lengths, 30);
			if (!codes(s, lencode, distcode)) return false;
		} else if (type == 2) {
			static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
			short lengths[320];
			const int nlen = getBits(s, 5) + 257;
			const int ndist = getBits(s, 5) + 1;
			const int ncode = getBits(s, 4) + 4;
			if (s.overrun || nlen > 286 || ndist > 30) return false;
			int index = 0;
			for (; index < ncode; index++) lengths[order[index]] = (short)getBits(s, 3);
			for (; index < 19; index++) lengths[order[index]] = 0;
			SHuff lencode, distcode;
			if (construct(lencode, lengths, 19) != 0) return false;
			index = 0;
			while (index < nlen + ndist) {
				int sym = decodeSym(s, lencode);
				if (sym < 0) return false;
				if (sym < 16) {
					lengths[index++] = (short)sym;
				} else {
					int len = 0, rep;
					if (sym == 16) {
						if (index == 0) return false;
						len = lengths[index - 1];
						rep = 3 + getBits(s, 2);
					} else if (sym == 17) {
						rep = 3 + getBits(s, 3);
					} else {
						rep = 11 + getBits(s, 7);
					}
					if (index + rep > nlen + ndist) return false;
					while (rep--) lengths[index++] = (short)len;
				}
			}
			if (lengths[256] == 0) return false;
			const int e1 = construct(lencode, lengths, nlen);
			if (e1 < 0 || (e1 > 0 && nlen - lencode.count[0] != 1)) return false;
			const int e2 = construct(distcode, lengths + nlen, ndist);
			if (e2 < 0 || (e2 > 0 && ndist - distcode.count[0] != 1)) return false;
			if (!codes(s, lencode, distcode)) return false;
		} else {
			return false;
		}
	} while (!last);
	return true;
}

unsigned rd16(const u8* p) { return p[0] | (p[1] << 8); }
unsigned rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

bool endsWithCt2(const std::string& n) {
	if (n.size() < 4) return false;
	const char* s = n.c_str() + n.size() - 4;
	return s[0] == '.' && tolower((u8)s[1]) == 'c' && tolower((u8)s[2]) == 't' && s[3] == '2';
}

}	// namespace

bool zipExtractFirstCt2(const char* zipPath, std::vector<unsigned char>& out) {
	FILE* f = fopen(zipPath, "rb");
	if (!f) return false;
	std::vector<u8> z;
	u8 buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		z.insert(z.end(), buf, buf + n);
		if (z.size() > (128u << 20)) break;	// sanity cap
	}
	fclose(f);
	if (z.size() < 22) return false;

	// End of central directory record (scan backwards, it may trail a comment).
	size_t eocd = std::string::npos;
	const size_t lo = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0;
	for (size_t i = z.size() - 22 + 1; i-- > lo;) {
		if (rd32(&z[i]) == 0x06054b50) { eocd = i; break; }
	}
	if (eocd == std::string::npos) return false;
	const unsigned entries = rd16(&z[eocd + 10]);
	size_t p = rd32(&z[eocd + 16]);

	for (unsigned e = 0; e < entries; e++) {
		if (p + 46 > z.size() || rd32(&z[p]) != 0x02014b50) return false;
		const unsigned flags = rd16(&z[p + 8]);
		const unsigned method = rd16(&z[p + 10]);
		const size_t csize = rd32(&z[p + 20]);
		const size_t usize = rd32(&z[p + 24]);
		const unsigned nameLen = rd16(&z[p + 28]);
		const unsigned extraLen = rd16(&z[p + 30]);
		const unsigned commentLen = rd16(&z[p + 32]);
		const size_t local = rd32(&z[p + 42]);
		if (p + 46 + nameLen > z.size()) return false;
		const std::string name((const char*)&z[p + 46], nameLen);
		p += 46 + nameLen + extraLen + commentLen;

		if (!endsWithCt2(name) || (flags & 1)) continue;
		if (local + 30 > z.size() || rd32(&z[local]) != 0x04034b50) return false;
		const size_t dataPos = local + 30 + rd16(&z[local + 26]) + rd16(&z[local + 28]);
		if (dataPos + csize > z.size() || usize > (64u << 20)) return false;

		out.clear();
		if (method == 0) {
			out.assign(&z[dataPos], &z[dataPos] + csize);
			return true;
		}
		if (method == 8) {
			out.reserve(usize);
			return inflateData(&z[dataPos], csize, out, usize + 1) && out.size() == usize;
		}
		return false;
	}
	return false;
}
