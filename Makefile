# ── Cosmos — simulação cosmológica ───────────────────────────────────────────
# Uso:
#   make [build]    — baixar deps (se necessário) + configurar + compilar
#   make setup      — baixar / verificar dependências apenas (idempotente)
#   make run        — compilar e lançar
#   make clean      — remover artefatos de build
#   make distclean  — remover build + dependências baixadas
#
# Opções (sobrescrever na linha de comando):
#   QUALITY=SAFE|LOW|MEDIUM|HIGH|ULTRA   (padrão: MEDIUM)
#   JOBS=N                           (padrão: nproc)
# ─────────────────────────────────────────────────────────────────────────────

SHELL   := /bin/bash
PROJECT := cosmos
BUILD   := build
LIBS    := libs
QUALITY ?= MEDIUM
LOG     ?= 0
JOBS    ?= $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

GLAD_SRC := $(LIBS)/glad/src/gl.c
IMGUI_H  := $(LIBS)/imgui/imgui.h

# Detectar downloader (preferir curl, usar wget como fallback)
CURL := $(shell command -v curl 2>/dev/null)
WGET := $(shell command -v wget 2>/dev/null)

.PHONY: all setup build run clean distclean help LOG

.PHONY: preview preview-build preview-run

ifeq ($(filter LOG,$(MAKECMDGOALS)),LOG)
LOG := 1
endif

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
	@echo "[setup] Obtaining GLAD (OpenGL 4.3 Core)..."
	@mkdir -p $(LIBS)/glad
	@TMPZIP=/tmp/glad_gen.zip; \
	TMPHDR=/tmp/glad_gen.headers; \
	TMPBODY=/tmp/glad_gen.body; \
	TRIED=0; \
	for ENDPOINT in \
		"https://gen.glad.sh/generate" \
		"https://glad.dav1d.de/generate"; do \
		if [ -n "$(CURL)" ]; then \
			TRIED=1; \
			echo "[setup] Trying $${ENDPOINT} ..."; \
			rm -f "$${TMPZIP}" "$${TMPHDR}" "$${TMPBODY}"; \
			curl -fsS -D "$${TMPHDR}" -o "$${TMPBODY}" -X POST "$${ENDPOINT}" \
				-H "Content-Type: application/x-www-form-urlencoded" \
				--data "generator=c&api=gl%3D4.3&profile=gl%3Dcore&options=MERGE" \
				2>/dev/null || true; \
			LOCATION=$$(sed -n 's/^location: //p' "$${TMPHDR}" | tr -d '\r'); \
			if [ -n "$${LOCATION}" ]; then \
				BASE="$${ENDPOINT%/generate}$${LOCATION%/}"; \
				mkdir -p $(LIBS)/glad/include/glad $(LIBS)/glad/src; \
				curl -fsSL "$${BASE}/include/glad/gl.h" -o $(LIBS)/glad/include/glad/gl.h 2>/dev/null && \
				curl -fsSL "$${BASE}/src/gl.c" -o $(LIBS)/glad/src/gl.c 2>/dev/null && { \
					rm -f "$${TMPZIP}" "$${TMPHDR}" "$${TMPBODY}"; \
					echo "[setup] GLAD downloaded from $${ENDPOINT}."; \
					exit 0; \
				}; \
			fi; \
			if [ -f "$${TMPBODY}" ] && unzip -t "$${TMPBODY}" >/dev/null 2>&1; then \
				mv "$${TMPBODY}" "$${TMPZIP}"; \
				unzip -qo "$${TMPZIP}" -d $(LIBS)/glad; \
				rm -f "$${TMPZIP}" "$${TMPHDR}"; \
				echo "[setup] GLAD downloaded from $${ENDPOINT}."; \
				exit 0; \
			fi; \
		elif [ -n "$(WGET)" ]; then \
			TRIED=1; \
			echo "[setup] Trying $${ENDPOINT} with wget..."; \
			rm -f "$${TMPZIP}" "$${TMPBODY}"; \
			wget -q --server-response --max-redirect=0 --method=POST \
				--body-data="generator=c&api=gl%3D4.3&profile=gl%3Dcore&options=MERGE" \
				-O "$${TMPBODY}" "$${ENDPOINT}" 2>"$${TMPHDR}" || true; \
			LOCATION=$$(sed -n 's/^  Location: //p' "$${TMPHDR}" | tr -d '\r' | tail -n 1); \
			if [ -n "$${LOCATION}" ]; then \
				BASE="$${ENDPOINT%/generate}$${LOCATION%/}"; \
				mkdir -p $(LIBS)/glad/include/glad $(LIBS)/glad/src; \
				wget -q -O $(LIBS)/glad/include/glad/gl.h "$${BASE}/include/glad/gl.h" 2>/dev/null && \
				wget -q -O $(LIBS)/glad/src/gl.c "$${BASE}/src/gl.c" 2>/dev/null && { \
					rm -f "$${TMPZIP}" "$${TMPHDR}" "$${TMPBODY}"; \
					echo "[setup] GLAD downloaded from $${ENDPOINT}."; \
					exit 0; \
				}; \
			fi; \
			if [ -f "$${TMPBODY}" ] && unzip -t "$${TMPBODY}" >/dev/null 2>&1; then \
				mv "$${TMPBODY}" "$${TMPZIP}"; \
				unzip -qo "$${TMPZIP}" -d $(LIBS)/glad; \
				rm -f "$${TMPZIP}" "$${TMPHDR}"; \
				echo "[setup] GLAD downloaded from $${ENDPOINT}."; \
				exit 0; \
			fi; \
		fi; \
	done; \
	if command -v glad >/dev/null 2>&1; then \
		echo "[setup] Generating GLAD using local 'glad' tool..."; \
		glad --api gl:core=4.3 --out-path $(LIBS)/glad --merge c >/dev/null 2>&1 && { echo "[setup] GLAD generated locally."; exit 0; } || true; \
	fi; \
	if [ "$${TRIED}" = "1" ]; then \
		echo ""; \
		echo "[error] Automatic GLAD download failed from known endpoints."; \
		echo "        Options to resolve:"; \
		echo "          1) Install the 'glad' generator and let Makefile generate it:"; \
		echo "               pip3 install glad2 --break-system-packages"; \
		echo "               make setup"; \
		echo "          2) Generate on the web (open https://gen.glad.sh/) and copy files:"; \
		echo "               Put include/glad/gl.h into $(LIBS)/glad/include/glad/"; \
		echo "               Put src/gl.c into $(LIBS)/glad/src/"; \
		echo ""; \
		exit 1; \
	else \
		echo "[error] No HTTP downloader (curl/wget) found. Install curl or wget, or install 'glad' via pip."; \
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
	COSMOS_LOG=$(LOG) ./$(BUILD)/$(PROJECT) $(if $(filter 1 true yes on,$(LOG)),--log,)

LOG:
	@:

# Build and run the shader preview binary (small GL app to inspect shaders)
preview: preview-build
	./$(BUILD)/shader_preview

preview-build: setup
	@mkdir -p $(BUILD)
	@cmake -S . -B $(BUILD) -DQUALITY=$(QUALITY) -DBUILD_SHADER_PREVIEW=ON -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD) --parallel $(JOBS) --target shader_preview

preview-run:
	./$(BUILD)/shader_preview

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
	@echo "  QUALITY=SAFE|LOW|MEDIUM|HIGH|ULTRA  Simulation quality  (default: MEDIUM)"
	@echo "  LOG=1                          Enable telemetry file in logs/"
	@echo "  JOBS=N                         Parallel build jobs  (default: nproc)"
	@echo ""
	@echo "Examples:"
	@echo "  make QUALITY=SAFE"
	@echo "  make run LOG"
	@echo "  make run QUALITY=ULTRA JOBS=8"
	@echo ""
