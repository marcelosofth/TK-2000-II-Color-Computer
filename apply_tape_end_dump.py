#!/usr/bin/env python3
"""
apply_tape_end_dump.py - TK2000 core: diagnostico do fim de carga da fita

O log do "Desfiladeiro Mortal" mostra a fita 100% lida (tape reached end of
queue), depois RUN sem erro nenhum e sem efeito nenhum -- volta direto pro
prompt. Isso e o comportamento de RUN sem programa BASIC na memoria (LOADT
carregou algo, mas nao um programa Applesoft tokenizado em $0801). A hipotese
mais provavel: e um binario (tipo BLOAD) que precisa de CALL <endereco>, nao
RUN. Este patch nao corrige nada, so despeja memoria e registradores no
exato momento em que a fita termina de tocar, para eu confirmar o endereço.

O que muda:
  Cpu6502.h/.cpp : CCpu6502::dbgPeek(addr) -- leitura publica p/ diagnostico.
  Tape.cpp       : no log existente "tape reached end of queue", despeja
                   $0060-$006F (zero page baixa, onde ficam ponteiros tipo
                   TXTTAB/program pointer), $0450-$047F (onde a ROM guarda o
                   cabecalho da fita que acabou de ler), $0800-$080F (inicio
                   tipico de programa BASIC) e $2000-$200F / $2000-$200F+dump
                   maior (onde a Karateka carrega binario).

Uso (na pasta que contem src/):   python3 apply_tape_end_dump.py
Valida tudo em memoria; aborta sem alterar nada se algo nao bater 1 vez.
Backups: *.bak_tapeenddump
"""
import os, sys

SRC = "src"

EDITS = [
    ("Cpu6502.h",
     "\tvoid setPC(const word pc);\n",
     "\tvoid setPC(const word pc);\n"
     "\t// Diagnostico (apply_tape_end_dump.py): leitura de memoria sem\n"
     "\t// efeitos colaterais de timing, para dump fora do loop de execucao.\n"
     "\tbyte dbgPeek(const word addr) const { return mBus.readByte(addr, mCumulativeCycles); }\n"),

    ("Tape.cpp",
     "\t\t\t\ttk2000_log(\"tape reached end of queue (%zu pulses) at cycle %llu\",\n"
     "\t\t\t\t\tmQueueCycles.size(), (unsigned long long)actualCycle);\n",
     "\t\t\t\ttk2000_log(\"tape reached end of queue (%zu pulses) at cycle %llu\",\n"
     "\t\t\t\t\tmQueueCycles.size(), (unsigned long long)actualCycle);\n"
     "\t\t\t\t{\n"
     "\t\t\t\t\tchar line[16 * 3 + 1];\n"
     "\t\t\t\t\tauto dump = [&](const char* tag, word base, int rows) {\n"
     "\t\t\t\t\t\tfor (int row = 0; row < rows; row++) {\n"
     "\t\t\t\t\t\t\tfor (int i = 0; i < 16; i++) {\n"
     "\t\t\t\t\t\t\t\tsnprintf(line + i * 3, 4, \"%02X \", mCpu.dbgPeek((word)(base + row * 16 + i)));\n"
     "\t\t\t\t\t\t\t}\n"
     "\t\t\t\t\t\t\ttk2000_log(\"  %s $%04X: %s\", tag, (word)(base + row * 16), line);\n"
     "\t\t\t\t\t\t}\n"
     "\t\t\t\t\t};\n"
     "\t\t\t\t\tdump(\"zp\", 0x0060, 1);\n"
     "\t\t\t\t\tdump(\"tape header\", 0x0450, 3);\n"
     "\t\t\t\t\tdump(\"basic start\", 0x0800, 1);\n"
     "\t\t\t\t\tdump(\"bin start\", 0x2000, 1);\n"
     "\t\t\t\t\ttk2000_log_flush();\n"
     "\t\t\t\t}\n"),
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
        with open(path, "rb") as o, open(path + ".bak_tapeenddump", "wb") as b:
            b.write(o.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_tapeenddump)" % (path, name))
    print("\nPronto. Recompile (make), carregue o Desfiladeiro Mortal (so LOADT, sem")
    print("precisar dar RUN) e me mande o log; procure a linha 'tape reached end of queue'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
