## TK2000 libretro core Makefile
## Ported from https://github.com/fbelavenuto/tk2000 (GPL-2.0).
## Follows the same conventions as the other cores in this collection
## (see the libretro-gme core's Makefile): platform auto-detection,
## *_libretro.<ext> naming, and DEBUG/fpic switches.

CORE_NAME := tk2000
TARGET_NAME := $(CORE_NAME)

SRC_DIR := src

SOURCES_CXX := \
	$(SRC_DIR)/Bus.cpp \
	$(SRC_DIR)/Cpu6502.cpp \
	$(SRC_DIR)/Rom.cpp \
	$(SRC_DIR)/LangCard.cpp \
	$(SRC_DIR)/Ram.cpp \
	$(SRC_DIR)/Video.cpp \
	$(SRC_DIR)/Audio.cpp \
	$(SRC_DIR)/Keyboard.cpp \
	$(SRC_DIR)/Tape.cpp \
	$(SRC_DIR)/Disk.cpp \
	$(SRC_DIR)/Joystick.cpp \
	$(SRC_DIR)/FilePicker.cpp \
	$(SRC_DIR)/Credits.cpp \
	$(SRC_DIR)/HostPaths.cpp \
	$(SRC_DIR)/ZipTape.cpp \
	$(SRC_DIR)/Machine.cpp \
	$(SRC_DIR)/libretro.cpp

OBJECTS := $(SOURCES_CXX:.cpp=.o)
DEPS := $(OBJECTS:.o=.d)

CXXFLAGS += -std=c++17 -Wall -fPIC -I$(SRC_DIR) -MMD -MP
LDFLAGS  += -shared -Wl,--no-undefined

ifeq ($(DEBUG), 1)
	CXXFLAGS += -O0 -g
else
	CXXFLAGS += -O2 -DNDEBUG
endif

# --- Frontend standalone para Windows (tk2000run.exe) ---
# O core so' expoe as funcoes retro_*; nao tem main(). Para rodar sem o RetroArch, o
# frontend/tk2000run.cpp carrega a DLL em tempo de execucao e cuida de janela, video, audio
# e teclado. Nao faz parte do core nem entra no link.T -- e' um programa separado.
#
#   make frontend            gera o .exe em standalone/
#   make frontend-selftest   roda sem janela e confere o caminho todo do core
#
# Precisa do mingw64 do MSYS2 no PATH (mingw64\bin), como o build do core.
#
# Este bloco vem DEPOIS da deteccao de plataforma de proposito: o teste de $(platform) abaixo
# precisa ver o valor ja' definido, senao a regra nunca existe e o make responde
# "Nothing to be done for 'frontend'" -- que e' o que aconteceu quando estava antes.

# --- Platform detection (mirrors the pattern used by other cores here) ---
ifeq ($(platform),)
	platform = unix
	ifeq ($(shell uname -a),)
	else ifneq ($(findstring MINGW,$(shell uname -a)),)
		platform = win
	else ifneq ($(findstring Darwin,$(shell uname -a)),)
		platform = osx
	else ifneq ($(findstring win,$(shell uname -a)),)
		platform = win
	endif
endif

TARGET := $(TARGET_NAME)_libretro.so
fpic := -fPIC

ifeq ($(platform), unix)
	TARGET := $(TARGET_NAME)_libretro.so
	fpic := -fPIC
	SHARED := -shared -Wl,--version-script=$(SRC_DIR)/link.T
else ifeq ($(platform), osx)
	TARGET := $(TARGET_NAME)_libretro.dylib
	fpic := -fPIC
	SHARED := -dynamiclib
else ifeq ($(platform), win)
	TARGET := $(TARGET_NAME)_libretro.dll
	fpic :=
	SHARED := -shared -static-libgcc -static-libstdc++ -Wl,--no-undefined -Wl,--version-script=$(SRC_DIR)/link.T
	CXX := $(CROSS_COMPILE)g++
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(fpic) $(SHARED) $(LDFLAGS) -o $@ $(OBJECTS)

# --- Frontend standalone (so' no Windows) ---
ifeq ($(platform), win)

# Nome da regra com o sufixo .exe e o mkdir do MSYS: sem os dois o make nem chega a compilar --
# falha em "mkdir: No such file or directory", que e' o mkdir do Windows procurando um executavel
# inexistente.
#
# Reparo: nenhuma linha deste bloco pode comecar por TAB fora das receitas. Dentro de um ifeq, uma
# linha indentada e' continuacao da receita da regra anterior, e nao uma atribuicao: com as
# variaveis indentadas, o make listava "frontend" no banco de regras mas respondia "Nothing to be
# done", e com as regras indentadas o alvo virava "/" -- os dois ja' aconteceram aqui.
FE_NAME := tk2000run.exe
FE_OUT  := standalone
MKDIR   := /usr/bin/mkdir.exe
FE_SRC  := frontend/tk2000run.cpp
FE_LIBS := -lwinmm -lshell32 -lgdi32 -luser32

.PHONY: frontend frontend-selftest

frontend: $(FE_OUT)/$(FE_NAME)

$(FE_OUT)/$(FE_NAME): $(FE_SRC) $(SRC_DIR)/libretro.h
	$(MKDIR) -p $(FE_OUT)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -static-libgcc -static-libstdc++ -I$(SRC_DIR) -o $@ $(FE_SRC) $(FE_LIBS)

# Roda o core 180 quadros sem janela e confere, por numeros, video/audio/input/teclado. O
# --bmp=45 grava o quadro 45 num BMP, que da' para ver a imagem sem depender de desktop.
frontend-selftest: $(FE_OUT)/$(FE_NAME)
	$(FE_OUT)/$(FE_NAME) --selftest --bmp=45

endif

%.o: %.cpp
	$(CXX) $(fpic) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

.PHONY: all clean

-include $(DEPS)
