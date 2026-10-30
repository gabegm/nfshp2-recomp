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

## Graphics profile on AC power

Measured on 2026-10-29 with the i5-8350U/UHD 620, Vulkan, AC connected,
CPU energy preference `performance`, 1x internal resolution, Crisp filtering,
texture predecode off and the separate VU thread disabled. The standard 4:3
race checkpoint starts at frame 3000; holding Cross through frame 3240 gives
201 logged frames after discarding the initial warm-up. The menu checkpoint
starts at 1800 and ends at 2160, with 301 frames retained. These are short,
scene-specific measurements; thermal state and desktop load can affect them.

| Race configuration | Video frames/s |
|---|---:|
| Default 8x8 raster tiles, first/repeated runs | 38.89 / 38.64 |
| 4x4 raster tiles, intervening repeated runs | 41.95 / 41.21 |

Menu results were 49.29 fps with 8x8 tiles and 49.90 with 4x4, too close to
claim a meaningful menu improvement. A preliminary 16x16 race test was slower
(31.39 fps). Immediate presentation did not show a substantial improvement
over Mailbox. The performance preset now selects 4x4 tiles; `PS2_TILE=8` restores
the upstream size. No geometry, guest timing or required graphics work is skipped.

Separate instrumented runs showed:

- The menu's sampled compute pass averaged about 19.5 ms. The final Crisp
  display render pass averaged about 0.55 ms in both scenes.
- In the race, processing GIF messages on the rendering thread averaged
  8.6 ms per picture with 8x8 tiles. Acquiring a screen buffer averaged 14.6 ms;
  submission averaged 2.5 ms and the present call about 0.03 ms. These are
  CPU wall times; the buffer wait can expose earlier GPU work or compositor
  delays and is not an independent shader duration.
- With 4x4 tiles, race GIF processing rose to 10.5 ms, while buffer acquisition
  fell to 10.2 ms. This trades more CPU tile setup for less GPU work.
- Detailed dispatch traces attributed about 72% of measured race compute-pass
  time and 92% of menu compute-pass time to `raster`. The remaining race work
  was mostly uploads and palette loads. This profiler splits dispatches into
  separate passes and waits for results, so these shares identify candidates;
  they are not a breakdown of ordinary frame time.

This points to raster shader paths, screen-filling menu sprites, texture
sampling and overdraw as the next investigation. Race upload/palette command
overhead is a secondary target. Replacing the final display filter again or
recompiling already compiled VU programs is a lower priority with this preset.
The UHD 620 limits the current renderer, but these traces do not establish a
hardware-only limit or promise that further changes can reach 60 fps.

The graphics profiling patch adds optional asynchronous compute/display
timestamp samples and fixes the detailed profiler to include compute commands
submitted with presentation. Previously that omission produced an empty menu
trace. After rebuilding, collect a local trace with:

```sh
mkdir -p .local/graphics-profile
PS2_PRESENT_LOG=1 PS2_GPU_TIMING="$PWD/.local/graphics-profile/session.tsv" \
  bash scripts/play.sh --performance --nointro
```

`PS2_GPU_TIMING` writes `frame presentation compute_ns blit_ns`. Rows are
samples of submissions, not complete frame totals. Only rows with
`presentation=1` measure the final display render pass; zero in another row
means that pass was absent. A pending query can cause submissions to go
unsampled, and the final pending sample may be absent at shutdown. Timestamp
query support is required. Tracing is disabled unless requested.

For detailed dispatch analysis, set
`PS2_GPU_PROFILE="1800,1803,$PWD/.local/graphics-profile/dispatches.tsv"`, choosing
an interval in the scene of interest and an output path without spaces. Keep
these runs separate from FPS comparisons. Do not combine the detailed profiler
with asynchronous timing when interpreting durations, since the detailed mode
changes pass boundaries. `PS2_PRESENT_LOG=1` logs CPU presentation stages;
`PS2_HITCH_LOG=1000` together with `PS2_HITCH_PICTURES=/absolute/private/path.tsv`
also records per-picture message processing and buffer waits.

Short deterministic checks compared menu, race and widescreen race pictures,
guest memory and audio with 8x8 versus 4x4 tiles and with profiling enabled.
All sampled outputs matched byte for byte. Those checks cover the sampled
scenes, not every track or effect. Captures, checkpoints and detailed traces
remain ignored under `.local/`.

## Raster shader specialisation

Follow-up experiments on the same UHD 620 specialise the primitive kind and
feature flags at GPU pipeline creation. The compiler can then remove paths
that the batch cannot use, while interpolation, texture sampling, blending,
depth behaviour and draw order keep the existing implementations.

The expensive menu layers are mainly triangles, despite looking like sprites.
A sprite-only shader showed no meaningful gain. Specialising both kind and
flags improved the menu, but broad application did not give a repeatable race
gain. The retained implementation therefore requires at most 32 primitives,
one kind throughout the batch, and identical feature flags in every setup.
Six precompiled variants cover triangles/sprites with three observed common
flag combinations. Other states, mixed batches and larger batches use the
generic shader. No game executable addresses or translated game code are
part of this patch.

The final small-batch experiment measured 57.96 and 57.39 menu video frames/s,
compared with 50.23 in the preceding generic run. An earlier unrestricted
comparison gave 58.42/58.58 versus 51.69/51.07. One later generic menu run
dropped to 41.62 with unusually high audio waits (9.49 ms/frame), illustrating
why these short runs should not be read as an exact universal speedup.
The small-batch race comparison was 41.87 with specialisation versus 42.09
without it: effectively unchanged. Full-speed 60 fps is still unverified.

`--performance` enables the feature with `PS2_RASTER_SPECIALIZE=1`; setting it
to `0` selects the generic shader. The runtime itself defaults to generic
unless requested. Older custom WGSL files without the new override declarations
also retain the generic path. Additional pipeline compilation can increase
startup time. GPU statistics report which specialised batch states were used.
The detailed profiler and captured-work GPU benchmark select the same pipelines
as normal rendering.

Checks compare deterministic menu, race and widescreen race images, guest
memory and audio against the previous renderer over 120-frame runs. Native
and 2x resolution also receive shorter shader comparisons. These checks cover
the sampled scenes; other tracks, effects, GPU drivers and full-game behaviour
remain unverified. All game-derived outputs stay private under `.local/`.

## Racing follow-up

The race trace's dominant large-triangle state uses flags `0xEA4B`, which was
absent from the menu specialisations. The racing patch adds that state for
batches whose primitives and flags all match; the 32-primitive limit stays in
place for the other specialisations. `PS2_RACE_SPECIALIZE=1` requests the new
pipeline and is enabled by `--performance`. Setting it to `0` retains the
previous state selection. It also requires `PS2_RASTER_SPECIALIZE=1` and a
compatible shader. Other states keep their existing paths.

A second change handles undecoded bilinear PSMT8 textures directly. It shares
page-coordinate calculations across the four taps, uses the same swizzle
table, and preserves wrapping, palettes, snapshot reads, sample selection,
colour expansion and interpolation. Other texture formats and predecoded
textures retain their previous sampling paths. This built-in shader change
also runs when racing-state specialisation is disabled.

Alternating short runs gave about 42 fps with the previous preset and 44 fps
with both changes. For a longer comparison, the same checkpoint at frame 3000
was driven with Cross held through frame 3600. Discarding warm-up through
frame 3039 left 561 video-frame timings per run:

| Configuration | First run | Repeated run |
|---|---:|---:|
| Previous preset | 45.13 fps | 45.26 fps |
| Racing state plus indexed-texture sampler | 47.29 fps | 46.86 fps |

That is approximately a 4% gain in this longer segment. Its later frames are
cheaper than the initial 240-frame sample, so comparing the earlier 42 fps
directly with the new 47 fps would overstate the improvement. The menu sample
measured 57.60 fps before and 59.54 afterwards. These measurements are still
specific to this scene, machine and driver; full-speed racing is unverified.

A final launcher comparison of the archived previous binary against the
updated built-in shader measured 44.83 versus 46.70 fps over the same 561
retained frames. Standard and widescreen launch checks passed, as did explicit
generic fallback and legacy custom-shader checks.

The state and sampler were also tested separately, each producing a small gain.
Specialising large untextured fill batches did not show a useful improvement
and is excluded. No filtering quality, depth checks, draw ordering or guest
clock rates were reduced to obtain these results.

Validation compares menu, standard race, widescreen race and 2x race outputs
against the archived previous renderer, including images, guest memory and
audio. The standard race comparison spans 600 guest frames. Profiling-mode
outputs are checked too. All game-derived captures and traces remain private.

Because the new patch changes context introduced by earlier patches, build
setup now stages the entire runtime patch series privately. It removes
installed patches in reverse order there, then verifies and reapplies the
complete series before copying changed source files back. Unrelated edits
outside patch context are preserved; conflicts fail before checkout files
are updated. Unchanged sources keep their timestamps. Synthetic conflict,
upgrade, repeat-build and symlink tests accompany this helper, and the real
patch stack is checked against clean and previously patched source trees.

## Refreshed race profiles and PSMT4 sampling

The updated renderer was profiled again on 2026-10-30, using the existing
frame-3000 checkpoint and a new private checkpoint at frame 4200. Holding
Cross for the intervening 1200 guest frames produces a later road-curve view
on the same track, following the initial pack and acceleration. This expands
the sampled workload; it does not cover other tracks, dense police traffic,
weather or every particle effect.

The per-dispatch trace attributed 67.1% of measured GPU time to raster passes
in the initial scene and 63.5% in the later one. Uploads and palette loads
accounted for most of the rest. These are intrusive profiler shares, not a
breakdown of ordinary frame time. Decoding the draw metadata showed that the
dominant triangle states read PSMT4 (`0x14`) textures; the preceding sampler
optimisation covered PSMT8 (`0x13`).

The retained patch adds a direct bilinear path for undecoded standard PSMT4
textures. It shares 128x128 page calculations between the four taps and uses
the same nibble swizzle table, palette bank, colour expansion, snapshot reads,
coordinate wrapping and interpolation. Predecoded textures and other formats
keep their previous paths. Filtering quality and guest timing are unchanged.

Alternating 600-frame runs on the initial checkpoint, discarding the first
40-frame warm-up, gave:

| Configuration | First run | Repeated run |
|---|---:|---:|
| Previous renderer | 46.47 fps | 46.52 fps |
| Direct PSMT4 sampler | 48.49 fps | 48.42 fps |

That is another roughly 4% improvement in this workload. The busier initial
240-frame sample measured 43.90/43.94 fps before and 46.01/45.29 afterwards.
A 600-frame run from the later checkpoint measured 58.57 versus 59.09 fps;
that lighter scene was already close to the guest's full-speed rate. These
results do not establish full-speed racing throughout the game.

The final native launcher check measured 46.72 versus 48.62 fps over the same
561 retained early-race frames. Standard and widescreen launch checks passed.

Other experiments were discarded: moving bounding-box or triangle rejection
ahead of setup loading, reducing redundant palette copies/barriers, and a
guarded 24-bit depth-write path did not show a clear enough overall gain.
The experimental C++ changes were restored, and the pre-patch binary matched
the archived baseline byte for byte before testing the retained shader path.

Validation compares the built-in shader against archived standard/widescreen
binaries for a 600-frame initial race, 120-frame later race and menu samples,
widescreen and 2x race samples. Images, guest memory and audio are checked,
including profiling mode. Captures, checkpoints and detailed traces remain
private. The generic shader with texture predecode enabled also matched the
archived renderer. The same patch-stack staging helper handles fresh builds, upgrades
and repeat builds; no additional launch arguments are required.

## Reuse and repository policy

PS2 Web Recomp and this runtime use MIT licensing; PS2Recomp and the cited God
of War native runtime use GPL-3.0. Ace Combat 5's repository declares Apache-2.0.
Review the applicable license before reusing
code; the runtimes are not interchangeable. This document records architecture
and experiment ideas, and imports no code from those projects.

Keep traces, captures, game-derived source, modules, executables and checkpoints
local. The public repository contains setup code and documentation only.
