#!/usr/bin/env python3
"""
apply_diag_c0write.py - TK2000 core: diagnostico do disco 2 (Karateka.ct2)

O log anterior mostrou, depois do ESPACO com a fita 2 em PLAY:
  1) quase nenhum pulso da fita e consumido (so o pulso 1);
  2) ~1M de ciclos depois, um loop copia a zero page para $C000-$C0FF
     (escritas sequenciais em $C001..$C07F, depois $C080/$C081);
  3) segue um plot de texto e um RTS volta para $D8EE+1 (meio de um LDY #$00),
     o que dispara o BRK.

Este patch NAO corrige nada; ele descobre QUEM faz (2) e QUEM empilhou o
endereco de retorno errado de (3):

  - Cpu6502: guarda, para cada byte da pagina $01xx, o PC da instrucao que
    escreveu por ultimo (rastreio de origem da pilha).
  - Depois do PLAY da 2a fita (armado por CTape::play), loga cada alvo NOVO de
    JSR/JMP abs (grafo de chamadas: "quem chamou quem"), ate 500 linhas.
  - Na PRIMEIRA escrita em $C001-$C00F: despeja as ultimas 64 instrucoes, os
    bytes de codigo ao redor do PC que escreveu, a zero page inteira e a pilha.
  - No PRIMEIRO opcode $00 (BRK): despeja as ultimas 64 instrucoes, a pilha e
    o PC que escreveu cada byte de pilha (+ bytes de codigo desse PC).

Uso (na pasta que contem src/):   python3 apply_diag_c0write.py
Valida tudo em memoria; aborta sem alterar nada se algo nao bater 1 vez.
Backups: *.bak_diagc0
Funciona com ou sem o apply_brk_trace.py ja aplicado.
"""
import os, sys

SRC = "src"

DBG_IMPL = r'''/*************************************************************************************************/
// DIAGNOSTIC (apply_diag_c0write.py)
void CCpu6502::dbgArm() {
	static int plays = 0;
	if (++plays < 2) {
		return;	// 1a fita (LOADT do boot): nao interessa
	}
	mDbgArmed = true;
	mDbgTripped = false;
	mDbgBrkDumped = false;
	mDbgCalls = 0;
	mDbgRingHead = 0;
	memset(mDbgSeen, 0, sizeof(mDbgSeen));
	tk2000_log("DIAG armed (tape play #%d) cycles=%llu", plays, (unsigned long long)mCumulativeCycles);
}

static void dbgHexLine(const char* tag, word base, const byte* b, int n) {
	char s[16 * 3 + 1] = {};
	for (int i = 0; i < n; i++) {
		snprintf(s + i * 3, 4, "%02X ", b[i]);
	}
	tk2000_log("  %s $%04X: %s", tag, base, s);
}

void CCpu6502::dbgDumpCommon(const char* why) const {
	tk2000_log("DIAG %s: PC=$%04X A=$%02X X=$%02X Y=$%02X S=$%02X P=$%02X cycles=%llu",
		why, mDbgPC, mRegA, mRegX, mRegY, mRegS, mRegFlags, (unsigned long long)mCumulativeCycles);
	const unsigned n = mDbgRingHead < 64 ? mDbgRingHead : 64;
	for (unsigned i = 0; i < n; i++) {
		const word pc = mDbgRing[(mDbgRingHead - n + i) & 63];
		tk2000_log("  ring[%02u] PC=$%04X op=$%02X", i, pc, readByte(pc));
	}
}

void CCpu6502::dbgDumpStack() const {
	byte b[16];
	for (int row = 0; row < 4; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte(0x0100 + ((mRegS + 1 + row * 16 + i) & 0xFF));
		}
		dbgHexLine("stack from S+1+row*16", (word)(0x0100 + ((mRegS + 1 + row * 16) & 0xFF)), b, 16);
	}
	for (int i = 1; i <= 12; i++) {
		const unsigned slot = (mRegS + i) & 0xFF;
		const word w = mDbgStackWriter[slot];
		tk2000_log("  stack[$01%02X]=$%02X written by PC=$%04X", slot, readByte(0x0100 + slot), w);
	}
	// bytes de codigo em torno de cada um dos primeiros 4 escritores distintos
	word seen[4] = { 0, 0, 0, 0 };
	int ns = 0;
	for (int i = 1; i <= 12 && ns < 4; i++) {
		const word w = mDbgStackWriter[(mRegS + i) & 0xFF];
		bool dup = false;
		for (int k = 0; k < ns; k++) {
			dup = dup || (seen[k] == w);
		}
		if (!dup && w != 0) {
			seen[ns++] = w;
			for (int i2 = 0; i2 < 16; i2++) {
				b[i2] = readByte((word)(w - 4 + i2));
			}
			dbgHexLine("code around writer (PC-4..)", (word)(w - 4), b, 16);
		}
	}
}

void CCpu6502::dbgStep() const {
	const word pc = mDbgPC;
	const byte op = readByte(pc);
	mDbgRing[mDbgRingHead++ & 63] = pc;
	if ((op == 0x20 || op == 0x4C) && mDbgCalls < 500) {
		const word target = (word)(readByte((word)(pc + 1)) | (readByte((word)(pc + 2)) << 8));
		if ((mDbgSeen[target >> 3] & (1 << (target & 7))) == 0) {
			mDbgSeen[target >> 3] |= (byte)(1 << (target & 7));
			++mDbgCalls;
			tk2000_log("DIAG %s $%04X from $%04X S=$%02X cycles=%llu",
				op == 0x20 ? "JSR" : "JMP", target, pc, mRegS, (unsigned long long)mCumulativeCycles);
		}
	}
	if (op == 0x00 && !mDbgBrkDumped) {
		mDbgBrkDumped = true;
		dbgDumpCommon("first BRK opcode");
		dbgDumpStack();
		tk2000_log_flush();
		mDbgArmed = false;
	}
}

void CCpu6502::dbgOnWrite(word address, byte value) const {
	mDbgTripped = true;
	tk2000_log("DIAG first write to $%04X data=$%02X", address, value);
	dbgDumpCommon("first write to $C001-$C00F");
	byte b[16];
	for (int row = 0; row < 4; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte((word)(mDbgPC - 16 + row * 16 + i));
		}
		dbgHexLine("code around writer", (word)(mDbgPC - 16 + row * 16), b, 16);
	}
	for (int row = 0; row < 16; row++) {
		for (int i = 0; i < 16; i++) {
			b[i] = readByte((word)(row * 16 + i));
		}
		dbgHexLine("zero page", (word)(row * 16), b, 16);
	}
	dbgDumpStack();
	tk2000_log_flush();
}

/*************************************************************************************************/
void CCpu6502::executeOpcode() {
'''

EDITS = [
    # ---------- CPU header ----------
    ("Cpu6502.h",
     "\tvoid loadState(const SState& s);\nprivate:\n\tCBus& mBus;\n",
     "\tvoid loadState(const SState& s);\n"
     "\tvoid dbgArm();\t// diagnostico (apply_diag_c0write): chamado por CTape::play()\n"
     "private:\n\tCBus& mBus;\n"),

    ("Cpu6502.h",
     "\tuint64_t mLastHeartbeatCycle{ 0 };\n",
     "\tuint64_t mLastHeartbeatCycle{ 0 };\n"
     "\t// Diagnostico (apply_diag_c0write.py)\n"
     "\tmutable bool mDbgArmed{ false };\n"
     "\tmutable bool mDbgTripped{ false };\n"
     "\tmutable bool mDbgBrkDumped{ false };\n"
     "\tmutable word mDbgPC{ 0 };\n"
     "\tmutable word mDbgRing[64]{};\n"
     "\tmutable unsigned mDbgRingHead{ 0 };\n"
     "\tmutable word mDbgStackWriter[256]{};\n"
     "\tmutable unsigned mDbgCalls{ 0 };\n"
     "\tmutable byte mDbgSeen[65536 / 8]{};\n"
     "\tvoid dbgStep() const;\n"
     "\tvoid dbgOnWrite(word address, byte value) const;\n"
     "\tvoid dbgDumpCommon(const char* why) const;\n"
     "\tvoid dbgDumpStack() const;\n"),

    ("Cpu6502.h",
     "\tinline void writeByte(word address, byte value) const {\n"
     "\t\tmBus.writeByte(address, value, mCumulativeCycles);\n"
     "\t}\n",
     "\tinline void writeByte(word address, byte value) const {\n"
     "\t\tif ((address & 0xFF00) == 0x0100) {\n"
     "\t\t\tmDbgStackWriter[address & 0xFF] = mDbgPC;\n"
     "\t\t} else if (mDbgArmed && !mDbgTripped && address >= 0xC001 && address <= 0xC00F) {\n"
     "\t\t\tdbgOnWrite(address, value);\n"
     "\t\t}\n"
     "\t\tmBus.writeByte(address, value, mCumulativeCycles);\n"
     "\t}\n"),

    # ---------- CPU source ----------
    ("Cpu6502.cpp",
     "void CCpu6502::executeOpcode() {\n",
     DBG_IMPL),

    ("Cpu6502.cpp",
     "\tconst byte opcode = readByte(this->mRegPC.value++);\n",
     "\tmDbgPC = mRegPC.value;\n"
     "\tif (mDbgArmed) {\n"
     "\t\tdbgStep();\n"
     "\t}\n"
     "\tconst byte opcode = readByte(this->mRegPC.value++);\n"),

    # ---------- Tape ----------
    ("Tape.cpp",
     "\t\tmCpu.setFullSpeed(true);\n\t\ttk2000_log(\"tape play() at pulse %td/%zu\",",
     "\t\tmCpu.setFullSpeed(true);\n\t\tmCpu.dbgArm();\n\t\ttk2000_log(\"tape play() at pulse %td/%zu\","),
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
        with open(path, "rb") as o, open(path + ".bak_diagc0", "wb") as b:
            b.write(o.read())
        with open(path, "wb") as f:
            f.write(text.encode("utf-8"))
        print("OK: %s alterado (backup: %s.bak_diagc0)" % (path, name))
    print("\nPronto. Recompile (make), carregue o disco 1, troque para o disco 2,")
    print("deixe travar e me envie o novo tk2000_debug.log (procure linhas 'DIAG').")
    return 0


if __name__ == "__main__":
    sys.exit(main())
