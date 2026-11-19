// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// The dr_wav implementation for the app's file input (third_party/dr_libs/dr_wav.h,
// MIT-0), compiled once in the audrey_input_wav target with warnings off. The
// offline tools compile their own copy (src/tools/dr_wav_impl.cpp); no binary
// links both targets. Only the decoder is used, on the loader thread.

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
