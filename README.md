# Hot Pursuit 2 PS2 recompilation setup

Experimental local build setup for the PS2 edition of Need for Speed: Hot
Pursuit 2 using [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp).
The US disc (`SLUS_203.62`, executable CRC `1D2818AF`) reaches the main
menu and starts the Coastal Parklands race with the included runtime patch.
Acceleration, 3D graphics and the race HUD were checked in a short automated run.
Full races, audio, saving and other disc revisions have not yet been verified.

## Build locally

Linux prerequisites: Git, Python 3, CMake, Ninja, a C++20 compiler with
`<format>` support, SDL2 development files, curl, unzip and a Vulkan driver.
On Fedora, the SDL2 development package is `SDL2-devel`.

Supply your own lawfully obtained PS2 disc image:

```sh
bash scripts/build.sh "/absolute/path/to/Need for Speed Hot Pursuit 2.iso"
```

The script uses upstream revision `ba3d4712a9291abe1767c8aa0aa8dc4eeda1db71`.
Upstream downloads wgpu-native during setup. All upstream files, extracted game
data, generated C++, binaries and exploration output stay under `.local/`.
From this directory run `bash scripts/play.sh` (F1 shows controls).
Additional upstream port options can follow the image path, for example
`--frames 600 --rounds 2`. `PS2_JOBS=4` controls game compilation concurrency.

The compatibility patch adds chained disc reads and fixes reading SIF DMA
descriptors from scratchpad memory. Both changes are runtime implementations;
they contain no translated game code. The script applies the patch automatically.
Builds can take several minutes. Use a timeout for unattended exploration runs:
a frame limit cannot stop a game that fails before advancing another frame.

SDL2 can also be built from its official source into `.local/deps`; this setup
automatically discovers `lib/` and `lib64/` there. This machine was tested with
SDL2 2.32.10 and Intel UHD Graphics 620 on Vulkan. The sandbox's software Vulkan
renderer is much slower; hardware GPU access is needed for a useful play test.

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

## Performance

For the tested Intel i5-8350U / UHD 620 laptop, try:

```sh
bash scripts/play.sh --performance
```

This disables the separate vector-unit thread and texture predecode, keeps
internal resolution at 1x, and uses the simpler Crisp display filter instead
of cubic sharpening and edge smoothing. The game's rendering resolution,
PS2 timing and arithmetic are unchanged; the final enlarged picture has fewer
visual enhancements.

Short repeated checkpoint tests on battery power gave:

| Scene | Default | Updated performance preset |
|---|---:|---:|
| Main menu | 45 fps | 50–52 fps |
| Coastal Parklands race | 29 fps | 40–41 fps |

The previous preset reached about 35 fps in that race; switching to the cheaper
display filter provided the additional gain. Results vary with the scene,
power limits and background load. These are emulated video frames; the game
may draw a new picture every other frame. The tested US disc needs 60 video
frames per second for full speed, so this is still slow motion with stretched
audio. Disabling audio stretching produces gaps rather than restoring speed.

Omit `--performance` to use the normal settings. To keep the runtime tuning but
restore the more expensive display filter:

```sh
PS2_LOOK=sharp_edges bash scripts/play.sh --performance
```

Plug into mains power and select the desktop's Performance power profile when
available. Keep `PS2_SCALE=1`; higher internal resolutions add graphics work.
The build already uses Release optimization and AVX2 when supported. CPU
specialization, CPU-specific compiler flags, aggressive inlining and ThinLTO
were tested but did not produce a repeatable menu improvement, so those
experiments are not enabled or required by the build.

The presentation patch fixes wgpu-native timestamp-unit conversion in GPU
profiling. The GPU meter covers compute passes; it excludes the final display
filter, whose cost was confirmed by comparing frame rates directly. A low GPU
reading therefore does not establish that display filtering is cheap.

The runtime changes were checked against the baseline using identical local
menu/race checkpoints: short-run memory dumps, audio samples and raw rendered
frames matched byte for byte. The display filter intentionally changes the
window's final appearance. Full-speed play and full-game compatibility remain
unverified.

For optimization ideas from Ace Combat 5, God of War and other PS2 projects,
see [OPTIMIZATION.md](OPTIMIZATION.md). These describe candidate changes;
their performance results do not establish an NFS speedup.

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
