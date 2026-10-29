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
