// SPDX-FileCopyrightText: Copyright 2026 Eden-Bounded Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"

namespace Common {

enum class MemoryClientPriority : u32 {
    Lowest = 0,
    StagingPool = 10,
    QueryCache = 20,
    CommandPool = 30,
    BufferCache = 40,
    TextureCache = 50,
    ObjectPools = 60,
    PipelineCache = 70,
    Highest = 100,
};

class IMemoryClient {
public:
    virtual ~IMemoryClient() = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual u64 SizeBytes() const = 0;
    [[nodiscard]] virtual u64 TargetBytes() const = 0;
    [[nodiscard]] virtual u64 FloorBytes() const { return TargetBytes() / 2; }
    [[nodiscard]] virtual MemoryClientPriority Priority() const = 0;

    /// Shrink this client's memory footprint towards target_bytes.
    /// Implementation must be bounded by max_time_budget to prevent frame stalls.
    /// @param target_bytes Desired upper memory bound in bytes.
    /// @param max_time_budget Maximum time budget to spend in this call.
    /// @return The actual size in bytes after shrinking.
    virtual u64 ShrinkTo(u64 target_bytes, std::chrono::microseconds max_time_budget) = 0;
};

class MemoryGovernor {
public:
    static MemoryGovernor& Instance();

    void RegisterClient(IMemoryClient* client);
    void UnregisterClient(IMemoryClient* client);

    /// Compute real process memory budget based on MemAvailable, reserve, and driver baseline
    [[nodiscard]] u64 ComputeBudget(u64 mem_available) const;

    /// Frame tick: shrinks clients if over budget, tracks deficit carry-over, emits throttle signal
    bool TickFrame(double current_fps = 0.0);

    /// Watchdog tick: called from low-frequency watchdog thread
    void TickWatchdog();

    [[nodiscard]] bool ShouldThrottle() const noexcept {
        return throttle_requested.load(std::memory_order_relaxed);
    }

    [[nodiscard]] u64 GetCurrentBudget() const noexcept {
        return current_budget.load(std::memory_order_relaxed);
    }

    [[nodiscard]] s64 GetCurrentDeficit() const noexcept {
        return current_deficit.load(std::memory_order_relaxed);
    }

    [[nodiscard]] s64 GetCarriedDeficit() const noexcept {
        return carried_deficit.load(std::memory_order_relaxed);
    }

    [[nodiscard]] u32 GetPersistentDeficitFrames() const noexcept {
        return persistent_deficit_frames.load(std::memory_order_relaxed);
    }

    [[nodiscard]] u64 GetTotalRegisteredBytes() const;
    [[nodiscard]] u64 GetTotalTargetBytes() const;
    [[nodiscard]] u64 GetTotalFloorBytes() const;
    [[nodiscard]] u64 GetClientTarget(MemoryClientPriority priority) const;

    void SetEnabled(bool enabled_) noexcept {
        enabled.store(enabled_, std::memory_order_relaxed);
    }

    [[nodiscard]] bool IsEnabled() const noexcept;

    void SetSafetyReserve(u64 bytes) noexcept {
        safety_reserve.store(bytes, std::memory_order_relaxed);
    }

    [[nodiscard]] u64 GetSafetyReserve() const noexcept {
        return safety_reserve.load(std::memory_order_relaxed);
    }

    void SetDriverBaseline(u64 bytes) noexcept {
        driver_baseline.store(bytes, std::memory_order_relaxed);
    }

    [[nodiscard]] u64 GetDriverBaseline() const noexcept {
        return driver_baseline.load(std::memory_order_relaxed);
    }

    void SetManualBudget(u64 bytes) noexcept {
        manual_budget.store(bytes, std::memory_order_relaxed);
    }

    [[nodiscard]] u64 GetManualBudget() const noexcept {
        return manual_budget.load(std::memory_order_relaxed);
    }

    void SetMaxShrinkTimePerFrame(std::chrono::microseconds time) noexcept {
        max_shrink_time_per_frame = time;
    }

    void SetPersistentDeficitThreshold(u32 frames) noexcept {
        persistent_deficit_threshold = frames;
    }

    void Reset();

private:
    MemoryGovernor();
    ~MemoryGovernor();

    MemoryGovernor(const MemoryGovernor&) = delete;
    MemoryGovernor& operator=(const MemoryGovernor&) = delete;

    std::vector<IMemoryClient*> clients;
    mutable std::mutex clients_mutex;

    std::atomic<bool> enabled{false};
    std::atomic<bool> throttle_requested{false};
    std::atomic<u64> manual_budget{0};
    std::atomic<u64> safety_reserve{200ULL * 1024 * 1024};    // 200 MB default
    std::atomic<u64> driver_baseline{768ULL * 1024 * 1024};   // 768 MB Android/GPU baseline
    std::atomic<u64> current_budget{0};
    std::atomic<s64> current_deficit{0};
    std::atomic<s64> carried_deficit{0};
    std::atomic<u32> persistent_deficit_frames{0};

    u32 persistent_deficit_threshold{30}; // ~1 second at 30 fps
    std::chrono::microseconds max_shrink_time_per_frame{10000}; // 10 ms max per frame
};

} // namespace Common
