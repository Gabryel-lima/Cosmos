# Cosmos — Simulação Cosmológica

Uma simulação cosmológica em tempo real em C++17, executada do Big Bang até o
presente. Cada época cosmológica é modelada como um regime físico distinto que
faz a transição suavemente para o seguinte, com renderização ao vivo usando
OpenGL 4.3, Dear ImGui e GLFW.

## Novidades

- Linha do tempo expandida para 7 regimes jogáveis (Idades Escuras e Reionização agora são estágios distintos).
- Transições automáticas entre regimes com cross-fade suave e transferência de estado que preserva a continuidade.
- Caminho de CPU seguro em tempo de execução: base portátil SSE2 com despacho AVX2 para loops críticos quando disponível.
- Loop de atualização da simulação com passo fixo e proteção contra sobrecarga para dinâmica estável em FPS baixo.
- Fluxo de câmera aprimorado: enquadramento automático da cena, recentralização rápida e rastreamento da partícula mais próxima.
- Resolução de caminhos de recursos em tempo de execução ancorada ao diretório do executável, para lançamentos mais confiáveis a partir de diretórios diferentes.
- HUD mais rico: linha do tempo ciente de transições, presets de velocidade, multiplicador de velocidade em escala logarítmica e painéis de composição/desempenho.

## Regimes Simulados

| Época | Física |
|-------|--------|
| Inflação | Expansão exponencial rápida, energia de vácuo |
| Plasma de Quarks e Glúons (QGP) | Desconfinamento de cor + estado de cor QCD simplificado |
| Nucleossíntese do Big Bang (BBN) | Rede de fusão próton/nêutron |
| Plasma Fóton-Bárion | Era dominada por radiação, grade de fluido + dinâmica de recombinação |
| Idades Escuras | Crescimento inicial de matéria após a recombinação |
| Reionização | Primeiras fontes luminosas e aumento da ionização |
| Formação de Estruturas Maduras | Gravidade N-body, evolução de halos/estrelas/buracos negros |

## Requisitos

**Pacotes de sistema** (Ubuntu / Debian):
```bash
sudo apt install build-essential cmake libglfw3-dev libglm-dev libgl-dev git curl unzip
```

**Toolchain**: GCC 13+ (ou Clang 16+), CMake 3.20+ e uma GPU compatível com OpenGL 4.3.

## Compilação

```bash
# 1. Baixe libs/glad e libs/imgui (seguro repetir; ignora se já existirem)
make setup

# 2. Compile
make

# 3. Execute
make run
```

Execução manual (equivalente):

```bash
./build/cosmos
```

### Perfis de qualidade

```bash
make QUALITY=LOW      # rápido — menos partículas, resolução menor
make QUALITY=MEDIUM   # padrão
make QUALITY=HIGH
make QUALITY=ULTRA    # detalhe máximo — exige uma GPU capaz
```

### Build com otimização nativa (opcional)

Se a sua CPU suportar AVX2 e você quiser que a auto-vetorização do compilador
explore isso, passe `NATIVE_OPT=ON` diretamente ao CMake:

```bash
cd build && cmake .. -DNATIVE_OPT=ON
```

> **Aviso**: um binário compilado com `-march=native` em uma máquina com AVX2
> vai gerar **SIGILL** em CPUs sem AVX (por exemplo, Intel Celeron / Pentium e
> muitas VMs). O build padrão usa a base portátil SSE2 e despacho AVX2 em tempo
> de execução quando disponível.

## Argumentos de Execução

```bash
./build/cosmos [--fullscreen|-f] [--width W] [--height H] [--seed N]
```

- `--fullscreen`, `-f`: inicia em tela cheia.
- `--width`, `--height`: sobrescrevem o tamanho inicial da janela.
- `--seed`: define uma semente determinística para a simulação.

## Controles

| Entrada | Ação |
|---------|------|
| `Space` | Pausar / retomar simulação |
| `.` | Avançar um passo fixo da simulação |
| `1`..`7` | Ir diretamente para um regime |
| `[` ou `,` | Diminuir a velocidade do relógio |
| `]` ou `;` | Aumentar a velocidade do relógio |
| `Tab` | Alternar modo da câmera |
| `T` | Alternar rastreamento da partícula mais próxima |
| `C` | Recentrar a câmera para a extensão atual da cena |
| `H` | Alternar visibilidade do HUD |
| `R` | Recarregar shaders |
| `F` | Alternar tela cheia |
| `Esc` | Soltar o rastreamento, ou sair se não estiver rastreando |
| `Ctrl+Q` | Sair |
| `W/A/S/D/Q/E` | Movimento livre da câmera |
| Arrastar com o botão esquerdo | Orbitar a câmera |
| Roda do mouse | Zoom |
| Painel ImGui | Linha do tempo + controles de salto, presets de velocidade, estatísticas de física/composição/desempenho |

Observação: os atalhos com pontuação seguem o caractere digitado, então `,`, `.`, `;`, `[` e `]` funcionam corretamente mesmo em layouts não-US, como pt-BR.

## Destaques de Física e Renderização

- Base cosmológica: resolvedor de Friedmann (`Lambda`CDM, integração RK4).
- O regime de QGP inclui marcação/tintura simplificada de cor QCD para quarks e glúons.
- A rede de BBN rastreia abundâncias de `n`, `p`, `D`, `He3`, `He4` e `Li7`.
- O regime de plasma evolui o fluido bariônico em uma grade 3D com solução de Poisson e comportamento de recombinação.
- Os regimes de estrutura usam gravidade N-body com Barnes-Hut, além de lógica por fase para halos.
- O renderizador inclui pipeline HDR, passes de bloom, renderização volumétrica e blend de transição entre regimes.

## Estrutura do Projeto

```text
Cosmos/
├── src/
│   ├── core/        — CosmicClock, RegimeManager, estado do Universo, Camera
│   ├── physics/     — resolvedor de Friedmann, N-body, FluidGrid, NuclearNetwork
│   ├── regimes/     — lógica por época (Inflação, QGP, BBN, Plasma, Idades Escuras, Reionização, Estrutura)
│   ├── render/      — Renderer, ParticleRenderer, VolumeRenderer, PostProcess
│   └── shaders/     — shaders GLSL vertex / fragment para partículas, volume e pós-processamento
├── libs/
│   ├── glad/        — loader OpenGL 4.3 (gerado por `make setup`)
│   └── imgui/       — branch docking do Dear ImGui (clonada por `make setup`)
├── assets/          — fontes e texturas
├── CMakeLists.txt
└── Makefile
```

## Início Rápido

Siga estes passos para executar o projeto localmente:

```bash
# 1. Baixe libs/glad e libs/imgui (seguro repetir; ignora se já existirem)
make setup

# 2. Compile
make

# 3. Execute
make run
```

Execução manual (equivalente):

```bash
./build/cosmos
```

## Exemplos de execução

```bash
./build/cosmos --width 1280 --height 720 --seed 42
./build/cosmos --fullscreen
```

## Solução de Problemas

- Aplicação trava ao iniciar / janela em branco: verifique drivers da GPU e suporte a OpenGL 4.3. No Debian/Ubuntu, confirme que `libgl1-mesa-dri` e `libglfw3` estão instalados.
- FPS baixo / interface travando: tente `make QUALITY=LOW` para reduzir contagem de partículas e resolução da grade.
- SIGILL (instrução ilegal) após compilar com otimizações nativas: provavelmente você compilou com `-DNATIVE_OPT=ON`. Recompile sem otimizações nativas:

```bash
cd build && cmake .. -DNATIVE_OPT=OFF
make clean && make
```

- Erros de compilação de shader: pressione `R` durante a execução para recarregar os shaders e verifique as mensagens no console/log.

Se o problema persistir, abra uma issue com descrição curta e informações sobre GPU/driver.

## Contribuindo

Contribuições são bem-vindas — faça um fork do repositório, crie um branch de tópico, e envie um pull request com uma descrição clara das alterações. Um bom PR inclui:

- um resumo curto da mudança
- como reproduzir ou testar localmente
- capturas de tela ou GIFs para mudanças visuais

Execute `make setup` e `make` antes de abrir um PR para garantir que libs externas estejam presentes e o projeto compile localmente.

## Créditos e Agradecimentos

- Dear ImGui — GUI imediata usada no HUD e nas ferramentas internas.
- GLAD — carregador de funções OpenGL.
- GLFW — gerenciamento de janelas e input.
- stb e headers utilitários usados pelo projeto.

Se você usar ativos ou fontes com licenças próprias, respeite essas licenças ao redistribuir builds.

## Contato / Issues

Por favor, registre bugs ou pedidos de funcionalidade na página de Issues do repositório. Inclua passos de reprodução e detalhes do ambiente (SO, GPU, versão do driver) para acelerar o diagnóstico.

## Licença

Este projeto é licenciado sob a licença MIT. Veja [LICENSE](LICENSE) para detalhes.
