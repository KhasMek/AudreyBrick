// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// SpscQueue with a real producer thread and a real consumer thread, at
// capacities 1, 2, 3 and 7, with two payloads:
//   - three words that all carry the same sequence number, so a torn element (a
//     slot read while it is being written) shows as words that disagree;
//   - a plain std::uint64_t sequence number.
// Every value must be popped exactly once, in order, untorn. Under
// ThreadSanitizer this also checks the queue's memory ordering: if the producer
// published the tail without release (or the consumer read it without acquire),
// TSan reports the slot access as a data race.
//
// Why three words and not more: measured with Apple clang 21 on arm64, TSan does
// not see a struct copy of 32 bytes or more (it becomes a vector or memcpy copy
// that is not checked), so a wider payload would hide exactly the race this test
// is for. At -O2 (the tsan build) copies of up to 24 bytes and scalars are seen.
//
// Pushes per capacity: 2,000,000 in optimised builds without TSan; fewer under
// TSan and in unoptimised (sanitizer) builds, which are much slower.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <thread>

#include "control/SpscQueue.h"
#include "control_test_common.h"

using namespace audreybrick;
using ctest::check;

#if defined(__SANITIZE_THREAD__)
#define AB_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define AB_TSAN 1
#endif
#endif

namespace
{
#if defined(AB_TSAN)
constexpr std::uint64_t kPushes = 200000;
#elif defined(NDEBUG)
constexpr std::uint64_t kPushes = 2000000;
#else
constexpr std::uint64_t kPushes = 500000;
#endif

struct Payload
{
    std::uint64_t word[3];
};

std::uint64_t seqOf(const Payload& p) { return p.word[0]; }
bool          torn(const Payload& p) { return p.word[1] != p.word[0] || p.word[2] != p.word[0]; }
void          fill(Payload& p, std::uint64_t seq) { p.word[0] = p.word[1] = p.word[2] = seq; }

std::uint64_t seqOf(std::uint64_t v) { return v; }
bool          torn(std::uint64_t) { return false; }
void          fill(std::uint64_t& v, std::uint64_t seq) { v = seq; }

struct Result
{
    std::uint64_t popped = 0, outOfOrder = 0, torn = 0;
};

template <typename T>
Result runOnce(std::size_t capacity)
{
    SpscQueue<T> q(capacity);
    Result       res;
    std::thread  producer([&] {
        for(std::uint64_t seq = 0; seq < kPushes; ++seq)
        {
            T p;
            fill(p, seq);
            while(!q.push(p))
                std::this_thread::yield();
        }
    });
    std::thread consumer([&] {
        std::uint64_t expect = 0;
        T             p;
        while(expect < kPushes)
        {
            if(!q.pop(p))
            {
                std::this_thread::yield();
                continue;
            }
            if(torn(p))
                ++res.torn;
            if(seqOf(p) != expect)
                ++res.outOfOrder;
            expect = seqOf(p) + 1; // resynchronise so one error is counted once
            ++res.popped;
        }
    });
    producer.join();
    consumer.join();
    T extra;
    if(q.pop(extra))
        ++res.outOfOrder; // nothing may be left over
    return res;
}

template <typename T>
void runAll(const char* name)
{
    for(std::size_t cap : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7}})
    {
        const Result r = runOnce<T>(cap);
        std::printf("       %s, capacity %zu: popped %llu, out of order %llu, torn %llu\n", name, cap,
                    static_cast<unsigned long long>(r.popped), static_cast<unsigned long long>(r.outOfOrder),
                    static_cast<unsigned long long>(r.torn));
        check(r.popped == kPushes && r.outOfOrder == 0 && r.torn == 0,
              std::string(name) + ", capacity " + std::to_string(cap) +
                  ": every value popped exactly once, in order, untorn");
    }
}
} // namespace

int main()
{
    std::printf("       %llu pushes per capacity\n", static_cast<unsigned long long>(kPushes));
    runAll<Payload>("3-word payload");
    runAll<std::uint64_t>("uint64 payload");
    return ctest::finish();
}
