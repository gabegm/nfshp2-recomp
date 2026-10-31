# Native renderer research

The game still uses the verified compute-based GS renderer. The first native
renderer milestone is optional instrumentation and geometry inspection; it
does not replace any game rendering or claim an FPS improvement.

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
