#!/usr/bin/env python3
"""
apply_joy_map.py - aplica no .dsk do Pitfall II (Lost Caverns) o mesmo
mapeamento de joystick/teclado que esta no Pitfall_II_corrigido.ct2.

Uso:
    python apply_joy_map.py arquivo.dsk              (aplica os 2 patches)
    python apply_joy_map.py arquivo.dsk --sem-teclas (so a deteccao do joystick)
    python apply_joy_map.py arquivo.dsk --sem-deteccao (so a tabela de teclas)

Os patches sao localizados por assinatura de bytes (nao por offset), entao
funcionam com .dsk em ordem DOS ou ProDOS. Roda varias vezes sem problema.
Antes de gravar, cria arquivo.dsk.bak (se ainda nao existir).

Patch 1 - TECLAS (tabela de teclas -> direcao, em $623A-$6241):
    entradas 6 e 7 (cima/baixo):  $0B,$0A  ->  $70,$71   (igual ao .ct2)
Patch 2 - DETECCAO (deteccao de joystick em $6329-$6334):
    BIT $C064 / NOP NOP / BIT $C065 / NOP NOP
        -> BIT $C064 / BPL $6335 / BIT $C065 / BPL $6335   (igual ao .ct2)
    Com os NOPs o jogo sempre cai no modo teclado; com os BPL ele volta
    a detectar o joystick (paddles $C064/$C065) quando ele esta presente.
"""
import os
import shutil
import sys

PATCHES = {
    "teclas": (
        bytes.fromhex("4a4b494d0815" "0b0a" "00000404"),
        bytes.fromhex("4a4b494d0815" "7071" "00000404"),
    ),
    "deteccao": (
        bytes.fromhex("2c64c0" "eaea" "2c65c0" "eaea" "c624"),
        bytes.fromhex("2c64c0" "1007" "2c65c0" "1002" "c624"),
    ),
}


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    if len(args) != 1:
        print(__doc__)
        return 2

    path = args[0]
    if not os.path.isfile(path):
        print("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)
        return 1

    wanted = [n for n in PATCHES
              if not (n == "teclas" and "--sem-teclas" in flags)
              and not (n == "deteccao" and "--sem-deteccao" in flags)]

    data = bytearray(open(path, "rb").read())
    if len(data) != 143360:
        print("AVISO: tamanho %d (esperado 143360 para .dsk de 35 trilhas)" % len(data))

    changed = []
    for name in wanted:
        old, new = PATCHES[name]
        n_old = bytes(data).count(old)
        n_new = bytes(data).count(new)
        if n_old == 1:
            i = bytes(data).find(old)
            data[i:i + len(old)] = new
            changed.append((name, i))
        elif n_old == 0 and n_new == 1:
            print("Patch '%s': ja esta aplicado." % name)
        elif n_old == 0:
            print("ERRO: patch '%s': assinatura nao encontrada "
                  "(este .dsk e a mesma versao do jogo?). Nada foi alterado." % name)
            return 1
        else:
            print("ERRO: patch '%s': assinatura ambigua (%d ocorrencias). "
                  "Nada foi alterado." % (name, n_old))
            return 1

    if not changed:
        print("Ja esta corrigido. Nada foi alterado.")
        return 0

    bak = path + ".bak"
    if not os.path.exists(bak):
        shutil.copyfile(path, bak)
        print("Backup criado: %s" % bak)
    with open(path, "wb") as f:
        f.write(data)
    for name, i in changed:
        print("Patch '%s' aplicado (offset no arquivo: 0x%06X, trilha %d setor %d)."
              % (name, i, i // 4096, (i // 256) % 16))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
