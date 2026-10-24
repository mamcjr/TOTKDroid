# Plano: TOTKRecompAndroid

Recompilação estática de *The Legend of Zelda: Tears of the Kingdom* (Nintendo Switch, **1.4.2**: o jogo com a
última atualização) para Android, a partir do motor do [LAS Recomp](https://github.com/peiikai/LASRecomp), do seu
porte para Android ([LASRecompAndroid](https://github.com/mamcjr/LASRecompAndroid)) e do
[BOTWRecompAndroid](https://github.com/mamcjr/BOTWRecompAndroid), de onde este repositório saiu (com o histórico dos
dois). Como neles, não é emulação: o código ARM64 do jogo vira C, compilado para o celular, e o NVN (a API gráfica do
Switch) é traduzido para Vulkan.

O plano e as medições do BOTW estão em [docs/botw/BOTW-PORT.md](botw/BOTW-PORT.md); os do LAS para Android em
[docs/las/ANDROID-PORT.md](las/ANDROID-PORT.md). Muito do que o motor sabe hoje veio de lá (SDK 20, NVN 55, afinidade
de núcleos, ciclo de vida das texturas, render targets lineares, samplers, oclusão): está anotado onde foi feito.

## A descoberta que muda tudo: o SDK é o do BOTW 1.8.2

O `sdk` (nnSdk 20.5.6, com o NVN e o vi dentro) e o `subsdk0` (`multimedia.nss`) do TOTK 1.4.2 são **os mesmos
arquivos, byte a byte,** que os do BOTW 1.8.2 (builds `5cf080e8...` e `3308c1d2...`). O `main.npdm` pede os mesmos 49
serviços. Tudo o que o BOTWRecompAndroid já faz rodar no 1.8.2 - kernel do SDK 20 (variáveis de condição, setjmp,
`UserExceptionContextAddress`), os serviços, o NVN 55.15 (`nvnDeviceGetInteger`), o `nn::movie` com FFmpeg - vale
para o TOTK sem mudança. O que é novo é só o `main`: o jogo.

## O dump (medido em 2026-10-24 com `tools/totk_dump.py`)

O jogo `0100F2C0115B6000` 1.0.0 (NSP de 17 GB em `LASRecompAndroid/ROM/`) com a atualização `0100F2C0115B6800`
**v655360 = 1.4.2** (`Updates_Switch/`). O hactoolnet de 2019 não abre NSP: os NSPs (PFS0) foram desempacotados por
um script de 40 linhas, as title keys tiradas dos tickets (`.tik`, 0x180 / 0x2A0), e depois:

```
hactoolnet --titlekeys title.keys -t nca <NCA do programa da atualização> --basenca <NCA do programa do jogo> \
    --exefsdir Dump/0100F2C0115B6000/exefs --romfsdir Dump/0100F2C0115B6000/romfs
```

(o "Master Hash (FAIL)" da seção BKTR do patch não impede nada, como no Scarlet). `Dump/` fica fora do git.

| Módulo | Imagem | Símbolos | Relocações | O que é | Tratamento |
|---|---|---|---|---|---|
| `main` | 58,8 MiB (texto 43,6) | 706 | 501.144 | o jogo (`EX-King.nss`, build `Product_CSPGO_Optimize`): o motor da EPD depois do BOTW (sead, agl, gsys), Havok 2022.1 (AI, Animation, Cloth), NintendoWare (G3d2 e Vfx2 20.5.6, Ui2d e Font 15.3.1, Meshoptimizer) | recompilado |
| `sdk` | 12,3 MiB | 23.039 | 34.034 | nnSdk **20.5.6** com NVN e vi - o do BOTW 1.8.2 | recompilado + serviços HLE; NVN pelo nome |
| `subsdk0` | 6,4 MiB | 10.110 | 19.072 | `multimedia.nss` (nn::movie) - o do BOTW 1.8.2 | HLE (FFmpeg) |
| `rtld` | — | — | — | carregador dinâmico | não usado |

- Build id do `main` 1.4.2: `5cb42b1cf25469fb0635fd046453d843c18bc8ab` (o próprio romfs o repete em
  `System/RegionLangMask.txt`).
- `main.npdm`: programa `0100F2C0115B6000`, espaço de endereçamento de 39 bits (tipo 3), thread principal prioridade
  44 no núcleo 0, pilha 1 MiB, system resource 24 MiB.
- O `main` (58,8 MiB) cabe nos 64 MiB entre `MAIN_BASE` e `SDK_BASE` (`0x7004000000`) com 5 MiB de folga: o mapa de
  memória do runtime fica como está.
- `romfs`: **304.243 arquivos**, 15,9 GiB (o do BOTW 1.0.0 tinha 39.678). Quase tudo em Zstandard (`.zs`, com os
  dicionários em `Pack/ZsDic.pack.zs`), que o próprio jogo descomprime; as mensagens de cada idioma em `Mals/` - são 15
  regiões-idioma, **português do Brasil incluído** (`USpt`), como no BOTW 1.8.2; shaders em `Shader/*.bfsha.zs`; áudio
  em `Sound/Resource/*.bars.zs`; 28 vídeos WebM (1,5 GB) em `Event/Movie/` - os únicos arquivos que o runtime abre pelo
  caminho (`hle/movie.c`; o `keep_loose` do launcher já os separa pelo `.webm`).

## Diferenças em relação ao BOTW

| # | Diferença | Onde | Gravidade |
|---|---|---|---|
| 1 | **Tamanho.** O `main` tem o dobro do código do BOTW 1.8.2 (43,6 MiB de texto contra 20,6) e 228 mil funções (o BOTW 1.8.2 tem 122.961). Gerar e compilar leva o dobro, no celular também, e o `totkgame.so` fica grande. | `tools/gen.py`, `android/build_gamelib.sh`, `runtime/gamebuild.c` | Alta (tempo, memória) |
| 2 | **O renderizador é novo.** O TOTK refez boa parte do renderizador do BOTW (G3d2, Vfx2 e o agl / gsys da EPD), com mais passes de computação. Instruções Maxwell e usos de NVN que o BOTW não tinha devem aparecer no tradutor de shaders e no backend Vulkan. | `runtime/nvn/`, `runtime/nvn/maxwell/translate.c` | Alta |
| 3 | **Memória.** O TOTK usa quase toda a memória que um app tem no Switch (~3,2 GB); o heap do runtime vai até 3,25 GB (`HEAP_MAX`). Celulares de 12 GB ou mais. | `runtime/rt.h`, `kernel.c` | Média |
| 4 | **Nenhum hook de jogo.** Os do BOTW 1.8.2 (ritmo de quadros a 60, widescreen, o travamento ao recarregar) são de endereços do BOTW e não há decompilação do TOTK. As funções do runtime que eles chamavam ficam, sem uso, até os pontos do TOTK serem achados. O jogo roda a 30 fps no Switch, com resolução dinâmica. | `tools/patches.py`, `runtime/hle/` | Média (para melhorias) |
| 5 | **Desempenho.** Mais física (Ultramão, Zonai), céu e subterrâneo, mais shaders: o TOTK já sofre no Switch. | tudo | Alta |
| 6 | **304 mil arquivos no romfs** (7,7 vezes os do BOTW). Copiar pasta por pasta é lento: o `adb push` do romfs/ perdeu a conexão no meio (2026-10-24), e a configuração no celular abre cada arquivo pelo seletor de pastas do Android. | `android/push_game.sh`, `TOTKRecompAndroidActivity.java`, `romfs_build.c` | Média |

## Fases

### Fase 0: repositório e dump — **feita (2026-10-24)**

- Clone do BOTWRecompAndroid com o histórico (que inclui o do LASRecompAndroid e o do LAS Recomp). Remotos: `botw` /
  `botwl` (GitHub / `../BOTWRecomp`), `las` / `lasl`, `upstream` (peiikai/LASRecomp), `pkmsvl` (`../PKMSVRecomp`).
  Nada publicado (sem `origin`).
- `android/deps/out` e `tools/venv` são *junctions* para os do LASRecompAndroid, como no BOTW. Para apagar um link
  desses use `cmd /c rmdir <link>`, nunca um `rm -rf` (segue a junction e apaga o original).
- `third_party/libadrenotools` (submódulo) inicializado a partir da cópia local do BOTW.
- `Dump/` fora do git; `tools/totk_dump.py` confere os módulos, a versão, o NPDM e o romfs.
- O repositório usa `core.autocrlf=false` (arquivos com LF: o `GAMELIB_ID` é um hash deles).

### Fase 1: tirar o BOTW e compilar sem o jogo — **feita (2026-10-24)**

O PKMSVRecompAndroid (o porte do *Pokémon Scarlet*, também saído do BOTWRecompAndroid) já tinha feito esse trabalho de
forma genérica: os commits dele foram trazidos com os nomes trocados.

- Renome para **TOTKRecompAndroid**: pacote Java e applicationId `com.totkrecompandroid` (um app novo, ao lado do
  BOTW e do LAS), `TOTKRecompAndroidActivity`, `android/totkrecompandroid`, `totkgame.so`, `totkgen`,
  `totkrecompandroid.ini`, log `TOTKRecompAndroid`, chave de release em `~/.totkrecompandroid/`. As variáveis de
  ambiente continuam `LARECOMP_*` (o motor é o do LAS).
- Saiu o companion ZeppOS (o mapa do BOTW no relógio) e o servidor dele no app (sem permissão de INTERNET). As artes
  de Zelda (ícone da Triforce, fundo da tela inicial, atlas Sheikah dos controles de toque) e o visual Sheikah dos
  menus ficam.
- Versões: `GAME_V_142` (TOTK 1.4.2) é a versão principal, na raiz de dados. O mecanismo de versões lado a lado do
  BOTW (pastas `v<versão>`) ficou, sem nenhuma outra versão (`paths.c other_version`).
- `game_new_sdk()` vale para o TOTK: NVN 55 e as esperas do SDK 20, os caminhos do BOTW 1.8.2.
- O launcher e o app acham o romfs pelas pastas `Pack/` e `Mals/` (o do BOTW não tem `Mals/`).
- O program id vem do `main.npdm` (`g_program_id`: GetInfo 18, base das DLCs).
- Idiomas: os 13 do menu existem no jogo, nenhum vira inglês.
- `tools/patches.py` ficou vazio para o 1.4.2 (os patches do BOTW estão no histórico).
- **O app compila** (`gradlew assembleDebug`: 3 min 21 s, APK de 98 MB).

### Fase 2: gerar o código do jogo — **gerado (2026-10-24)**

O gerador do LAS rodou no TOTK (`--simd asm --dyn-base`, em `generated-android-dyn/`) sem mudança além dos pares
exclusivos do PKMSV:

| Módulo | Funções | Tabelas de salto | Arquivos C | C gerado | Tempo |
|---|---|---|---|---|---|
| `sdk` | 40.461 | 555 | 215 | 123 MB | 25 s |
| `main` | 228.251 | 2.243 | 1.678 | 907 MB | 3 min 49 s |

- **0 instruções sem suporte** no `main`. O TOTK tem 6 pares exclusivos de 16 bytes (`LDXP` / `STXP`): sem o lifter
  do PKMSV (`tools/test_excl_pair.py`) seriam 6 armadilhas. No `sdk`, 1 (dados lidos como código, igual ao BOTW 1.8.2).
- `android/build_gamelib.sh Dump/0100F2C0115B6000/exefs` compila o `totkgame.so` com o NDK: **compilou e linkou
  (2026-10-24)**, 1.897 arquivos, 11 no -O0 (os que têm uma função de mais de 15 mil rótulos: `heavy.txt`), 272,6 MB
  sem debug info. O ID (`7f464d46a5a632a7`) bate com o do app, e os 8 símbolos que ele pede ao runtime (`rt_dispatch`,
  `rt_svc`, `rt_udf`, `rt_ticks`, `rt_setjmp_slot`, `hle_longjmp`, `fnprof_hit`, `g_fnprof`) existem no `libmain.so`.
- Tempo: ~4 h de relógio (65 min até ser interrompida, 2 h 56 min ao retomar), dividindo a máquina com outra
  compilação. Os mais lentos são arquivos com funções grandes logo abaixo do limite do -O0: `main_591.c` (9.745
  rótulos) levou mais de 2 h, `main_001.c` (14.661) e `main_584.c` (12.348) perto de 1 h cada. Baixar o limite para
  ~9 mil encurtaria a compilação (no celular também), ao custo de código mais lento nessas funções.

### Fase 3: boot até o primeiro quadro — **feita (2026-10-24)**

Feito sem rodar o jogo (`tools/imports.py <exefs>`):

- **Importações do `main` (679):** 629 vêm do `sdk`, 42 de implementações do runtime (26 do SDK, 16 do `nn::movie`).
  Sobram só os 8 `glslc*` (os mesmos do LAS e do BOTW, nunca chamados). Nada de rede além do que o BOTW 1.8.2 já
  importa.
- **Serviços:** os mesmos 49 do BOTW 1.8.2.

Levar o jogo ao celular:

- `android/build_romfs.sh Dump/0100F2C0115B6000` faz o `romfs.bin` aqui no PC com `tools/romfs_build.py`, o
  `romfs_build.c` do app em Python. O `cc` do WSL foi tentado primeiro: lendo o romfs pelo `/mnt/d`, levou 31 minutos
  para varrer 9 das 52 pastas. O `tools/test_romfs_build_py.py` confere que os dois dão a mesma imagem, byte a byte
  (uma pasta sintética e `Mals/`, `System/` e `Pack/` do jogo). Só que no Windows o Defender examina cada arquivo que
  ele abre (~50 ms por arquivo, horas para os 304 mil). **O que se usa no Windows:** a imagem do próprio jogo, que o
  hactoolnet grava inteira (`--romfs Dump/0100F2C0115B6000/romfs.bin` no lugar do `--romfsdir`, e um
  `touch Dump/0100F2C0115B6000/.romfs.bin.built`): 17,05 GB em 2 min 53 s. O runtime lê qualquer imagem RomFS pelo
  cabeçalho (`services/fs.c rom_index`), e essa tem o mesmo layout da nossa (dados em 0x200, tabelas depois). Conferida
  contra a pasta extraída (2026-10-24): os 304.243 arquivos com os mesmos tamanhos, e 328 de 328 iguais byte a byte
  (300 sorteados e os 28 vídeos).
- `ROMFS_HOST=1 android/push_game.sh Dump/0100F2C0115B6000` manda o exefs, esse `romfs.bin` num arquivo só, os 28
  vídeos e o `totkgame.so`, em vez dos 304 mil arquivos do romfs. Sem o `ROMFS_HOST`, o caminho antigo (romfs/
  inteiro, `romfs.bin` feito no celular).
- O `push_game.sh` agora só dá uma parte por enviada com o marcador `.<parte>.pushed`, depois de conferir o número de
  arquivos, e faz o `romfs.bin` em `romfs.bin.part` (renomeado no fim). Antes, uma pasta não vazia contava como
  enviada: a cópia que caiu no meio (2026-10-24) viraria um `romfs.bin` faltando arquivos.
- O envio com `ROMFS_HOST=1` levou 9 min 43 s (o `romfs.bin` inteiro, os vídeos, o `totkgame.so`).
- O HyperOS do POCO recusa `adb install` em silêncio (`INSTALL_FAILED_USER_RESTRICTED`, sem aviso na tela) até
  "Instalar via USB" ser ligado de novo nas opções do desenvolvedor.

O primeiro boot (POCO F7 Ultra, driver Vulkan da Qualcomm, 2026-10-24), boot a boot (`android/run_boot.sh`):

| Boot | Até onde foi | O que parou | Correção |
|---|---|---|---|
| 1 | `nn::init`, os 10.717 inicializadores do `main`, o índice do romfs (304.243 arquivos), o idioma (pt-BR), o NVN e a janela | `VammManager::AllocateAddressRegion` num gerenciador nunca criado: o SDK só liga a memória virtual (`nn::os::AllocateAddressRegion` / `AllocateMemoryPages`, sobre a região de alias) se o `GetInfo 16` (system resource) não for 0, e o jogo a usa sem perguntar | `GetInfo 16` = o system resource do `main.npdm` (24 MiB no TOTK, 0 no BOTW) |
| 2 | mais 13 threads, mais leituras do romfs | `no function at 0x1001bfabc`: o `memcpy` do jogo volta por `adr x19, volta` ... `br x19`, que o gerador mandava ao despacho | `tools/gen.py`: um `br xN` com `adr xN` na função vira um switch sobre esses alvos (6 `br` do `main`, 4 arquivos C) |
| 3 | idem | `too many regions`: o mapa de memória do kernel tinha 1.024 entradas fixas, e o VammManager mapeia memória física de 128 KiB em 128 KiB | mapa que cresce, regiões físicas vizinhas juntas, `UnmapPhysicalMemory` por faixa e devolvendo a memória (`madvise`) |
| 4-5 | o renderizador de áudio, o `hwopus` | ponteiro nulo: o heap de som do jogo (do tamanho que o `System/HeapSize` dele diz) acabou | o `GetWorkBufferSize` do `audren` dizia 16 MB para qualquer renderizador (agora ~1 MB, pelos parâmetros); o `hwopus` virou um serviço de verdade (`runtime/services/hwopus.c`, Opus pelo FFmpeg do app) |
| 6 | saves montados, o relógio | `nn::fs::OpenCacheStorageList`: o `fsp-srv` cmd 62 respondia "ok" sem o objeto | `OpenSaveDataInfoReader*` (60, 61, 62, 68) devolvem um leitor sem saves |
| 7 | **o primeiro quadro: a abertura ("Nintendo apresenta"), a 30 fps, por uns 10 s**; o jogo procura os saves (nenhum), o álbum, toca o áudio | a GPU perdida (*device lost*) depois de ~450 envios | Fase 4 |

Visto no 7º boot e ainda não tratado:

- `am` ICommonStateGetter cmd 66 e ISelfController cmd 62, `prepo:u` cmd 10105 (relatórios de jogo: o stub serve),
  `nfp:user` (amiibo).
- `audren`: os efeitos 2, 5 e 7 (não implementados no mixer) e *wave buffers* "fora da memória do guest" (os
  parâmetros de voz lidos num layout errado para a revisão do TOTK).
- `[nvn] ReadTexels` e `WriteTexels` parcial: não implementados (o jogo os chama em série).

### Fase 4: gráficos — **em andamento**

O 7º boot chega à abertura e cai quando o jogo passa dela:

- O tradutor Maxwell não conhece `VOTE` (`0x50D8...`, votação entre as threads de um warp) nem `RED` (`0xEBF8...`,
  redução atômica em memória global): fragment shaders que falham na tradução.
- Compute shaders lidos como zeros ("unknown instruction 0 at 0x8"): o endereço do código deles deve estar errado
  para o TOTK.
- Depois, *device lost* no driver da Qualcomm (`vkWaitSemaphores`). O BOTW roda melhor no Turnip: vale testar com ele.
- Meta: a tela título.

### Fase 5: áudio, vídeos, saves, entrada

### Fase 6: Android

- `totkgame.so` com o NDK (`android/build_gamelib.sh Dump/0100F2C0115B6000/exefs`); `romfs.bin` feito direto da pasta
  escolhida; a compilação no celular (`runtime/gamebuild.c`) com 228 mil funções.

### Fase 7: desempenho

- Ritmo de quadros, resolução, widescreen: achar os pontos do TOTK (`tools/patches.py`).

### Fase 8: distribuição

- APK sem nada do jogo.

## Regras

- Nada da Nintendo no repositório: nem o dump, nem código gerado, nem assets (ver `CONTRIBUTING.md`).
- Mudanças no motor que não dependem do TOTK devem ficar fáceis de levar de volta ao LAS e ao BOTW (e vice-versa).
