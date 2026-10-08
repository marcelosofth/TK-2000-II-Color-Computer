#!/usr/bin/env python3
"""
apply_an1_upper.py - TK2000 core: RAM/ROM de $D000-$FFFF passa a ser controlada
SO pelo AN1 ($C05A/$C05B), como no hardware real (e no MAME). Remove a "language
card" inventada.

Por que (evidencia do log tk2000_debug_1.log + MAME src/mame/apple/tk2000.cpp):
  - O TK2000 NAO tem language card. Em $C080-$C0FF as leituras devolvem o
    barramento flutuante e as escritas sao ignoradas (o "STA $C088 / LDA $C080
    ate voltar 5" da ROM e so um teste de interface; nao troca memoria).
  - $C05B liga a RAM em TODA a faixa $C100-$FFFF (leitura e escrita);
    $C05A traz a ROM de volta. Escritas com a ROM ativa nao tem efeito.
  - No nosso core a LangCard ficava com readRAM=1 fixo desde o boot (a ROM faz
    LDA $C080 / STA $C088). Depois do ESPACO do disco 2 o jogo liga AN1, copia o
    stub e desliga AN1 para chamar a ROM (LOAD em $EDAE). No hardware real a ROM
    fica visivel; aqui $D000-$FFFF continuava lendo RAM, entao o JSR $FDDA
    (PRBYTE, chamado por $EE31 ao imprimir o cabecalho da fita) executava codigo
    do jogo guardado sob a ROM: o loop de copia auto-modificavel, que sobrescreve
    a memoria (a tela de ruido) ate dar BRK.

O que muda:
  Rom.h        : CRom::isAn1() (leitura do estado do AN1)
  LangCard.cpp : $D000-$FFFF le/escreve RAM somente com AN1 ligado; $C080-$C08F
                 viram no-op (leitura devolve $FF); um unico banco em $D000-$DFFF.

Uso (na pasta que contem src/):   python3 apply_an1_upper.py
Valida tudo em memoria; aborta sem alterar nada se algo nao bater 1 vez.
Backups: *.bak_an1upper   (para desfazer, copie os backups de volta)
IMPORTANTE: teste tambem o DISCO 1 (boot + carga completa) - ele passa a usar o
modelo de hardware real.
"""
import os, sys

SRC = "src"

EDITS = [
    ("Rom.h",
     "\tvoid reset() override;\nprivate:\n\tbool mAn1{ false };",
     "\tvoid reset() override;\n"
     "\t// AN1 state: true = RAM banked over $C100-$FFFF ($C05B), false = ROM ($C05A)\n"
     "\tbool isAn1() const { return mAn1; }\n"
     "private:\n\tbool mAn1{ false };"),

    ("LangCard.cpp",
     "\treturn mBank2 ? &mBank2Ram[addr - 0xD000] : &mBank1[addr - 0xD000];",
     "\t// Real TK2000: a single RAM image under $D000-$DFFF (no bank switching).\n"
     "\treturn &mBank2Ram[addr - 0xD000];"),

    ("LangCard.cpp",
     "\tif (addr >= 0xC080 && addr <= 0xC08F) {\n"
     "\t\thandleSwitch(addr, false);\n"
     "\t\treturn 0xFF;\t// soft switches don't drive real data onto the bus\n"
     "\t}\n"
     "\t// 0xD000-0xFFFF\n"
     "\tif (mReadRAM) {\n",
     "\tif (addr >= 0xC080 && addr <= 0xC08F) {\n"
     "\t\t// The real TK2000 has no language card: nothing happens here.\n"
     "\t\treturn 0xFF;\n"
     "\t}\n"
     "\t// 0xD000-0xFFFF: RAM only while AN1 is on ($C05B), ROM otherwise.\n"
     "\tif (mRom.isAn1()) {\n"),

    ("LangCard.cpp",
     "\tif (addr >= 0xC080 && addr <= 0xC08F) {\n"
     "\t\thandleSwitch(addr, true);\n"
     "\t\treturn;\n"
     "\t}\n",
     "\tif (addr >= 0xC080 && addr <= 0xC08F) {\n"
     "\t\treturn;\t// no language card on the real TK2000\n"
     "\t}\n"),

    ("LangCard.cpp",
     "\tif (mWriteRAM) {\n\t\t*ramPtr(addr) = data;\n",
     "\tif (mRom.isAn1()) {\n\t\t*ramPtr(addr) = data;\n"),
]


def main():
    if not os.path.isdir(SRC):
        print("ERRO: pasta '%s/' nao encontrada. Rode na pasta do Makefile." % SRC)
        return 1
    contents = {}
    for name, old, new in EDITS:
        path = os.path.join(SRC, name)
        if name not in contents:
            if not os.path.isfile(path):
                print("ERRO: arquivo nao encontrado: %s" % path)
                return 1
            raw = open(path, "rb").read()
            if b"\r\n" in raw:
                print("ERRO: %s usa CRLF; script preparado para LF. Nada alterado." % path)
                return 1
            contents[name] = raw.decode("utf-8")
    for name, old, new in EDITS:
        n = contents[name].count(old)
        if n != 1:
            print("ERRO: em %s o trecho abaixo apareceu %d vez(es) (esperado 1)." % (name, n))
            print("----\n%s\n----\nNada foi alterado." % old)
            return 1
        contents[name] = contents[name].replace(old, new, 1)
    for name, text in contents.items():
        path = os.path.join(SRC, name)
        with open(path, "rb") as o, open(path + ".bak_an1upper", "wb") as b:
            b.write(o.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_an1upper)" % (path, name))
    print("\nPronto. Recompile (make), teste o disco 1 e depois a troca para o disco 2.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
