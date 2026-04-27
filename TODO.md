# TODO — Plano de Otimização Adaptativa: CPU / GPU / Híbrido

**Objetivo**
- Tornar o Cosmos escalável para hardware fraco, médio e forte sem depender de um único perfil-alvo.
- Manter interatividade com degradação controlada: a engine deve reduzir custo e preservar estabilidade antes de perder FPS de forma caótica.
- Separar claramente otimização de `simulação`, `renderização` e `transferência CPU↔GPU`, porque os gargalos mudam conforme o hardware.

**Princípio central**
- Em vez de otimizar primeiro para um PC específico, a engine deve escolher um `perfil de capacidade` em runtime e ligar/desligar recursos com base nisso.
- O alvo correto não é "10k partículas no Ryzen 3600 + RX 5700"; isso deve virar apenas um perfil de referência para benchmark.

**Princípio de implementação**
- Reutilizar primeiro o que já existe e já funciona antes de criar novas camadas, nomes ou subsistemas.
- Evitar renomear conceitos já estáveis no código sem ganho técnico claro.
- Se uma melhoria puder ser feita estendendo `Renderer`, `NBody`, `ThreadPool`, `CpuFeatures` e a configuração já existente, este é o caminho preferencial.
- Novas abstrações só devem entrar quando resolverem um problema real de acoplamento, fallback ou manutenção; não apenas para "organizar" o plano no papel.

**Situação atual do código**
- A física é CPU-first: `NBody` calcula forças no CPU e só depois o renderer envia dados para a GPU.
- A renderização de partículas já depende de OpenGL moderno com SSBO em [src/render/Renderer.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.cpp).
- Hoje não existe uma política explícita de fallback por capacidade nem uma separação formal entre backends de renderização.

**Meta revisada**
- Definir `tiers` de execução e fazer a engine se adaptar ao hardware disponível.
- Permitir três modos de trabalho:
   - `CPU-first`: física no CPU, preparação no CPU, GPU apenas para desenhar.
   - `Hybrid`: física principal no CPU com culling, compactação, LOD e parte do processamento visual na GPU.
   - `GPU-heavy`: dados persistentes na GPU, mínimo tráfego CPU↔GPU, compute shader quando houver suporte real.

**Ponto importante sobre renderização "CPU-only"**
- Para uso interativo, o melhor caminho continua sendo `CPU ou híbrido na simulação` e `GPU para apresentação`.
- Um renderer gráfico totalmente em CPU só vale como modo de compatibilidade extrema, debug ou headless com captura offline; como meta principal, ele tende a piorar portabilidade e custo de manutenção.
- Portanto, a versatilidade ideal é:
   - `simulação no CPU ou GPU`
   - `preparação visual no CPU ou GPU`
   - `apresentação preferencialmente na GPU`

**Arquitetura recomendada**

**1. Capability detection primeiro**
- Criar um bloco de capacidades de runtime:
   - versão OpenGL suportada
   - suporte a SSBO
   - suporte a compute shader
   - tamanho máximo de buffer
   - VRAM estimada ou limites úteis
   - número de threads de CPU
   - presença de AVX2/FMA
- Resultado: escolher automaticamente um perfil como `low`, `balanced`, `high`.
- Arquivos-alvo iniciais: [src/main.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/main.cpp), [src/render/Renderer.hpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.hpp), [src/render/Renderer.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.cpp).

**2. Separar backend lógico de renderização**
- O plano melhora muito se o renderer deixar de ser um caminho único.
- Proposta de backends:
   - `RenderPath::Legacy`: VBO/instancing simples, sem compute, sem assumir pipeline pesada.
   - `RenderPath::SSBO`: caminho atual otimizado com SSBO e uploads mais eficientes.
   - `RenderPath::Compute`: compute shader opcional para etapas visuais ou físicas específicas.
- Mesmo que a implementação inicial use uma única classe, o plano deve prever uma fronteira clara entre `decidir o backend` e `executar o backend`.
- Esses nomes devem ser tratados como rótulos de estratégia do plano, não como obrigação de criar classes novas ou renomear APIs existentes.
- Se o comportamento puder continuar centralizado em `Renderer` com flags, enums e helpers locais, isso é preferível a multiplicar tipos sem necessidade.

**3. Separar modo de simulação do modo de render**
- Não acoplar "física na GPU" com "render na GPU" como se fosse o mesmo problema.
- Combinações desejáveis:
   - `CPU sim + Legacy render`
   - `CPU sim + SSBO render`
   - `CPU sim + Compute-assisted render`
   - `GPU sim + SSBO render`
   - `GPU sim + Compute render`
- Isso permite escalar para qualquer hardware sem reescrever todo o pipeline de uma vez.

**Perfis de capacidade sugeridos**

**Tier 0 — Headless / compatibilidade extrema**
- Sem foco em render bonito.
- Serve para benchmark de física, captura de dados, testes automáticos.
- Desabilitar bloom, volume, overlays caros, uploads desnecessários.

**Tier 1 — GPU básica / hardware antigo**
- Física no CPU.
- Render de partículas com caminho simples.
- Sem compute shader.
- LOD agressivo, culling forte, redução de resolução e de efeitos.

**Tier 2 — Hardware intermediário**
- Física no CPU com AVX2 e threads.
- Render com buffers persistentes ou `glBufferSubData`.
- Pós-processamento opcional e controlado por orçamento.
- Este deve ser o perfil padrão mais robusto.

**Tier 3 — Hardware forte**
- Pode usar compute shader para partes isoladas.
- Mantém dados por mais tempo na GPU.
- Ativa efeitos mais caros apenas quando houver folga medida, não por configuração fixa.

**Mudança de mentalidade recomendada**
- O plano atual enumera otimizações individuais corretas, mas falta uma política de orçamento.
- Adicionar `frame budget manager`:
   - orçamento para física
   - orçamento para render
   - orçamento para upload
- Quando exceder orçamento por alguns frames consecutivos, reduzir automaticamente:
   - contagem efetiva de partículas desenhadas
   - frequência de atualização da física
   - resolução interna
   - qualidade do bloom / volume
   - frequência de rebuild de estruturas auxiliares

**Métricas & baseline revisados**
- Medir por perfil, não só por máquina:
   - FPS médio
   - p95 de frame time
   - ms da física no CPU
   - ms de upload CPU→GPU
   - ms de GPU
   - uso RAM
   - variação de alocações por segundo
   - ocupação de threads
- Cenários mínimos:
   - `low`: 500 / 1k / 2k partículas
   - `balanced`: 1k / 2k / 5k / 10k partículas
   - `high`: 10k / 20k / 50k partículas, se suportado

**Quick wins reais para qualquer hardware**
1. Remover alocações por frame.
    - `Renderer::renderParticles` hoje recria `pos_data` e `col_data` a cada chamada.
    - `NBodySolver::computeForces` recria `ax`, `ay`, `az` e `active_indices`.
    - Isso é bom em qualquer máquina e reduz jitter, não só média de FPS.
2. Evitar realocação de buffer GL por frame.
    - Trocar `glBufferData` recorrente por capacidade pré-alocada + `glBufferSubData` ou map persistente quando suportado.
3. Introduzir culling e LOD antes do upload.
    - Partículas invisíveis ou irrelevantes não devem consumir banda nem draw count.
4. Tornar pós-processamento adaptativo.
    - Bloom e volume devem obedecer o perfil de capacidade, não rodar sempre com o mesmo custo.
5. Expor configuração de perfil em runtime.
    - `auto`, `low`, `balanced`, `high`, além de overrides finos.

**Regra para reaproveitamento do que já está pronto**
- Antes de criar qualquer modo novo, verificar se o comportamento já pode ser expresso com os pontos de extensão atuais.
- Priorizar:
   - ampliar `Renderer` existente em vez de criar outro renderer paralelo
   - ampliar `NBodySolver` e `NBody` em vez de duplicar solver
   - usar `CpuFeatures` e variáveis já existentes como `COSMOS_THREADS` em vez de inventar nova configuração equivalente
- Evitar sinônimos desnecessários no plano e no código:
   - se já existe `Renderer`, não introduzir outro nome para o mesmo papel
   - se já existe `capabilities`, `profile` ou `render path`, escolher um termo e manter
   - se um recurso já está operacional, a tarefa deve falar em `estender`, `reusar` ou `generalizar`, não em `substituir` sem necessidade

**Otimizações CPU-first**
- Objetivo: fazer a simulação escalar mesmo quando a GPU for fraca ou inexistente para compute.
1. Reusar buffers auxiliares em `NBodySolver`.
2. Implementar pool contíguo para octree em vez de `unique_ptr` por nó.
3. Ajustar chunking e granularidade do `ThreadPool` com base no número de partículas ativas.
4. Validar e habilitar o caminho AVX2 quando houver suporte, sem tornar isso obrigatório.
5. Permitir frequência de física desacoplada da renderização.
6. Avaliar precisão mista:
    - estado principal em `double` onde necessário
    - buffers transitórios e render em `float`

**Otimizações de render / GPU**
- Objetivo: reduzir transferência e deixar o custo visual proporcional ao hardware.
1. Definir capacidade inicial máxima dos buffers de partículas e crescer raramente.
2. Adotar upload parcial quando a quantidade ativa variar pouco.
3. Introduzir compactação de draw list no CPU.
4. Separar efeitos caros em qualidade escalável:
    - `off`
    - `cheap`
    - `full`
5. Só partir para compute shader depois da camada de capacidade e do backend SSBO estarem limpos.

**Plano híbrido recomendado**
- Este deve ser o alvo principal de médio prazo.
- Estratégia:
   - CPU mantém a autoridade da simulação principal e estruturas adaptativas.
   - GPU desenha e acelera apenas os trechos visualmente caros ou massivamente paralelos.
   - Uploads são reduzidos com culling, LOD e buffers persistentes.
- Benefício: funciona bem em mais máquinas e reduz o risco de uma migração prematura para compute total.

**O que eu mudaria na ordem de prioridade**

PRIORIDADE ALTA — Base adaptativa
- [ ] Adicionar estrutura de `capabilities` e seleção de perfil `auto/low/balanced/high`.
   - Aceitação: o app identifica suporte de CPU/GPU e escolhe um perfil inicial de execução.
   - Arquivos: [src/main.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/main.cpp), [src/render/Renderer.hpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.hpp), [src/render/Renderer.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.cpp)
- [ ] Reusar `pos_data`/`col_data` como buffers membros do renderer.
   - Aceitação: zero alocações recorrentes por frame nesse caminho.
   - Arquivo: [src/render/Renderer.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.cpp)
- [ ] Transformar `ax/ay/az` e `active_indices` em buffers persistentes do `NBodySolver`.
   - Aceitação: menos alocações por passo e menos jitter de frame.
   - Arquivos: [src/physics/NBody.hpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/physics/NBody.hpp), [src/physics/NBody.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/physics/NBody.cpp)
- [ ] Trocar uploads recorrentes com `glBufferData` por buffer reutilizável com capacidade explícita.
   - Aceitação: queda no p95 de frame time em cenas com partículas variáveis.
   - Arquivo: [src/render/Renderer.cpp](/home/gabry/Área%20de%20trabalho/Cosmos/src/render/Renderer.cpp)

PRIORIDADE MÉDIA — Escala por orçamento
- [ ] Adicionar culling simples e LOD de partículas antes do upload.
   - Aceitação: draw count e tráfego diminuem sem quebrar a leitura visual.
- [ ] Introduzir atualização adaptativa de efeitos visuais por orçamento de frame.
   - Aceitação: bloom/volume reduzem custo automaticamente sob pressão.
- [ ] Implementar pool de nós para octree.
   - Aceitação: tempo de `buildTree` e alocações caem de forma mensurável.
- [ ] Tunear paralelismo com base em partículas ativas e threads disponíveis.
   - Aceitação: melhor escalonamento entre 2, 4, 6, 8+ threads.

PRIORIDADE MÉDIA/ALTA — Backend versátil
- [ ] Formalizar modos de renderização `Legacy`, `SSBO` e `Compute`.
   - Aceitação: o app consegue escolher o caminho visual sem duplicar toda a lógica de alto nível.
- [ ] Expor override manual do perfil e do backend.
   - Aceitação: usuário consegue testar combinações e comparar hardware facilmente.

PRIORIDADE BAIXA — Compute pesado / migração grande
- [ ] Prova de conceito de compute shader para uma etapa isolada, não para reescrever tudo de uma vez.
   - Aceitação: POC com ganho real em hardware compatível e fallback intacto.
- [ ] Avaliar GPU Barnes-Hut ou outro método mais adequado para GPU apenas depois de medir gargalos reais.

**Critérios de aceitação melhores**
- O aplicativo inicia e escolhe um perfil válido sem intervenção manual.
- Em hardware fraco, a simulação continua utilizável com redução progressiva de qualidade.
- Em hardware forte, a engine usa recursos extras sem quebrar compatibilidade.
- O usuário pode forçar manualmente perfil e backend para diagnóstico.
- A métrica principal deixa de ser apenas FPS máximo e passa a ser `estabilidade de frame time por tier`.

**Ferramentas & comandos úteis**
- Definir threads:
```bash
export COSMOS_THREADS=6
./build/cosmos
```
- Perfilar CPU:
```bash
perf stat ./build/cosmos
```
- Medir alocações / heap:
```bash
valgrind --tool=massif ./build/cosmos
```

**Resumo da recomendação**
- A melhor melhoria neste plano é trocar uma lista de otimizações locais por uma arquitetura adaptativa orientada por capacidade.
- A direção mais versátil não é "CPU-only ou GPU-only" como escolha global; é permitir combinações entre `simulação`, `preparação visual` e `apresentação`, com fallback limpo e seleção automática por hardware.
