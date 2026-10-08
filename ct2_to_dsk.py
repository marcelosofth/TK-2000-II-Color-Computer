#!/usr/bin/env python3
"""
ct2_to_dsk.py - converte um .ct2 (fita TK2000) do Pitfall II para .dsk.

Uso:
    python ct2_to_dsk.py Pitfall_II_corrigido.ct2 MODELO.dsk SAIDA.dsk

MODELO.dsk = o .dsk que ja boota no seu emulador (ex.: o
Pit_Fall_II_-_Lost_Caverns_fix.dsk ORIGINAL, sem o patch do joystick).
Dele o script aproveita a trilha 0 (boot + loader), o catalogo e o preambulo
do arquivo binario "PITFALL II" ($07B0-$08EF). O corpo do jogo ($08F0-$99FF)
vem do .ct2, ou seja, com o teclado/joystick da fita.

O que o script faz:
  1. le o .ct2 (blocos CA/CB/DA), remonta a imagem de memoria do jogo
     (arquivo "PIT.II": $08F0-$99FF);
  2. monta o novo arquivo binario: preambulo do modelo + imagem da fita,
     carregado em $07B0 (mesmo endereco do modelo);
  3. regrava as listas T/S, o catalogo (tamanho em setores) e o bitmap do VTOC;
  4. confere tudo lendo o .dsk de volta pelo catalogo.
"""
import sys

LOAD_ADDR = 0x07B0          # endereco de carga do arquivo no modelo
BODY_ADDR = 0x08F0          # onde comeca o corpo vindo da fita
SECSZ = 256


# ---------------------------------------------------------------- CT2
def read_ct2(path):
    """Devolve {id_arquivo: (inicio, bytes)} a partir dos blocos DA."""
    d = open(path, "rb").read()
    if d[:4] != b"CTK2":
        raise SystemExit("ERRO: %s nao parece um .ct2 (faltando 'CTK2')" % path)
    p, blocks = 4, []
    while p + 4 <= len(d):
        tag = d[p:p + 2]
        ln = int.from_bytes(d[p + 2:p + 4], "little")
        blocks.append((tag, d[p + 4:p + 4 + ln]))
        p += 4 + ln
    files, cur = {}, None
    for tag, body in blocks:
        if tag != b"DA":
            continue
        fid, seq = body[6], body[7]
        if len(body) == 13:                      # cabecalho: id 00 ini fim chk
            start = body[8] | body[9] << 8
            end = body[10] | body[11] << 8
            cur = files.setdefault(fid, {"start": start, "end": end, "data": bytearray()})
            if seq == 0 and fid != 1:
                cur["start"], cur["end"] = start, end
        elif seq >= 1 and fid in files:
            files[fid]["data"] += body[8:-1]     # sem nome/seq e sem checksum
    out = {}
    for fid, f in files.items():
        want = f["end"] - f["start"] + 1
        out[fid] = (f["start"], bytes(f["data"][:want]), len(f["data"]) >= want)
    return out


# ---------------------------------------------------------------- DSK
def sec(dsk, t, s):
    o = (t * 16 + s) * SECSZ
    return dsk[o:o + SECSZ]


def put(dsk, t, s, data):
    o = (t * 16 + s) * SECSZ
    dsk[o:o + SECSZ] = data.ljust(SECSZ, b"\x00")


def vtoc_free(v, t, s):
    w = int.from_bytes(v[0x38 + 4 * t:0x38 + 4 * t + 2], "big")
    return bool(w >> s & 1)


def vtoc_set(v, t, s, free):
    o = 0x38 + 4 * t
    w = int.from_bytes(v[o:o + 2], "big")
    w = (w | 1 << s) if free else (w & ~(1 << s))
    v[o:o + 2] = w.to_bytes(2, "big")


def read_file(dsk, ts_t, ts_s):
    """Le um arquivo DOS 3.3 pela lista T/S. Devolve (pares, listas, bytes)."""
    pairs, lists, seen = [], [], set()
    while ts_t and (ts_t, ts_s) not in seen:
        seen.add((ts_t, ts_s))
        lists.append((ts_t, ts_s))
        l = sec(dsk, ts_t, ts_s)
        for i in range(122):
            t, s = l[12 + 2 * i], l[13 + 2 * i]
            if (t, s) != (0, 0):
                pairs.append((t, s))
        ts_t, ts_s = l[1], l[2]
    data = b"".join(sec(dsk, t, s) for t, s in pairs)
    return pairs, lists, data


def find_entry(dsk):
    v = sec(dsk, 17, 0)
    t, s = v[1], v[2]
    seen = set()
    while t and (t, s) not in seen:
        seen.add((t, s))
        c = sec(dsk, t, s)
        for i in range(7):
            e = 11 + i * 35
            if c[e] not in (0, 0xFF):
                return t, s, e
        t, s = c[1], c[2]
    raise SystemExit("ERRO: catalogo do modelo vazio")


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    ct2_path, tpl_path, out_path = argv[1:]

    # ---- fita
    files = read_ct2(ct2_path)
    game = None
    for fid, (start, data, complete) in files.items():
        if start == BODY_ADDR:
            game = (start, data, complete)
    if not game:
        raise SystemExit("ERRO: nao achei no .ct2 o arquivo que carrega em $%04X" % BODY_ADDR)
    start, body, complete = game
    if not complete:
        raise SystemExit("ERRO: o .ct2 esta truncado (faltam blocos de dados)")
    print("CT2: corpo do jogo $%04X-$%04X (%d bytes)" % (start, start + len(body) - 1, len(body)))

    # ---- modelo
    tpl = bytearray(open(tpl_path, "rb").read())
    if len(tpl) != 143360:
        raise SystemExit("ERRO: o modelo deve ter 143360 bytes (35 trilhas)")
    ct, cs, ce = find_entry(tpl)
    cat = bytearray(sec(tpl, ct, cs))
    entry = bytes(cat[ce:ce + 35])
    name = bytes(b & 0x7F for b in entry[3:33]).decode("latin1").rstrip()
    pairs, lists, old = read_file(tpl, entry[0], entry[1])
    addr = old[0] | old[1] << 8
    print("Modelo: arquivo '%s' (T/S %d/%d), %d setores de dados, carga $%04X"
          % (name, entry[0], entry[1], len(pairs), addr))
    if addr != LOAD_ADDR:
        raise SystemExit("ERRO: o modelo carrega em $%04X, esperado $%04X" % (addr, LOAD_ADDR))

    # preambulo = bytes do arquivo do modelo entre $07B0 e $08EF
    pre = old[4:4 + (BODY_ADDR - LOAD_ADDR)]
    payload = pre + body
    length = len(payload)
    blob = bytes([LOAD_ADDR & 255, LOAD_ADDR >> 8, length & 255, length >> 8]) + payload
    need = -(-len(blob) // SECSZ)
    print("Novo arquivo: $%04X-$%04X, %d bytes, %d setores de dados"
          % (LOAD_ADDR, LOAD_ADDR + length - 1, length, need))

    # ---- alocacao de setores (reaproveita os do modelo, acrescenta/solta o resto)
    vt = bytearray(sec(tpl, 17, 0))
    new_pairs = list(pairs[:need])
    if need > len(pairs):
        t, s = pairs[-1]
        while len(new_pairs) < need:
            s -= 1
            if s < 0:
                t, s = t - 1, 15
            if t < 3:
                raise SystemExit("ERRO: sem setores livres suficientes")
            if t == 17:
                continue
            if not vtoc_free(vt, t, s):
                raise SystemExit("ERRO: setor T%d S%d nao esta livre no modelo" % (t, s))
            vtoc_set(vt, t, s, False)
            new_pairs.append((t, s))
    else:
        for t, s in pairs[need:]:
            vtoc_set(vt, t, s, True)

    # ---- grava dados
    for i, (t, s) in enumerate(new_pairs):
        put(tpl, t, s, blob[i * SECSZ:(i + 1) * SECSZ])

    # ---- listas T/S
    n_lists = -(-need // 122)
    list_ts = list(lists[:n_lists])
    while len(list_ts) < n_lists:
        raise SystemExit("ERRO: faltam setores para listas T/S adicionais")
    for k in range(n_lists):
        t, s = list_ts[k]
        l = bytearray(SECSZ)
        if k + 1 < n_lists:
            l[1], l[2] = list_ts[k + 1]
        off = k * 122
        l[5], l[6] = off & 255, off >> 8
        for i, (pt, ps) in enumerate(new_pairs[off:off + 122]):
            l[12 + 2 * i], l[13 + 2 * i] = pt, ps
        put(tpl, t, s, bytes(l))
    for t, s in lists[n_lists:]:
        vtoc_set(vt, t, s, True)
    put(tpl, 17, 0, bytes(vt))

    # ---- catalogo: tamanho em setores (dados + listas)
    total = need + n_lists
    cat[ce + 33], cat[ce + 34] = total & 255, total >> 8
    put(tpl, ct, cs, bytes(cat))

    # ---- confere lendo de volta pelo catalogo
    ct2_, cs2_, ce2_ = find_entry(tpl)
    e2 = bytes(sec(tpl, ct2_, cs2_)[ce2_:ce2_ + 35])
    p2, l2, back = read_file(tpl, e2[0], e2[1])
    ok = (back[:len(blob)] == blob and len(p2) == need
          and (e2[33] | e2[34] << 8) == total
          and back[0] | back[1] << 8 == LOAD_ADDR)
    # tudo que e do jogo (corpo) tem que ser igual a fita
    base = 4 + (BODY_ADDR - LOAD_ADDR)
    ok = ok and back[base:base + len(body)] == body
    if not ok:
        raise SystemExit("ERRO: a verificacao falhou; nada foi gravado")
    open(out_path, "wb").write(tpl)
    print("OK: %s gravado (%d bytes). Arquivo '%s' = %d setores (%d dados + %d listas)."
          % (out_path, len(tpl), name, total, need, n_lists))
    print("Verificacao: corpo $%04X-$%04X identico ao .ct2." % (start, start + len(body) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
