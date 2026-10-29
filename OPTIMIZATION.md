# Optimization research

Reviewed 2026-10-29. These are candidate experiments, not promised speedups.
Results reported by other projects have not been reproduced for Hot Pursuit 2.

## References and what transfers

| Project | Useful idea | Applicability here |
|---|---|---|
| [Ace Combat 5 native renderer](https://github.com/sal063/Ace-Combat-5-Static-recompilation) | Recognized 3D paths use native GPU meshes; other rendering keeps a fallback | A concrete model for migrating selected NFS rendering paths incrementally. |
| [God of War Native Port](https://github.com/Waffle565/God-Of-War-Native-Port/blob/main/docs/native-port.md) | Native GPU render targets/textures and selected native service replacements | A possible larger rendering project; its renderer is specific to another runtime and game. |
| [PS2Recomp](https://github.com/ran-j/PS2Recomp#game-override-hooks) | Overrides selected by executable identity and function address | A useful design for replacing a verified hot routine while preserving a fallback. |
| [PS2 Web Recomp](https://github.com/snuri00/ps2-web-recomp#lessons-from-making-it-fast) | Batch uploads and draws, cache repeated uploads, keep shader setup uniform | Our runtime already has upload hoisting, an upload cache, and a compiled VU path. Measure remaining costs before duplicating those changes. |
| [God of War performance methodology](https://github.com/KIexster/god-of-war-recomp/blob/main/docs/RENDIMIENTO.md) | Separate game progress, host presentation, and instrumented timings | Use equivalent scenes and repeated alternating runs; heavy GPU profiling can change the rate being measured. |

The God of War port also compiles its IOP modules and uses a native movie
decoder. Those are useful examples, but neither establishes that the same
changes would help this game. The menu's codec must be identified before
choosing a decoder. Our earlier race sample spent about 1 ms/frame in the
IOP/sound/disc path, so replacing that entire path is a lower priority for FPS.

## Ace Combat 5: a particularly relevant example

Its [documented renderer](https://github.com/sal063/Ace-Combat-5-Static-recompilation#ps2recomp-ace-combat-5)
draws supported 3D geometry as GPU meshes with vertex shaders, while menus and
unsupported effects retain the GS path. This suggests an incremental experiment
for NFS: select one well-understood car or track rendering path and retain the
current renderer for everything else.

The [VU program dispatcher](https://github.com/sal063/Ace-Combat-5-Static-recompilation/blob/main/runtime/src/rn/rn_vp.c)
matches the loaded microprogram, supports native implementations that can decline
unsupported cases, and has a verification mode comparing output packets against
the original VU execution. That match/verify/fallback pattern is more useful here
than copying game-specific addresses or aircraft rendering code.

Its [native triangle path](https://github.com/sal063/Ace-Combat-5-Static-recompilation/blob/main/runtime/src/rn/rn_tri3d.c)
similarly leaves a draw on the existing path when native handling is unavailable.
A port of that architecture would require new NFS-specific analysis and code;
this is a research direction, not an implemented or benchmarked NFS speedup.

## Proposed order

1. **Capture a representative menu and race workload.** Record CPU samples,
   draw/batch counts, uploads and time waiting for rendering. Keep FPS runs
   separate from image captures and detailed GPU traces. Record power state,
   binary hash, input sequence and checkpoint identity locally.
2. **Measure renderer command overhead.** Investigate redundant state updates,
   batch breaks and transfers. Preserve writes with side effects and ordering
   dependencies. Compare images against the existing renderer.
3. **Identify hot menu routines.** Determine which work is decoding, copying or
   synchronization. Prototype one native replacement, gated by the exact ELF
   identity. Compare outputs, register state and guest-time behavior against
   the translated routine before enabling it.
4. **Evaluate a native hardware-rasterization backend.** This has larger scope
   than compiler tuning. Preserve texture formats, palettes, blending,
   framebuffer feedback and depth behavior; keep the existing backend as a
   reference while bringing up menus, cars, track, HUD and effects separately.
5. **Revisit IOP compilation only if it becomes significant** in the measured
   scene or loading workload.

Compiled VU coverage was complete in the sampled race. The current generator
also performs flag liveness and pipeline-state analysis; compiling already
compiled programs again is not an established optimization.

## Existing measurements and boundaries

The updated performance preset improved short battery-powered checkpoint runs
from about 45 to 50–52 video frames/s in the menu and 29 to 40–41 in the race.
The default sharpening filter was a material cost. CPU specialization,
CPU-specific flags, higher inlining thresholds and ThinLTO did not produce a
repeatable menu improvement in the experiments performed so far.

The GPU meter measures compute passes and excludes final display filtering.
Its native timestamp conversion is patched to account for the queue's timestamp
period. Low compute utilization alone is not evidence that presentation is cheap.

Do not improve a benchmark by changing guest clock rates, skipping required
work, muting audio, or counting repeated presentations as new game frames.
Widescreen and 60-fps game patches change the workload and need separate tests.

## Reuse and repository policy

PS2 Web Recomp and this runtime use MIT licensing; PS2Recomp and the cited God
of War native runtime use GPL-3.0. Ace Combat 5's repository declares Apache-2.0.
Review the applicable license before reusing
code; the runtimes are not interchangeable. This document records architecture
and experiment ideas, and imports no code from those projects.

Keep traces, captures, game-derived source, modules, executables and checkpoints
local. The public repository contains setup code and documentation only.
