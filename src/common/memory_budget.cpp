// SPDX-FileCopyrightText: Copyright 2026 Eden-Bounded Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/memory_budget.h"
#include "common/memory_governor.h"
#include "common/settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fmt/format.h>

#include "common/logging.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <unistd.h>
#else
// Linux / Android / BSD
#include <sys/resource.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace Common {

namespace {

constexpr const char* SubsystemDefaultNames[] = {
    "Guest Heap (Touched)",
    "Texture Cache",
    "Buffer Cache",
    "Descriptor Ring",
    "Staging Pool",
    "Query Cache",
    "Command Pool",
    "Pipeline Cache",
    "IR Object Pools",
    "Disk Cache Blob",
};

static_assert(sizeof(SubsystemDefaultNames) / sizeof(SubsystemDefaultNames[0]) ==
              static_cast<size_t>(MemorySubsystem::Count));

constexpr u64 ToMiB(u64 bytes) {
    return bytes / (1024ULL * 1024ULL);
}

constexpr double ToMiBF(u64 bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // anonymous namespace

MemoryBudget& MemoryBudget::Instance() {
    static MemoryBudget instance;
    return instance;
}

MemoryBudget::MemoryBudget() {
    start_time = std::chrono::steady_clock::now();
    last_report_time = start_time;
    last_fps_time = start_time;
    last_overlay_update = start_time;

    // Default target bytes configured for standard 8 GB device profile (6 GB working budget)
    // Subsystem targets default to balanced allocation:
    // - Guest Heap: 3584 MB
    // - Texture Cache: 1024 MB
    // - Buffer Cache: 512 MB
    // - Descriptor Ring: 64 MB
    // - Staging Pool: 128 MB
    // - Query Cache: 32 MB
    // - Command Pool: 32 MB
    // - Pipeline Cache: 128 MB
    // - Object Pools: 64 MB
    // - Disk Cache Blob: 32 MB
    constexpr u64 DefaultTargets[static_cast<size_t>(MemorySubsystem::Count)] = {
        3584ULL * 1024 * 1024, // GuestHeap
        1024ULL * 1024 * 1024, // TextureCache
        512ULL * 1024 * 1024,  // BufferCache
        64ULL * 1024 * 1024,   // DescriptorRing
        128ULL * 1024 * 1024,  // StagingPool
        32ULL * 1024 * 1024,   // QueryCache
        32ULL * 1024 * 1024,   // CommandPool
        128ULL * 1024 * 1024,  // PipelineCache
        64ULL * 1024 * 1024,   // ObjectPools
        32ULL * 1024 * 1024,   // DiskCacheBlob
    };

    for (size_t i = 0; i < static_cast<size_t>(MemorySubsystem::Count); ++i) {
        subsystems[i].id = static_cast<MemorySubsystem>(i);
        subsystems[i].name = SubsystemDefaultNames[i];
        subsystems[i].target_bytes = DefaultTargets[i];
        subsystems[i].last_eviction_sample = start_time;
    }

    AppendTraceToFile("\n===== Eden-Bounded Memory Governor Session Start =====\n");
    AppendTraceToFile("| Minute | Elapsed | RSS (MB) | MemAvail (MB) | PSI full avg10 | FPS | GuestHeap (MB) | TexCache (MB) | BufCache (MB) | PipeCache (MB) | Staging (MB) | DescRing (MB) |\n");
    AppendTraceToFile("|---|---|---|---|---|---|---|---|---|---|---|---|\n");

    watchdog_thread = std::jthread([this](std::stop_token st) { WatchdogLoop(st); });
}

MemoryBudget::~MemoryBudget() {
    watchdog_thread.request_stop();
    if (watchdog_thread.joinable()) {
        watchdog_thread.join();
    }
}

void MemoryBudget::AppendTraceToFile(std::string_view text) {
    static const char* const candidate_paths[] = {
        "/sdcard/Download/eden_memdiag.txt",
        "/data/data/dev.eden.eden_emulator/cache/eden_memdiag.txt",
        "/data/user/0/dev.eden.eden_emulator/files/eden_memdiag.txt",
        "/sdcard/eden_memdiag.txt",
        "/tmp/eden_memdiag.txt",
    };
    for (const char* path : candidate_paths) {
        FILE* fp = std::fopen(path, "a");
        if (fp) {
            std::fwrite(text.data(), 1, text.size(), fp);
            std::fflush(fp);
#if !defined(_WIN32)
            int fd = fileno(fp);
            if (fd >= 0) {
                fsync(fd);
            }
#endif
            std::fclose(fp);
            break;
        }
    }
}

void MemoryBudget::WatchdogLoop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (stop_token.stop_requested() || !enabled) {
            continue;
        }
        MemoryGovernor::Instance().TickWatchdog();
        const auto now = std::chrono::steady_clock::now();
        const auto since_last_report = std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count();
        if (since_last_report >= 5) {
            GenerateReport(true);
        }
    }
}

void MemoryBudget::Reset() {
    std::scoped_lock lk{registration_mutex, history_mutex};
    start_time = std::chrono::steady_clock::now();
    last_report_time = start_time;
    last_fps_time = start_time;
    frame_counter = 0;
    last_fps_frame = 0;
    last_snapshot_minute = 0;
    history.clear();
}

void MemoryBudget::RegisterSubsystem(MemorySubsystem id, std::string_view name,
                                     std::function<u64()> bytes_getter,
                                     u64 default_target_bytes,
                                     std::function<u64()> evictions_getter) {
    const size_t idx = static_cast<size_t>(id);
    if (idx >= subsystems.size()) {
        return;
    }

    std::scoped_lock lk{registration_mutex};
    auto& sub = subsystems[idx];
    sub.id = id;
    if (!name.empty()) {
        sub.name = std::string(name);
    }
    sub.bytes_getter = std::move(bytes_getter);
    sub.evictions_getter = std::move(evictions_getter);
    if (default_target_bytes > 0) {
        sub.target_bytes = default_target_bytes;
    }
    sub.last_bytes = 0;
    sub.last_evictions = 0;
    sub.last_evictions_per_sec = 0.0;
    sub.last_eviction_sample = std::chrono::steady_clock::now();
    sub.registered = true;

    LOG_INFO(HW_Memory, "Registered subsystem counter: '{}' (Target: {:.1f} MB)", sub.name,
             ToMiBF(sub.target_bytes.load()));
}

void MemoryBudget::UnregisterSubsystem(MemorySubsystem id) {
    const size_t idx = static_cast<size_t>(id);
    if (idx >= subsystems.size()) {
        return;
    }

    std::scoped_lock lk{registration_mutex};
    subsystems[idx].registered = false;
    subsystems[idx].bytes_getter = nullptr;
    subsystems[idx].evictions_getter = nullptr;
}

void MemoryBudget::SetSubsystemTarget(MemorySubsystem id, u64 target_bytes) {
    const size_t idx = static_cast<size_t>(id);
    if (idx < subsystems.size()) {
        subsystems[idx].target_bytes = target_bytes;
    }
}

u64 MemoryBudget::GetSubsystemTarget(MemorySubsystem id) const {
    const size_t idx = static_cast<size_t>(id);
    if (idx < subsystems.size()) {
        return subsystems[idx].target_bytes.load();
    }
    return 0;
}

u64 MemoryBudget::GetSubsystemBytes(MemorySubsystem id) const {
    const size_t idx = static_cast<size_t>(id);
    if (idx >= subsystems.size() || !subsystems[idx].registered || !subsystems[idx].bytes_getter) {
        return 0;
    }
    try {
        return subsystems[idx].bytes_getter();
    } catch (...) {
        return 0;
    }
}

double MemoryBudget::GetSubsystemEvictionsPerSec(MemorySubsystem id) const {
    const size_t idx = static_cast<size_t>(id);
    if (idx < subsystems.size()) {
        return subsystems[idx].last_evictions_per_sec;
    }
    return 0.0;
}

SystemMemoryInfo MemoryBudget::ReadSystemMemoryInfo() const {
    SystemMemoryInfo info{};

#if defined(_WIN32)
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        info.total_physical_ram = mem_status.ullTotalPhys;
        info.available_ram = mem_status.ullAvailPhys;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                            sizeof(pmc))) {
        info.process_rss = pmc.WorkingSetSize;
        info.process_anon_rss = pmc.PrivateUsage;
    }
#elif defined(__APPLE__)
    int mib[2]{CTL_HW, HW_MEMSIZE};
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        info.total_physical_ram = static_cast<u64>(memsize);
    }
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stat), &count) == KERN_SUCCESS) {
        info.available_ram = static_cast<u64>(vm_stat.free_count + vm_stat.inactive_count) *
                             static_cast<u64>(sysconf(_SC_PAGESIZE));
    }
    struct mach_task_basic_info task_info;
    mach_msg_type_number_t task_count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&task_info), &task_count) == KERN_SUCCESS) {
        info.process_rss = task_info.resident_size;
    }
#else
    // Linux / Android
    const long page_size = sysconf(_SC_PAGESIZE);
    const long num_pages = sysconf(_SC_PHYS_PAGES);
    if (page_size > 0 && num_pages > 0) {
        info.total_physical_ram = static_cast<u64>(num_pages) * static_cast<u64>(page_size);
    }

    // 1. /proc/meminfo: MemAvailable, SwapTotal, SwapFree
    std::ifstream meminfo_file("/proc/meminfo");
    if (meminfo_file.is_open()) {
        std::string line;
        while (std::getline(meminfo_file, line)) {
            unsigned long long val_kb = 0;
            if (std::sscanf(line.c_str(), "MemAvailable: %llu kB", &val_kb) == 1) {
                info.available_ram = static_cast<u64>(val_kb) * 1024ULL;
            } else if (std::sscanf(line.c_str(), "SwapTotal: %llu kB", &val_kb) == 1) {
                info.swap_total = static_cast<u64>(val_kb) * 1024ULL;
            } else if (std::sscanf(line.c_str(), "SwapFree: %llu kB", &val_kb) == 1) {
                info.swap_free = static_cast<u64>(val_kb) * 1024ULL;
            }
        }
    }

    // 2. cgroup v2 / v1
    std::ifstream cgroup_max_file("/sys/fs/cgroup/memory.max");
    if (cgroup_max_file.is_open()) {
        std::string val;
        cgroup_max_file >> val;
        if (val != "max" && !val.empty()) {
            info.cgroup_max = std::strtoull(val.c_str(), nullptr, 10);
        }
    } else {
        std::ifstream cgroup_v1_limit("/sys/fs/cgroup/memory/memory.limit_in_bytes");
        if (cgroup_v1_limit.is_open()) {
            cgroup_v1_limit >> info.cgroup_max;
            if (info.cgroup_max > info.total_physical_ram * 4) {
                info.cgroup_max = 0; // Kernel sentinel for unlimited
            }
        }
    }

    std::ifstream cgroup_current_file("/sys/fs/cgroup/memory.current");
    if (cgroup_current_file.is_open()) {
        cgroup_current_file >> info.cgroup_current;
    } else {
        std::ifstream cgroup_v1_usage("/sys/fs/cgroup/memory/memory.usage_in_bytes");
        if (cgroup_v1_usage.is_open()) {
            cgroup_v1_usage >> info.cgroup_current;
        }
    }

    // 3. RLIMIT_AS
    struct rlimit rl;
    if (getrlimit(RLIMIT_AS, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        info.rlimit_as = rl.rlim_cur;
    }

    // 4. /proc/self/status: VmRSS, RssAnon, RssFile, RssShmem
    std::ifstream status_file("/proc/self/status");
    if (status_file.is_open()) {
        std::string line;
        while (std::getline(status_file, line)) {
            unsigned long long val_kb = 0;
            if (std::sscanf(line.c_str(), "VmRSS: %llu kB", &val_kb) == 1) {
                info.process_rss = static_cast<u64>(val_kb) * 1024ULL;
            } else if (std::sscanf(line.c_str(), "RssAnon: %llu kB", &val_kb) == 1) {
                info.process_anon_rss = static_cast<u64>(val_kb) * 1024ULL;
            } else if (std::sscanf(line.c_str(), "RssFile: %llu kB", &val_kb) == 1) {
                info.process_file_rss = static_cast<u64>(val_kb) * 1024ULL;
            } else if (std::sscanf(line.c_str(), "RssShmem: %llu kB", &val_kb) == 1) {
                info.process_shmem_rss = static_cast<u64>(val_kb) * 1024ULL;
            }
        }
    }

    // Fallback if status unreadable: /proc/self/statm
    if (info.process_rss == 0) {
        std::ifstream statm_file("/proc/self/statm");
        if (statm_file.is_open()) {
            u64 size_p = 0, resident_p = 0;
            if (statm_file >> size_p >> resident_p) {
                info.process_rss = resident_p * static_cast<u64>(page_size);
            }
        }
    }

    // 5. /proc/pressure/memory (PSI)
    std::ifstream psi_file("/proc/pressure/memory");
    if (psi_file.is_open()) {
        std::string line;
        while (std::getline(psi_file, line)) {
            double a10 = 0.0, a60 = 0.0;
            if (std::sscanf(line.c_str(), "some avg10=%lf avg60=%lf", &a10, &a60) >= 1) {
                info.psi_some_avg10 = a10;
                info.psi_some_avg60 = a60;
            } else if (std::sscanf(line.c_str(), "full avg10=%lf avg60=%lf", &a10, &a60) >= 1) {
                info.psi_full_avg10 = a10;
                info.psi_full_avg60 = a60;
            }
        }
    }

    // 6. /sys/block/zram0/mm_stat (Android zram stats)
    std::ifstream zram_file("/sys/block/zram0/mm_stat");
    if (zram_file.is_open()) {
        u64 orig = 0, compr = 0, used = 0;
        if (zram_file >> orig >> compr >> used) {
            info.zram_orig_data_size = orig;
            info.zram_compr_data_size = compr;
            info.zram_mem_used_total = used;
        }
    }
#endif

    return info;
}

void MemoryBudget::RecordSnapshot(u64 elapsed_seconds, const SystemMemoryInfo& sys_info, double fps) {
    std::scoped_lock lk{history_mutex};
    MemoryHistorySnapshot snap{};
    snap.timestamp = std::chrono::steady_clock::now();
    snap.frame_number = frame_counter;
    snap.elapsed_seconds = elapsed_seconds;
    snap.process_rss = sys_info.process_rss;
    snap.available_ram = sys_info.available_ram;
    snap.psi_full_avg10 = sys_info.psi_full_avg10;
    snap.fps = fps;

    for (size_t i = 0; i < subsystems.size(); ++i) {
        if (subsystems[i].registered && subsystems[i].bytes_getter) {
            try {
                snap.subsystem_bytes[i] = subsystems[i].bytes_getter();
            } catch (...) {
                snap.subsystem_bytes[i] = 0;
            }
        }
    }

    const u64 minute = elapsed_seconds / 60;
    const u64 gh = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::GuestHeap)]);
    const u64 tc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::TextureCache)]);
    const u64 bc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::BufferCache)]);
    const u64 pc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::PipelineCache)]);
    const u64 st = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::StagingPool)]);
    const u64 dr = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::DescriptorRing)]);

    std::string snap_line = fmt::format(
        "| {:>6} | {:>5}s | {:>8} | {:>13} | {:>14.2f} | {:>4.1f} | {:>14} | {:>13} | {:>13} | {:>14} | {:>12} | {:>13} |\n",
        minute, elapsed_seconds, ToMiB(snap.process_rss),
        ToMiB(snap.available_ram), snap.psi_full_avg10, snap.fps,
        gh, tc, bc, pc, st, dr);

    AppendTraceToFile(snap_line);

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "EdenMemoryBudget", "SNAP: %s", snap_line.c_str());
    __android_log_print(ANDROID_LOG_INFO, "HW_Memory", "SNAP: %s", snap_line.c_str());
#endif

    history.push_back(snap);
    // Keep max 120 minutes of history snapshots (2 hours)
    if (history.size() > 120) {
        history.erase(history.begin());
    }
}

void MemoryBudget::TickFrame(double current_fps) {
    if (!enabled) {
        return;
    }

    ++frame_counter;
    const auto now = std::chrono::steady_clock::now();

    // Update measured FPS if not supplied
    const auto fps_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_time).count();
    if (fps_duration >= 1000) {
        const u64 frame_delta = frame_counter - last_fps_frame;
        measured_fps = (static_cast<double>(frame_delta) * 1000.0) / static_cast<double>(fps_duration);
        last_fps_time = now;
        last_fps_frame = frame_counter;
    }

    const double effective_fps = (current_fps > 0.0) ? current_fps : measured_fps;
    const u64 elapsed_seconds = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count());

    // Record history snapshot every 60 seconds (1 minute interval)
    const u64 current_minute = elapsed_seconds / 60;
    if (current_minute > last_snapshot_minute || (frame_counter == 1 && history.empty())) {
        last_snapshot_minute = current_minute;
        SystemMemoryInfo sys_info = ReadSystemMemoryInfo();
        RecordSnapshot(elapsed_seconds, sys_info, effective_fps);
    }

    // Check report interval
    if (frame_counter % report_interval_frames == 0) {
        GenerateReport(true);
    }
}

std::string MemoryBudget::GenerateReport([[maybe_unused]] bool force_read) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_seconds_f = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count()) / 1000.0;

    SystemMemoryInfo sys = ReadSystemMemoryInfo();

    std::scoped_lock lk{registration_mutex};

    // Update evictions/sec for caches with eviction getters
    for (size_t i = 0; i < subsystems.size(); ++i) {
        auto& sub = subsystems[i];
        if (sub.registered && sub.evictions_getter) {
            try {
                const u64 curr_evictions = sub.evictions_getter();
                const auto time_delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - sub.last_eviction_sample).count();
                if (time_delta_ms >= 500) {
                    const u64 eviction_delta = (curr_evictions >= sub.last_evictions)
                                                   ? (curr_evictions - sub.last_evictions)
                                                   : 0;
                    sub.last_evictions_per_sec = (static_cast<double>(eviction_delta) * 1000.0) /
                                                 static_cast<double>(time_delta_ms);
                    sub.last_evictions = curr_evictions;
                    sub.last_eviction_sample = now;
                }
            } catch (...) {
            }
        }
    }

    std::string report;
    report.reserve(2048);

    report += fmt::format("===== MemoryBudget Report [Frame: {}, Time: {:.1f}s, FPS: {:.1f}] =====\n",
                         frame_counter, elapsed_seconds_f, measured_fps);

    // 1. System RAM & Pressure
    report += fmt::format("System RAM: {:.1f} MB total, {:.1f} MB available",
                         ToMiBF(sys.total_physical_ram), ToMiBF(sys.available_ram));
    if (sys.psi_some_avg10 > 0.0 || sys.psi_full_avg10 > 0.0) {
        report += fmt::format(" | PSI full avg10: {:.2f} (some avg10: {:.2f})",
                             sys.psi_full_avg10, sys.psi_some_avg10);
    }
    report += "\n";

    // 2. Process RSS breakdown
    report += fmt::format("Process RSS: {:.1f} MB (Anon: {:.1f} MB, File: {:.1f} MB, Shmem: {:.1f} MB)\n",
                         ToMiBF(sys.process_rss), ToMiBF(sys.process_anon_rss),
                         ToMiBF(sys.process_file_rss), ToMiBF(sys.process_shmem_rss));

    // 3. Swap / ZRAM state
    if (sys.swap_total > 0 || sys.zram_orig_data_size > 0) {
        report += "Swap / ZRAM: ";
        if (sys.zram_orig_data_size > 0) {
            const double ratio = (sys.zram_compr_data_size > 0)
                                     ? static_cast<double>(sys.zram_orig_data_size) /
                                           static_cast<double>(sys.zram_compr_data_size)
                                     : 1.0;
            report += fmt::format("zram0 orig: {:.1f} MB, compr: {:.1f} MB ({:.2f}x), used: {:.1f} MB | ",
                                 ToMiBF(sys.zram_orig_data_size), ToMiBF(sys.zram_compr_data_size),
                                 ratio, ToMiBF(sys.zram_mem_used_total));
        }
        report += fmt::format("SwapFree: {:.1f} MB / {:.1f} MB\n",
                             ToMiBF(sys.swap_free), ToMiBF(sys.swap_total));
    }

    // 4. cgroup / rlimit if active
    if (sys.cgroup_max > 0 || sys.rlimit_as > 0) {
        report += "Limits: ";
        if (sys.cgroup_max > 0) {
            report += fmt::format("cgroup.max: {:.1f} MB (curr: {:.1f} MB) ",
                                 ToMiBF(sys.cgroup_max), ToMiBF(sys.cgroup_current));
        }
        if (sys.rlimit_as > 0) {
            report += fmt::format("RLIMIT_AS: {:.1f} MB ", ToMiBF(sys.rlimit_as));
        }
        report += "\n";
    }

    // 4.5 Memory Governor
    if (MemoryGovernor::Instance().IsEnabled()) {
        const u64 budget = MemoryGovernor::Instance().GetCurrentBudget();
        const s64 def = MemoryGovernor::Instance().GetCurrentDeficit();
        const s64 carried = MemoryGovernor::Instance().GetCarriedDeficit();
        const u32 def_frames = MemoryGovernor::Instance().GetPersistentDeficitFrames();
        const bool thr = MemoryGovernor::Instance().ShouldThrottle();
        report += "Memory Governor Status:\n";
        report += "  - State                 : Active\n";
        report += fmt::format("  - Budget                : {:>7.1f} MB\n", ToMiBF(budget));
        report += fmt::format("  - Deficit               : {:>7.1f} MB (Carried: {:.1f} MB)\n",
                              static_cast<double>(def) / (1024.0 * 1024.0),
                              static_cast<double>(carried) / (1024.0 * 1024.0));
        report += fmt::format("  - Deficit Frames        : {} frames\n", def_frames);
        report += fmt::format("  - Pressure Throttle     : {}\n\n", thr ? "ACTIVE (Pacing)" : "Inactive");
    } else {
        report += "Memory Governor Status: Inactive (Unmanaged)\n\n";
    }

    // 5. Per-Subsystem Breakdown
    report += "Subsystem Breakdown:\n";
    u64 total_registered_bytes = 0;
    std::vector<std::string> warnings;

    for (size_t i = 0; i < subsystems.size(); ++i) {
        auto& sub = subsystems[i];
        if (!sub.registered || !sub.bytes_getter) {
            continue;
        }

        u64 current_bytes = 0;
        try {
            current_bytes = sub.bytes_getter();
        } catch (...) {
            current_bytes = 0;
        }
        sub.last_bytes = current_bytes;
        total_registered_bytes += current_bytes;

        const u64 target = sub.target_bytes.load();
        std::string line = fmt::format("  - {:<24}: {:>7.1f} MB / target: {:>7.1f} MB",
                                      sub.name, ToMiBF(current_bytes), ToMiBF(target));

        if (sub.evictions_getter) {
            line += fmt::format(" | {:>5.1f} evictions/s", sub.last_evictions_per_sec);
        }
        line += "\n";
        report += line;

        // Check if any subsystem exceeds twice its target
        if (target > 0 && current_bytes > 2 * target) {
            warnings.push_back(fmt::format("-- WARN: {} ({:.1f} MB) exceeds twice its target ({:.1f} MB)!",
                                          sub.name, ToMiBF(current_bytes), ToMiBF(target)));
        }
    }

    report += fmt::format("  Total Registered Bytes  : {:>7.1f} MB\n", ToMiBF(total_registered_bytes));

    // 6. Warnings
    for (const auto& warn : warnings) {
        report += warn + "\n";
        LOG_WARNING(HW_Memory, "{}", warn);
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_WARN, "EdenMemoryBudget", "%s", warn.c_str());
        __android_log_print(ANDROID_LOG_WARN, "HW_Memory", "%s", warn.c_str());
#endif
        AppendTraceToFile(warn + "\n");
    }

    report += "========================================================\n";

    // Log the summary lines under HW_Memory
    LOG_INFO(HW_Memory, "MemoryBudget [F: {}, {:.1f}s]: RSS: {:.1f} MB, Avail: {:.1f} MB, Reg: {:.1f} MB, PSI: {:.2f}",
             frame_counter, elapsed_seconds_f, ToMiBF(sys.process_rss), ToMiBF(sys.available_ram),
             ToMiBF(total_registered_bytes), sys.psi_full_avg10);

#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "EdenMemoryBudget", "%s", report.c_str());
    __android_log_print(ANDROID_LOG_INFO, "HW_Memory", "MemoryBudget [F: %llu, %.1fs]: RSS: %.1f MB, Avail: %.1f MB, Reg: %.1f MB, PSI: %.2f",
                        static_cast<unsigned long long>(frame_counter), elapsed_seconds_f,
                        ToMiBF(sys.process_rss), ToMiBF(sys.available_ram),
                        ToMiBF(total_registered_bytes), sys.psi_full_avg10);
#endif

    // Update overlay cache
    std::string gov_str;
    if (MemoryGovernor::Instance().IsEnabled()) {
        const u64 budget_mb = ToMiB(MemoryGovernor::Instance().GetCurrentBudget());
        const s64 def_mb = MemoryGovernor::Instance().GetCurrentDeficit() / (1024 * 1024);
        const bool thr = MemoryGovernor::Instance().ShouldThrottle();
        gov_str = fmt::format("|Gov:{:.0f}M({}M{})", static_cast<double>(budget_mb), def_mb, thr ? "!THR" : "");
    }
    cached_overlay_summary = fmt::format(
        "RSS:{:.0f}M|Avail:{:.0f}M|PSI:{:.1f}{}|TC:{:.0f}M|BC:{:.0f}M|GH:{:.0f}M",
        ToMiBF(sys.process_rss), ToMiBF(sys.available_ram), sys.psi_full_avg10,
        gov_str,
        ToMiBF(GetSubsystemBytes(MemorySubsystem::TextureCache)),
        ToMiBF(GetSubsystemBytes(MemorySubsystem::BufferCache)),
        ToMiBF(GetSubsystemBytes(MemorySubsystem::GuestHeap)));
    last_overlay_update = now;

    last_report_time = now;
    return report;
}

std::string MemoryBudget::GetOverlaySummary() {
    const auto now = std::chrono::steady_clock::now();
    if (cached_overlay_summary.empty() ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_overlay_update).count() > 1000) {
        SystemMemoryInfo sys = ReadSystemMemoryInfo();
        std::string gov_str;
        if (MemoryGovernor::Instance().IsEnabled()) {
            const u64 budget_mb = ToMiB(MemoryGovernor::Instance().GetCurrentBudget());
            const s64 def_mb = MemoryGovernor::Instance().GetCurrentDeficit() / (1024 * 1024);
            const bool thr = MemoryGovernor::Instance().ShouldThrottle();
            gov_str = fmt::format("|Gov:{:.0f}M({}M{})", static_cast<double>(budget_mb), def_mb, thr ? "!THR" : "");
        }
        cached_overlay_summary = fmt::format(
            "RSS:{:.0f}M|Avail:{:.0f}M|PSI:{:.1f}{}|TC:{:.0f}M|BC:{:.0f}M|GH:{:.0f}M",
            ToMiBF(sys.process_rss), ToMiBF(sys.available_ram), sys.psi_full_avg10,
            gov_str,
            ToMiBF(GetSubsystemBytes(MemorySubsystem::TextureCache)),
            ToMiBF(GetSubsystemBytes(MemorySubsystem::BufferCache)),
            ToMiBF(GetSubsystemBytes(MemorySubsystem::GuestHeap)));
        last_overlay_update = now;
    }
    return cached_overlay_summary;
}

std::string MemoryBudget::GetEssentialOverlay() {
    const auto now = std::chrono::steady_clock::now();
    if (!cached_essential_valid ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_essential_update).count() > 1000) {
        const SystemMemoryInfo sys = ReadSystemMemoryInfo();
        const double avail_mb = ToMiBF(sys.available_ram);

        const bool gov_enabled = MemoryGovernor::Instance().IsEnabled();
        const bool throttling = gov_enabled && MemoryGovernor::Instance().ShouldThrottle();
        const bool deficit = gov_enabled && MemoryGovernor::Instance().GetPersistentDeficitFrames() > 0;

        // Warn once available RAM closes in on the safety reserve, or the governor signals trouble
        const int reserve_mb = std::max(Settings::values.memory_governor_safety_reserve.GetValue(), 0);
        const double warn_mb = std::max(2.0 * static_cast<double>(reserve_mb), 280.0);

        if (avail_mb > warn_mb && !throttling && !deficit) {
            cached_essential_summary.clear();
        } else {
            const char* flag = throttling ? " !THR" : (deficit ? " !DEF" : "");
            cached_essential_summary = fmt::format("Avail:{:.0f}M{}", avail_mb, flag);
        }
        cached_essential_valid = true;
        last_essential_update = now;
    }
    return cached_essential_summary;
}

std::string MemoryBudget::GenerateHistoryTable() const {
    std::scoped_lock lk{history_mutex};
    if (history.empty()) {
        return "No memory budget history snapshots recorded yet.\n";
    }

    std::string table;
    table.reserve(4096);

    table += "| Minute | Elapsed | RSS (MB) | MemAvail (MB) | PSI full avg10 | FPS | GuestHeap (MB) | TexCache (MB) | BufCache (MB) | PipeCache (MB) | Staging (MB) | DescRing (MB) |\n";
    table += "|---|---|---|---|---|---|---|---|---|---|---|---|\n";

    for (const auto& snap : history) {
        const u64 minute = snap.elapsed_seconds / 60;
        const u64 gh = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::GuestHeap)]);
        const u64 tc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::TextureCache)]);
        const u64 bc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::BufferCache)]);
        const u64 pc = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::PipelineCache)]);
        const u64 st = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::StagingPool)]);
        const u64 dr = ToMiB(snap.subsystem_bytes[static_cast<size_t>(MemorySubsystem::DescriptorRing)]);

        table += fmt::format("| {:>6} | {:>5}s | {:>8} | {:>13} | {:>14.2f} | {:>4.1f} | {:>14} | {:>13} | {:>13} | {:>14} | {:>12} | {:>13} |\n",
                             minute, snap.elapsed_seconds, ToMiB(snap.process_rss),
                             ToMiB(snap.available_ram), snap.psi_full_avg10, snap.fps,
                             gh, tc, bc, pc, st, dr);
    }

    return table;
}

} // namespace Common
