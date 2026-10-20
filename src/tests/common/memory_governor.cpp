// SPDX-FileCopyrightText: Copyright 2026 Eden-Bounded Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "common/host_memory.h"
#include "common/memory_governor.h"
#include "shader_recompiler/object_pool.h"

namespace Common {

namespace {

class MockMemoryClient : public IMemoryClient {
public:
    MockMemoryClient(std::string_view name_, u64 size_, u64 target_, MemoryClientPriority priority_)
        : name{name_}, size{size_}, target{target_}, priority{priority_} {}

    [[nodiscard]] std::string_view Name() const override {
        return name;
    }

    [[nodiscard]] u64 SizeBytes() const override {
        return size;
    }

    [[nodiscard]] u64 TargetBytes() const override {
        return target;
    }

    [[nodiscard]] MemoryClientPriority Priority() const override {
        return priority;
    }

    u64 ShrinkTo(u64 target_bytes, [[maybe_unused]] std::chrono::microseconds max_time_budget) override {
        shrink_calls.push_back(target_bytes);
        if (size > target_bytes) {
            const u64 amount_to_shrink = std::min(size - target_bytes, max_shrinkable_bytes);
            size -= amount_to_shrink;
        }
        return size;
    }

    std::string name;
    u64 size{0};
    u64 target{0};
    MemoryClientPriority priority{MemoryClientPriority::Lowest};
    u64 max_shrinkable_bytes{std::numeric_limits<u64>::max()};
    std::vector<u64> shrink_calls;
};

} // anonymous namespace

TEST_CASE("MemoryGovernor: Allocation Accounting", "[common][memory_governor]") {
    auto& governor = MemoryGovernor::Instance();
    governor.Reset();

    MockMemoryClient client_staging("Staging", 100 * 1024 * 1024, 64 * 1024 * 1024, MemoryClientPriority::StagingPool);
    MockMemoryClient client_texture("Texture", 500 * 1024 * 1024, 256 * 1024 * 1024, MemoryClientPriority::TextureCache);
    MockMemoryClient client_buffer("Buffer", 200 * 1024 * 1024, 128 * 1024 * 1024, MemoryClientPriority::BufferCache);

    governor.RegisterClient(&client_staging);
    governor.RegisterClient(&client_texture);
    governor.RegisterClient(&client_buffer);

    REQUIRE(governor.GetTotalRegisteredBytes() == 800ULL * 1024 * 1024);
    REQUIRE(governor.GetClientTarget(MemoryClientPriority::TextureCache) == 256ULL * 1024 * 1024);

    governor.UnregisterClient(&client_staging);
    REQUIRE(governor.GetTotalRegisteredBytes() == 700ULL * 1024 * 1024);

    governor.UnregisterClient(&client_texture);
    governor.UnregisterClient(&client_buffer);
    REQUIRE(governor.GetTotalRegisteredBytes() == 0);
}

TEST_CASE("MemoryGovernor: Shrink Ordering Ascending Priority", "[common][memory_governor]") {
    auto& governor = MemoryGovernor::Instance();
    governor.Reset();
    governor.SetEnabled(true);

    static std::vector<std::string> shrink_order;
    shrink_order.clear();

    class OrderTrackingClient : public IMemoryClient {
    public:
        OrderTrackingClient(std::string name_, u64 size_, u64 target_, MemoryClientPriority priority_)
            : name{std::move(name_)}, size{size_}, target{target_}, priority{priority_} {}

        [[nodiscard]] std::string_view Name() const override { return name; }
        [[nodiscard]] u64 SizeBytes() const override { return size; }
        [[nodiscard]] u64 TargetBytes() const override { return target; }
        [[nodiscard]] MemoryClientPriority Priority() const override { return priority; }

        u64 ShrinkTo(u64 target_bytes, [[maybe_unused]] std::chrono::microseconds max_time) override {
            shrink_order.push_back(name);
            size = target_bytes;
            return size;
        }

        std::string name;
        u64 size;
        u64 target;
        MemoryClientPriority priority;
    };

    OrderTrackingClient texture_client("Texture", 1000 * 1024 * 1024, 500 * 1024 * 1024, MemoryClientPriority::TextureCache);
    OrderTrackingClient staging_client("Staging", 300 * 1024 * 1024, 100 * 1024 * 1024, MemoryClientPriority::StagingPool);
    OrderTrackingClient pipeline_client("Pipeline", 200 * 1024 * 1024, 50 * 1024 * 1024, MemoryClientPriority::PipelineCache);

    // Register out of order
    governor.RegisterClient(&texture_client);
    governor.RegisterClient(&staging_client);
    governor.RegisterClient(&pipeline_client);

    // Total size = 1500 MB. Set manual budget = 800 MB (deficit = 700 MB)
    governor.SetManualBudget(800ULL * 1024 * 1024);

    governor.TickFrame(30.0);

    // Ascending priority order: StagingPool (10) -> TextureCache (50) -> PipelineCache (70)
    REQUIRE(!shrink_order.empty());
    REQUIRE(shrink_order[0] == "Staging");
    if (shrink_order.size() > 1) {
        REQUIRE(shrink_order[1] == "Texture");
    }

    governor.UnregisterClient(&texture_client);
    governor.UnregisterClient(&staging_client);
    governor.UnregisterClient(&pipeline_client);
}

TEST_CASE("MemoryGovernor: Deficit Carry-over and Throttle Signaling", "[common][memory_governor]") {
    auto& governor = MemoryGovernor::Instance();
    governor.Reset();
    governor.SetEnabled(true);
    governor.SetPersistentDeficitThreshold(5); // 5 frames threshold for fast testing

    MockMemoryClient stubborn_client("Stubborn", 1000 * 1024 * 1024, 500 * 1024 * 1024, MemoryClientPriority::TextureCache);
    // Client can only shrink 50 MB at a time
    stubborn_client.max_shrinkable_bytes = 50 * 1024 * 1024;

    governor.RegisterClient(&stubborn_client);

    // Set budget to 600 MB (initial deficit = 400 MB)
    governor.SetManualBudget(600ULL * 1024 * 1024);

    REQUIRE(governor.ShouldThrottle() == false);

    // Frame 1: client shrinks 50 MB (size becomes 950 MB). Leftover deficit = 350 MB carries over!
    governor.TickFrame(30.0);
    REQUIRE(governor.GetCarriedDeficit() > 0);
    REQUIRE(governor.GetPersistentDeficitFrames() == 1);
    REQUIRE(governor.ShouldThrottle() == false);

    // Run until threshold (5 frames)
    for (int i = 0; i < 4; ++i) {
        governor.TickFrame(30.0);
    }

    REQUIRE(governor.GetPersistentDeficitFrames() >= 5);
    REQUIRE(governor.ShouldThrottle() == true);

    // Now expand budget above client size (e.g. 2000 MB): deficit should clear immediately
    governor.SetManualBudget(2000ULL * 1024 * 1024);
    governor.TickFrame(30.0);

    REQUIRE(governor.GetCurrentDeficit() == 0);
    REQUIRE(governor.GetCarriedDeficit() == 0);
    REQUIRE(governor.GetPersistentDeficitFrames() == 0);
    REQUIRE(governor.ShouldThrottle() == false);

    governor.UnregisterClient(&stubborn_client);
}

TEST_CASE("MemoryGovernor: Deficit Does Not Accumulate Runaway", "[common][memory_governor]") {
    auto& governor = MemoryGovernor::Instance();
    governor.Reset();
    governor.SetEnabled(true);

    MockMemoryClient client("Fixed", 1000 * 1024 * 1024, 800 * 1024 * 1024, MemoryClientPriority::Lowest);
    client.max_shrinkable_bytes = 0; // Unshrinkable

    governor.RegisterClient(&client);
    governor.SetManualBudget(800ULL * 1024 * 1024); // Exactly 200 MB deficit

    for (int i = 0; i < 100; ++i) {
        governor.TickFrame(30.0);
    }

    // Deficit must remain bounded to exactly 200 MB, NOT 20,000 MB!
    REQUIRE(governor.GetCurrentDeficit() == 200LL * 1024 * 1024);
    REQUIRE(governor.GetCarriedDeficit() == 200LL * 1024 * 1024);

    governor.UnregisterClient(&client);
}

TEST_CASE("MemoryGovernor: ObjectPool Reset Reclaims Memory", "[common][memory_governor]") {
    struct TestNode {
        u64 payload[8]{};
    };
    Shader::ObjectPool<TestNode> pool(64);
    REQUIRE(pool.TotalBytes() > 0);
    const size_t initial_bytes = pool.TotalBytes();

    // Allocate many objects across multiple chunks
    for (size_t i = 0; i < 2000; ++i) {
        void(pool.Create());
    }
    const size_t expanded_bytes = pool.TotalBytes();
    REQUIRE(expanded_bytes > initial_bytes);

    // Reset pool back to initial size
    pool.Reset();
    REQUIRE(pool.TotalBytes() == initial_bytes);
}

TEST_CASE("MemoryGovernor: HostMemory ReleaseBackingRegion Validated", "[common][memory_governor]") {
    HostMemory host_mem(0x10000, 0x100000);
    REQUIRE(host_mem.BackingBasePointer() != nullptr);

    // Dirty the backing memory
    std::memset(host_mem.BackingBasePointer(), 0xAA, 0x1000);
    REQUIRE(host_mem.BackingBasePointer()[0] == 0xAA);

    // Releasing the backing region via fallocate hole-punch and madvise
    host_mem.ReleaseBackingRegion(0, 0x1000);
    // Any newly faulted read from released backing region must be zero
    REQUIRE(host_mem.BackingBasePointer()[0] == 0);

    // Clearing backing region with 0 must also zero out dirty regions
    std::memset(host_mem.BackingBasePointer(), 0xBB, 0x1000);
    REQUIRE(host_mem.BackingBasePointer()[0] == 0xBB);
    host_mem.ClearBackingRegion(0, 0x1000, 0);
    REQUIRE(host_mem.BackingBasePointer()[0] == 0);

    REQUIRE(host_mem.GetBackingTotalBytes() == 0x10000);
}

} // namespace Common
