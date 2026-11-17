// Shared setup for the M1-A smoke tests: sets all eleven Engine parameters
// through the upstream Engine setters (engine units, not normalised knob values).
#pragma once

#include "FeedbackSynthEngine.h"

namespace smoke
{
constexpr float kSampleRate = 48000.0f;

inline void setAllParameters(infrasonic::FeedbackSynth::Engine& engine, float feedbackGainDb)
{
    engine.SetStringPitch(40.0f);           // MIDI note (82.4 Hz)
    engine.SetFeedbackGain(feedbackGainDb); // dBFS, -60 to +12
    engine.SetFeedbackDelay(0.01f);         // body delay, seconds
    engine.SetFeedbackLPFCutoff(8000.0f);   // Hz
    engine.SetFeedbackHPFCutoff(120.0f);    // Hz
    engine.SetReverbMix(0.3f);              // 0..1
    engine.SetReverbFeedback(0.85f);        // 0.2..1
    engine.SetEchoDelaySendAmount(0.25f);   // 0..1
    engine.SetEchoDelayTime(0.4f);          // seconds
    engine.SetEchoDelayFeedback(0.6f);      // 0..1.5
    engine.SetOutputLevel(0.5f);            // 0..1
}
} // namespace smoke
