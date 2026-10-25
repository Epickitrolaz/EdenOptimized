<!--
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<h1 align="center">
  <br>
  <img src="./dist/qt_themes/default/icons/256x256/eden.png" alt="Eden Optimized" width="160">
  <br>
  <b>Eden Optimized</b>
  <br>
</h1>

<h4 align="center">
A performance and memory-governed fork of the <a href="https://git.eden-emu.dev/eden-emu/eden">Eden Nintendo Switch emulator</a>, engineered for stability and native 30 fps gameplay on 8 GB Android handhelds.
</h4>

---

## Overview

On 8 GB Android handheld gaming devices (such as the Retroid Pocket 5 and Odin 2), memory-intensive open-world Switch titles (like *Xenoblade Chronicles 3* and *The Legend of Zelda: Tears of the Kingdom*) frequently trigger Android Low Memory Killer (LMKD) kills, SMMU GPU context faults, or full kernel reboots due to unmanaged VRAM accumulation and aggressive texture eviction bugs.

**Eden Optimized** addresses these limitations by introducing a real-time **Memory Governor**, calibrated cache working sets, fault-free texture eviction rings, and low-level engine tuning options to ensure long, crash-free play sessions with consistent framerates.

---

## Key Enhancements

* **Active Memory Governor:** Dynamically monitors real-time Linux available RAM (`/proc/meminfo`) and orders proactive cache shrinkage before available memory approaches Android's low-memory killer thresholds.
* **Fault-Free Texture Eviction:** Combines a 64-frame Vulkan destruction ring cushion with descriptor auto-recovery (`SlotVector::contains()`), guaranteeing that texture pruning never passes stale handles to the GPU or causes hardware context faults.
* **Calibrated Working Sets:** Replaces unmanaged growth with targeted cache floors (1,024 MB Texture Cache target, 320 MB Buffer Cache target) that prevent memory ballooning while keeping active 3D world art stutter-free.
* **Granular Engine Controls:** Exposes low-level settings directly in the Android UI, including Safety Reserve Margin, Texture Cache Isolation, Compute Pipelines, Barrier Feedback Loops, Async Presentation, and Driver Pipeline Cache.
* **Handheld Frontend Integration:** Out-of-the-box launcher compatibility with **iiSU**, **Daijishō**, and **Beacon** using standard Switch emulator package registration (`dev.eden.eden_emulator`).

---

## Handheld Compatibility

### Verified 8 GB Handheld Results

| Game Title | Title ID | Tested Device | Status | Target FPS | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Xenoblade Chronicles 3** | `010074F013262000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Solid 30 fps in open world; momentary dips to ~20 fps only during shader compilation or memory reclamation. Tested 46+ min continuous play without crashes. **GPU overclocked to 905 MHz**. |
| **Xenoblade Chronicles: Definitive Edition** | `0100FF500E34A000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Solid 25–30 fps. Protected against NCE map/menu black screen glitch and memory leak crashes when navigating menus fast. |
| **Xenoblade Chronicles 2** | `0100E95004038000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Locked 30 fps in large titan environments (Gormott, Mor Ardain). Requires **1408 MB (Balanced)** Texture Cache limit to accommodate XC2's ~1.4 GB working set without exceeding 8 GB RAM. Protected against menu black screens and LMKD kills. |
| **Xenoblade Chronicles X: Definitive Edition** | `0100453019AA8000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Verified through Starfall Basin → Primordia with Memory Governor enabled. Protected against GC-era memory crashes and cutscene glitching; LMKD-safe with the stale-tail deficit trim. Requires the default Texture Cache settings (Memory Governor + async shaders on). |

---

### Thermal & Power Characteristics Note

Because **Eden Optimized** continuously runs real-time memory monitoring, periodic LRU texture cache sweeps, ASTC decompressed texture unswizzle/staging evictions, and host memory unmap pruning to prevent Android's Low Memory Killer (LMKD) from terminating games on 8 GB devices, the additional CPU/GPU cache maintenance cycles result in slightly higher SoC temperatures and battery power consumption compared to unconstrained emulators that let RAM balloon freely until a crash.

On handhelds like the Retroid Pocket 5, using active fan cooling (e.g. **Smart** or **Sport** fan mode) is recommended during extended play sessions with memory-heavy titles like *Xenoblade Chronicles 2* and *3*.

---

## Building from Source

### Android Build (APK)

```bash
cd src/android
./gradlew assembleMainlineRelWithDebInfo
```

The compiled APK will be located at:
`src/android/app/build/outputs/apk/mainline/relWithDebInfo/app-mainline-relWithDebInfo.apk`

### Running Unit Tests (Linux / Host)

```bash
cmake -B build-test -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C build-test tests
./build-test/bin/tests [memory_governor],[memory_budget]
```

---

## Upstream Provenance & Attribution

**Eden Optimized** is a fork of the [Eden Nintendo Switch emulator](https://git.eden-emu.dev/eden-emu/eden) (`eden-emu/eden`), distributed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).

* **Upstream Project:** [Eden Emulator](https://git.eden-emu.dev/eden-emu/eden)
* **Upstream Base Commit:** `de7e6e01d43e8cf2ec193d74fa053b0d7949955f` (recorded in `UPSTREAM_BASE.txt`)
* **License:** GNU General Public License v3.0 or later (see [LICENSE.txt](./LICENSE.txt))

### Credits & Acknowledgments

* **The Eden Emulator Project & Developers** (Camille LaVey, crueter, MaranBr, lizzie, and contributors) for the modern, multi-platform Switch emulation codebase, driver innovations, and active development.
* **The Yuzu Emulator Project & Developers** for establishing the foundational open-source Nintendo Switch emulation architecture, reverse-engineering work, and core subsystems.
* **The Emulation Community & Predecessors** (Ryujinx, Sudachi, Citron, Torzu) for shared research and hardware documentation.
