#!/usr/bin/env python3
"""
apply_langcard_hybrid.py - TK2000 core: LangCard hibrida (disco 2 do Karateka)

Offsets PARES ($C080/82/84/86/88/8A/8C/8E): comportamento atual
    (writeRAM acompanha readRAM) -> disco 1 continua funcionando.
Offsets IMPARES ($C081/83/85/87/89/8B/8D/8F): protocolo real do Apple II:
    dois LEITURAS seguidas em offsets impares habilitam escrita na RAM;
    escrita num offset impar zera o pre-write. Um acesso impar NAO desliga
    o writeRAM que um offset par ja tinha ligado.

Uso (na pasta que contem src/):
    python3 apply_langcard_hybrid.py

Valida tudo em memoria primeiro; se algo nao bater exatamente 1 vez, aborta
sem alterar nada. Cria backup *.bak_lchybrid.
"""
import os
import sys

SRC = "src"

EDITS = [
    ("LangCard.h",
     "\tvoid handleSwitch(const word addr);",
     "\tvoid handleSwitch(const word addr, const bool isWrite);"),

    ("LangCard.cpp",
     "void CLangCard::handleSwitch(const word addr) {\n",
     "void CLangCard::handleSwitch(const word addr, const bool isWrite) {\n"),

    ("LangCard.cpp",
     "\tmWriteRAM = mReadRAM;\n\tmPrewrite = false;\n",
     "\tif ((off & 1) == 0) {\n"
     "\t\t// even offset: this board's behaviour (write follows read)\n"
     "\t\tmWriteRAM = mReadRAM;\n"
     "\t\tmPrewrite = false;\n"
     "\t} else if (isWrite) {\n"
     "\t\t// write to an odd offset resets the pre-write latch\n"
     "\t\tmPrewrite = false;\n"
     "\t} else {\n"
     "\t\t// odd offset read: 2nd consecutive read enables RAM writes\n"
     "\t\t// (real Apple II protocol); never disables an earlier enable\n"
     "\t\tif (mPrewrite) {\n"
     "\t\t\tmWriteRAM = true;\n"
     "\t\t}\n"
     "\t\tmPrewrite = true;\n"
     "\t}\n"),

    ("LangCard.cpp",
     "\t\thandleSwitch(addr);\n\t\treturn 0xFF;",
     "\t\thandleSwitch(addr, false);\n\t\treturn 0xFF;"),

    ("LangCard.cpp",
     "\t\thandleSwitch(addr);\n\t\treturn;\n",
     "\t\thandleSwitch(addr, true);\n\t\treturn;\n"),
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
            print("----\n%s\n----\nNada foi alterado." % old)
            return 1
        contents[name] = contents[name].replace(old, new, 1)

    for name, text in contents.items():
        path = os.path.join(SRC, name)
        with open(path, "rb") as orig, open(path + ".bak_lchybrid", "wb") as bak:
            bak.write(orig.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_lchybrid)" % (path, name))

    print("\nPronto. Recompile (make) e teste o disco 2.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
