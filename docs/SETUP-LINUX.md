# Setting up on Linux

Short version of how to get a build toolchain and an emulator running on
Linux (tested layout: Ubuntu 24.04). You need about 5 GB of disk and an
internet connection for the first build.

## 1. Toolchain

```sh
sudo apt-get update
sudo apt-get install -y \
  autoconf automake bison build-essential ca-certificates clang cmake curl \
  flex git gperf libgmp-dev libmpc-dev libmpfr-dev libtool ninja-build \
  nodejs patch pkg-config texinfo xz-utils

mkdir -p ~/palm && cd ~/palm
git clone https://github.com/palmsnipe/palm-toolchain.git
```

## 2. Palm OS SDK

The SDK isn't redistributable, so fetch **SDK 5r4 (Garnet)** yourself from
[PalmDB](https://palmdb.net/app/palm-os-sdk) or
[archive.org](https://archive.org/download/palm-os-sdk) and unzip it. Use the
full SDK; reduced GitHub mirrors are missing headers such as `PdbMgr.h`.

```sh
cd ~/palm/palm-toolchain
PALM_SDK_SOURCE=/path/to/unzipped/sdk-5r4 make setup   # builds gcc, PilRC, build-prc
make check
```

This takes a while (it compiles GCC). Everything stays under
`palm-toolchain/.toolchain/`; nothing is installed globally.

## 3. Build Diapalm

```sh
git clone https://github.com/syserr/diapalm.git && cd diapalm
make            # -> build/Diapalm.prc
make test
```

If the toolchain isn't at `~/palm/palm-toolchain`, add
`PALM_TOOLCHAIN_ROOT=/path/to/palm-toolchain`.

## 4. Emulator

Easiest: use the web app at <https://cloudpilot-emu.github.io/app/>. No
install needed; it runs the same emulator core.

1. Supply a Palm OS ROM (e.g. Palm OS 4.1 for a **Palm m515**). ROMs are
   copyrighted, so you must dump one from your own device or obtain it legally.
2. Create a session with that ROM and device.
3. Drag `build/Diapalm.prc` onto the emulator window. It shows up in the
   launcher as **Diapalm**.

Optional, for scripted/headless testing: build the native `cloudpilot-emu`
CLI from the [CloudpilotEmu source](https://github.com/cloudpilot-emu/cloudpilot-emu)
(follow its README; needs SDL2 and a C++ toolchain), then run it with
`SDL_VIDEODRIVER=dummy cloudpilot-emu --device-id PalmM515 /path/to/rom`
and feed it commands (`install`, `screenshot`, `pen X Y`, `launch`, ...) on
stdin.

## Troubleshooting

- **Crash on launch in the emulator:** the two-stage link (relocation
  generator) was skipped. Use plain `make`, not hand-run compiler commands.
- **Missing header errors:** the SDK wasn't the full 5r4 package, or
  `make setup` wasn't given `PALM_SDK_SOURCE`.
