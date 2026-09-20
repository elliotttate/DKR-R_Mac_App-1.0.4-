# Plano: cachoeira no addon

## O desejo do usuário

> E se eu quiser aplicar o material de cachoeira? Porque ele tem um certo
> movimento.

No DKR, cachoeira não é um material nem uma textura animada. É uma textura
**parada** que um objeto de fase, o **TexScroll**, faz deslizar. Hoje o addon
já consegue colocar esse objeto, mas o campo que diz qual textura ele move é um
número cru, sem ligação com a textura. O pedido é um fluxo de um clique:
selecionar as faces, escolher a cachoeira e ver a água descendo no jogo.

Estado: **implementação no addon** (2026-09-17). O fluxo Add Waterfall, os
vínculos de textura, a interface, importação/exportação e validação estão
implementados. A prévia animada no viewport (§5) continua opcional e não foi
incluída. Os testes manuais no jogo (§7) continuam pendentes.

Decisões adotadas: entrada dedicada sempre; seção Waterfalls dentro de Water;
velocidade padrão 95 (44,53 texels/s); Remove apaga apenas o movimento e mantém
faces, textura e colisão. Texturas próprias e presets usam o mesmo operador.
Uma referência ambígua é recusada em vez de escolher outra cachoeira com imagem
idêntica. A exportação preserva as entradas duplicadas e resolve o índice na
cópia exportada, sem reescrever os bytes crus do objeto na cena.

Testes específicos: `tests/test_texture_scroll.py` e
`tests/test_blender_waterfalls.py`, ambos incluídos em `run_tests.py`.
Verificação: as 23 suítes de `python tools/blender/run_tests.py --all` passaram,
incluindo a rodada completa de mapas no Blender 5.2 e o encoder byte a byte.
O teste do loop percorre os 27 TexScroll dos mapas retail e simula 4.096 passos
nas 559 faces das entradas efetivamente apontadas pelos presets (a contagem
de 687 citada na investigação abaixo não é a contagem desse filtro).

A investigação e a proposta original abaixo foram baseadas no decomp
(`extern/dkr-decomp/src`) e nos modelos e mapas de objetos retail extraídos
(`extern/dkr-decomp/assets/.vanilla/us.v77`).

---

## O que o jogo faz

### Cachoeira = textura parada + TexScroll

Os mapas retail têm 27 objetos `ASSET_OBJECT_TEXSCROLL`. Cruzando o
`textureIndex` de cada um com a tabela de texturas do modelo da fase:

| Fase | Índice | U | V | Textura |
|---|---|---|---|---|
| Boulder Canyon | 8 / 21 | 0 / 0 | 127 / 85 | `WATERFALL3` / `WATERFALL2` |
| Central Area | 26 / 29 | 0 / 0 | 127 / 47 | `WATERFALL` / `WATERFALL2` |
| Fossil Canyon | 4 | 0 | 127 | `WATERFALL` |
| Frosty Village | 21 | 0 | 127 | `WATERFALL` |
| Jungle Falls | 24 | 0 | 32 | `WATERFALL` |
| Last Bit (A/B) | 25 | 0 | 94 / 95 | `WATERFALL` |
| Snowball Valley | 27 | 0 | 107 | `WINTER_ICYWATERFALL` |
| Treasure Caves | 3 | 0 | 126 | `WATERFALL3` |
| Whale Bay | 7 | 0 | 71 | `WATERFALL` |
| Windmill Plains | 4 | 0 | 89 | `WATERFALL` |
| Hot Top Volcano | 5 | 0 | 40 | `DINO_MAGMAFALL` |
| Haunted Woods | 59 | 0 | -58 | `MEDIEVAL_WATERFOUNTAIN` |

Os outros usos são água corrente (Everfrost Peak), sol, névoa, lava, luzes,
eletricidade e o ventilador do Spaceport Alpha. Toda cachoeira rola só em V, com
velocidade entre 32 e 127 (mediana 95).

As texturas de cachoeira do ROM têm **1 quadro**: `waterfall`, `waterfall2`,
`waterfall3`, `icy_waterfall` e `magma_fall` são 32x32 e `water_fountain` é
16x32, todas RGBA com alfa. As de água são `TRANSPARENT`, com `wrap-s: Clamp` e
`wrap-t: Wrap`. Portanto só o eixo V se repete. A animação por quadros
(`RENDER_TEX_ANIM`, `track_tex_anim`, `tracks.c:1288`) é outro mecanismo, que as
cachoeiras não usam.

### Como o TexScroll funciona

`obj_init_texscroll` (`object_functions.c:5499`):

- `textureIndex` é a **posição na tabela de texturas do modelo da fase**, não o
  id da textura no ROM. Um valor fora da tabela é **limitado** à última
  entrada, então rola outra textura sem aviso.
- `unkA` é a velocidade em U e `unkB` a velocidade em V (`s8`).

`obj_loop_texscroll` (`object_functions.c:5522`), a cada atualização:

- acumula `velocidade × updateRate` com 2 bits de fração e desloca
  `acumulado >> 2` unidades de UV;
- percorre **todos os segmentos** e todo batch cujo `textureIndex` é o do
  objeto, e soma o deslocamento às três UVs de cada triângulo;
- **pula triângulos com `TRI_FLAG_80`** (`0x80`), que é o bit "sem colisão"
  por triângulo (`level_model_layout.TRI_FLAG_NO_COLLISION`);
- faz o *wrap* olhando só `uv0`: acima de `largura << 8` (U) ou
  `altura << 8` (V), subtrai esse valor; abaixo de 0, soma. Com 1 texel = 32
  unidades (`textures.TEXEL`), isso é um salto de **8 repetições** da
  textura, invisível numa textura que se repete.

Consequências:

1. **Unidade de velocidade:** V/4 unidades por tick de 60 Hz, ou seja,
   `V × 15 / 32` ≈ **0,47 × V texels/s**. `V = 95` dá ≈ 45 texels/s, ou 1,4
   repetição por segundo numa textura de 32. O número foi derivado do código e
   precisa ser conferido no jogo.
2. **Escopo global:** toda face que usa aquela entrada da tabela rola, em
   qualquer lugar da pista. A entrada tem que ser **exclusiva** da cachoeira.
3. **A posição do objeto não importa:** o retail o coloca a 47–750 unidades da
   cachoeira, mas o sol de Tricky Topps é rolado de 12.536 unidades.
4. **Dois TexScroll na mesma entrada somam as velocidades.**
5. **Estouro de UV:** `uv0` fica em `[0, vão]`, mas `uv1` e `uv2` recebem o
   mesmo deslocamento. Para cada face rolada vale
   `vão + passo + extensão da face ≤ 32767`, com `vão = altura × 256`. Numa
   textura de 32 texels, o vão é 8192 e a face pode ter no máximo ≈ 24500
   unidades de extensão em V (≈ 24 repetições).
6. **Colisão:** nenhum dos 687 triângulos de cachoeira retail tem o bit
   `0x80`. A passagem sem colisão vem do batch (`RENDER_NO_COLLISION`, bit 9),
   presente em quase todos. 440 triângulos desenham os dois lados (`0x40`). Os
   batches ficam na segunda passada, como toda textura transparente.
7. **Mapas e modos:** no retail o TexScroll aparece 23 vezes no mapa
   *structure* e 4 no *collectables*; em modos, 17 corridas, 4 hubs, 4 chefes e
   2 cutscenes.

### O que já existe no addon

- **Objeto:** `ASSET_OBJECT_TEXSCROLL` está no catálogo
  (`data/catalog.json`, categoria *effects*) e sai pelo *Place*. Os campos são
  crus: `textureIndex` (padrão **5**), `unkA` (padrão 0) e `unkB` (padrão 127).
- **Texturas:** o navegador aplica qualquer textura do ROM ou da pista.
  `operators/textures.py:allocate` (linha 216) acrescenta a entrada à tabela
  (base + adicionadas, só por anexação). `apply_texture` (326) grava índice,
  flags, lado da passada e UVs; os mapeamentos disponíveis são *Keep* e
  *Project*.
- **Modelo a seguir:** o operador de água (`operators/water.py`) aplica textura
  retail e flags num só passo.
- **Ordem da exportação:** o `.dkrmap` codifica os mapas de objetos
  (`operators/pack.py:112`) **antes** do modelo (156). A tabela final, porém,
  já é conhecida pelos registros da malha (`geometry.texture_table`), na mesma
  ordem que o modelo recebe (`geometry_export` confere isso).
- **Objetos no Blender:** cada objeto é um Empty com uma propriedade por campo
  (`scene.create_empty`, 138; `scene.read_object`, 329).

### Lacunas

1. `textureIndex` é um número sem ligação com a textura, e o padrão 5 rola o
   que estiver na posição 5 da pista.
2. Nada garante que a entrada rolada seja exclusiva da cachoeira.
3. Não existe mapeamento de UV "ao longo da queda", nem checagem de estouro
   para faces roladas.
4. Faces vindas de geometria importada podem trazer o bit `0x80` e ficar
   paradas sem explicação.
5. O painel não mostra qual textura um TexScroll move, nem a velocidade em
   unidades legíveis.
6. Nada se move no viewport.

---

## Proposta

### 1. Regras sem `bpy`: `dkr_track_editor/texture_scroll.py`

No mesmo espírito de `water.py` e `transparency.py`:

- **Constantes:** `OBJECT_ID`, `WRAP_REPEATS = 8`, `SUBSTEPS = 4`,
  `TICKS_PER_SECOND = 60`, `MAX_WRAP_SIZE` (reusar `textures.py`).
- **`PRESETS`:** as texturas de cachoeira do ROM (asset id, eixo e velocidade
  padrão). Padrão V = 95, a mediana retail.
- **Conversões:** `texels_per_second(speed, texel_size)` e a inversa, limitada
  a `s8`.
- **`resolve_index(table, reference)`:** devolve a posição atual da entrada
  referenciada ou um erro legível (ver §2).
- **`fall_mapping(corners, texture, repeats)`:** UVs cruas com V descendo pela
  queda (maior declive no espaço do mapa) e U na horizontal. O deslocamento é um
  número inteiro de repetições, como `_projected` já faz.
- **`uv_problems(faces, texture, axis, speed)`:** aplica a regra 5 acima.
- **`problems(...)`:** a lista usada pela validação (§6).

### 2. Referência de textura no objeto

- **Referência:** nova propriedade no Empty do TexScroll, `dkr_scroll_entry`
  (JSON: `index`, `id`, `w`, `h`, `format`, `surface`), com o índice e a
  assinatura da entrada. O campo cru `textureIndex` continua sendo o que o
  encoder lê, para que um mapa importado e não tocado saia byte a byte igual
  (`tests/test_encoder.py`).
- **Exportação:** antes de codificar os mapas, cada TexScroll com referência é
  resolvido contra `geometry.texture_table` e grava `textureIndex`.
  - Se a assinatura no índice mudou, procurar a entrada equivalente reservada.
  - Se não houver nenhuma, **abortar** com a mensagem "o TexScroll *X* move uma
    textura que a pista não tem mais".
  - Sem geometria na cena (remix só de objetos), manter o valor cru.
- **Importação de fase e mapa:** quando a geometria tem
  `PROP_BASE_TEXTURES`, preencher a referência a partir de
  `tabela[textureIndex]`, assim remixes retail já mostram a textura certa.
- **Exclusividade:**
  - Uma entrada referenciada por algum TexScroll da cena é **reservada**.
  - `allocate()` ganha um parâmetro para ignorar entradas reservadas no *Apply
    Texture* comum, e o operador de cachoeira cria uma entrada nova mesmo que
    exista outra idêntica. Duas entradas com o mesmo id são legais (o jogo
    carrega a imagem uma vez), e o filtro do TexScroll é pela posição na
    tabela.
  - A reserva é derivada dos objetos da cena, não de uma flag no registro,
    porque `record_texture_table` reconstrói a base a partir do modelo em
    *Re-segment* e *Add Water* e perderia essa flag.

### 3. Operador *Add Waterfall* (`operators/waterfall.py`)

`dkr.add_waterfall`, com `poll` igual ao de água: geometria presente e faces
selecionadas.

**Opções:**
- textura: um dos presets, ou "a escolhida no painel Textures", aceitando
  textura própria;
- velocidade em texels/s (grava V);
- sentido (descer ou subir);
- repetições (escala);
- visual: *Blend* (padrão) ou *Cut-out*;
- passável (padrão ligado);
- desenhar os dois lados (padrão ligado, como 440 dos 687 triângulos retail).

**Execução:**
1. Criar uma entrada dedicada (§2).
2. Aplicar a textura com o mapeamento `FALL` (§1) e recusar se `uv_problems`
   acusar estouro.
3. Nas faces: limpar `0x80` em `ATTR_TRI_FLAGS`, ligar `RENDER_NO_COLLISION` no
   batch se passável, e `0x40` se dois lados. O lado da passada continua
   derivado pela transparência, como hoje.
4. Criar o TexScroll com `scene.create_empty`: no centro das faces, mapa
   *structure*, `unkA = 0`, `unkB = ±V`, com a referência preenchida e nome
   "Waterfall".
5. Relatar a textura, as faces, a velocidade e o índice.

**Operadores irmãos:** *Select Waterfall* (faces de uma entrada reservada) e
*Remove Waterfall* (apaga o TexScroll e, opcionalmente, devolve as faces à
textura anterior).

**Textura própria:** permitida se o lado no eixo rolado tiver no máximo 64
texels (`MAX_WRAP_SIZE`). As texturas próprias já são gravadas repetindo nos
dois eixos. A translucidez delas depende do alfa da imagem ou do alfa por
vértice, que é um plano separado.

### 4. Painel

- **Painel *DKR Object*, para um TexScroll:**
  - no lugar da linha crua de `textureIndex`: miniatura, nome, posição na
    tabela (só leitura) e o botão *Pick From Active Face*, que preenche a
    referência a partir da face ativa em Edit Mode;
  - velocidade em texels/s e sentido;
  - o valor cru só em *Show Raw Bytes*.
- **Painel *Water*:** nova seção *Waterfalls*, com *Add*, *Select* e *Remove* e
  a lista dos TexScroll da cena (textura e velocidade de cada um), com alerta
  para os que não têm referência.

### 5. Prévia no viewport (opcional)

No material do slot da entrada rolada, um nó *Mapping* com a posição em V
guiada por um driver (`frame × velocidade / fps / altura`), para que tocar a
timeline mostre a água correndo. É só prévia; a exportação nunca lê esse nó.

### 6. Validação (`validate.py` / `operators/checks.py`)

**Erros:**
- referência que não resolve;
- `textureIndex` cru fora da tabela (o jogo limita e rola outra textura);
- estouro de UV numa face rolada;
- face rolada com `TRI_FLAG_80`, que fica parada.

**Avisos:**
- TexScroll sem referência apontando para uma entrada que nenhuma face usa;
- entrada rolada compartilhada com faces que não são cachoeira (só em dados
  importados ou crus);
- dois TexScroll na mesma entrada (as velocidades somam);
- rolagem num eixo em *Clamp* (U das cachoeiras do ROM) ou maior que 64
  texels;
- velocidade zero nos dois eixos.

### 7. Runtime (DKR-R)

Nenhuma mudança esperada. O runtime serve o modelo e os mapas de objetos, e
`obj_loop_texscroll` lê a textura do modelo carregado, o que vale também para
as texturas próprias de um `.dkrmap`.

A conferir no jogo:
1. Um TexScroll sobre textura própria no Track Lab.
2. A interpolação do perfil Modern no salto de 8 repetições, comparando com uma
   cachoeira retail (Whale Bay) em Modern e em Accurate.
3. O pacote HD de uma textura rolada, já que o RT64 identifica pela textura e
   não pelas UVs.
4. L+Z e troca de pista.

---

## Testes

**`tests/test_texture_scroll.py` (sem Blender):**
- os 27 TexScroll retail resolvidos para as texturas da tabela acima;
- ida e volta da conversão de velocidade;
- `fall_mapping` sempre com V descendo;
- `obj_loop_texscroll` portado para Python e rodado milhares de passos sobre as
  cachoeiras retail, sem estouro, o que confere a regra 5;
- referência que muda de índice, assinatura divergente e entrada ausente.

**`tests/test_blender_operators.py`:**
- *Add Waterfall* numa pista gerada: entrada dedicada, faces apontando para ela,
  `0x80` limpo, TexScroll com referência;
- a exportação resolve o índice;
- *Re-segment* e *Add Water* preservam a referência;
- *Apply Texture* comum não reaproveita a entrada reservada;
- importar uma fase retail preenche as referências.

**Regressões:** `tests/test_encoder.py`, com os 136 mapas retail byte a byte, e
a suíte completa (`python tools/blender/run_tests.py`).

**Validação manual no jogo:**
1. `test01` com `waterfall` do ROM: desce na velocidade esperada, translúcida e
   passável.
2. A mesma cachoeira com textura própria.
3. L+Z e troca de pista no Track Lab.
4. Perfis Modern e Accurate.

## Fora do escopo

- **Alfa por vértice** a partir do Alpha do material, para cachoeiras próprias
  translúcidas sem editar a imagem. É um plano separado.
- **Texturas próprias com vários quadros (flipbook).** Não verificado se o
  importador de texturas próprias aceita mais de um quadro.
- **Espuma, respingos e som** da cachoeira. Não foi investigado se o retail usa
  objetos para isso.

## Decisões adotadas nesta entrega

1. **Velocidade padrão:** 95, a mediana das cachoeiras retail (≈ 45 texels/s).
2. **Entrada dedicada sempre**, inclusive quando já existe uma imagem igual.
3. **Lugar da interface:** seção *Waterfalls* no painel *Water*, com os
   controles do objeto também em *DKR Object*.
4. **Prévia animada (§5):** depois. Nesta entrega, o movimento é exportado
   para o jogo; o viewport mostra a textura parada.
