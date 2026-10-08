# tk2000_libretro

Core libretro do **TK2000** (o clone brasileiro do Apple II feito pela
Microdigital), portado do emulador standalone de Fábio Belavenuto
(https://github.com/fbelavenuto/tk2000, GPL-2.0).

Ao contrário do MAME (que emula o TK2000 apenas como uma variação do
driver do Apple II), este core roda a **ROM original do TK2000
embutida** (16 KB, extraída de `src/Rom.cpp` do projeto original) sobre
uma reimplementação dedicada do 6502, do vídeo (280×192, mono ou
"colorido" via artifacting NTSC simulado) e do beeper — o mesmo núcleo
de emulação do projeto original, só que sem SDL/janela/thread, para
caber no modelo de `retro_run()` por frame do libretro.

## O que foi portado e o que foi reescrito

| Arquivo | Origem | O que mudou |
|---|---|---|
| `Cpu6502.*`, `Bus.*`, `Rom.*`, `Ram.*`, `Video.*`, `Audio.*`, `Tape.*` | copiados do projeto original | só ganharam `save/loadState()` para savestate; lógica de emulação intocada |
| `Keyboard.*` | adaptado | `keyEvent(SDL_KeyboardEvent&)` → `keyEvent(bool down, unsigned keycode, uint16_t mod)`; todo o `switch` foi um rename mecânico `SDLK_*` → `RETROK_*` (o `retro_key` do libretro é, símbolo a símbolo, o mesmo enum do SDL 1.2 em que o código original já se baseava) |
| `Machine.*` | reescrito | o original possuía janela SDL, thread de CPU e menu on-screen; aqui virou uma classe fina com `runFrame()`, `getFrameBuffer()`, `takeAudioSamples()`, controle de fita e savestate |
| `libretro.cpp` | novo | implementa a API libretro (`retro_init`, `retro_run`, `retro_load_game`, `retro_serialize*`, etc.) por cima de `CMachine` |
| `libretro.h` | novo | subconjunto enxuto (mas ABI-compatível) do header oficial — só o que este core usa |
| `WindowSDL.*`, `AudioSDL.*`, `Menu.*`, `main.cpp` | **descartados** | eram só a camada de janela/UI SDL; o frontend libretro assume esse papel |

## Build

```
make
```

Gera `tk2000_libretro.so` (Linux), `.dylib` (macOS) ou `.dll` (Windows,
cross-compilando com `platform=win` e `CROSS_COMPILE=...`).

## Controles

- **Teclado físico**: o core usa o `retro_keyboard_callback` do
  libretro, então digitar no teclado do computador vai direto pra
  matriz de teclado do TK2000 (BASIC/monitor). No RetroArch, o
  "Game Focus" (normalmente tecla Scroll Lock) pode ser necessário
  pra liberar o teclado pro core em vez dos atalhos do frontend.
- **Joypad** (RetroPad):
  - `A` — play/pause da fita cassete (.ct2) inserida
  - `B` — rebobina a fita
  - `Start` — reset (equivalente ao F5 do emulador original)
  - `Select` — hard reset (também limpa a RAM)

## Carregar fitas (.ct2)

`retro_get_system_info` anuncia a extensão `ct2` e `need_fullpath =
true` (o `CTape::insertCt2()` original lê por caminho de arquivo, não
por buffer em memória — mantido assim para não mexer nessa lógica).
O core também sobe sem conteúdo nenhum (`supports_no_game`), abrindo
direto no monitor/BASIC da ROM, exatamente como o TK2000 real ligando
sem fita.

## Savestates

Implementados via `retro_serialize`/`retro_unserialize`: registradores
e ciclos da CPU, os 64 KB de RAM inteiros, estado de vídeo/áudio/
teclado e a posição de leitura da fita. Testado com round-trip
save→load sem quebrar a execução (ver `test/smoke_test.cpp`).

## Teste de fumaça

`test/smoke_test.cpp` carrega o `.so` via `dlopen`, simula os
callbacks de um frontend, roda alguns segundos de emulação, digita
"HI"+ENTER pelo `retro_keyboard_callback` e despeja o framebuffer
resultante em PPM — confirmando visualmente que a ROM inicializa e
que o teclado chega até o BASIC (a tela mostra a mensagem de erro
`?SINTAXE #ERRO`, em português, confirmando que é a ROM original
brasileira e não uma aproximação via Apple II).

```
g++ -std=c++17 -O1 test/smoke_test.cpp -o /tmp/smoke_test -ldl
cd /caminho/do/tk2000_libretro.so/ && /tmp/smoke_test
```

## Licença

GPL-2.0, herdada do projeto original de Fábio Belavenuto.
