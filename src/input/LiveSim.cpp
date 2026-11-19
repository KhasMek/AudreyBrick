// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "input/LiveSim.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace audreybrick
{

LiveSim::LiveSim(LiveInput& live, std::vector<SimCaptureDevice> devices, FramesFn frames, const void* ctx,
                 int blockFrames)
: live_(live), devices_(std::move(devices)), frames_(frames), ctx_(ctx), block_(blockFrames > 0 ? blockFrames : 256)
{
}

LiveSim::~LiveSim()
{
    close();
}

bool LiveSim::listDevices(std::vector<std::string>& names)
{
    names.clear();
    for(const SimCaptureDevice& d : devices_)
        if(d.present)
            names.push_back(d.name);
    return true;
}

bool LiveSim::setPresent(const std::string& name, bool present)
{
    bool found = false;
    for(SimCaptureDevice& d : devices_)
        if(d.name == name)
        {
            found     = true;
            d.present = present;
        }
    if(!found)
        return false;
    ++changes_;
    if(!present && open_ && openName_ == name)
        unplugged_.store(true, std::memory_order_release); // the thread stops writing
    return true;
}

bool LiveSim::open(const char* name, LiveOpenResult& r)
{
    close();
    const auto t0 = std::chrono::steady_clock::now();
    r.askedName     = name != nullptr ? name : "";
    r.askedRate     = 48000;
    r.askedChannels = 2;
    r.askedFrames   = block_;
    r.askedFormat   = "f32";
    std::size_t at  = devices_.size();
    for(std::size_t i = 0; i < devices_.size() && at == devices_.size(); ++i)
        if(devices_[i].present && (name == nullptr || devices_[i].name == name))
            at = i;
    auto done = [&] { r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    if(at == devices_.size())
    {
        r.ok    = false;
        r.error = name == nullptr ? "SIMULATED: NO DEVICE" : "SIMULATED: NO SUCH DEVICE";
        done();
        return false;
    }
    const SimCaptureDevice& d = devices_[at];
    if(d.fails)
    {
        r.ok    = false;
        r.error = "SIMULATED: DEVICE BUSY";
        done();
        return false;
    }
    buf_.assign(static_cast<std::size_t>(block_) * static_cast<std::size_t>(d.channels > 0 ? d.channels : 1), 0.0f);
    stop_.store(false, std::memory_order_relaxed);
    unplugged_.store(false, std::memory_order_relaxed);
    written_.store(0, std::memory_order_relaxed);
    thread_   = std::thread([this, dev = d] { run(dev); }); // a copy: the UI thread may change devices_
    open_     = true;
    openName_ = d.name;
    r.ok       = true;
    r.rate     = 48000;
    r.channels = d.channels;
    r.frames   = block_;
    r.format   = "f32";
    done();
    return true;
}

void LiveSim::close() noexcept
{
    if(thread_.joinable())
    {
        stop_.store(true, std::memory_order_release);
        thread_.join();
    }
    open_ = false;
    openName_.clear();
}

void LiveSim::run(const SimCaptureDevice& d) noexcept
{
    const int              ch    = d.channels > 0 ? d.channels : 1;
    const auto             block = static_cast<std::uint64_t>(block_);
    const double           step  = 2.0 * 3.14159265358979323846 * d.toneHz / 48000.0;
    const std::uint64_t    f0    = frames_(ctx_);
    const std::uint64_t    pre   = 2 * block;
    std::uint64_t          done  = 0;
    double                 phase = 0.0;
    while(!stop_.load(std::memory_order_acquire))
    {
        if(!unplugged_.load(std::memory_order_acquire))
        {
            const std::uint64_t f   = frames_(ctx_) - f0;
            const auto          due = pre + static_cast<std::uint64_t>(static_cast<double>(f) * d.rateHz / 48000.0);
            while(done + block <= due && !stop_.load(std::memory_order_relaxed))
            {
                for(std::uint64_t i = 0; i < block; ++i)
                {
                    const auto v = static_cast<float>(d.amplitude * std::sin(phase));
                    phase += step;
                    if(phase > 2.0 * 3.14159265358979323846)
                        phase -= 2.0 * 3.14159265358979323846;
                    for(int c = 0; c < ch; ++c)
                        buf_[static_cast<std::size_t>(i) * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] = v;
                }
                live_.writeInterleaved(buf_.data(), static_cast<std::size_t>(block), ch);
                done += block;
                written_.store(done, std::memory_order_relaxed);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace audreybrick
