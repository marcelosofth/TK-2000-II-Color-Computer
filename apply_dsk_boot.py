#!/usr/bin/env python3
# apply_dsk_boot.py -- corrige o boot do PITFALL_II_CORRIGIDO.dsk no TK2000.
#
# Uso (no Git Bash use barra normal /):
#     python apply_dsk_boot.py "Jogos DSK/PITFALL II CORRIGIDO.dsk"
#
# Problema: o setor de boot (CompatiBoot, Lisias) comeca com "BCS $0854", ou seja, so' segue
# o boot se o flag Carry vier LIGADO na entrada em $0801 (como na ROM de um Apple II). No
# TK2000 o Carry chega DESLIGADO, o desvio nao acontece e o CPU executa dado ($0A $00 = ASL, BRK)
# -> cai no monitor ("0805-  A=C0 X=60 ...").
# Correcao (2 trocas no setor 0 da trilha 0, offsets 0x01 e 0x88):
#   $0801: B0 51 0A  (BCS $0854 + dado)  ->  4C 54 08  (JMP $0854, sem depender do Carry)
#   $0888: AD 03 08  (LDA $0803)         ->  A9 0A EA  (LDA #$0A; NOP)  -- $0803 guardava o 0A
#
# Valida tudo em memoria antes de gravar; se algo nao bater, aborta sem alterar nada.
# Grava um novo arquivo <nome>_boot.dsk e NAO mexe no original.
import sys, os

if len(sys.argv) < 2:
    sys.exit("Uso: python apply_dsk_boot.py caminho/arquivo.dsk")
path = sys.argv[1]
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

d = bytearray(open(path, "rb").read())
if len(d) != 143360:
    sys.exit("ERRO: tamanho %d (esperado 143360). Nada foi alterado." % len(d))

if d[0:4] == bytes([0x01, 0x4C, 0x54, 0x08]) and d[0x88:0x8B] == bytes([0xA9, 0x0A, 0xEA]):
    sys.exit("Ja esta corrigido. Nada foi alterado.")
if d[0:4] != bytes([0x01, 0xB0, 0x51, 0x0A]):
    sys.exit("ERRO: setor de boot nao e' o esperado (bytes 0-3 = %s). Nada foi alterado." % d[0:4].hex())
if d[0x88:0x8B] != bytes([0xAD, 0x03, 0x08]):
    sys.exit("ERRO: instrucao em $0888 nao e' LDA $0803 (%s). Nada foi alterado." % d[0x88:0x8B].hex())
if d[0x54:0x56] != bytes([0xA2, 0xFF]):
    sys.exit("ERRO: destino $0854 nao comeca com LDX #$FF. Nada foi alterado.")

d[1:4] = bytes([0x4C, 0x54, 0x08])
d[0x88:0x8B] = bytes([0xA9, 0x0A, 0xEA])

root, ext = os.path.splitext(path)
out = root + "_boot" + ext
open(out, "wb").write(d)
print("OK: gravado %s (original intacto)" % out)
