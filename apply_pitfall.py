#!/usr/bin/env python3
# apply_pitfall.py -- Pitfall II: botao Y do pad -> tecla "A" (iniciar o jogo).
#
# Uso (no Git Bash use barra normal /):
#     python apply_pitfall.py src/libretro.cpp
#
# Funciona nos 3 estados do arquivo:
#   1) patch ainda nao aplicado        -> aplica tudo, ja com o botao Y
#   2) versao antiga aplicada (botao B) -> troca B por Y
#   3) ja esta com Y                    -> nao faz nada
#
# Valida TUDO em memoria antes de gravar: se qualquer ancora nao for
# encontrada exatamente 1 vez, aborta sem alterar nada. Antes de gravar,
# salva <arquivo>.bak_pitfall (so se ainda nao existir).
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "libretro.cpp"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

with open(path, "r", encoding="utf-8", newline="") as f:
    src = f.read()

nl = "\r\n" if "\r\n" in src else "\n"
def N(s):
    return s.replace("\n", nl)

OLD_B_BLOCK = N("\t\tif (b != g_pitADown) {\n"
                "\t\t\tg_pitADown = b;\n"
                "\t\t\tg_machine->padKeyEvent(b, RETROK_a);\n")
NEW_Y_BLOCK = N("\t\tif (y != g_pitADown) {\n"
                "\t\t\tg_pitADown = y;\n"
                "\t\t\tg_machine->padKeyEvent(y, RETROK_a);\n")

if "current_image_is_pitfall" in src:
    if NEW_Y_BLOCK in src and OLD_B_BLOCK not in src:
        sys.exit("Ja esta com o botao Y. Nada foi alterado.")
    # ---- estado 2: troca B por Y ----
    edits = [
        (OLD_B_BLOCK, NEW_Y_BLOCK),
        (N("\t\t// B = tecla \"A\" (inicia); o Fire 2 nativo"),
         N("\t\t// Y = tecla \"A\" (inicia); o Fire 2 nativo")),
        (N("// (2o botao) vira a tecla A; o A do pad continua sendo o botao 1 do joystick (pular).\n"),
         N("// vira a tecla A (Y do pad); o A do pad continua sendo o botao 1 do joystick (pular).\n")),
        (N("// O jogo so' sai da tela de titulo com a tecla \"A\" (o joystick nao inicia). Entao o B do pad\n"),
         N("// O jogo so' sai da tela de titulo com a tecla \"A\" (o joystick nao inicia). Entao o botao\n")),
    ]
else:
    # ---- estado 1: aplica tudo, ja com Y ----
    edits = [
        (N("bool g_chopLDown = false;\n"),
         N("bool g_chopLDown = false;\n"
           "\n"
           "// --- Per-game mapping: Pitfall II ---\n"
           "// O jogo so' sai da tela de titulo com a tecla \"A\" (o joystick nao inicia). Entao o botao\n"
           "// vira a tecla A (Y do pad); o A do pad continua sendo o botao 1 do joystick (pular).\n"
           "bool g_pitADown = false;\n")),
        (N("\treturn name.find(\"chop\") != std::string::npos;\n}\n"),
         N("\treturn name.find(\"chop\") != std::string::npos;\n}\n"
           "\n"
           "bool current_image_is_pitfall() {\n"
           "\tif (g_images.empty() || g_imageIndex >= g_images.size()) {\n"
           "\t\treturn false;\n"
           "\t}\n"
           "\tstd::string name = baseName(g_images[g_imageIndex]);\n"
           "\tfor (char& c : name) {\n"
           "\t\tc = (char)std::tolower((unsigned char)c);\n"
           "\t}\n"
           "\treturn name.find(\"pitfall\") != std::string::npos || name.find(\"pit_fall\") != std::string::npos;\n"
           "}\n")),
        (N("void release_karateka_keys() {\n"),
         N("void release_karateka_keys() {\n"
           "\tif (g_pitADown) {\n"
           "\t\tg_pitADown = false;\n"
           "\t\tif (g_machine) {\n"
           "\t\t\tg_machine->padKeyEvent(false, RETROK_a);\n"
           "\t\t}\n"
           "\t}\n")),
        (N("\t} else {\n"
           "\t\tif (g_chopLDown) {\n"
           "\t\t\tg_chopLDown = false;\n"),
         N("\t} else if (current_image_is_pitfall()) {\n"
           "\t\t// Y = tecla \"A\" (inicia); o Fire 2 nativo fica solto para nao disparar o \".\" junto.\n"
           "\t\tg_machine->padEvent(left, right, up, down, a, false);\n"
           + NEW_Y_BLOCK.replace("\r\n", "\n") +
           "\t\t}\n"
           "\t} else {\n"
           "\t\tif (g_pitADown) {\n"
           "\t\t\tg_pitADown = false;\n"
           "\t\t\tg_machine->padKeyEvent(false, RETROK_a);\n"
           "\t\t}\n"
           "\t\tif (g_chopLDown) {\n"
           "\t\t\tg_chopLDown = false;\n")),
    ]

# --- valida tudo em memoria ---
for i, (old, new) in enumerate(edits, 1):
    c = src.count(old)
    if c != 1:
        sys.exit("ERRO: ancora %d encontrada %d vez(es) (esperado 1). Nada foi alterado." % (i, c))

out = src
for old, new in edits:
    out = out.replace(old, new, 1)

for tok in ("g_pitADown", "current_image_is_pitfall()", "padKeyEvent(y, RETROK_a)"):
    if tok not in out:
        sys.exit("ERRO: verificacao final falhou (%s). Nada foi alterado." % tok)
if OLD_B_BLOCK in out:
    sys.exit("ERRO: restou o bloco do botao B. Nada foi alterado.")

bak = path + ".bak_pitfall"
if not os.path.exists(bak):
    shutil.copyfile(path, bak)
with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(out)
print("OK: %d edicoes aplicadas em %s (backup: %s)" % (len(edits), path, bak))
