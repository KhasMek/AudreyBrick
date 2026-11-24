// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md): a small, strict JSON reader and the two
// writing helpers the preset format needs. Ours; no third-party code. No SDL.
// UI thread or tools only (it allocates); never on the audio thread.
//
// Reading (parseJson):
//   - RFC 8259 grammar, strictly: objects, arrays, strings, numbers, true,
//     false, null. No comments, no trailing commas, no single quotes, no
//     leading '+', no leading zeros ("01"), no ".5" or "5.", no hex, no NaN or
//     Infinity, nothing but whitespace after the top-level value.
//   - Whitespace: space, tab, LF, CR (so CRLF files read as LF files).
//   - A UTF-8 byte order mark (EF BB BF) at the very start is skipped.
//   - Strings: the escapes \" \\ \/ \b \f \n \r \t and \uXXXX (a surrogate
//     pair becomes one code point; a lone or reversed surrogate is an error);
//     raw control characters (below 0x20) are an error; the raw bytes must be
//     valid UTF-8 (no overlong forms, no surrogates, nothing above U+10FFFF).
//     The result is UTF-8. A NUL (\u0000) is allowed and kept.
//   - Numbers are read as double, independent of the C and C++ locale. A number
//     whose value is not finite as a double (1e999) is an error. A number whose
//     magnitude is below 1e-300 reads as 0 (with its sign), so every platform
//     gives the same result (the C libraries differ in how they report
//     underflow); the preset format has no use for such values.
//   - Duplicate keys in one object are an error (exact byte comparison).
//   - Bounded: input of at most kMaxJsonBytes, nesting of at most
//     kMaxJsonDepth arrays/objects (the recursion is bounded by it), at most
//     kMaxJsonValues values in all, at most kMaxJsonObjectKeys keys in one
//     object (PRESET-A review: it bounds the duplicate-key check, which is
//     quadratic in the keys of one object; a preset object has at most 15).
//   - No exception escapes: an allocation failure is reported as an error.
//
// Errors carry the line and column (1-based, in bytes) of the offending byte.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace audreybrick
{

constexpr std::size_t kMaxJsonBytes  = 64 * 1024;
constexpr int         kMaxJsonDepth  = 32;
constexpr std::size_t kMaxJsonValues = 16 * 1024;
constexpr std::size_t kMaxJsonObjectKeys = 256;

class JsonValue
{
  public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type        type    = Type::Null;
    bool        boolean = false;
    double      number  = 0.0;
    std::string string;
    // Array: the elements. Object: the member values, in file order, with
    // their keys in `keys` (same index).
    std::vector<JsonValue>   items;
    std::vector<std::string> keys;

    bool isObject() const noexcept { return type == Type::Object; }
    // Object: the member with exactly this key, or nullptr.
    const JsonValue* find(std::string_view key) const noexcept;
};

struct JsonError
{
    std::string message; // e.g. "expected ':'"
    int         line   = 0;
    int         column = 0;
};

// Parses `text`. On success fills `out` and returns true; otherwise fills
// `error` and returns false (`out` is then unspecified).
bool parseJson(std::string_view text, JsonValue& out, JsonError& error) noexcept;

// A JSON string literal (with the quotes) for UTF-8 `s`: '"' and '\' escaped,
// control characters as \n, \t, ... or \u00XX. Other bytes are copied.
std::string jsonQuote(std::string_view s);

// The shortest-safe text for a float that reads back as the identical float:
// "%.9g" in the classic locale (never a decimal comma). -0 is "-0". Not for
// NaN or infinity (they cannot be written in JSON): those give "0".
std::string jsonFloat(float v);

} // namespace audreybrick
