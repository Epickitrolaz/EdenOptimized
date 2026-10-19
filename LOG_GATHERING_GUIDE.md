# Eden-Bounded: Diagnostic & Crash Log Gathering Guide
**Target Device:** Retroid Pocket 5 (8 GB) / Android AArch64 Handhelds  
**Target Games:** Memory-intensive Switch titles (*Xenoblade Chronicles 3*, *Tears of the Kingdom*, etc.)

---

## 1. Quick Reference: Where Logs Live

| Log Artifact | Location / Command | Survives Reboot / Panic? | Primary Purpose |
|---|---|---|---|
| **Persistent Memdiag Trace** | `/sdcard/Download/eden_memdiag.txt` | **YES** (Direct `fsync` appends) | Frame-by-frame memory timeline up to the exact moment of crash |
| **Real-time Logcat** | `adb logcat -s EdenMemoryBudget HW_Memory Render_Vulkan` | No (in-memory circular buffer) | Emulation engine decisions, GC evictions, governor throttles |
| **Rootless dumpsys Report** | `adb shell dumpsys activity org.yuzu.yuzu_emu` | No (live query) | Full snapshot of governor budget, deficit, and 10 subsystem byte counters |
| **Android meminfo** | `adb shell dumpsys meminfo org.yuzu.yuzu_emu` | No (live query) | OS-level PSS, Native Heap, and Graphics (`GL mtrack`) accounting |
| **Kernel Panic Ramoops** | `adb shell cat /sys/fs/pstore/console-ramoops` | **YES** (PStore RAM buffer) | Proves whether crash was LMKD kill, kernel OOM, or Adreno GPU driver panic |

---

## 2. Capturing the Persistent Diagnostics Trace (`eden_memdiag.txt`)

Eden-Bounded runs a dedicated watchdog thread that writes a continuous diagnostic log directly to the device storage with synchronous `fsync()`. Even if the Linux kernel panics or the device instantly black-screens and reboots, this file **survives on disk**.

### How to pull the file via ADB:
```bash
adb pull /sdcard/Download/eden_memdiag.txt ./eden_memdiag.txt
```

### How to retrieve without a PC:
1. Open any Android File Manager on your Retroid Pocket 5.
2. Navigate to `Internal Storage` -> `Download`.
3. Locate `eden_memdiag.txt`.

### What to look for in `eden_memdiag.txt`:
- **The final 10 lines before termination:**
  - `Available RAM`: Check if `MemAvailable` dropped below 200 MB.
  - `PSI full avg10`: Check if pressure exceeded 25–30 (indicates reclaim thrashing).
  - `Subsystem Breakdown`: Shows which subsystem surged (Texture Cache, Buffer Cache, Guest Heap, Pipeline Cache, etc.).
  - `Memory Governor Status`: Shows if Budget was being enforced and whether `Pressure Throttle: ACTIVE` was signaled.

---

## 3. Capturing Live Logs via ADB (While Emulation Runs)

Connect your device over USB with USB Debugging enabled:

### A. Memory Governor & Diagnostics Stream
To monitor governor budget, deficit, evictions, and pressure warnings in real time:
```bash
adb logcat -s EdenMemoryBudget HW_Memory Render_Vulkan YuzuNative
```

### B. Full Logcat Capture (For Issue Reports)
Run this immediately after a crash, freeze, or unexpected shutdown:
```bash
adb logcat -v time -d > logcat_full.txt
```

### C. Check for Android Low Memory Killer (LMKD) kills
To verify if Android OS terminated Eden due to memory exhaustion:
```bash
adb logcat -b events -d | grep -i -E "am_kill|lmk|lowmemorykiller"
```
Or:
```bash
adb logcat -d | grep -i -E "lowmemorykiller|lmkd|Out of memory|killed process"
```
*Signature of LMKD:* `Low memory: Free memory ... LMKD killed org.yuzu.yuzu_emu (pid ...)`

---

## 4. Querying Live Memory Dumps (`dumpsys`)

You can query Eden-Bounded's complete memory telemetry without root permissions while a game is running.

### A. Eden Memory Governor & Subsystem Report
```bash
adb shell dumpsys activity org.yuzu.yuzu_emu > dumpsys_eden.txt
```
This dumps:
1. System RAM & PSI: `MemTotal`, `MemAvailable`, `Active`, `Inactive`, `SwapTotal`, `SwapFree`, `zram orig/compr/used`.
2. Process RSS and high-water mark.
3. Memory Governor Status: Current Budget, Deficit, Deficit Frames, Pressure Throttle state.
4. Per-Subsystem Breakdown: Bytes and evictions/sec across all 10 registered subsystems.
5. Minute-by-minute historical snapshot table.

### B. Android OS Memory Tracker
```bash
adb shell dumpsys meminfo org.yuzu.yuzu_emu > dumpsys_os.txt
```
Examine:
- `TOTAL PSS`: Total memory proportional set size.
- `Native Heap`: Memory allocated via malloc/mmap in C++ runtime.
- `Graphics` / `GL mtrack`: GPU memory allocated via Vulkan (`vkAllocateMemory`).

---

## 5. Investigating Kernel Panics and Reboots (Post-Mortem)

If the Retroid Pocket 5 completely rebooted or powered off, pull the kernel ramoops buffers immediately after booting back up:

```bash
# Pull kernel console log from crash
adb shell cat /sys/fs/pstore/console-ramoops > console_ramoops.txt

# Pull kernel dmesg crash buffer
adb shell cat /sys/fs/pstore/dmesg-ramoops-0 > dmesg_ramoops.txt
```

### Distinguishing Crash Root Causes:
| Log Keyword in Ramoops | Actual Cause | What It Means |
|---|---|---|
| `kgsl` / `adreno` / `turnip` timeout | GPU Driver Hang | The driver hung waiting for a fence or faulted inside an allocation call under OOM pressure. |
| `watchdog: BUG: soft lockup` | Kernel Livelock | Direct-reclaim spent >20s attempting to compress pages into zram while CPU kept submitting draws. Pressure pacing prevents this. |
| `Out of memory: Killed process` | Kernel OOM Killer | System ran out of both available RAM and swap/zram. |
| `super critical pressure event` | Android LMKD Kill | LMKD proactively killed foreground app because PSI pressure remained above threshold. |

---

## 6. Real-Time HUD Overlay Guide

When playing, you can enable real-time memory metrics on the on-screen display:
1. Open Eden Settings -> **Performance Overlay**.
2. Enable **Show Performance Overlay**.
3. Enable **App RAM Usage**.

### Reading the HUD Line:
```
RSS: 3200M | Avail: 750M | PSI: 12.0 | Gov: 3584M(def: 0M) | TC: 820M | BC: 410M | GH: 1820M
```
- **`RSS`**: Current resident set size of the Eden process in RAM.
- **`Avail`**: Total available system RAM reported by `/proc/meminfo`.
- **`PSI`**: Android Memory Pressure Stall Index (`full avg10`). Below 10 is healthy; above 25 indicates heavy page thrashing.
- **`Gov`**: Memory Governor target budget in MB.
- **`def`**: Current memory deficit over the budget.
- **`!THR`**: Appears when persistent deficit triggered active pressure pacing (draw thread throttled to drain GPU queues and protect system from crashing).
- **`TC`**: Texture Cache resident size.
- **`BC`**: Buffer Cache resident size.
- **`GH`**: Guest DRAM heap touched size.
