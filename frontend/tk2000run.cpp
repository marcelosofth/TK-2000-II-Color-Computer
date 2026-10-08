// frontend/tk2000run.cpp -- Frontend Win32 minimo para o core libretro do TK2000.
//
// O core (tk2000_libretro.dll) so' expoe as funcoes retro_*; nao tem main().
// Este arquivo e' o frontend: carrega a DLL, registra os callbacks que o core
// chama (video, audio, input, environment) e fica no laco de emulacao a 60 fps.
//
// Uso:  tk2000run.exe <fita.ct2 | disco.dsk | playlist.m3u> [core.dll]
//       tk2000run.exe                   -> sem argumento, abre o seletor de
//                                          fitas do proprio core (LOADT)
//
// Mapeamento de teclado (pad retro, o mesmo que o core le em retro_input_state):
//   setas  -> D-pad            Q       -> L  (hard reset)
//   X      -> A (fire)        W       -> R  (velocidade da fita)
//   Z      -> B (fire 2)      1 / 2   -> L2 / R2
//   A      -> Y (sem funcao)  4 / 5   -> L3 / R3
//   S      -> X (creditos)    Tab     -> soft reset
//   Enter  -> START (teclado virtual)     F1 -> ajuda
//   Shift dir. -> SELECT (escolher disco 2) Esc -> sai
//
// Qualquer outra tecla vai para o teclado da maquina, como num TK2000 de
// verdade -- e' por isso que da' para digitar um programa BASIC direto.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>

#include "libretro.h"

// -------------------------------------------------------------------------------------------------
// O libretro.h deste projeto e' anterior a 1.9: falta parte do enum de ambientes e os ids
// L3/R3 do pad. O libretro.cpp compensa com o proprio bloco #ifndef; aqui repito com os mesmos
// numeros, senao o core e o frontend discordariam do que cada um esta' perguntando.
#ifndef RETRO_ENVIRONMENT_GET_LIBRETRO_PATH
#define RETRO_ENVIRONMENT_GET_LIBRETRO_PATH 19
#endif
#ifndef RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY
#define RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY 30
#endif
#ifndef RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE 13
#endif
#ifndef RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION
#define RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION 57
#endif
#ifndef RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE
#define RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE 58
#endif
#ifndef RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL
#define RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL 8
#endif
#ifndef RETRO_ENVIRONMENT_GET_CAN_DUPE
#define RETRO_ENVIRONMENT_GET_CAN_DUPE 3
#endif
#ifndef RETRO_ENVIRONMENT_SET_GEOMETRY
#define RETRO_ENVIRONMENT_SET_GEOMETRY 37
#endif
#ifndef RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO
#define RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO 32
#endif
#ifndef RETRO_ENVIRONMENT_SET_CONTROLLER_INFO
#define RETRO_ENVIRONMENT_SET_CONTROLLER_INFO 35
#endif
#ifndef RETRO_DEVICE_ID_JOYPAD_L3
#define RETRO_DEVICE_ID_JOYPAD_L3 14
#endif
#ifndef RETRO_DEVICE_ID_JOYPAD_R3
#define RETRO_DEVICE_ID_JOYPAD_R3 15
#endif

namespace {

// -------------------------------------------------------------------------------------------------
// Log em arquivo. Sem janela de console, mas com rastro para diagnosticar.

FILE* g_log = nullptr;
int g_logLines = 0;
constexpr int kLogMax = 4000;

void logf(const char* fmt, ...) {
	if (!g_log) {
		return;
	}
	// O core registra cada leitura da matriz do teclado em nivel WARN, o que da' milhares de linhas
	// por segundo (27 mil em 4 segundos, medido). Filtrar por nivel nao ajuda, porque ele usa WARN
	// justamente para esse rastro de depuracao. Sem teto, uma sessao de dez minutos daria centenas
	// de MB. Passando do limite, para de gravar e avisa uma unica vez.
	if (g_logLines >= kLogMax) {
		return;
	}
	if (g_logLines == kLogMax - 1) {
		fprintf(g_log, "... log truncado em %d linhas\n", kLogMax);
		fflush(g_log);
		g_logLines++;
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fflush(g_log);
	g_logLines++;
}

void RETRO_CALLCONV cbLog(enum retro_log_level level, const char* fmt, ...) {
	static const char* names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
	const char* tag = (level < 4) ? names[level] : "?";
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	// A janela de console sumiria junto com o .exe; o log em disco e' que vale.
	logf("[%s] %s", tag, buf);
	OutputDebugStringA(buf);
}

// -------------------------------------------------------------------------------------------------
// Estado global.

struct SCore {
	HMODULE dll = nullptr;
	void (*set_environment)(retro_environment_t) = nullptr;
	void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
	void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
	void (*set_input_poll)(retro_input_poll_t) = nullptr;
	void (*set_input_state)(retro_input_state_t) = nullptr;
	unsigned (*api_version)() = nullptr;
	void (*init)() = nullptr;
	void (*deinit)() = nullptr;
	void (*get_system_info)(struct retro_system_info*) = nullptr;
	void (*get_system_av_info)(struct retro_system_av_info*) = nullptr;
	void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
	bool (*load_game)(const struct retro_game_info*) = nullptr;
	void (*unload_game)() = nullptr;
	void (*run)() = nullptr;
	void (*reset)() = nullptr;		// opcional: sem isso o TAB vira mensagem no log
};

SCore g_core;
std::string g_exeDir;
std::wstring g_exeDirW;			// o mesmo caminho em Unicode, para abrir arquivo no wide
std::string g_contentDir;
std::map<std::string, std::string> g_vars;
retro_keyboard_event_t g_keyboardCb = nullptr;
int16_t g_pad = 0;
bool g_loading = false;				// durante retro_load_game: mensagem vai para MessageBox
volatile LONG g_quit = 0;
HWND g_hWnd = nullptr;

// -------------------------------------------------------------------------------------------------
// Video: DIB de base_width x base_height que o core escreve direto; o WM_PAINT faz StretchBlt.
//
// O core pede RETRO_PIXEL_FORMAT_XRGB8888, isto e', a palavra de 32 bits e' 0x00RRGGBB. Em
// little-endian os bytes em memoria ficam BB GG RR 00, que e' exatamente o layout de uma BI_RGB de
// 32 bits do GDI. Por isso o memcpy e' direto, sem trocar canal nenhum.

BITMAPINFO g_bmi = {};
HBITMAP g_dib = nullptr;
void* g_pixels = nullptr;
unsigned g_fbW = 0, g_fbH = 0;
int g_scale = 3;

bool createDib(unsigned w, unsigned h) {
	memset(&g_bmi, 0, sizeof(g_bmi));
	g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	g_bmi.bmiHeader.biWidth = (LONG)w;
	g_bmi.bmiHeader.biHeight = -(LONG)h;		// negativo = de cima para baixo
	g_bmi.bmiHeader.biPlanes = 1;
	g_bmi.bmiHeader.biBitCount = 32;
	g_bmi.bmiHeader.biCompression = BI_RGB;
	g_pixels = nullptr;
	g_dib = CreateDIBSection(nullptr, &g_bmi, DIB_RGB_COLORS, &g_pixels, nullptr, 0);
	g_fbW = w;
	g_fbH = h;
	return g_dib != nullptr;
}

// -------------------------------------------------------------------------------------------------
// Audio: WinMM waveOut em modo polado. O retro_run roda na thread principal e nao registramos
// callback no device, entao devolvemos os buffers olhando a flag WHDR_DONE. Sem thread extra e sem
// corrida -- tudo acontece na mesma thread.

constexpr int kSampleRate = 44100;
constexpr int kFramesPerVideoFrame = kSampleRate / 60;		// 735
constexpr int kAudioBufs = 8;

struct SAudioBuf {
	WAVEHDR hdr{};
	std::vector<int16_t> samples;
};

HWAVEOUT g_wave = nullptr;
SAudioBuf g_bufs[kAudioBufs];
std::vector<int16_t> g_staging;		// audio do frame ainda nao empacotado
size_t g_stagingFrames = 0;
bool g_waveDead = false;

bool audioOpen() {
	WAVEFORMATEX wf;
	memset(&wf, 0, sizeof(wf));
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 2;
	wf.nSamplesPerSec = kSampleRate;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = 4;
	wf.nAvgBytesPerSec = kSampleRate * 4;
	// WAVE_MAPPER primeiro (o caminho normal). Se ele falhar, tenta cada dispositivo pelo indice:
	// quando o mapper nao esta' publicado -- maquina com mais de uma placa, sessao sem audio
	// compartilhado -- o WAVE_MAPPER devolve BADDEVICEID (MMRESULT 1) mesmo com o hardware
	// presente, e abrir direto pelo indice funciona.
	MMRESULT mm = waveOutOpen(&g_wave, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
	if (mm != MMSYSERR_NOERROR) {
		g_wave = nullptr;
		const UINT n = waveOutGetNumDevs();
		logf("WAVE_MAPPER falhou (MMRESULT %u); ha %u dispositivo(s), tentando por indice\n",
			(unsigned)mm, n);
		for (UINT i = 0; i < n && !g_wave; i++) {
			mm = waveOutOpen(&g_wave, i, &wf, 0, 0, CALLBACK_NULL);
			if (mm == MMSYSERR_NOERROR) {
				logf("abrindo dispositivo de saida %u\n", i);
			} else {
				g_wave = nullptr;
			}
		}
	}
	if (!g_wave) {
		logf("nenhum dispositivo de audio respondeu (ultimo MMRESULT %u)\n", (unsigned)mm);
		return false;
	}
	for (int i = 0; i < kAudioBufs; i++) {
		g_bufs[i].samples.assign(kFramesPerVideoFrame * 2, 0);
		memset(&g_bufs[i].hdr, 0, sizeof(WAVEHDR));
		g_bufs[i].hdr.lpData = (LPSTR)g_bufs[i].samples.data();
		g_bufs[i].hdr.dwBufferLength = (DWORD)(g_bufs[i].samples.size() * sizeof(int16_t));
	}
	return true;
}

void audioClose() {
	if (!g_wave) {
		return;
	}
	waveOutReset(g_wave);
	for (int i = 0; i < kAudioBufs; i++) {
		if (g_bufs[i].hdr.dwFlags & WHDR_PREPARED) {
			waveOutUnprepareHeader(g_wave, &g_bufs[i].hdr, sizeof(WAVEHDR));
		}
	}
	waveOutClose(g_wave);
	g_wave = nullptr;
}

void audioFlushStaging() {
	if (!g_wave || g_stagingFrames == 0) {
		return;
	}
	// Buffers que o device ja' tocou voltam a lista de livres.
	for (int i = 0; i < kAudioBufs; i++) {
		WAVEHDR& h = g_bufs[i].hdr;
		if ((h.dwFlags & WHDR_PREPARED) && (h.dwFlags & WHDR_DONE)) {
			waveOutUnprepareHeader(g_wave, &h, sizeof(WAVEHDR));
		}
	}
	for (int i = 0; i < kAudioBufs; i++) {
		WAVEHDR& h = g_bufs[i].hdr;
		if (h.dwFlags & WHDR_PREPARED) {
			continue;
		}
		const size_t room = g_bufs[i].samples.size() / 2;
		const size_t n = (g_stagingFrames < room) ? g_stagingFrames : room;
		memcpy(g_bufs[i].samples.data(), g_staging.data(), n * 2 * sizeof(int16_t));
		h.lpData = (LPSTR)g_bufs[i].samples.data();
		h.dwBufferLength = (DWORD)(n * 2 * sizeof(int16_t));
		h.dwFlags = 0;
		if (waveOutPrepareHeader(g_wave, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
			h.dwFlags = 0;
			g_waveDead = true;
			return;
		}
		if (waveOutWrite(g_wave, &h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
			g_waveDead = true;
		}
		g_stagingFrames = 0;
		return;
	}
	// Os 8 buffers ainda em uso: o core rendeu mais audio do que o device consumiu (aceleracao de
	// fita roda varias imagens por quadro). Descarta em vez de travar o emulador.
	g_stagingFrames = 0;
}

// -------------------------------------------------------------------------------------------------
// Callbacks que o core chama.

void RETRO_CALLCONV cbVideo(const void* data, unsigned width, unsigned height, size_t pitch) {
	if (!g_pixels || !data) {
		return;						// nullptr = "repita o quadro atual" (dupe)
	}
	if (width != g_fbW || height != g_fbH) {
		// O core promete base_width x base_height e nunca muda no meio do jogo. Se mudar,
		// recreio o DIB -- mas aviso, porque o resto do frontend foi feito em cima do tamanho
		// declarado em retro_get_system_av_info.
		logf("aviso: quadro %ux%u, esperado %ux%u\n", width, height, g_fbW, g_fbH);
		if (!createDib(width, height)) {
			logf("falha ao recriar o DIB\n");
			return;
		}
	}
	const size_t linha = (size_t)width * 4;
	const unsigned char* src = (const unsigned char*)data;
	unsigned char* dst = (unsigned char*)g_pixels;
	for (unsigned y = 0; y < height; y++) {
		memcpy(dst, src, linha);
		src += pitch;
		dst += linha;
	}
	InvalidateRect(g_hWnd, nullptr, FALSE);
}

size_t RETRO_CALLCONV cbAudioBatch(const int16_t* data, size_t frames) {
	if (!data || frames == 0) {
		return 0;
	}
	const size_t cap = g_staging.size() / 2;
	const size_t room = cap - g_stagingFrames;
	const size_t n = (frames < room) ? frames : room;
	memcpy(g_staging.data() + g_stagingFrames * 2, data, n * 2 * sizeof(int16_t));
	g_stagingFrames += n;
	return frames;					// devolve o total pedido, mesmo tendo guardado menos
}

int16_t RETRO_CALLCONV cbInputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void)port;
	(void)index;
	if (device != RETRO_DEVICE_JOYPAD || id >= 16) {
		return 0;
	}
	return (g_pad & (1 << id)) ? 1 : 0;
}

void RETRO_CALLCONV cbInputPoll() {
	// O estado ja' foi montado pela thread da janela, antes do retro_run.
}

bool RETRO_CALLCONV cbEnvironment(unsigned cmd, void* data) {
	switch (cmd) {
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
			auto* log = (struct retro_log_callback*)data;
			log->log = cbLog;
			return true;
		}
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
			auto* fmt = (enum retro_pixel_format*)data;
			if (*fmt != RETRO_PIXEL_FORMAT_XRGB8888) {
				logf("pixel format %d nao suportado\n", (int)*fmt);
				return false;
			}
			return true;
		}
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
			auto* out = (const char**)data;
			*out = g_contentDir.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: {
			auto* out = (const char**)data;
			*out = g_exeDir.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_SET_VARIABLES: {
			auto* vars = (const struct retro_variable*)data;
			for (const struct retro_variable* v = vars; v && v->key; v++) {
				// Default = primeiro valor da lista, que vem depois do ';'.
				const std::string s = v->value ? v->value : "";
				const size_t sc = s.find(';');
				std::string def = (sc == std::string::npos) ? s : s.substr(sc + 1);
				const size_t bar = def.find('|');
				if (bar != std::string::npos) {
					def = def.substr(0, bar);
				}
				g_vars[v->key] = def;
			}
			return true;
		}
		case RETRO_ENVIRONMENT_GET_VARIABLE: {
			auto* v = (struct retro_variable*)data;
			const auto it = g_vars.find(v->key ? v->key : "");
			if (it == g_vars.end()) {
				return false;
			}
			v->value = it->second.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
			auto* k = (struct retro_keyboard_callback*)data;
			g_keyboardCb = k->callback;
			return true;
		}
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
			return true;
		case RETRO_ENVIRONMENT_SET_MESSAGE: {
			auto* msg = (const struct retro_message*)data;
			logf("[msg] %s\n", msg->msg ? msg->msg : "");
			if (g_loading && msg->msg) {
				MessageBoxA(g_hWnd, msg->msg, "TK2000", MB_OK | MB_ICONINFORMATION);
			}
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
			return true;			// sem argumento na linha de comando, o proprio core abre o seletor
		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*(bool*)data = true;
			return true;
		// Controle de disco: este frontend nao tem UI de troca, entao nao registra a interface. O
		// core so' avisa o frontend e nunca chama de volta, entao recusar e' seguro.
		case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
		case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
		case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
			return false;
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_GEOMETRY:
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			return true;
		default:
			return false;
	}
}

// -------------------------------------------------------------------------------------------------
// Teclado.

struct SKbdMap { int vk; unsigned retrok; };

// As setas ficam de fora de proposito: no TK2000 de verdade elas sao o joystick, e ja' vao para o
// D-pad do pad retro. Mandar as duas coisas junto faria o BASIC ler seta E joystick ao mesmo tempo.
const SKbdMap kKbd[] = {
	{ 'A', RETROK_a }, { 'B', RETROK_b }, { 'C', RETROK_c }, { 'D', RETROK_d },
	{ 'E', RETROK_e }, { 'F', RETROK_f }, { 'G', RETROK_g }, { 'H', RETROK_h },
	{ 'I', RETROK_i }, { 'J', RETROK_j }, { 'K', RETROK_k }, { 'L', RETROK_l },
	{ 'M', RETROK_m }, { 'N', RETROK_n }, { 'O', RETROK_o }, { 'P', RETROK_p },
	{ 'Q', RETROK_q }, { 'R', RETROK_r }, { 'S', RETROK_s }, { 'T', RETROK_t },
	{ 'U', RETROK_u }, { 'V', RETROK_v }, { 'W', RETROK_w }, { 'X', RETROK_x },
	{ 'Y', RETROK_y }, { 'Z', RETROK_z },
	{ '0', RETROK_0 }, { '1', RETROK_1 }, { '2', RETROK_2 }, { '3', RETROK_3 },
	{ '4', RETROK_4 }, { '5', RETROK_5 }, { '6', RETROK_6 }, { '7', RETROK_7 },
	{ '8', RETROK_8 }, { '9', RETROK_9 },
	{ VK_OEM_1, RETROK_COLON }, { VK_OEM_PLUS, RETROK_EQUALS },
	{ VK_OEM_COMMA, RETROK_COMMA }, { VK_OEM_MINUS, RETROK_MINUS },
	{ VK_OEM_PERIOD, RETROK_PERIOD }, { VK_OEM_2, RETROK_SLASH },
	{ VK_OEM_3, RETROK_BACKQUOTE },
	{ VK_OEM_4, RETROK_LEFTBRACKET }, { VK_OEM_5, RETROK_BACKSLASH },
	{ VK_OEM_6, RETROK_RIGHTBRACKET },
	{ VK_SPACE, RETROK_SPACE }, { VK_RETURN, RETROK_RETURN },
	{ VK_BACK, RETROK_BACKSPACE }, { VK_TAB, RETROK_TAB }, { VK_F1, RETROK_F1 },
	{ VK_F2, RETROK_F2 }, { VK_F3, RETROK_F3 }, { VK_F4, RETROK_F4 },
	{ VK_F5, RETROK_F5 }, { VK_F6, RETROK_F6 }, { VK_HOME, RETROK_HOME },
};

struct SPadMap { int vk; unsigned id; };

const SPadMap kPad[] = {
	{ VK_UP, RETRO_DEVICE_ID_JOYPAD_UP },
	{ VK_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ VK_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ VK_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
	{ 'X', RETRO_DEVICE_ID_JOYPAD_A },
	{ 'Z', RETRO_DEVICE_ID_JOYPAD_B },
	{ 'A', RETRO_DEVICE_ID_JOYPAD_Y },
	{ 'S', RETRO_DEVICE_ID_JOYPAD_X },
	{ VK_RETURN, RETRO_DEVICE_ID_JOYPAD_START },
	{ VK_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT },
	{ 'Q', RETRO_DEVICE_ID_JOYPAD_L },
	{ 'W', RETRO_DEVICE_ID_JOYPAD_R },
	{ '1', RETRO_DEVICE_ID_JOYPAD_L2 },
	{ '2', RETRO_DEVICE_ID_JOYPAD_R2 },
	{ '4', RETRO_DEVICE_ID_JOYPAD_L3 },
	{ '5', RETRO_DEVICE_ID_JOYPAD_R3 },
};

bool vkIsBoundToPad(int vk) {
	for (const SPadMap& p : kPad) {
		if (p.vk == vk) {
			return true;
		}
	}
	return false;
}

bool vkIsReserved(int vk) {
	return vk == VK_ESCAPE || vk == VK_F1 || vk == VK_F2 || vk == VK_TAB;
}

unsigned vkToRetroK(int vk) {
	for (const SKbdMap& k : kKbd) {
		if (k.vk == vk) {
			return k.retrok;
		}
	}
	return RETROK_UNKNOWN;
}

void updatePad() {
	int16_t pad = 0;
	for (const SPadMap& p : kPad) {
		if (GetAsyncKeyState(p.vk) & 0x8000) {
			pad |= (int16_t)(1 << p.id);
		}
	}
	g_pad = pad;
}

// Gancho de teclado em nivel de sistema: pega as teclas mesmo com outra janela em foco, que e' o
// que o RetroArch faz com o "raw keyboard".
HHOOK g_kbHook = nullptr;

LRESULT CALLBACK lowLevelKb(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION) {
		const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
		const bool up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
		if ((down || up) && g_keyboardCb) {
			const auto* k = (const KBDLLHOOKSTRUCT*)lp;
			const int vk = (int)k->vkCode;
			if (!vkIsReserved(vk) && !vkIsBoundToPad(vk)) {
				g_keyboardCb(down, vkToRetroK(vk), k->scanCode, 0);
			}
		}
	}
	return CallNextHookEx(g_kbHook, code, wp, lp);
}

// -------------------------------------------------------------------------------------------------
// Janela.

void showHelp() {
	MessageBoxA(g_hWnd,
		"TECLADO\n"
		"  setas ................ D-pad (joystick)\n"
		"  X .................... A  (fire)\n"
		"  Z .................... B  (fire 2)\n"
		"  S .................... X  (creditos)\n"
		"  A .................... Y  (sem funcao)\n"
		"  Enter ................ START (teclado virtual)\n"
		"  Shift direito ......... SELECT (escolher disco 2)\n"
		"  Q .................... L  (hard reset)\n"
		"  W .................... R  (velocidade da fita)\n"
		"  1 / 2 ................ L2 / R2 (acelerar carga, creditos)\n"
		"  4 / 5 ................ L3 / R3\n"
		"  Tab .................. soft reset\n"
		"  F2 ................... escala 2x / 3x / 4x\n"
		"  Esc .................. sai\n"
		"\n"
		"Qualquer outra tecla vai para o teclado do TK2000,\n"
		"entao da' para digitar um programa BASIC direto.\n"
		"\n"
		"CREDITOS\n"
		"  S abre; S de novo pausa o texto; qualquer outro botao\n"
		"  fecha e deixa a maquina 30% mais lenta (acumula).",
		"TK2000 - ajuda", MB_OK);
}

void applyScale(int scale) {
	g_scale = scale;
	SetWindowPos(g_hWnd, nullptr, 0, 0, (int)(g_fbW * scale), (int)(g_fbH * scale),
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
		case WM_CREATE:
			g_hWnd = h;
			return 0;
		case WM_ERASEBKGND:
			return 1;				// o StretchBlt cobre tudo; evita piscar
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(h, &ps);
			RECT r;
			GetClientRect(h, &r);
			FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
			if (g_pixels && g_dib) {
				HDC mem = CreateCompatibleDC(dc);
				HGDIOBJ old = SelectObject(mem, g_dib);
				SetStretchBltMode(mem, COLORONCOLOR);
				StretchBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, (int)g_fbW, (int)g_fbH, SRCCOPY);
				SelectObject(mem, old);
				DeleteDC(mem);
			}
			EndPaint(h, &ps);
			return 0;
		}
		case WM_KEYDOWN: {
			const int vk = (int)wp;
			if (vk == VK_ESCAPE) {
				DestroyWindow(h);
				return 0;
			}
			if (vk == VK_F1) {
				showHelp();
				return 0;
			}
			if (vk == VK_F2) {
				applyScale(g_scale == 2 ? 3 : (g_scale == 3 ? 4 : 2));
				return 0;
			}
			if (vk == VK_TAB && g_core.reset) {
				g_core.reset();
			}
			return 0;				// come TAB e ESC para o Windows nao inventar atalho
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(h, msg, wp, lp);
	}
}

using byte = unsigned char;		// atalho so' para o cabecalho do BMP, que e' 100% casts

// Grava um quadro do DIB como BMP 24 bits. Existe para dar prova visual sem depender de desktop:
// o --selftest passa por video, audio e input sem ningem ver nada, e um BMP e' a unica forma de
// conferir que a imagem que chegou no DIB e' mesmo a da TK2000 (e nao um retangulo preto).
// O DIB tem 32 bits por pixel; aqui vira 24 porque e' o que qualquer visualizador abre sem Fahrn.
bool dumpBmp(const wchar_t* path, unsigned w, unsigned h, const void* px) {
	// Tudo escrito a mao, byte a byte. A temptacao e' usar BITMAPFILEHEADER e fazer
	// fwrite(&fh, sizeof(fh), 1, f) -- mas sizeof(structo) nao e' o tamanho do formato em disco:
	// o cabecalho tem 14 bytes e o compilador entrega 16, com 2 de padding. Sobravam 2 bytes de
	// lixo entre o fim do cabecalho de arquivo e o comeco do DIB, e o resultado era biBitCount
	// valendo 0 e biCompression valendo 24, que nenhum leitor aceita.
	const unsigned linha24 = ((w * 3 + 3) & ~3u);		// BMP: cada linha em multiplo de 4
	const unsigned dados = linha24 * h;
	const unsigned ppm = 2835u * 2835u;
	const unsigned sz = 14u + 40u + dados;
	// 10 dwords do BITMAPINFOHEADER, na ordem do formato, e os 14 bytes do BITMAPFILEHEADER
	// concatenados em um unico arranjo.
	// Cada valor na posicao exata do arquivo. Montar em dwords e_sobreviver 2 bytes de padding, ou
	// um 'B' que vai parar no byte errado -- as duas tentativas anteriores deram arquivo que o
	// ImageMagick recusava. Sao 54 bytes, escritos um a um: e' curto e nao tem como dar errado.
	unsigned char cab[54] = {
		'B', 'M',											// bfType
		(byte)(sz), (byte)(sz >> 8), (byte)(sz >> 16), (byte)(sz >> 24),		// bfSize
		0, 0, 0, 0,											// bfReserved1, bfReserved2
		54, 0, 0, 0,											// bfOffBits
		40, 0, 0, 0,											// biSize
		(byte)w, (byte)(w >> 8), 0, 0,						// biWidth
		(byte)h, (byte)(h >> 8), 0, 0,						// biHeight
		1, 0,												// biPlanes
		24, 0,												// biBitCount
		0, 0, 0, 0,											// biCompression = BI_RGB
		(byte)dados, (byte)(dados >> 8), (byte)(dados >> 16), (byte)(dados >> 24),	// biSizeImage
		(byte)ppm, (byte)(ppm >> 8), (byte)(ppm >> 16), (byte)(ppm >> 24),			// biXPelsPerMeter
		0, 0, 0, 0,											// biYPelsPerMeter
		0, 0, 0, 0,											// biClrUsed
		0, 0, 0, 0,											// biClrImportant
	};
	FILE* f = _wfopen(path, L"wb");
	if (!f) {
		return false;
	}
	fwrite(cab, 1, sizeof(cab), f);
	const unsigned char* base = (const unsigned char*)px;
	unsigned char* linha = (unsigned char*)_alloca(linha24);
	for (int y = (int)h - 1; y >= 0; y--) {		// BMP e' de baixo para cima
		const unsigned char* src = base + (size_t)y * w * 4;
		memset(linha, 0, linha24);
		for (unsigned x = 0; x < w; x++) {
			linha[x * 3 + 0] = src[x * 4 + 2];	// B
			linha[x * 3 + 1] = src[x * 4 + 1];	// G
			linha[x * 3 + 2] = src[x * 4 + 0];	// R
		}
		fwrite(linha, 1, linha24, f);
	}
	fclose(f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// --selftest: roda o core sem janela nem placa de som e confere, por numeros, o caminho inteiro --
// core, video, audio, input e teclado. Existe porque a sessao onde este .exe foi compilado roda em
// um desktop separado, onde SetForegroundWindow e SendKeys nao alcancam a janela: sem isso o teste
// de input ficaria sem evidencia. Aqui nada depende de foco de teclado.

struct SSelfTest {
	int frames = 0;
	int videos = 0;
	int audioCalls = 0;
	size_t audioFrames = 0;
	int inputPolls = 0;
	int inputStates = 0;
	int kbDown = 0;
	int kbUp = 0;
	unsigned long pixSum = 0;			// soma dos pixels: prova que imagem chegou ao DIB
	int dumpFrame = 45;				// --bmp=N sobrescreve
	int botaoId = 9;				// --botao=N: id do pad do core (X=9, Y=1, R2=13)
	bool bmpOK = false;
	wchar_t bmpPath[MAX_PATH] = {};
};

const char* nomeBotao(int id) {
	switch (id) {
		case 0: return "B"; case 1: return "Y"; case 2: return "SELECT"; case 3: return "START";
		case 4: return "UP"; case 5: return "DOWN"; case 6: return "LEFT"; case 7: return "RIGHT";
		case 8: return "A"; case 9: return "X"; case 10: return "L"; case 11: return "R";
		case 12: return "L2"; case 13: return "R2";
		default: return "?";
	}
}

SSelfTest g_st;

void RETRO_CALLCONV cbVideoSt(const void* data, unsigned w, unsigned h, size_t pitch) {
	cbVideo(data, w, h, pitch);
	g_st.videos++;
	if (data) {
		const unsigned char* p = (const unsigned char*)data;
		const size_t linha = (size_t)w * 4;
		for (unsigned y = 0; y < h; y += 8) {		// uma linha a cada 8: da' medida sem custar tudo
			for (size_t i = 0; i < linha; i += 4) {
				g_st.pixSum += p[y * pitch + i] + p[y * pitch + i + 1] + p[y * pitch + i + 2];
			}
		}
	}
}

size_t RETRO_CALLCONV cbAudioBatchSt(const int16_t* d, size_t f) {
	g_st.audioCalls++;
	g_st.audioFrames += f;
	return cbAudioBatch(d, f);			// segue o mesmo caminho do jogo de verdade
}

int16_t RETRO_CALLCONV cbInputStateSt(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	g_st.inputStates++;
	return cbInputState(port, dev, idx, id);
}

void RETRO_CALLCONV cbInputPollSt() {
	g_st.inputPolls++;
	cbInputPoll();
}

int runSelfTest(const std::string& game) {
	g_core.set_environment(cbEnvironment);
	g_core.set_video_refresh(cbVideoSt);
	g_core.set_audio_sample(nullptr);
	g_core.set_audio_sample_batch(cbAudioBatchSt);
	g_core.set_input_poll(cbInputPollSt);
	g_core.set_input_state(cbInputStateSt);
	g_core.init();

	struct retro_game_info gi;
	memset(&gi, 0, sizeof(gi));
	if (!game.empty()) {
		gi.path = game.c_str();
	}
	g_loading = false;					// sem MessageBox: isto roda sem desktop
	if (!g_core.load_game(game.empty() ? nullptr : &gi)) {
		logf("selftest: retro_load_game devolveu false\n");
		g_core.deinit();
		return 1;
	}
	struct retro_system_av_info av;
	g_core.get_system_av_info(&av);
	if (!createDib(av.geometry.base_width, av.geometry.base_height)) {
		logf("selftest: CreateDIBSection falhou\n");
		return 1;
	}

	const int kFrames = 180;
	// No quadro 30 aperta o botao pedido (--botao=N, o pad do core) por 5 quadros; no 60 digita
	// 'M' no teclado da maquina. O nome do botao entra no log, porque o sintoma que importa nao e'
	// "algo deu errado" e sim "qual entrada fez a janela de creditos aparecer".
	const int16_t comBotao = (int16_t)(1 << (g_st.botaoId & 15));
	for (int i = 0; i < kFrames; i++) {
		g_pad = (i >= 30 && i < 35) ? comBotao : 0;
		if (i == 60 && g_keyboardCb) {
			g_keyboardCb(true, RETROK_m, 0, 0);
			g_st.kbDown++;
		}
		if (i == 66 && g_keyboardCb) {
			g_keyboardCb(false, RETROK_m, 0, 0);
			g_st.kbUp++;
		}
		g_core.run();
		g_st.frames++;
		// Com --bmp N, guarda o quadro N. O default e' 45, few frames depois do aperto do botao
		// dos creditos -- tempo suficiente para a janela aparecer e ainda estar subindo.
		if (i == g_st.dumpFrame && g_pixels) {
			wchar_t bmp[MAX_PATH];
			_snwprintf_s(bmp, MAX_PATH, _TRUNCATE, L"%lsquadro_%03d.bmp", g_exeDirW.c_str(), i);
			if (dumpBmp(bmp, g_fbW, g_fbH, g_pixels)) {
				g_st.bmpOK = true;
				wcsncpy_s(g_st.bmpPath, MAX_PATH, bmp, _TRUNCATE);
			}
		}
	}

	// O relatorio vai para dois lugares: a saida padrao e um arquivo. O stdout porque e' o unico
	// destino que o chamador controla sem depender de pasta nenhuma -- quando o .exe roda em outra
	// sessao, um caminho calculado a partir do proprio executavel pode nao ser gravavel la.
	printf("quadros rodados .......... %d\n", g_st.frames);
	printf("chamadas de video ....... %d\n", g_st.videos);
	printf("soma dos pixels ......... %lu\n", g_st.pixSum);
	printf("chamadas de audio ....... %d  (%zu quadros)\n", g_st.audioCalls, g_st.audioFrames);
	printf("input_poll .............. %d\n", g_st.inputPolls);
	printf("input_state ............. %d\n", g_st.inputStates);
	printf("teclado down/up ......... %d / %d\n", g_st.kbDown, g_st.kbUp);
	printf("callback de teclado ..... %s\n", g_keyboardCb ? "registrado" : "AUSENTE");
	printf("DIB ..................... %ux%u\n", g_fbW, g_fbH);
	printf("pasta do exe ............ [%ls]\n", g_exeDirW.c_str());
	printf("botao testado .......... %s (id %d)\n", nomeBotao(g_st.botaoId), g_st.botaoId);
	if (g_st.bmpOK) {
		printf("quadro gravado .......... %ls\n", g_st.bmpPath);
	}

	// Tenta gravar ao lado do .exe; se nao der (pasta somente-leitura, sessao sem perfil), cai
	// para o TEMP. Sem isso o --selftest devolvia "passou" sem deixar nenhum rastro, porque a
	// falha de _wfopen e' silenciosa e o criterio de aprovacao nao depende do arquivo.
	wchar_t relPath[MAX_PATH];
	_snwprintf_s(relPath, MAX_PATH, _TRUNCATE, L"%lsselftest.txt", g_exeDirW.c_str());
	FILE* r = _wfopen(relPath, L"w");
	if (!r) {
		wchar_t tmp[MAX_PATH];
		if (GetTempPathW(MAX_PATH, tmp)) {
			wcscat_s(tmp, L"tk2000run-selftest.txt");
			r = _wfopen(tmp, L"w");
			if (r) {
				wcsncpy_s(relPath, MAX_PATH, tmp, _TRUNCATE);
			}
		}
	}
	if (r) {
		fprintf(r, "quadros rodados .......... %d\n", g_st.frames);
		fprintf(r, "chamadas de video ....... %d\n", g_st.videos);
		fprintf(r, "soma dos pixels ......... %lu\n", g_st.pixSum);
		fprintf(r, "chamadas de audio ....... %d  (%zu quadros)\n", g_st.audioCalls, g_st.audioFrames);
		fprintf(r, "input_poll .............. %d\n", g_st.inputPolls);
		fprintf(r, "input_state ............. %d\n", g_st.inputStates);
		fprintf(r, "teclado down/up ......... %d / %d\n", g_st.kbDown, g_st.kbUp);
		fprintf(r, "callback de teclado ..... %s\n", g_keyboardCb ? "registrado" : "AUSENTE");
		fprintf(r, "DIB ..................... %ux%u\n", g_fbW, g_fbH);
		fprintf(r, "relatorio em ............ %ls\n", relPath);
		fclose(r);
	} else {
		printf("nao consegui gravar o relatorio em arquivo\n");
	}
	fflush(stdout);
	logf("selftest: %d quadros, %d videos, soma pixels %lu, %d audio (%zu quadros), "
		"%d polls, %d input_state, teclado %d/%d, teclado-cb %s\n",
		g_st.frames, g_st.videos, g_st.pixSum, g_st.audioCalls, g_st.audioFrames,
		g_st.inputPolls, g_st.inputStates, g_st.kbDown, g_st.kbUp,
		g_keyboardCb ? "ok" : "AUSENTE");

	const bool ok = (g_st.frames == kFrames) && (g_st.videos >= kFrames) && (g_st.pixSum > 0)
		&& (g_st.audioCalls > 0) && (g_st.inputPolls >= kFrames) && (g_st.inputStates > 0)
		&& (g_keyboardCb != nullptr);
	logf("selftest: %s\n", ok ? "PASSOU" : "FALHOU");
	g_core.unload_game();
	g_core.deinit();
	return ok ? 0 : 2;
}

// -------------------------------------------------------------------------------------------------
// Carregamento da DLL.

// O aviso de -Wcast-function-type aqui e' inevitavel e inofensivo: e' justamente o que a
// GetProcAddress faz -- ela devolve FARPROC e quem chama sabe qual a assinatura correta. Sem
// isso seriam 14 avisos para os 14 callbacks.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
template <class T>
bool sym(HMODULE m, const char* name, T& out) {
	out = (T)GetProcAddress(m, name);
	if (!out) {
		logf("simbolo ausente: %s\n", name);
		return false;
	}
	return true;
}
#pragma GCC diagnostic pop

bool loadCore(const char* path) {
	HMODULE m = LoadLibraryA(path);
	if (!m) {
		logf("LoadLibrary falhou (%lu): %s\n", GetLastError(), path);
		return false;
	}
	g_core.dll = m;
	bool ok = true;
	ok &= sym(m, "retro_set_environment", g_core.set_environment);
	ok &= sym(m, "retro_set_video_refresh", g_core.set_video_refresh);
	ok &= sym(m, "retro_set_audio_sample", g_core.set_audio_sample);
	ok &= sym(m, "retro_set_audio_sample_batch", g_core.set_audio_sample_batch);
	ok &= sym(m, "retro_set_input_poll", g_core.set_input_poll);
	ok &= sym(m, "retro_set_input_state", g_core.set_input_state);
	ok &= sym(m, "retro_api_version", g_core.api_version);
	ok &= sym(m, "retro_init", g_core.init);
	ok &= sym(m, "retro_deinit", g_core.deinit);
	ok &= sym(m, "retro_get_system_info", g_core.get_system_info);
	ok &= sym(m, "retro_get_system_av_info", g_core.get_system_av_info);
	ok &= sym(m, "retro_set_controller_port_device", g_core.set_controller_port_device);
	ok &= sym(m, "retro_load_game", g_core.load_game);
	ok &= sym(m, "retro_unload_game", g_core.unload_game);
	ok &= sym(m, "retro_run", g_core.run);
	if (!ok) {
		return false;
	}
	sym(m, "retro_reset", g_core.reset);	// opcional
	return true;
}

std::string acp(const wchar_t* w);

// Acha o caminho do proprio .exe, sem a barra final, nas duas codificacoes. Vale uma vez so, no
// comeco: tanto o log quanto o diretorio de conteudo sao derivados daqui.
void findExeDir() {
	wchar_t w[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, w, MAX_PATH);
	if (!n || n >= MAX_PATH) {
		return;
	}
	wchar_t* last = wcsrchr(w, L'\\');
	if (last) {
		*last = 0;					// sem a barra final: g_exeDir e' usado para montar o caminho do core
	} else {
		w[0] = 0;
	}
	g_exeDir = acp(w);
	// Esta e' a versao COM a barra final, para concatenar nome de arquivo direto. Sem ela o BMP
	// saia como "...\standalonequadro_045.bmp", e o selftest.txt nem aparecia na pasta -- que
	// era exatamente o que eu vi antes de differ aqui.
	g_exeDirW = w;
	g_exeDirW += L'\\';
}

std::string acp(const wchar_t* w) {
	std::string s;
	const int need = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (need > 1) {
		s.resize(need - 1);
		WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], need, nullptr, nullptr);
	}
	return s;
}

}	// namespace

// -------------------------------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	findExeDir();
	{
		// Log ao lado do .exe, para nao sujar o %TEMP%. g_exeDirW ja' vem com a barra final.
		const std::wstring logPath = g_exeDirW + L"tk2000run.log";
		g_log = _wfopen(logPath.c_str(), L"w");
	}
	logf("tk2000run iniciando\n");

	int argc = 0;
	LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::string argGame, argCore;
	bool selftest = false;
	if (argvW) {
		for (int i = 1; i < argc; i++) {
			const std::string a = acp(argvW[i]);
			// --bmp tem de vir colado no numero (--bmp=45). Aceitar "--bmp 45" com o numero
			// solto seria um convite a ler o 45 como nome do jogo, e a falha seguinte cai num
			// MessageBox que -- num desktop sem ninguem -- segura o processo para sempre.
			if (a == "--selftest" || a == "-t") {
				selftest = true;
			} else if (a.rfind("--bmp=", 0) == 0) {
				const int v = atoi(a.c_str() + 6);
				if (v >= 0 && v < 100000) {
					g_st.dumpFrame = v;
				} else {
					logf("--bmp fora de faixa: %s\n", a.c_str());
				}
				selftest = true;
			} else if (a.rfind("--botao=", 0) == 0) {
				const int v = atoi(a.c_str() + 8);
				if (v >= 0 && v < 16) {
					g_st.botaoId = v;
				}
				selftest = true;
			} else if (a.rfind("--botao", 0) == 0) {
				logf("use --botao=N, com o numero colado (X=9, Y=1, R2=13)\n");
				selftest = true;
			} else if (!a.empty() && a[0] == '-') {
				logf("opcao desconhecida: %s\n", a.c_str());
			} else if (argGame.empty()) {
				argGame = a;
			} else if (argCore.empty()) {
				argCore = a;
			}
		}
		LocalFree(argvW);
	}

	g_contentDir = g_exeDir;			// sem argumento, o seletor de fitas olha junto do .exe
	if (!argGame.empty()) {
		const size_t s = argGame.find_last_of("\\/");
		if (s != std::string::npos) {
			g_contentDir = argGame.substr(0, s);
		}
	}

	const std::string corePath = argCore.empty() ? (g_exeDir + "\\tk2000_libretro.dll") : argCore;
	logf("core: %s\n", corePath.c_str());
	logf("jogo: %s\n", argGame.empty() ? "(seletor do core)" : argGame.c_str());
	logf("conteudo: %s\n", g_contentDir.c_str());

	if (!loadCore(corePath.c_str())) {
		char msg[640];
		snprintf(msg, sizeof(msg),
			"Nao consegui carregar o core:\n%s\n\nColoque a tk2000_libretro.dll ao lado do "
			"tk2000run.exe.\nO detalhe esta' em tk2000run.log.", corePath.c_str());
		if (selftest) {
			// Sem MessageBox no --selftest: ele existe justamente para rodar sem ninguem na
			// frente da tela, e um dialogo modal ali trava o processo para sempre.
			printf("FALHOU: %s\n", msg);
			fflush(stdout);
		} else {
			MessageBoxA(nullptr, msg, "TK2000", MB_OK | MB_ICONERROR);
		}
		return 1;
	}
	if (g_core.api_version() != RETRO_API_VERSION) {
		logf("aviso: API %u, esperava %u\n", g_core.api_version(), RETRO_API_VERSION);
	}

	if (selftest) {
		const int rc = runSelfTest(argGame);
		FreeLibrary(g_core.dll);
		if (g_log) {
			fclose(g_log);
			g_log = nullptr;
		}
		return rc;
	}

	g_core.set_environment(cbEnvironment);
	g_core.set_video_refresh(cbVideo);
	g_core.set_audio_sample(nullptr);
	g_core.set_audio_sample_batch(cbAudioBatch);
	g_core.set_input_poll(cbInputPoll);
	g_core.set_input_state(cbInputState);
	g_core.init();

	struct retro_system_info sysInfo;
	g_core.get_system_info(&sysInfo);
	logf("core '%s' %s, extensoes: %s\n", sysInfo.library_name, sysInfo.library_version,
		sysInfo.valid_extensions ? sysInfo.valid_extensions : "");

	struct retro_game_info game;
	memset(&game, 0, sizeof(game));
	if (!argGame.empty()) {
		game.path = argGame.c_str();
	}
	g_loading = true;
	const bool loaded = g_core.load_game(argGame.empty() ? nullptr : &game);
	g_loading = false;
	if (!loaded) {
		if (selftest) {
			printf("FALHOU: o core recusou o conteudo\n");
			fflush(stdout);
		} else {
			MessageBoxA(nullptr, "O core recusou o conteudo. O detalhe esta' em tk2000run.log.",
				"TK2000", MB_OK | MB_ICONERROR);
		}
		g_core.deinit();
		return 1;
	}

	struct retro_system_av_info av;
	g_core.get_system_av_info(&av);
	logf("%ux%u  %.3f fps  %u Hz\n", av.geometry.base_width, av.geometry.base_height,
		av.timing.fps, (unsigned)av.timing.sample_rate);
	if (av.timing.sample_rate != (double)kSampleRate) {
		logf("aviso: core pede %u Hz, o audio sai em %d Hz\n",
			(unsigned)av.timing.sample_rate, kSampleRate);
	}
	if (!createDib(av.geometry.base_width, av.geometry.base_height)) {
		MessageBoxA(nullptr, "Falha ao criar o DIB de video.", "TK2000", MB_OK | MB_ICONERROR);
		return 1;
	}
	g_core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

	bool semAudio = false;

	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = wndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = "TK2000Run";
	RegisterClassA(&wc);

	char title[600];
	snprintf(title, sizeof(title), "TK2000 - %s",
		argGame.empty() ? "seletor de fitas" : argGame.c_str());

	// 3x em 280x192 = 840x576, que e' 4:3 exato (o core declara 4:3).
	g_hWnd = CreateWindowExA(0, "TK2000Run", title, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, (int)g_fbW * 3 + 16, (int)g_fbH * 3 + 39,
		nullptr, nullptr, wc.hInstance, nullptr);
	if (!g_hWnd) {
		MessageBoxA(nullptr, "Falha ao criar a janela.", "TK2000", MB_OK | MB_ICONERROR);
		return 1;
	}

	g_staging.assign((size_t)kFramesPerVideoFrame * 8 * 2, 0);
	bool temPeriod = false;
	if (audioOpen()) {
		// So' o que da' para dormir com precisao de ~1 ms, que o pacing do laco precisa.
		temPeriod = (timeBeginPeriod(1) == TIMERR_NOERROR);
	} else {
		// Sem MessageBox aqui: modal no boot trava o emulador antes da primeira imagem, e o
		// unico sintoma (nao sai som) ja' aparece marcado no titulo da janela.
		logf("aviso: waveOut nao abriu, seguindo sem som\n");
		semAudio = true;
	}
	g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKb, nullptr, 0);

	char titulo[640];
	snprintf(titulo, sizeof(titulo), "TK2000 - %s%s",
		argGame.empty() ? "seletor de fitas" : argGame.c_str(), semAudio ? "  (sem audio)" : "");
	SetWindowTextA(g_hWnd, titulo);
	logf("titulo: [%s]  (argGame = %u bytes)\n", titulo, (unsigned)argGame.size());

	ShowWindow(g_hWnd, SW_SHOW);
	UpdateWindow(g_hWnd);
	SetForegroundWindow(g_hWnd);
	logf("entrando no laco\n");

	LARGE_INTEGER freq, last, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&last);
	const double dtFrame = 1.0 / (av.timing.fps > 1.0 ? av.timing.fps : 60.0);
	double acc = 0.0;
	MSG msg;

	while (!g_quit) {
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				g_quit = 1;
			}
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
		if (g_quit) {
			break;
		}

		QueryPerformanceCounter(&now);
		acc += (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
		last = now;
		// Se a janela ficou arrastada, o contador cresce e o laco tentaria recuperar dezenas de
		// quadros de uma vez -- e o emulador daria um salto. Descarta o passado.
		if (acc > dtFrame * 4.0) {
			acc = dtFrame;
		}

		while (acc >= dtFrame) {
			acc -= dtFrame;
			updatePad();
			g_core.run();
			if (g_wave) {
				if (g_waveDead) {
					g_waveDead = false;
					audioClose();
				} else {
					audioFlushStaging();
				}
			}
		}

		// Dorme ate' o proximo quadro.
		const DWORD ms = (DWORD)(acc * 1000.0);
		if (ms) {
			Sleep(temPeriod ? ms : ms > 10 ? 10 : ms);
		} else {
			Sleep(0);
		}
	}

	logf("saindo\n");
	if (g_kbHook) {
		UnhookWindowsHookEx(g_kbHook);
	}
	if (g_wave) {
		audioClose();
	}
	if (temPeriod) {
		timeEndPeriod(1);
	}
	g_core.unload_game();
	g_core.deinit();
	if (g_dib) {
		DeleteObject(g_dib);
		g_dib = nullptr;
	}
	FreeLibrary(g_core.dll);
	if (g_log) {
		fclose(g_log);
		g_log = nullptr;
	}
	return 0;
}
