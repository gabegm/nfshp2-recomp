# Isolated Vulkan GS feasibility test

This is an experimental, Linux-only test of Play!'s GS graphics backend. It is
not connected to `scripts/play.sh` and does not change either game executable.
It uses synthetic commands and contains no game code, ROM, BIOS or assets.

From the repository root:

```sh
python3 scripts/build-play-gs-smoke.py --fetch --run
```

The first build requires network access, Git, CMake, Ninja, a C/C++17 compiler,
threads, zlib development files, the Vulkan loader and XCB headers. ICU/OpenSSL
are optional dependencies detected by upstream Framework. Vulkan headers are
fetched locally; no system package installation or root access is performed.
Subsequent builds work without `--fetch`. Source changes are refused rather
than overwritten. The script pins every fetched repository revision and stores
sources, build files, configuration and retained notices under the ignored
`.local/play-backend/` directory. It uses two build jobs by default.

`--run` selects a hardware GPU that meets the pinned backend's Vulkan 1.2,
fragment pixel interlock and shader/storage requirements. Software-only Vulkan
is an error. The test uses a private configuration directory and does not read
or change an installed Play! configuration. `--vulkan-library /absolute/path`
can select an installed Vulkan loader if automatic discovery fails.

The four initial cases upload a known 64x64 framebuffer, draw a sprite and read
it back, comparing all 4,096 pixels against independently calculated values:
32-bit and 16-bit colour, each with blending disabled and enabled. Blending
uses `(Cs-Cd)*As/128+Cd`. No screenshots or approximate image comparisons are
used. These cases passed on the UHD 620. Software-driver rejection also passed.
This is a small correctness/feasibility test, not a performance benchmark or
proof of complete GS compatibility. Depth, textures, palette coherency, state
restoration and presentation need separate validation before game integration.

The executable links upstream GS handling, Vulkan rendering and utility code.
It does not build or link Play!'s CPU emulator or CodeGen library. CodeGen
headers are needed because the GS interrupt declarations transitively include
MIPS declarations. This does not change the game's static recompilation model.

## Third-party licensing

No upstream code or binaries are vendored in this public directory. The build
fetches unchanged, pinned sources and preserves their original notices locally:

| Source | Applicable licensing |
|---|---|
| Play!, Framework, Nuanceur, CodeGen headers | BSD 2-clause |
| ghc_filesystem | MIT |
| Zstandard | BSD option selected from its BSD/GPL dual license |
| gzip wrapper derived from zlib and system zlib | zlib |
| Vulkan-Headers | Per-file MIT or Apache-2.0 notices; both license texts retained |

Dependency revisions are recorded explicitly in `scripts/build-play-gs-smoke.py`
and the generated private `sources.json`. Framework also links installed system
libraries when available. Any future binary distribution must include the
applicable dependency notices and account for those system libraries; this
repository currently distributes only the handwritten build/test sources.
