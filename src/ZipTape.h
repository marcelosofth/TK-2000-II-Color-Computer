// Minimal .zip reader: extracts the first .ct2 inside a zip archive (stored
// or deflated entries; no encryption, no zip64) so tapes can be picked
// straight from a folder of zipped ROMs.
#pragma once
#include <string>
#include <vector>

// Reads `zipPath` and puts the bytes of its first *.ct2 entry in `out`.
bool zipExtractFirstCt2(const char* zipPath, std::vector<unsigned char>& out);
