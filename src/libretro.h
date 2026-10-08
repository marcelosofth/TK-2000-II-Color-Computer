/* libretro.h -- trimmed subset for the tk2000 core.
 *
 * This is NOT the full official libretro.h (which is a much larger,
 * community-maintained header covering hardware rendering, camera,
 * sensors, achievements, disk-swap interfaces, memory maps, etc).
 * It only defines the symbols this core actually calls into or is
 * called through: the core entry points, the environment calls used
 * (pixel format, input descriptors, keyboard callback, variables,
 * no-game support, log interface), joypad/keyboard IDs, and the
 * av_info/game_info structures. All numeric values below match the
 * stable, long-frozen libretro API and are safe to link against a
 * real libretro frontend (e.g. RetroArch) — nothing here is invented,
 * it is simply a subset.
 *
 * If this core is later folded into a build alongside the *official*
 * libretro.h from the libretro-common repo, drop this file and use
 * that one instead; every symbol below is name-for-name compatible.
 */
#ifndef LIBRETRO_TK2000_H
#define LIBRETRO_TK2000_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RETRO_CALLCONV
#define RETRO_CALLCONV
#endif

#ifndef RETRO_API
#if defined(__CELLOS_LV2__) || defined(_WIN32)
#define RETRO_API RETRO_CALLCONV
#else
#define RETRO_API RETRO_CALLCONV __attribute__((visibility("default")))
#endif
#endif

#define RETRO_API_VERSION 1

/* ------------------------------------------------------------------ */
/* Device types                                                        */
#define RETRO_DEVICE_NONE     0
#define RETRO_DEVICE_JOYPAD   1
#define RETRO_DEVICE_KEYBOARD 3

#define RETRO_DEVICE_ID_JOYPAD_B      0
#define RETRO_DEVICE_ID_JOYPAD_Y      1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START  3
#define RETRO_DEVICE_ID_JOYPAD_UP     4
#define RETRO_DEVICE_ID_JOYPAD_DOWN   5
#define RETRO_DEVICE_ID_JOYPAD_LEFT   6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT  7
#define RETRO_DEVICE_ID_JOYPAD_A      8
#define RETRO_DEVICE_ID_JOYPAD_X      9
#define RETRO_DEVICE_ID_JOYPAD_L      10
#define RETRO_DEVICE_ID_JOYPAD_R      11
#define RETRO_DEVICE_ID_JOYPAD_L2     12
#define RETRO_DEVICE_ID_JOYPAD_R2     13

#define RETRO_REGION_NTSC 0
#define RETRO_REGION_PAL  1

/* ------------------------------------------------------------------ */
/* Environment calls used by this core                                 */
#define RETRO_ENVIRONMENT_SET_MESSAGE             6
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY    9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT       10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS  11
#define RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK  12
#define RETRO_ENVIRONMENT_GET_VARIABLE           15
#define RETRO_ENVIRONMENT_SET_VARIABLES          16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE    17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME    18
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY     31
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE      27

enum retro_pixel_format {
	RETRO_PIXEL_FORMAT_0RGB1555 = 0,
	RETRO_PIXEL_FORMAT_XRGB8888 = 1,
	RETRO_PIXEL_FORMAT_RGB565   = 2,
	RETRO_PIXEL_FORMAT_UNKNOWN  = INT32_MAX
};

/* ------------------------------------------------------------------ */
/* Core structures                                                     */
struct retro_message {
	const char *msg;
	unsigned frames;
};

struct retro_system_info {
	const char *library_name;
	const char *library_version;
	const char *valid_extensions;
	bool need_fullpath;
	bool block_extract;
};

struct retro_game_geometry {
	unsigned base_width;
	unsigned base_height;
	unsigned max_width;
	unsigned max_height;
	float aspect_ratio;
};

struct retro_system_timing {
	double fps;
	double sample_rate;
};

struct retro_system_av_info {
	struct retro_game_geometry geometry;
	struct retro_system_timing timing;
};

struct retro_game_info {
	const char *path;
	const void *data;
	size_t size;
	const char *meta;
};

struct retro_input_descriptor {
	unsigned port;
	unsigned device;
	unsigned index;
	unsigned id;
	const char *description;
};

struct retro_variable {
	const char *key;
	const char *value;
};

enum retro_log_level {
	RETRO_LOG_DEBUG = 0,
	RETRO_LOG_INFO,
	RETRO_LOG_WARN,
	RETRO_LOG_ERROR
};

typedef void (RETRO_CALLCONV *retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);

struct retro_log_callback {
	retro_log_printf_t log;
};

/* ------------------------------------------------------------------ */
/* Keyboard callback                                                   */
enum retro_mod {
	RETROKMOD_NONE     = 0x0000,
	RETROKMOD_SHIFT     = 0x01,
	RETROKMOD_CTRL      = 0x02,
	RETROKMOD_ALT       = 0x04,
	RETROKMOD_META      = 0x08,
	RETROKMOD_NUMLOCK   = 0x10,
	RETROKMOD_CAPSLOCK  = 0x20,
	RETROKMOD_SCROLLOCK = 0x40
};

/* Mirrors the classic SDL1.2 SDLK_* symbol table 1:1 (this is the
 * historical basis for retro_key, and is why porting SDL-keysym-based
 * core logic to libretro is a mechanical rename). */
enum retro_key {
	RETROK_UNKNOWN      = 0,
	RETROK_BACKSPACE    = 8,
	RETROK_TAB          = 9,
	RETROK_CLEAR        = 12,
	RETROK_RETURN       = 13,
	RETROK_PAUSE        = 19,
	RETROK_ESCAPE       = 27,
	RETROK_SPACE        = 32,
	RETROK_EXCLAIM      = 33,
	RETROK_QUOTEDBL     = 34,
	RETROK_HASH         = 35,
	RETROK_DOLLAR       = 36,
	RETROK_PERCENT      = 37,
	RETROK_AMPERSAND    = 38,
	RETROK_QUOTE        = 39,
	RETROK_LEFTPAREN    = 40,
	RETROK_RIGHTPAREN   = 41,
	RETROK_ASTERISK     = 42,
	RETROK_PLUS         = 43,
	RETROK_COMMA        = 44,
	RETROK_MINUS        = 45,
	RETROK_PERIOD       = 46,
	RETROK_SLASH        = 47,
	RETROK_0            = 48,
	RETROK_1            = 49,
	RETROK_2            = 50,
	RETROK_3            = 51,
	RETROK_4            = 52,
	RETROK_5            = 53,
	RETROK_6            = 54,
	RETROK_7            = 55,
	RETROK_8            = 56,
	RETROK_9            = 57,
	RETROK_COLON        = 58,
	RETROK_SEMICOLON    = 59,
	RETROK_LESS         = 60,
	RETROK_EQUALS       = 61,
	RETROK_GREATER      = 62,
	RETROK_QUESTION     = 63,
	RETROK_AT           = 64,

	RETROK_LEFTBRACKET  = 91,
	RETROK_BACKSLASH    = 92,
	RETROK_RIGHTBRACKET = 93,
	RETROK_CARET        = 94,
	RETROK_UNDERSCORE   = 95,
	RETROK_BACKQUOTE    = 96,
	RETROK_a            = 97,
	RETROK_b            = 98,
	RETROK_c            = 99,
	RETROK_d            = 100,
	RETROK_e            = 101,
	RETROK_f            = 102,
	RETROK_g            = 103,
	RETROK_h            = 104,
	RETROK_i            = 105,
	RETROK_j            = 106,
	RETROK_k            = 107,
	RETROK_l            = 108,
	RETROK_m            = 109,
	RETROK_n            = 110,
	RETROK_o            = 111,
	RETROK_p            = 112,
	RETROK_q            = 113,
	RETROK_r            = 114,
	RETROK_s            = 115,
	RETROK_t            = 116,
	RETROK_u            = 117,
	RETROK_v            = 118,
	RETROK_w            = 119,
	RETROK_x            = 120,
	RETROK_y            = 121,
	RETROK_z            = 122,
	RETROK_DELETE       = 127,

	RETROK_KP0          = 256,
	RETROK_KP1          = 257,
	RETROK_KP2          = 258,
	RETROK_KP3          = 259,
	RETROK_KP4          = 260,
	RETROK_KP5          = 261,
	RETROK_KP6          = 262,
	RETROK_KP7          = 263,
	RETROK_KP8          = 264,
	RETROK_KP9          = 265,
	RETROK_KP_PERIOD    = 266,
	RETROK_KP_DIVIDE    = 267,
	RETROK_KP_MULTIPLY  = 268,
	RETROK_KP_MINUS     = 269,
	RETROK_KP_PLUS      = 270,
	RETROK_KP_ENTER     = 271,
	RETROK_KP_EQUALS    = 272,

	RETROK_UP           = 273,
	RETROK_DOWN         = 274,
	RETROK_RIGHT        = 275,
	RETROK_LEFT         = 276,
	RETROK_INSERT       = 277,
	RETROK_HOME         = 278,
	RETROK_END          = 279,
	RETROK_PAGEUP       = 280,
	RETROK_PAGEDOWN     = 281,

	RETROK_F1           = 282,
	RETROK_F2           = 283,
	RETROK_F3           = 284,
	RETROK_F4           = 285,
	RETROK_F5           = 286,
	RETROK_F6           = 287,
	RETROK_F7           = 288,
	RETROK_F8           = 289,
	RETROK_F9           = 290,
	RETROK_F10          = 291,
	RETROK_F11          = 292,
	RETROK_F12          = 293,
	RETROK_F13          = 294,
	RETROK_F14          = 295,
	RETROK_F15          = 296,

	RETROK_NUMLOCK      = 300,
	RETROK_CAPSLOCK     = 301,
	RETROK_SCROLLOCK    = 302,
	RETROK_RSHIFT       = 303,
	RETROK_LSHIFT       = 304,
	RETROK_RCTRL        = 305,
	RETROK_LCTRL        = 306,
	RETROK_RALT         = 307,
	RETROK_LALT         = 308,
	RETROK_RMETA        = 309,
	RETROK_LMETA        = 310,
	RETROK_LSUPER       = 311,
	RETROK_RSUPER       = 312,
	RETROK_MODE         = 313,
	RETROK_COMPOSE      = 314,

	RETROK_HELP         = 315,
	RETROK_PRINT        = 316,
	RETROK_SYSREQ       = 317,
	RETROK_BREAK        = 318,
	RETROK_MENU         = 319,
	RETROK_POWER        = 320,
	RETROK_EURO         = 321,
	RETROK_UNDO         = 322,

	RETROK_LAST
};

/* ------------------------------------------------------------------ */
/* Callback typedefs                                                    */
typedef bool (RETRO_CALLCONV *retro_environment_t)(unsigned cmd, void *data);
typedef void (RETRO_CALLCONV *retro_video_refresh_t)(const void *data, unsigned width,
		unsigned height, size_t pitch);
typedef void (RETRO_CALLCONV *retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (RETRO_CALLCONV *retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV *retro_input_state_t)(unsigned port, unsigned device,
		unsigned index, unsigned id);

typedef void (RETRO_CALLCONV *retro_keyboard_event_t)(bool down, unsigned keycode,
		uint32_t character, uint16_t key_modifiers);

struct retro_keyboard_callback {
	retro_keyboard_event_t callback;
};

/* ------------------------------------------------------------------ */
/* Core entry points (implemented in libretro.cpp)                     */
RETRO_API void retro_set_environment(retro_environment_t);
RETRO_API void retro_set_video_refresh(retro_video_refresh_t);
RETRO_API void retro_set_audio_sample(retro_audio_sample_t);
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
RETRO_API void retro_set_input_poll(retro_input_poll_t);
RETRO_API void retro_set_input_state(retro_input_state_t);

RETRO_API void retro_init(void);
RETRO_API void retro_deinit(void);

RETRO_API unsigned retro_api_version(void);

RETRO_API void retro_get_system_info(struct retro_system_info *info);
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info);

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device);

RETRO_API void retro_reset(void);
RETRO_API void retro_run(void);

RETRO_API size_t retro_serialize_size(void);
RETRO_API bool retro_serialize(void *data, size_t size);
RETRO_API bool retro_unserialize(const void *data, size_t size);

RETRO_API void retro_cheat_reset(void);
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code);

RETRO_API bool retro_load_game(const struct retro_game_info *game);
RETRO_API bool retro_load_game_special(unsigned game_type,
		const struct retro_game_info *info, size_t num_info);
RETRO_API void retro_unload_game(void);

RETRO_API unsigned retro_get_region(void);

RETRO_API void *retro_get_memory_data(unsigned id);
RETRO_API size_t retro_get_memory_size(unsigned id);

#define RETRO_MEMORY_SAVE_RAM  0
#define RETRO_MEMORY_RTC       1
#define RETRO_MEMORY_SYSTEM_RAM 2
#define RETRO_MEMORY_VIDEO_RAM 3

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_TK2000_H */
