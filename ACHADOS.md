# Achados — engenharia reversa de DKR

Investigações feitas sobre `extern/dkr-decomp` (build `us.v80`) e a extração retail em
`extern/dkr-decomp/assets/.vanilla/us.v80/`: 65 level headers, 136 object maps, 55 level models.

Tudo aqui é medido no código ou nos dados, não inferido do jogo rodando. Onde é inferência,
está marcado como tal.

---

## 1. AI nodes (`ASSET_OBJECT_AINODE`)

### O que é

Entrada de 16 bytes no object map, sem modelo, invisível em jogo. Vértice de um grafo de
navegação. `include/level_object_entries.h:184`:

```c
typedef struct LevelObjectEntry_AiNode {
    /* 0x00 */ LevelObjectEntryCommon common;   // objectID, size, x, y, z
    /* 0x08 */ u8 unk8;         // tag de destino (ver abaixo)
    /* 0x09 */ u8 nodeID;
    /* 0x0A */ u8 adjacent[4];  // vizinhos; bit 7 setado = vazio
    /* 0x0E */ s8 elevation;    // camada vertical, NÃO altura
    /* 0x0F */ s8 padF;
} LevelObjectEntry_AiNode;
```

No load, `ainode_update()` (`src/objects.c:7328`) preenche `gAINodes[128]`, resolve cada
`adjacent` para ponteiro de objeto e **pré-calcula a distância euclidiana de cada aresta** em
`Object_AiNode.distToNode[4]`. `obj_loop_ainode` é vazia — o nó é dado inerte.

### NÃO é a linha de corrida

Achado principal, e contradiz o `docstring` de `tools/blender/dkr_track_editor/ai_graph.py` e o
`docs/BLENDER_ADDON_PLAN.md`, que chamam isso de "AI racing line".

A corrida normal usa `func_80045C48` (`src/racer.c:1360`), que interpola uma **spline de
checkpoints**. O despacho é explícito em `src/racer.c:613-624`:

```c
switch (level_type()) {
  case RACETYPE_CHALLENGE_BATTLE:
  case RACETYPE_CHALLENGE_BANANAS: racer_ai_challenge(...); break;  // usa AI nodes
  case RACETYPE_CHALLENGE_EGGS:    racer_ai_eggs(...);      break;  // não usa
  default:                         func_80045C48(...);      break;  // spline de checkpoints
}
```

### Os quatro usos reais

| Uso | Onde |
|---|---|
| IA dos desafios Battle e Bananas | `racer_ai_challenge`, `src/racer.c:682` |
| NPCs de hub: T.T., Taj, balão dourado | `obj_loop_stopwatchman` / `obj_loop_parkwarden` / `obj_loop_goldenballoon` |
| Loop-de-loop (trilho on-rails) | `obj_loop_modechange`, `src/object_functions.c:3283`; direção em `src/racer.c:3762` |
| Origem dos limiares de elevação do mapa | `ainode_update`, `src/objects.c:7410-7429` |

`func_8001CD28` (`src/objects.c:7674`) é um Dijkstra de verdade: fila ordenada por custo
acumulado usando `distToNode`, devolve o **primeiro passo** rumo a um nó cuja tag bate com o alvo.

### `unk8` = classe de destino

Consumido como `arg1 == aiNodeEntry->unk8` em `src/objects.c:7778`. Retail usa 0..7.

| valor | significado | evidência |
|---|---|---|
| 0 | nó de caminho comum | — |
| 1 | balão de arma | estados 3 e 5 pedem alvo `1` — `src/racer.c:929,948` |
| 3 | ponto de (re)entrada | `ainode_find_nearest(..., useElevation=2)` só aceita `unk8 == 3` — `src/objects.c:7492` |
| 4–7 | base/ninho do jogador *i* | estado 7 pede `racerIndex + 4` — `src/racer.c:951` |

`arg1 & 0x100` é o modo alternativo: "vá até este `nodeID` específico" (perseguir outro racer).

### `elevation` = andar, não altura

Índice de camada vertical 0–3 para arenas multi-nível. `ainode_update` faz `elevation & 3`,
ordena os nós por Y e grava em `gElevationHeights[]` o ponto médio entre nós consecutivos onde a
tier muda — **os nós ensinam ao jogo onde ficam os andares**. `obj_elevation(y)` depois
classifica qualquer Y em 0..3. Alimenta:

- busca de nó restrita à mesma camada (`src/objects.c:7489`)
- escala do marcador no minimapa em Battle (`src/game_ui.c:3739`)
- seleção de alvo: só considera racers na mesma elevação (`src/racer.c:7303`)

Inconsistência do jogo: `-1 & 3 == 3`, mas `ainode_find_nearest` compara o `s8` cru sem máscara,
então um nó com `elevation = -1` nunca casa ali.

### BUG no addon: o limite é 128, não 255

`ai_graph.py:36` e `validate.py:133` permitem até 255 nós. O jogo aceita **128**, ids 0–127:

- `AINODE_COUNT` é 128 e `gAINodes` tem 128 ponteiros (`src/objects.c:50`, `:730`)
- `ainode_update` filtra com `if (!(index2 & AINODE_COUNT))` — isto é, `!(id & 0x80)`. Vale para
  o `nodeID` **e** para cada slot de `adjacent` (`src/objects.c:7358`, `:7377`)
- `ainode_get` e `ainode_find_next` limitam a `< 128`; `func_8001CD28` mascara o alvo com `0x7F`
- retail nunca passa de `nodeID` 38

Uma track com 200 nós exporta sem erro e o jogo **descarta silenciosamente** todos os nós 128+ e
todas as arestas que apontem para eles. O 255 funciona como sentinela vazia por acidente (bit 7);
128..254 funcionam igual.

Ponto menor: `attach_branch` grava `unk8 = 0` fixo (`operators/ai.py:223`), então branches nunca
podem ser destino.

### Nós isolados: rascunho descartado do dinossauro

Varrendo os 16 mapas retail que têm AI node (208 nós):

| | elevation −1 | 0 | 1 | 3 |
|---|---|---|---|---|
| **isolados** (27) | 26 | 1 | 0 | 0 |
| **ligados** (181) | 0 | 142 | 31 | 8 |

`elevation = -1` é, na prática, "este nó não faz parte de grafo algum".

**Ancient Lake** (`asset_level_object_maps_5.gltf`) tem 4 nós, todos com
`adjacent = [255,255,255,255]` e `elevation = -1`, em Y −47/−53/−45/−51 — os únicos objetos
abaixo da linha d'água (resto do mapa: Y +14 a +882).

O dinossauro de Ancient Lake é o `actorIndex = -108` do map 73: 6 waypoints
`ASSET_OBJECT_ANIMATION` que dão `objectIdToSpawn = ASSET_OBJECT_DINOSAUR2` no waypoint 0,
`nodeSpeed = 31`, fechando o circuito com `goToNode = 0`. Distâncias:

```
                    o0     o1     o2     o3     o4     o5
  ai-node 0        1365   1399   1299    835    103    862
  ai-node 1        1469   1961   2049   1607    836    318
  ai-node 2        2123   2415   2274   1681   1013   1013
  ai-node 3        2404   2209   1722   1054    945   1697
```

ai-node 0 está a **103 unidades** do waypoint 4 e ai-node 1 a 318 do waypoint 5. Todos os outros
11 actors do mapa ficam a 1700+ unidades.

**Medido:** nada em Ancient Lake lê esses nós. Resolvi os headers dos 25 tipos de objeto dos dois
mapas; os behaviours presentes são `ANIMATED_OBJECT, CAR_ANIMATION, CAMERA_ANIMATION, FISH,
WORLD_KEY, LENS_FLARE, ZIPPER_WATER, ZIPPER_GROUND, WEAPON_BALLOON, CAMERA_CONTROL, SCENERY,
CHECKPOINT, SETUP_POINT, ANIMATION, BANANA, SILVER_COIN, SILVER_COIN_2` — nenhum dos quatro
consumidores. O DINOSAUR2 é `BHV_ANIMATED_OBJECT`, movido por `func_8001F460` (`src/objects.c:8677`),
o interpolador de spline dos waypoints de `ANIMATION`, que não toca em AI node.

**Inferido:** é o rascunho descartado do trajeto do dinossauro. Foi esboçado com a ferramenta
genérica de waypoint e depois reescrito como actor de `ANIMATION`, que tem o que o dino precisa e
o AI node não tem: velocidade por trecho, delay, animação, rotação, spawn do modelo.

Mesmo padrão em Hot Top Volcano (map 7): 8 nós isolados, `elevation = -1`, num plano em Y = −148
sob a pista.

**Pendente:** os outros mapas com nós isolados (FossilCanyon 1, SherbetIslandHub 5,
OpeningSequence 9) não foram cruzados contra os actors de `ANIMATION`.

### Consequência prática

Ao importar Ancient Lake no Blender, esses 4 empties `ai-node` são lixo morto. O
`validate.py:206` emite "AI node(s) cannot be reached from node 0" para eles, o que é ruído. Vale
tratar `elevation == -1` + isolado como categoria própria em vez de warning de conectividade.

---

## 2. O céu

### É um objeto normal do jogo, não parte do nível

Cada level header tem `skybox` em `0x38`, que guarda **um object ID**:

```c
init_track(gCurrentLevelHeader->geometry, gCurrentLevelHeader->skybox, ...)  // src/game.c:555
  └─ skydome_spawn(skybox);                                                  // src/tracks.c:239
       └─ gSkydomeSegment = spawn_object(&spawnObject, OBJECT_SPAWN_UNK02);  // src/tracks.c:1340
```

Depois o jogo apaga a identidade dele (`level_entry = NULL`, `objectID = -1`). Se `skybox == -1`,
`gSkydomeSegment` fica `NULL` e o nível não tem céu.

É por isso que o céu não vem junto com a pista: mora na seção de **objetos** do ROM, não na do
modelo do nível. Ancient Lake tem `model: ASSET_LEVELMODEL_ANCIENTLAKE` e
`skybox: ASSET_OBJECT_DOME13` — duas referências independentes.

### 15 céus para 65 níveis

| dome | níveis | exemplos |
|---|---|---|
| DOME1 | 23 | Bluey, Bubbler, CentralAreaHub, JungleFalls… |
| *(nenhum)* | 7 | FrontEnd, CharacterSelect, DinoDomainHub… |
| DOME11 | 6 | DarkmoonCaverns, FrostyVillage, SpaceportAlpha |
| DOME8 | 5 | EverfrostPeak, WalrusCove, IciclePyramid |
| DOME9 | 4 | CrescentIsland, TreasureCaves, DragonForestHub |
| DOME / DOME15 | 3 cada | BoulderCanyon+HorseshoeGulch / Trickytops |
| DOME2, 6, 7, 10 | 2 cada | HauntedWoods+StarCity, FireMountain+HotTopVolcano… |
| DOME4, 12, 13, 14, 16 | 1 cada | PirateLagoon, SpacedustAlley, **AncientLake**, FossilCanyon, DarkwaterBeach |

Sobram `dome3`, `dome5`, `dome17`, `AnimDome` e `SpaceDome` sem nenhum header apontando para eles
como skybox.

### Geometria

Decodificado com `tools/blender/dkr_track_editor/object_model.py`:

| dome | comprimido | inflado | texturas | vértices | triângulos | raio | Y |
|---|---|---|---|---|---|---|---|
| dome1 | 720 B | 1928 B | 10 | 77 | 42 | ~3900 | −143..2276 |
| **dome13** (Ancient Lake) | 768 B | 1904 B | 11 | 78 | 40 | ~8400 | 316..4500 |
| dome11 | 1088 B | 3968 B | 20 | 170 | 88 | ~9100 | −4941..4849 |
| dome7 (WhaleBay) | 752 B | 1872 B | 13 | 72 | 42 | ~10300 | −256..10226 |
| dome17 | 288 B | 376 B | 1 | 9 | 8 | ~6600 | −324 (plano) |

**É casca 3D, não plane.** Os vértices vêm em anéis horizontais empilhados:

```
dome13 — 78 vértices, 40 tris
   Y=4500   28 vértices   raio 5694..7570   <- anel de cima, mais estreito
   Y=2156   32 vértices   raio 6831..8768
   Y= 316   18 vértices   raio 6831..8768

dome1 — 77 vértices, 42 tris
   Y=2276    1 vértice    raio  457         <- ápice, fecha em ponta
   Y=1901   24 vértices   raio 2884..3479
   Y=1009   32 vértices   raio 3438..4019
   Y= -143  16 vértices   raio 3438..4019
```

`dome1` fecha no topo; `dome13` é tronco de cone aberto (no Ancient Lake não se olha para o
zênite). Os raios não são constantes dentro do mesmo anel — são polígonos irregulares modelados à
mão. O único plane do lote é `dome17`, e ele não é referenciado por nenhum header.

Cada domo carrega **sua própria tabela de texturas**, independente da pista.

### Renderização: paralaxe zero, não paralaxe

```c
void skydome_render(void) {                      // src/tracks.c:1644
    cam = cam_get_active_camera();
    if (gCurrentLevelHeader2->skyDome == 0) {
        gSkydomeSegment->trans.x_position = cam->trans.x_position;
        gSkydomeSegment->trans.y_position = cam->trans.y_position;
        gSkydomeSegment->trans.z_position = cam->trans.z_position;
    }
    mtx_world_origin(&gTrackDL, &gTrackMtxPtr);
    if (gSceneRenderSkyDome) render_object(..., gSkydomeSegment);
}
```

Copia **só a posição**, nunca a rotação. A translação da câmera é cancelada exatamente —
deslocamento relativo zero. É o cancelamento total que faz um domo de raio 8000 ler como horizonte
infinito. O que muda a imagem é a rotação, que nunca é copiada: o domo mantém orientação fixa no
mundo e a câmera gira dentro da casca parada.

Ordem por viewport (`src/tracks.c:359-395`): `viewport_main` → céu → `initialise_player_viewport_vars`
→ `render_level_geometry_and_objects`. O céu é pintado **antes** da pista.

### Três caminhos

```c
if (numViewports < 2) {                              // 1 jogador
    if (gCurrentLevelHeader2->skyDome == -1) trackbg_render_flashy();
    else                                             skydome_render();
} else {                                             // 2+ jogadores
    trackbg_render_gradient();
}
```

- **1 jogador** — o domo texturizado.
- **2 a 4 jogadores** — o domo nem é desenhado. Gradiente de dois vértices coloridos com
  `BGColourTop*`/`BGColourBottom*` (`0xBE`–`0xC3`). Economia de fillrate em tela dividida.
- **Wizpig 2** — único nível com `special-sky = -1` (campo `skyDome`, `0x49`), cai em
  `trackbg_render_flashy()` com `special-sky-texture = 63`.

### `SKYCONTROL`: implementado, nunca usado

`ASSET_OBJECT_SKYCONTROL` / `BHV_SKY_CONTROL`: objeto com `setting` e `radius` que chama
`set_skydome_visbility()` quando o jogador entra no raio, para desligar o céu em túnel
(`src/object_functions.c:4173`). **Zero ocorrências** nos 136 object maps retail. Só existe o
sprite `ASSET_SPRITE_OBJECTS_DEBUGSKYCONTROL`. Funciona e está disponível para custom track.

### Para o addon

`tools/blender/dkr_track_editor/level_header.py:139` já mapeia `0x38` → `/background/skybox/id`, e
`0x49`/`0xA4` os campos do céu especial. Trocar o céu de uma custom track é trocar um id. Criar um
céu **novo** exigiria adicionar `ObjectHeader` + `ObjectModel` na seção de objetos, o que o
packager atual não escreve.

Se for modelar um: casca de 40–90 triângulos centrada na origem, raio horizontal ~3000–10000,
anel de baixo abaixo da altura do olho, topo fechado num vértice ou aberto. Texturas próprias.
Cabe em ~2 KB comprimido.

---

## 3. Checkpoints

### Estrutura

28 bytes. `obj_loop_checkpoint` é vazia — o checkpoint é dado, não comportamento.

| Off | Campo | Tipo | O que é |
|---|---|---|---|
| 0x08 | `scale` | u8 | Largura do portão. Mínimo forçado de 5, dividido por 64. |
| 0x09 | `index` | u8 | Chave de ordenação. Só a ordem relativa importa. |
| 0x0A | `angleY` | u8 | Direção do portão (graus ÷ 64). |
| 0x0B–0x0E | `unkB`…`unkE` | s8[4] | **Deslocamento lateral por faixa da IA.** |
| 0x0F–0x12 | `unkF`…`unk12` | s8[4] | **Deslocamento vertical por faixa da IA.** |
| 0x13–0x16 | `unk13`…`unk16` | s8[4] | **Flag de comportamento por faixa da IA.** |
| 0x17 | `isAltCheckpoint` | u8 | Pertence à rota alternativa. |
| 0x1A | `vehicleType` | s8 | Filtro de cadeia contra `gTajChallengeType`. |

### Montagem da cadeia

`checkpoint_update_all()` (`src/objects.c:5533`), uma vez por carga:

1. **Filtra por veículo** — só entram os com `vehicleType == gTajChallengeType`. É assim que a
   mesma pista tem traçados distintos para carro/hovercraft/avião nos desafios do Taj.
2. **Separa a rota alternativa** — `isAltCheckpoint` ganha `+255` no índice.
3. **Ordena por índice** (bubble sort). Duplicado imprime `Error: Multiple checkpoint no: %d !!`
4. **Casa alternativo com principal** de mesmo índice, preenchendo `altRouteID` nos dois sentidos.
5. **Fecha o laço e mede** — cada nó guarda distância até o próximo; o último aponta para o primeiro.

### O índice é descartado

Depois da ordenação, `checkpoint->checkpointID = obj->trans.scale * 128.0f` — o campo passa a
guardar a largura do portão. O valor de `index` não sobrevive à inicialização.

**Numerar 0, 2, 4, 6 e numerar 0, 1, 2, 3 produzem exatamente a mesma corrida.**

### Hipótese dos índices 0, 2, 4, 6 — convenção, não regra

Medido em 1256 checkpoints, 41 cadeias (agrupadas por mapa + `vehicleType`, excluindo alt):

```
passos entre índices consecutivos: {1: 224, 2: 931, 3: 2, 4: 16, 24: 1, 34: 1}
índice inicial das cadeias:        {0: 30, 2: 11}
cadeias só com índices pares:      21 de 41
```

Passo 2 domina, e 21 cadeias são inteiramente pares. Mas 20 misturam, e CentralAreaHub tem saltos
de 24 e 34. O passo 2 deixa espaço para inserir um checkpoint sem renumerar a pista — os 224
intervalos de 1 são exatamente essas inserções.

### Hipótese "checkpoint 0 inicia a corrida" — REFUTADA

11 das 41 cadeias começam no índice 2 (Bluey1/2, JungleFalls, HauntedWoods, SpacedustAlley,
HotTopVolcano, TreasureCaves…). O que inicia é a **posição 0 do array** depois da ordenação.

E a volta incrementa quando `nextCheckpoint` passa do fim e volta a zero, ou seja **ao cruzar o
último checkpoint da cadeia**. É esse que funciona como linha de chegada.

### Limites

| | |
|---|---|
| `MAX_CHECKPOINTS` | 60 (principais + alternativos) |
| maior cadeia retail | 48 (Windmill Plains, `vehicleType 1`) |

### O elo com a IA — quatro linhas de corrida por checkpoint

Achado não documentado. Os 12 bytes "unk" são 4 faixas × (lateral, vertical, flag). O índice é
`racer->unk1CA`, de 0 a 3, atribuído pela posição na corrida (`D_800DCDA0[racer->racePosition]`).

```c
splineX[i] += checkpoint->scale * checkpoint->rotationZFrac  * checkpoint->unk2E[racer->unk1CA];
splineY[i] += checkpoint->scale                              * checkpoint->unk32[racer->unk1CA];
splineZ[i] += checkpoint->scale * -checkpoint->rotationXFrac * checkpoint->unk2E[racer->unk1CA];
// src/racer.c:1426-1428
```

Ponto da spline = posição do checkpoint + deslocamento lateral ao longo do vetor perpendicular do
portão, escalado pela largura, + deslocamento vertical. Quatro racers, quatro traçados pelo mesmo
portão.

Quando um racer da IA fica preso, `racer_AI_pathing_inputs` faz `unk1CA = (unk1CA + 1) & 3`. Se
dois racers estão na mesma faixa a menos de 100 unidades, um troca (`src/racer.c:448-450`).

### Flags por faixa (`unk36[lane]`)

| valor | efeito | onde |
|---|---|---|
| 2 | Força o racer para a rota alternativa ao cruzar. | `src/racer.c:8846` |
| 4 | Amortece a velocidade em 10% por frame — dica de "freie aqui". | `src/racer.c:8840` |
| 5 | Arma `unk201 = 30`; se estiver na água, manda para a rota alternativa. | `src/racer.c:8831` |
| 6 | **Encerra a corrida imediatamente**: `racer->lap = laps + 1`. | `src/racer.c:8837` |

### Hipótese "corrida precisa de checkpoint" — CONFIRMADA

Sem checkpoints o nível carrega e roda, mas não é corrida:

- `func_80045C48` começa com `if (get_checkpoint_count() == 0) { gCurrentStickX = 25; return; }`
  — a IA trava o analógico à direita.
- `checkpoint_is_passed` devolve 1 sem testar nada. Ninguém passa de checkpoint, ninguém completa
  volta.
- `courseCheckpoint` fica 0 para todos, e é ele que ordena a classificação (`src/objects.c:6319`).
- Respawn tem plano B: `func_800230D0` (`src/objects.c:9904`) normalmente devolve o racer 35
  unidades atrás do último checkpoint; com a cadeia vazia procura o setup point de `racerIndex 0`.

É essa ramificação que torna um nível sem checkpoint jogável — e é o que os hubs fazem.

### Andar de ré

Cruzar um portão de trás para frente chama `racer_update_progress` (`src/racer.c:9249`), que
decrementa `nextCheckpoint`, `lap` e `courseCheckpoint`. É assim que o jogo impede ganho de
posição andando ao contrário.

---

## 4. Tipos de track

Um byte no level header, `0x4C`. Não descreve a geometria — escolhe qual função de IA roda,
quantos racers nascem e qual HUD aparece.

| Valor | Constante | Racers | IA usa | Níveis |
|---|---|---|---|---|
| 0 | `RACETYPE_DEFAULT` | 8 | spline de checkpoints | 20 pistas |
| 3 | `RACETYPE_HORSESHOE_GULCH` | 8 | spline de checkpoints | Horseshoe Gulch |
| 5 | `RACETYPE_HUBWORLD` | nº jogadores | — | 16 hubs e sequências |
| 6 / 7 | `RACETYPE_CUTSCENE_1` / `_2` | 1 | — | 14 cenas |
| 8 | `RACETYPE_BOSS` | 2 | spline de checkpoints | 10 corridas de chefe |
| 64 | `RACETYPE_CHALLENGE_BATTLE` | 4 | **grafo de AI nodes** | Darkwater Beach, Icicle Pyramid |
| 65 | `RACETYPE_CHALLENGE_BANANAS` | 4 | **grafo de AI nodes** | Smokey Castle |
| 66 | `RACETYPE_CHALLENGE_EGGS` | 4 | `racer_ai_eggs` | Fire Mountain |

O bit `0x40` é máscara de desafio: `race_type & RACETYPE_CHALLENGE` pega os três.

**A fase da boia e a fase do gelo são o mesmo tipo.** Darkwater Beach e Icicle Pyramid são ambas
`RACETYPE_CHALLENGE_BATTLE`; o que difere é só o nível. "Pegar os ovos" (Fire Mountain) e "levar
as bananas ao tesouro" (Smokey Castle) têm tipos próprios, com um único nível cada no jogo inteiro.

Arena não tem cadeia de checkpoints — é por isso que os desafios usam AI nodes.

---

## 5. Voltas

Um byte, `0x4B` do level header. `tools/blender/dkr_track_editor/level_header.py:152` mapeia para
`/lap-count`, default 3.

| lap-count | níveis |
|---|---|
| 3 | 61 — todo o resto, inclusive Bubbler, Smokey e Wizpig |
| 1 | 4 — Bluey 1 e 2, Trickytops 1 e 2 |

O contador vive em `racer->lap` e é comparado com `levelHeader->laps`. Duas coisas furam isso: a
flag **6** de um checkpoint (`lap = laps + 1`, encerra na hora) e o teto rígido de 120 voltas no
incremento. `courseCheckpoint` tem seu próprio limite, `(laps + 3) * nº de checkpoints`.

---

## 6. Minimapa

### Não é gerado

É um **sprite desenhado à mão**; o jogo só projeta a posição do jogador em cima. Todos os
parâmetros ficam no header do **level model** — não no level header, não no object map.

| Off | Campo | Tipo | O que é |
|---|---|---|---|
| 0x20 | `minimapSpriteIndex` | s32 | Id do sprite com a imagem. `0` = sem minimapa. |
| 0x24 | `minimapRotation` | u16 | Rotação em **graus**, 0–359, via `(rot * 0xFFFF) / 360`. |
| 0x28 | `minimapXScale` | f32 | Escala horizontal do marcador. |
| 0x2C | `minimapYScale` | f32 | Escala vertical do marcador. |
| 0x30 | `minimapOffsetXAdv1` / `Y` | s16 ×2 | Deslocamento na tela, Adventure 1. |
| 0x34 | `minimapOffsetXAdv2` / `Y` | s16 ×2 | Deslocamento na tela, Adventure 2 (espelhada). |
| 0x38 | `minimapColor` | u32 | Tinta RGB aplicada ao sprite inteiro. |
| 0x3C | `lower/upperXYZBounds` | s16 ×6 | Caixa envolvente. É a régua da projeção. |

### A projeção

`minimap_marker_pos`, `src/game_ui.c`:

```c
a = upperXBounds - lowerXBounds;
b = upperZBounds - lowerZBounds;
scaledX = (60.0f * (a / b) * minimapXScale * (x - lowerXBounds)) / a;
scaledY = (minimapYScale * -60.0f          * (z - lowerZBounds)) / b;

pos.x = telaX + (scaledX * cos + scaledY * sin) + offsetXAdv1 - dotOffsetX;
pos.y = telaY - (scaledX * sin - scaledY * cos) + offsetYAdv1 + dotOffsetY;
```

Normaliza X e Z para 0–1 dentro da caixa envolvente, multiplica por 60 pixels, corrige a proporção
pela razão largura/profundidade, rotaciona pelo ângulo do sprite e desloca. Só X e Z entram —
altura é ignorada, e por isso pistas sobrepostas ficam ambíguas no mapa.

`dotOffsetX/Y` vem de `load_sprite_info` sobre o próprio sprite, então o tamanho da imagem entra
na equação. Em Adventure 2 o jogo usa os offsets alternativos e inverte o sinal do termo
rotacionado.

### Calibração retail (30 modelos com minimapa)

| Modelo | Sprite | Rot° | EscX | EscY | OffX¹ | OffY¹ | OffX² | OffY² | Tinta | Bounds X | Bounds Z |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ancient_lake | 1 | 0 | 1.56 | 1.49 | 6 | 9 | 41 | 9 | ffffff | −5918 … −23 | −12559 … −2048 |
| pirate_lagoon | 2 | 269 | 1.13 | 1.13 | 9 | −81 | 44 | −81 | ffffff | −5445 … 6145 | −4040 … 4586 |
| whale_bay | 3 | 0 | 1.40 | 1.40 | −8 | 5 | 66 | 9 | ffffff | −5160 … 5160 | −5806 … 4515 |
| snowball_valley | 4 | 269 | 0.81 | 0.81 | 9 | −86 | 48 | −86 | ff3c68 | −4807 … 5339 | −3428 … 2670 |
| hot_top_volcano | 5 | 89 | 0.60 | 0.60 | 53 | −1 | −1 | 0 | ffffff | −5600 … 8742 | −3984 … 3187 |
| crescent_island | 6 | 272 | 1.14 | 1.14 | 6 | −61 | 68 | −62 | 3c8080 | −6474 … 8831 | −7653 … 5102 |
| everfrost_peak | 8 | 179 | 0.91 | 0.91 | 71 | −57 | 0 | −57 | ff3c68 | −6094 … 4573 | −12451 … −1790 |
| walrus_cove | 10 | 0 | 1.00 | 1.00 | 15 | −6 | 59 | −5 | ff3c68 | −6400 … 6400 | −5974 … 6400 |
| boulder_canyon | 12 | 177 | 0.87 | 0.87 | 72 | −47 | −2 | −48 | ffffff | −3840 … 7680 | −7063 … 3840 |
| smokey_castle | 13 | 0 | 0.88 | 0.88 | 16 | −5 | 0 | 0 | ffffff | −4488 … 2560 | −2880 … 4302 |
| central_area_hub | 14 | 357 | 1.40 | 1.40 | 19 | 20 | 0 | 0 | ffffff | −4702 … 10101 | −8081 … 4807 |
| fire_mountain | 15 | 0 | 0.74 | 0.74 | 18 | −7 | 0 | 0 | ffffff | −2668 … 2668 | −2668 … 2668 |
| spaceport_alpha | 16 | 294 | 0.91 | 0.91 | 5 | −46 | 70 | −46 | ffffff | −5112 … 6105 | −3810 … 6777 |
| spacedust_alley | 17 | 90 | 0.72 | 0.72 | 61 | −5 | 1 | −3 | ffffff | −7185 … 6931 | −5688 … 3316 |
| treasure_caves | 18 | 0 | 1.56 | 1.56 | 15 | 21 | 49 | 22 | ffffff | −4276 … 6414 | −8552 … 4276 |
| greenwood_village | 19 | 270 | 0.97 | 0.97 | 17 | −39 | 58 | −39 | ffffff | −4162 … 2884 | −7136 … 4255 |
| darkmoon_caverns | 20 | 105 | 0.90 | 0.90 | 76 | −5 | −5 | −5 | ffffff | −3903 … 7390 | −8519 … 1102 |
| star_city | 21 | 178 | 1.08 | 1.08 | 57 | −63 | 1 | −61 | ffffff | −729 … 14158 | −3872 … 10319 |
| windmill_plains | 22 | 0 | 1.04 | 1.04 | 11 | −5 | 49 | −6 | ffffff | −7065 … 3383 | −13728 … 307 |
| frosty_village | 23 | 268 | 1.40 | 1.40 | 14 | −44 | 66 | −44 | ff3c68 | −4624 … 9600 | −7743 … 11872 |
| darkwater_beach | 24 | 359 | 0.76 | 0.76 | −26 | −7 | 0 | 0 | ffffff | −11520 … 3840 | −3840 … 3840 |
| jungle_falls | 25 | 269 | 0.99 | 0.99 | 19 | −54 | 58 | −56 | ffffff | −4510 … 3917 | −4719 … 4451 |
| icicle_pyramid | 26 | 180 | 0.92 | 0.92 | 60 | −59 | 0 | 0 | ff3c68 | −2256 … 2206 | −2820 … 2820 |
| haunted_woods | 27 | 269 | 0.87 | 0.87 | −2 | −72 | 49 | −70 | ffffff | −3840 … 5057 | −3840 … 2560 |
| bluey | 28 | 177 | 0.85 | 0.85 | 71 | −55 | −1 | −53 | ff3c68 | −18934 … 3367 | −9133 … 12885 |
| bubbler | 29 | 314 | 1.34 | 1.34 | −13 | −31 | 75 | −30 | ffffff | −5760 … 5760 | −5760 … 5760 |
| smokey | 30 | 278 | 1.00 | 1.00 | 15 | −56 | 60 | −55 | ffffff | −7561 … 1751 | −5213 … 4788 |
| trickytops | 31 | 200 | 1.96 | 1.96 | 84 | −98 | −10 | −97 | ffffff | −11008 … 8992 | −8458 … 12800 |
| wizpig1 | 32 | 50 | 1.04 | 1.04 | 49 | 9 | 22 | 12 | ffffff | −5120 … 5120 | −5120 … 5120 |
| wizpig2 | 33 | 236 | 0.78 | 0.78 | 37 | −73 | 27 | −73 | ffffff | −8446 … 7126 | −6204 … 4206 |

Três leituras: as escalas X e Y são idênticas em 29 dos 30 — **Ancient Lake é o único que difere**
(1.56 vs 1.49). A rotação quase nunca é 0: o mapa foi desenhado na orientação que ficou boa e
corrigido pelo campo. A tinta é branca em quase tudo, exceto os seis níveis de Snowflake Mountain
(`ff3c68`) e Crescent Island (`3c8080`) — é tingimento por mundo.

### O marcador do jogador

Não existe um marcador por jogador. Existe **um único** `HUD_MINIMAP_MARKER`, reposicionado,
recolorido e redesenhado uma vez por entidade na mesma display list. A cor vem de
`gHudMinimapColours[characterId]`. Também desenha o fantasma do time trial (cinza 60,60,60), o
fantasma da Rare e, nos hubs, o Taj — em magenta puro `255,0,255`.

Em `RACETYPE_CHALLENGE_BATTLE` a escala do marcador codifica altura: 0.8 no nível baixo, 1.0 no
normal, 1.2 nos dois de cima (`src/game_ui.c:3739`). Os limiares vêm dos AI nodes da arena.

### Para uma pista customizada

Faltam duas coisas que o packager não escreve: um sprite novo na seção de sprites, e os oito
campos de `0x20` a `0x38` no header do level model. As bounds em `0x3C` já saem corretas do
modelo, então a projeção funciona sozinha — o trabalho é desenhar o sprite na mesma proporção da
caixa envolvente e depois acertar rotação e offsets.

Atalho para validar: reaproveitar o `minimapSpriteIndex` de uma pista retail dá um marcador que se
move corretamente sobre o desenho errado.

---

## 7. Spawners (`ASSET_OBJECT_SETUPPOINT`)

| Off | Campo | Tipo | O que é |
|---|---|---|---|
| 0x08 | `racerIndex` | u8 | Qual slot da grelha preenche. Só `< 8` é lido. |
| 0x09 | `entranceID` | u8 | Filtro de conjunto. |
| 0x0A | `angleY` | u8 | Direção inicial (graus ÷ 64). |
| 0x0B | `vehicle` | s8 | Sobrescreve o veículo. `−1` deixa como está. |

O coletor (`src/objects.c:1121-1136`):

```c
for (j = 0; j < ARRAY_COUNT(spawnZ); j++) { spawnX[j] = 0; spawnY[j] = 0; spawnZ[j] = 0; }   // 8 slots, zerados

if (obj->behaviorId == BHV_SETUP_POINT)
  if (entranceID == obj->properties.setupPoint.entranceID)
    if (obj->properties.setupPoint.racerIndex < 8)
      spawnX[racerIndex] = obj->trans.x_position;   // ... e Y, Z, ângulo
```

### Quantos, por tipo

A contagem retail bate exatamente com o `gNumRacers` que o tipo impõe:

| Tipo | Setup points | racerIndex | entranceID | Exemplos |
|---|---|---|---|---|
| Corrida | 8 | 0–7 | 0 | todas as 20 pistas |
| Desafio | 4 | 0–3 | 0 | Fire Mountain, Icicle Pyramid, Smokey Castle, Darkwater Beach |
| Chefe | 2 | 0–1 | 0 | as 10 corridas de chefe |
| Hub | 1 por entrada | 0 | 0–7 | 5 a 7 entradas por hub |
| Cutscene | 1 | 0 | 0 | 14 sequências |

Nenhuma entrada do jogo inteiro repete um `racerIndex` dentro do mesmo `entranceID`.

### Sobrando e faltando

- **`racerIndex >= 8`** — ignorados em silêncio pelo teste `< 8`. Mas atenção: a atribuição de
  `vehicle` fica **fora** desse teste e dentro do teste de `entranceID`, então um spawner
  descartado ainda pode trocar o veículo da pista inteira.
- **`entranceID` diferente** — inertes até alguém entrar por aquela porta. É o mecanismo dos hubs
  e funciona em qualquer tipo de nível.
- **Índice repetido** — não dá erro nem aviso, ao contrário do checkpoint. O último varrido
  sobrescreve o anterior no mesmo slot; qualquer índice sem dono continua em `(0, 0, 0)`.
- **Spawners a menos** — os slots vazios permanecem `(0, 0, 0)`. Com 4 pontos numa pista de
  corrida, quatro racers nascem empilhados na origem do mundo. Não trava, mas quebra a corrida.

### O slot 0 tem peso extra

Três sistemas leem o slot 0: o fantasma do time trial nasce em `spawnX[0]`, o respawn sem
checkpoints procura o setup point de `racerIndex 0`, e nesse mesmo caminho a orientação do racer é
copiada do `angleY` dele. É o único setup point que nunca pode faltar.

### Hipótese do lobby — CONFIRMADA

Um nível com **um setup point e nenhum checkpoint** é exatamente a configuração dos sete hubs.
`RACETYPE_HUBWORLD` põe `gNumRacers = numPlayers`, então nenhum slot fica vazio; sem cadeia de
checkpoints não há voltas nem classificação; e o respawn cai na ramificação que usa o próprio setup
point. Espalhar setup points por `entranceID` distintos dá pontos de chegada diferentes conforme a
porta usada — é como Central Area distingue suas seis entradas.

---

## 8. Materiais de superfície (grama, pista, água)

### Onde o material mora: na textura, não no triângulo

Não existe material por triângulo nem por objeto. O material é o byte `0x07` de cada entrada da
**tabela de texturas do level model** (`include/structs.h:564`):

```c
/* Size: 8 bytes */
typedef struct TextureInfo {
    /* 0x00 */ TextureHeader *texture;
    /* 0x04 */ u8 width;
    /* 0x05 */ u8 height;
    /* 0x06 */ u8 format;
    /* 0x07 */ s8 surfaceType;   // <- o material
} TextureInfo;
```

Todo triângulo herda o material do *batch* a que pertence, via `batch.textureIndex`. O comentário
do próprio decomp em `src/hasm/collision.c:143` diz isso: *"Skip batches with no texture, since the
surface type is stored in the texture data"*.

Duas consequências diretas:

- **Batch sem textura (`textureIndex == 255`) não tem colisão nenhuma.** É pulado antes de qualquer
  teste.
- Para ter a mesma imagem com dois comportamentos, basta duas entradas na tabela apontando para a
  mesma textura com `surfaceType` diferente. O material é do *slot*, não do arquivo de textura.

### O enum

`include/enums.h:152`. Os nomes `UNK` codificam o índice em hexadecimal (`UNK10` = 0x10):

| # | Constante | # | Constante |
|---|---|---|---|
| 0 | `SURFACE_DEFAULT` (asfalto/pista) | 10 | `SURFACE_FROZEN_WATER` |
| 1 | `SURFACE_GRASS` | 11 | `SURFACE_WATER_CALM` |
| 2 | `SURFACE_SAND` | 12 | `SURFACE_TAJ_PAD` |
| 3 | `SURFACE_ZIP_PAD` | 13 | `SURFACE_SNOW` |
| 4 | `SURFACE_STONE` | 14 | `SURFACE_WATER_WAVY` |
| 5 | `SURFACE_EGG_SPAWN` | 15 | `SURFACE_WATER_UNK_F` |
| 6–9 | `SURFACE_EGG_01`…`_04` | 16 | `SURFACE_UNK10` |
| | | 17 | `SURFACE_INVIS_WALL` |
| | | 18 | `SURFACE_UNK12` |
| | | 255 | `SURFACE_NONE` |

### A ordem do enum é ordem de prioridade

Dois lugares resolvem conflito pegando o **maior valor**:

```c
// src/hasm/collision.c:398 — vários triângulos sob o mesmo ponto
// Store surface type with highest priority (higher ID wins)
if (*surface < gCollisionSurfaces[i]) { *surface = gCollisionSurfaces[i]; }

// src/racer.c:5347 — as quatro rodas em superfícies diferentes
if (surfaceType < racer->wheel_surfaces[i]) { surfaceType = racer->wheel_surfaces[i]; }
```

Então uma roda na grama e três no asfalto reportam GRASS para os efeitos que usam um único tipo
(som, partícula, zip pad). Mas a **física é somada roda a roda**, não pelo vencedor — ver abaixo.

### As tabelas de comportamento

Sete tabelas de 19 entradas indexadas pelo tipo, em `src/racer.c:58-90`:

```c
// Perda de velocidade. "grass will slow you down more than the road"
// Um gatilho antipirataria pode pôr 0.5f no índice 0, tornando a pista intransitável.
f32 gSurfaceTractionTable[19] = {
    0.004, 0.007, 0.01, 0.004, 0.01, 0.01, 0.01, 0.01, 0.01, 0.01,
    0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004, 0.004 };

// Aderência lateral (derrapagem)
f32 D_800DCBE8[19] = { 0.8, 0.85, 0.85, 0.5, 0.5, ... 0.8, 0.84, 0.8, ... };

// Carro balança para dar sensação de terreno irregular. Só grama e areia.
s32 gSurfaceBobbingTable[19] = { 0, 1, 1, 0, 0, ... };

// Som ao aterrissar. "they did only two surface types then called it a day"
s32 gSurfaceSoundTable[19] = { SOUND_NONE, SOUND_LAND_GRASS, SOUND_LAND_SAND, SOUND_NONE, ... };

s32 gSurfaceFlagTable[19]   = { 1, 4, 0x10, 1, 1, ..., 0x100(SNOW), 1, ... };
s32 gSurfaceFlagTable4P[20] = { 0, 4, 0x10, 0, 0, ..., 0x100(SNOW), 0, ... };
u16 D_800DCCCC[19]          = { 0x010C, 0x010B, 0x0009, 0x010C, ..., 0x0005(WATER_CALM), ... };
```

Resumo do que diferencia os três materiais que você perguntou:

| | pista (`DEFAULT`) | grama (`GRASS`) | areia (`SAND`) |
|---|---|---|---|
| perda de velocidade | 0.004 | **0.007** (75% pior) | **0.010** (150% pior) |
| aderência lateral | 0.80 | 0.85 | 0.85 |
| balança o carro | não | **sim** | **sim** |
| som ao aterrissar | nenhum | `SOUND_LAND_GRASS` | `SOUND_LAND_SAND` |
| rumble a alta velocidade | não | não | **sim** (`src/racer.c:5377`) |
| cor da marca de pneu | — | 192, 8, 64 | 255, 96, 8 |

Note que a aderência lateral de grama e areia é *maior* que a da pista (0.85 vs 0.80) — elas
seguram mais na curva, mas custam velocidade. A neve tem 0.84 e a flag `0x100`, exclusiva dela.

Cores de partícula em `src/particles.c:120`, `gVehicleTrackMarkColors[16]` — só GRASS, SAND, ZIP_PAD
e SNOW têm cor; o resto é alfa 0 (sem marca).

### Como a superfície chega ao carro

`resolve_collisions` (`src/hasm/collision.c:282`) recebe os pontos de consulta e devolve um
`s8 *surface` por ponto. O racer consulta as quatro rodas e guarda em
`Object_Racer.wheel_surfaces[4]` (offset `0x1DC`). Depois, `src/racer.c:5336-5354` **soma** roda a
roda:

```c
for (i = 0; i < 4; i++) {
    if (racer->wheel_surfaces[i] != SURFACE_NONE) {
        traction        += gSurfaceTractionTable[racer->wheel_surfaces[i]];
        surfaceTraction += D_800DCBE8[racer->wheel_surfaces[i]];
        sp68            += gSurfaceBobbingTable[racer->wheel_surfaces[i]];
        if (surfaceType < racer->wheel_surfaces[i]) surfaceType = racer->wheel_surfaces[i];
        if (racer->wheel_surfaces[i] == SURFACE_STONE) stoneWheels++;
    }
}
```

Duas rodas na grama e duas na pista dá uma penalidade intermediária — a transição é contínua, não
binária. O cheat `CHEAT_FOUR_WHEEL_DRIVER` força o índice 0 para todas as rodas, ou seja, anda em
qualquer terreno como se fosse asfalto.

### Materiais que fazem coisa especial

| Superfície | Efeito | Onde |
|---|---|---|
| `STONE` | Conta `stoneWheels`; corta a velocidade máxima por `ASSET_MISC_32[stoneWheels]`, zera `magnetTimer` e a velocidade lateral. Também é a exceção do empurrão vertical na colisão. | `racer.c:5407`, `collision.c:371` |
| `ZIP_PAD` | `boostTimer = normalise_time(45)`, `BOOST_LARGE`, toca `SOUND_ZIP_PAD_BOOST`. | `racer.c:5417` |
| `TAJ_PAD` | Só para `PLAYER_ONE` com as rodas no chão — gatilho do desafio do Taj. | `racer.c:5367` |
| `FROZEN_WATER` | `BOOST_SOUND_UNK4` + `SOUND_BOUNCE2`. | `racer.c:5412` |
| `SAND` | Rumble acima de velocidade 2. | `racer.c:5377` |
| `EGG_SPAWN`, `EGG_01`–`_04` | Zonas do desafio dos ovos (Fire Mountain). | — |

### Passabilidade: quem atravessa o quê

Em `src/hasm/collision.c:145-153`, três materiais não são sólidos:

```c
surface = gCurrentLevelModel->textures[seg->batches[batchIndex].textureIndex].surfaceType;
if (surface == SURFACE_WATER_CALM                              // atravessa para todo mundo
    || (vehicleID == VEHICLE_PLANE && surface == SURFACE_INVIS_WALL)   // avião passa
    || (vehicleID != VEHICLE_CAR   && surface == SURFACE_UNK12)) {     // só o carro é barrado
    continue;
}
```

Mais dois filtros antes disso: batches com `RENDER_NO_COLLISION` são pulados sempre, e batches com
`RENDER_HIDDEN` são pulados para objetos que não são veículos (projéteis, pickups) — ou seja, uma
parede invisível barra o carro mas deixa o míssil passar.

### Inclinação: o que é chão e o que é parede

`B` é a componente Y da normal do triângulo. `src/hasm/collision.c:371-383`:

- `B >= 0.707` (≈ 45° ou menos de inclinação) **e não é STONE** → empurra o objeto verticalmente
  para cima. É chão pisável.
- `B < 0.45` (≈ 63° ou mais) → `gHitWall = TRUE`, guarda a normal. É parede.
- Entre 0.45 e 0.707, ou qualquer inclinação em STONE → empurra ao longo da normal.

Ou seja, **STONE nunca recebe o empurrão vertical**: mesmo plana, ela empurra pela normal. É o que
faz pedra "raspar" em vez de sustentar.

### Água: dois caminhos completamente distintos

Água não é colisão. É um **plano com altura**, e o jogo compara o seu Y contra ele.

**1. Água de geometria** — `get_level_segment_waves` (`src/tracks.c:2494`) varre os batches do
segmento e transforma em plano de água todo batch cuja textura tenha `surfaceType` **11
(`WATER_CALM`)** ou **15 (`WATER_UNK_F`)**. A altura vem da equação do plano do triângulo avaliada
no seu X/Z. Batches de água são processados mesmo marcados como `RENDER_HIDDEN`.

**2. Água simulada** — se o segmento tem `hasWaves` e existe bloco de ondas
(`gWaveBlockCount != 0`), é anexado um plano extra de tipo **14 (`WATER_WAVY`)** cuja altura vem da
simulação `func_800BB2F4`, alimentada pelos parâmetros de onda do level header (`0x56`–`0x71`:
`waveSubdivisons`, `waveSineHeight0/1`, `waveSeedSize`, `waveTexID`, `waveUVScrollX/Y`…).

Isso explica a assimetria que parecia bug: **`WATER_WAVY` nunca aparece numa tabela de texturas**
porque não é material de textura — é sintetizado.

Depois, `get_wave_properties` (`src/tracks.c:2450`) escolhe o plano relevante e devolve tipo e
altura. Ele aceita só `WATER_CALM` e `WATER_WAVY`. O racer então:

```c
gRacerWaveType = get_wave_properties(obj->trans.y_position, &waterHeight, &gCurrentRacerWaterPos);
if (gRacerWaveType != SURFACE_DEFAULT) {
    if (obj->trans.y_position - 5.0f < waterHeight) {
        tempRacer->waterTimer = 5;
        tempRacer->buoyancy = waterHeight - (obj->trans.y_position - 5.0f);
    }
}
// src/racer.c:4384-4392 — pulado inteiro para VEHICLE_HOVERCRAFT
```

`buoyancy` é o quanto você está submerso. `waterTimer` conta 5 e decai fora d'água — é ele que
alimenta as flags de checkpoint 2 e 5 (rota alternativa quando na água).

Em `WATER_WAVY` há ainda amortecimento de velocidade proporcional à onda (`src/racer.c:2489`).

**`WATER_UNK_F` (15) é o plano de morte.** Ele não é aceito por `get_wave_properties`; seu único
consumidor é `src/racer.c:4374-4381`:

```c
for (i = 0; i < gRacerWaveCount; i++) {
    if (gRacerCurrentWave[i]->type == SURFACE_WATER_UNK_F) {
        if (gRacerCurrentWave[i]->waveHeight < gCurrentCourseHeight) {
            gCurrentCourseHeight = gRacerCurrentWave[i]->waveHeight;
        }
    }
}
```

Ele **rebaixa localmente o `course-height`** — a altura abaixo da qual o racer é resetado. O valor
base é o `course_height` do level header (`0x08`, 400.0 no Ancient Lake). É por isso que Ancient
Lake, Jungle Falls, Frosty Village e Hot Top Volcano têm exatamente **uma** textura tipo 15 cada:
é a lâmina que define o fundo do mundo naquele trecho.

### Uso real no cartucho

Contagem sobre as tabelas de textura dos 55 level models:

| Tipo | texturas | Tipo | texturas |
|---|---|---|---|
| `DEFAULT` | 1020 | `WATER_CALM` | 14 |
| `STONE` | 212 | `WATER_UNK_F` | 11 |
| `SAND` | 34 | `TAJ_PAD` | 5 |
| `SNOW` | 26 | `INVIS_WALL` | 5 |
| `GRASS` | **18** | `FROZEN_WATER` | 3 |
| `EGG_*` | 1 cada (5) | `UNK12` | 3 |
| `ZIP_PAD` | **0** | `WATER_WAVY` | **0** |

Exemplos por pista:

```
ancient_lake       DEFAULT 19, GRASS 1, SAND 1, STONE 2, WATER_UNK_F 1, INVIS_WALL 1
jungle_falls       DEFAULT 21, STONE 2, WATER_CALM 1, WATER_UNK_F 1
frosty_village     DEFAULT 25, STONE 6, SNOW 2, WATER_UNK_F 1
whale_bay          DEFAULT 13, SAND 4, STONE 14
pirate_lagoon      DEFAULT 22, STONE 4
hot_top_volcano    DEFAULT 18, STONE 1, WATER_UNK_F 1
```

Três leituras:

- **Grama é rara: 18 texturas no jogo inteiro.** Ancient Lake tem *uma*. A maior parte do que
  parece grama é `DEFAULT` — visualmente verde, mecanicamente idêntico ao asfalto. A penalidade
  real está reservada para poucas faixas.
- **`ZIP_PAD` nunca aparece na geometria.** Os turbos são objetos (`ASSET_OBJECT_GROUNDZIPPER`,
  `BHV_ZIPPER_GROUND`), não superfícies. O tipo 3 existe e é funcional, mas nenhuma pista retail o
  usa via textura.
- **`STONE` é o segundo mais usado (212)** e é o material que mais muda a física: corta velocidade
  máxima, zera aderência lateral e nunca recebe o empurrão vertical.

### Para o addon

`tools/blender/dkr_track_editor/level_model.py` **já lê** o campo: `TextureRef.surface_type`, com o
comentário certo (*"`SurfaceType` for level geometry — what the ground behaves like"*). O que falta
verificar é se `level_model_encoder.py` escreve o byte de volta, e expor o campo na UI — hoje um
autor não tem como marcar uma faixa como grama ou água.

Ideia de menor esforço: como o material é do slot da tabela e não da textura, dá para expor
`surface_type` como propriedade do **material do Blender**, que é exatamente o que já mapeia 1:1
com o batch.

### Divergência de nomes no decomp

`src/particles.c:120` ainda usa nomes antigos que conflitam com `include/enums.h`: chama o índice 11
de `SURFACE_UNK0B` (é `WATER_CALM`), o 14 de `SURFACE_UNK0E` (é `WATER_WAVY`) e o 15 de
`SURFACE_UNK0F` (é `WATER_UNK_F`). Só comentário, sem efeito no código.

---

## Pendências

- Nós isolados de FossilCanyon (1), SherbetIslandHub (5) e OpeningSequence (9) não foram cruzados
  contra os actors de `ANIMATION` dos respectivos níveis.
- A unidade exata dos deslocamentos laterais do checkpoint (`0x0B`–`0x0E`) depende do `scale` do
  portão e não foi medida contra o retail.
- Flags de checkpoint 0, 1 e 3: nenhum leitor no código. Se aparecem nos dados, são inertes.
- `unk19` do checkpoint, copiado para `unk3B` do nó, não tem leitor identificado.
- `AnimDome` e `SpaceDome` não são skybox de nenhum nível; provavelmente são cenário comum, não
  verificado.
- Superfícies `UNK10` (16) e `UNK12` (18): `UNK12` só barra o carro, mas o que ela representa não
  foi identificado (3 texturas no retail). `UNK10` não aparece em nenhuma tabela.
- As tabelas de superfície `D_800DCCCC` (19 × u16) e `gSurfaceFlagTable`/`4P` têm consumidor
  identificado mas semântica não decifrada.
- Cores de `gVehicleTrackMarkColors` assumidas como RGBA na ordem declarada; grama dá 192,8,64
  (magenta), o que sugere que a ordem dos campos do union pode não ser essa.

## Correções pendentes no addon

1. `ai_graph.py:36` — `MAX_NODES` deve ser 128, não 255; e `validate.py:133` precisa da mensagem
   correspondente.
2. `validate.py:206` — não emitir warning de conectividade para nós com `elevation == -1` e sem
   vizinhos; são marcadores inertes, não grafo quebrado.
3. `ai_graph.py` docstring e `docs/BLENDER_ADDON_PLAN.md` — AI node **não** é a linha de corrida.
4. `operators/ai.py:223` — `attach_branch` grava `unk8 = 0` fixo, impedindo branches de serem
   destino.
5. `catalog.json` — renomear os campos de checkpoint `unkB`–`unk16` para os três grupos de 4 por
   faixa da IA.
6. `level_model_encoder.py` — verificar se o byte `surfaceType` (offset `0x07` de cada entrada de 8
   bytes da tabela de texturas) é escrito de volta. Sem ele toda pista customizada é `DEFAULT`:
   sem grama, sem água, sem parede invisível.
7. Expor `surface_type` como propriedade do material do Blender — o material já mapeia 1:1 com o
   batch, que é exatamente a granularidade do campo.
