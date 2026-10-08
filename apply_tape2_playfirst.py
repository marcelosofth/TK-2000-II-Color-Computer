#!/usr/bin/env python3
"""
apply_tape2_playfirst.py  -  TK2000 core: corrige a troca para a fita 2 (Karateka disco 2)

O que faz:
  1) Machine.h / Machine.cpp / libretro.cpp
     No auto-start do seletor de fitas, agora o PLAY é ligado NO MESMO QUADRO
     em que o ESPACO é "digitado" (como o jogo pede: "LIGUE O GRAVADOR E TECLE
     ESPACO"). Antes o ESPACO ia primeiro e o PLAY só ~30 quadros depois; nesse
     intervalo o loader do jogo lia $C010 sem fita rodando e se perdia.
  2) Tape.cpp
     Corrige o log "tape pulse N/total": o contador estático ficava preso no
     valor da fita 1 (~708499) e nunca mais logava na fita 2.

Uso (na pasta que contém a pasta src/ e o Makefile):
    python3 apply_tape2_playfirst.py

Segurança: valida TUDO em memória primeiro. Se qualquer trecho não bater
exatamente uma vez, aborta SEM alterar nenhum arquivo.
"""
import os
import sys

SRC = "src"

# (arquivo, trecho antigo, trecho novo)  -- cada 'antigo' deve aparecer exatamente 1 vez
EDITS = [
    # ---- Machine.h ----
    ("Machine.h",
     "\tvoid queueAutoType(const char* text, int bootDelayFrames = -1);",
     "\tvoid queueAutoType(const char* text, int bootDelayFrames = -1, bool playFirst = false);"),
    ("Machine.h",
     "\tint mAutoTypeTimer{ 0 };\n",
     "\tint mAutoTypeTimer{ 0 };\n"
     "\t// true: o PLAY da fita e apertado junto com a 1a tecla (troca de fita\n"
     "\t// com o jogo ja rodando); false: PLAY so depois do ultimo caractere\n"
     "\t// (LOADT no BASIC).\n"
     "\tbool mAutoPlayFirst{ false };\n"),

    # ---- Machine.cpp ----
    ("Machine.cpp",
     "void CMachine::queueAutoType(const char* text, int bootDelayFrames) {\n"
     "\tmAutoTypeText = text ? text : \"\";\n",
     "void CMachine::queueAutoType(const char* text, int bootDelayFrames, bool playFirst) {\n"
     "\tmAutoPlayFirst = playFirst;\n"
     "\tmAutoTypeText = text ? text : \"\";\n"),
    ("Machine.cpp",
     "\t\tmAutoTypeState = EAutoTypeState::KeyDown;\n"
     "\t\t// fall through to press the first key this same frame\n",
     "\t\tmAutoTypeState = EAutoTypeState::KeyDown;\n"
     "\t\tif (mAutoPlayFirst && mTapeLoaded) {\n"
     "\t\t\tmTape.play();\t// PLAY no mesmo quadro do ESPACO\n"
     "\t\t}\n"
     "\t\t// fall through to press the first key this same frame\n"),
    ("Machine.cpp",
     "\t\t\tif (mTapeLoaded) {\n"
     "\t\t\t\tmAutoTypeState = EAutoTypeState::WaitAutoPlay;\n",
     "\t\t\tif (mTapeLoaded && !mAutoPlayFirst) {\n"
     "\t\t\t\tmAutoTypeState = EAutoTypeState::WaitAutoPlay;\n"),

    # ---- libretro.cpp ----
    ("libretro.cpp",
     "\t\tg_machine->queueAutoType(\" \", kPickerAutoStartDelayFrames);",
     "\t\tg_machine->queueAutoType(\" \", kPickerAutoStartDelayFrames, true);"),

    # ---- Tape.cpp ----
    ("Tape.cpp",
     "\t\t\t\tif (idx - lastLoggedIdx >= 500) {",
     "\t\t\t\tif (idx < lastLoggedIdx || idx - lastLoggedIdx >= 500) {"),
]


def main():
    if not os.path.isdir(SRC):
        print("ERRO: pasta '%s/' nao encontrada. Rode na pasta do Makefile." % SRC)
        return 1

    # 1) le e valida tudo em memoria
    contents = {}
    for name, old, new in EDITS:
        path = os.path.join(SRC, name)
        if name not in contents:
            if not os.path.isfile(path):
                print("ERRO: arquivo nao encontrado: %s" % path)
                return 1
            with open(path, "rb") as f:
                raw = f.read()
            if b"\r\n" in raw:
                print("ERRO: %s usa CRLF; script preparado para LF. Nada foi alterado." % path)
                return 1
            contents[name] = raw.decode("utf-8")

    for name, old, new in EDITS:
        n = contents[name].count(old)
        if n != 1:
            print("ERRO: em %s o trecho abaixo apareceu %d vez(es) (esperado 1)." % (name, n))
            print("----\n%s\n----" % old)
            print("Nada foi alterado.")
            return 1
        contents[name] = contents[name].replace(old, new, 1)

    # 2) tudo validado: grava
    for name, text in contents.items():
        path = os.path.join(SRC, name)
        with open(path + ".bak_playfirst", "wb") as f:
            with open(path, "rb") as orig:
                f.write(orig.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_playfirst)" % (path, name))

    print("\nPronto. Agora recompile (make) e teste a troca para o disco 2.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
