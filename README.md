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

## Handheld Compatibility & Recommended Profiles

### Verified 8 GB Handheld Results

| Game Title | Title ID | Tested Device | Status | Target FPS | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Xenoblade Chronicles 3** | `010074F013262000` | Retroid Pocket 5 (8 GB RAM, Snapdragon 865) | **Playable / Stable** | 30 FPS | Solid 30 fps in open world; momentary dips to ~20 fps only during shader compilation or memory reclamation. Tested 46+ min continuous play without crashes. **GPU overclocked to 905 MHz**. |

### Xenoblade Chronicles 3 Configuration Profile

* **Target Device:** Retroid Pocket 5 (Snapdragon 865 / Adreno 650, 8 GB RAM)
* **Hardware Note:** GPU overclocked to **905 MHz** (via handheld performance mode / governor)
* **Graphics Driver:** Mesa Turnip (e.g. Turnip 24.2.99 / 24.3 rev 9)
* **Settings Changed from Default:**
  * **Enable Memory Governor:** Enabled (`use_memory_governor = true` [default: disabled])
  * **Isolate Texture Cache GC:** Enabled (`texture_cache_gc_isolated = true` [default: disabled])
  * **Enable Compute Pipelines:** Enabled (`enable_compute_pipelines = true` [default: disabled])
  *(All other settings can remain at their defaults: Safety Reserve at 140 MB, VRAM Usage Mode on Conservative, Driver Pipeline Cache enabled, Barrier Feedback Loops enabled, Async Presentation enabled, Optimized Vertex Buffers disabled, and Force Maximum Clocks disabled).*
* **Verified Session Telemetry:**
  * **Framerate:** Locked **30.0 FPS** native (session average: 28.7 FPS including zone loads)
  * **Texture Cache:** Settles naturally at **~1,000 MB** (session average: 1,001.1 MB, peak: 1,375.7 MB) with 0.0 unmanaged ballooning
  * **Buffer Cache:** ~**315 MB**
  * **Memory Headroom:** Available RAM averages **~116 MB**; safe, controlled dips to ~40 MB handled cleanly by the memory governor without LMKD kills or kernel panics
  * **Session Stability:** 46.5+ minutes continuous play session verified (77,520+ frames rendered) with zero crashes

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
