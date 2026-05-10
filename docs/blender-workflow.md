# Blender Bake Workflow

This project currently ships GLSL runtime shaders in `src/shaders/`. Those files are not image textures; they are GPU programs executed by the OpenGL renderer.

Blender is still useful here, but in a different role: asset authoring. The productive workflow is to build procedural looks in Blender, bake them into image maps, and then consume those maps from the runtime shaders when a precomputed texture is cheaper or easier to art-direct.

## Concept: baking

Texture baking is the process of evaluating a material, lighting term, mask, or procedural graph ahead of time and writing the result into an image.

Why that matters here:

- runtime shaders stay simpler
- repeated noise and mask evaluation can move offline
- visual iteration becomes easier because Blender's node editor is faster to art-direct than raw GLSL

Mental model:

1. Blender computes a procedural look once.
2. The result is saved into a texture file.
3. The C++ renderer samples that file every frame instead of rebuilding the same pattern from scratch.

What baking is not:

- it does not automatically convert GLSL into a Blender material
- it does not remove the need to integrate the exported texture in the runtime shader

## Workspace setup

The repository includes workspace settings in `.vscode/settings.json` that:

- open `src/shaders/blender/cosmos.blend` by default when Blender is started from VS Code
- isolate Blender extension state under `${workspaceFolder}/.blender-vscode`
- enable debug logging for the Blender output channel
- document the `blender.executables` block with Linux examples, including Steam installs

The workspace also recommends the VS Code extension via `.vscode/extensions.json`, but you still need to install it in the editor before the `Blender:*` commands are available.

`make setup` also verifies that a usable `bpy` runtime exists for automation. The target first accepts a host `python3` that already imports `bpy`, then tries a detected Blender executable, and only creates a local `.venv-blender-tools/` if neither is available. If autodetection picks the wrong Blender install, run `BLENDER_BIN=/absolute/path/to/blender make setup`.

Recommended extension:

- `JacquesLucke.blender-development`

One machine-specific setting should stay in your user settings, not in the repo: the Blender executable path.

Open VS Code settings JSON and add something like this:

```jsonc
{
  "blender.executables": [
    {
      "path": "/absolute/path/to/blender",
      "isDefault": true,
      "platform": "linux"
    }
  ]
}
```

Example Linux paths:

- `/usr/bin/blender`
- `/snap/bin/blender`
- `/home/<user>/snap/steam/common/.local/share/Steam/steamapps/common/Blender/blender`
- `/home/<user>/blender-4.x/blender`
- `/home/<user>/.local/share/Steam/steamapps/common/Blender/blender`
- `/home/<user>/.steam/steam/steamapps/common/Blender/blender`

If Blender was installed through Steam and `blender` is not on `PATH`, point `blender.executables[].path` to the binary inside `steamapps/common/Blender/blender`.

If Steam itself was installed through Snap, the most likely path is:

- `/home/<user>/snap/steam/common/.local/share/Steam/steamapps/common/Blender/blender`

## First-use flow with the Jacques Lucke extension

1. Install the recommended VS Code extension: `JacquesLucke.blender-development`.
2. Open the workspace root in VS Code.
3. Run `Blender: Start` from the command palette.
4. Select your Blender executable the first time.
5. Confirm that Blender opens `src/shaders/blender/cosmos.blend`.
6. Check the `Blender` output panel if the launch fails.

## Suggested asset layout

Use `assets/textures/` for generated maps.

Recommended naming:

- `assets/textures/<regime>/<asset_name>_albedo.png`
- `assets/textures/<regime>/<asset_name>_emission.exr`
- `assets/textures/<regime>/<asset_name>_mask.png`
- `assets/textures/<regime>/<asset_name>_density.exr`

Suggested conventions:

- use `png` for masks and low-dynamic-range maps
- use `exr` for emissive, density, or HDR data
- keep source `.blend` files in `src/shaders/blender/`
- treat baked textures as runtime assets under `assets/textures/`

## Basic bake workflow in Blender

### 1. Create or refine the source material

In Blender's Shader Editor:

- build a procedural graph for noise, filaments, glow, density, or masks
- keep parameters grouped and named clearly
- prefer stable coordinate sources such as `UV`, `Object`, or `Generated`

### 2. Prepare a bake target image

In the Shader Editor:

1. Add an `Image Texture` node.
2. Create a new image with the target resolution.
3. Give it a descriptive name such as `filament_mask_2048`.
4. Leave that `Image Texture` node selected before baking.

Recommended starting resolutions:

- `1024` for masks, breakup maps, and secondary noise
- `2048` for hero maps
- `4096` only when close-up detail justifies it

### 3. Choose the right bake type

Use the bake type that matches the data you want to preserve:

- `Emit` for emissive patterns and pure procedural light maps
- `Diffuse` when you want the base color without lighting complexity
- `Combined` only when you intentionally want the full lit result

For data maps such as masks and scalar fields, `Emit` is usually the cleanest option because it avoids lighting contamination.

### 4. Bake and save

1. Switch to `Cycles` if the bake option you need is unavailable in your current renderer.
2. Open the `Render Properties` panel.
3. Use `Bake` with the selected image node as target.
4. In the Image Editor, save the generated image into `assets/textures/...`.

## Optimization guidelines

Start with these rules before exporting many maps:

1. Bake procedural detail that does not need to change every frame.
2. Keep truly dynamic effects in GLSL.
3. Pack multiple grayscale maps into RGB channels when the sampling code can unpack them.
4. Prefer one good shared lookup texture over many tiny one-off textures.
5. Reuse a texture across regimes when the shape language is compatible.

Typical division of responsibilities:

- keep time-varying glow flicker, camera-dependent effects, and post-process in GLSL
- move stable density masks, breakup noise, and art-directed gradients into baked textures

## Blender script workflow

The extension becomes especially useful once you automate repetitive bake/export tasks with Python scripts.

Typical use cases:

- batch-bake multiple materials
- export several resolutions in one pass
- regenerate masks after parameter changes
- enforce file naming and output directories automatically

Suggested script location:

- `tools/blender/` for standalone scripts

After creating a script:

1. Run `Blender: Run Script` from VS Code.
2. Inspect the `Blender` output panel.
3. Iterate until the bake/export step is one command.

## First implemented example in this repository

The first end-to-end example now targets the late-regime volume shader.

- Blender script: `tools/blender/generate_volume_macro_lookup.py`
- Runtime asset: `assets/textures/structure/volume_macro_lookup.pgm`
- Runtime shader consumer: `src/shaders/volume.frag`

The same script now also emits two extra baked 2D point-sprite profiles:

- `assets/textures/dark_ages/gas_splat_profile.pgm` consumed by `src/shaders/gas_splat.frag`
- `assets/textures/reionization/star_glow_profile.pgm` consumed by `src/shaders/star_glow.frag`

What this example does:

- generates a grayscale 2D macro lookup map from Blender via Python
- generates grayscale 2D baked profiles for gas splats and star glow billboards
- stores it under `assets/textures/structure/`
- reloads it inside the OpenGL renderer when the app starts or when shaders are reloaded with `R`
- uses it to modulate the late volumetric breakup and filament shaping

Why this is the first target:

- it is low-risk because `volume.frag` already depends on texture samplers
- the baked map is optional and falls back safely if missing
- it demonstrates the full pipeline before moving to more ambitious material bakes

To regenerate the lookup:

1. Start Blender from VS Code.
2. Open `tools/blender/generate_volume_macro_lookup.py`.
3. Run `Blender: Run Script`.
4. Go back to the running app and press `R` to reload shaders and the baked lookup.

For headless validation outside the VS Code extension, `make setup` is enough to validate a usable `bpy` source. On machines where a pip wheel is unavailable, the project falls back to the `bpy` bundled with the detected Blender binary.

## Integration checklist for runtime shaders

Before replacing procedural GLSL with a baked texture, confirm:

1. the texture uses a stable coordinate space
2. the asset resolution is justified by the camera distance
3. the shader uniform and sampler wiring are ready on the C++ side
4. the visual result still works under the project's tonemapping and bloom passes

## Common mistakes

- Baking a lit result when the runtime shader expects raw data.
- Exporting everything at 4K and paying unnecessary memory cost.
- Using different naming conventions for files that belong to the same regime.
- Forgetting that Blender and runtime shaders may interpret color space differently.
- Trying to use Blender as a direct GLSL editor instead of as an offline asset tool.

## Where this appears elsewhere

The same concept shows up in:

- lightmap baking in game engines
- normal/AO baking in asset pipelines
- LUT and signed-distance-field generation for real-time rendering
