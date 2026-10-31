# Racing performance plan

The target is full-speed guest execution with stable audio and pacing on the
current AC-powered i5-8350U/UHD 620 laptop. Native compilation alone does not
remove graphics emulation costs. Normal play still uses the verified GS
renderer; the experimental native renderer has no demonstrated racing FPS win.

## Milestone 1: reproducible baseline (completed)

The earlier SDL window-creation stall no longer reproduces in this desktop
session. A minimal SDL probe and real windowed game runs both succeed. Its
cause remains unproven; no speculative startup workaround was installed.

`scripts/benchmark-performance.py` runs a private checkpoint with the established
performance preset, fixed input/RTC and a real window, Vulkan GPU and audio
device. It rejects incomplete logs, software rendering, missing/dummy audio,
GPU errors and experimental native rendering. It clears inherited PS2 experiment
variables. With `--baseline`, two rounds alternate A/B/B/A. Without it, repeated
runs measure the current binary. No screenshot capture or detailed GPU profiler
runs during FPS measurement. Do not run benchmarks alongside builds or other
GPU experiments.

Each private report records executable/checkpoint hashes, adapter identity,
power/thermal/CPU-policy snapshots, run order and raw logs. It reports guest
vblanks per wall second, guest speed, frame-time percentiles, adjacent two-vblank
intervals, VU interpretation, audio starvation and queue occupancy. The verified
US runtime advances guest video at 60 Hz; these rates do not count unique game
images or imply a 60-fps rendering patch. Two-vblank intervals expose pacing
without treating alternating render/non-render ticks as independent game images.

```sh
python3 scripts/benchmark-performance.py \
  --checkpoint .local/ps2-native-recomp/games/nfshp2/performance/race.ps2state \
  --start 3000 --output .local/benchmarks/my-early-race
```

The example checkpoint is private development data, not distributed. Other
locally created checkpoints can be supplied with their saved frame number.
Use `--scene menu` without acceleration input, and `--widescreen` with a matching
widescreen checkpoint. Output must be a new directory under ignored `.local`.
Default runs cover 600 guest vblanks and discard the first 40. Raw profiling
and game-derived data remain private.

Measured 2026-10-31, AC online and CPU energy preference `performance`:

| Scene | Current guest vblanks/s, two runs | Guest speed | Two-vblank p99, two runs |
|---|---:|---:|---:|
| Early 4:3 race | 50.89 / 50.63 | 84.8% / 84.4% | 108.61 / 106.84 ms |
| Later 4:3 race | 58.18 / 58.43 | 97.0% / 97.4% | 64.12 / 63.57 ms |
| Menu | 59.66 / 59.50 | 99.4% / 99.2% | 43.61 / 46.97 ms |
| Early widescreen race | 50.13 / 50.62 | 83.6% / 84.4% | 85.13 / 91.97 ms |

The archived early-race binary measured 50.59/50.89 in alternating runs: no
measured regression or improvement from the disabled native experiments.
Every run had zero interpreted VU calls and zero logged post-warmup audio
starvation events; each had one whole-run startup starvation. Audio stretching
can conceal slow guest execution, so those audio results do not establish
full speed. Logged starvation events can be throttled; the whole-run audio
summary is retained separately. Pacing tails remain a problem even when the
average rate approaches full speed. Temperature snapshots are boundary samples,
not continuous throttling measurements.

The early race needs roughly a 15% reduction in average time per guest vblank
just to reach 60, plus headroom to handle spikes. Hardware insufficiency is not
yet established: different scenes behave differently on the same hardware.

## Milestone 2: measure useful native coverage (completed)

Classify accepted GS primitives with the actual native renderer guard, then
associate eligibility counts with per-dispatch GPU timestamps. Report fully
eligible, mixed and unsupported raster batches. Do not apportion a mixed
batch's GPU time by its triangle count or bounding-box area. Intrusive GPU
profiling is separate from windowed performance measurements and cannot be
interpreted as ordinary frame time.

Measured 120 guest-vblank windows with `PS2_NATIVE_COVERAGE=1` and the GPU
profiler. Raster metadata is classified per accepted primitive using the actual
native guard, not inferred from a batch's first setup. Fully eligible raster
batches are timed directly; mixed batches provide only an upper bound on work
that includes eligible primitives.

| Scene | Eligible primitives | Fully eligible raster time | Including all mixed raster time | Fully eligible compute time |
|---|---:|---:|---:|---:|
| Early race | 52.1% | 18.8% | 41.4% | 14.0% |
| Later race | 49.6% | 14.0% | 43.2% | 9.7% |
| Widescreen race | 52.5% | 21.3% | 45.3% | 15.5% |
| Menu | 0.0% | 0.0% | 0.0% | 0.0% |

These are intrusive dispatch timings, not ordinary frame-time shares or
predicted speedups. Native execution and ownership transfers still have costs.
A narrow existing-subset implementation cannot be assumed to recover the
roughly 15% of whole-frame time needed in the early race. Mixed-dispatch costs
must be measured more precisely before deciding whether that subset is enough.

The early unsupported raster time is grouped by uniform target formats:
23.2% of total raster time uses PSMCT32/PSMZ16S, 18.9% PSMCT32/PSMZ24,
11.7% PSMCT16/PSMZ16 and 4.7% PSMCT16/PSMZ16S. Opaque and masked/blended
triangle states appear among the expensive batches. Texture/flag hints in the
report describe only a batch's first setup and cannot establish every reason
for rejection. The menu's sampled raster work uses PSMZ16S, which the current
native bridge rejects; its measured normal-play speed is already near full.

The next investigation should therefore inventory *all* rejection causes in
expensive racing states and test the benefit of moving the opaque part of mixed
batches. Use that evidence alongside persistent attachment work. Do not blindly
implement broad native blending or declare the remaining limit to be hardware.
See [NATIVE_RENDERER.md](NATIVE_RENDERER.md) for commands and profile schema.
Diagnostic instrumentation is compiled into a separate runner: normal 4:3 and widescreen executables retain
exactly their pre-coverage hashes. This avoids relying on noisy disabled-hook
benchmarks to argue that instrumentation has zero cost. The regular benchmark
runner refuses the diagnostic build, even when its runtime coverage flag is off.

## Milestone 3: keep native attachments authoritative across materials

Import colour/depth once for a compatible rendering sequence. Keep native
attachments authoritative across safe material changes and resolve only when
GS fallback, an overlapping transfer/read or a target change requires it.
Maintain explicit ownership and dirty ranges for colour and depth; preserve
the upper byte of PSMZ24. Palette loads and downloads must observe the correct
version of VRAM. Start conservatively, then remove demonstrated unnecessary
resolves. Do not move expensive CPU readbacks into the normal drawing path.

Completion: batch/sequential equivalence, tested GS transitions, fewer measured
full-frame transfers and an alternating baseline/candidate racing benchmark.
Correctness without a speedup is an infrastructure milestone, not a performance
success. Keep it experimental until frame cost improves.

## Milestone 4: coherent GPU texture/palette caching

Decode/upload a texture once and reuse it until a relevant VRAM or palette
change. Include texture layout, palette bank, TEXA and other decode parameters
in identity. Invalidate on overlapping uploads, framebuffer writes, copies and
state restoration. Bound memory and test eviction and feedback explicitly.

Completion: fewer decodes/transfers and a repeatable whole-run benefit while
preserving draw ordering and reference output. Do not enable based on command
counts alone; earlier command reductions did not improve racing FPS.

## Milestone 5: cover expensive geometry, then validate sustained play

Extend the measured opaque track/car paths first. Handle unsupported blending,
reflections, shadows, HUD and effects through GS until profiles justify their
migration. Investigate pacing spikes separately if mean raster time improves
but spikes remain. Optimize CPU/VU/IOP work only when current profiles show a
material limit there. Ghidra helps identify higher-level game routines if the
remaining submission/VU cost becomes significant.

Initial acceptance target: at least 99% guest speed over repeated two-minute
race segments, no recurring post-warmup audio starvation, and a two-vblank p99
of 50 ms or better in those segments. These are test targets, not current results
or guarantees. Expand across other tracks, traffic, effects, menus, saves and
widescreen; adjust the pacing analysis if longer captures reveal a measurement
artifact. Compare native images/VRAM as well as gameplay timing: current sampled
opaque native colours can differ by up to 2/255, so bit-exact full-game rendering
is not established. Coverage/depth/alpha errors cannot be excused as colour
rounding. Default changes require broad validation and a repeatable benefit.

Do not change guest clocks, skip required work, mute sound or count repeated
presentations to manufacture a speedup. Preserve the reference renderer and
commit each validated milestone. Keep ROMs, assets, generated code, captures,
save states and binaries out of the public repository.

## PCSX2 control measurement

A same-hardware PCSX2 run is a useful external control, not an interchangeable
benchmark or a replacement for profiling this runtime. This machine has the
Flatpak PCSX2 v2.8.2 installed. The inspected global settings use 1x resolution,
a 100% normal-speed target, and no widescreen patches; performance overlays
are off. Per-game settings can override globals, so verify effective settings
in a running game.

On 2026-10-31 the user reported that PCSX2 never dropped below 100% speed while
racing, including after switching to Vulkan. This is a successful same-machine
control: the laptop can run this game at full speed through another
implementation. The exact race duration and effective per-game settings were
not logged by our benchmark runner; this is the user's observed racing result,
not an automated frame-for-frame comparison.

Use the same lawfully owned US disc, AC power, Vulkan hardware rendering,
1x resolution, original guest clocks, normal 100% speed, and no optional game
speed patches/cheats. Keep ordinary per-game compatibility fixes. Start with
4:3; repeat widescreen separately. Use the same track (Coastal Parklands),
car/camera and race-start conditions as closely as possible. Warm the shaders,
then repeat two-minute racing segments without running the recomp alongside.
Record speed percentage, image pacing/audio and available EE/GS performance
readings. Do not equate the game's image FPS with emulation speed.

PCSX2's full-speed result, alongside this runtime's roughly 85% early-race
speed, makes implementation efficiency the primary investigation. Vulkan alone
is not the fix: both paths can use Vulkan while submitting very different work.
Our compute-dispatch traces still identify rasterization and memory traffic as
large costs. This does not isolate a single cause or establish that CPU-side
submission costs cannot also matter.
Save states are implementation-specific, so the recomp's checkpoints cannot
supply an identical PCSX2 starting state. Record this comparability limit.
Keep BIOS files, ROMs, emulator logs, saves and any captures private. For an
isolated future test profile, PCSX2's [documented `-datapath` option](https://github.com/PCSX2/pcsx2/blob/v2.8.2/pcsx2-qt/QtHost.cpp)
selects a separate application data directory; no global settings were edited
for this investigation.


### Priority after the successful PCSX2 control

1. Inventory the rejection causes in the expensive racing states, especially
   16-bit depth/colour targets and destination blending. The current native
   guard accepts roughly half the primitives but fully eligible batches cover
   only about 19% of early-race raster time. Do not mistake that primitive count
   for enough native coverage to deliver full speed.
2. Combine useful native coverage with persistent GPU attachments and coherent
   texture/palette reuse. Avoid per-triangle CPU readbacks or full-frame
   transfers at every material change. Keep the GS fallback correct.
3. Measure an enabled native path against the GS reference over whole racing
   segments, including pacing and audio. Only retain performance claims supported
   by alternating runs; fewer draws alone are not a racing speedup.

Hardware replacement and speculative CPU/VU/IOP rewrites are lower priorities
unless new measurements identify a limit there. The successful PCSX2 test does
not remove the need to test our own implementation for correctness and speed.

### Rejection inventory result

The diagnostic now records simultaneous descriptive conditions per primitive.
Only dispatches whose primitives all share one mask receive time for that
combination; heterogeneous dispatch time is kept separate. In the early-race
sample, destination blending as the sole reported condition accounts for 14.7%
of measured raster time, depth format alone 4.6%, combined depth/blending 8.3%,
and colour/depth/raster-feature combinations 7.9%. Heterogeneous masks account
for another 25.2%. These conditions do not prove a proposed implementation
would satisfy all guards or accelerate the corresponding time.

The result lowers the priority of a depth-only extension as a standalone fix.
A small batching experiment is also justified: a full-screen untextured fill
can share a GS batch with textured triangles, preventing uniform-kind/flag
specialization. Test a guarded separation of that fill before committing to
more rendering features. Required evidence remains byte-exact GS output and a
repeatable whole-race improvement; reject the experiment if it only reduces
profiled command cost or moves work elsewhere.
