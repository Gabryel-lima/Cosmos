# ── Cosmos — simulação cosmológica ───────────────────────────────────────────
# Uso:
#   make [build]    — baixar deps (se necessário) + configurar + compilar
#   make setup      — baixar / verificar dependências apenas (idempotente)
#   make run        — compilar e lançar
#   make clean      — remover artefatos de build
#   make distclean  — remover build + dependências baixadas
#
# Opções (sobrescrever na linha de comando):
#   QUALITY=LOW|MEDIUM|HIGH|ULTRA   (padrão: MEDIUM)
#   JOBS=N                           (padrão: nproc)
# ─────────────────────────────────────────────────────────────────────────────

SHELL   := /bin/bash
PROJECT := cosmos
BUILD   := build
LIBS    := libs
QUALITY ?= MEDIUM
JOBS    ?= $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

GLAD_SRC := $(LIBS)/glad/src/gl.c
IMGUI_H  := $(LIBS)/imgui/imgui.h

# Detectar downloader (preferir curl, usar wget como fallback)
CURL := $(shell command -v curl 2>/dev/null)
WGET := $(shell command -v wget 2>/dev/null)

.PHONY: all setup build run clean distclean help

# ── Alvo padrão ──────────────────────────────────────────────────────────────
all: 
	rm -rf $(PROJECT)
	@echo "Removido binário antigo (se existia). Preservando dependências para build mais rápido."
	@$(MAKE) build

# ── setup: baixar dependências (idempotente) ──────────────────────────────────
setup: $(GLAD_SRC) $(IMGUI_H)
	@echo "[setup] All dependencies present."

# Carregador GLAD OpenGL 4.3 Core ─────────────────────────────────────────────
# Baixa um carregador C GLAD2 pré-gerado do gerador web oficial.
# Ignora silenciosamente se o arquivo já existir.
$(GLAD_SRC):
	@echo "[setup] Downloading GLAD (OpenGL 4.3 Core)..."
	@mkdir -p $(LIBS)/glad
	@if [ -n "$(CURL)" ]; then \
	    curl -fsSL \
	        "https://gen.glad.sh/generate?lang=c&spec=gl&profile=core&api=gl=4.3&merge=True&extensions=" \
	        -o /tmp/glad_gen.zip 2>/dev/null; \
	elif [ -n "$(WGET)" ]; then \
	    wget -q \
	        "https://gen.glad.sh/generate?lang=c&spec=gl&profile=core&api=gl=4.3&merge=True&extensions=" \
	        -O /tmp/glad_gen.zip 2>/dev/null; \
	fi
	@if [ -f /tmp/glad_gen.zip ] && unzip -t /tmp/glad_gen.zip >/dev/null 2>&1; then \
	    unzip -qo /tmp/glad_gen.zip -d $(LIBS)/glad; \
	    rm -f /tmp/glad_gen.zip; \
	    echo "[setup] GLAD downloaded."; \
	else \
	    rm -f /tmp/glad_gen.zip; \
	    echo ""; \
	    echo "[error] Automatic GLAD download failed."; \
	    echo "        Manual option A — install glad2 via pip and generate:"; \
	    echo "          pip3 install glad2 --break-system-packages"; \
	    echo "          glad --api gl:core=4.3 --out-path $(LIBS)/glad --merge c"; \
	    echo "        Manual option B — copy pre-generated files into:"; \
	    echo "          $(LIBS)/glad/include/glad/gl.h"; \
	    echo "          $(LIBS)/glad/src/gl.c"; \
	    echo ""; \
	    exit 1; \
	fi

# Dear ImGui (branch docking) via git ────────────────────────────────────────
$(IMGUI_H):
	@echo "[setup] Cloning Dear ImGui (docking branch)..."
	@git clone --depth=1 --branch docking \
	    https://github.com/ocornut/imgui.git $(LIBS)/imgui 2>&1 | tail -3
	@echo "[setup] ImGui ready."

# ── compilação ───────────────────────────────────────────────────────────────
build: setup
	@mkdir -p $(BUILD)
	@cmake -S . -B $(BUILD) -DQUALITY=$(QUALITY) -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD) --parallel $(JOBS)

# ── execução ─────────────────────────────────────────────────────────────────
run: build
	./$(BUILD)/$(PROJECT)

# ── limpeza ──────────────────────────────────────────────────────────────────
clean:
	@rm -rf $(BUILD)
	@echo "Build artefacts removed.  Run 'make' to rebuild."

distclean: clean
	@rm -rf $(LIBS)/glad $(LIBS)/imgui
	@echo "Dependencies removed.  Run 'make setup' to re-download."

# ── ajuda ────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "Cosmos — cosmological simulation"
	@echo ""
	@echo "Targets:"
	@echo "  make [build]    Build (downloads deps first if needed)"
	@echo "  make setup      Download / verify dependencies only"
	@echo "  make run        Build and launch the simulation"
	@echo "  make clean      Remove build artefacts"
	@echo "  make distclean  Remove build artefacts + downloaded dependencies"
	@echo "  make help       Show this message"
	@echo ""
	@echo "Options (set on command line):"
	@echo "  QUALITY=LOW|MEDIUM|HIGH|ULTRA  Simulation quality  (default: MEDIUM)"
	@echo "  JOBS=N                         Parallel build jobs  (default: nproc)"
	@echo ""
	@echo "Examples:"
	@echo "  make QUALITY=HIGH"
	@echo "  make run QUALITY=ULTRA JOBS=8"
	@echo ""
