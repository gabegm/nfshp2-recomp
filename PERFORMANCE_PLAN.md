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

## Milestone 2: measure useful native coverage (in progress)

Classify accepted GS primitives with the actual native renderer guard, then
associate eligibility counts with per-dispatch GPU timestamps. Report fully
eligible, mixed and unsupported raster batches. Do not apportion a mixed
batch's GPU time by its triangle count or bounding-box area. Intrusive GPU
profiling is separate from windowed performance measurements and cannot be
interpreted as ordinary frame time.

Completion: identify substantial expensive raster work the native path can
replace, and the material/palette/fallback transitions that would force it to
resolve. Existing evidence is about 75% raster share of measured compute time;
this says neither that 75% of total frame time is removable nor that the current
native subset covers that work. If eligible work is small, investigate the
expensive rejected states before expanding renderer infrastructure.

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
