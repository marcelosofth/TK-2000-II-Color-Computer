#include "HostPaths.h"

#ifdef _WIN32
#include <windows.h>

static std::string dirOf(const char* path) {
	std::string p = path;
	const size_t pos = p.find_last_of("/\\");
	return pos == std::string::npos ? std::string() : p.substr(0, pos);
}

std::string hostExeDir() {
	char buf[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
	return (n > 0 && n < sizeof(buf)) ? dirOf(buf) : std::string();
}

std::string hostCoreDir() {
	HMODULE hm = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)&hostCoreDir, &hm)) {
		return std::string();
	}
	char buf[MAX_PATH * 2];
	const DWORD n = GetModuleFileNameA(hm, buf, (DWORD)sizeof(buf));
	return (n > 0 && n < sizeof(buf)) ? dirOf(buf) : std::string();
}

#else
#include <unistd.h>
#include <dlfcn.h>

std::string hostExeDir() {
	char buf[4096];
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n <= 0) {
		return std::string();
	}
	buf[n] = 0;
	std::string p = buf;
	const size_t pos = p.find_last_of('/');
	return pos == std::string::npos ? std::string() : p.substr(0, pos);
}

std::string hostCoreDir() {
	Dl_info info;
	if (dladdr((void*)&hostCoreDir, &info) && info.dli_fname) {
		std::string p = info.dli_fname;
		const size_t pos = p.find_last_of('/');
		return pos == std::string::npos ? std::string() : p.substr(0, pos);
	}
	return std::string();
}
#endif
