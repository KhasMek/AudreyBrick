// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Criterion 4: ButtonControlSource::read() performs no operator new over 24000
// control blocks with commands and held words arriving, and AudreyVoice::process
// driven by it performs none either. Same technique as
// tests/core/voice_alloc_test.cpp: this binary replaces the global operator
// new/delete and counts calls while a flag is set.
//
// Informational: whether ControlModel::onButton/tick allocate (UI thread; not a
// requirement).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "app/ControlModel.h"
#include "control/ButtonControlSource.h"
#include "core/AudreyVoice.h"

namespace
{
std::atomic<long> g_whileCounting{0};
std::atomic<bool> g_counting{false};

void note()
{
    if(g_counting.load(std::memory_order_relaxed))
        g_whileCounting.fetch_add(1, std::memory_order_relaxed);
}

void* countedAlloc(std::size_t size)
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p == nullptr)
        throw std::bad_alloc();
    note();
    return p;
}

void* countedAlignedAlloc(std::size_t size, std::size_t alignment)
{
    void* p = nullptr;
    if(alignment < sizeof(void*))
        alignment = sizeof(void*);
    if(posix_memalign(&p, alignment, size == 0 ? 1 : size) != 0)
        throw std::bad_alloc();
    note();
    return p;
}

int g_failures = 0;
void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok)
        ++g_failures;
}
} // namespace

void* operator new(std::size_t size) { return countedAlloc(size); }
void* operator new[](std::size_t size) { return countedAlloc(size); }
void* operator new(std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a) { return countedAlignedAlloc(size, static_cast<std::size_t>(a)); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    void* p = std::malloc(size == 0 ? 1 : size);
    if(p)
        note();
    return p;
}
void* operator new[](std::size_t size, const std::nothrow_t& t) noexcept { return operator new(size, t); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void  operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

int main()
{
    using namespace audreybrick;

    {
        g_counting  = true;
        void* probe = ::operator new(16);
        g_counting  = false;
        const long seen = g_whileCounting.exchange(0);
        ::operator delete(probe);
        check(seen == 1, "the counting operator new sees an allocation made while counting");
    }

    // read() alone, with commands of every type and changing held words.
    {
        auto src = std::make_unique<ButtonControlSource>();
        ControlFrame f{};
        float        vals[kKnobCount] = {};
        long         readAllocs       = 0;
        int          blocks           = 0;
        for(; blocks < 24000; ++blocks)
        {
            if(blocks % 7 == 0)
                src->post(ControlCommand::step(static_cast<Knob>(blocks % kKnobCount), 0.01));
            if(blocks % 101 == 0)
                src->post(ControlCommand::reset(static_cast<Knob>(blocks % kKnobCount)));
            if(blocks % 997 == 0)
                src->post(ControlCommand::setAll(vals, true));
            if(blocks % 503 == 0)
                src->post(ControlCommand::toggleSwitchLatch());
            if(blocks % 1000 == 0)
            {
                HeldState s;
                s.axisKnob[0] = Knob::ReverbMix;
                s.axisDir[0]  = static_cast<std::int8_t>(blocks / 1000 % 3 - 1);
                s.axisKnob[1] = Knob::OutputVolume;
                s.axisDir[1]  = 1;
                s.feedbackDir = -1;
                s.fine        = blocks / 2000 % 2;
                s.feedbackMax = blocks / 3000 % 2;
                s.switchHeld  = blocks / 5000 % 2;
                src->setHeld(packHeld(s));
            }
            g_counting = true;
            src->read(f);
            g_counting = false;
        }
        readAllocs = g_whileCounting.exchange(0);
        std::printf("read(): %d blocks, %llu commands processed, operator new calls = %ld\n", blocks,
                    static_cast<unsigned long long>(src->commandsProcessed()), readAllocs);
        check(src->commandsProcessed() > 3000, "commands arrived during the run");
        check(readAllocs == 0, "ButtonControlSource::read() performs no operator new over 24000 blocks");
    }

    // AudreyVoice::process with the button source, driven by a ControlModel
    // between chunks (the model is not counted; see below).
    {
        auto voice = std::make_unique<AudreyVoice>();
        voice->init();
        auto         src   = std::make_unique<ButtonControlSource>();
        ControlModel model(*src);
        std::vector<float> l(256), r(256);
        long               processAllocs = 0, modelAllocs = 0;
        const Button       cycle[] = {Button::R2, Button::DpadRight, Button::R1, Button::DpadUp, Button::A,
                                      Button::B,  Button::Select,    Button::X,  Button::L2,     Button::Y};
        double             t       = 0.0;
        for(int chunk = 0; chunk < 400; ++chunk) // 400 x 64 blocks = 25600 blocks
        {
            const Button b = cycle[chunk / 10 % 10];
            g_counting     = true;
            if(chunk % 10 == 0)
                model.onButton({b, true}, t);
            if(chunk % 10 == 7)
                model.onButton({b, false}, t);
            model.tick(t);
            g_counting = false;
            modelAllocs += g_whileCounting.exchange(0);

            g_counting = true;
            voice->process(*src, nullptr, l.data(), r.data(), l.size());
            g_counting = false;
            processAllocs += g_whileCounting.exchange(0);
            t += 256.0 / AudreyVoice::kSampleRate;
        }
        std::printf("process() with ButtonControlSource: 25600 blocks, %llu commands, operator new calls = %ld\n",
                    static_cast<unsigned long long>(src->commandsProcessed()), processAllocs);
        std::printf("(informational) ControlModel::onButton/tick operator new calls = %ld\n", modelAllocs);
        check(processAllocs == 0, "AudreyVoice::process driven by ButtonControlSource performs no operator new");
    }

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
