// SPDX-FileCopyrightText: Copyright 2026 Eden-Bounded Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "common/memory_budget.h"

namespace Common {

TEST_CASE("MemoryBudget: System Memory Reading", "[common][memory_budget]") {
    auto& mb = MemoryBudget::Instance();
    const auto sys_info = mb.ReadSystemMemoryInfo();

    // Verify system memory reading succeeds on the current platform
    REQUIRE(sys_info.total_physical_ram > 0);
    REQUIRE(sys_info.available_ram > 0);
    REQUIRE(sys_info.available_ram <= sys_info.total_physical_ram);
}

TEST_CASE("MemoryBudget: Subsystem Registration and Target Accounting", "[common][memory_budget]") {
    auto& mb = MemoryBudget::Instance();
    mb.Reset();

    u64 dummy_tex_bytes = 500ULL * 1024 * 1024; // 500 MB
    u64 dummy_tex_evictions = 10;
    mb.RegisterSubsystem(
        MemorySubsystem::TextureCache, "Test Texture Cache",
        [&dummy_tex_bytes]() -> u64 { return dummy_tex_bytes; },
        1024ULL * 1024 * 1024 /* 1024 MB target */,
        [&dummy_tex_evictions]() -> u64 { return dummy_tex_evictions; }
    );

    u64 dummy_buf_bytes = 200ULL * 1024 * 1024; // 200 MB
    mb.RegisterSubsystem(
        MemorySubsystem::BufferCache, "Test Buffer Cache",
        [&dummy_buf_bytes]() -> u64 { return dummy_buf_bytes; },
        512ULL * 1024 * 1024 /* 512 MB target */
    );

    REQUIRE(mb.GetSubsystemBytes(MemorySubsystem::TextureCache) == dummy_tex_bytes);
    REQUIRE(mb.GetSubsystemBytes(MemorySubsystem::BufferCache) == dummy_buf_bytes);
    REQUIRE(mb.GetSubsystemTarget(MemorySubsystem::TextureCache) == 1024ULL * 1024 * 1024);
    REQUIRE(mb.GetSubsystemTarget(MemorySubsystem::BufferCache) == 512ULL * 1024 * 1024);

    // Initial report should be under target: no -- WARN line
    std::string report = mb.GenerateReport(true);
    REQUIRE(report.find("Test Texture Cache") != std::string::npos);
    REQUIRE(report.find("Test Buffer Cache") != std::string::npos);
    REQUIRE(report.find("-- WARN:") == std::string::npos);

    // Now spike Texture Cache above 2x target (> 2048 MB)
    dummy_tex_bytes = 2500ULL * 1024 * 1024; // 2500 MB > 2 * 1024 MB
    report = mb.GenerateReport(true);
    REQUIRE(report.find("-- WARN: Test Texture Cache") != std::string::npos);
    REQUIRE(report.find("exceeds twice its target") != std::string::npos);

    // Clean up
    mb.UnregisterSubsystem(MemorySubsystem::TextureCache);
    mb.UnregisterSubsystem(MemorySubsystem::BufferCache);
    REQUIRE(mb.GetSubsystemBytes(MemorySubsystem::TextureCache) == 0);
    REQUIRE(mb.GetSubsystemBytes(MemorySubsystem::BufferCache) == 0);
}

TEST_CASE("MemoryBudget: Overlay Summary and History Table", "[common][memory_budget]") {
    auto& mb = MemoryBudget::Instance();
    mb.Reset();

    u64 dummy_guest_bytes = 3000ULL * 1024 * 1024;
    mb.RegisterSubsystem(MemorySubsystem::GuestHeap, "Guest Heap",
                         [&dummy_guest_bytes]() -> u64 { return dummy_guest_bytes; },
                         3584ULL * 1024 * 1024);

    u64 dummy_tex_bytes = 800ULL * 1024 * 1024;
    mb.RegisterSubsystem(MemorySubsystem::TextureCache, "Texture Cache",
                         [&dummy_tex_bytes]() -> u64 { return dummy_tex_bytes; },
                         1024ULL * 1024 * 1024);

    // Verify overlay summary string format
    std::string overlay = mb.GetOverlaySummary();
    REQUIRE(!overlay.empty());
    REQUIRE(overlay.find("RSS:") != std::string::npos);
    REQUIRE(overlay.find("Avail:") != std::string::npos);
    REQUIRE(overlay.find("TC:") != std::string::npos);
    REQUIRE(overlay.find("GH:") != std::string::npos);

    // Tick a frame to create initial snapshot
    mb.TickFrame(25.0);

    // Verify history table contains markdown structure
    std::string table = mb.GenerateHistoryTable();
    REQUIRE(!table.empty());
    REQUIRE(table.find("| Minute |") != std::string::npos);
    REQUIRE(table.find("| RSS (MB) |") != std::string::npos);
    REQUIRE(table.find("| GuestHeap (MB) |") != std::string::npos);
    REQUIRE(table.find("| TexCache (MB) |") != std::string::npos);

    mb.UnregisterSubsystem(MemorySubsystem::GuestHeap);
    mb.UnregisterSubsystem(MemorySubsystem::TextureCache);
}

} // namespace Common
