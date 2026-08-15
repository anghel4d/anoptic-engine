# Result composition threshold sweep - 2026-08-15

## Purpose

This full sweep tests commit `b14efec91be1` against the absolute minimum
throughput, 1% low, and GPU-time thresholds supplied for the reflected Result
composition work. The unmodified candidate passed every threshold on its first
complete sweep, so no speculative runtime optimization was applied.

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T, AM4, 3.4 GHz base.
- GPU: NVIDIA GeForce RTX 4090, 24 GiB, driver 572.61
  (Windows `32.0.15.7261`).
- RAM: 64 GiB, 4x16 GiB Corsair `CMK32GX4M2D3600C18`, DDR4-3600.
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b.
- OS: Windows 11 Pro 25H2, build 26200, x64.
- Display: 3840x2160 physical pixels, 3840x2160 largest realizable framebuffer.

## Runtime environment

- Engine: Anoptic at `b14efec91be1`, branch `hyper-c-resourcemgr`.
- Build: Nix Release, GCC 16.2.0 MinGW-w64 UCRT x86-64 target, LTO.
- Renderer: Vulkan, mesh shaders enabled, 4x MSAA,
  `VK_PRESENT_MODE_IMMEDIATE_KHR`, unthrottled.
- Scene: Sponza, Viking room, candle holders and dynamic lights, reflected text
  HUD, shadow-map mode, two of 42 shadow frusta per frame, HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py`, default display-derived sweep,
  30 seconds per process, two-second elapsed-time warmup dropped, per-window
  medians, foreground-verified.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Window manager and mode

Desktop Window Manager, composited. Borderless `WS_POPUP` windows used
physical framebuffer-sized client areas under per-monitor-v2 DPI awareness.
The 3840x2160 row filled the native desktop.

## Results

| res | swap MiB | wall fps | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 41.4 | 1197.8 | 1197.6 | 616.1 | 602.8 | 1.980 | 0.468 | 2137 | 0.56 | 2.0 | CPU/present |
| 960x540 | 97.6 | 1083.0 | 1080.8 | 579.4 | 567.9 | 2.437 | 0.551 | 1815 | 0.60 | 2.0 | CPU/present |
| 1280x720 | 164.7 | 981.3 | 983.1 | 545.6 | 536.8 | 7.937 | 0.630 | 1587 | 0.62 | 2.0 | CPU/present |
| 1920x1080 | 360.1 | 809.9 | 818.1 | 503.5 | 485.2 | 2.587 | 0.794 | 1260 | 0.65 | 2.0 | CPU/present |
| 2560x1440 | 642.3 | 648.3 | 653.8 | 432.0 | 424.6 | 2.834 | 1.065 | 939 | 0.70 | 2.0 | CPU/present |
| 3840x2160 | 1406.3 | 520.6 | 522.2 | 494.3 | 483.4 | 2.733 | 1.898 | 527 | 0.99 | 2.0 | GPU |

All rows were `FRONT`. Requested and realized render extents matched. Swap
storage ranged from 169.5 to 188.3 MiB per megapixel. The 4K-to-1080p swap
ratio was 3.905 against a 4.000 pixel ratio, so extent and swap accounting
agree.

## Absolute threshold result

FPS and 1% low are lower bounds. GPU milliseconds are upper bounds.

| res | FPS minimum | measured | headroom | 1% minimum | measured | headroom | GPU maximum | measured | headroom | result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 1112.5 | 1197.8 | +85.3 | 483.6 | 616.1 | +132.5 | 0.485 | 0.468 | 0.017 | PASS |
| 960x540 | 1026.6 | 1083.0 | +56.4 | 470.1 | 579.4 | +109.3 | 0.553 | 0.551 | 0.002 | PASS |
| 1280x720 | 929.2 | 981.3 | +52.1 | 437.7 | 545.6 | +107.9 | 0.640 | 0.630 | 0.010 | PASS |
| 1920x1080 | 763.2 | 809.9 | +46.7 | 388.8 | 503.5 | +114.7 | 0.829 | 0.794 | 0.035 | PASS |
| 2560x1440 | 614.3 | 648.3 | +34.0 | 337.1 | 432.0 | +94.9 | 1.102 | 1.065 | 0.037 | PASS |
| 3840x2160 | 504.0 | 520.6 | +16.6 | 318.1 | 494.3 | +176.2 | 1.946 | 1.898 | 0.048 | PASS |

The narrowest gate is 960x540 GPU time, which passed by 0.002 ms. The
candidate nevertheless met the stated absolute grade exactly as defined: no
margin was added to or subtracted from any threshold.
