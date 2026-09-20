# Plano: água com ondas no addon

## O desejo do usuário

> Qual material eu devo aplicar a uma textura para ela ter ondas?

Nenhum: ondas no DKR não são material de textura. O pedido de fundo é poder ter
água com ondas numa pista feita do zero (Track From Mesh), coisa que hoje só se
consegue remixando uma pista retail que já tem ondas.

Estado: **implementado** (2026-09-16). O painel *Water* do addon cria água com
ondas ou parada; `tools/blender/dkr_track_editor/water.py` tem as regras e
`tests/test_water.py` as confere contra os 22 modelos retail com ondas. A seção
*Implementação* no fim diz o que mudou em relação a esta proposta. Tudo foi lido
no decomp (`extern/dkr-decomp/src`) e medido nos modelos retail extraídos
(`us.v77`/`us.v80`).

---

## O que o jogo faz

### Ondas não são material

- `SURFACE_WATER_WAVY` (14) nunca aparece numa tabela de texturas retail: é o
  tipo que o jogo dá ao plano de água que ele mesmo sintetiza (`tracks.c:2636`).
  Aplicado a uma textura, não faz nada.
- A água com ondas é uma **simulação** (`waves.c`) que desenha a sua própria
  malha, com a textura do header (`/waves/texture-ID`, retail
  `ASSET_TEX2D_WATER_DETAIL`). A geometria de água marcada deixa de ser
  desenhada: `RENDER_WATER` — *"Not rendered if the block is utilising
  wavegen"* (`textures_sprites.h:59`).
- Só em single-player: `init_track` só conta segmentos com ondas quando
  `numberOfPlayers < 2` (`tracks.c:218`). Em tela dividida a simulação nem
  começa.

### O que liga a simulação

1. **`hasWaves` do segmento** (byte `0x2B` do `LevelModelSegment`). `init_track`
   conta os segmentos com `hasWaves != 0`; se nenhum, `waves_init` não roda
   (`tracks.c:216-236`). Retail grava `-1` (`0xFF`).
2. **Batch de água**: flags `RENDER_WATER` (bit 13) + `RENDER_UNK_0400000`
   (bit 22). A altura da água do ladrilho é o Y do primeiro vértice desse batch
   (`waves.c:1459-1465`). No retail o batch é plano, sem colisão
   (`RENDER_NO_COLLISION`), semitransparente e com `RENDER_TEX_ANIM`; valor
   típico `0x412205`; a superfície da textura é 0 (Road), não água.
3. **Ladrilho de referência**: o primeiro batch com `RENDER_UNK_1000000`
   (bit 24) + `RENDER_WATER` e sem `RENDER_HIDDEN` (`waves.c:1334`). A caixa do
   segmento dele define o tamanho de todos os ladrilhos
   (`gWaveBoundingBoxDiffX/Z`). Retail: um batch de 2 triângulos por pista,
   `0x1412205`.
4. **Parâmetros do header** `0x56`–`0x71`: subdivisões, duas senoides (passo,
   base, altura), tamanho da semente, força, textura, escala e rolagem de UV,
   distância de visão (`level_header.py:163-182`). O
   `data/level_header_defaults.json` já traz os valores retail; a textura é um
   *asset default*, preenchido pelo **Fill Defaults** quando há assets
   configurados. Um remix herda os da pista-base.

### A grade

Cada segmento vira um ladrilho na posição da sua caixa (`x1`, `z1`),
arredondada para a grade do ladrilho de referência (`waves.c:1454-1489`). Por
isso as pistas com ondas são segmentadas numa **grade regular de quadrados
iguais** — medido:

| pista | ladrilho (caixa do segmento) | segmentos com `hasWaves` |
|---|---|---|
| Whale Bay | 1290 × 1290 | 49 / 49 |
| Pirate Lagoon | 828 × 828 | 109 / 117 |
| Crescent Island | 2551 × 2551 | 18 / 24 |

(As caixas variam de 1 a 2 unidades.) Vinte fases retail usam ondas, entre
elas Boulder Canyon, Windmill Plains, Treasure Caves, Haunted Woods, Hot Top
Volcano (lava, `0x2416205`), os hubs e os chefes de Sherbet Island.

**Limites:** a máscara `D_8012A0E8[64]` é de `s32`, então no máximo **32
colunas × 64 linhas** de ladrilhos; `gWaveBlockIDs[512]`.

**Física:** o segmento com ondas ganha um plano `WATER_WAVY` com a altura da
simulação (`get_level_segment_waves`). O carro boia uns 5 unidades abaixo da
superfície (`racer.c:4389`, `:4810`), com tração quase zero (`:5372`). O
hovercraft segue outro caminho.

### Água parada, para comparação

Ancient Lake e Frosty Village: batch `0x12205` (água, sem colisão, sem os bits
22 e 24) e `hasWaves = 0`. O plano de água parada vem de textura com superfície
`WATER_CALM` (11) ou `WATER_UNK_F` (15) (`tracks.c:2494`), inclusive em batch
`RENDER_HIDDEN`. Um autor pode fazer água parada hoje: basta a superfície
*Water Calm* no material.

---

## O que o addon faz hoje

- **Import**: os batches de água não colidem, então provavelmente caem no slot
  de decoração. As flags viajam inteiras em `dkr_flags` e o export in-place as
  preserva, então **um remix de Whale Bay que só mexe em vértices mantém as
  ondas**.
- **`blank_model`, `rebuild`, `resegment`**: um `Segment` novo nasce com
  `has_waves = 0`, e o `resegment` cria segmentos novos. Consequência: **Re-segment
  Track numa pista com ondas apaga as ondas**, e o `_partition` também não
  respeita a grade. É um bug a corrigir mesmo sem a funcionalidade nova. Falta
  verificar se o `_rebuild` do export que muda contagem mantém os segmentos (e
  com eles o `has_waves`).
- Não há UI para marcar faces como água nem para ligar as ondas.

---

## Proposta

1. **Slots "Água com ondas" e "Água parada"** no material de geometria, ao lado
   de superfície, decoração e parede invisível. O import de pistas retail passa
   a pôr os batches de água neles.
2. **Operador "Criar Água com Ondas"**: gera um plano de água em grade, na
   altura do 3D cursor, cobrindo a área escolhida, com tamanho de ladrilho
   configurável — um quad (2 triângulos) por ladrilho. Mais simples e mais
   robusto do que recortar uma malha arbitrária de água.
3. **Segmentação em grade** quando há água com ondas: o `resegment` particiona
   pela grade do ladrilho (o modelo inteiro, porque cada segmento vira um
   ladrilho pela sua caixa). Cada segmento com água ganha `hasWaves = -1` e um
   batch ganha o bit 24 (referência).
4. **Flags** dos batches de água: `0x412205`, o modelo do retail. A textura do
   autor tanto faz — com ondas ela não é desenhada.
5. **Header**: garantir os parâmetros de onda e a textura quando houver água com
   ondas, e expor três ou quatro deles no Level Header (altura das ondas,
   subdivisões, textura, rolagem).
6. **Validação**: grade dentro de 32 × 64; ladrilho de referência presente e
   único; segmento com água de tamanho diferente do ladrilho (ele desloca o
   ladrilho); aviso de que em tela dividida não há ondas.
7. **Corrigir o Re-segment que apaga ondas**: derivar `hasWaves` dos batches com
   `RENDER_WATER | RENDER_UNK_0400000`, e manter a grade quando a pista já tem
   ondas.

## Testes

- **Retail**: nas 20 pistas com ondas, derivar `hasWaves`, o ladrilho de
  referência e a grade a partir dos batches e bater com o arquivo.
- **Round trip**: re-segmentar Whale Bay mantém a grade e o `hasWaves`.
- **Blender**: criar água com ondas numa pista nova → export → o modelo tem os
  segmentos com `hasWaves`, um batch de referência, flags `0x412205` e o header
  com os parâmetros de onda.

## Decisões em aberto

1. Tamanho padrão do ladrilho (Pirate Lagoon 828, Whale Bay 1290, Crescent
   Island 2551).
2. O que fazer com a geometria de terra num modelo em grade: o retail põe tudo
   em ladrilhos do mesmo tamanho, o que muda a segmentação da pista inteira.
3. Se a água parada ganha o mesmo tratamento (operador e superfície 11 num
   batch escondido, como o retail faz).

---

## Implementação

O que a leitura mais funda do decomp corrigiu nesta proposta:

- **A textura da água importa.** `func_800BBE08` pega a textura do batch de
  referência (`gWaveTexture`), e `waves_render` desenha as ondas com ela; a do
  header (`/waves/texture-ID`) é só a camada de detalhe do modo translúcido.
  Nesse modo, `wave_load_material` carrega as duas como blocos RGBA quadrados de
  16 ou 32 texels, e o addon recusa outra coisa para ondas.
- **A grade vale para o modelo inteiro, não só para a água.** `func_800BBF78`
  posiciona *todo* segmento pelo canto `(x1, z1)` da caixa (+8), e
  `waves_block_hq` põe na lista de ondas qualquer segmento cujo ladrilho esteja
  visível. Um segmento de terra num ladrilho de ondas desenha uma segunda malha
  de ondas ali, na altura 0. Por isso o retail corta a pista inteira em
  quadrados, um segmento por quadrado, com o segmento de água primeiro - 21 dos
  22 modelos; o `ocean_track`, que nenhum header carrega, é a exceção.
- **Água parada não precisa de superfície.** `func_8002C71C` guarda a altura do
  primeiro vértice de qualquer batch `RENDER_WATER` do segmento, e
  `get_level_segment_waves` sempre acrescenta esse plano. A flag basta; o
  operador usa `0x12205`, como a Ancient Lake.

As decisões em aberto:

1. **Tamanho do ladrilho:** automático - o menor entre 1024 e 6400 que deixa a
   pista em até 115 segmentos e a água nas 32 colunas marcáveis. O autor pode
   fixar outro; uma pista que já tem ondas mantém o seu.
2. **Terra no modelo em grade:** cortada nas linhas da grade
   (`level_model_layout.resegment_grid`), com posição, UV e cor interpoladas e
   a aresta compartilhada cortada no mesmo ponto pelos dois lados. Se a pista
   passar de 127 quadrados, os secos se juntam em blocos `n x n` que não tocam
   água.
3. **Água parada:** o mesmo operador, opção *Calm*.

E o bug do Re-segment: o `resegment` agora detecta água com ondas e corta na
grade do próprio modelo; o export corta de novo se uma edição tirou um segmento
do seu quadrado ou apagou a água de referência, e desliga `hasWaves` onde o
autor apagou a água. Um remix retail só remodelado continua byte a byte.

## Retomada e validação (2026-09-16)

Os testes de desenho dos painéis já estavam aplicados quando o trabalho foi
retomado. O layout falso agora também valida propriedades atribuídas aos botões
de operadores e exige propriedades enum nos menus.

A suíte de operadores revelou duas falhas em Merge by Distance: IDs de segmento
e cores empacotadas eram misturados pelo Blender. A importação agora preserva o
segmento por face e os quatro canais de cor separadamente; a exportação usa esses
dados após a união. Malhas antigas continuam legíveis pelo contrato anterior;
reimportar a geometria adiciona os atributos necessários antes de unir vértices.

Validação concluída:

- As 21 suítes de `tools/blender/run_tests.py` passaram, com Blender 5.2. Quatro
  suítes precisaram ser repetidas fora do sandbox porque a pasta temporária do
  Windows recusava gravações. Logs: `build/blender-resume-tests.log` e
  `build/blender-resume-retry.log`.
- `DKRCustomTracksTests` compilou em Release e passou pelo CTest na configuração
  `build/water-transparency-checks`, com `DKR_RUNTIME_BUILD_GENERATED=OFF` e
  `DKR_RUNTIME_BUILD_RT64=ON`.
- ZIP atualizado e verificado:
  `build/water-transparency-checks/addon/dkr_track_editor.zip`.

A compilação principal ainda exige regenerar o código v77: o CMake acusa falta
do hook `dkr_legacy_scene_begin` em `runtime-recomp/RecompiledFuncs`. Nenhum
executável completo atualizado foi produzido nesta retomada, nem houve
validação visual das ondas dentro do jogo. Log do bloqueio:
`build/custom-tracks-resume-build.log`.
