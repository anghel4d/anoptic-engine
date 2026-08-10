# CLAUDE.md

## What this is
Anoptic Engine. A SOTA C++26 game engine for million-entity simulation.
We implement the very pinnacle of the modern SOTA techniques and algorithms, without reservation. We believe Agentic Code Assistants make it possible to turn our years of experience and knowledge into real, proven deliverables.
Keep reading to get a grasp of how we ship fast, clean, idiomatic systems-level code.

## Build
Read flake.nix and CMakelists.txt

## Reflection

Reflection is the default for closed-world structural metaprogramming. Use reflection for structural discovery and orchestration, `consteval` for computation, templates for reusable kernels, and explicit code for runtime behavior. Do not encode program structure through template metaprogramming when C++26 reflection can inspect it directly.

## Benchmark scope

Unless explicitly instructed to run a huge benchmark, do not run one.

- Start with one representative resolution and one quick A/B.
- Use the driver minimum of 2 pairs only when statistical confirmation matters.
- Expand pairs or resolutions only if the result is ambiguous or resolution-dependent.
- Never default to the full sweep.

## Constraints
- C23. It has cool features, use them.
- Use worktrees/ or scratch/ for temporary files.
- We use a platform abstraction layer, such that any platform-specific utility or even memory allocation should use the appropriate header from include/anoptic_xxxx.h and an ano_xxxxx() type call. Robust tooling.
- Module layout: `include/anoptic_<module>.h` is a module's PUBLIC interface: `ano*()` declarations and platform-agnostic types ONLY. The matching `src/<module>/` folder is the implementation and MAY contain private headers (e.g. `src/threads/threads_macos.h`) included only by files within that module. Never leak runtime implementation details.
- Platform code lives in `src/<module>/`: a common `<module>.c` always compiles; the module's `CMakeLists.txt` selects the per-platform file (`<module>_win64.c` / `<module>_macos.c` / `<module>_linux.c`). (APPLE before UNIX: both are true on macOS). Callers only ever `#include <anoptic_<module>.h>` and call `ano*()`; they never see which platform file was built.
- anoptic_memory.h replaces and overloads the default glibc allocator.
- No heavyweight deps. No frameworks.

## Writing Style
- Heavenly Blessings ( 天官賜福 ) grace this codebase.
- Use 〜 instead of the em-dash.
- Comments should follow the existing convention and generally be constrained to the top of functions: Inputs and their types, outputs and their types, invariants.
- Commments inside of functions should be extraordilarily terse and to the point.
- Write markdown the same way: flat, terse prose with no decorative bolding. Bold only academically and selectively for load-bearing terms.
- One long line per paragraph or list item. Let the editor soft-wrap. Do not hard-wrap prose at a column.
- Preserve the author's voice and his own comments verbatim. Tighten, don't rewrite.

## Constraints
- Always check if a directory you're working in has a .md file. 
- Do NOT add yourself as a contributor.
- Never put product attribution in git: no Cursor/Claude/Copilot/ChatGPT/etc. `Co-authored-by`, `Claude-Session`, `Made-with`, or any other AI/tool credit trailer, badge, or banner.
- Commit and push freely. Do not delete branches without explicit approval.

## Current step
Read docs/conventions.md then check the latest 3 commits.
