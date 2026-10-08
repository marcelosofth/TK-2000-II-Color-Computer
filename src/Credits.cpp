#include "pch.h"

#include "Credits.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "credits_music_tk.h"
#include "credits_bg.h"
#include "credits_karateka.h"
#include "credits_photo.h"

namespace {

// --- Geometry (280x192 frame) ---
// Deliberately the same box as the SELECT tape picker (FilePicker.cpp), so the
// two windows read as one UI: same inset, same size, same colours.
constexpr int kPanelX = 8;
constexpr int kPanelY = 8;
constexpr int kPanelW = VIDEOWIDTH - 16;	// 264
constexpr int kPanelH = VIDEOHEIGHT - 16;	// 176
constexpr int kTitleH = 11;
// Sem rodape: o texto desce ate quase a borda, com 3 px de respiro.
constexpr int kBottomPad = 3;

// The row that scrolls off the top dissolves to nothing over kFadeH pixels.
// kFadeTop is the first row BELOW the title bar, so the text has already faded
// away before it can reach the blue bar. Fading rather than hard-clipping: a
// hard edge across the middle of a line reads as a mistake, not as a scroll.
constexpr int kFadeTop = kPanelY + kTitleH;
	
// 19
constexpr int kFadeH = 10;
	
// kRowH, so exactly one row is in mid-fade

// The artwork is drawn at 85% over the solid panel colour: 15% transparent.
constexpr int kArtOpacity = 256;	// 100% opaco: a arte entra como esta no arquivo
	
// 0.85 * 256

constexpr int kContentY = kPanelY + kTitleH + 2;
constexpr int kContentH = kPanelH - kTitleH - 2 - kBottomPad;	// 160
constexpr int kMargin = 6;
constexpr int kCharW = 6;	// 5px glyph + 1px gap
constexpr int kCharH = 7;
constexpr int kRowH = 10;	// 7px glyph + 3px leading
// The scrolling text, 20% smaller than the title: 4x6 glyph, 5px advance,
// 8px rows. 4/5 = 80% of the width, 5/6 = 83% of the advance, 8/10 = 80% of
// the line height. With a 5px advance more characters fit per line, so
// kMaxChars below is larger than the 42 the 5x7 allowed.
constexpr int kSmallCharW = 5;	// 4px glyph + 1px gap
constexpr int kSmallCharH = 6;
constexpr int kSmallRowH = 8;	// 6px glyph + 2px leading
constexpr int kInnerW = kPanelW - 2 * kMargin;
constexpr int kMaxChars = kInnerW / kSmallCharW;	// 50

// Velocidade de rolagem: o texto sobe 1 pixel a cada kFramesPerPx quadros
// (60 fps / 5 = 12 px/s). E' um numero INTEIRO de quadros por pixel de
// proposito: com velocidade em ponto fixo (ex.: 0,246 px/quadro) o intervalo
// entre um pixel e o seguinte alternava entre 5 e 6 quadros e a rolagem
// "engasgava". Com intervalo fixo todos os passos sao iguais. A velocidade
// tambem NAO muda mais ao fechar a janela: antes, cada fecho cortava 30% e a
// segunda abertura subia mais lenta e aos trancos.
constexpr int kFramesPerPx = 5;

// --- Colours (the picker's palette) ---
constexpr SRGB kBorder{ 255, 255, 255 };
constexpr SRGB kTitleBg{ 0, 80, 200 };
constexpr SRGB kBg{ 0, 0, 70 };
constexpr SRGB kText{ 255, 255, 255 };
// Green, like the equaliser in the gme2 window, for section titles and rules.
constexpr SRGB kAccent{ 90, 255, 90 };
// Red, for the Karateka section title. Chosen to stay readable over the moon
// and the sky, which are the brighter parts of the artwork: full 255 red on
// 255 white is invisible, and the artwork already sits under 40.
constexpr SRGB kWarn{ 235, 70, 70 };

// --- 5x7 font, 5 bits/row (bit4 = left), top row first ---
// A copy of the table in FilePicker.cpp. Kept local so this window does not
// depend on the picker's internals (they are file-static there); if you change
// one, change the other.
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
	// Til e cedilha, os dois unicos acentuados que o texto usa. As chaves sao os
	// bytes latin-1 0xD5 (O com til) e 0xC7 (C com cedilha), e o texto escreve
	// os mesmos valores em octal, de modo que o arquivo fonte fica em ASCII
	// puro: nao depende de como o editor salva, nem do charset do compilador.
	//
	// Os dois glifos sao a letra comprimida em 5 linhas, com o acento ocupando
	// as linhas que sobraram: o til vai em cima (linha 0, linha 1 vazia) e o
	// cedilha embaixo (linhas 5 e 6). O corpo do O e do C perde uma linha em
	// relacao aos outros glifos, o que e o preco de caber num acento na altura
	// de 7 pixels. Numa fonte maior cada letra usaria 8 linhas.
	{ (char)0xD5, {0b01110,0b00000,0b01110,0b10001,0b10001,0b10001,0b01110} },
	{ (char)0xC7, {0b01110,0b10001,0b10000,0b10000,0b01110,0b00100,0b01000} },
	// As quatro setas do D-pad. As chaves sao os bytes 0x80..0x83, da faixa de
	// controle C1: nunca aparecem em texto normal, entao nao colidem com letra
	// acentuada nem com o dobramento UTF-8 do seletor de fitas.
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

// --- 4x6 font, 4 bits/row (bit3 = left), top row first ---
// A fonte do texto que sobe, 20% menor que a 5x7 de cima: glifo 4x6 em vez
// de 5x7, avanco de 5 px em vez de 6, linha de 8 px em vez de 10. Cada
// glifo foi redesenhado a mao, nao reduzido por escala: numa 5x7 as
// hastes diagonais do A, do M, do N e do W dependem de pixels que uma
// 4x6 nao tem onde cair. Verificada linha a linha contra o texto real.
// Gerado por emitir46.py; rode o script de novo em vez de editar.
struct SSmallGlyph { char ch; byte rows[6]; };
const SSmallGlyph kSmallFont[] = {
	{ '!', {0b0010,0b0010,0b0010,0b0010,0b0000,0b0010} },
	{ '#', {0b0101,0b0101,0b1111,0b0101,0b1111,0b0101} },
	{ '%', {0b1001,0b0001,0b0010,0b0010,0b0100,0b1001} },
	{ '&', {0b0110,0b1001,0b0110,0b0110,0b1001,0b0110} },
	{ '\'', {0b0010,0b0010,0b0100,0b0000,0b0000,0b0000} },
	{ '(', {0b0001,0b0010,0b0100,0b0100,0b0010,0b0001} },
	{ ')', {0b1000,0b0100,0b0010,0b0010,0b0100,0b1000} },
	{ '*', {0b0000,0b1010,0b0111,0b1010,0b0000,0b0000} },
	{ '+', {0b0000,0b0010,0b0111,0b0010,0b0000,0b0000} },
	{ ',', {0b0000,0b0000,0b0000,0b0000,0b0110,0b0010} },
	{ '-', {0b0000,0b0000,0b1111,0b0000,0b0000,0b0000} },
	{ '.', {0b0000,0b0000,0b0000,0b0000,0b0000,0b0110} },
	{ '/', {0b0001,0b0001,0b0010,0b0100,0b1000,0b1000} },
	{ '0', {0b0110,0b1001,0b1001,0b1001,0b1001,0b0110} },
	{ '1', {0b0010,0b0110,0b0010,0b0010,0b0010,0b0111} },
	{ '2', {0b0110,0b1001,0b0001,0b0010,0b0100,0b1111} },
	{ '3', {0b1110,0b0001,0b0110,0b0001,0b0001,0b1110} },
	{ '4', {0b1001,0b1001,0b1111,0b0001,0b0001,0b0001} },
	{ '5', {0b1111,0b1000,0b1110,0b0001,0b0001,0b1110} },
	{ '6', {0b0110,0b1000,0b1110,0b1001,0b1001,0b0110} },
	{ '7', {0b1111,0b0001,0b0010,0b0010,0b0100,0b0100} },
	{ '8', {0b0110,0b1001,0b0110,0b1001,0b1001,0b0110} },
	{ '9', {0b0110,0b1001,0b1001,0b0111,0b0001,0b0110} },
	{ ':', {0b0000,0b0010,0b0000,0b0010,0b0000,0b0000} },
	{ ';', {0b0000,0b0010,0b0000,0b0000,0b0010,0b0100} },
	{ '<', {0b0001,0b0010,0b0100,0b1000,0b0100,0b0001} },
	{ '=', {0b0000,0b0000,0b1111,0b0000,0b1111,0b0000} },
	{ '>', {0b1000,0b0100,0b0010,0b0001,0b0010,0b1000} },
	{ '?', {0b0110,0b1001,0b0001,0b0010,0b0000,0b0010} },
	{ '@', {0b0110,0b1001,0b1011,0b1010,0b1000,0b0110} },
	{ 'A', {0b0110,0b1001,0b1111,0b1001,0b1001,0b1001} },
	{ 'B', {0b1110,0b1001,0b1110,0b1001,0b1001,0b1110} },
	{ 'C', {0b0110,0b1001,0b1000,0b1000,0b1001,0b0110} },
	{ 'D', {0b1110,0b1001,0b1001,0b1001,0b1001,0b1110} },
	{ 'E', {0b1111,0b1000,0b1110,0b1000,0b1000,0b1111} },
	{ 'F', {0b1111,0b1000,0b1110,0b1000,0b1000,0b1000} },
	{ 'G', {0b0110,0b1001,0b1000,0b1011,0b1001,0b0111} },
	{ 'H', {0b1001,0b1001,0b1111,0b1001,0b1001,0b1001} },
	{ 'I', {0b0111,0b0010,0b0010,0b0010,0b0010,0b0111} },
	{ 'J', {0b0011,0b0001,0b0001,0b0001,0b1001,0b0110} },
	{ 'K', {0b1001,0b1010,0b1100,0b1010,0b1001,0b1001} },
	{ 'L', {0b1000,0b1000,0b1000,0b1000,0b1000,0b1111} },
	{ 'M', {0b1001,0b1111,0b1111,0b1001,0b1001,0b1001} },
	{ 'N', {0b1001,0b1101,0b1011,0b1001,0b1001,0b1001} },
	{ 'O', {0b0110,0b1001,0b1001,0b1001,0b1001,0b0110} },
	{ 'P', {0b1110,0b1001,0b1110,0b1000,0b1000,0b1000} },
	{ 'Q', {0b0110,0b1001,0b1001,0b1011,0b1010,0b0110} },
	{ 'R', {0b1110,0b1001,0b1110,0b1010,0b1001,0b1001} },
	{ 'S', {0b0111,0b1000,0b0110,0b0001,0b0001,0b1110} },
	{ 'T', {0b1111,0b0010,0b0010,0b0010,0b0010,0b0010} },
	{ 'U', {0b1001,0b1001,0b1001,0b1001,0b1001,0b0110} },
	{ 'V', {0b1001,0b1001,0b1001,0b1001,0b0110,0b0110} },
	{ 'W', {0b1001,0b1001,0b1001,0b1111,0b1111,0b1001} },
	{ 'X', {0b1001,0b1001,0b0110,0b0110,0b1001,0b1001} },
	{ 'Y', {0b1001,0b1001,0b0110,0b0010,0b0010,0b0010} },
	{ 'Z', {0b1111,0b0001,0b0010,0b0100,0b1000,0b1111} },
	{ '[', {0b0111,0b0100,0b0100,0b0100,0b0100,0b0111} },
	{ ']', {0b1110,0b0010,0b0010,0b0010,0b0010,0b1110} },
	{ '_', {0b0000,0b0000,0b0000,0b0000,0b0000,0b1111} },
	{ '|', {0b0010,0b0010,0b0010,0b0010,0b0010,0b0010} },
	{ (char)0x80, {0b00000,0b00110,0b01111,0b11111,0b01111,0b00110} },
	{ (char)0x81, {0b00000,0b01100,0b11110,0b11111,0b11110,0b01100} },
	{ (char)0x82, {0b00100,0b11111,0b01110,0b01110,0b00100,0b00100} },
	{ (char)0x83, {0b00100,0b00100,0b01110,0b01110,0b11111,0b00100} },
	{ (char)0xC7, {0b0110,0b1001,0b1000,0b0110,0b0001,0b0110} },
	{ (char)0xD5, {0b0110,0b0000,0b0110,0b1001,0b1001,0b0110} },
};
constexpr int kSmallFontCount = sizeof(kSmallFont) / sizeof(kSmallFont[0]);
















// Cada linha de kCreditsLines[] comeca com uma letra de tipo, que fica FORA do
// texto: 'R' regra, 'G' titulo verde, 'V' titulo vermelho, 'P' normal. A cor
// vem daqui, e nao de "a linha logo depois de uma regra e' titulo" como era
// antes: com essa regra a linha vazia que vinha logo apos um titulo tambem
// virava titulo, e como ela nao tem texto o espaco que ela criava sumia.
struct SLineKind {
	char kind;
	std::string text;
};

SLineKind splitLine(const char* line) {
	SLineKind k;
	k.kind = line[0];
	k.text = line + 1;
	return k;
}

const byte* findGlyph(char ch) {
	for (int i = 0; i < kFontCount; i++) {
		if (kFont[i].ch == ch) {
			return kFont[i].rows;
		}
	}
	return nullptr;
}

const byte* findSmallGlyph(char ch) {
	for (int i = 0; i < kSmallFontCount; i++) {
		if (kSmallFont[i].ch == ch) {
			return kSmallFont[i].rows;
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


// Alpha is 0..256, with 256 fully opaque. Blending against whatever is already
// on the frame is what lets a glyph dissolve into the artwork, and what lets the
// artwork sit at 15% transparency over the solid panel colour.
inline void blendPixel(SRGB* fb, int x, int y, SRGB c, int a) {
	if (x < 0 || x >= VIDEOWIDTH || y < 0 || y >= VIDEOHEIGHT) {
		return;
	}
	SRGB& d = fb[y * VIDEOWIDTH + x];
	if (a >= 256) {
		d = c;
		return;
	}
	const int inv = 256 - a;
	d.red   = (byte)((d.red   * inv + c.red   * a) >> 8);
	d.green = (byte)((d.green * inv + c.green * a) >> 8);
	d.blue  = (byte)((d.blue  * inv + c.blue  * a) >> 8);
}

void fillRectAlpha(SRGB* fb, int x0, int y0, int w, int h, SRGB c, int a) {
	for (int y = y0; y < y0 + h; y++) {
		for (int x = x0; x < x0 + w; x++) {
			blendPixel(fb, x, y, c, a);
		}
	}
}

// w = largura do glifo, h = altura, avanco = pixels por caractere, achar = busca
// na fonte pedida. Os dois tamanhos de fonte convivem: a 5x7 para o titulo da
// tarja, a 4x6 para o texto que sobe. Passar tudo por parametro e o que evita
// duas copias quase iguais deste laco.
// The four D-pad arrows are 5 columns wide, one more than the other glyphs.
// The cell is a 5 px advance, so a 5-column glyph fills it exactly and the next
// character (always a space after an arrow) is the separator: the '=' column does
// not move. With 4 columns there is no room for the V-shaped head to be
// separate from the shaft, and the drawing turns into a directionless blob.
inline bool isArrow(char ch) {
	return ch == (char)0x80 || ch == (char)0x81 || ch == (char)0x82 || ch == (char)0x83;
}

void drawText(SRGB* fb, int x, int y, const std::string& text, SRGB color, int a,
			  int w, int h, int avanco, const byte* (*achar)(char)) {
	for (const char raw : text) {
		const char ch = (char)std::toupper((unsigned char)raw);
		if (ch != ' ') {
			const byte* rows = achar(ch);
			if (!rows) {
				rows = achar('?');
			}
			// The arrows carry 5 bits per row; everything else carries 4. The
			// shift has to match, or the arrow is drawn shifted left by one
			// column and its point lands in the wrong place.
			const int gw = isArrow(ch) ? w + 1 : w;
			for (int gy = 0; gy < h; gy++) {
				for (int gx = 0; gx < gw; gx++) {
					if (rows[gy] & (1 << (gw - 1 - gx))) {
						blendPixel(fb, x + gx, y + gy, color, a);
					}
				}
			}
		}
		x += avanco;
	}
}

// Atalho para o texto grande (titulo da tarja).
void drawTextBig(SRGB* fb, int x, int y, const std::string& text, SRGB color, int a = 256) {
	drawText(fb, x, y, text, color, a, 5, kCharH, kCharW, findGlyph);
}

// Atalho para o texto pequeno (o que sobe).
void drawTextSmall(SRGB* fb, int x, int y, const std::string& text, SRGB color, int a = 256) {
	drawText(fb, x, y, text, color, a, 4, kSmallCharH, kSmallCharW, findSmallGlyph);
}

int textWidth(const std::string& text) {
	return text.empty() ? 0 : (int)text.size() * kCharW - 1;
}

// Uma linha marcada 'R' e' uma regra: desenhada como uma barra cheia no lugar
// dos 50 glifos de '=', que e' o que a janela do gme2 faz com as linhas "=====".
// A barra e' desenhada por draw(), nao aqui; aqui so recognize a marca.

// --- The credits text ---
// One string per line, in scroll order; "" is a blank line. Long lines are
// wrapped at kMaxChars (50, which the 4x6 font fits). A run of 4+ "=" is a
// section rule, drawn as one solid bar instead of 50 glyphs.
//
// The 4x6 font has only two accented glyphs, so the text is written with just
// the accents it can actually render: c-cedilla and o-tilde, in the TK2000
// history paragraph. The rest of the Portuguese is unaccented on purpose.
// The source file stays pure ASCII: those two accents are octal escapes
// (\307 and \325) for the same reason the title uses them -- an \x escape
// would swallow the hex digits that follow it.
// Edit freely; texto_creditos.py regenerates this block from Python.


const char* const kCreditsLines[] = {
	"R==================================================",
	"GUSE COM JOYSTICK PADRAO PC/XBOX",
	"R==================================================",
	"GMAPEAMENTO GERAL:",
	"PD-pad \200     = Esquerda",
	"PD-pad \201     = Direita",
	"PD-pad \203     = Cima",
	"PD-pad \202     = Baixo",
	"PBotao X     = Creditos",
	"PBotao A     = Botao 1",
	"PBotao B     = Botao 2",
	"PBotao START = Teclado Virtual",
	"PBotao SELECT= Escolher disco 2",
	"PBotao L1    = Reset",
	"PBotao R1    = Acelerar carregamento",
	"PBotao L2    = Play / Pause da fita",
	"PBotao R2    = Play / Pause da fita",
	"P",
	"PSCROLL LOCK = Ativa Teclado TK 2000",
	"P",
	"VMAPEAMENTO ESPECIAL (KARATEKA)",
	"PD-pad \200     = Esquerda",
	"PD-pad \201     = Direita",
	"PD-pad \202     = Posicao de luta",
	"PD-pad \203     = Cumprimentar",
	"PBotao X     = Soco baixo",
	"PBotao Y     = Soco alto",
	"PBotao A     = Chute baixo",
	"PBotao B     = Chute alto",
	"PBotao L     = Soco medio",
	"PBotao R     = Chute medio",
	"PBotao START = Teclado virtual",
	"PBotao SELECT= Escolher disco 2",
	"PBotao L2    = Acelerar carregamento",
	"PBotao R2    = Creditos (ombro)",
	"P",
	"R==================================================",
	"GSUPORTE PARA CARREGAMENTO DE ARQUIVOS:",
	"P.CT2 (Botao do Joystick para Carregar o disco 2)",
	"P.DSK (COMPATIBOOT)",
	"P",
	"R==================================================",
	"P",
	"PO TK2000, produzido pela empresa brasileira",
	"PMicrodigital Eletronica Ltda, foi um micro-",
	"Pcomputador apresentado ao publico durante a",
	"PFeira de Informatica de 1983 e lancado em",
	"P1984. Utilizava a CPU 6502 e era, na verdade,",
	"Pum clone do taiwanes Micro-Professor MPF-II",
	"Pfabricado pela Multitech, antecessora da Acer.",
	"PEra parcialmente compativel com o software e",
	"Po hardware do Apple II+.",
	"P",
	"PEm 1985 foi lancada uma versao aperfei\307oada",
	"Pcom op\307ao de 128K, o TK2000 II. Em 1987 o",
	"PTK2000 foi descontinuado.",
	"P",
	"R==================================================",
	"P",
	"Pby Marcelo Tavares (2026)",
	"Pmarcelosofth@gmail.com",
	"R==================================================",
};
constexpr int kCreditsLineCount = sizeof(kCreditsLines) / sizeof(kCreditsLines[0]);

// A musica esta em base64 no header porque em array hex seriam ~5,3 MB de
// codigo-fonte. A tabela abaixo e o alfabeto de base64, com -1 em "nao eh
// base64" (o que tambem descarta as quebras de linha do literal).
const signed char kB64[256] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// 00-0F
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// 10-1F
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,	// 20-2F: '+' = 62, '/' = 63
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,	// 30-3F: '0'..'9'
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,	// 40-4F: 'A'..'O'
	15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,	// 50-5A: 'P'..'Z'
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,	// 61-6F: 'a'..'o'
	41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,	// 70-7A: 'p'..'z'
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

// IMA ADPCM 4 bits, as mesmas tabelas que o gme2 usa (libretro.c:1643).
const int kIndexTab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
const int16_t kStepTab[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// Um nibble -> uma amostra. Mesma conta do gme2: multiplica antes de deslocar.
inline int16_t adpcmExpand(int nib, int& pred, int& idx) {
	const int step = kStepTab[idx];
	int diff = ((2 * (nib & 7) + 1) * step) >> 3;
	if (nib & 8) {
		diff = -diff;
	}
	pred += diff;
	if (pred > 32767) {
		pred = 32767;
	} else if (pred < -32768) {
		pred = -32768;
	}
	idx += kIndexTab[nib];
	if (idx < 0) {
		idx = 0;
	} else if (idx > 88) {
		idx = 88;
	}
	return (int16_t)pred;
}

} // namespace

/*************************************************************************************************/

void CCredits::buildRows() {
	mRows.clear();

	for (int i = 0; i < kCreditsLineCount; i++) {
		const SLineKind src = splitLine(kCreditsLines[i]);
		// A cor vem da marca, nao da posicao. Uma linha vazia e' sempre normal,
		// seja qual for a letra: e' o que garante que o espaco entre um titulo e
		// o texto seguinte exista.
		const EColor cor = src.kind == 'V' ? EColor::RED
			: (src.kind == 'G' ? EColor::GREEN : EColor::WHITE);

		if (src.text.size() > (size_t)kMaxChars) {
			// Wrap on spaces, so words stay whole.
			size_t at = 0;
			while (at < src.text.size()) {
				size_t take = std::min((size_t)kMaxChars, src.text.size() - at);
				if (at + take < src.text.size()) {
					const size_t sp = src.text.rfind(' ', at + take);
					if (sp != std::string::npos && sp > at) {
						take = sp - at;
					}
				}
				mRows.push_back({ src.text.substr(at, take), 0, cor, false });
				at += take;
				while (at < src.text.size() && src.text[at] == ' ') {
					at++;
				}
			}
		} else {
			mRows.push_back({ src.text, 0, cor, src.kind == 'R' });
		}
	}

	// Now that every row exists, give each one its vertical offset.
	for (size_t i = 0; i < mRows.size(); i++) {
		mRows[i].y = (int)i * kSmallRowH;
	}
	// The sprite rides with the row that names KARATEKA, so it is only on
	// screen during that section. That is also what keeps it clear of the long
	// lines further down: the widest line of the whole text is 50 chars = 250
	// px and the sprite sits at x 231, so it would collide with those.
	mSpriteRowY = -1;
	for (size_t i = 0; i < mRows.size(); i++) {
		if (mRows[i].text.find("KARATEKA") != std::string::npos) {
			mSpriteRowY = mRows[i].y;
			break;
		}
	}
	mContentH = (int)mRows.size() * kSmallRowH;
	// The photo goes after the last row, with a blank line in between so it does
	// not start right under the closing rule. Its own height, not a row height:
	// it is an image, and giving it kSmallRowH would overlap the line above.
	mPhotoY = mContentH + kSmallRowH;
	mContentH = mPhotoY + PHOTO_H;
	// The period is the block PLUS the window, and one copy is enough.
	//
	// Com a ancora "entra por baixo" (o bloco comeca na base da janela), o
	// bloco esta em tela durante exatamente bloco + janela pixels de rolagem --
	// entra pelos 160 px de baixo e sai pelos 160 px de cima. Esse e' o periodo
	// certo, e nao ha buraco: quando a ultima linha (a foto) chega ao topo, a
	// primeira linha do proximo ciclo ja esta entrando por baixo.
	//
	// O periodo antigo era 2x o bloco, que deixava uma faixa vazia de
	// 2*bloco - (bloco + janela) = bloco - janela = 413 px, uns 28 s sem
	// nada na tela. Com a foto, o bloco passou de 440 para 573 px e a falha
	// ficou visivel.
	mTravel = mContentH + kContentH;
}

/*************************************************************************************************/

void CCredits::open() {
	mOpen = true;
	mPaused = false;
	mY = 0;
	mScrollTick = 0;
	mBlock = 0;
	mPos = 0;
	mSpriteFrame = 0;
	mSpriteTimer = 0;
	loadSprite();
	loadPhoto();
	buildRows();
}

/*************************************************************************************************/

void CCredits::close() {
	mOpen = false;
}

/*************************************************************************************************/

void CCredits::loadSprite() {
	if (!mSpriteRle.empty()) {
		return;
	}
	// Mesmo alfabeto base64 da musica. O stream e' um RLE plano: pares
	// (contagem, indice de cor), sem cabecalho por quadro -- o tamanho do quadro
	// esta no #define, entao da para pular de um para o outro por conta.
	const size_t inLen = std::strlen(karateka_rle_b64);
	mSpriteRle.clear();
	mSpriteRle.reserve(inLen / 4 * 3 + 4);
	int acc = 0;
	int bits = 0;
	for (size_t i = 0; i < inLen; i++) {
		const signed char v = kB64[(unsigned char)karateka_rle_b64[i]];
		if (v < 0) {
			continue;		// '=' e as quebras de linha
		}
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			mSpriteRle.push_back((unsigned char)((acc >> bits) & 0xFF));
		}
	}
	mSpriteFrame = 0;
	mSpriteTimer = 0;
}

/*************************************************************************************************/

void CCredits::drawSprite(SRGB* fb, int x, int y, int fadeTop, int clipBottom) const {
	const int npix = KARATEKA_W * KARATEKA_H;
	// Descomprime o quadro corrente uma vez num buffer, e desenha linha a linha
	// com o alfa daquela linha. O alfa por linha e' o que faz o sprite sumir
	// gradualmente em cima, igual ao texto: se fosse um alfa so para o sprite
	// inteiro, ele apareceria inteiro ou nada, e nao desapareceria antes de
	// tocar a tarja azul.
	mSpritePix.assign((size_t)npix, 0);
	// A tabela de offsets e' obrigatoria: cada quadro ocupa um numero diferente
	// de bytes, porque o RLE comprime e o desenho muda de quadro para quadro
	// (326 a 570 bytes aqui, contra 2940 sem compressao). Assumir um tamanho
	// fixo faz o leitor comecar no meio do quadro errado -- o sprite sai
	// cortado e, depois de alguns quadros, o offset passa do fim do stream e
	// ele some de vez.
	if (mSpriteFrame < 0 || mSpriteFrame >= KARATEKA_FRAMES) {
		return;
	}
	const size_t offset = karateka_frame_off[mSpriteFrame];
	if (offset >= mSpriteRle.size()) {
		return;
	}
	// Percorre os pares (contagem, cor) a partir do offset do quadro. O fim e'
	// o offset do proximo quadro, ou o fim do stream no ultimo.
	const size_t fim = (mSpriteFrame + 1 < KARATEKA_FRAMES)
		? (size_t)karateka_frame_off[mSpriteFrame + 1] : mSpriteRle.size();
	size_t i = offset;
	int p = 0;
	while (p < npix && i + 1 < fim) {
		const unsigned char n = mSpriteRle[i];
		const unsigned char c = mSpriteRle[i + 1];
		i += 2;
		if (c >= KARATEKA_PALETTE) {
			p += n;		// cor invalida: pula os pixels, nao trava
			continue;
		}
		for (int k = 0; k < n && p < npix; k++) {
			mSpritePix[p++] = c;
		}
	}
	for (int py = 0; py < KARATEKA_H; py++) {
		const int ly = y + py;
		if (ly < fadeTop || ly >= clipBottom) {
			continue;
		}
		int a = (ly - fadeTop) * 256 / kFadeH;
		if (a > 256) {
			a = 256;
		}
		if (a <= 0) {
			continue;
		}
		const unsigned char* linha = &mSpritePix[(size_t)py * KARATEKA_W];
		for (int px = 0; px < KARATEKA_W; px++) {
			const unsigned char* rgb = karateka_palette[linha[px]];
			blendPixel(fb, x + px, ly, SRGB{ rgb[0], rgb[1], rgb[2] }, a);
		}
	}
}

/*************************************************************************************************/

void CCredits::loadPhoto() {
	if (!mPhoto.empty()) {
		return;
	}
	// Mesmo alfabeto base64 da musica e do sprite.
	const size_t inLen = std::strlen(credits_photo_b64);
	mPhoto.clear();
	mPhoto.reserve(inLen / 4 * 3 + 4);
	int acc = 0;
	int bits = 0;
	for (size_t i = 0; i < inLen; i++) {
		const signed char v = kB64[(unsigned char)credits_photo_b64[i]];
		if (v < 0) {
			continue;		// '=' e as quebras de linha
		}
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			mPhoto.push_back((unsigned char)((acc >> bits) & 0xFF));
		}
	}
}

/*************************************************************************************************/

void CCredits::drawPhoto(SRGB* fb, int x, int y, int fadeTop, int clipBottom) const {
	const size_t need = (size_t)PHOTO_W * PHOTO_H * 3;
	if (mPhoto.size() < need) {
		return;		// payload incompleto: melhor nada do que lixo
	}
	for (int py = 0; py < PHOTO_H; py++) {
		const int ly = y + py;
		if (ly < fadeTop || ly >= clipBottom) {
			continue;
		}
		int a = (ly - fadeTop) * 256 / kFadeH;
		if (a > 256) {
			a = 256;
		}
		if (a <= 0) {
			continue;
		}
		const unsigned char* linha = &mPhoto[(size_t)py * PHOTO_W * 3];
		for (int px = 0; px < PHOTO_W; px++) {
			blendPixel(fb, x + px, ly,
				SRGB{ linha[px * 3], linha[px * 3 + 1], linha[px * 3 + 2] }, a);
		}
	}
}

/*************************************************************************************************/

void CCredits::update(const SInput& in) {
	if (!mOpen) {
		return;
	}
	if (in.close) {
		close();
		return;
	}
	if (in.togglePause) {
		mPaused = !mPaused;
	}

	// The sprite animates on the wall clock, and this happens BEFORE the pause
	// check on purpose: pausing is for reading the text, and a frozen karateka
	// looks like a crash. The kata keeps moving while the text stands still.
	//
	// The delay is in centiseconds and one frame is 1/60 s, so the countdown is
	// KARATEKA_DELAY_CS * 6 / 10 frames. Integer maths, so it drifts a little
	// over a full 1,74 s loop -- not worth carrying a timebase for a sprite.
	if (mSpriteTimer > 0) {
		mSpriteTimer--;
	} else {
		mSpriteTimer = KARATEKA_DELAY_CS * 6 / 10 - 1;
		mSpriteFrame++;
		if (mSpriteFrame >= KARATEKA_FRAMES) {
			mSpriteFrame = 0;
		}
	}

	// Paused: the text and the photo hold their position. The music is not
	// touched here either -- it runs off in fillAudio(), by however many samples
	// are actually sent -- so pausing reads, it does not silence.
	if (mPaused) {
		return;
	}
	if (mTravel > 0) {
		// Um pixel inteiro a cada kFramesPerPx quadros (ver o comentario da constante).
		if (++mScrollTick >= kFramesPerPx) {
			mScrollTick = 0;
			mY += 256;
		}
		// Roll over and start again once the text has gone past the top.
		while (mY >> 8 >= mTravel) {
			mY -= mTravel << 8;
		}
	}
}

/*************************************************************************************************/

void CCredits::draw(SRGB* fb) const {
	if (!mOpen) {
		return;
	}

	// Frame, then a solid fill, then the artwork over it. The artwork's colours
	// are used exactly as they are in the file -- the generator applies no gamma
	// and no gain, so what is in fundo.png is what arrives here. The solid fill
	// matters: if the artwork ever failed to cover the panel, whatever the
	// emulated machine left on the frame would show through as garbage instead
	// of as a clean colour. The image is exactly kPanelW x kPanelH, so it lands
	// 1:1 with no scaling.
	//
	// Sem mistura nenhuma: kArtOpacity e 256, entao o pixel da arte substitui o
	// do painel. A transparencia de 15% que existia antes era destrutiva nesta
	// imagem: como ela e quase toda preta, o azul do painel virava um veu que
	// achatava o contraste. Medido: o p90 do fundo caia de 22.7 para 10.0 e o
	// p95 de 28.1 para 10.9 -- e' isso que sumia com os detalhes do castelo. A
	// media quase nao mudava (8.0 para 7.7), que e' por isso que media engana
	// aqui: o estrago esta no contraste, nao no nivel.
	// mean luminance from 8.0 to 7.7 -- below a pixel of difference, so it
	// neither washes out nor crushes the picture.
	fillRect(fb, kPanelX - 1, kPanelY - 1, kPanelW + 2, kPanelH + 2, kBorder);
	fillRect(fb, kPanelX, kPanelY, kPanelW, kPanelH, kBg);
	for (int y = 0; y < CREDITS_BG_H_ && y < kPanelH; y++) {
		for (int x = 0; x < CREDITS_BG_W && x < kPanelW; x++) {
			const size_t o = ((size_t)y * CREDITS_BG_W + x) * 3;
			blendPixel(fb, kPanelX + x, kPanelY + y, SRGB{
				credits_bg_rgb[o], credits_bg_rgb[o + 1], credits_bg_rgb[o + 2]
			}, kArtOpacity);
		}
	}
	// Title bar stays opaque: it has to read over the artwork.
	fillRect(fb, kPanelX, kPanelY, kPanelW, kTitleH, kTitleBg);
	// \307 = C com cedilha, \325 = O com til, em octal latin-1. Octal e nao hex
	// de proposito: um escape \x em C++ come todos os digitos hex que vierem
	// depois, entao "\xC7\xD5ES" viraria um unico escape invalido de 3 digitos.
	// Em octal cada um para em 3 digitos, o que resolve o problema.
	const std::string title = "TK 2000 II - INSTRU\307\325ES";
	// The title keeps the 5x7 font: it is a single short line, so it can afford
	// the larger glyphs, and the scrolling text below is what needed to shrink.
	drawTextBig(fb, kPanelX + (kPanelW - textWidth(title)) / 2, kPanelY + 2, title, kText);

	// The text rolls up from the bottom of the box, as it always did: the
	// block's first line starts at the base of the content area and rises. One
	// copy is enough because mTravel is the block plus the window -- see the
	// note next to mTravel in buildRows() for why the old 2x-block period left
	// a hole.
	const int offset = mY >> 8;
	const int base = kContentY + kContentH - offset;
	const int fadeTop = kFadeTop;
	const int bottom = kContentY + kContentH;

	// The karateka sprite, riding with the row that names KARATEKA. Drawn from
	// the same y as that row, with the same fade and the same clip, so it
	// dissolves and gets cut where the text does -- that is what makes it look
	// like one thing rising instead of a sprite pasted on top. On the right,
	// clear of the text: the longest line of that section is 30 chars = 150 px,
	// and the sprite starts at x 231.
	if (mSpriteRowY >= 0) {
		const int sy = base + mSpriteRowY;
		if (sy < bottom && sy + KARATEKA_H > fadeTop) {
			drawSprite(fb, kPanelX + kPanelW - kMargin - KARATEKA_W, sy, fadeTop, bottom);
		}
	}

	// The photo, at the very end of the block, centred in the content box.
	// Centred because there is nothing to read next to it, and the content box
	// is 252 px wide against a 150 px photo: 51 px on each side.
	if (mPhotoY > 0) {
		const int py = base + mPhotoY;
		if (py < bottom && py + PHOTO_H > fadeTop) {
			drawPhoto(fb, kPanelX + (kPanelW - PHOTO_W) / 2, py, fadeTop, bottom);
		}
	}

	for (const SRow& r : mRows) {
		const int y = base + r.y;
		// Fade out over kFadeH pixels starting at kFadeTop, which is the first
		// row BELOW the title bar. At y == kFadeTop the alpha is still zero, so
		// the text is already gone before it could ever touch the blue bar.
		if (y < fadeTop || y + kSmallCharH > bottom) {
			continue;
		}
		int a = (y - fadeTop) * 256 / kFadeH;
		if (a > 256) {
			a = 256;
		}
		// The rule bar keeps the accent green whatever the row's colour is: a
		// rule is a divider, not text, and the green reads as a divider.
		if (r.rule) {
			fillRectAlpha(fb, kPanelX + kMargin, y + 3, kInnerW, 1, kAccent, a);
			continue;
		}
		SRGB cor = kText;
		if (r.color == EColor::GREEN) {
			cor = kAccent;
		} else if (r.color == EColor::RED) {
			cor = kWarn;
		}
		drawTextSmall(fb, kPanelX + kMargin, y, r.text, cor, a);
	}
}

/*************************************************************************************************/

void CCredits::loadMusic() {
	if (mMusicLoaded) {
		return;
	}
	mMusicLoaded = true;
	// O comprimento vem do proprio payload: 3 caracteres base64 -> 2 bytes.
	const size_t inLen = std::strlen(credits_tk_b64);
	mMusic.clear();
	mMusic.reserve(inLen / 4 * 3 + 4);
	int acc = 0;
	int bits = 0;
	for (size_t i = 0; i < inLen; i++) {
		const signed char v = kB64[(unsigned char)credits_tk_b64[i]];
		if (v < 0) {
			continue;		// '=' e as quebras de linha
		}
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			mMusic.push_back((unsigned char)((acc >> bits) & 0xFF));
		}
	}
	// Intercalado: [esq, dir] por amostra, como o gme2 entrega.
	mPcm.assign((size_t)CREDITS_TK_SPB * CREDITS_TK_CHANNELS, 0);
	mBlock = 0;
	mPos = 0;
}

/*************************************************************************************************/

void CCredits::decodeBlock() {
	if (mMusic.empty()) {
		return;
	}
	const unsigned char* p = &mMusic[(size_t)mBlock * CREDITS_TK_BLOCK];
	int pred[CREDITS_TK_CHANNELS];
	int idx[CREDITS_TK_CHANNELS];
	// Cabecalho de 4 bytes por canal: predictor int16, indice do passo, enchimento.
	for (int ch = 0; ch < CREDITS_TK_CHANNELS; ch++) {
		pred[ch] = (int16_t)(p[0] | (p[1] << 8));
		idx[ch] = p[2] > 88 ? 88 : p[2];
		p += 4;
		mPcm[ch] = (int16_t)pred[ch];
	}
	// Grupos de 8 amostras: 4 bytes do canal esquerdo, depois 4 do direito.
	for (int n = 1; n < CREDITS_TK_SPB; n += 8) {
		for (int ch = 0; ch < CREDITS_TK_CHANNELS; ch++) {
			for (int k = 0; k < 4; k++) {
				const unsigned char v = *p++;
				mPcm[(size_t)(n + k * 2) * CREDITS_TK_CHANNELS + ch] =
					adpcmExpand(v & 15, pred[ch], idx[ch]);
				mPcm[(size_t)(n + k * 2 + 1) * CREDITS_TK_CHANNELS + ch] =
					adpcmExpand(v >> 4, pred[ch], idx[ch]);
			}
		}
	}
	if (++mBlock >= CREDITS_TK_BLOCKS) {
		mBlock = 0;		// repete do comeco
	}
	mPos = 0;
}

/*************************************************************************************************/

void CCredits::fillAudio(int16_t* out, size_t frames) {
	if (!mOpen || !out || frames == 0) {
		return;
	}
	loadMusic();
	if (mMusic.empty()) {
		for (size_t i = 0; i < frames * CREDITS_TK_CHANNELS; i++) {
			out[i] = 0;
		}
		return;
	}
	// Escreve direto no buffer do frontend, ja intercalado: sem copia para mono
	// e sem duplicacao -- o que sai e o stereo do arquivo.
	for (size_t i = 0; i < frames; i++) {
		if (mPos >= CREDITS_TK_SPB) {
			decodeBlock();
		}
		const size_t s = (size_t)mPos * CREDITS_TK_CHANNELS;
		out[i * CREDITS_TK_CHANNELS + 0] = mPcm[s + 0];
		out[i * CREDITS_TK_CHANNELS + 1] = mPcm[s + 1];
		mPos++;
	}
}
