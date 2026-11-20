// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "record/RecordTap.h"

#include <chrono>
#include <thread>

namespace audreybrick
{

bool RecordTap::waitIdle(double timeoutSeconds) noexcept
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(timeoutSeconds);
    while(busy_.load(std::memory_order_seq_cst))
    {
        if(std::chrono::steady_clock::now() >= until)
            return false;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

} // namespace audreybrick
