# Resource and memory cleanup validation — 2026-08-13

## Change under test

- Engine: Anoptic commit `8bd64e857f86e0d68dd96601c3c6740b4c23f10f`
  on `hyper-c-resourcemgr`.
- Diff: 20 files, 692 insertions, 827 deletions; net **−135 lines**.
- Scope: memory regions/volumes, reflected memory layouts, cooker revision
  storage, pack opening, runtime residency, glTF import arithmetic, and their
  public-boundary tests.

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T, AM4, 3.4 GHz base.
- GPU: NVIDIA GeForce RTX 4090, 24 GiB, driver 572.61
  (Windows `32.0.15.7261`, 2025-02-25).
- RAM: 64 GiB, 4×16 GiB Corsair `CMK32GX4M2D3600C18`, DDR4-3600.
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840×2160 at 119 Hz, 150% scale.

## Runtime environment

- Build: Release `-O3 -DNDEBUG`, CMake + Ninja, MSYS2 UCRT64 GCC 16.1.0
  Rev5, x86-64 Windows target, LTO.
- Renderer: Vulkan, mesh shaders enabled, 4× MSAA,
  `VK_PRESENT_MODE_IMMEDIATE_KHR`, unthrottled.
- Scene: Sponza, Viking room, candle holders and dynamic lights, reflected text
  HUD, shadow-map mode, 2 of 42 shadow frusta per frame.
- Harness: `tools/perf/bench_fps_win64.py`, default display-derived sweep,
  30 seconds per point, warmup dropped, per-point medians,
  foreground-verified.
- Window manager: Desktop Window Manager; borderless composited `WS_POPUP`
  windows sized to the requested physical framebuffer extent.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Full FPS sweep

| res | swap MiB | wall fps | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640×360 | 41.4 | 1197.7 | 1197.8 | 617.7 | 604.6 | 2.523 | 0.465 | 2151 | 0.56 | 2.0 | CPU/present |
| 960×540 | 97.6 | 1081.4 | 1080.7 | 586.2 | 572.1 | 2.471 | 0.549 | 1821 | 0.59 | 2.0 | CPU/present |
| 1280×720 | 164.7 | 979.0 | 976.6 | 545.9 | 536.2 | 2.502 | 0.632 | 1582 | 0.62 | 2.0 | CPU/present |
| 1920×1080 | 360.1 | 816.9 | 821.8 | 514.7 | 493.8 | 3.262 | 0.789 | 1267 | 0.65 | 2.0 | CPU/present |
| 2560×1440 | 642.3 | 649.4 | 655.9 | 432.2 | 425.0 | 3.432 | 1.064 | 940 | 0.70 | 2.0 | CPU/present |
| 3840×2160 | 1406.3 | 518.7 | 520.4 | 490.0 | 478.7 | 2.868 | 1.905 | 525 | 0.99 | 2.0 | GPU |

Every row was `FRONT`; requested and realized render extents matched. Swap
storage ranges from 169.5 to 188.3 MiB per megapixel. The 4K/1080p swap ratio
is 3.905 versus a 4.000 pixel ratio, so render and swap accounting agree.

## Baking and hot-reload proof

Every committed mutation permuted all 69 Sponza source textures while toggling
the Viking source between the standard room and the hat variant. Captures came
from the engine's own F12 framebuffer path, not desktop capture.

| epoch | result | end-to-end time | capture SHA-256 prefix |
| --- | --- | ---: | --- |
| Hat + material rotation 1 | committed | 0.609 s | `37a5990fd9101a09` |
| Standard Viking + rotation 7 | committed | 0.594 s | `206b4bb48b093448` |
| Viking/candle source-role cross | rejected atomically | 0.547 s | `8f102709f49c9348` |
| Hat + rotation 31 | committed | 0.515 s | `6cbf3f576882be9d` |
| Standard Viking + rotation 47 | committed | 0.594 s | `a06732bd3e664eb4` |
| Hat + rotation 17 | committed | 0.594 s | `904a98913818b094` |
| Original sources and materials | committed | 0.578 s | `fb51b8f9b520dda5` |

The cross-role experiment cooks successfully but would reuse derived asset IDs
with different reflected types. Runtime publication rejects that generation
instead of violating an existing typed handle or exposing a mixed epoch. Full
topology substitution therefore needs stable semantic derived IDs or fresh IDs
with tombstones; relaxing the type check would be incorrect.

## Build and test matrix

| target | result |
| --- | --- |
| WSL/Nix full Vulkan Release engine | built; C++ runtime-symbol audit passed |
| WSL Release CTest | 29/29 enabled tests passed; 21 optional tests disabled by policy |
| Windows UCRT64 full native renderer build | built; C++ runtime-symbol audit passed |
| Windows native CTest | 34/34 enabled tests passed; Vulkan lifecycle, shadow, memory and texture-domain tests included |
| AddressSanitizer memory/resource subset | 5/5 passed |
| ThreadSanitizer memory/resource subset | 5/5 passed |
| Reflected declaration compile-fail contracts | 4/4 passed |

Generated frame captures remain transient validation artifacts; this report
records their hashes and the engine-native capture method without adding binary
images to source history.
