// SPDX-FileCopyrightText: Copyright 2026 Eden-Bounded Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/common_types.h"

namespace Common {

enum class MemorySubsystem : u8 {
    GuestHeap = 0,
    TextureCache,
    BufferCache,
    DescriptorRing,
    StagingPool,
    QueryCache,
    CommandPool,
    PipelineCache,
    ObjectPools,
    DiskCacheBlob,
    Count
};

struct SystemMemoryInfo {
    u64 total_physical_ram{};
    u64 available_ram{};
    u64 cgroup_max{};       // 0 if unlimited/unspecified
    u64 cgroup_current{};
    u64 rlimit_as{};        // 0 if unlimited
    u64 process_rss{};
    u64 process_anon_rss{};
    u64 process_file_rss{};
    u64 process_shmem_rss{};
    u64 zram_orig_data_size{};
    u64 zram_compr_data_size{};
    u64 zram_mem_used_total{};
    u64 swap_total{};
    u64 swap_free{};
    double psi_some_avg10{0.0};
    double psi_some_avg60{0.0};
    double psi_full_avg10{0.0};
    double psi_full_avg60{0.0};
};

struct SubsystemInfo {
    MemorySubsystem id{MemorySubsystem::Count};
    std::string name;
    std::function<u64()> bytes_getter;
    std::function<u64()> evictions_getter;
    std::atomic<u64> target_bytes{0};
    u64 last_bytes{0};
    u64 last_evictions{0};
    double last_evictions_per_sec{0.0};
    std::chrono::steady_clock::time_point last_eviction_sample{};
    bool registered{false};
};

struct MemoryHistorySnapshot {
    std::chrono::steady_clock::time_point timestamp{};
    u64 frame_number{0};
    u64 elapsed_seconds{0};
    u64 process_rss{0};
    u64 available_ram{0};
    double psi_full_avg10{0.0};
    double fps{0.0};
    std::array<u64, static_cast<size_t>(MemorySubsystem::Count)> subsystem_bytes{};
};

class MemoryBudget {
public:
    static MemoryBudget& Instance();

    void RegisterSubsystem(MemorySubsystem id, std::string_view name,
                           std::function<u64()> bytes_getter,
                           u64 default_target_bytes = 0,
                           std::function<u64()> evictions_getter = nullptr);

    void UnregisterSubsystem(MemorySubsystem id);

    void SetSubsystemTarget(MemorySubsystem id, u64 target_bytes);
    [[nodiscard]] u64 GetSubsystemTarget(MemorySubsystem id) const;

    [[nodiscard]] u64 GetSubsystemBytes(MemorySubsystem id) const;
    [[nodiscard]] double GetSubsystemEvictionsPerSec(MemorySubsystem id) const;

    // Call on each frame (from renderer / rasterizer)
    void TickFrame(double current_fps = 0.0);

    // Collect fresh system memory metrics (MemAvailable, PSI, zram, RSS, etc.)
    [[nodiscard]] SystemMemoryInfo ReadSystemMemoryInfo() const;

    // Generate periodic diagnostics report (logged under HW_Memory and exposed to dumpsys)
    std::string GenerateReport(bool force_read = false);

    // Compact 1-line summary for Android in-game overlay
    [[nodiscard]] std::string GetOverlaySummary();

    // Essential-mode overlay: empty while memory is healthy, compact warning line near a limit
    [[nodiscard]] std::string GetEssentialOverlay();

    // Markdown minute-by-minute table for benchmarks and post-mortem analysis
    [[nodiscard]] std::string GenerateHistoryTable() const;

    void SetReportInterval(u32 interval_frames) {
        report_interval_frames = interval_frames;
    }

    [[nodiscard]] u32 GetReportInterval() const {
        return report_interval_frames;
    }

    void SetEnabled(bool enabled_) {
        enabled = enabled_;
    }

    [[nodiscard]] bool IsEnabled() const {
        return enabled;
    }

    void Reset();

private:
    MemoryBudget();
    ~MemoryBudget();

    MemoryBudget(const MemoryBudget&) = delete;
    MemoryBudget& operator=(const MemoryBudget&) = delete;

    void WatchdogLoop(std::stop_token stop_token);
    void AppendTraceToFile(std::string_view text);

    void RecordSnapshot(u64 elapsed_seconds, const SystemMemoryInfo& sys_info, double fps);

    std::array<SubsystemInfo, static_cast<size_t>(MemorySubsystem::Count)> subsystems{};
    mutable std::mutex registration_mutex;

    std::vector<MemoryHistorySnapshot> history;
    mutable std::mutex history_mutex;

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_report_time;
    std::chrono::steady_clock::time_point last_fps_time;
    u64 last_fps_frame{0};
    u64 frame_counter{0};
    u64 last_snapshot_minute{0};
    u32 report_interval_frames{120};
    bool enabled{true};
    double measured_fps{0.0};

    std::string cached_overlay_summary;
    std::chrono::steady_clock::time_point last_overlay_update{};

    bool cached_essential_valid{false};
    std::string cached_essential_summary;
    std::chrono::steady_clock::time_point last_essential_update{};

    std::jthread watchdog_thread;
};

} // namespace Common
