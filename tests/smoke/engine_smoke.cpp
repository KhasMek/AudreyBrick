// M1-A smoke test: the unmodified upstream Engine builds and runs on the host.
//
// Case 1: feedback gain +6 dB for 5 s. Every output sample is finite and the
//         output is not all zeros.
// Case 2: feedback gain -60 dB for 5 s. Output peak is below 0.001.
//
// The engine output is checked directly (no output limiter; that belongs to the
// voice wrapper in M1-B).

#include <cmath>
#include <cstdio>
#include <memory>

#include "ParameterRegistry.h"
#include "engine_setup.h"

namespace
{
// Compile and link check for the header-only upstream ParameterRegistry and
// SmoothedValue, which no vendored .cpp file includes. Returns the value the
// handler received after one Process() call.
enum class TestParam
{
    Gain
};

float registryRoundTrip()
{
    infrasonic::ParameterRegistry<TestParam> registry;
    registry.Init(smoke::kSampleRate / 4.0f);
    float received = -1.0f;
    registry.Register(TestParam::Gain, 0.0f, -60.0f, 12.0f,
                      [&received](const float v) { received = v; }, 0.05f);
    registry.UpdateNormalized(TestParam::Gain, 1.0f);
    registry.Process();
    return received;
}

struct RunStats
{
    long  nonFinite = 0;
    long  nonZero   = 0;
    float peak      = 0.0f;
};

RunStats run(float feedbackGainDb, float seconds)
{
    auto engine = std::make_unique<infrasonic::FeedbackSynth::Engine>();
    engine->Init(smoke::kSampleRate);
    smoke::setAllParameters(*engine, feedbackGainDb);

    RunStats   stats;
    const long frames = static_cast<long>(seconds * smoke::kSampleRate);
    for(long i = 0; i < frames; ++i)
    {
        float outL = 0.0f;
        float outR = 0.0f;
        engine->Process(0.0f, outL, outR);
        for(float s : {outL, outR})
        {
            if(!std::isfinite(s))
            {
                ++stats.nonFinite;
                continue;
            }
            if(s != 0.0f)
                ++stats.nonZero;
            const float a = std::fabs(s);
            if(a > stats.peak)
                stats.peak = a;
        }
    }
    return stats;
}

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok)
        ++failures;
}
} // namespace

int main()
{
    {
        // SmoothedValue snaps to its target on the first get() (docs/PARAMETERS.md section 4).
        const float v = registryRoundTrip();
        std::printf("registry: UpdateNormalized(1.0) on -60..12 gives %.3f after one Process()\n",
                    static_cast<double>(v));
        check(v == 12.0f, "ParameterRegistry builds and maps n=1 to the range maximum");
    }
    {
        const RunStats s = run(6.0f, 5.0f);
        std::printf("feedback +6 dB, 5 s: non-finite=%ld non-zero=%ld peak=%.6f\n",
                    s.nonFinite, s.nonZero, static_cast<double>(s.peak));
        check(s.nonFinite == 0, "feedback +6 dB: every output sample is finite");
        check(s.nonZero > 0, "feedback +6 dB: output is not all zeros");
        check(s.peak > 0.05f, "feedback +6 dB: output peak above 0.05 (feedback builds)");
    }
    {
        const RunStats s = run(-60.0f, 5.0f);
        std::printf("feedback -60 dB, 5 s: non-finite=%ld non-zero=%ld peak=%.6g\n",
                    s.nonFinite, s.nonZero, static_cast<double>(s.peak));
        check(s.nonFinite == 0, "feedback -60 dB: every output sample is finite");
        check(s.peak < 0.001f, "feedback -60 dB: output peak below 0.001");
    }
    return failures == 0 ? 0 : 1;
}
