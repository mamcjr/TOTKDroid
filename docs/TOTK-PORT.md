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
- `[nvn] ReadTexels` e `WriteTexels` parcial: não implementados (o jogo os chama em série). (Feitos na Fase 5.)

### Fase 4: gráficos — **a tela título (2026-10-24)**

O 7º boot chegava à abertura e caía quando o jogo passava dela. O que faltava:

- **Instruções Maxwell** (`runtime/nvn/maxwell/translate.c`): `VOTE` (subgroupAll / Any / AllEqual e o ballot, com as
  extensões de subgrupo só nos shaders que o usam), `RED` / `ATOM` (atômicos de 32 bits na memória global, pelo mesmo
  endereço e cópia local do `LDG` / `STG`), `ATOMS` (na memória compartilhada), `VMNMX` (min / max de vídeo: min de
  três, *clamps*) e `LEA` / `LEA.HI` (registrador, cbuf, imediato). O fuzz do `tools/test_translate.c` (8.000
  programas, o GLSL compilado pelo shaderc do WSL) não mostrou falha nova - as 35 que restam são de `textureGather`,
  antigas. Para rodá-lo sem o MSYS2: compilar `translate.c` e o teste no WSL (`-lshaderc -lSPIRV-Tools`) com um stub do
  tradutor de referência, modo `glsl`.
- **Compute shaders com cabeçalho:** os do TOTK começam com um cabeçalho NVN de marca `0x12345679` (os gráficos:
  `0x12345678`) e o código em 0x100; o tradutor lia o cabeçalho como código. Os do LAS e do BOTW não tinham cabeçalho.
- **Driver:** com o da Qualcomm a GPU se perde (*device lost* em `vkQueueSubmit2`, sem informação de falha) logo
  depois da abertura, sempre no mesmo ponto - o rastro (`LARECOMP_VK_DEVLOST_TRACE=1`) não aponta um passe. **Com o
  Turnip (Gen8 V37, o do BOTW) não**: o jogo chega à tela título e fica nela (20-30 fps, 25-40 mil draws e 500-850
  dispatches a cada 2 s). Para testar pelo adb (o app de debug aceita `run-as`): `adb push libvulkan_freedreno.so
  /data/local/tmp/`, `run-as com.totkrecompandroid` para copiá-lo em `files/drivers/Turnip_Gen8_V37/`, e
  `android/run_boot.sh 150 LARECOMP_VK_DRIVER=/data/user/0/com.totkrecompandroid/files/drivers/Turnip_Gen8_V37/libvulkan_freedreno.so`
  (a activity não acha sozinha um driver copiado assim: o caminho tem de vir na variável).
- **A tela título** (Turnip, 2026-10-24): o logo, o fundo animado dos murais Zonai, o menu em português (Continuar /
  Novo jogo / Opções / amiibo), a 30 fps; os controles de toque navegam (o `input tap` do adb é curto demais: `input
  swipe x y x y 300`). Na primeira vez fica preta, só com o indicador de carregamento, por minutos: os shaders sendo
  compilados (o cache de pipelines passa de 5 MB); depois, ~40 s até ela.

Falta:

- O *device lost* no driver da Qualcomm.
- `textureGather` em alguns tipos de textura (`ReadTexels` / `WriteTexels`: feitos na Fase 5).
- A cena só ocupa 16:9 no meio da tela de 20:9 (widescreen: Fase 7).

### Fase 5: áudio, vídeos, saves, entrada — **em andamento**

- **Novo jogo — o prólogo, jogável (Turnip, 2026-10-24).** Antes caía em ~15 s, quando os sons começavam: o TOTK
  manda o `RequestUpdate` do `audren` na **revisão 15** (`REV?`), cujas vozes têm **0x188 bytes**: os dois biquads
  viraram float (ativo, 3 reservados, f32 numerador[3] e denominador[2]: 0x18 cada, não os 0xC dos Q14) e todo campo
  depois deles anda 0x18. Lidas de 0x170 em 0x170, toda voz depois da primeira era lixo (wave buffers em
  `0x380000000000`). Conferido num dump real de uma atualização (`LARECOMP_AUDIO_DUMP=<pasta>`: ids, formatos ADPCM,
  taxas, wave buffers e ids de canal no lugar). Agora: "Nintendo apresenta", "Tears of the Kingdom", a abertura (as
  tochas na caverna sob o castelo) e o **Link controlável** no prólogo, com o HUD e as dicas em português, a ~20 fps.
- **A caverna em magenta — resolvido (2026-10-24).** Não eram os suspeitos do log: o `nvnTextureGetViewOffset` era
  um stub que devolvia 0. O carregador do TOTK descomprime cada mip dos `TexToGo/*.txtg` (um quadro zstd por mip, já
  em block-linear, cortado no último byte usado: o mip 3 de um BC5 128x128 tem 384 bytes, não 256) no deslocamento
  que esse `GetViewOffset` dá para a vista daquele nível; com 0, só o nível 0 ficava na textura e os mips liam zero.
  Em ASTC (os albedos do TOTK) um bloco zerado é bloco de erro: **magenta**. As rochas tinham a textura certa de perto
  e magenta de longe, e o cubemap de ambiente que o jogo captura da própria caverna saía magenta e iluminava tudo de
  magenta. O próprio jogo lia zeros: `ReadTexels` do nível 1 de uma textura BC4 1024x1024 para compor o terreno.
  Como se achou (ferramentas que ficaram): `LARECOMP_DEBUG_FRAME` + `LARECOMP_DEBUG_DIR=/data/user/0/com.totkrecompandroid/files/dbg`
  (os passes num lugar que o `run-as` lê: a pasta externa do app o shell do HyperOS não lê) e `LARECOMP_DEBUG_RT=<n>`
  (o alvo de cor n do passe: o albedo do G-buffer é o 1), `LARECOMP_DUMP_FS=<hash>[,...]` (o GLSL),
  `LARECOMP_LOG_CBUF=<hash>:<bancos>` + `LARECOMP_LOG_CBUF_FROM=<quadro>` (os bancos de constantes),
  `LARECOMP_LOG_ASTC_ZERO=1` (níveis ASTC enviados com blocos zerados) e `LARECOMP_NO_ASTC=1` (decodifica ASTC na CPU:
  separa dado inválido de decodificador).
- Também nesta rodada: `ReadTexels` / `WriteTexels` (com e sem stride) de qualquer região, nível e camada
  (`tegra_copy_region`), e as instruções de ponto flutuante duplo (`DADD`, `DMUL`, `DFMA`, `DMNMX`, `DSETP`, F64 em
  `F2F` / `F2I` / `I2F`) emuladas em fp32 - o Adreno não tem `shaderFloat64`.
- Vistos depois disso (a investigar): as **vozes** dos personagens mudas e o **corpo do Link se deformando**
  (resolvidos, abaixo) e **piscadas** na imagem.
- **O corpo deformado - resolvido (2026-10-25).** Não eram os ossos: o tronco, os braços e a cabeça do Link e quase
  toda a Zelda viravam "varetas" esticadas para um mesmo ponto, e no G-buffer o Link só tinha pernas e espada. O
  skinning de 4 ossos do TOTK (VS `247c7c700`) guarda o endereço da matriz de um osso + 32 e lê as duas primeiras
  linhas em -32 / -16: `LDC c[5][r + 0xFFE0]`. O deslocamento do `LDC` é **com sinal** (s16; Ryujinx `Imm16ToSInt`,
  yuzu `BitField<20, 16, s64>`) e o tradutor o lia sem sinal: o endereço passava 64 KB, o `cbr` o limitava ao último
  vec4 e duas das quatro matrizes saíam lixo. Índices, pesos e paletas conferidos nos dumps (todos dentro, nenhuma
  matriz zerada). `tools/test_translate_ops.sh`: 31 casos (2 de `LDC`), 0 falhas.
  Ferramentas que ficaram: `<LARECOMP_DEBUG_DIR>/dump_now` (criado pelo adb: o F9 no celular, despeja o próximo
  quadro), `LARECOMP_LOG_DRAWS=1` (cada draw do quadro despejado: shaders, formato / deslocamento / stride de cada
  atributo, tamanho de cada cbuf do vertex shader) e `LARECOMP_DEBUG_SWEEP=<fs hash>` + `LARECOMP_DEBUG_FRAMES=<n>`
  (o passe que começa por esse shader, um quadro sem cada um dos seus draws: qual draw estraga a imagem).
- **As vozes - resolvido (2026-10-25).** Eram duas coisas:
  - **Opus:** o FFmpeg do app não tinha o decodificador (ele precisa do `libswresample`, que o
    `android/deps/build_ffmpeg.sh` deixava de fora) e o `hwopus` devolvia silêncio. Agora o script compila o FFmpeg
    com o swresample numa pasta só do TOTK, `android/deps/ffmpeg` (o `android/deps/out` é o do LASRecompAndroid, uma
    junction: um `libavcodec` que pede o `libswresample` lá impediria os apps do LAS e do BOTW de carregar), e o
    CMake / Gradle a preferem quando existe. As falas são Opus mono a 48 kHz; o ambiente da caverna, 6 canais Opus.
  - **Splitters:** a maior parte dos sons do TOTK vai por *splitters* (a voz com mix `0x7FFFFFFF` e um splitter id;
    o "não usado" do mix é `0x7FFFFFFF`, não negativo, e o aviso que existia nunca disparava): o `audren` os
    descartava. Agora cada canal c da voz vai aos destinos c, c + canais, ... do splitter, cada um ao seu mix com os
    seus volumes (e os sub mixes por splitter, o destino k levando o buffer k % buffers). O layout da REV15, visto nos
    dumps (`LARECOMP_AUDIO_DUMP` grava as primeiras atualizações que mudam splitters, `audren_split_<n>.bin`): `SNDI`
    {id, taxa, n, n ids} + 12 bytes; `SNDD` de 0xA8 bytes (os biquads float antes) com `is_used` em 0x9C. Música e
    ambiente vêm em pares de vozes ADPCM mono meio / lado (volumes (a, a) e (b, -b)) para os mixes 3, 4 e 5.
  - Conferido sem ouvir: `LARECOMP_OPUS_RAW=<pasta>` grava o que cada decodificador Opus decodificou e
    `LARECOMP_AUDIO_WAV=<arquivo>` a mixagem final; a fala está na mixagem com correlação 0,995 (antes 0,10), e
    `LARECOMP_LOG_OPUS=1` mostra o pico de um em cada 200 pacotes.
- Efeitos de áudio 2 (aux), 5 e 7 do `audren`: não implementados.

### Fase 6: Android

- `totkgame.so` com o NDK (`android/build_gamelib.sh Dump/0100F2C0115B6000/exefs`); `romfs.bin` feito direto da pasta
  escolhida; a compilação no celular (`runtime/gamebuild.c`) com 228 mil funções.

### Fase 7: desempenho

- Ritmo de quadros, resolução, widescreen: achar os pontos do TOTK (`tools/patches.py`).
- **Trazido do LASRecompAndroid e do BOTWRecompAndroid (2026-10-25)** - o que o motor ganhou lá depois que o BOTW saiu
  do LAS (2026-10-17) e não depende do jogo:
  - **Memória global por ponteiro** (LAS `1883b8d`): o heap do TOTK (2144 MB) passa do alcance de um storage buffer
    no Adreno 830 (2047 MB); acima disso os shaders liam zero, salvo os storage buffers ligados ao draw (as janelas
    do BOTW, `gwin_mode`). Agora, com `bufferDeviceAddress`, todo acesso alcança o heap inteiro; as janelas só sem
    ele. `LARECOMP_VK_GMEM_BDA=0/1`.
  - **UBWC** (LAS `4380fe5`): os render targets listam os formatos das suas vistas e o Turnip os comprime (-8% de
    GPU no S20 FE da medição original). `LARECOMP_VK_FMTLIST=0` desliga.
  - **Cópias de cor preguiçosas** (LAS `d0b1c66`): cópias de imagem inteira viram alias e só se fazem quando alguém
    lê o destino (~1 ms de GPU no LAS). `LARECOMP_VK_COPY_LAZY=0` desliga.
  - **Checagem de texturas** (LAS `05b5f2b`, `e811c6e`): hash completo em 8 trilhas; sem amostra para texturas
    dinâmicas; e cada textura identificada uma vez por conteúdo (o `-1` de "nenhuma" virava 0, "não checada": toda
    textura sem patch era identificada de novo a cada checagem). No Android a amostra fica nos 1024 pontos do BOTW.
  - **Build do código do jogo** (LAS `d2da382`): o maior arquivo pronto primeiro e `-mllvm -dag-maps-huge-region=500`
    (o escalonador quadrático em sequências enormes de stores: 4,3 s -> 0,7 s num arquivo) - no celular e no PC
    (`android/gamelib/CMakeLists.txt`, fora das flags que entram no id do código: o `totkgame.so` atual vale).
  - Correções do tradutor: `LDC.64` com o registrador de endereço também destino (LAS `764e4bf`), `I2I .SAT`, modos
    do `SHF`, `HSETP2 .H_AND` (LAS `d813760`; `tools/test_translate_ops.sh`: 29 casos, 0 falhas). O fuzz GLSL
    (8.000 programas): 4.231 compilados e validados, as 26 falhas são as de `textureGather` de antes.
  - Compatibilidade: sem import do heap por padrão (LAS `4a8749f`, Xclipse / Mali), BC4/BC5 com sinal na CPU (LAS
    `bf48501`), Vulkan sem synchronization2 / estado dinâmico / demote / 16-bit (BOTW `2c08088`), a causa de uma falha
    da preparação dos arquivos na tela (BOTW `ffca023`).
  - Ficaram de fora: o que é do LAS (ganchos do jogo, versões 1.0.1/1.1.0, PhysX, warps, ultrawide), os tamanhos
    próprios dos alvos de brilho/água do LAS (`95c5b52`), o build quente/frio (precisa de uma lista de funções
    quentes do TOTK) e a **geração de quadros** por reprojeção (LAS `4ab0b3d` e seguintes, ~700 linhas com
    heurísticas da cena do LAS) - candidata para depois, com o TOTK a 20-30 fps.

### Fase 8: distribuição

- APK sem nada do jogo.

## Regras

- Nada da Nintendo no repositório: nem o dump, nem código gerado, nem assets (ver `CONTRIBUTING.md`).
- Mudanças no motor que não dependem do TOTK devem ficar fáceis de levar de volta ao LAS e ao BOTW (e vice-versa).
