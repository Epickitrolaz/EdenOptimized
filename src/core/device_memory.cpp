// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/memory_budget.h"
#include "core/device_memory.h"
#include "hle/kernel/board/nintendo/nx/k_system_control.h"

namespace Core {

#ifdef HAS_NCE
constexpr size_t VirtualReserveSize = 1ULL << 38;
#else
constexpr size_t VirtualReserveSize = 1ULL << 39;
#endif

DeviceMemory::DeviceMemory()
    : buffer{Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize(),
             VirtualReserveSize} {
    Common::MemoryBudget::Instance().RegisterSubsystem(
        Common::MemorySubsystem::GuestHeap, "Guest Heap (Touched)",
        [this]() -> u64 { return static_cast<u64>(buffer.GetBackingResidentBytes()); },
        3584ULL * 1024 * 1024 /* 3.5 GB target */
    );
}

DeviceMemory::~DeviceMemory() {
    Common::MemoryBudget::Instance().UnregisterSubsystem(Common::MemorySubsystem::GuestHeap);
}

} // namespace Core
