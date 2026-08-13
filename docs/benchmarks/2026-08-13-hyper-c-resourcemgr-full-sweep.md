# 2026-08-13 hyper-c-resourcemgr full sweep

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T, x86-64, AM4, 3.4 GHz base.
- GPU: NVIDIA GeForce RTX 4090, 24 GB, driver 572.61 (Windows 32.0.15.7261, 2025-02-25).
- RAM: 64 GB (4x16 GB) Corsair CMK32GX4M2D3600C18, DDR4-3600 at 3600 MT/s.
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 at 119 Hz, 150% scale.

## Runtime environment

- Engine: working tree based on commit `2c38da92cc5dea4abab36372094d76058f76ea56` on `hyper-c-resourcemgr`, including mimalloc 3.4.5 and the region-allocation substrate.
- Build: Windows Release `-O3` with LTO, CMake + Ninja, GCC 16.1 MinGW-w64 targeting `x86_64-w64-mingw32`.
- Renderer: Vulkan, mesh shaders enabled, 4x MSAA, immediate present mode with vsync off, unthrottled render loop.
- Scene: Sponza, Viking room, candle holders, dynamic lighting and text; SHADOWMAP mode; two of 42 shadow frusta per frame; HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py`, default display-derived sweep, 30 seconds per point, two-second warmup dropped, per-point summaries, foreground verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Borderless windowed (`WS_POPUP`), client area set to each physical-pixel target by the per-monitor-DPI-aware-v2 harness. The 3840x2160 row fills the native desktop.

## Results

| target | front | render | swap MiB | avg FPS | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | FRONT | 640x360 | 41.4 | 966.3 | 965.5 | 400.5 | 384.5 | 3.523 | 0.551 | 1815 | 0.53 | 2.0 | CPU/present |
| 960x540 | FRONT | 960x540 | 97.6 | 876.1 | 875.3 | 382.7 | 371.2 | 3.152 | 0.646 | 1548 | 0.57 | 2.0 | CPU/present |
| 1280x720 | FRONT | 1280x720 | 164.7 | 800.8 | 801.4 | 370.8 | 357.0 | 3.426 | 0.729 | 1372 | 0.58 | 2.0 | CPU/present |
| 1920x1080 | FRONT | 1920x1080 | 360.1 | 705.7 | 668.3 | 343.2 | 336.5 | 4.040 | 0.924 | 1082 | 0.62 | 2.0 | CPU/present |
| 2560x1440 | FRONT | 2560x1440 | 642.3 | 567.2 | 566.1 | 302.9 | 296.8 | 3.730 | 1.181 | 847 | 0.67 | 2.0 | CPU/present |
| 3840x2160 | FRONT | 3840x2160 | 1406.3 | 517.9 | 518.9 | 478.9 | 469.7 | 2.664 | 1.908 | 524 | 0.99 | 2.0 | GPU |

All rows are foreground verified, every requested target equals the engine-reported render extent, and every row retains GPU profiles after warmup. The run logs contain no error, fatal, validation-failure or rejected-reload entries.

Resolution check: swap residency ranges from 169.55 to 188.27 MiB per megapixel. The 4K-to-1080p swap ratio is 3.905 against a 4.000 pixel ratio.
