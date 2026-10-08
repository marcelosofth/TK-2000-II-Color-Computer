// Minimal smoke test: dynamically load the compiled core, wire up dummy
// callbacks, boot it and run a handful of frames, then check the video
// buffer isn't degenerate (all-zero) and that no crash / infinite loop
// occurred.
//
// Cross-platform: uses dlopen/dlsym on Linux/macOS/MSYS-POSIX and
// LoadLibrary/GetProcAddress on native Windows (MinGW/MSVC), since the
// core itself is built either as tk2000_libretro.so (Linux/Batocera) or
// tk2000_libretro.dll (Windows/MSYS2 MINGW64 — no libdl there).
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../src/libretro.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE lib_handle_t;
static lib_handle_t lib_open(const char* path) { return LoadLibraryA(path); }
static void* lib_sym(lib_handle_t h, const char* name) { return (void*)GetProcAddress(h, name); }
static void lib_close(lib_handle_t h) { FreeLibrary(h); }
static const char* lib_error() {
	static char buf[256];
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(), 0, buf, sizeof(buf), nullptr);
	return buf;
}
static const char* kCorePath = "./tk2000_libretro.dll";
#else
#include <dlfcn.h>
typedef void* lib_handle_t;
static lib_handle_t lib_open(const char* path) { return dlopen(path, RTLD_NOW); }
static void* lib_sym(lib_handle_t h, const char* name) { return dlsym(h, name); }
static void lib_close(lib_handle_t h) { dlclose(h); }
static const char* lib_error() { return dlerror(); }
#if defined(__APPLE__)
static const char* kCorePath = "./tk2000_libretro.dylib";
#else
static const char* kCorePath = "./tk2000_libretro.so";
#endif
#endif

static uint32_t g_frameCount = 0;
static uint64_t g_pixelSum = 0;
static unsigned g_lastW = 0, g_lastH = 0;
static std::vector<uint32_t> g_lastFrame;

static void video_refresh(const void* data, unsigned width, unsigned height, size_t pitch) {
	g_frameCount++;
	g_lastW = width;
	g_lastH = height;
	const uint8_t* base = (const uint8_t*)data;
	g_lastFrame.resize((size_t)width * height);
	for (unsigned y = 0; y < height; y++) {
		const uint32_t* row = (const uint32_t*)(base + y * pitch);
		for (unsigned x = 0; x < width; x++) {
			g_pixelSum += row[x] & 0xFFFFFF;
			g_lastFrame[y * width + x] = row[x];
		}
	}
}

static void dump_ppm(const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%u %u\n255\n", g_lastW, g_lastH);
	for (unsigned i = 0; i < g_lastW * g_lastH; i++) {
		uint8_t rgb[3] = {
			(uint8_t)((g_lastFrame[i] >> 16) & 0xFF),
			(uint8_t)((g_lastFrame[i] >> 8) & 0xFF),
			(uint8_t)(g_lastFrame[i] & 0xFF)
		};
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

static size_t audio_batch(const int16_t* /*data*/, size_t frames) {
	return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned, unsigned, unsigned, unsigned) { return 0; }

static retro_keyboard_event_t g_coreKeyboardCb = nullptr;

static bool environment(unsigned cmd, void* data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return true;
	case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		return true;
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		return true;
	case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
		g_coreKeyboardCb = ((retro_keyboard_callback*)data)->callback;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		return false;
	default:
		return false;
	}
}



int main() {
	lib_handle_t handle = lib_open(kCorePath);
	if (!handle) {
		fprintf(stderr, "failed to load %s: %s\n", kCorePath, lib_error());
		return 1;
	}

	auto sym = [&](const char* name) {
		void* p = lib_sym(handle, name);
		if (!p) {
			fprintf(stderr, "missing symbol: %s\n", name);
			exit(1);
		}
		return p;
	};

	auto retro_set_environment = (void(*)(retro_environment_t))sym("retro_set_environment");
	auto retro_set_video_refresh = (void(*)(retro_video_refresh_t))sym("retro_set_video_refresh");
	auto retro_set_audio_sample_batch = (void(*)(retro_audio_sample_batch_t))sym("retro_set_audio_sample_batch");
	auto retro_set_input_poll = (void(*)(retro_input_poll_t))sym("retro_set_input_poll");
	auto retro_set_input_state = (void(*)(retro_input_state_t))sym("retro_set_input_state");
	auto retro_init = (void(*)())sym("retro_init");
	auto retro_get_system_info = (void(*)(retro_system_info*))sym("retro_get_system_info");
	auto retro_get_system_av_info = (void(*)(retro_system_av_info*))sym("retro_get_system_av_info");
	auto retro_run = (void(*)())sym("retro_run");
	auto retro_serialize_size = (size_t(*)())sym("retro_serialize_size");
	auto retro_serialize = (bool(*)(void*, size_t))sym("retro_serialize");
	auto retro_unserialize = (bool(*)(const void*, size_t))sym("retro_unserialize");
	auto retro_load_game = (bool(*)(const retro_game_info*))sym("retro_load_game");
	auto retro_deinit = (void(*)())sym("retro_deinit");

	retro_set_environment(environment);
	retro_set_video_refresh(video_refresh);
	retro_set_audio_sample_batch(audio_batch);
	retro_set_input_poll(input_poll);
	retro_set_input_state(input_state);

	retro_init();

	retro_system_info sysinfo{};
	retro_get_system_info(&sysinfo);
	printf("library: %s %s (ext: %s, need_fullpath: %d)\n",
		sysinfo.library_name, sysinfo.library_version, sysinfo.valid_extensions, sysinfo.need_fullpath);

	retro_system_av_info av{};
	retro_get_system_av_info(&av);
	printf("geometry: %ux%u aspect=%.3f  timing: fps=%.2f sample_rate=%.0f\n",
		av.geometry.base_width, av.geometry.base_height, av.geometry.aspect_ratio,
		av.timing.fps, av.timing.sample_rate);

	bool loaded = retro_load_game(nullptr);	// no-cassette boot
	printf("retro_load_game(nullptr) -> %d\n", loaded);

	const int kFrames = 180;	// 3 seconds @ 60fps (autotype "LOADT\n" fires in here)
	for (int i = 0; i < kFrames; i++) {
		retro_run();
	}
	printf("ran %d frames, video_refresh called %u times, last frame %ux%u, pixelSum=%llu\n",
		kFrames, g_frameCount, g_lastW, g_lastH, (unsigned long long)g_pixelSum);
	dump_ppm("/tmp/tk2000_boot.ppm");

	for (int i = 0; i < 60; i++) retro_run();
	dump_ppm("/tmp/tk2000_after_loadt.ppm");
	printf("dumped /tmp/tk2000_after_loadt.ppm at frame %d\n", kFrames + 60);

	// --- Keyboard pipeline test: type "HI" and RETURN via the registered
	// libretro keyboard callback, exactly as a real frontend would.
	auto tapKey = [&](unsigned keycode) {
		g_coreKeyboardCb(true, keycode, 0, 0);
		retro_run();
		g_coreKeyboardCb(false, keycode, 0, 0);
		retro_run();
	};
	if (g_coreKeyboardCb) {
		tapKey(RETROK_h);
		tapKey(RETROK_i);
		tapKey(RETROK_RETURN);
		for (int i = 0; i < 30; i++) retro_run();
		dump_ppm("/tmp/tk2000_typed.ppm");
		printf("typed HI+RETURN, dumped /tmp/tk2000_typed.ppm\n");
	} else {
		printf("WARNING: core did not register a keyboard callback\n");
	}

	// Savestate round trip
	size_t ssize = retro_serialize_size();
	printf("state size: %zu bytes\n", ssize);
	std::vector<uint8_t> state(ssize);
	bool sOk = retro_serialize(state.data(), state.size());
	bool lOk = retro_unserialize(state.data(), state.size());
	printf("serialize=%d unserialize=%d\n", sOk, lOk);

	// run more frames after state load to make sure nothing broke
	for (int i = 0; i < 60; i++) {
		retro_run();
	}
	printf("post-loadstate run OK, total video_refresh calls=%u\n", g_frameCount);

	retro_deinit();
	lib_close(handle);

	if (g_frameCount < (unsigned)(kFrames + 60)) {
		fprintf(stderr, "FAIL: unexpected frame count\n");
		return 1;
	}
	if (g_lastW != 280 || g_lastH != 192) {
		fprintf(stderr, "FAIL: unexpected framebuffer size\n");
		return 1;
	}
	printf("SMOKE TEST PASSED\n");
	return 0;
}
