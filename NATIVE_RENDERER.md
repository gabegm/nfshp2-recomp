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
