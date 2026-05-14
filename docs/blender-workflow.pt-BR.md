# Workflow de Bake no Blender

Este projeto atualmente usa shaders GLSL em `src/shaders/`. Esses arquivos nao sao texturas de imagem; eles sao programas de GPU executados pelo renderizador OpenGL.

Ainda assim, o Blender e muito util aqui, mas em outro papel: autoria de assets. O fluxo mais produtivo e montar looks procedurais no Blender, bakear isso em mapas de imagem e depois consumir esses mapas nos shaders de runtime quando uma textura precomputada for mais barata ou mais facil de dirigir artisticamente.

## Conceito: baking

Baking de textura e o processo de avaliar um material, termo de iluminacao, mascara ou grafo procedural antes do tempo de execucao e gravar o resultado em uma imagem.

Por que isso importa aqui:

- os shaders de runtime ficam mais simples
- ruido repetido e mascaras podem sair do custo por frame
- iterar no visual fica mais facil porque o editor de nodes do Blender e mais rapido de dirigir artisticamente do que GLSL cru

Modelo mental:

1. O Blender calcula um look procedural uma vez.
2. O resultado e salvo em um arquivo de textura.
3. O renderizador em C++ amostra esse arquivo a cada frame em vez de reconstruir o mesmo padrao do zero.

O que baking nao e:

- nao converte automaticamente GLSL em material do Blender
- nao remove a necessidade de integrar a textura exportada no shader de runtime

## Setup do workspace

O repositorio inclui configuracoes de workspace em `.vscode/settings.json` que:

- abrem `src/shaders/blender/cosmos.blend` por padrao quando o Blender e iniciado pelo VS Code
- isolam o estado da extensao em `${workspaceFolder}/.blender-vscode`
- ativam logs de debug no painel `Blender`
- documentam o bloco `blender.executables` com exemplos de Linux, incluindo instalacao via Steam

O workspace tambem recomenda automaticamente a extensao do VS Code em `.vscode/extensions.json`, mas voce ainda precisa instala-la no editor para usar os comandos `Blender:*`.

`make setup` tambem verifica se existe um runtime `bpy` utilizavel para automacao. O alvo primeiro aceita um `python3` do host que ja importe `bpy`, depois tenta um executavel Blender detectado e, por fim, baixa um pacote portatil do Blender para `.blender-tools/blender/` se nenhum deles estiver disponivel. Se a autodeteccao pegar a instalacao errada, rode `BLENDER_BIN=/caminho/absoluto/para/blender make setup` ou sobrescreva a origem do arquivo com `BLENDER_DOWNLOAD_URL`.

Extensao recomendada:

- `JacquesLucke.blender-development`

Uma configuracao dependente da sua maquina deve ficar nas configuracoes de usuario, nao no repositorio: o caminho do executavel do Blender.

Abra o JSON de configuracoes do VS Code e adicione algo assim:

```jsonc
{
  "blender.executables": [
    {
      "path": "/caminho/absoluto/para/o/blender",
      "isDefault": true,
      "platform": "linux"
    }
  ]
}
```

Exemplos comuns no Linux:

- `/usr/bin/blender`
- `/snap/bin/blender`
- `/home/<usuario>/snap/steam/common/.local/share/Steam/steamapps/common/Blender/blender`
- `/home/<usuario>/blender-4.x/blender`
- `/home/<usuario>/.local/share/Steam/steamapps/common/Blender/blender`
- `/home/<usuario>/.steam/steam/steamapps/common/Blender/blender`

Se o Blender tiver sido instalado pela Steam e nao existir `blender` no `PATH`, normalmente voce deve apontar `blender.executables[].path` para o binario dentro de `steamapps/common/Blender/blender`.

Se a sua Steam foi instalada via Snap, o caso mais provavel e este:

- `/home/<usuario>/snap/steam/common/.local/share/Steam/steamapps/common/Blender/blender`

## Primeiro uso com a extensao do Jacques Lucke

1. Instale a extensao recomendada do VS Code: `JacquesLucke.blender-development`.
2. Abra a raiz do workspace no VS Code.
3. Rode `Blender: Start` pela command palette.
4. Selecione o executavel do Blender na primeira vez.
5. Confirme que o Blender abriu `src/shaders/blender/cosmos.blend`.
6. Se a abertura falhar, verifique o painel de saida `Blender`.

## Layout sugerido para assets

Use `assets/textures/` para mapas gerados.

Padrao de nomes recomendado:

- `assets/textures/<regime>/<nome_do_asset>_albedo.png`
- `assets/textures/<regime>/<nome_do_asset>_emission.exr`
- `assets/textures/<regime>/<nome_do_asset>_mask.png`
- `assets/textures/<regime>/<nome_do_asset>_density.exr`

Convencoes sugeridas:

- use `png` para mascaras e mapas de baixa faixa dinamica
- use `exr` para emissive, density ou dados HDR
- mantenha os arquivos `.blend` de origem em `src/shaders/blender/`
- trate as texturas baked como assets de runtime dentro de `assets/textures/`

## Workflow basico de bake no Blender

### 1. Criar ou refinar o material de origem

No Shader Editor do Blender:

- monte um grafo procedural para ruido, filamentos, glow, densidade ou mascaras
- mantenha os parametros agrupados e nomeados com clareza
- prefira fontes estaveis de coordenadas como `UV`, `Object` ou `Generated`

### 2. Preparar uma imagem alvo para o bake

No Shader Editor:

1. Adicione um node `Image Texture`.
2. Crie uma nova imagem com a resolucao desejada.
3. Dê um nome descritivo como `filament_mask_2048`.
4. Deixe esse node `Image Texture` selecionado antes do bake.

Resolucao inicial recomendada:

- `1024` para mascaras, mapas de breakup e ruido secundario
- `2048` para mapas principais
- `4096` so quando detalhe de close justificar

### 3. Escolher o tipo de bake certo

Use o tipo de bake que corresponde aos dados que voce quer preservar:

- `Emit` para padroes emissivos e light maps puramente procedurais
- `Diffuse` quando voce quer a cor base sem complexidade de iluminacao
- `Combined` apenas quando voce quer deliberadamente o resultado final iluminado

Para mapas de dados como mascaras e campos escalares, `Emit` costuma ser a melhor opcao porque evita contaminacao por iluminacao.

### 4. Bakear e salvar

1. Troque para `Cycles` se a opcao de bake desejada nao estiver disponivel no render atual.
2. Abra o painel `Render Properties`.
3. Use `Bake` com o node de imagem selecionado como alvo.
4. No Image Editor, salve a imagem gerada em `assets/textures/...`.

## Regras de otimizacao

Comece por estas regras antes de exportar muitos mapas:

1. Bakeie detalhe procedural que nao precisa mudar a cada frame.
2. Mantenha em GLSL os efeitos realmente dinamicos.
3. Empacote varios mapas em tons de cinza nos canais RGB quando o shader puder desempacotar isso.
4. Prefira uma boa textura lookup compartilhada a muitas texturas pequenas de uso unico.
5. Reaproveite uma textura entre regimes quando a linguagem visual for compativel.

Divisao tipica de responsabilidades:

- mantenha em GLSL flicker temporal, efeitos dependentes de camera e pos-processamento
- mova para textura baked mascaras de densidade estaveis, ruido de breakup e gradientes dirigidos artisticamente

## Workflow com scripts Blender

A extensao fica especialmente util quando voce automatiza tarefas repetitivas de bake/export com scripts Python.

Casos de uso tipicos:

- batch bake de varios materiais
- exportacao de varias resolucoes em uma passada
- regeneracao de mascaras apos mudar parametros
- padronizacao automatica de nomes de arquivo e diretorios de saida

Local sugerido para scripts:

- `tools/blender/` para scripts standalone

Depois de criar um script:

1. Rode `Blender: Run Script` no VS Code.
2. Inspecione o painel `Blender`.
3. Itere ate o bake/export virar um comando so.

## Primeiro exemplo implementado neste repositorio

O primeiro exemplo ponta a ponta agora mira o shader volumetrico dos regimes tardios.

- Script Blender: `tools/blender/generate_volume_macro_lookup.py`
- Asset de runtime: `assets/textures/structure/volume_macro_lookup.pgm`
- Shader consumidor no runtime: `src/shaders/volume.frag`

O script agora tambem gera dois lookups 2D adicionais para billboards de pontos:

- `assets/textures/dark_ages/gas_splat_profile.pgm` usado por `src/shaders/gas_splat.frag`
- `assets/textures/reionization/star_glow_profile.pgm` usado por `src/shaders/star_glow.frag`

O que esse exemplo faz:

- gera um mapa grayscale 2D de macro breakup via Python dentro do Blender
- gera perfis grayscale 2D baked para splats de gas e glow estelar
- salva esse mapa em `assets/textures/structure/`
- recarrega o asset dentro do renderer OpenGL quando o app inicia ou quando voce aperta `R`
- usa esse lookup para modular breakup volumetrico e shaping de filamentos nos regimes tardios

Por que esse foi o primeiro alvo:

- e um caso de baixo risco porque `volume.frag` ja trabalha com samplers
- o mapa baked e opcional e tem fallback seguro se o arquivo nao existir
- ele demonstra o pipeline inteiro antes de partir para bakes de material mais ambiciosos

Para regenerar o lookup:

1. Inicie o Blender pelo VS Code.
2. Abra `tools/blender/generate_volume_macro_lookup.py`.
3. Rode `Blender: Run Script`.
4. Volte para o app em execucao e aperte `R` para recarregar shaders e o lookup baked.

Para validacao headless fora da extensao do VS Code, `make setup` ja basta para verificar uma fonte utilizavel de `bpy`. Em maquinas onde o Blender nao estiver instalado, o projeto baixa um pacote portatil e usa o `bpy` que vem junto dele.

## Checklist de integracao com shaders de runtime

Antes de trocar logica procedural GLSL por textura baked, confirme:

1. a textura usa um espaco de coordenadas estavel
2. a resolucao do asset faz sentido para a distancia de camera
3. o sampler e os uniforms ja estao preparados no lado C++
4. o resultado visual continua funcionando sob tonemap e bloom do projeto

## Erros comuns

- Bakear um resultado iluminado quando o shader de runtime espera dados crus.
- Exportar tudo em 4K e pagar custo de memoria sem necessidade.
- Usar convencoes de nome diferentes para arquivos do mesmo regime.
- Esquecer que Blender e shader de runtime podem interpretar espaco de cor de forma diferente.
- Tentar usar o Blender como editor direto de GLSL em vez de ferramenta offline de assets.

## Onde mais isso aparece

O mesmo conceito aparece em:

- bake de lightmaps em engines de jogo
- bake de normal/AO em pipelines de assets
- geracao de LUTs e signed-distance fields para render em tempo real
