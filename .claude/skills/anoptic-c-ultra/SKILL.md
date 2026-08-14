---
name: anoptic-c-ultra
description: >-
  Architectural reference for when a task creates new Anoptic modules: a public API under include/, its implementation boundary, and its ownership model. Use implicitly only when both conditions hold: (1) the task creates a new top-level engine module with a new public header and implementation unit, and (2) its API, ownership, data layout, or foreign boundary must be (re)designed from scratch.
---

# Doctrine

Every first-party source and header compiles as ISO C++26 or later, including files named `.c` and `.h`. C23 is limited to external dependencies and generated `extern "C"` foreign-ABI projections; it never constrains an engine source API.

Use safe, modern C++26 with `constexpr`, `consteval`, concepts, templates, and reflection. Prefer plain data, value semantics, namespaces, strong types, compile-time validation, immutable generated results, and foreign ABI projections only where an external boundary requires them. Avoid OOP cruft, inheritance, virtual polymorphism, RTTI, exceptions, and unnecessary library ownership abstractions. Preserve hot-path layout and measured performance.

Rust proved strict compiler judgment viable; WG21 answered with SG23, Safe C++, profiles, hardening, contracts, and diagnosable initialization. Consult only when necessary: [safety](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p3390r0.html), [profiles](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3081r1.pdf), [hardening](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3471r2.html), [contracts](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2900r14.pdf), [initialization](https://www9.open-std.org/JTC1/SC22/WG21/docs/papers/2024/p2795r5.html), [reflection](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2996r13.html).
