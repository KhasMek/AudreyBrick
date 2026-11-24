// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A: the strict JSON reader (preset/Json.h). Valid documents of every
// kind; malformed input of every kind; a valid preset truncated at every byte;
// deep nesting at and past the bound (and far past it, which must not recurse);
// the value-count bound; huge and tiny numbers; the number grammar; string
// escapes, surrogates and raw UTF-8; a byte order mark; CRLF; duplicate keys;
// the error positions; jsonQuote and jsonFloat (every float written reads back
// as the identical float). Run under ASan + UBSan as well.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "preset/Json.h"
#include "preset/PresetData.h"
#include "preset/PresetFormat.h"
#include "preset_test_common.h"

using namespace audreybrick;
using ptest::check;
using ptest::quiet;

namespace
{
bool ok(const std::string& text, JsonValue& v)
{
    JsonError e;
    return parseJson(text, v, e);
}
bool ok(const std::string& text)
{
    JsonValue v;
    return ok(text, v);
}
bool bad(const std::string& text, std::string* msg = nullptr, int* line = nullptr, int* col = nullptr)
{
    JsonValue v;
    JsonError e;
    const bool r = parseJson(text, v, e);
    if(msg != nullptr)
        *msg = e.message;
    if(line != nullptr)
        *line = e.line;
    if(col != nullptr)
        *col = e.column;
    return !r && !e.message.empty();
}
double num(const std::string& text)
{
    JsonValue v;
    if(!ok(text, v) || v.type != JsonValue::Type::Number)
        return std::numeric_limits<double>::quiet_NaN();
    return v.number;
}
std::string str(const std::string& text)
{
    JsonValue v;
    if(!ok(text, v) || v.type != JsonValue::Type::String)
        return "<<error>>";
    return v.string;
}

void testValid()
{
    JsonValue v;
    check(ok("null", v) && v.type == JsonValue::Type::Null, "null");
    check(ok("true", v) && v.type == JsonValue::Type::Bool && v.boolean, "true");
    check(ok("false", v) && v.type == JsonValue::Type::Bool && !v.boolean, "false");
    check(ok("  \t\r\n 0 \r\n", v) && v.type == JsonValue::Type::Number && v.number == 0.0, "whitespace around a value");
    check(ok("[]", v) && v.type == JsonValue::Type::Array && v.items.empty(), "empty array");
    check(ok("{}", v) && v.isObject() && v.items.empty(), "empty object");
    check(ok("[1, [2, [3]], {\"a\": [true, false, null]}]", v) && v.items.size() == 3 &&
              v.items[1].items[1].items[0].number == 3.0 && v.items[2].find("a") != nullptr &&
              v.items[2].find("a")->items.size() == 3,
          "nested arrays and objects");
    check(ok("{\"b\":1,\"a\":2}", v) && v.keys.size() == 2 && v.keys[0] == "b" && v.keys[1] == "a" &&
              v.find("a")->number == 2.0 && v.find("c") == nullptr,
          "object keys in file order, find()");
    check(ok("{\"\":1}", v) && v.find("") != nullptr, "empty key");
    check(ok("\"\"", v) && v.type == JsonValue::Type::String && v.string.empty(), "empty string");
    check(JsonValue().find("x") == nullptr, "find() on a non-object is nullptr");
}

void testMalformed()
{
    const char* cases[] = {
        "", " ", "\n", "nul", "nulll", "tru", "True", "FALSE", "NaN", "Infinity", "-Infinity", "undefined",
        "{", "}", "[", "]", "[1,]", "[,1]", "[1 2]", "{\"a\":1,}", "{\"a\" 1}", "{\"a\":}", "{a:1}", "{'a':1}",
        "{\"a\":1 \"b\":2}", "{1:2}", "[1]]", "{}{}", "1 2", "\"abc", "\"a\nb\"", "\"a\tb\"", "'a'", "[1,,2]",
        "{\"a\":1}x", "/* c */ 1", "// c\n1", "[1] // c", "+1", "01", "-01", "00", ".5", "5.", "-", "-.5",
        "1e", "1e+", "1E-", "0x10", "1.0e", "--1", "1..2", "1.2.3", "[\"\\x\"]", "\"\\u12\"", "\"\\u12G4\"",
        "\"\\\"", "\"\\", "\"\\U0041\"", "\"\\a\"", "\"\\0\"", "\"\\'\"", "\x01", "[\x7f]", "\xEF\xBB", "\xEF\xBB\xBF",
        "\xEF\xBB\xBF\xEF\xBB\xBF{}", "{\"a\":1,\"a\":2}", "[true false]", "[null,]", "{\"a\":[}", "{\"a\":{]}",
    };
    int n = 0;
    for(const char* c : cases)
    {
        quiet(bad(c), std::string("malformed rejected: ") + c);
        ++n;
    }
    check(true, "malformed inputs rejected (" + std::to_string(n) + " cases)");
    // A NUL byte inside the text (not as an escape).
    check(bad(std::string("[1,\0 2]", 7)), "raw NUL byte rejected");
    check(bad(std::string("\"a\0b\"", 5)), "raw NUL in a string rejected");
}

void testErrorPositions()
{
    std::string msg;
    int         line = 0, col = 0;
    check(bad("{\n  \"a\": 1\n  \"b\": 2\n}", &msg, &line, &col) && line == 3 && col == 3 &&
              msg == "expected ',' or '}'",
          "error position: line 3, column 3 (" + msg + " at " + std::to_string(line) + ":" + std::to_string(col) + ")");
    check(bad("{\r\n\"a\":1,\r\n\"a\":2}", &msg, &line, &col) && line == 3 && col == 1 && msg == "duplicate key",
          "duplicate key reported at the second key (CRLF lines)");
    check(bad("[1,\n2,\n", &msg, &line, &col) && msg == "unexpected end of text", "unexpected end of text");
    check(bad("01", &msg) && msg == "leading zero in a number", "leading zero message");
}

void testTruncation()
{
    PresetData p = presetDefaults();
    p.knobs[1]   = 0.123456789f;
    p.mod[2].target = ModTarget::ReverbMix;
    p.mod[2].depth  = -0.5f;
    const std::string text = encodePreset(p, "test \"version\" \\ 1");
    JsonValue         full;
    check(ok(text, full), "the encoded preset is valid JSON");
    // Every proper prefix that ends before the closing brace must fail (the
    // text is one object; only trailing whitespace may be dropped).
    const std::size_t closing = text.rfind('}');
    bool              all     = true;
    for(std::size_t n = 0; n <= closing; ++n)
        all = quiet(bad(text.substr(0, n)), "prefix of " + std::to_string(n) + " bytes rejected") && all;
    check(all, "a valid preset truncated at every byte before its last '}' is rejected (" +
                   std::to_string(closing + 1) + " prefixes)");
    check(ok(text.substr(0, closing + 1)), "the preset without its final newline is valid");
    // And the same for every byte replaced by a byte that cannot appear there
    // ('\x01' is never valid in JSON outside strings and not even inside).
    bool none = true;
    for(std::size_t i = 0; i < text.size(); ++i)
    {
        std::string t = text;
        t[i]          = '\x01';
        none          = quiet(bad(t), "byte " + std::to_string(i) + " replaced by 0x01 rejected") && none;
    }
    check(none, "every single byte replaced by 0x01 is rejected");
}

void testDepth()
{
    const auto nest = [](int depth, char open, char close) {
        return std::string(static_cast<std::size_t>(depth), open) + std::string(static_cast<std::size_t>(depth), close);
    };
    check(ok(nest(kMaxJsonDepth, '[', ']')), "arrays nested to the bound (32) accepted");
    check(bad(nest(kMaxJsonDepth + 1, '[', ']')), "arrays nested one past the bound rejected");
    std::string objs;
    for(int i = 0; i < kMaxJsonDepth; ++i)
        objs += "{\"a\":";
    objs += "1";
    objs += std::string(kMaxJsonDepth, '}');
    check(ok(objs), "objects nested to the bound accepted");
    std::string deeper = "{\"a\":" + objs + "}";
    check(bad(deeper), "objects nested one past the bound rejected");
    // Far past the bound: rejected without recursing (no stack overflow under ASan).
    check(bad(std::string(60000, '[')), "60000 open brackets rejected");
    check(bad(nest(30000, '[', ']')), "30000-deep nesting rejected");
    std::string mixed;
    for(int i = 0; i < 20000; ++i)
        mixed += "{\"k\":[";
    check(bad(mixed), "20000-deep mixed nesting (truncated) rejected");
}

void testSizeBounds()
{
    // Exactly 64 KiB: a long string, accepted.
    std::string s = "\"" + std::string(kMaxJsonBytes - 2, 'a') + "\"";
    check(s.size() == kMaxJsonBytes && ok(s), "a 64 KiB document accepted");
    std::string t = "\"" + std::string(kMaxJsonBytes - 1, 'a') + "\"";
    check(bad(t), "a document of 64 KiB + 1 rejected");
    // The value-count bound.
    std::string many = "[";
    for(std::size_t i = 0; i < kMaxJsonValues; ++i)
        many += i == 0 ? "0" : ",0";
    many += "]"; // kMaxJsonValues + 1 values (the array itself)
    check(many.size() <= kMaxJsonBytes && bad(many), "more than 16384 values rejected");
    std::string fewer = "[";
    for(std::size_t i = 0; i + 1 < kMaxJsonValues; ++i)
        fewer += i == 0 ? "0" : ",0";
    fewer += "]";
    check(ok(fewer), "exactly 16384 values accepted");
}

void testNumbers()
{
    check(num("0") == 0.0 && !std::signbit(num("0")), "0");
    check(num("-0") == 0.0 && std::signbit(num("-0")), "-0 keeps its sign");
    check(num("-0.0e5") == 0.0 && std::signbit(num("-0.0e5")), "-0.0e5 is -0");
    check(num("1") == 1.0 && num("-1") == -1.0 && num("10") == 10.0, "integers");
    check(num("0.5") == 0.5 && num("1.25e2") == 125.0 && num("1E-2") == 0.01 && num("2e+3") == 2000.0,
          "fractions and exponents");
    check(num("0.1") == 0.1, "0.1 is the double nearest 0.1");
    check(num("1.7976931348623157e308") == std::numeric_limits<double>::max(), "DBL_MAX accepted");
    check(bad("1.8e308") && bad("-1.8e308") && bad("1e309") && bad("1e999") && bad("1e99999999999999999999"),
          "numbers beyond a double rejected");
    check(bad("123456789012345678901234567890123456789e300"), "huge mantissa times a big exponent rejected");
    check(num("123456789012345678901234567890") == 1.2345678901234568e29, "a 30-digit integer reads");
    check(num("1e-300") == 1e-300, "1e-300 reads exactly");
    check(num("1e-301") == 0.0 && num("4.9e-324") == 0.0 && num("1e-99999") == 0.0 &&
              std::signbit(num("-1e-400")),
          "below 1e-300 reads as (signed) zero on every platform");
    check(num("0.00000000000000000000000000000000000001") == 1e-38, "many leading zeros in a fraction");
    std::string longZeros = "0." + std::string(400, '0') + "1";
    check(num(longZeros) == 0.0, "a fraction below 1e-300 written out reads as zero");
    std::string longDigits = "0." + std::string(5000, '3');
    check(std::fabs(num(longDigits) - 1.0 / 3.0) < 1e-16, "5000 fraction digits read");
    std::string bigInt = std::string(308, '9');
    check(num(bigInt) > 9.9e307, "a 308-digit integer reads");
    check(bad(std::string(310, '9')), "a 310-digit integer is too large");
}

void testStrings()
{
    check(str("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"") == "a\"b\\c/d\b\f\n\r\t", "every simple escape");
    check(str("\"\\u0041\\u00e9\\u20AC\"") == "A\xC3\xA9\xE2\x82\xAC", "\\u escapes to UTF-8 (1, 2, 3 bytes)");
    check(str("\"\\uD83D\\uDE00\"") == "\xF0\x9F\x98\x80", "a surrogate pair to one 4-byte code point");
    check(str("\"\\u0000\"") == std::string("\0", 1), "\\u0000 kept as a NUL");
    check(bad("\"\\uD83D\"") && bad("\"\\uD83Dx\"") && bad("\"\\uD83D\\u0041\"") && bad("\"\\uDE00\"") &&
              bad("\"\\uDE00\\uD83D\""),
          "lone and reversed surrogates rejected");
    check(str("\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\"") == "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "raw UTF-8 kept");
    check(bad("\"\xC3\"") && bad("\"\xC3x\"") && bad("\"\x80\"") && bad("\"\xC0\xAF\"") && bad("\"\xE0\x80\xAF\"") &&
              bad("\"\xED\xA0\x80\"") && bad("\"\xF4\x90\x80\x80\"") && bad("\"\xF5\x80\x80\x80\"") &&
              bad("\"\xFF\"") && bad("\"\xE2\x82\""),
          "invalid UTF-8 rejected (truncated, stray continuation, overlong, surrogate, above U+10FFFF)");
    check(bad("[\xC3\xA9]"), "raw UTF-8 outside a string rejected");
}

void testBomAndCrlf()
{
    PresetData        p    = presetDefaults();
    const std::string text = encodePreset(p, "x");
    check(ok("\xEF\xBB\xBF" + text), "a UTF-8 byte order mark at the start is skipped");
    check(bad(" \xEF\xBB\xBF{}"), "a byte order mark after whitespace is rejected");
    std::string crlf;
    for(char c : text)
    {
        if(c == '\n')
            crlf += "\r\n";
        else
            crlf.push_back(c);
    }
    JsonValue a, b;
    check(ok(crlf, a) && ok(text, b) && a.keys == b.keys, "CRLF line endings read like LF");
    PresetData  q;
    PresetError e;
    check(decodePreset("\xEF\xBB\xBF" + crlf, q, e) && presetBitsEqual(q, p), "a preset with a BOM and CRLF decodes");
}

void testQuoteAndFloat()
{
    const std::string raw = std::string("a\"b\\c\n\r\t\b\f\x01\x1f/\xC3\xA9", 15);
    check(str(jsonQuote(raw)) == raw, "jsonQuote round trip with every escape and UTF-8");
    check(jsonQuote("\x01") == "\"\\u0001\"", "control characters as \\u00XX");
    check(jsonFloat(0.0f) == "0" && jsonFloat(-0.0f) == "-0" && jsonFloat(1.0f) == "1" && jsonFloat(0.5f) == "0.5",
          "simple floats");
    check(jsonFloat(std::numeric_limits<float>::quiet_NaN()) == "0" &&
              jsonFloat(std::numeric_limits<float>::infinity()) == "0",
          "non-finite floats written as 0");
    // Every float written reads back bit for bit: special ones, then 200000 random bit patterns.
    std::vector<float> specials = {0.428571433f,
                                   0.53724216f,
                                   0.30151134f,
                                   0.70710678f,
                                   1e-7f,
                                   -1e-7f,
                                   std::numeric_limits<float>::min(),
                                   std::numeric_limits<float>::denorm_min(),
                                   std::numeric_limits<float>::max(),
                                   -std::numeric_limits<float>::max(),
                                   std::nextafter(1.0f, 0.0f),
                                   std::nextafter(0.0f, 1.0f),
                                   -0.0f,
                                   1.0f / 3.0f};
    bool               all = true;
    for(float f : specials)
    {
        const double d = num(jsonFloat(f));
        all = quiet(ptest::sameBits(static_cast<float>(d), f), "float round trip " + jsonFloat(f)) && all;
    }
    ptest::Rng rng;
    int        n = 0;
    for(int i = 0; i < 200000; ++i)
    {
        std::uint32_t bits = rng.next();
        float         f;
        std::memcpy(&f, &bits, sizeof f);
        if(!std::isfinite(f) || std::fabs(f) < 1e-30f)
            continue; // NaN/inf are not written; tiny ones read as 0 below 1e-300 only for doubles
        ++n;
        const double d = num(jsonFloat(f));
        all = quiet(ptest::sameBits(static_cast<float>(d), f), "float round trip " + jsonFloat(f)) && all;
    }
    check(all, "every float written with jsonFloat reads back bit for bit (" + std::to_string(n) +
                   " random bit patterns and " + std::to_string(specials.size()) + " special values)");
}

void testDuplicateKeys()
{
    check(bad("{\"a\":1,\"b\":2,\"a\":3}"), "duplicate key rejected");
    check(bad("{\"x\":{\"a\":1,\"a\":1}}"), "duplicate key in a nested object rejected");
    check(ok("{\"a\":1,\"A\":2}"), "keys differing in case are different keys for the reader");
    check(bad("{\"\\u0061\":1,\"a\":2}"), "duplicate after unescaping rejected");
}

// PRESET-B (the review of PRESET-A): at most kMaxJsonObjectKeys keys in one
// object, so the duplicate check stays cheap; a hostile 64 KiB object of
// distinct tiny keys fails fast.
void testKeyCap()
{
    auto object = [](std::size_t keys) {
        std::string t = "{";
        for(std::size_t i = 0; i < keys; ++i)
            t += (i == 0 ? "\"k" : ",\"k") + std::to_string(i) + "\":0";
        return t + "}";
    };
    check(ok(object(kMaxJsonObjectKeys)), "an object with exactly 256 keys accepted");
    std::string msg;
    check(bad(object(kMaxJsonObjectKeys + 1), &msg) && msg == "too many keys in one object",
          "an object with 257 keys rejected (\"" + msg + "\")");
    // Several objects of 256 keys each are fine (the cap is per object).
    std::string arr = "[" + object(200) + "," + object(200) + "]";
    check(ok(arr), "the cap is per object");
    // The hostile case: as many distinct keys as fit in 64 KiB.
    std::string hostile = "{";
    for(std::size_t i = 0; hostile.size() + 16 < kMaxJsonBytes; ++i)
        hostile += (i == 0 ? "\"" : ",\"") + std::to_string(i) + "\":0";
    hostile += "}";
    const auto t0 = std::chrono::steady_clock::now();
    const bool rejected = bad(hostile, &msg);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(rejected && msg == "too many keys in one object" && ms < 50.0,
          "a 64 KiB object of tiny distinct keys is rejected quickly (" + std::to_string(ms) + " ms)");
}
} // namespace

int main()
{
    testValid();
    testMalformed();
    testErrorPositions();
    testTruncation();
    testDepth();
    testSizeBounds();
    testNumbers();
    testStrings();
    testBomAndCrlf();
    testQuoteAndFloat();
    testDuplicateKeys();
    testKeyCap();
    return ptest::finish();
}
