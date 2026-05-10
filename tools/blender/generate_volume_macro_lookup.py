import importlib
import math
import os
from pathlib import Path
from types import ModuleType


WIDTH = 512
HEIGHT = 512
OUTPUT_NAME = "volume_macro_lookup.pgm"
IMAGE_NAME = "CosmosVolumeMacroLookup"
GAS_SPLAT_OUTPUT = "gas_splat_profile.pgm"
GAS_SPLAT_IMAGE = "CosmosGasSplatProfile"
STAR_GLOW_OUTPUT = "star_glow_profile.pgm"
STAR_GLOW_IMAGE = "CosmosStarGlowProfile"


def require_bpy() -> ModuleType:
    try:
        return importlib.import_module("bpy")
    except ModuleNotFoundError as exc:
        raise RuntimeError(
            "This script requires bpy. Use Blender: Run Script, `blender --python`, or run `make setup` to verify a usable bpy runtime."
        ) from exc


bpy = require_bpy()


def fract(value: float) -> float:
    return value - math.floor(value)


def hash2(x: float, y: float) -> float:
    return fract(math.sin(x * 127.1 + y * 311.7) * 43758.5453123)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def value_noise(x: float, y: float) -> float:
    ix = math.floor(x)
    iy = math.floor(y)
    fx = fract(x)
    fy = fract(y)
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)

    v00 = hash2(ix, iy)
    v10 = hash2(ix + 1.0, iy)
    v01 = hash2(ix, iy + 1.0)
    v11 = hash2(ix + 1.0, iy + 1.0)

    vx0 = v00 + (v10 - v00) * fx
    vx1 = v01 + (v11 - v01) * fx
    return vx0 + (vx1 - vx0) * fy


def fbm(x: float, y: float, octaves: int = 5) -> float:
    amplitude = 0.5
    frequency = 1.0
    total = 0.0
    norm = 0.0
    for _ in range(octaves):
        total += amplitude * value_noise(x * frequency, y * frequency)
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.03
    return total / max(norm, 1e-6)


def generate_lookup(width: int, height: int) -> list[int]:
    pixels: list[int] = []
    for y in range(height):
        v = y / max(height - 1, 1)
        for x in range(width):
            u = x / max(width - 1, 1)
            primary = fbm(u * 5.0 + 0.17, v * 5.0 + 0.31)
            wispy = fbm(u * 11.0 + 3.1, v * 9.0 + 1.7, octaves=4)
            ridge = 1.0 - abs(fbm(u * 7.0 + wispy, v * 7.0 + primary, octaves=3) * 2.0 - 1.0)
            bands = 0.5 + 0.5 * math.sin((u * 9.0 + v * 5.0 + primary * 2.7) * math.pi)
            value = 0.48 * primary + 0.22 * wispy + 0.20 * ridge + 0.10 * bands
            value = smoothstep(0.16, 0.88, value)
            pixels.append(int(max(0.0, min(1.0, value)) * 255.0 + 0.5))
    return pixels


def generate_gas_splat_profile(width: int, height: int) -> list[int]:
    pixels: list[int] = []
    sigma2 = 0.04
    for y in range(height):
        v = y / max(height - 1, 1) - 0.5
        for x in range(width):
            u = x / max(width - 1, 1) - 0.5
            dist2 = u * u + v * v
            alpha = math.exp(-dist2 / (2.0 * sigma2))
            pixels.append(int(max(0.0, min(1.0, alpha)) * 255.0 + 0.5))
    return pixels


def generate_star_glow_profile(width: int, height: int) -> list[int]:
    pixels: list[int] = []
    for y in range(height):
        v = y / max(height - 1, 1) - 0.5
        for x in range(width):
            u = x / max(width - 1, 1) - 0.5
            dist2 = u * u + v * v
            core = math.exp(-dist2 / 0.005)
            halo = math.exp(-dist2 / 0.08) * 0.3
            total = max(0.0, min(1.0, core + halo))
            pixels.append(int(total * 255.0 + 0.5))
    return pixels


def resolve_project_root() -> Path:
    env_root = os.environ.get("COSMOS_PROJECT_ROOT")
    if env_root:
        return Path(env_root).resolve()

    if bpy.data.filepath:
        return Path(bpy.data.filepath).resolve().parent.parents[2]

    return Path(__file__).resolve().parents[2]


def ensure_blender_image(width: int, height: int, pixels: list[int]) -> None:
    image = bpy.data.images.get(IMAGE_NAME)
    if image is None:
        image = bpy.data.images.new(IMAGE_NAME, width=width, height=height, alpha=False, float_buffer=False)
    elif image.size[0] != width or image.size[1] != height:
        image.scale(width, height)

    rgba: list[float] = []
    for value in pixels:
        channel = value / 255.0
        rgba.extend((channel, channel, channel, 1.0))
    image.pixels = rgba
    image.update()


def write_image(image_name: str, width: int, height: int, pixels: list[int]) -> None:
    image = bpy.data.images.get(image_name)
    if image is None:
        image = bpy.data.images.new(image_name, width=width, height=height, alpha=False, float_buffer=False)
    elif image.size[0] != width or image.size[1] != height:
        image.scale(width, height)

    rgba: list[float] = []
    for value in pixels:
        channel = value / 255.0
        rgba.extend((channel, channel, channel, 1.0))
    image.pixels = rgba
    image.update()


def write_pgm(path: Path, width: int, height: int, pixels: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(f"P5\n{width} {height}\n255\n".encode("ascii"))
        handle.write(bytes(pixels))


def main() -> None:
    project_root = resolve_project_root()
    output_path = project_root / "assets" / "textures" / "structure" / OUTPUT_NAME
    pixels = generate_lookup(WIDTH, HEIGHT)
    ensure_blender_image(WIDTH, HEIGHT, pixels)
    write_pgm(output_path, WIDTH, HEIGHT, pixels)

    point_profile_size = 128
    gas_output_path = project_root / "assets" / "textures" / "dark_ages" / GAS_SPLAT_OUTPUT
    gas_pixels = generate_gas_splat_profile(point_profile_size, point_profile_size)
    write_image(GAS_SPLAT_IMAGE, point_profile_size, point_profile_size, gas_pixels)
    write_pgm(gas_output_path, point_profile_size, point_profile_size, gas_pixels)

    glow_output_path = project_root / "assets" / "textures" / "reionization" / STAR_GLOW_OUTPUT
    glow_pixels = generate_star_glow_profile(point_profile_size, point_profile_size)
    write_image(STAR_GLOW_IMAGE, point_profile_size, point_profile_size, glow_pixels)
    write_pgm(glow_output_path, point_profile_size, point_profile_size, glow_pixels)

    print(f"[Blender] Wrote {output_path}")
    print(f"[Blender] Wrote {gas_output_path}")
    print(f"[Blender] Wrote {glow_output_path}")


if __name__ == "__main__":
    main()
