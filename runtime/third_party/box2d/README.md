# Box2D

A 2D physics engine for games by Erin Catto, version 3, written in C17, under the MIT
licence (`LICENSE`).

- Source: https://github.com/erincatto/box2d
- Copied from commit `2b14ae3` of 2026-11-16: the folders `src/` and `include/` as they
  are. Nothing was changed.

It moves the `RigidBody2D` nodes of the runtime (`runtime/core/rr_physics2d.c`).

## How it is built

Every platform's Makefile compiles the files in `src/` with a rule of their own, apart
from the runtime's code, with

```
-std=c17 (gnu17 on the consoles) -O2 -w -DNDEBUG -DBOX2D_DISABLE_SIMD -Iinclude -Isrc
```

- `BOX2D_DISABLE_SIMD`: the engine's SSE2, AVX2 and NEON paths are off, on the PC too, so
  that the PC runs the code the consoles run.
- `-w`: the engine's code is not held to the runtime's warnings.
- `-Wno-incompatible-pointer-types` on the consoles: where `int32_t` is `long`
  (KallistiOS), `dynamic_tree.c` passes an `int32_t *` for an `int *`, which newer
  compilers take as an error. Both are 32 bits there.
- The engine can spread its work over threads. The runtime does not ask for that: it
  steps the world on its own thread.
- The engine takes its memory from `malloc`: when a world and its bodies are made, and
  when one of its buffers grows, which is in the first steps of a scene.

To take a newer version, copy `src/` and `include/` again; `rr_physics2d.c` is the only
file that uses the engine.
