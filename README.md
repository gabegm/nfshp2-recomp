# Hot Pursuit 2 PS2 recompilation setup

Experimental local build setup for the PS2 edition of Need for Speed: Hot
Pursuit 2 using [ps2-native-recomp](https://github.com/exoteric33/ps2-native-recomp).
The US disc (`SLUS_203.62`, executable CRC `1D2818AF`) boots with the included
runtime patch. The upgraded native runner passed a short launch test and a
several-minute race; a full playthrough and other disc revisions remain unverified.

## Supported builds

This setup supports a locally built **Linux x86_64** runner. It has been built
and launched on Fedora/Aurora Linux. The RetroAchievements integration is
compiled on Linux only. Steam Deck uses Linux, but this setup has not been
verified on Steam Deck hardware. Windows and macOS builds are not supported by
these setup scripts. Releases contain the setup source and patches; you supply
your own PS2 disc image and build the runner locally. No game data or prebuilt
game executable is distributed.

## Build locally

Linux prerequisites: Git, Python 3, CMake, Ninja, Clang with C++20
`<format>` support, SDL2 and X11 development headers, curl, unzip and a Vulkan
driver. On Fedora, the relevant packages are `SDL2-devel` and `libX11-devel`.

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

### Play settings

The in-game quick menu (F1) can change picture style, internal resolution,
full screen, and controls. Picture style, resolution, and controls are saved
in `keys.ini`; full screen applies to the current run. Resolution changes take
effect the next time the game starts. To keep resolution editable there, leave
`PS2_SCALE` unset; `--performance` now does so.

To quit with a controller, hold Start for one second to open the quick menu.
The existing Select + Start shortcut and F1 also open it. From **Resume**,
press Up once to reach **Quit the game**, then press Cross/A twice to confirm.
The runner writes the memory card before exiting. A short Start press still
works as the game's pause button.

Startup choices such as no-intro and widescreen, plus renderer environment
options, can be saved in `.local/play.conf`. On this installation, `.local/`
points into the game directory. For example:

```ini
# .local/play.conf
nointro = true
performance = false
widescreen = false
multilanguage = false
modern_controls = false
achievements = true
# PS2_TILE = 8
# PS2_DECODE = 1
# PS2_SCALE = 2
# arg = --fullscreen
```

Run `bash scripts/play.sh` with no flags to use the file. Add one `arg =` line
per runner argument; a flag and its value need separate lines. The file is
private and is never treated as shell code. Command-line profile flags override
the file, including `--no-nointro`, `--no-performance`, `--no-widescreen`, and
`--no-multilanguage`, and `--no-modern-controls`. An environment variable set for one launch overrides
the same `PS2_` setting in the file. Build the widescreen or language profile
before enabling it here. A separate game-folder or desktop launcher must call
this `scripts/play.sh`; launching `ps2run` directly bypasses `play.conf`.
`PS2_SCALE` accepts 1 through 4. If set in the file, it overrides the F1
resolution choice and locks that menu item; leave it commented to choose
resolution in-game.

### RetroAchievements

This Linux build includes the official [rcheevos](https://github.com/RetroAchievements/rcheevos)
client, pinned to v12.4.0. The US disc matches the existing
[97-achievement PS2 set](https://retroachievements.org/game/2999). The client
identifies the original disc even when the game runs with widescreen or control patches.

Sign in once from a terminal:

```sh
bash scripts/play.sh --ra-login
```

The password is sent over HTTPS and never saved. The returned login token is
stored in `.local/ra-credentials` with mode `0600` inside the game installation.
After that, `play.sh` enables casual achievements automatically and shows
unlock notifications in the game window. Set `achievements = false` in
`.local/play.conf` or pass `--no-achievements` to turn them off. The game still
runs without an account. Quick-loads reset achievement progress for the current
session, which avoids counting progress from before the loaded state. Hardcore
mode is not enabled because this runner supports gameplay patches, save-state
loads, and recorded input playback.

If an existing checkout is pinned to the previous upstream revision, move
`.local/ps2-native-recomp` to a backup name before running the build. Keep its
`games/nfshp2/player.ps2` memory card and `keys.ini` controller settings; copy
them into the new checkout after the build. Older quick saves may not load in
the upgraded runtime, so retain the backup until you no longer need them.

The compatibility patch adds chained disc reads. Upstream now handles SIF DMA
descriptors in scratchpad memory. The local patch contains no translated game
code. The script applies it automatically. When a controller is connected, its
game input takes priority over keyboard and mouse input, preventing Steam's
desktop mappings from sending duplicate PS2 buttons. Set
`PS2_KEYBOARD_WITH_PAD=1` if you intentionally want both input devices to
control player 1 and navigate the quick menu. F1 and other runner hotkeys
still work; while the quick menu is open, its keyboard navigation is ignored
when a controller is connected so Steam's desktop bindings cannot close it.
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
Only the widescreen and HUD sections are enabled in this profile. The 60-fps
hack and controller changes in the downloaded file are left disabled.

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

## Classic and modern controller layouts

The original game offers classic setups A–D. The optional modern controls
profile enables Silent's pinned [trigger mapping patch](https://silentsblog.com/updates/2024/04/14/need-for-speed-hot-pursuit-2-ps2-trigger-control-mappings/),
which adds setups E–H to the game's own controller options. The new setups
use analog triggers for throttle and brake. You can still choose A–D in the
same menu. **Enabling the profile does not change the selected in-game setup:**
open **Options → Controller → Configuration**. Press left or right on the
directional pad while the setup letter is highlighted to cycle through A–H;
press Triangle to return to Controller. Select **Setup E, F, G or H**. Setup H
is based on the Xbox version of Hot Pursuit 2. If acceleration still uses A,
the game is still using one of the classic setups. To keep your choice after
restarting, return with Triangle, then use **Options → Save/Load → Save**.
Build the profile with the
other features you use:

```sh
bash scripts/build.sh "/absolute/path/to/game.iso" --widescreen --multilanguage --modern-controls --no-explore
bash scripts/play.sh --widescreen --multilanguage --modern-controls
```

Set `modern_controls = true` in `.local/play.conf` to use that profile on
ordinary launches. Use `--no-modern-controls` to return to the original
profile. The build keeps the original game executable and controller options.
The trigger patch is limited to the verified US disc executable.

If a Steam Controller opens Steam's keyboard or moves the HUD as you drive,
add the game directory's `play.sh` as a non-Steam game and launch it from
Steam with a gamepad layout. Steam's desktop layout can generate keyboard
events while the game is launched outside Steam; the runner filters duplicate
game input from those events, but it cannot stop Steam's own keyboard overlay.
See [Steam's non-Steam game instructions](https://help.steampowered.com/en/faqs/view/33E8-5EDF-24E6-4CFB).

If a controller that has been used in the current session disconnects, the
quick menu opens and pauses the game. Reconnect it or another controller,
then choose **Resume**. An idle second controller can disconnect without
interrupting play.

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

The `--performance` preset disables texture predecode and uses 4x4 raster
tiles. Internal resolution remains controlled by the F1 menu unless
`PS2_SCALE` is set explicitly:

```sh
bash scripts/play.sh --performance
```

The upstream renderer changed substantially in this revision. The older local
renderer patches and their Intel UHD 620 benchmark results are retained in the
repository as historical experiments but are not applied to this build. This
preset changes work scheduling and texture handling, not the selected
resolution. It has not yet been benchmarked against the previous version. The new
upstream revision also includes its own renderer and threading improvements.
`PS2_DECODE`, `PS2_SCALE`, and `PS2_TILE` can be overridden in the environment.

The normal renderer compiles its nine main shader pipelines at startup and
builds the mipmap variant alongside them. A separate shader precompilation
step is unlikely to address stutters during a race. To capture the cause of
slow frames, add `PS2_HITCH_LOG = 40` to `.local/play.conf`, play a race, then
inspect the newest `ps2run_*.log` in the active profile's `logs/` directory.
The `[hitch]` lines time rendering, presenting, disc reads and other steps.
Remove the setting after collecting the log.

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
