// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Render metadata stored in a parameter script's comment header. This is the only
// parser for these lines: audrey-render, wavcheck --script and the parity test use
// it; scripts/make_reference.sh does no parsing of its own.
//
// The script format (src/control/ScriptControlSource.h) has no duration or input
// field, so each script in scripts/ states them in comment lines of this form:
//
//   # @duration <seconds>     length of the render (0 < d <= 3600)
//   # @input <file.wav>       input WAV: a plain file name, looked up in the inputs
//                             directory (reference/inputs/ for the reference set)
//   # @jump <threshold>       wavcheck absolute jump threshold, when the render needs
//                             a higher one than wavcheck's default
//   # @click <ratio>          wavcheck level-relative click threshold, likewise
//   # @click-near <ratio>     wavcheck click threshold just after events, likewise
//
// A directive line is any line whose first non-blank character is '#', followed
// (with or without blanks) by '@name' and exactly one value; leading indentation is
// allowed. To ScriptControlSource these are ordinary comments. Each directive may
// appear at most once; an unknown '@name' is an error. Numbers use the script
// grammar (0.5, not .5). @input must be a plain file name: not empty, not "." or
// "..", and without '/' or '\'.

#pragma once

#include <string>

namespace audreybrick::tools
{

struct ScriptMeta
{
    bool        hasDuration     = false;
    double      durationSeconds = 0.0;
    std::string inputFile;            // empty = no input
    double      jumpThreshold  = 0.0; // 0 = not set
    double      clickThreshold = 0.0; // 0 = not set
    double      clickNearThreshold = 0.0; // 0 = not set
};

// Parses a plain decimal ([+-]digits[.digits][(e|E)[+-]digits]), locale-independent.
bool parsePlainDecimal(const std::string& token, double& out);

// True if `name` is acceptable as an @input value (see above).
bool isPlainFileName(const std::string& name);

// Parses the directives. Does not require @duration; callers that need it check
// hasDuration.
bool parseScriptMeta(const std::string& scriptText, ScriptMeta& out, std::string& error);
bool readScriptMeta(const std::string& path, ScriptMeta& out, std::string& error);

} // namespace audreybrick::tools
