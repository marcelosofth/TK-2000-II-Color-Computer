#!/usr/bin/env python3
"""
apply_tape_end_scan.py - TK2000 core: acha ONDE a fita realmente escreveu

Requer o apply_tape_end_dump.py JA aplicado (adiciona a este mesmo bloco).

O ultimo log mostrou $0800 e $2000 zerados -- ou seja, o "Desfiladeiro
Mortal" NAO carrega nem em $0800 (BASIC) nem em $2000 (onde a Karateka
carrega). Tentei calcular o endereco a mao pela ROM e nao bateu com
confianca suficiente para te passar um CALL certo. Em vez de chutar, este
patch faz o emulador mesmo dizer onde os ~8.7KB da fita foram parar:

  - Despeja $003C-$003F (o ponteiro de escrita REAL que o loop de leitura da
    fita incrementa, ROM $FCC2/$FCC6 -- confirmado por INC $3C / INC $3D).
  - Varre $0000-$BFFF em paginas de 256 bytes e loga qual(is) pagina(s) tem
    byte diferente de zero -- aponta exatamente a faixa de memoria onde o
    binario caiu, sem eu precisar adivinhar.

Uso (na pasta que contem src/, DEPOIS de apply_tape_end_dump.py):
    python3 apply_tape_end_scan.py
Valida tudo em memoria; aborta sem alterar nada se algo nao bater 1 vez.
Backups: *.bak_tapeendscan
"""
import os, sys

SRC = "src"

EDITS = [
    ("Tape.cpp",
     "\t\t\t\t\tdump(\"bin start\", 0x2000, 1);\n"
     "\t\t\t\t\ttk2000_log_flush();\n",
     "\t\t\t\t\tdump(\"bin start\", 0x2000, 1);\n"
     "\t\t\t\t\tdump(\"tape ptr $3C-$3F\", 0x003C, 1);\n"
     "\t\t\t\t\t// Coarse map of every non-empty 256-byte page in the main\n"
     "\t\t\t\t\t// 48K, so we don't have to guess the load address by hand.\n"
     "\t\t\t\t\tfor (word page = 0x0000; page < 0xC000; page += 0x0100) {\n"
     "\t\t\t\t\t\tbool any = false;\n"
     "\t\t\t\t\t\tfor (int i = 0; i < 256 && !any; i++) {\n"
     "\t\t\t\t\t\t\tif (mCpu.dbgPeek((word)(page + i)) != 0) {\n"
     "\t\t\t\t\t\t\t\tany = true;\n"
     "\t\t\t\t\t\t\t}\n"
     "\t\t\t\t\t\t}\n"
     "\t\t\t\t\t\tif (any) {\n"
     "\t\t\t\t\t\t\ttk2000_log(\"  non-empty page $%04X-$%04X\", page, (word)(page + 0xFF));\n"
     "\t\t\t\t\t\t}\n"
     "\t\t\t\t\t}\n"
     "\t\t\t\t\ttk2000_log_flush();\n"),
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
            print("----\n%s\n----" % old)
            print("Isso normalmente significa que apply_tape_end_dump.py ainda nao foi")
            print("aplicado (ou ja foi desfeito) neste %s. Aplique-o primeiro." % name)
            return 1
        contents[name] = contents[name].replace(old, new, 1)
    for name, text in contents.items():
        path = os.path.join(SRC, name)
        with open(path, "rb") as o, open(path + ".bak_tapeendscan", "wb") as b:
            b.write(o.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_tapeendscan)" % (path, name))
    print("\nPronto. Recompile (make), carregue o Desfiladeiro Mortal (so LOADT) e")
    print("me mande o log; procure as linhas 'non-empty page' e 'tape ptr $3C-$3F'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
