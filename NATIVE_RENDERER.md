# Native renderer research

The game still uses the verified compute-based GS renderer. Optional research
paths now cover capture, GPU-only attachment handoff and
selected native textured triangles/batches. They are disabled by default; no
racing FPS improvement is claimed.

## Capture complete VU invocations

Rebuild normally to install `patches/vu-census.patch`. Use a fresh private
capture directory and a short frame range during the intended race scene:

```sh
PS2_VU_CENSUS_DIR="$PWD/.local/vu-capture" \
PS2_VU_CENSUS_FROM=3040 PS2_VU_CENSUS_FRAMES=120 \
PS2_VU_CENSUS_SAMPLES=2 bash scripts/play.sh --performance \
  --load-state games/nfshp2/performance/race.ps2state --frames 3180 \
  --press 3001:cross:180 --no-record
python3 scripts/analyze-vu-census.py .local/vu-capture --meshes
```

That example uses a private development checkpoint; it is not distributed.
For an ordinary boot, choose a suitable later frame and omit `--load-state`.
Allow execution to continue past the capture window so its last frame totals
are written. Without `PS2_VU_CENSUS_DIR`, capture is disabled. Measurements
with capture enabled are not FPS benchmarks.

The hook surrounds a complete outer VU invocation, including compiled block
chaining. Capturing each internal block would greatly overcount programs and
misattribute their output. The capture records:

- Per-frame invocation, XGKICK and output-qword totals by program fingerprint
  and entry address.
- Selected input/output VU memory, loaded microcode, registers and emitted
  GIF packets, with a shared invocation ID.
- TOP and ITOP values for sampled invocations.

Frame labels are sampled from the emulation clock at invocation boundaries;
they are not exact GS fences, especially with a separate VU thread. Program
fingerprints identify reachable microcode plus its entry point, not semantic
labels such as "track". A future replacement must verify the actual code and
supported input/state conditions before taking over.

`PS2_VU_CENSUS_FRAMES` is limited to 1–600 (default 1).
`PS2_VU_CENSUS_SAMPLES` is limited to 0–8 per fingerprint (default 1), with
at most 128 sampled invocations overall and at most 32 stored packets per
sample. All kicks are counted even when their packet bytes are not stored.
Registers are serialized as 157 little-endian 32-bit words: 32 VF vectors,
ACC, 16 VI values, then Q/P/I/R/MAC/STATUS/CLIP/CMSAR0/TPC. The raw data/code
memories are each 16 KiB. No C++ structure padding enters the register files.

All captures, reports, OBJ meshes, Ghidra projects and decompiled game code
are game-derived private data. Keep them under ignored local storage; none
belongs in the public repository.

## Inspect geometry

The analyzer decodes packed GIF tags, PRE/PRIM, packed colour, STQ, UV and XYZ
writes, including triangle lists/strips/fans and ADC-suppressed drawing. Its
synthetic tests cover truncation, strip/fan assembly, unknown inherited state,
non-packed packets and capture-path confinement. Unsupported geometry is
reported as incomplete rather than guessed.

OBJ files contain projected GS coordinates, not original world/model-space
vertices. They do not reproduce textures, the global framebuffer state or
PS2 blending. They are useful for inspecting packet topology, not a playable
native renderer. REGLIST/IMAGE and A+D vertex writes are not reconstructed.
Packet boundaries without a fresh primitive setup are treated conservatively.

## First observations and validation

Two 120-frame race windows each exercised eight outer entry points. One
shared entry produced over 99% of the emitted qwords and generated textured
triangle strips; another emitted mostly state packets. The same dominant
fingerprint appeared in both scenes. This is a strong starting point for
analysis, but does not yet distinguish track geometry, cars and effects.

Capture-enabled versus archived-renderer comparisons matched byte for byte:
a 600-frame initial race, 120-frame later/menu/widescreen samples, a 24-frame
2x-resolution sample and a 24-frame texture-predecode sample. Checks include
images, guest RAM, raw audio and complete save states. Captures remain private.

With capture disabled, paired 600-frame windowed runs measured
50.52/50.49 fps for the archived build and 50.73/50.63 fps with the hook
installed. The diagnostic object is kept off the ordinary VU call stack.
These results show no measured regression and do not establish a speedup.

The next stages are to identify the dominant entry's input layouts and mode
flags, correlate them with game-side submission functions, and implement one
recognized rendering path with the original renderer as its fallback.

## Private Ghidra analysis

The optional setup uses official [Ghidra 12.1.4](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build)
and the matching [Emotion Engine extension v2.1.39](https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases/tag/v2.1.39).
Both download URLs and SHA256 hashes are pinned. Tools and archives stay in
`.local/tools`; no binaries are redistributed. This version works with the
machine's existing 64-bit JDK 21. The headless workflow was tested on Linux.

```sh
python3 scripts/setup-ghidra.py
python3 scripts/analyze-renderer.py
```

The analysis helper requires the verified original US executable already used
by the build. It reads the global pointer from ELF `.reginfo` and locates
`.vutext` instead of embedding guessed addresses. `SetPs2Gp.java` applies GP
before analysis; without it, GP-relative globals can remain unresolved in the
decompiler. `ExportVuReferences.java` writes references, containing functions,
disassembly and decompilation to private notes. Additional discovered data
ranges can be followed with `--reference-range START_HEX END_HEX`.

Notes live in `.local/renderer-analysis`. Ghidra rejects project paths with a
component beginning with a dot, so the actual project is created in a temporary
non-dot directory. Its path is recorded in `project-path.txt` and `metadata.json`.
Archive that directory privately if it must survive temporary-directory cleanup.
The Java preferences/home override also stays in `.local`.

In the initial analysis, two functions referenced the VU code area. Following
a discovered renderer command-buffer pointer identified 55 containing functions.
Those are candidates for inspection, not 55 proven track-drawing functions.
The PS2 extension analyzes the main Emotion Engine CPU; this workflow does not
claim that ordinary CPU decompilation reconstructs VU microcode or original
model formats automatically. DVP overlay-loader warnings remain in the private
log and limit conclusions about those overlay sections.

Synthetic tests validate ELF bounds/metadata and safe archive extraction,
including restoration of executable permissions. No game data is used by them.

## Standalone native mesh and material preview

The Linux research preview uses Vulkan hardware triangle rasterization through
wgpu-native, vertex buffers and a conventional vertex/fragment pipeline. It
is a separate executable and never replaces the playable renderer.

```sh
bash scripts/build-native-preview.sh
python3 scripts/test-native-preview.py
python3 scripts/analyze-vu-census.py .local/vu-capture --meshes
.local/native-preview/native-mesh-preview \
  .local/vu-capture/CHOSEN_PACKET.nfm .local/native-preview/mesh.ppm
```

Choose a `preview_mesh` filename listed in the private `summary.json`;
`CHOSEN_PACKET` is a placeholder. The analyzer emits `.nfm` only for supported,
self-contained geometry with drawable triangles. Each file starts with `NFM1`,
a little-endian vertex count, then flattened triangle-list vertices containing
five little-endian floats: projected X/Y and RGB. Unknown colours use grey.

The NFM1 preview fits projected geometry into a 512x512 view and interpolates
its vertex colours. NFM2 adds decoded base-level textures, perspective STQ,
repeat/clamp addressing, nearest/bilinear sampling, texture functions, TCC and
alpha tests. Neither mode implements depth, fog, destination alpha, PS2 blending
or framebuffer sharing. This is not a complete native material implementation
or a screenshot comparison. It demonstrates that captured geometry can reach
the hardware rasterizer without the compute-based GS raster loop. No frame-rate
claim follows from drawing an isolated packet.

The explicit hardware test checks a synthetic RGB triangle against analytic
colour/coverage at 729 sample pixels, then six textured cases covering addressing,
filtering, perspective coordinates, texture functions and alpha tests. It also
rejects malformed/non-finite meshes.
It requires a hardware Vulkan adapter and refuses a software adapter. Captured
geometry from both race samples was also rendered on the UHD 620; those meshes
and previews remain private. The default unit suite needs neither GPU access
nor game assets; the hardware test is a separate explicit command.

The next rendering milestone needs native textures/materials and a compatible
colour/depth handoff with the fallback renderer. A standalone preview does not
solve draw ordering or render-to-texture dependencies inside the game.

## Capture a complete material input

`patches/native-draw-capture.patch` optionally snapshots one primitive at the
GPU draw entry. It reads actual GPU VRAM and the current palette after flushing
prior work. It also runs the existing GPU texture decoder into a temporary
buffer to provide an independent reference for the offline decoder. It does
not rely on potentially stale software-side palette or VRAM shadows.

Use internal scale 1 and a deterministic single-thread capture arrangement.
For example, with the same private race checkpoint used above:

```sh
PS2_SCALE=1 PS2_NATIVE_DRAW_CAPTURE="3040,100,$PWD/.local/native-draw" \
  bash scripts/play.sh --performance \
  --load-state games/nfshp2/performance/race.ps2state \
  --frames 3044 --press 3001:cross:44 --rtc 1700000000 --no-record \
  --frames-dir "$PWD/.local/material-reference" --frame-every 3044
python3 scripts/prepare-native-material.py .local/native-draw
.local/native-preview/native-mesh-preview \
  .local/native-draw/triangle.nfm .local/native-preview/material.ppm
```

The selector is `emulation-frame,GPU-draw-ordinal,directory`. Ordinals start
at zero for that selected frame and count GPU draw calls before clipping or
degeneracy rejection. With separate emulation/render threads, the sampled
emulation frame is not an exact GS frame boundary; use `--frames-dir` or
`PS2_ONE_THREAD=1` for reproducible selection. The hook is disabled by default,
keeps capture state off the normal draw stack, and rejects higher internal
scales, shader overrides and GPU skip/replay combinations.

Snapshots contain 4 MB VRAM, 512 palette entries, swizzle tables, the 40-word
runtime setup descriptor and original vertices. Word buffers are little-endian.
These are private game data. The decoder supports direct and indexed PS2 texture
formats, including palette banking and TEXA alpha expansion, and fails if its
RGBA output differs from the captured GPU reference. The current NFM2 exporter
accepts one assembled textured triangle and repeat/clamp addressing; it rejects
region wrapping. An NFM2 header contains `NFM2` followed by six little-endian
words: vertex count, texture width/height, setup flags/modes and alpha reference.
Each vertex contains nine floats: X/Y, RGBA and S/T/Q. RGBA texels follow.

Real 8-bit and 4-bit captures matched the GPU texture reference byte for byte,
including later-scene, menu, widescreen and predecode samples. Captured textured
triangles from both race scenes were drawn using the native hardware pipeline.
Some inspected draws use a source-pass blend equation even though the PS2 ABE
flag is set; that is a useful candidate for an opaque/cutout native path, not
proof that every draw with the same VU entry is safe to replace.

Material-capture-enabled comparisons matched images, guest RAM, audio and full
save states for the 600-frame initial race, later/menu/widescreen 120-frame
samples and a 24-frame predecode sample. A 24-frame 2x run correctly rejected
capture and retained matching output. With diagnostics disabled, paired runs
were 50.63/50.39 fps for the archived build and 50.78/50.14 for the instrumented
build, within observed variation. No gameplay speedup is claimed.

The synthetic unit suite now includes texture addressing, 4-bit nibble selection,
palette banks, alpha expansion, buffer bounds, flat colour/FST conversion and
GPU-reference mismatch rejection. The hardware suite separately tests the
conventional render pipeline. Both are required evidence before expanding
this preview into an in-game path; neither establishes full-game compatibility.

## One-draw framebuffer/depth proof

`patches/native-frame-proof.patch` extends the optional snapshot with the
reference VRAM immediately after the selected draw. The draw executes once;
the hook captures around it and then returns to the original renderer. No
native draw is inserted into gameplay.

A fresh capture can be prepared and compared at its original screen coordinates:

```sh
python3 scripts/prepare-native-material.py .local/native-draw --framebuffer
.local/native-preview/native-mesh-preview \
  .local/native-draw/frame-triangle.nfm .local/native-preview/frame.ppm
python3 scripts/compare-native-frame.py \
  .local/native-draw .local/native-preview/frame.ppm
```

This NFM3 mode initializes native colour and depth attachments from the snapshot,
then draws one textured triangle with hardware rasterization and depth testing.
It accepts only a guarded subset: unmasked PSMCT32 colour, PSMZ24 depth,
source-pass blending, a full zero-origin scissor, ordinary texture addressing,
and KEEP alpha failure. It rejects destination blending, fog, DATE/FBA, scan
masking, aliased surfaces, wrapped surfaces and framebuffer-feedback textures.
An incompatible reflection/shadow draw was correctly rejected during testing.

The native API forbids direct queue writes to Depth32Float textures. A depth-only
fullscreen render pass initializes that attachment from a storage buffer instead.
A separate fragment entry reproduces the reference's integer depth quantization
before writing fragment depth. Native interpolated depth alone differed by one
integer unit in some pixels; the explicit quantization removed those differences
in the sampled draws. Its performance implications have not been measured.

The private NFM3 file embeds one triangle, texture, initial RGBA framebuffer and
normalized depth buffer. It starts with `NFM3` and nine little-endian words:
vertex count, texture width/height, flags, modes, alpha reference, framebuffer
width/height and integer depth maximum. Vertices contain ten floats (NFM2 fields
plus normalized Z). Texture RGBA, initial framebuffer RGBA and initial float32
depth follow. The preview writes PPM plus `.rgba` and `.depth` buffers for exact
comparison; the helper saves `native-comparison.json` beside the private capture.

Five sampled real draws produced these results:

| Sample | Reference changed pixels | Native changed pixels | RGBA-different pixels | Maximum channel error | Depth differences |
|---|---:|---:|---:|---:|---:|
| Early A | 4,697 | 4,697 | 56 | 1 | 0 |
| Early B | 360 | 360 | 10 | 1 | 0 |
| Early C | 24 | 24 | 1 | 2 | 0 |
| Later A | 10 | 10 | 0 | 0 | 0 |
| Later B | 2 | 2 | 0 | 0 | 0 |

Changed-pixel masks matched in all five. Those masks are not direct raster
coverage measurements: unchanged colours or rejected fragments can conceal
coverage differences. Colour differences remain from interpolation/filtering;
this is not bit-exact native rendering or full-game validation.

The hardware test also checks seven framebuffer cases: depth pass/reject,
GEQUAL versus GREATER at equal depth, masked depth writes, alpha rejection and
ALWAYS depth. Synthetic unit tests verify proof-state rejection, framebuffer
layout and comparison metrics. The before/after capture hook retains matching
images, audio, guest RAM and save states across the existing six scenarios,
including a 600-frame early race; scale 2 continues to reject capture safely.

The remaining in-game work is persistent native colour/depth resources, texture
cache coherence and guarded transitions back to the GS renderer. The proof
currently uploads and reads back captured buffers as an offline test. Moving
that machinery into the game unchanged would add substantial overhead, so it
must not be mistaken for an accelerated gameplay renderer.

The final windowed FPS rerun could not complete: the archived baseline stalled
at SDL window creation before GPU initialization, with both Wayland and X11.
No new FPS conclusion is drawn for this final hook revision. Headless game
comparisons, native hardware tests and patch-stack checks passed. Earlier
capture-disabled measurements above remain separate from the offline proof.

The native depth attachment represents the 24-bit depth values, not the unused
upper byte of each PS2 Z24 word. A future live VRAM resolve must preserve those
unrepresented bits and synchronize all fallback reads/writes. Matching the
standalone depth comparison does not establish that live cache-coherence work.

## GPU-only colour/depth handoff

The shared, hand-written `tools/native_target_bridge.hpp` now imports actual
PS2 GPU VRAM into reusable RGBA8 and Depth32Float attachments and resolves them
back entirely on the GPU. The runtime build installs that header alongside the
optional `native-target-bridge.patch`; no game code is included in either.

```sh
bash scripts/build-native-preview.sh
.local/native-preview/native-target-bridge-test
```

The synthetic hardware test checks all 4 MB of VRAM after eight transactions:
round trips and native attachment clears, four sizes including partial pages,
resource reuse, untouched regions and preservation of all depth upper bytes.
Independent colour/depth address permutations detect mapping mistakes. The
production state gate is also tested without GPU access.

Depth uses an exact power-of-two representation, `Z / 2^24`, rather than
`Z / (2^24 - 1)`. The latter caused one-integer-unit round-trip errors on the
UHD 620. Every 24-bit integer is representable in the chosen representation;
resolve clamps and scales back before retaining the original upper byte.
Native draws using these attachments must use the same representation.

The optional in-runtime diagnostic selects one draw boundary:

```sh
PS2_NATIVE_TARGET_BRIDGE=3040,100 bash scripts/play.sh --performance \
  --load-state games/nfshp2/performance/race.ps2state --frames 3044 \
  --press 3001:cross:44 --frames-dir "$PWD/.local/bridge-check" --no-record
```

This diagnostic imports and resolves without replacing any game draw. It rejects
scales other than 1, custom shaders, GPU skip/replay, wrapped or aliased surfaces,
nonzero-origin/partial scissors, masked colour, and formats other than PSMCT32
colour plus PSMZ24 depth. It is disabled by default.

Only resource allocation persists across transactions. Contents are imported
fresh after queued GS work is submitted, and resolved on the same queue before
fallback work resumes. There is no persistent authoritative native framebuffer,
texture cache, CPU readback, or game-draw acceleration in this milestone.
The conservative per-transaction import/resolve is a correctness building block;
it would be too expensive to perform for every triangle.

The in-runtime round trip retained byte-identical images, guest RAM, audio and
complete save states against archived binaries in the 600-frame initial race,
120-frame later/widescreen/menu runs and 24-frame predecode/2x runs. The actual
handoff ran in the race, later, widescreen and predecode cases; menu and 2x
configurations were rejected and exercised the unchanged fallback.

## First native triangle inside gameplay (experimental)

`native-triangle.patch` and the shared `tools/native_triangle.hpp` now allow
one selected game triangle to use native Vulkan rasterization. This path uses
the runtime's existing GPU texture decoder and GPU-only colour/depth handoff;
it does not read textures or framebuffers back through the CPU. Normal gameplay
continues to use the GS renderer unless this diagnostic is explicitly enabled.

```sh
PS2_NATIVE_TRIANGLE=3040,100 bash scripts/play.sh --performance \
  --load-state games/nfshp2/performance/race.ps2state --frames 3044 \
  --press 3001:cross:44 --frames-dir "$PWD/.local/native-check" --no-record
.local/native-preview/native-triangle-test
```

The selector is an emulation frame and GPU draw ordinal, with the same
single-thread/frame-dump requirement as material capture. The path consumes
its selection once. Unsupported states execute normally through the GS path.
It supports only the guarded source-pass textured triangle subset described
above, positive perspective Q, finite inputs and 24-bit vertex depth. All other
draws retain GS rendering. Attachments, decoded texture storage, samplers and
bounded depth-state pipelines are reused; texture contents are decoded fresh
for the selected draw, so no unvalidated texture-cache coherence is assumed.

The explicit hardware suite now includes seven native triangle transactions.
It checks every VRAM word against analytic coverage, colour, depth and preserved
upper bytes, including equal-depth rules, alpha rejection and masked Z writes.
Synthetic tests additionally exercise the production material rejection gate.

Five actual in-game selections matched the earlier offline results: 56, 10,
1, 0 and 0 differing colour words, with maximum channel errors of 1, 1, 2,
0 and 0 respectively. Depth and every other VRAM word matched exactly; all
captured texture/material inputs, guest RAM and audio matched. An incompatible
reflection/shadow draw was rejected and retained identical output.

This proves the native-to-GS handoff for sampled individual draws, not general
rendering compatibility. Colour interpolation/filtering is still not bit exact.
The one-draw import/decode/resolve has substantial overhead and is not an FPS
optimization or a supported play mode. The next performance step is to batch
compatible consecutive triangles so they share those costs, then measure the
result before enabling broader replacement.

The first captured post-handoff image also matched exactly in all six live
selection tests. Small one-draw colour differences above were measured in VRAM
immediately after the selected draw, rather than inferred from a later image.

## Bounded native triangle batching (experimental)

`native-batch.patch` can queue a bounded selection of compatible consecutive
triangles and render each compatible run with one native draw, one texture
decode and one colour/depth import/resolve. Triangle-specific depth planes are
read in the vertex stage and passed as flat fragment inputs, so separate
triangles retain their own integer-depth calculation within a single draw.

```sh
PS2_NATIVE_BATCH=3040,1400,128 bash scripts/play.sh --performance \
  --load-state games/nfshp2/performance/race.ps2state --frames 3044 \
  --press 3001:cross:44 --frames-dir "$PWD/.local/native-batch-check" --no-record
```

The selector is `emulation-frame,first-GPU-draw-ordinal,number-of-draws`.
The selected window is capped at 4,096 draws; each native run is capped at 128
triangles. Only matching complete setup descriptors can share a run. Ineligible
draws retain GS rendering. `PS2_NATIVE_BATCH_SIZE=1` forces sequential native
execution for comparison; values from 1 to 128 are supported (default 128).
The batch selector is inactive when the single-triangle or bridge selector is
also set. These remain diagnostics, without a normal launcher play option.

Pending native runs drain before fallback draws, uploads, palette changes,
texture flushes, local copies, presentation, downloads and save-state access.
After a run, VRAM is authoritative again; the fallback never needs to interpret
a stale native attachment. Setup changes and the end of the selected frame also
end a run. If native preparation fails, queued original states/vertices replay
through GS in order instead of dropping their geometry. The disabled draw hook
keeps the large diagnostic queue record off the normal draw stack.

Synthetic tests exercise the actual installed queue implementation: ordering,
material changes, rejection, capacity, frame end, selection limits and fallback
replay. Hardware tests now draw overlapping triangles within one native draw,
checking every VRAM word against analytic colour, depth, coverage and preserved
upper bytes across the seven depth/alpha cases.

Private in-game comparisons produced these results:

| Selected scene | Native triangles | Sequential native draws | Batched native draws | Largest batch |
|---|---:|---:|---:|---:|
| Early sky window | 11 | 11 | 3 | 5 |
| Early world window | 128 | 128 | 9 | 21 |
| Later world window | 33 | 33 | 4 | 14 |
| Widescreen sky window | 29 | 29 | 5 | 9 |

Batched and sequential native runs matched byte for byte in captured VRAM,
material buffers, images, audio, guest RAM and complete save states. Against
GS reference captures, the four windows had 352, 117, 1 and 535 differing
colour words respectively, with maximum channel errors of 2, 2, 1 and 2.
Depth and other VRAM words matched exactly. The menu and 2x selections accepted
no native triangles and retained identical fallback output.

These counts demonstrate lower native submission/texture/handoff work than
sequential native execution. They do not compare racing FPS with the original
GS renderer. Runs still end at material and palette changes; each new run
imports and resolves a full framebuffer. Persistent authoritative attachments
across those transitions, coherent texture caching, broader raster correctness
and representative racing benchmarks remain necessary for a viable accelerated
renderer. All defaults continue to use the established GS renderer.

The final default-disabled comparison again matched images, guest RAM, audio
and full save states in all six existing scenarios. The unit suite has 49
passing tests; `scripts/test-native-preview.py` runs the original material/frame
checks plus both new hardware handoff and batching executables after building.

## Native eligibility weighted by GPU dispatch timings

`native-coverage.patch` adds optional CPU-side eligibility counters to the
existing per-dispatch GPU profiler. It uses the exact production native guard
and vertex checks for each primitive accepted into a GS batch. It does not
replace any draw, change shader work, or read back additional game buffers.
The hook is compiled only into a separate diagnostic runner. Ordinary builds
exclude its fields, counters and branches entirely. The validated ordinary
4:3 and widescreen binaries match their pre-instrumentation SHA256 hashes.
The diagnostic runner additionally requires `PS2_NATIVE_COVERAGE=1`; its
temporary setup and vertex objects remain outside the ordinary draw stack.

```sh
python3 scripts/build-native-coverage.py
mkdir -p .local/native-coverage
root="$PWD"
(
  cd .local/ps2-native-recomp
  export LD_LIBRARY_PATH="$root/.local/deps/lib64:$root/.local/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  PS2_NATIVE_COVERAGE=1 PS2_NO_VU_THREAD=1 PS2_DECODE=0 PS2_SCALE=1 \
  PS2_TILE=4 PS2_LOOK=crisp PS2_RASTER_SPECIALIZE=1 \
  PS2_RACE_SPECIALIZE=1 PS2_CLUT_REUSE=1 \
  PS2_GPU_PROFILE="3040,3160,$root/.local/native-coverage/dispatches.txt" \
    "$root/.local/native-coverage-build/nfshp2/ps2run" \
    games/nfshp2/disc/SLUS_203.62 --disc games/nfshp2/disc.iso \
    --load-state games/nfshp2/performance/race.ps2state --frames 3164 \
    --press 3001:cross:164 --frames-dir "$root/.local/native-coverage/images" \
    --frame-every 3164 --rtc 1700000000 --no-record
)
python3 scripts/analyze-native-coverage.py \
  .local/native-coverage/dispatches.txt \
  --output .local/native-coverage/summary.json
```

The Linux build helper enables the CMake option
`PS2_NATIVE_COVERAGE_DIAGNOSTIC` in an independent runtime directory, reuses
already-built game objects, and links a separate executable without replacing
the ordinary one. `--widescreen` uses the matching existing widescreen build.
It invokes a validated linker argument list without a shell and keeps linker
dependency output private, too. Diagnostic executables print a permanent marker;
the ordinary benchmark runner rejects them even with coverage disabled.

This is an intrusive timing/correctness run, not a windowed performance
benchmark. Use a single-thread arrangement for reproducible frame labels.
The existing eight raster metadata fields are followed by `native`, the
eligible primitive count, summed clipped bounding-box area, and eligible
bounding-box area. Areas use 64-bit counters, can overlap, and do not count
actual shaded fragments. The ordinary profiler format is unchanged without
the coverage flag. Do not combine coverage measurement with native replacement,
custom shaders or skip/replay experiments.

The analyzer reports fully eligible, mixed and unsupported dispatch time.
It never estimates a mixed dispatch's eligible time from its primitive count
or bounding-box area. Its state hints refer to the first setup; they are not
an exhaustive rejection analysis. Profiles and summaries remain private.

The 120-vblank race windows found roughly half the primitives eligible, but
only 14–21% of raster time in fully eligible batches. Including the entirety
of mixed batches gives upper bounds of 41–45% of raster time; the actual
eligible fraction is unknown within those mixed batches. The sampled menu had
no eligible primitives. Detailed scene results and the resulting priorities
are in [PERFORMANCE_PLAN.md](PERFORMANCE_PLAN.md).

Coverage-enabled profiling retained byte-identical images, guest RAM, raw audio
and full save states against archived binaries across the 600-frame early race,
120-frame later/menu/widescreen samples, and 24-frame 2x/predecode samples.
Synthetic tests check timing bounds, malformed metadata and the actual runtime
accumulator, including areas greater than 32 bits.
