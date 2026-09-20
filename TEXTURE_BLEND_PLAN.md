# Plano: mistura de texturas por pintura (areia ↔ grama)

## O desejo do usuário

> Imagine: uma pista com areia e grama e um degradê entre elas.

> Gostaria que eu pudesse ficar alterando entre materiais. Não criar um outro.

A inspiração é o Banjo-Kazooie, em que o mesmo terreno mistura duas texturas e o
vertex alpha decide quanto aparece de cada uma
([artigo](https://alfredbaudisch.com/experiment-logs/banjo-kazooie-n64-environments-and-levels-texture-blending-and-vertex-color-usage/)).
O pedido é pintar com os materiais que a pista **já tem**, como uma paleta: onde
dois se encontram, aparece um degradê. Nenhum material combinado entra na lista
do autor.

Estado: **plano** (2026-09-18). Nada implementado. Tudo abaixo foi lido no
decomp (`extern/dkr-decomp/src`), medido nos 110 modelos retail extraídos
(`us.v77`) e conferido no runtime (`runtime-recomp/src/game`).

---

## O que o jogo faz hoje

### Na pista, cor de vértice é luz

- O vértice tem 10 bytes: `x, y, z` (s16) e `r, g, b, a` (u8), sem normais
  (`structs.h:574`). O RGB é a iluminação baked.
- O combiner comum é `G_CC_MODULATEIDECALA`: cor = `TEXEL0 × SHADE`, alpha =
  `TEXEL0` (`textures_sprites.c:80-87`).
- `render_level_segment` liga `RENDER_FOG_ACTIVE` em **todo** batch
  (`tracks.c:1928`). Com `G_FOG`, o RSP escreve o fator de fog no shade alpha, e
  o blender usa `G_RM_FOG_SHADE_A`. O alpha que vem do arquivo nunca chega à
  tela: 124.614 dos 125.333 vértices retail têm `a = 255`.

### A única exceção: o alpha "mágico"

`tracks.c:2904-2920`: ao carregar a pista, todo vértice com `r == 1 && g == 1`
vira cinza 128 com `a = b`, e o batch inteiro ganha `RENDER_VTX_ALPHA` (bit 27).
Esse batch fica translúcido e sem fog. No retail são 719 vértices, 232 batches e
14 modelos, todos com `(1, 1, 0)`: um fade-out de borda, não uma mistura entre
texturas.

Consequência para este plano: um vértice nosso cuja luz baked seja `(1, 1, x)`
seria sequestrado pelo loader.

### Onde o DKR já mistura duas texturas

- **Água com ondas** (`waves.c:992-1000`): duas texturas na TMEM (tile 0 = a do
  batch, tile 1 = a do wave controller), com o combiner
  `G_CC_BLENDTEX_MODULATEA_1_PRIM` = `lerp(TEXEL0, TEXEL1, SHADE)` e fog
  desligado. As cores dos vértices são recalculadas a cada frame.
- **Luzes de Spaceport Alpha** (`material_set_blinking_lights`,
  `textures_sprites.c:849-883`): dois `gDPLoadMultiBlock` e
  `G_CC_BLENDTEX_PRIM`. No fim, a função faz `gForceFlags = TRUE`,
  `gCurrentRenderFlags = RENDER_NONE` e `gCurrentTextureHeader = NULL`. É esse o
  padrão para devolver o estado ao `material_set` do próximo batch.

A técnica já roda nesta engine; só não está disponível para a geometria da pista.

### Surface type é por triângulo

- O surface vem da entrada da tabela de texturas do batch (`tracks.c:2582`,
  `hasm/collision.c:149`). Não existe surface parcial.
- A tração é somada roda por roda (`racer.c:5335-5348`). Ao cruzar a linha, o
  kart passa por 0 a 4 rodas na areia, em degraus.
- Os efeitos do kart inteiro (poeira, som, `gCurrentSurfaceType`) usam o maior
  número de surface entre as rodas (`enums.h:153`: DEFAULT 0, GRASS 1, SAND 2).

### Espaço no formato

Varredura dos 10.389 batches retail:

- **Bit 12** (`RENDER_UNK_0001000`): nunca aparece no retail, nenhum código o lê
  na geometria da pista, e `material_set` o descarta pela máscara. Serve de flag
  de mistura. O bit 17 é a alternativa: também é 0 no retail e só é lido em
  modelos de objeto (`objects.c:8096`).
- **`miscData`**: vale 0 em todo batch de pista retail. Na pista, ele só é lido
  quando o batch tem `RENDER_TEX_ANIM` e `RENDER_UNK_80000000`
  (`tracks.c:1300-1307`). Serve para guardar o índice da segunda textura na
  tabela.

Com isso, a mistura cabe **dentro do próprio modelo**, sem seção nova no
`.dkrmap`. Isso importa porque o runtime recusa um pacote com seção desconhecida
(`custom_tracks.cpp:1150-1155`). Codificada no modelo, a pista abre num DKR-R
antigo e desenha só a textura A, com fog e com a mesma física.

### TMEM

São 4 KB. A textura A vai para o endereço 0 pelo display list do próprio header
da textura; B iria para o tmem 256 (2 KB), como na água. Então cada textura pode
ocupar no máximo 2 KB:

- 32×32 RGBA16 cabe exatamente. É o tamanho de 385 texturas retail.
- 64×32 RGBA16 (a `Sand`, por exemplo) ocupa os 4 KB sozinha.
- Texturas CI põem a paleta na metade de cima e colidiriam com B. Ficam fora da
  v1.

### O que já existe no addon

- As cores cruas ficam por canal em `dkr_colour_r/g/b/a`
  (`operators/geometry.py:170-175`), e a ida e volta é exata.
- `BatchKey` já inclui `flags` e `misc` (`level_model_layout.py:104`), então as
  faces de mistura viram batches separados naturalmente.
- Uma entrada da tabela de texturas é o par (textura, surface). A mesma textura
  com dois surfaces vira duas entradas (`props.py:474-482`). O surface dominante
  usa esse mecanismo.
- `transparency.py:62` documenta o alpha mágico.
- `operators/new_track.py:349` converte o alpha com `_linear_to_srgb` junto com
  o RGB. Isso precisa ser corrigido antes de o alpha ter significado.
- O runtime dimensiona o display list com `kCommandsPerBatch = 10`
  (`custom_tracks_hooks.cpp:176`), a partir de `count_level_model_batches`
  (`custom_tracks.cpp:560`).

---

## Decisões (tomadas na conversa)

1. **Paleta.** O autor pinta com os materiais que já existem. Nenhum material
   combinado aparece na lista.
2. **Três jeitos de pintar:** pintura dura (a face recebe o material), pincel
   suave (deixa degradê) e **Suavizar bordas** (fade automático nas fronteiras,
   com largura escolhida).
3. **Surface** é o do material dominante no triângulo. O viés é configurável,
   com padrão em 50%.
4. **Fog** fica desligado só nos triângulos de transição. Faces puras continuam
   sendo batches normais, com fog.
5. **No máximo 2 materiais por triângulo.** Um triângulo com mais que isso fica
   destacado, e a exportação usa os dois mais fortes.
6. **Materiais elegíveis na v1:** só opacos e estáticos, com texturas do mesmo
   tamanho, até 2 KB cada e sem CI. Um material que entra numa mistura é
   convertido **por inteiro**: todas as faces dele usam a mesma versão, para não
   haver costura de resolução.
7. **Codificação** dentro do modelo (bit 12 + `miscData`), sem seção nova.

---

## O batch de mistura

| Campo | Valor |
|---|---|
| `textureIndex` | entrada (textura A, surface do material dominante) |
| `miscData` | índice da entrada da textura B |
| `flags` | bit 12 + as flags do material dominante |
| RGB do vértice | luz baked, sem mudança |
| alpha do vértice | peso de B, de 0 a 255 |

- **Ordem canônica por par.** A e B são fixos para cada par (por exemplo, o
  menor índice da tabela é A). Assim o alpha de um vértice significa a mesma
  coisa em todos os batches do par. O surface não exige trocar A com B, porque
  vem da entrada (A, surface dominante).
- **Clones.** Um vértice que está em batches de dois pares diferentes (numa
  junção tripla) precisa de alphas diferentes. Hoje o rebatch tira a cor do
  vértice do pool (`level_model_layout.py:175`), então o clone tem de existir no
  pool.
- **Guarda do código mágico:** em todo vértice exportado, `r == 1 && g == 1`
  vira `r = 2`.

Render no runtime:

- **Ciclo 1:** cor = `(TEXEL1 − TEXEL0) × SHADE_ALPHA + TEXEL0`, alpha =
  `TEXEL0`.
- **Ciclo 2:** cor = `COMBINED × SHADE` (a luz baked), alpha = `COMBINED`.
- **Other mode:** `DKR_OMH_2CYC_BILERP`, com `G_RM_NOOP` no ciclo 1 (como o DKR
  faz) e `G_RM_AA_ZB_OPA_SURF2` ou `G_RM_ZB_OPA_SURF2` no ciclo 2, conforme o
  anti-aliasing.
- `gSPClearGeometryMode(G_FOG)`.

---

## Fase 0: prova no runtime

Objetivo: ver a mistura dentro do jogo antes de investir no addon.

1. **Pista de teste.** Um script pega uma pista de teste pequena (ou a de
   `tools/blender/make_texture_demo_track.py`) e escreve, num batch com duas
   texturas 32×32 RGBA16, o bit 12, o `miscData` e o alpha dos vértices.
2. **Hook** em `render_level_segment`, logo depois do retorno de `material_set`
   no ramo sem luzes piscando (`tracks.c:1972`) e antes do `gSPVertexDKR`. Uma
   função nova, por exemplo `dkr_texture_blend_batch` em
   `runtime-recomp/src/game/texture_blend.cpp`, faz o seguinte quando o batch
   atual tem o bit 12 e uma entrada válida:
   - escreve em `gTrackDL`: pipe sync, `LoadMultiBlock` de B no tile 1 (tmem
     256, com formato, tamanho e clamp tirados do `TextureHeader` de B),
     combiner, other mode e a limpeza de `G_FOG`;
   - põe `gForceFlags = TRUE` e `gCurrentRenderFlags = RENDER_NONE`, para o
     próximo `material_set` reemitir o estado completo (com `G_FOG`), como as
     luzes piscando fazem;
   - usa os endereços da us.v77: `gTrackDL` 0x8011B0A0, `gForceFlags`
     0x80126382, `gCurrentRenderFlags` 0x80126374 e `gCurrentLevelModel`
     0x800DC918. Os da us.v80 vêm do arquivo de símbolos da revisão;
   - **confirma no disassembly** que o ponto do hook fica depois do delay slot
     do `jal` e antes de `gTrackDL` ser relido. O ponteiro do batch atual é lido
     de onde o código o guarda (registrador ou pilha);
   - é registrada nas duas policies (`dkr.us.v77/v80.recomp-policy.json`);
   - é defensiva: só age em níveis `.dkrmap`, confere os 2 KB de cada textura e
     que `miscData < numberOfTextures`. Se algo não bate, não faz nada, e o
     batch cai para "só A".
3. **Orçamento.** `count_level_model_batches` passa a contar também os batches
   de mistura e soma os comandos extras deles. São cerca de 12 (sync, os 7 do
   `LoadMultiBlock`, combine, other mode, geometry mode); o número exato sai
   quando a emissão estiver escrita.
4. **Conferir no jogo**, em Modern e em Accurate:
   - o degradê aparece e bate com `lerp(A, B, alpha) × luz`;
   - o batch seguinte volta ao normal, com fog, sem vazamento de estado;
   - **mipmaps gerados** (`docs/GENERATED-MIPMAP-VALIDATION.md`): eles valem
     para o tile 1? Se não valerem, a faixa treme de longe. Nesse caso, decidir
     entre aceitar ou tirar a faixa inteira dos mipmaps gerados, por
     coerência;
   - **pacote HD** da pista (`<pista>-hd.zip`): a textura B é substituída?
   - a interpolação de frames (`dkr_level_segment_interpolation_*`) não é
     afetada;
   - funciona com 2 a 4 jogadores;
   - um runtime antigo abre a mesma pista e mostra A com fog.
5. **Testes:** a contagem do orçamento em
   `runtime-recomp/tests/custom_tracks_tests.cpp`; os comandos emitidos e a
   devolução do estado em `custom_tracks_runtime_tests.cpp`.

Critério de saída: uma captura de tela, dentro do jogo, de grama ↔ areia. Se
mipmaps ou HD não funcionarem, registrar aqui e decidir antes da Fase 1.

---

## Fase 1: regras sem Blender (`texture_blend.py`)

Um módulo sem `bpy`, como `transparency.py` e `water.py`, em
`tools/blender/dkr_track_editor/texture_blend.py`:

- `eligible(material)`: opaco, estático, sem scroll, não é água, não é CI, até
  2 KB. Devolve o motivo da recusa como texto para a interface.
- `classify(pesos da face)`: A puro, B puro, par de mistura ou conflito (3 ou
  mais materiais).
- `normalise` e `snap`: pesos abaixo de uns 2% viram 0; quantização para 0..255.
- `dominant(pesos, viés)`: o surface do triângulo.
- `pair_order(a, b)`: a ordem canônica do par.
- `guard_magic(rgba)`: a guarda do código mágico.
- `convert_plan(material)`: para qual tamanho e formato o material inteiro vai.

Testes: `tools/blender/tests/test_texture_blend.py`, incluído em
`run_tests.py`.

---

## Fase 2: exportação e reimportação

- **Exportação** (`operators/geometry_export.py`, `operators/new_track.py`): ler
  os pesos e classificar os triângulos; criar as entradas (A, surface) e a de B;
  montar o `BatchKey` com o bit 12 e o `misc`; escrever o alpha por par, com os
  clones; aplicar a guarda do código mágico; copiar as flags do material
  dominante.
- **Correção:** `operators/new_track.py:349` deixa de converter o alpha com
  `_linear_to_srgb`.
- **Reimportação** (`operators/geometry.py`): um batch com o bit 12 reconstrói os
  pesos dos dois materiais a partir do alpha e do `misc`. Importar e exportar
  sem editar tem de dar os mesmos bytes. O teste vai em
  `test_blender_roundtrip.py` e `test_level_model_roundtrip.py`.
- **Validação** (`validate.py`, `operators/checks.py`): TMEM, conflito de 3
  materiais, material inelegível e tamanhos diferentes.
- **Orçamento:** a contagem de batches de mistura tem de dar o mesmo número no
  addon e no runtime.

---

## Fase 3: a ferramenta no Blender

- **Painel "Material Paint"** (`ui/panels.py`): a paleta com os materiais da
  malha, raio, suavidade, viés e o botão **Suavizar bordas** com a largura.
- **Onde a pintura fica guardada:** um atributo float POINT por material, com a
  chave num id estável guardado no material. O nome não serve, porque o autor
  pode trocá-lo.
- **Pincel:** começa por um spike de meio dia comparando duas opções:
  - (a) o Vertex Paint nativo no atributo do material ativo, com a normalização
    feita no fim de cada traço;
  - (b) um pincel modal do próprio addon, que pinta e normaliza ao mesmo tempo.

  Critério: pintar areia por cima da grama tem de *substituir* a grama, e a
  sensação tem de ser a de um programa de pintura. A opção é escolhida pelo
  spike.
- **O material da face acompanha o dominante** (o slot é reatribuído). Assim a
  lista de materiais e a atribuição das faces continuam fazendo sentido.
- **Suavizar bordas:** em cada fronteira entre dois materiais, os pesos seguem a
  distância na malha, até a largura escolhida.
- **Preview:** o node tree de cada material ganha as texturas dos parceiros,
  misturadas pelos atributos de peso. A mistura é feita em espaço gamma, como no
  RDP, para o meio do degradê bater com o jogo. Nenhum material novo aparece.
- **Overlays:** a linha de surface (draw handler `gpu`) e o destaque dos
  triângulos com conflito.
- **Subdividir faixa** (opcional): subdivisão só na faixa de transição, porque a
  resolução do degradê é a da malha.
- **Conversão automática** do material inteiro para a versão elegível,
  reaproveitando `textures.best_size` e as texturas próprias da pista.

Testes: `tools/blender/tests/test_blender_texture_blend.py`, no Blender 5.2,
rodando pelo `run_tests.py --all`.

---

## Fase 4: validação no jogo

- Grama ↔ areia em terreno plano e em rampa.
- Canto triplo.
- Kart cruzando a linha: a tração muda em degraus e aparece a poeira de areia.
- Pista com muito fog: a faixa sem fog, vista de longe.
- 2 a 4 jogadores, e o modo Accurate.
- Runtime antigo: a pista abre e mostra só A.
- Reimportar e reexportar.

Documentação ao fim de cada fase: `docs/LEVEL_MODEL_FORMAT.md` (o bit 12 e o
`miscData` do batch de mistura), `docs/CUSTOM_TRACKS.md`,
`tools/blender/README.md` e a nota de `transparency.py`.

---

## Riscos

| Risco | Efeito | Onde aparece | Saída |
|---|---|---|---|
| Mipmaps gerados não valem para o tile 1 | a faixa treme de longe | Fase 0 | tirar a faixa dos mipmaps gerados, ou estender a geração |
| Pacote HD não cobre a textura B | B aparece em baixa resolução na faixa | Fase 0 | ajustar a identidade da textura carregada no tile 1 |
| `gTrackDL` guardado em registrador no ponto do hook | comandos perdidos ou sobrescritos | Fase 0 (disassembly) | mudar o ponto do hook |
| Fog | faixa sem fog em pista com névoa | Fase 4 | aceitar na v1 e documentar |
| Pincel nativo não consegue substituir o material | pintura desajeitada | spike da Fase 3 | pincel modal |
| Malha pouco densa | degradê reto ou borrado | Fase 3 | Subdividir faixa |

---

## Fora do escopo da v1

- 3 materiais no mesmo triângulo.
- Texturas animadas, com scroll, água, transparentes ou cutout na mistura.
- Texturas CI ou maiores que 2 KB.
- Bordas orgânicas com textura de ruído, como no Banjo. Exige estudar o
  combiner.
- Mistura em objetos: só a geometria da pista.
- Pistas retail: a feature é só para `.dkrmap`.
