# Why Xenoblade Chronicles 3 kills the Retroid Pocket 5 (8 GB) on Eden — a memory model

**Status:** diagnosis / explanation only. No fixes applied, no solutions proposed here.
**Sources:** Eden `master` source tree read directly (GitHub mirror of `git.eden-emu.dev/eden-emu/eden`, cloned 2026-10-18, ~commit `7e6e6e01d4`). File + line references are to that tree, and are the *actual* code paths, not paraphrase. Two upstream corroborating data points are cited at the end.
**Device assumption:** RP5 8 GB, Android, "Virtual RAM" = 4 GB zram + 4 GB (presumably) disk swap, Eden Nightly, Vulkan, ~25 fps before death, ~10 min to kernel panic.

---

## 0. The one-sentence version

Your five-term budget (VRAM + guest + Android + pipeline + texture ≈ 9 GB) is arithmetically right but **structurally wrong**: those terms are not five independent buckets. Three of them (VRAM, texture cache, pipeline cache) are *the same physical memory* on an integrated GPU, "guest memory" is **already counted** inside VRAM because the Switch's RAM and VRAM are emulated views onto one host allocation, and the term that is actually growing without bound at 25 fps is **not** a cache with a size policy — it's the Vulkan buffer cache, the descriptor ring, and the *reserved-but-touched* guest heap. The OS death is a separate event (zram saturation → LMKD/PSI → foreground kill with a kernel-level symptom), triggered by the growth rate, not by the total.

So: two different failures are being conflated.
- **Failure A (the 10-minute clock):** a monotonic consumption curve in the *host* address space that the emulator never budgets against.
- **Failure B (the panic):** what Linux/Android does when that curve meets a 4 GB zram device and a swap file that cannot absorb write-back under sustained 25 fps pressure.

---

## 1. Physical reality: what is actually allocated, and where

### 1.1 There is one 4 GB+ heap, not a guest heap and a separate VRAM heap

The Switch's physical memory and its VRAM are two *views* of the same 4 GB (the GPU uses system RAM). Eden models this exactly:

- `src/common/host_memory.cpp` reserves a large virtual arena and creates a **backing store** for it:
  - `memfd_create` + `ftruncate(fd, backing_size)` + `mmap(..., MAP_SHARED, fd, 0)` — lines ~530–561 — or
  - if `memfd_create` or `ftruncate` fails (Android `memfd_create` exists but `ftruncate` on a large memfd can fail under `/dev/shm`-style limits), it falls back to `mmap(..., MAP_ANONYMOUS | MAP_PRIVATE)` and logs `"Using private mappings instead of shared ones"`.
- The *backing* is `4_GiB + 128_MiB` of Switch DRAM (the `+128 MiB` is the service/applet carve-out) — this is the "guest memory" you counted as 4 GB host.
- The *virtual* arena is 2^36 bytes (64 GiB) reserved with `MAP_NORESERVE` (`ChooseVirtualBase`, `host_memory.cpp` ~line 437) — free at reserve time. Note it is placed **above the 36-bit boundary** deliberately, with the comment *"For Qualcomm devices, we must also allocate memory above 36 bits"*; this is an Adreno/Turnip requirement, not an accident.
- **The backing store itself is NOT `MAP_NORESERVE`.** Both paths — memfd `MAP_SHARED` (line ~561) and the anonymous fallback `MAP_ANONYMOUS | MAP_PRIVATE` (line ~555) — are plain overcommitted mappings. So the kernel's commit accounting treats the full 4.1 GB as *promised* from the moment Eden starts, while RSS only grows as pages are touched. On Android, where `vm.overcommit_memory` is typically `0` (heuristic) and there is no swap-backed commit guarantee for a memfd, this means: **the commitment is made up-front, and the OOM risk accrues silently as the game touches pages** — which is exactly the profile of "runs fine, then dies at 10 minutes" rather than "refuses to launch".
- `HostMemory::Map()` (line ~752) and the POSIX `Impl::Map()` (line ~593) `mmap` windows of the backing into the arena. `HeapTracker` (`src/common/heap_tracker.cpp`) keeps a bounded number of these resident (`/proc/sys/vm/map_map_count - 20000`, evicting **half** the resident mappings at once when exceeded — `RebuildSeparateHeapAddressSpace`).

**Consequence for your budget:** the guest's 3.2 GB of used DRAM *is* host RSS, and the textures/rendertargets the GPU writes are `vkAllocateMemory`-backed allocations that, on an integrated GPU, **alias the same physical DRAM**. Adding "VRAM 1 GB" *on top of* "game 4 GB" double-counts the DRAM, but **also** under-counts, because every `vkAllocateMemory` allocation is a *separate* commitment that the driver cannot overlap with the guest pages the game thinks it owns. On integrated GPUs the driver does **not** deduplicate an emulated draw target against the emulated guest page it came from. So the honest model is:

```
host RSS ≈ HALO/reserved (0.3–0.6 GB)
         + guest DRAM touched (up to 4.0–4.1 GB)
         + vkAllocateMemory total (textures + buffers + descriptor ring + staging)
           − (small overlap the driver happens to alias)
```

The `vkAllocateMemory` term is where the growth lives, and it is **not** visible to any of Eden's own budgets (see §2).

### 1.2 What the emulator *thinks* it is allowed to use

`src/video_core/vulkan_common/vulkan_device.cpp`, `Device::CollectPhysicalMemoryInfo()` (lines ~1616–1656):

```cpp
if (is_integrated) {
    const s64 available_memory = device_access_memory - device_initial_usage;
    const u64 memory_size = vram_usage_mode == Aggressive ? 6_GiB : 4_GiB;
    device_access_memory = max(min(available_memory - 8_GiB, memory_size),
                               min(local_memory, memory_size));
}
```

Read that carefully. On integrated GPUs, `device_access_memory` (the number the texture cache treats as "the GPU heap") is:

- `available_memory - 8_GiB` — on an 8 GB device this is **≤ 0**, so
- clamped up to `min(local_memory, 4_GiB)` — the physical device-local heap size as *reported by the driver*,
- and then `VmaAllocatorCreateInfo.preferredLargeHeapBlockSize` is set to **64 MiB** for integrated GPUs (vs 256 MiB on discrete) — `vulkan_device.cpp` ~line 755.

This is the first structural bug for your case: the "VRAM budget" is a **fiction of the reported heap**, and with Turnip on Adreno the reported device-local heap is essentially the whole system RAM. So Eden believes it has 4 GB of GPU memory available when the process has ~2 GB of *usable* RSS headroom.

### 1.3 The texture cache's budget is computed from that fiction

`src/video_core/texture_cache/texture_cache.h`, constructor (lines ~60–77):

```cpp
if constexpr (HAS_DEVICE_MEMORY_INFO) {
    const s64 device_local_memory = runtime.GetDeviceLocalMemory();
    const s64 mem_threshold   = min(device_local_memory, TARGET_THRESHOLD /* 4_GiB */);
    const s64 min_vacancy_expected = (6 * mem_threshold) / 10;
    const s64 min_vacancy_critical = (2 * mem_threshold) / 10;
    expected_memory = max(min(device_local_memory - min_vacancy_expected,
                              device_local_memory - 1_GiB), DEFAULT_EXPECTED_MEMORY /* 1.125 GiB */);
    critical_memory = max(min(device_local_memory - min_vacancy_critical,
                              device_local_memory - 512_MiB), DEFAULT_CRITICAL_MEMORY /* 1.625 GiB */);
    minimum_memory  = (device_local_memory - mem_threshold) / 2;   // = 0 when heap == 4 GiB
}
```

With `device_local_memory ≈ 4 GiB` (or larger), `expected_memory ≈ 2.5 GiB` and `critical_memory ≈ 2.9 GiB`, and `minimum_memory = 0`.

`TickFrame()` (same file, ~line 170):

```cpp
if (runtime.CanReportMemoryUsage()) total_used_memory = runtime.GetDeviceMemoryUsage();
if (total_used_memory > minimum_memory) RunGarbageCollector();
```

`GetDeviceMemoryUsage()` → `Device::GetDeviceMemoryUsage()` → `VK_EXT_memory_budget`'s `heapUsage` summed over `valid_heap_memory`. **It is not the texture cache's usage — it is the whole process's Vulkan allocation total.** So the texture GC is being driven by a number that includes the buffer cache, the descriptor ring, staging, and every other allocation, and it only becomes aggressive above ~2.9 GiB. On your device that is *already past the point of no return*.

### 1.4 The GC is a deferred, LRU, "polite" collector

`RunGarbageCollector()` (~lines 117–166):

- Normal mode: evict only images unused for **50 ticks (frames)**, at most **10 iterations**.
- High priority (`total_used_memory ≥ expected`): 25 ticks, 20 iterations.
- Aggressive (`≥ critical`): 10 ticks, **40 iterations**, and it may *download* images back to guest memory first (`DownloadStagingBuffer`, `Finish()` — a full GPU sync) before deleting.
- Images marked `CostlyLoad` are **never** collected outside aggressive mode (`(!aggressive_mode && CostlyLoad) → skip`).
- Deletion itself is deferred: `DelayedDestructionRing<Image, TICKS_TO_DESTROY>` with `TICKS_TO_DESTROY = 8` frames, and `DeleteImage(id, immediate)` where immediate only happens when `image.scale_tick > frame_tick + 5`.

At 25 fps you get 25 GC opportunities per second. A tick is a *frame*, and if the emulator is running slowly, 50 ticks = 2 seconds of wall-clock the image survives, but each frame you may also be **creating** new images faster than 40 evictions can retire. **This is a throughput mismatch, not a leak in the strict sense.** That distinction matters enormously (see §2.4).

### 1.5 There is no budget at all for the buffer cache, descriptor ring, or staging

- `src/video_core/renderer_vulkan/vk_buffer_cache.cpp` / `buffer_cache_base.h`: buffers are keyed by guest address and grown as needed; there is **no** total-size cap and **no** GC pass comparable to the texture cache's. The only pressure valve is `memory_tracker_base.h` / `usage_tracker.h` (dirty-page tracking), which is about *correctness*, not residency.
- `DescriptorBufferRing` (`vk_descriptor_buffer.h/.cpp`): ring of descriptor buffers; grows and is only recycled at frame boundaries.
- `vk_staging_buffer_pool.cpp`: staging buffers are pooled and **not returned to the driver** — they are kept for reuse ("the pool never shrinks"). This is a deliberate upstream design (allocation churn causes Adreno driver bugs and huge latency), and it is a permanent RSS floor once XC3's streaming has touched large staging sizes.
- Shader pools: `ShaderPools { ObjectPool<IR::Inst> inst{8192}; ObjectPool<IR::Block> block{32}; ObjectPool<Flow::Block> flow_block{32}; }` in `vk_pipeline_cache.h` — these are **per-thread** worker pools and `ReleaseContents()` is only called at teardown. XC3 has the largest shader count of the Monolith games; the IR object pools for the pipeline workers pin tens to low hundreds of MB.

**Net:** the only memory the emulator actively manages is the texture cache. Everything else is "allocate on demand, keep forever, hope."

### 1.6 Shaders: there is no pipeline-cache eviction path

`Vulkan::PipelineCache` (`vk_pipeline_cache.h`, lines 105–188) holds:

```cpp
::Common::unordered_map<ComputePipelineCacheKey, std::unique_ptr<ComputePipeline>>  compute_cache;
::Common::unordered_map<GraphicsPipelineCacheKey, std::unique_ptr<GraphicsPipeline>> graphics_cache;
```

with **no** `GarbageCollect`, no tick counter, no LRU, no size cap. Pipelines are inserted and only removed when the cache object dies. `GraphicsPipeline` holds `vk::Pipeline` handles whose backing is a driver allocation counted in the same `heapUsage` as everything else.

XC3 is one of the worst cases in the library for this: Monolith's deferred renderer produces a very large combination space of specialised pipelines (per-material, per-pass, per-shadow-cascade variants). Over 10 minutes of play at 25 fps you are generating *new* permutations continuously. Neither Eden nor Turnip has a budget for `vkPipelineCacheData` growth.

Corroboration: Eden's own release notes list *"Fixed some memory leaks present in the shader compiler"* and *"VRAM usage now works properly on integrated graphics"* as in-flight work — i.e. this area is known-churny upstream.

---

## 2. The growth curve: five distinct mechanisms, ranked

### 2.1 Primary suspect — descriptor ring + command buffers + fence pacing at 25 fps

Eden uses a ring of frames-in-flight with a `MasterSemaphore` (timeline semaphore) in `vk_scheduler.cpp`/`vk_master_semaphore.cpp`, and a `DescriptorBufferRing`. Every frame the GPU doesn't finish in time, the CPU keeps recording into *new* ring slots rather than stalling. At 25 fps on an Adreno 650-class GPU, XC3's draw submission is CPU-limited: the CPU is *ahead* of the GPU most of the time. The ring therefore fills to its maximum depth permanently and each slot's descriptor buffer + command pool block stays resident forever. This is bounded in theory (ring depth is a constant), but the *constant* is sized for desktop and the buffers per slot are sized for the game's descriptor count — XC3 uses a lot of descriptors (large material tables). This is a fixed 100–400 MB floor, not the growth.

### 2.2 Primary suspect — texture "resurrection" churn (the real growth)

This is the mechanism that produces your "VRAM keeps rising" observation, and it is specific to streaming open-world games:

1. XC3 streams a new region: new images created (`InsertImage`, `total_used_memory += AlignUp(size, 1024)` — line ~2156).
2. Old region's images go unused. They become eligible for LRU eviction **after 50 frames** (2 s at 25 fps).
3. Before 50 frames elapse, the camera or a cutscene re-references an old image → its tick is refreshed → it is **not** evicted.
4. Repeated region transitions (the "cutscene" trigger in every XC3 report — see §5) mean the working set is always ~2 regions wide and never settles.
5. `JoinImages` / alias resolution creates *additional* images that alias existing guest pages; when an alias is created, both are resident. `SynchronizeAliases` copies between them.
6. `CostlyLoad` images (BC-compressed textures that were expensive to decode) are **exempt from normal-mode GC entirely**. XC3's compressed textures all qualify. So even when `total_used_memory` crosses `expected_memory`, the normal pass skips exactly the biggest items, and you only start reclaiming them at `critical_memory` — which, per §1.3, is *after* you're already dying.

The result is a sawtooth that ratchets up: each region transition adds high-water mark; the GC only claws back aggressively once already past 2.9 GiB; and because `GetDeviceMemoryUsage()` counts *everything*, the GC starts firing on memory it cannot free, then gives up (`AllowAggressive` is only reached when `total_used_memory >= critical_memory`, and even then `num_iterations` is capped at 40).

### 2.3 Secondary — the "guest 3.2 GB" is not a constant, it is a *commitment*

Four things make the guest heap bigger than the game's own 3.2 GB:

- **`EnableDirectMappedAddress`** (POSIX, ~line 650 in `host_memory.cpp`): Eden unmaps the arena and re-maps the backing *directly* as the guest address space, so guest writes go straight to backing pages with no tracking layer. That removes the boundary at which `HeapTracker` could evict separate-heap maps — but it is also the only way to get usable performance. On a memory-starved device this is a direct trade: speed for the inability to reclaim untouched guest pages.
- **`MADV_HUGEPAGE`** is issued over the whole arena (`madvise(virtual_base, virtual_size, MADV_HUGEPAGE)`). THP collapses pages into 2 MiB blocks; a single touched byte can therefore commit 2 MiB of RSS. On a 4 GB heap with fragmented access this measurably inflates RSS and defeats zram's compression ratio (2 MiB hugepages compress worse and split under pressure — khugepaged activity itself is a source of PSI stalls).
- **`MAP_NOCORE`/`MADV_DONTDUMP`** is set, so these pages never appear in a coredump — if you've been reading `/proc/pid/smaps` or a crash log, this is why guest pages may look absent.
- **The heap does not shrink.** `KPageHeap::Free` returns pages to the guest's own allocator (`k_page_heap.h`), and the guest allocator may or may not hand them back to the OS. Nothing in Eden calls `madvise(MADV_DONTNEED)` on freed guest ranges. Whether a freed guest page becomes reusable host RAM depends *entirely on the guest*, i.e. on Monolith's allocator behaviour — which is known to be sticky (long-lived arenas, no `free`).

### 2.4 Secondary — the shader/pipeline population (§1.6), plus disk cache serialisation

`SerializeVulkanPipelineCache` → `QueueVulkanPipelineCacheFlush` with `pipelines_since_flush` / `last_flush` / `last_cache_size` atomics. At each flush, `vkGetPipelineCacheData` **allocates a full copy of the pipeline cache in host memory**, then writes it out on a `serialization_thread` (`Common::ThreadWorker`). At 25 fps with continuous new permutations, this is a repeating allocation of a growing blob — and `last_cache_size` exists precisely because this operation is known to be memory-hungry. Each serialisation is a transient spike of *cache size* bytes, and the file keeps growing across sessions.

---

## 3. Why swap makes it worse, not better (Failure B)

You have 4 GB zram + 4 GB swap. Even in the best case that gives +8 GB of *address space*, not +8 GB of *speed*, and there are three concrete reasons it doesn't save you:

### 3.1 memfd pages are not ordinary anonymous pages

If Eden's backing store is a `memfd_shmem` object (`MAP_SHARED` on a memfd — the success path in `host_memory.cpp`), those pages are **shmem/tmpfs pages**. Mainline Linux swap support for shmem is conditional (`CONFIG_SHMEM` swap support + the `shmem_swp` machinery), and on Android kernels built for zram-only devices it is frequently unavailable or effectively never exercised. zram is a *swap device*; it cannot compress a shmem page that the kernel has no swap entry for. **If you are on the memfd path, a large part of your guest heap is structurally unswappable.**

If you are on the *fallback* path (`"Using private mappings instead of shared ones"` — check your Eden log for that line), the pages are plain anonymous and *are* swappable. **This one log line changes the entire memory behaviour of the device**, and it is a diagnostic you must capture before anything else.

### 3.2 Even swappable, the working set can't be compressed

zram's win is on *cold, compressible* pages (textures of regions you've left, code, heap slack). XC3's growth is precisely the opposite: active textures, active command buffers, active descriptor data. These are:
- **hot** (re-referenced within 50 frames, so above the LRU's eviction tick), and
- **high-entropy** (compressed texture data, GPU command streams) — real-world compression on GPU-shaped data is often 1.1–1.5×, not 3–4×.

So 4 GB of zram might hold 1.5–2 GB of your working set *if* the pages were swappable and *if* they were cold. Neither holds.

### 3.3 The failure mode you call "kernel panic" is downstream of a livelock

Once free memory hits the watermark, the kernel enters **direct reclaim on the allocation path**: a thread that needs a page must first free pages, which on a zram device means compressing another page, which may itself need a page, which may need to free, … At 25 fps the emulator cannot slow down (it's not frame-rate-aware of memory pressure), so it keeps allocating. Android's `lmkd` watches PSI (`/proc/pressure/memory`) and kills the foreground process — but the *symptom* you see first is the device freezing, watchdog timeouts, and eventually a kernel BUG/oops if a driver (Adreno/Turnip, or the ION/dma-heap allocation path) is entered under extreme pressure or runs its own OOM path with a fatal allocation.

**A GPU driver allocating device memory inside an OOM-reclaim context is a well-known source of kernel oopses.** Whether yours is that or a watchdog is determinable from `/sys/fs/pstore/console-ramoops` or `dmesg` after the reboot — that file survives the panic and will name the faulting path.

**This is why the swap doesn't help:** it delays the OOM, but it converts a *clean* process kill (recoverable, ~2 s) into *prolonged reclaim thrash* (device frozen for tens of seconds, watchdog fires). The 4 GB zram + 4 GB swap configuration is actively raising the probability of the kernel-level crash you are trying to avoid.

---

## 4. Instrumentation: how to prove which mechanism dominates

You don't need to guess which of §2.1–§2.4 is eating the RAM. Each produces a *characteristically different curve*. Capture these while the game runs, in this order:

### 4.1 Before launch — establish the baseline and the mapping path

```
adb shell dumpsys meminfo org.yuzu.yuzu_emu     # before start
adb shell cat /proc/meminfo | grep -Ei 'MemTotal|MemAvailable|SwapTotal|SwapFree|Shmem|ShmemPmd|CmaFree|SReclaimable'
adb shell cat /proc/swaps                       # confirm zram0 + file swap sizes
adb shell cat /sys/block/zram0/mm_stat          # orig_data_size / compr_data_size / mem_used
adb shell cat /proc/pressure/memory             # PSI: some/full avg10
adb shell getprop ro.config.low_ram ; adb shell getprop ro.lmk.*
```

In the Eden log (Settings → Debug → log to file), confirm **which** of these appear:
- `Using private mappings instead of shared ones` → §3.1 fallback path (swappable, but different perf)
- `Current maximum map count: N` → HeapTracker limit
- `memfd_create: ...` / `ftruncate: ...` warnings
- any `Render_Vulkan` line naming `heapBudget`/`heapUsage`

### 4.2 During play — sample every 30 s from a second shell

```
while sleep 30; do
  adb shell dumpsys meminfo org.yuzu.yuzu_emu | grep -E 'TOTAL|Native Heap|Graphics|GL mtrack|EGL mtrack'
  adb shell cat /proc/pressure/memory | head -3
  adb shell cat /sys/block/zram0/mm_stat | awk '{print "zram orig="$1" compr="$2" used="$3}'
  adb shell dumpsys meminfo | grep -E 'Free RAM|Used RAM|Lost RAM|ZRAM|SwapPss'
done
```

`dumpsys meminfo`'s **Graphics** / **GL mtrack** lines are the GPU-side total (they map to `vkAllocateMemory`); **Native Heap** is everything else. The *ratio* of their growth identifies the culprit:

| Observation | Dominant mechanism | Section |
|---|---|---|
| GL mtrack grows steadily even standing still in one area | Descriptor ring + command/query pools + staging not recycling | 2.1 |
| GL mtrack/KB-per-second grows only when moving between regions or during cutscenes | Texture resurrection churn / CostlyLoad exemption | 2.2 |
| Native Heap grows, GL mtrack flat | Pipeline IR pools, pipeline-cache serialisation, shader compiler | 2.4 |
| MemAvailable falls but `Pss` flat, `Shmem` rising | Guest heap on memfd, unswappable | 3.1 |
| `zram compr_data_size` climbs but `SwapFree` barely moves | Working set is incompressible → swap can't buy headroom | 3.2 |
| PSI `full avg10` above ~20–30 before the kill | You are already in thrash; the kill is LMKD, not OOM | 3.3 |

### 4.3 The single most informative sample — per-range RSS of the arena

With adb root (RP5 is unlockable), while Eden is running and *just before* it dies:

```
adb shell "cat /proc/$(pidof org.yuzu.yuzu_emu)/smaps_rollup"
adb shell "grep -E 'Rss|Pss|Swap|Anonymous|Shared' /proc/$(pidof org.yuzu.yuzu_emu)/smaps | awk ..."
```

`Anonymous` vs `Shared` split in `smaps` tells you whether your guest heap is memfd-backed (`Shared`) or anonymous (`Anonymous`) — the §3.1 question, answered empirically rather than inferred.

### 4.4 After the crash — the panic's cause

```
adb shell cat /sys/fs/pstore/console-ramoops     # survives reboot
adb shell cat /sys/fs/pstore/dmesg-ramoops-0
adb shell ls /data/vendor/dontpanic 2>/dev/null
```

Look for: `lmkd` kill reason lines (`reason: pressure` / `super critical pressure event`), `Out of memory: Killed process`, `page allocation failure`, `watchdog`, and any `msm`/`kgsl`/`adreno` frame preempt timeout. **These are different bugs with different fixes**, which is why fixing this matters before designing the fork.

---

## 5. Corroborating external evidence

1. **Eden issue #543** (feature request, closed *as not planned* by maintainer `crueter`, 2026-06-13): user on a Mali device asked for a hard RAM cap for low-end devices, noting the Android skin "has terrible memory management and will kill anything that exceeds the ~3.6 GB PSS limit". Maintainer's response: *"Not realistically possible, we also at minimum require 8 GB."* — i.e. **a global cap is explicitly out of scope upstream**, which is exactly the gap a fork can fill.
2. **Eden issue #557** (`[Crash] RAM leak`, 2026-06-22): on an AYN Thor, with PurpleVK/Turnip and both VRAM modes, **LMKD killed Eden** (`oom_score_adj 50`, one test at `oom_score_adj 0`, reason `device is not responding`). Critically: *"Same June 10 build with Handheld, 0.5x resolution, VRAM Conservative, BC3 ASTC recompression, and fresh shader cache — the minimum-VRAM profile did not lower the fatal RSS peak, **suggesting the dominant allocation is not render-resolution or texture residency**."* This is direct upstream evidence for §2.1/§2.4 over §2.2, and for the fact that the existing levers don't touch the real cost.
3. **XC3-specific history** (GBAtemp XC3 emulator thread, 2022, yuzu EA era): multiple users report RAM going 7 GB → 14–15 GB specifically **after a cutscene**, on Vulkan, on 16 GB machines — the same "region/cutscene transition ratchets the high-water mark" signature as §2.2, on hardware where it merely thrashed instead of dying.
4. **Eden release notes** (2026) list, as recent work: *"Optimized dummy thread storage"*, *"Fixed some memory leaks present in the shader compiler"*, *"VRAM usage now works properly on integrated graphics"*, *"Improved Vulkan memory allocation… VRAM usage now works properly on integrated graphics"*. Translation: the integrated-GPU VRAM accounting (§1.2) was only recently and incompletely addressed.

---

## 6. Summary table — what your five terms actually are

| Your term | What it really is in Eden | Bounded? | Counts guest? | Managed by |
|---|---|---|---|---|
| "Game mem (host) 4 GB" | `HostMemory` backing ((memfd or anon), + `HeapTracker` resident window | by guest allocator only | — | guest; no `MADV_DONTNEED` on free |
| "VRAM 1 GB" | `vkAllocateMemory` total (`heapUsage`), *includes textures + buffers + ring + staging* | **no** except texture cache | aliases guest DRAM | driver/VMA, 64 MiB blocks |
| "Texture cache 1 GB" | `slot_images` images + `DelayedDestructionRing(8 frames)` | LRU @ 50/25/10 frames, ≤40 iters; `CostlyLoad` exempt | yes (aliases) | `RunGarbageCollector` |
| "Pipeline cache 1 GB" | `graphics_cache`/`compute_cache` maps + at-driver `vkPipelineCache` + disk blob copy | **no eviction path at all** | no | nothing |
| "Android RAM 2 GB" | HALO + system_server + launcher + SurfaceFlinger + Adreno driver + Eden Kotlin/JVM | — | no | LMKD/PSI |

And the term that is *missing* from your budget entirely: **the process's own Vulkan driver internal allocations on Turnip/Adreno** — shader module backing, pipeline object backing, descriptor set backing, command pool backing. These are invisible to `heapUsage` accounting on some driver builds and are the classic source of "the emulator says 2 GB, `dumpsys` says 6 GB".

---

## 7. What a fork would have to change (statement of problem, not a solution)

*This section exists only because the fork prompt in the companion file needs a precise problem statement. It is not a fix recommendation for your device.*

1. **A process-wide memory governor.** One object that owns a *real* budget derived from `RLIMIT_AS`/`MemAvailable`/cgroup headroom (not from `VK_EXT_memory_budget`'s heap fiction on integrated GPUs), and that every subsystem (texture, buffer, descriptor ring, staging pool, pipeline cache, IR pools) registers with and can be asked to shrink *to a target*, not "when something is over".
2. **Eviction for the pipeline cache**, with on-disk backing so evicted pipelines are cheap to rebuild (this is the one place where the disk cache becomes a memory-relief mechanism rather than just a stutter-avoider).
3. **A shrink path for staging pools** (`vk_staging_buffer_pool.cpp` currently never returns memory), with an idle timeout.
4. **Removal of the `CostlyLoad` exemption under pressure**, or an explicit policy: past the target, everything is evictable and re-decodable.
5. **A pressure-aware frame pacer**: when the governor reports pressure, *intentionally* reduce frames-in-flight / insert GPU waits, because on this device the correct trade is 20 fps with bounded memory vs 25 fps with death.
6. **Guest heap reclamation**: `madvise(MADV_DONTNEED)` or `MADV_FREE` on guest-freed ranges, and disabling/instrumenting `MADV_HUGEPAGE` on memory-constrained builds (hugepages hurt zram effectiveness — §2.3).
7. **Telemetry that matters**: a `dumpsys`-visible or logcat budget report (per-subsystem bytes, governor target, evictions/sec), so the next 30 hours of debugging are observation instead of guesswork.

---

## 8. Corrections and confidence

What is *verified from source* (high confidence):
- The integrated-GPU `device_access_memory` clamp and the 8 GiB subtraction (§1.2) — read verbatim.
- `expected_memory`/`critical_memory`/`minimum_memory` formulas and the `TICKS_TO_DESTROY = 8` / 50-tick / 40-iteration GC parameters (§1.3–1.4).
- `CostlyLoad` images being exempt from non-aggressive GC (§1.4).
- `PipelineCache` having **no** eviction path whatsoever (§1.6).
- Backing store being plain (non-`MAP_NORESERVE`) with a memfd-or-anonymous fork (§1.1).
- `MADV_HUGEPAGE` on the arena and **no** `MADV_DONTNEED`/`MADV_FREE` anywhere (§2.3).
- The 36-bit placement requirement for Qualcomm (§1.1).

What is *inferred* (medium confidence — the ranking could shift once §4.2 telemetry is collected):
- The exact split between §2.1 (ring/pools), §2.2 (texture churn) and §2.4 (pipelines) for XC3 specifically.
- The claim that the memfd path dominates your failure. This is *testable in one log line* (§4.1) and the whole zram analysis in §3 hinges on it.

What is *uncertain* (stated because it matters):
- Whether your "kernel panic" is a driver oops, a watchdog timeout, or a hard OOM path. §4.4 distinguishes them.
- Turnip's specific `vkAllocateMemory` overhead versus stock Qualcomm — the numbers differ per driver build.
- Whether XC3 actually touches ~3.2 GB of guest DRAM on your settings; that figure came from the user, not from measurement.

**A note on arithmetic in §0:** the "9 GB vs 8 GB" framing over-counts because VRAM/textures alias guest DRAM (§1.1), but *under*-counts because driver-internal allocations are invisible to `heapUsage` on some builds (§6). The two errors partially cancel, which is why the budget "looks about right" while the machine still dies — and why tuning the numbers you listed will never fix it.

---

*Prepared 2026-10-18. Line numbers are approximate (±10) and refer to Eden master as cloned that day; verify before citing in a PR.*
