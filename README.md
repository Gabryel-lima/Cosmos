# Cosmos — Cosmological Simulation

<p align="center">
	<img alt="License" src="https://img.shields.io/badge/license-MIT-green.svg" />
	<img alt="C++17" src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" />
	<img alt="OpenGL 4.3" src="https://img.shields.io/badge/OpenGL-4.3-brightgreen.svg" />
	<img alt="ffmpeg" src="https://img.shields.io/badge/ffmpeg-required-red.svg" />
	<img alt="quality" src="https://img.shields.io/badge/quality-MEDIUM-orange.svg" />
	<img alt="build" src="https://img.shields.io/badge/build-local-lightgrey.svg" />
</p>

A compact, visual cosmological simulation (C++17) that runs from the Big Bang
to later structure formation. Each epoch is modelled as a focused physics
regime with real-time rendering (OpenGL 4.3), a Dear ImGui HUD and a small
set of user controls for fast exploration and reproducible diagnostics.

- Português (pt-BR): [README.pt-BR.md](README.pt-BR.md)

## What's New

- Expanded timeline to 9 playable phases spanning inflation through mature structure formation.
- Early-universe flow now separates Reheating and the Lepton/Electroweak era before the QGP and BBN stages.
- Automatic regime transitions with smooth cross-fade and continuity-preserving state handoff.
- Late macro regimes now use a richer volume-driven look: neutral gas fog, ionization fronts, emissivity fields, and gradual condensation into luminous sources.
- Quality presets were rebalanced around the new macro renderer, and a new `SAFE` tier targets weak CPUs / integrated GPUs.
- `SAFE` now also swaps in a cheaper volume-shader configuration, so weak machines get relief from both particle counts and volumetric raymarch cost.
- Runtime-safe CPU path: portable SSE2 baseline plus AVX2 dispatch for hot loops when available.
- Fixed-step simulation update loop with overload protection for stable dynamics under low FPS.
- Improved camera workflow: scene auto-framing, quick recenter, and nearest-particle tracking.
- Runtime resource path resolution anchored to executable directory (more reliable launches from different working directories).
- Jump requests from keyboard/HUD are now applied safely on the next frame, which keeps regime changes consistent with the simulation loop and camera framing.
- Richer HUD: transition-aware timeline, speed presets, log-scale speed multiplier, composition/performance panels, and late-regime visual tuning presets (`Planck-like`, `Patchy EoR`, `AGN-heavy`).
- Startup diagnostics now print the active build quality, structure particle count, plasma grid size, and Barnes-Hut theta for easier reproducibility.

## Simulated Regimes

| Epoch | Physics |
|-------|---------|
| Inflation | Rapid exponential expansion, vacuum energy |
| Reheating | Post-inflation particle seeding and thermalisation |
| Lepton / Electroweak Era | Relativistic lepton-boson plasma before confinement |
| Quark-Gluon Plasma (QGP) | Colour deconfinement + simplified QCD color state |
| Big Bang Nucleosynthesis (BBN) | Proton/neutron fusion network |
| Photon-Baryon Plasma | Radiation-dominated era, fluid grid + recombination dynamics |
| Dark Ages | Early post-recombination matter growth |
| Reionization | First luminous sources and ionization rise |
| Mature Structure Formation | N-body gravity, halo/star/black-hole evolution |

## Requirements

**System packages** (Ubuntu / Debian):
```bash
sudo apt install build-essential cmake ffmpeg libglfw3-dev libglm-dev libgl-dev git curl unzip
```

**Toolchain**: GCC 13+ (or Clang 16+), CMake 3.20+, an OpenGL 4.3 capable GPU.

## Build

```bash
# 1. Download libs/glad and libs/imgui (safe to re-run — skips if present)
make setup

# 2. Compile
make

# 3. Launch
make run
```

Manual run (equivalent):

```bash
./build/cosmos
```

### Quality profiles

```bash
make QUALITY=SAFE     # survival mode for weak CPU/iGPU laptops and debug captures
make QUALITY=LOW      # fast — fewer particles, lower resolution
make QUALITY=MEDIUM   # default balance for the newer volume-heavy macro pass
make QUALITY=HIGH     # fuller structure fields and denser particle phases
make QUALITY=ULTRA    # maximum detail — demands a capable CPU and GPU
```

`SAFE` reduces CPU-heavy particle counts aggressively in the late N-body stages while keeping a minimal 3D field resolution so the smoke-like macro gas remains readable.

In current builds, `SAFE` also lowers the cost of the late volumetric pass itself by trimming raymarch steps and shader noise complexity, which makes it the recommended first option on weak hardware.

### Native-speed build (optional)

If your CPU supports AVX2 and you want compiler auto-vectorisation to exploit
it, pass `NATIVE_OPT=ON` directly to CMake:

```bash
cd build && cmake .. -DNATIVE_OPT=ON
```

> **Warning**: a binary built with `-march=native` on an AVX2 host will
> **SIGILL** on CPUs that lack AVX (e.g. Intel Celeron / Pentium, many VMs).
> The default build targets the portable SSE2 baseline and uses runtime AVX2
> dispatch where available.

## Run Arguments

```bash
./build/cosmos [--fullscreen -f] [--width W] [--height H] [--seed N] [--video [FILE.mp4]] [--video-panorama] [--video-panorama-preset NAME] [--video-panorama-speed N] [--video-panorama-distance N] [--video-panorama-zoom N] [--video-panorama-height N] [--video-panorama-sway N] [--video-panorama-smooth N] [--video-capture-fps N] [--video-fps N]
```

- `--fullscreen`, `-f`: start in fullscreen.
- `--width`, `--height`: override initial window size.
- `--seed`: set deterministic simulation seed.
- `--video [FILE.mp4]`: enable deterministic video export through `ffmpeg`. The simulation advances one fixed capture frame at a time, so slow live rendering does not distort the diagnostic output. If omitted, the output file defaults to `cosmos_video.mp4`.
- `--video-panorama` / `--video-autocam`: keep the video export deterministic, but hand camera motion over to an autonomous panoramic flight that orbits the current scene, varying angle, height, and proximity while tracking the active regime extent.
- `--video-panorama-preset NAME`: load a named flight profile before applying manual overrides. Available presets: `cinematic`, `gentle`, `orbit`, `flyby`, `inspect`.
- `--video-panorama-speed N`: multiplies the camera travel speed around the scene.
- `--video-panorama-distance N`: scales the orbit distance from the framed scene.
- `--video-panorama-zoom N`: scales the visual zoom. Values above `1.0` move to a tighter framing; values below `1.0` keep the shot wider.
- `--video-panorama-height N`: scales the vertical lift of the trajectory.
- `--video-panorama-sway N`: scales the lateral sway used to avoid a rigid orbit.
- `--video-panorama-smooth N`: controls interpolation smoothness while the autonomous camera follows the path.
- `--video-capture-fps`: capture cadence used for the offline render. Default: `30`.
- `--video-fps`: final encoded frame rate. When greater than the capture cadence, `ffmpeg` applies motion interpolation (`minterpolate`). Default: `60`.

Preset overview:

- `cinematic`: balanced default path with moderate orbit, zoom variation, and sway.
- `gentle`: slower and wider framing with softer vertical and lateral motion.
- `orbit`: cleaner orbital movement with less sway and more stable framing.
- `flyby`: faster, closer, and more energetic motion for dynamic passes.
- `inspect`: slower and tighter framing for lingering near objects and dense regions.

Example:

```bash
./build/cosmos --video cosmos_panorama.mp4 --video-panorama --video-capture-fps 30 --video-fps 60
```

Customized example:

```bash
./build/cosmos --video cosmos_flyby.mp4 --video-panorama --video-panorama-preset flyby --video-panorama-speed 1.35 --video-panorama-distance 0.85 --video-panorama-zoom 1.20 --video-panorama-height 0.24 --video-panorama-sway 0.18 --video-panorama-smooth 3.0
```

## Controls

| Input | Action |
|-------|--------|
| `Space` | Pause / resume simulation |
| `.` | Advance one fixed simulation step |
| `1`..`9` | Jump directly to a phase |
| `[` or `,` | Increase time scale |
| `]` or `;` | Decrease time scale |
| `Tab` | Cycle camera mode |
| `T` | Toggle nearest-particle tracking |
| `C` | Recenter camera to current scene extent |
| `H` | Toggle HUD visibility |
| `R` | Reload shaders |
| `F` | Toggle fullscreen |
| `Esc` | Release tracking, or quit if not tracking |
| `Ctrl+Q` | Quit |
| `W/A/S/D/Q/E` | Free-flight camera movement |
| Left-drag | Orbit camera |
| Scroll wheel | Zoom |
| ImGui panel | Timeline + jump controls, speed presets, physics/composition/perf stats, late-regime tuning + visual presets |

Note: punctuation shortcuts follow the typed character via GLFW's char callback, so `,`, `.`, `;`, `[` and `]` keep working on non-US keyboard layouts too.

The jump controls are queued and executed on the following frame instead of inside the raw key callback, which avoids mid-frame state changes during heavy transitions.

## Physics + Rendering Highlights

- Cosmology backbone: Friedmann solver (`Lambda`CDM, RK4 integration).
- Early relativistic phases cover reheating, electroweak/lepton plasma, and QGP with recipe-driven particle seeding.
- QGP regime includes simplified QCD color tagging/tinting for quarks and gluons.
- BBN network tracks `n`, `p`, `D`, `He3`, `He4`, `Li7` abundances.
- Plasma regime evolves baryon fluid on 3D grid with Poisson solve and recombination behavior.
- Dark Ages, Reionization, and mature Structure formation share a phased Barnes-Hut N-body evolution with halo and ionization logic.
- Renderer includes HDR pipeline, bloom passes, regime transition blending, and a three-field macro volume pass (density, ionization, emissivity).

## Project Structure

```
Cosmos/
├── src/
│   ├── core/        — CosmicClock, RegimeManager, Universe state, Camera
│   ├── physics/     — Friedmann solver, N-body, FluidGrid, NuclearNetwork
│   ├── regimes/     — Per-epoch and multi-phase logic (Inflation, Reheating, Lepton era, QGP, BBN, Plasma, Dark Ages, Reionization, Structure)
│   ├── render/      — Renderer, ParticleRenderer, VolumeRenderer, PostProcess
│   └── shaders/     — GLSL vertex / fragment shaders for particles, volume, post-process
├── libs/
│   ├── glad/        — OpenGL 4.3 loader (generated by make setup)
│   └── imgui/       — Dear ImGui docking branch (cloned by make setup)
├── assets/          — Fonts and textures
├── CMakeLists.txt
└── Makefile
```

## Quick Start

Follow these steps to get the project running locally:

```bash
# 1. Download libs/glad and libs/imgui (safe to re-run — skips if present)
make setup

# 2. Compile
make

# 3. Launch (example)
make run
```

Manual run (equivalent):

```bash
./build/cosmos
```

## Run Examples

```bash
./build/cosmos --width 1280 --height 720 --seed 42
./build/cosmos --fullscreen
./build/cosmos --video diagnostics.mp4
./build/cosmos --video diagnostics.mp4 --video-capture-fps 30 --video-fps 60
```

## Video Demo

<video controls loop muted width="640">
	<source src="assets/videos/demo.mp4" type="video/mp4">
	Your browser does not support the video tag. You can open `assets/videos/demo.mp4` locally.
</video>

If you prefer to open the video locally or from the repo clone, use one of these commands:

```bash
xdg-open assets/videos/demo.mp4   # Linux desktop
mpv assets/videos/demo.mp4        # Lightweight player (mpv)
ffplay -autoexit assets/videos/demo.mp4  # quick check with ffmpeg tools
```

Note: some hosting platforms (including GitHub) may restrict autoplay and certain HTML features; if embedding does not autoplay, download or open the file locally.

## Troubleshooting

- Application crashes on start / blank window: verify GPU drivers and OpenGL 4.3 support. On Debian/Ubuntu, ensure `libgl1-mesa-dri` and `libglfw3` are installed.
- Low FPS / UI choppiness: try `make QUALITY=SAFE` first on weak integrated GPUs, then `LOW` if you still want a bit more fidelity.
- Need a stable baseline on weak hardware: start with `SAFE`; it is the only profile that reduces both late-era particle load and the volumetric shader workload.
- Video export fails immediately: confirm that `ffmpeg` is installed and available on `PATH`.
- SIGILL (illegal instruction) after building with native optimizations: you probably built with `-DNATIVE_OPT=ON`. Rebuild without native optimizations or use a portable build:

```bash
cd build && cmake .. -DNATIVE_OPT=OFF
make clean && make
```

- Shader compile errors: press `R` in the running app to reload shaders and check the console/log for compilation messages.
- Late regimes stall weak hardware: the new macro visuals lean on a volumetric pass plus Barnes-Hut evolution. Prefer `SAFE` for local validation and reserve `HIGH`/`ULTRA` for stronger hardware.

If a problem persists, open an issue with a short description and your GPU/driver information.

## Contributing

Contributions are welcome — fork the repo, create a topic branch, and submit a pull request with a clear description of changes. A good PR includes:

- a short summary of the change
- how to reproduce or test it locally
- screenshots or GIFs for visual changes

Run `make setup` and `make` before opening a PR to ensure external libs are present and the project builds locally.

## Credits & Acknowledgements

- Dear ImGui — immediate-mode GUI used for the in-game HUD and tools.
- GLAD — OpenGL function loader.
- GLFW — window and input management.
- stb and tiny helper headers used across the codebase.

If you use assets or fonts with their own licenses, please respect those licenses when redistributing builds.

## Contact / Issues

Please file bugs or feature requests using the repository Issues page. Include reproduction steps and environment details (OS, GPU, driver versions) for faster triage.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
