#!/usr/bin/env python3
"""
apply_brk_trace.py - TK2000 core: trace dos ultimos 64 PCs no primeiro BRK

- Cpu6502.h/.cpp : buffer circular com (PC, opcode, A, X, Y, S, P) das ultimas
  64 instrucoes. No PRIMEIRO BRK despeja: trace, 16 bytes da pilha, vetor
  $FFFE, bytes ao redor do PC. Apenas os 4 primeiros BRKs sao logados; depois
  disso o log e silenciado (acaba o log de 194 MB).
- LangCard.h/.cpp: logState() (estado dos switches + bytes da RAM em $D8EE e
  $FFFE/$FFFF), chamado junto com o dump.

Uso (na pasta que contem src/):   python3 apply_brk_trace.py
Valida tudo em memoria; aborta sem alterar nada se algo nao bater 1 vez.
Backups: *.bak_brktrace
"""
import os, sys

SRC = "src"

EDITS = [
    # ---------- LangCard ----------
    ("LangCard.h",
     "\tvoid loadState(const SState& s);\n",
     "\tvoid loadState(const SState& s);\n"
     "\tvoid logState() const;\t// diagnostico (dump no BRK)\n"),

    ("LangCard.cpp",
     "CLangCard::SState CLangCard::saveState() const {\n",
     "void CLangCard::logState() const {\n"
     "\ttk2000_log(\"langcard state: readRAM=%d writeRAM=%d bank2=%d prewrite=%d \"\n"
     "\t\t\"ramVec($FFFE)=$%02X%02X ramD8EE bank1/bank2=$%02X/$%02X\",\n"
     "\t\tmReadRAM ? 1 : 0, mWriteRAM ? 1 : 0, mBank2 ? 1 : 0, mPrewrite ? 1 : 0,\n"
     "\t\tmUpper[0x1FFF], mUpper[0x1FFE], mBank1[0x8EE], mBank2Ram[0x8EE]);\n"
     "}\n\n"
     "/*************************************************************************************************/\n"
     "CLangCard::SState CLangCard::saveState() const {\n"),

    # ---------- CPU header ----------
    ("Cpu6502.h",
     "\tuint64_t mLastHeartbeatCycle{ 0 };\n",
     "\tuint64_t mLastHeartbeatCycle{ 0 };\n"
     "\t// Diagnostic: ring buffer of the last 64 executed instructions,\n"
     "\t// dumped on the first BRK (see executeOpcode).\n"
     "\tstruct STraceEntry { word pc; byte op, a, x, y, s, p; };\n"
     "\tSTraceEntry mTrace[64]{};\n"
     "\tunsigned mTraceHead{ 0 };\n"
     "\tint mBrkLogged{ 0 };\n"),

    # ---------- CPU source ----------
    ("Cpu6502.cpp",
     "#include \"Disk.h\"\n",
     "#include \"Disk.h\"\n#include \"LangCard.h\"\n"),

    ("Cpu6502.cpp",
     "\tconst byte opcode = readByte(this->mRegPC.value++);\n",
     "\tconst word pcBefore = mRegPC.value;\n"
     "\tconst byte opcode = readByte(this->mRegPC.value++);\n"
     "\tmTrace[mTraceHead++ & 63] = { pcBefore, opcode, mRegA, mRegX, mRegY, mRegS, mRegFlags };\n"),

    ("Cpu6502.cpp",
     "\t\ttk2000_log(\"BRK trap: PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X cycles=%llu\",\n"
     "\t\t\tmRegPC.value, mRegA, mRegX, mRegY, mRegS, (unsigned long long)mCumulativeCycles);\n",
     "\t\tif (mBrkLogged < 4) {\n"
     "\t\t\t++mBrkLogged;\n"
     "\t\t\ttk2000_log(\"BRK trap #%d: PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X cycles=%llu\",\n"
     "\t\t\t\tmBrkLogged, mRegPC.value, mRegA, mRegX, mRegY, mRegS, (unsigned long long)mCumulativeCycles);\n"
     "\t\t\tif (mBrkLogged == 1) {\n"
     "\t\t\t\tconst unsigned n = mTraceHead < 64 ? mTraceHead : 64;\n"
     "\t\t\t\tfor (unsigned i = 0; i < n; i++) {\n"
     "\t\t\t\t\tconst STraceEntry& e = mTrace[(mTraceHead - n + i) & 63];\n"
     "\t\t\t\t\ttk2000_log(\"  trace[%02u] PC=$%04X op=$%02X A=$%02X X=$%02X Y=$%02X S=$%02X P=$%02X\",\n"
     "\t\t\t\t\t\ti, e.pc, e.op, e.a, e.x, e.y, e.s, e.p);\n"
     "\t\t\t\t}\n"
     "\t\t\t\tchar stk[16 * 3 + 1] = {};\n"
     "\t\t\t\tfor (int i = 0; i < 16; i++) {\n"
     "\t\t\t\t\tsnprintf(stk + i * 3, 4, \"%02X \", readByte(0x0100 + ((mRegS + 1 + i) & 0xFF)));\n"
     "\t\t\t\t}\n"
     "\t\t\t\ttk2000_log(\"  stack from S+1: %s\", stk);\n"
     "\t\t\t\ttk2000_log(\"  vector $FFFE (as CPU sees it) = $%04X\", readWord(0xFFFE));\n"
     "\t\t\t\tif (mRegPC.value > 0xC100) {\n"
     "\t\t\t\t\ttk2000_log(\"  bytes at PC-1..PC+6: %02X %02X %02X %02X %02X %02X %02X %02X\",\n"
     "\t\t\t\t\t\treadByte(mRegPC.value - 1), readByte(mRegPC.value), readByte(mRegPC.value + 1),\n"
     "\t\t\t\t\t\treadByte(mRegPC.value + 2), readByte(mRegPC.value + 3), readByte(mRegPC.value + 4),\n"
     "\t\t\t\t\t\treadByte(mRegPC.value + 5), readByte(mRegPC.value + 6));\n"
     "\t\t\t\t}\n"
     "\t\t\t\tCLangCard* lc = static_cast<CLangCard*>(mBus.getDevice(EDevices::LANGCARD));\n"
     "\t\t\t\tif (lc != nullptr) {\n"
     "\t\t\t\t\tlc->logState();\n"
     "\t\t\t\t}\n"
     "\t\t\t\ttk2000_log_flush();\n"
     "\t\t\t}\n"
     "\t\t} else if (mBrkLogged == 4) {\n"
     "\t\t\t++mBrkLogged;\n"
     "\t\t\ttk2000_log(\"BRK: further BRK traps suppressed\");\n"
     "\t\t}\n"),
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
        with open(path, "rb") as o, open(path + ".bak_brktrace", "wb") as b:
            b.write(o.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_brktrace)" % (path, name))
    print("\nPronto. Recompile (make), rode o disco 2 ate travar e me envie o log.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
