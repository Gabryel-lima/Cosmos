cmake_minimum_required(VERSION 3.20)
project(cosmos CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Flags base: SSE2 garantido em qualquer x86-64 ────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -O2
        -march=x86-64          # linha-base x86-64: sem AVX
        -mtune=generic
        -mno-avx               # proíbe AVX no alvo padrão explicitamente
        -mno-avx2
    )

    option(NATIVE_OPT "Enable -march=native (faster, may SIGILL on other CPUs)" OFF)
    if(NATIVE_OPT)
        add_compile_options(-march=native)
        message(WARNING "NATIVE_OPT=ON: binary will SIGILL on CPUs without AVX")
    endif()
endif()

# ── Localizar pacotes do sistema ─────────────────────────────────────────────
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
find_package(glm REQUIRED)
find_package(Threads REQUIRED)

# ── GLAD ─────────────────────────────────────────────────────────────────────
set(GLAD_SRC "${CMAKE_SOURCE_DIR}/libs/glad/src/gl.c")
if(NOT EXISTS "${GLAD_SRC}")
    message(FATAL_ERROR "GLAD not found.  Run: make setup")
endif()
add_library(glad STATIC "${GLAD_SRC}")
target_include_directories(glad PUBLIC "${CMAKE_SOURCE_DIR}/libs/glad/include")

# ── Dear ImGui ────────────────────────────────────────────────────────────────
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/libs/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "ImGui not found.  Run: make setup")
endif()
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC ${GLFW3_LIBRARIES})
target_include_directories(imgui PUBLIC ${GLFW3_INCLUDE_DIRS})

# ── Arquivos-fonte principais ─────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/core/SimulationRandom.cpp
    src/core/CosmicClock.cpp
    src/core/RegimeManager.cpp
    src/core/Universe.cpp
    src/core/Camera.cpp
    src/physics/Friedmann.cpp
    src/physics/NBody.cpp
    src/physics/FluidGrid.cpp
    src/physics/ParticlePool.cpp
    src/physics/NuclearNetwork.cpp
    src/physics/Hadronization.cpp
    src/regimes/RegimeInflation.cpp
    src/regimes/RegimeQGP.cpp
    src/regimes/RegimeNucleosynthesis.cpp
    src/regimes/RegimePlasma.cpp
    src/regimes/RegimeStructure.cpp
    src/render/Renderer.cpp
    src/render/ParticleRenderer.cpp
    src/render/VolumeRenderer.cpp
    src/render/PostProcess.cpp
    src/render/RegimeOverlay.cpp
)

add_executable(cosmos ${SOURCES})

target_include_directories(cosmos PRIVATE
    src
    ${GLFW3_INCLUDE_DIRS}
)

target_link_libraries(cosmos PRIVATE
    OpenGL::GL
    glad
    imgui
    Threads::Threads
    ${GLFW3_LIBRARIES}
    glm::glm
    dl
)

# ── Dispatcher AVX em runtime ────────────────────────────────────────────────
# Verifica se o compilador consegue emitir AVX2 (só capacidade do compilador,
# não da CPU do host).  O binário resultante carrega AMBAS as implementações;
# o dispatcher consulta CPUID em runtime e escolhe o caminho seguro.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)

if(COMPILER_SUPPORTS_AVX2 AND NOT NATIVE_OPT)
    # Fontes com hot-loops que se beneficiam de AVX (adicione conforme necessário)
    set(AVX_SOURCES
        src/physics/NBody_avx.cpp
    )

    # Objeto separado: compilado com -mavx2 -mfma, sem contaminar o restante
    add_library(cosmos_avx OBJECT ${AVX_SOURCES})
    target_compile_options(cosmos_avx PRIVATE
        -mavx2 -mfma
        -O3               # vale otimização agressiva aqui pois o caminho é guardado
        -mno-avx512f      # evita AVX-512 mesmo em compiladores que o suportam
    )
    target_include_directories(cosmos_avx PRIVATE src)
    target_compile_definitions(cosmos_avx PRIVATE COSMOS_AVX2_IMPL=1)

    # Injeta os .o AVX no executável final sem re-linkar flags erradas
    target_sources(cosmos PRIVATE $<TARGET_OBJECTS:cosmos_avx>)

    # Informa ao dispatcher que a implementação AVX está disponível
    target_compile_definitions(cosmos PRIVATE COSMOS_HAS_AVX2=1)

    message(STATUS "cosmos: AVX2 dispatch object compiled (-mavx2 -mfma)")
else()
    message(STATUS "cosmos: AVX2 not available — SSE2-only build")
endif()

# ── Perfil de qualidade ───────────────────────────────────────────────────────
set(QUALITY "MEDIUM" CACHE STRING "Simulation quality profile")
set_property(CACHE QUALITY PROPERTY STRINGS SAFE LOW MEDIUM HIGH ULTRA)
target_compile_definitions(cosmos PRIVATE "QUALITY_${QUALITY}=1")

message(STATUS "cosmos: quality=${QUALITY}, build=${CMAKE_BUILD_TYPE}")
