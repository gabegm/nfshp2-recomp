# Hot Pursuit 2 PS2 recompilation setup

Experimental local build setup for the PS2 edition of Need for Speed: Hot
Pursuit 2 using [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp).
The US disc (`SLUS_203.62`, executable CRC `1D2818AF`) boots with the included
runtime patch. The upgraded native runner passed a 600-frame smoke test;
full races, audio, saving and other disc revisions remain unverified.

## Build locally

Linux prerequisites: Git, Python 3, CMake, Ninja, Clang with C++20
`<format>` support, SDL2 development files, curl, unzip and a Vulkan driver.
On Fedora, the SDL2 development package is `SDL2-devel`.

Upstream now requires Clang because its vector code uses Clang builtins. Clang
also needs a C++ standard library with `<format>`. On Fedora, install `clang`,
`libcxx-devel`, `libcxxabi-devel`, and `SDL2-devel`, then use `CXX=clang++` if
more than one compiler is installed. See [issue #2](https://github.com/gabegm/nfshp2-recomp/issues/2)
for the compiler probe discussion.

Supply your own lawfully obtained PS2 disc image:

```sh
bash scripts/build.sh "/absolute/path/to/Need for Speed Hot Pursuit 2.iso"
```

The script uses upstream revision `5d1ebbcf133cb3c31df75a294dbe38e30734c1ce`.
Upstream downloads wgpu-native during setup. All upstream files, extracted game
data, generated C++, binaries and exploration output stay under `.local/`.
From this directory run `bash scripts/play.sh` (F1 shows controls).
Additional upstream port options can follow the image path, for example
`--frames 600 --rounds 2`. `PS2_JOBS=4` controls game compilation concurrency.

If an existing checkout is pinned to the previous upstream revision, move
`.local/ps2-native-recomp` to a backup name before running the build. Keep its
`games/nfshp2/player.ps2` memory card and `keys.ini` controller settings; copy
them into the new checkout after the build. Older quick saves may not load in
the upgraded runtime, so retain the backup until you no longer need them.

The compatibility patch adds chained disc reads. Upstream now handles SIF DMA
descriptors in scratchpad memory. The local patch contains no translated game
code. The script applies it automatically.
Builds can take several minutes. Use a timeout for unattended exploration runs:
a frame limit cannot stop a game that fails before advancing another frame.

SDL2 can also be built from its official source into `.local/deps`; this setup
automatically discovers `lib/` and `lib64/` there. The upgraded build was
smoke-tested with SDL2 2.32.10 and AMD Radeon RX 9070 XT on Vulkan. The
sandbox's software Vulkan renderer is much slower; hardware GPU access is
needed for a useful play test.

## Skip startup movies

```sh
bash scripts/play.sh --nointro --performance
bash scripts/play.sh --nointro --widescreen --performance
```

`--nointro` skips the startup movies and reaches **Press Start** without sending
controller input. No rebuild is needed. It supports the verified US executable
and generates private boot-time patches under `.local/nointro/`; your disc image
and extracted executable stay unchanged. Reward and making-of movie paths are
not targeted. The option is ignored when launching with `--load-state` or
`--replay`, which restore their own state.

## Widescreen 16:9

Build the separate widescreen profile from your own US disc image:

```sh
bash scripts/build.sh "/absolute/path/to/Need for Speed Hot Pursuit 2.iso" --widescreen --no-explore
bash scripts/play.sh --widescreen --performance
```

This downloads the [PCSX2 community patch](https://github.com/PCSX2/pcsx2_patches/blob/8afa626253d5fdcb23e660314779c91e6b8cbb8a/patches/SLUS-20362_1D2818AF.pnach)
privately and enables the projection correction by nemesis2000 and HUD correction
by Aero_. The patch revision and download checksum are pinned. The full game
executable's SHA-256 and US serial are checked: the PAL edition can share the
same PCSX2 CRC, so CRC alone is not enough.

Code patches are applied before translation. The launcher uses the matching
patched executable, translated binary, runtime patch list and 16:9 display.
Only the widescreen and HUD sections are enabled. The 60-fps hack and controller
changes in the downloaded file are left disabled.

The normal 4:3 build stays available with `bash scripts/play.sh --performance`.

Both profiles use the same in-game memory card path, while widescreen quick
saves are stored separately. Start widescreen from a cold boot or a widescreen
quick save; a 4:3 quick save contains previously initialized projection state.
The first widescreen build takes several minutes. It uses its own build folders
and reuses vector programs already observed in the normal build.

Widescreen increases the visible area; it is a visual feature, not a speed fix.
Cold boot, menus and an early race are the initial validation scope. Other
tracks, movies and a full playthrough remain unverified. Downloaded community
patches, patched game files and generated binaries stay in `.local/`.

## US disc language selection

Build an optional profile that enables the language menu using the two
instructions shared by Felixthecat1970 in [issue #1](https://github.com/gabegm/nfshp2-recomp/issues/1):

```sh
bash scripts/build.sh "/absolute/path/to/game.iso" --multilanguage --no-explore
bash scripts/play.sh --multilanguage --performance
```

The build checks the US disc serial, executable SHA-256 and original
instructions before applying the patch and translating the game. The ISO and
unpatched 4:3 build stay intact. To combine the language menu with widescreen,
build and launch with both `--widescreen` and `--multilanguage`. Each profile
has separate quick saves; they share the in-game memory card. The language
menu patch has not yet been checked across a full playthrough.

## Performance

The `--performance` preset selects 1x internal resolution, disables texture
predecode, and uses 4x4 raster tiles:

```sh
bash scripts/play.sh --performance
```

The upstream renderer changed substantially in this revision. The older local
renderer patches and their Intel UHD 620 benchmark results are retained in the
repository as historical experiments but are not applied to this build. This
preset has not yet been benchmarked against the previous version. The new
upstream revision also includes its own renderer and threading improvements.
`PS2_DECODE`, `PS2_SCALE`, and `PS2_TILE` can be overridden in the environment.

Run the setup tests with:

```sh
python3 -m unittest discover -s tests
```

[PERFORMANCE_PLAN.md](PERFORMANCE_PLAN.md), [OPTIMIZATION.md](OPTIMIZATION.md),
and [NATIVE_RENDERER.md](NATIVE_RENDERER.md) document the older renderer work.
Their patch details and performance figures do not describe the current build.

## Publication policy

Publish only the reviewed setup files and runtime patch. Do not publish disc images, extracted
executables or assets, translated game source, game binaries, memory dumps,
save states, saves, screenshots or recordings. The pre-existing `src/` directory
is game-derived source and is deliberately excluded, as is all local build output.
The ignore file permits only the documented setup files and patch; do not force-add data.

Before publishing, inspect the full Git history and staged diff, then run:

```sh
python3 scripts/check-public-files.py
git diff --cached
```

The checker examines tracked filenames, not their contents or previous commits.
Keep upstream copyright and license notices with any upstream code you redistribute.
The patch includes small amounts of upstream context under the MIT license;
its notice is in `patches/LICENSE.upstream`. No game source is included.
The setup scripts are MIT licensed (see `LICENSE`). This project is unofficial
and is not affiliated with EA or Sony. Game and platform names belong to their owners.
