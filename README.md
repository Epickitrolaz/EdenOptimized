<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->
<!-- lang: en-GB -->

<h1 align="center">
  <br>
  <a href="https://git.eden-emu.dev/eden-emu/eden"><img src="./dist/qt_themes/default/icons/256x256/eden.png" alt="Eden" width="200"></a>
  <br>
  <b>Eden</b>
  <br>
</h1>

<h4 align="center"><b>Eden</b> is a free and open-source (FOSS) Switch 1 emulator started by developer Camille LaVey.
<br>
Written in C++, with builds for Windows, Linux, macOS, Android, FreeBSD and more.
</h4>

<p align="center">
    </a>
    <a href="https://discord.gg/HstXbPch7X">
        <img src="https://img.shields.io/discord/1367654015269339267?color=5865F2&label=Eden&logo=discord&logoColor=white"
            alt="Discord">
    </a>
    <a href="https://stt.gg/qKgFEAbH">
        <img src="https://img.shields.io/revolt/invite/qKgFEAbH?color=d61f3a&label=Stoat"
            alt="Stoat">
    </a>
</p>

<p align="center">
  <a href="#compatibility">Compatibility</a> |
  <a href="#development">Development</a> |
  <a href="#building">Building</a> |
  <a href="#download">Download</a> |
  <a href="#support">Support</a> |
  <a href="#license">License</a>
</p>

## Compatibility

The emulator is capable of running commercial Switch games at full speed, provided your device meets the necessary hardware requirements.

### Handheld Compatibility List (8 GB Devices)

| Game Title | Title ID | Tested Device | Status | Target FPS | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Xenoblade Chronicles 3** | `010074F013262000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Solid 30 fps in open world; momentary dips to ~20 fps only during shader compilation or memory reclamation. Tested 46+ min continuous play without crashes. **GPU overclocked to 905 MHz**. |

#### Xenoblade Chronicles 3 Recommended Configuration

- **Target Device:** Retroid Pocket 5 (Snapdragon 865 / Adreno 650, 8 GB RAM)
- **Hardware Profile:** GPU overclocked to **905 MHz** (via handheld performance mode / governor)
- **Graphics Driver:** Mesa Turnip (e.g. Turnip 24.2.99 / 24.3 rev 9)
- **Recommended Engine & Graphics Settings:**
  - **Memory Governor:** Enabled (`use_memory_governor = true`)
  - **Safety Reserve Margin:** 140 MB
  - **Isolate Texture Cache GC:** Enabled (`texture_cache_gc_isolated = true`)
  - **VRAM Usage Mode:** Conservative
  - **Async Presentation:** Enabled
  - **Enable Compute Pipelines:** Enabled
  - **Vulkan Driver Pipeline Cache:** Enabled
  - **Barrier Feedback Loops:** Enabled
  - **Optimized Vertex Buffers:** Disabled (requires Mesa Turnip 26.0+)
  - **Force Maximum Clocks:** Disabled (hardware GPU is already overclocked to 905 MHz)
- **Verified Telemetry & Performance:**
  - **Framerate:** Stable **30.0 FPS** native; brief dips to ~20 FPS during shader building or memory trim passes
  - **Texture Cache:** Settles naturally at **~1,000 MB** (avg: 1,001.1 MB, peak: 1,375.7 MB) with 0.0 unmanaged ballooning
  - **Buffer Cache:** ~**315 MB**
  - **Memory Headroom:** Available RAM averages **~116 MB**; safe, controlled dips to ~40 MB handled cleanly by the memory governor without LMKD kills or kernel panics
  - **Session Stability:** 46.5+ minutes continuous play session verified (77,520+ frames rendered) with zero crashes

Check out our [website](https://eden-emu.dev) for the latest news on exciting features, monthly progress reports, and more!

[![Packaging status](https://repology.org/badge/vertical-allrepos/eden-emulator.svg)](https://repology.org/project/eden-emulator/versions)

## Contribute

To contribute to Eden; be it financially, code, bug reports, or otherwise, see our [Contributing guidelines](./CONTRIBUTING.md).

## Documentation

We have a user manual! See our [User Handbook](./docs/user/README.md).

## Building

See the [General Build Guide](docs/Build.md)

For information on provided development tooling, see the [Tools directory](./tools)

## Download

You can download the latest releases from [our release page](https://git.eden-emu.dev/eden-emu/eden/releases).

Save us some bandwidth! We have [mirrors available](./docs/user/ThirdParty.md#mirrors) as well.

## License

Eden is licensed under the GPLv3 (or any later version). Refer to the [LICENSE.txt](https://git.eden-emu.dev/eden-emu/eden/src/branch/master/LICENSE.txt) file.

## Eden-Bounded Fork & Upstream Provenance

**Eden-Bounded** is a fork of the [Eden Nintendo Switch emulator](https://git.eden-emu.dev/eden-emu/eden) (`eden-emu/eden`, GPLv3), with the single mission of running memory-hungry Switch titles (such as *Xenoblade Chronicles 3*) reliably on 8 GB Android handhelds without being terminated by OS low-memory killers (LMKD/OOM).

- **Upstream Repository:** `https://git.eden-emu.dev/eden-emu/eden` (GitHub mirror: `https://github.com/eden-emulator/eden`)
- **Upstream Base Commit:** `de7e6e01d43e8cf2ec193d74fa053b0d7949955f` (recorded in `UPSTREAM_BASE.txt`)
- **License:** GNU General Public License v3.0 or later (GPL-3.0-or-later).
- **Core Strategy:** Replace implicit "grow until driver crashes" memory allocations with an explicit process-wide memory governor and bounded cache policies.
- **Diagnostics Build:** Phase 0 observability subsystem (`v0.1-memdiag`) exposes fine-grained per-subsystem telemetry and Android `dumpsys`/overlay reporting.

## Special thanks

Super special thanks to Cloudflare for preventing the git server from blowing up.

- Yuzu
- Ryujinx
- Sudachi
- Citron
- Torzu
- Suyu
- Ryubing

And everyone who continues or had contributed to the project! <3
