# Result API benchmark and paired regression test - 2026-08-14

## Change under test

- A: parent commit `cf31d66cf100c9809574ff30d927e8e3bc9f4c0d`.
- B: Result conversion commit `b451b41bcf79a9fdd7199c35c08aea65f638d543`
  on `hyper-c-resourcemgr`.
- Diff: 123 files, 5,248 insertions, 5,314 deletions, net **-66 lines**.
- Scope: fallible public and internal boundaries now return the typed
  `ano::Result` algebra; call sites consume its success and error alternatives
  directly. Reflected and constant-evaluated Result metadata remains compile
  time data.

## System

- CPU: AMD Ryzen 9 5950X, 16C/32T, AM4, 3.4 GHz base.
- GPU: NVIDIA GeForce RTX 4090, 24 GiB, driver 572.61
  (Windows `32.0.15.7261`, 2025-02-25).
- RAM: 64 GiB, 4x16 GiB Corsair `CMK32GX4M2D3600C18`, DDR4-3600.
- Motherboard: Gigabyte X570S AORUS PRO AX, BIOS F5b.
- OS: Windows 11 Pro 25H2, build 26200.8875, x64.
- Display: 3840x2160 at 119 Hz, 150% scale.

## Runtime environment

- Engine: Anoptic at commits A and B above, branch `hyper-c-resourcemgr`.
- Build: Release `-O3 -DNDEBUG`, CMake + Ninja, MSYS2 UCRT64 GCC 16.1.0
  Rev5, x86-64 Windows target, LTO.
- Renderer: Vulkan, mesh shaders enabled, 4x MSAA,
  `VK_PRESENT_MODE_IMMEDIATE_KHR`, unthrottled.
- Scene: Sponza, Viking room, candle holders and dynamic lights, reflected text
  HUD, shadow-map mode, 2 of 42 shadow frusta per frame, HUD menu open.
- Harness: `tools/perf/bench_fps_win64.py`, full display-derived ladder,
  30 seconds per process, elapsed-time warmup dropped, per-window medians,
  foreground-verified.
- Comparison: exact adjacent A/B executables, two pairs per resolution in the
  driver's balanced ABBA/BAAB order with Student-t 95% confidence intervals.
- Display state: monitor awake for the complete survey and comparison.

`ENV_VARS: ANO_SHADOW_BUDGET=2`

## Window manager

Desktop Window Manager (DWM), composited.

## Mode

Borderless windowed `WS_POPUP`, physical framebuffer-sized client area,
per-monitor DPI aware. The 3840x2160 row fills the native desktop.

## B standalone full sweep

| res | swap MiB | wall fps | p50 | 1% low | 0.1% low | max ms | GPU ms | GPU cap | wall/cap | frusta | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 640x360 | 41.4 | 1112.5 | 1109.6 | 483.6 | 406.3 | 2.805 | 0.485 | 2062 | 0.54 | 2.0 | CPU/present |
| 960x540 | 97.6 | 1026.6 | 1026.3 | 470.1 | 399.0 | 3.436 | 0.553 | 1808 | 0.57 | 2.0 | CPU/present |
| 1280x720 | 164.7 | 929.2 | 932.7 | 437.7 | 375.9 | 3.119 | 0.640 | 1562 | 0.60 | 2.0 | CPU/present |
| 1920x1080 | 360.1 | 763.2 | 767.6 | 388.8 | 345.1 | 6.143 | 0.829 | 1206 | 0.64 | 2.0 | CPU/present |
| 2560x1440 | 642.3 | 614.3 | 616.6 | 337.1 | 305.3 | 3.838 | 1.102 | 907 | 0.68 | 2.0 | CPU/present |
| 3840x2160 | 1406.3 | 504.0 | 504.0 | 318.1 | 307.7 | 3.487 | 1.946 | 514 | 0.98 | 2.0 | GPU |

All rows were `FRONT`; requested, rendered, and swapchain extents matched.
Swap storage ranges from 169.5 to 188.3 MiB per megapixel. The 4K/1080p
swap ratio is 3.905 versus a 4.000 pixel ratio, so render and swap accounting
agree.

## Historical absolute context

The prior monitor-awake sweep at commit `8bd64e857f86e0d68dd96601c3c6740b4c23f10f`
is the supplied reference bar. A standalone comparison shows session-level
drift, but cannot attribute it to a revision. The exact paired comparison below
is the regression test.

| res | historical fps | B standalone fps | difference | historical GPU ms | B GPU ms | difference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 640x360 | 1196.8 | 1112.5 | -7.0% | 0.464 | 0.485 | +4.5% |
| 960x540 | 1084.0 | 1026.6 | -5.3% | 0.547 | 0.553 | +1.1% |
| 1280x720 | 979.1 | 929.2 | -5.1% | 0.629 | 0.640 | +1.7% |
| 1920x1080 | 817.9 | 763.2 | -6.7% | 0.786 | 0.829 | +5.5% |
| 2560x1440 | 649.0 | 614.3 | -5.3% | 1.066 | 1.102 | +3.4% |
| 3840x2160 | 520.8 | 504.0 | -3.2% | 1.900 | 1.946 | +2.4% |

## Paired raw observations

Every process was foreground-verified. Requested and realized render extents
matched in every row. DWM is the one-Hz aggregate Windows GPU Engine counter.

| pair | order | rev | res | fps | p50 | 1% low | 0.1% low | max ms | GPU ms | DWM % |
| ---: | :---: | :---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | AB | A | 640x360 | 1115.4 | 1117.5 | 490.8 | 410.2 | 3.739 | 0.482 | 13.33 |
| 1 | AB | B | 640x360 | 1121.9 | 1123.0 | 488.9 | 415.0 | 3.132 | 0.480 | 13.37 |
| 2 | BA | B | 640x360 | 1116.2 | 1114.0 | 485.3 | 406.0 | 2.766 | 0.482 | 13.43 |
| 2 | BA | A | 640x360 | 1117.7 | 1121.8 | 479.2 | 402.4 | 2.988 | 0.481 | 13.44 |
| 1 | AB | A | 960x540 | 1016.4 | 1019.3 | 445.4 | 381.2 | 3.449 | 0.554 | 13.91 |
| 1 | AB | B | 960x540 | 1023.3 | 1023.2 | 456.3 | 380.1 | 3.439 | 0.554 | 13.74 |
| 2 | BA | B | 960x540 | 1019.0 | 1021.6 | 448.8 | 386.3 | 3.758 | 0.557 | 13.98 |
| 2 | BA | A | 960x540 | 1021.0 | 1024.5 | 455.6 | 392.0 | 3.024 | 0.556 | 14.02 |
| 1 | AB | A | 1280x720 | 925.2 | 925.6 | 435.4 | 376.8 | 3.024 | 0.639 | 14.38 |
| 1 | AB | B | 1280x720 | 930.1 | 930.9 | 437.0 | 384.2 | 3.047 | 0.639 | 14.40 |
| 2 | BA | B | 1280x720 | 928.2 | 929.1 | 437.1 | 374.1 | 3.366 | 0.641 | 14.53 |
| 2 | BA | A | 1280x720 | 925.6 | 928.5 | 437.1 | 383.1 | 3.895 | 0.640 | 14.46 |
| 1 | AB | A | 1920x1080 | 766.7 | 767.4 | 393.2 | 344.8 | 4.749 | 0.829 | 15.74 |
| 1 | AB | B | 1920x1080 | 765.3 | 766.1 | 389.3 | 340.1 | 3.282 | 0.833 | 15.84 |
| 2 | BA | B | 1920x1080 | 764.9 | 765.9 | 394.6 | 340.1 | 3.629 | 0.831 | 15.86 |
| 2 | BA | A | 1920x1080 | 763.6 | 764.3 | 395.3 | 349.5 | 3.231 | 0.835 | 15.84 |
| 1 | AB | A | 2560x1440 | 612.8 | 613.4 | 341.5 | 308.3 | 4.899 | 1.111 | 17.78 |
| 1 | AB | B | 2560x1440 | 615.0 | 617.5 | 338.5 | 303.1 | 3.946 | 1.104 | 17.84 |
| 2 | BA | B | 2560x1440 | 614.9 | 617.3 | 348.6 | 308.3 | 4.161 | 1.106 | 17.75 |
| 2 | BA | A | 2560x1440 | 613.2 | 613.5 | 334.8 | 305.0 | 4.402 | 1.107 | 17.98 |
| 1 | AB | A | 3840x2160 | 502.1 | 504.6 | 324.0 | 312.5 | 3.765 | 1.949 | 0.01 |
| 1 | AB | B | 3840x2160 | 503.8 | 505.7 | 328.9 | 315.0 | 3.586 | 1.946 | 0.01 |
| 2 | BA | B | 3840x2160 | 502.7 | 505.1 | 329.3 | 317.5 | 3.527 | 1.946 | 0.01 |
| 2 | BA | A | 3840x2160 | 502.0 | 503.9 | 324.1 | 313.0 | 3.522 | 1.950 | 0.01 |

## Paired comparison

A is the exact parent; B is the Result conversion. Two adjacent pairs were run
per resolution. Every interval includes zero, so the required verdict is
neutral at all six resolutions.

| res | A fps | B fps | B - A | paired 95% CI | A DWM % | B DWM % | verdict |
| --- | ---: | ---: | ---: | --- | ---: | ---: | --- |
| 640x360 | 1116.56 | 1119.02 | +0.22% | [-4.36%, +4.80%] | 13.38 | 13.40 | neutral |
| 960x540 | 1018.68 | 1021.20 | +0.25% | [-5.33%, +5.82%] | 13.96 | 13.86 | neutral |
| 1280x720 | 925.41 | 929.14 | +0.40% | [-1.18%, +1.98%] | 14.42 | 14.46 | neutral |
| 1920x1080 | 765.14 | 765.08 | -0.01% | [-2.24%, +2.22%] | 15.79 | 15.85 | neutral |
| 2560x1440 | 613.01 | 614.96 | +0.32% | [-0.06%, +0.69%] | 17.88 | 17.79 | neutral |
| 3840x2160 | 502.06 | 503.28 | +0.24% | [-1.02%, +1.51%] | 0.01 | 0.01 | neutral |

The simple mean of the six FPS point estimates is **+0.24% for B**. That is a
useful direction check, not a claim of a measured speedup, because the paired
confidence intervals include zero.

## Secondary metrics

These are descriptive two-run means; the driver's formal confidence interval
is for wall FPS.

| res | A 1% low | B 1% low | difference | A GPU ms | B GPU ms | difference |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 640x360 | 485.00 | 487.10 | +0.43% | 0.4815 | 0.4810 | -0.10% |
| 960x540 | 450.50 | 452.55 | +0.46% | 0.5550 | 0.5555 | +0.09% |
| 1280x720 | 436.25 | 437.05 | +0.18% | 0.6395 | 0.6400 | +0.08% |
| 1920x1080 | 394.25 | 391.95 | -0.58% | 0.8320 | 0.8320 | 0.00% |
| 2560x1440 | 338.15 | 343.55 | +1.60% | 1.1090 | 1.1050 | -0.36% |
| 3840x2160 | 324.05 | 329.10 | +1.56% | 1.9495 | 1.9460 | -0.18% |

## Verdict

The Result conversion introduces **no measured performance regression**.
Exact paired builds are neutral across the complete resolution ladder. Five of
six FPS point estimates favor B, the remaining result is effectively zero,
1% lows favor B in five of six rows, and average GPU-pass time differs by no
more than 0.36% in either direction.

The historical absolute bar is not reproduced in this session, but the exact
parent reproduces the same present-session throughput as B. The absolute shift
therefore belongs to session or external state, not to the Result conversion.
No runtime repair or standard-library expansion is justified by these data:
the typed alternatives, reflected metadata, and constant-evaluated helpers are
optimized away from the measured render path as intended.
