// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <locale>
#include <new>
#include <sstream>
#include <string>
#include <utility>

namespace audreybrick
{

const JsonValue* JsonValue::find(std::string_view key) const noexcept
{
    if(type != Type::Object)
        return nullptr;
    for(std::size_t i = 0; i < keys.size() && i < items.size(); ++i)
        if(keys[i] == key)
            return &items[i];
    return nullptr;
}

namespace
{
class Parser
{
  public:
    explicit Parser(std::string_view t) noexcept : t_(t) {}

    bool parse(JsonValue& out, JsonError& err)
    {
        if(t_.size() > kMaxJsonBytes)
            return fail(err, "text larger than 64 KiB", 0);
        if(t_.size() >= 3 && static_cast<unsigned char>(t_[0]) == 0xEF && static_cast<unsigned char>(t_[1]) == 0xBB &&
           static_cast<unsigned char>(t_[2]) == 0xBF)
            pos_ = 3;
        skipWs();
        if(!value(out, 0, err))
            return false;
        skipWs();
        if(pos_ != t_.size())
            return fail(err, "unexpected text after the value", pos_);
        return true;
    }

  private:
    bool fail(JsonError& err, const char* msg, std::size_t at)
    {
        // Line and column of byte `at`.
        int line = 1, col = 1;
        for(std::size_t i = 0; i < at && i < t_.size(); ++i)
        {
            if(t_[i] == '\n')
            {
                ++line;
                col = 1;
            }
            else
                ++col;
        }
        err.message = msg;
        err.line    = line;
        err.column  = col;
        return false;
    }

    void skipWs() noexcept
    {
        while(pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(const char* word, JsonError& err)
    {
        const std::size_t start = pos_;
        for(const char* p = word; *p != '\0'; ++p, ++pos_)
            if(pos_ >= t_.size() || t_[pos_] != *p)
                return fail(err, pos_ >= t_.size() ? "unexpected end of text" : "invalid literal", pos_ >= t_.size() ? pos_ : start);
        return true;
    }

    bool value(JsonValue& v, int depth, JsonError& err)
    {
        if(++count_ > kMaxJsonValues)
            return fail(err, "too many values", pos_);
        if(pos_ >= t_.size())
            return fail(err, "unexpected end of text", pos_);
        const char c = t_[pos_];
        switch(c)
        {
            case '{': return object(v, depth, err);
            case '[': return array(v, depth, err);
            case '"': v.type = JsonValue::Type::String; return string(v.string, err);
            case 't': v.type = JsonValue::Type::Bool; v.boolean = true; return literal("true", err);
            case 'f': v.type = JsonValue::Type::Bool; v.boolean = false; return literal("false", err);
            case 'n': v.type = JsonValue::Type::Null; return literal("null", err);
            default:
                if(c == '-' || (c >= '0' && c <= '9'))
                {
                    v.type = JsonValue::Type::Number;
                    return number(v.number, err);
                }
                return fail(err, "expected a value", pos_);
        }
    }

    bool object(JsonValue& v, int depth, JsonError& err)
    {
        if(depth + 1 > kMaxJsonDepth)
            return fail(err, "nested too deeply", pos_);
        v.type = JsonValue::Type::Object;
        ++pos_; // '{'
        skipWs();
        if(pos_ < t_.size() && t_[pos_] == '}')
        {
            ++pos_;
            return true;
        }
        for(;;)
        {
            skipWs();
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text", pos_);
            if(t_[pos_] != '"')
                return fail(err, "expected a key in quotes", pos_);
            const std::size_t keyAt = pos_;
            // PRESET-A review: the duplicate check below compares each key with
            // every earlier one; the cap bounds it (256 keys: at most 32640
            // short comparisons per object; a hostile 64 KiB object of tiny
            // keys would otherwise cost tens of millions).
            if(v.keys.size() >= kMaxJsonObjectKeys)
                return fail(err, "too many keys in one object", keyAt);
            std::string key;
            if(!string(key, err))
                return false;
            for(const std::string& k : v.keys)
                if(k == key)
                    return fail(err, "duplicate key", keyAt);
            skipWs();
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text", pos_);
            if(t_[pos_] != ':')
                return fail(err, "expected ':'", pos_);
            ++pos_;
            skipWs();
            v.keys.push_back(std::move(key));
            v.items.emplace_back();
            if(!value(v.items.back(), depth + 1, err))
                return false;
            skipWs();
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text", pos_);
            if(t_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if(t_[pos_] == '}')
            {
                ++pos_;
                return true;
            }
            return fail(err, "expected ',' or '}'", pos_);
        }
    }

    bool array(JsonValue& v, int depth, JsonError& err)
    {
        if(depth + 1 > kMaxJsonDepth)
            return fail(err, "nested too deeply", pos_);
        v.type = JsonValue::Type::Array;
        ++pos_; // '['
        skipWs();
        if(pos_ < t_.size() && t_[pos_] == ']')
        {
            ++pos_;
            return true;
        }
        for(;;)
        {
            skipWs();
            v.items.emplace_back();
            if(!value(v.items.back(), depth + 1, err))
                return false;
            skipWs();
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text", pos_);
            if(t_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if(t_[pos_] == ']')
            {
                ++pos_;
                return true;
            }
            return fail(err, "expected ',' or ']'", pos_);
        }
    }

    static void appendUtf8(std::string& s, std::uint32_t cp)
    {
        if(cp < 0x80)
            s.push_back(static_cast<char>(cp));
        else if(cp < 0x800)
        {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if(cp < 0x10000)
        {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    // Four hex digits at pos_ (advanced past them).
    bool hex4(std::uint32_t& out, JsonError& err)
    {
        out = 0;
        for(int i = 0; i < 4; ++i, ++pos_)
        {
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text", pos_);
            const char     c = t_[pos_];
            std::uint32_t d;
            if(c >= '0' && c <= '9')
                d = static_cast<std::uint32_t>(c - '0');
            else if(c >= 'a' && c <= 'f')
                d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F')
                d = static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return fail(err, "bad \\u escape", pos_);
            out = out * 16 + d;
        }
        return true;
    }

    // One raw UTF-8 sequence starting at pos_ (a byte >= 0x80), validated and copied.
    bool utf8(std::string& s, JsonError& err)
    {
        const std::size_t   at = pos_;
        const unsigned char b0 = static_cast<unsigned char>(t_[pos_]);
        int                 n;
        std::uint32_t       cp;
        std::uint32_t       min;
        if(b0 >= 0xC2 && b0 <= 0xDF)
        {
            n   = 1;
            cp  = b0 & 0x1F;
            min = 0x80;
        }
        else if(b0 >= 0xE0 && b0 <= 0xEF)
        {
            n   = 2;
            cp  = b0 & 0x0F;
            min = 0x800;
        }
        else if(b0 >= 0xF0 && b0 <= 0xF4)
        {
            n   = 3;
            cp  = b0 & 0x07;
            min = 0x10000;
        }
        else
            return fail(err, "invalid UTF-8", at);
        for(int i = 1; i <= n; ++i)
        {
            if(at + static_cast<std::size_t>(i) >= t_.size())
                return fail(err, "invalid UTF-8", at);
            const unsigned char b = static_cast<unsigned char>(t_[at + static_cast<std::size_t>(i)]);
            if((b & 0xC0) != 0x80)
                return fail(err, "invalid UTF-8", at);
            cp = (cp << 6) | (b & 0x3F);
        }
        if(cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return fail(err, "invalid UTF-8", at);
        s.append(t_.data() + at, static_cast<std::size_t>(n) + 1);
        pos_ = at + static_cast<std::size_t>(n) + 1;
        return true;
    }

    bool string(std::string& s, JsonError& err)
    {
        s.clear();
        ++pos_; // opening quote
        for(;;)
        {
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text in a string", pos_);
            const char          c  = t_[pos_];
            const unsigned char uc = static_cast<unsigned char>(c);
            if(c == '"')
            {
                ++pos_;
                return true;
            }
            if(uc < 0x20)
                return fail(err, "control character in a string", pos_);
            if(uc >= 0x80)
            {
                if(!utf8(s, err))
                    return false;
                continue;
            }
            if(c != '\\')
            {
                s.push_back(c);
                ++pos_;
                continue;
            }
            const std::size_t escAt = pos_;
            ++pos_;
            if(pos_ >= t_.size())
                return fail(err, "unexpected end of text in a string", pos_);
            const char e = t_[pos_++];
            switch(e)
            {
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                case '/': s.push_back('/'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'u':
                {
                    std::uint32_t cp;
                    if(!hex4(cp, err))
                        return false;
                    if(cp >= 0xDC00 && cp <= 0xDFFF)
                        return fail(err, "lone low surrogate", escAt);
                    if(cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        if(pos_ + 1 >= t_.size() || t_[pos_] != '\\' || t_[pos_ + 1] != 'u')
                            return fail(err, "lone high surrogate", escAt);
                        pos_ += 2;
                        std::uint32_t lo;
                        if(!hex4(lo, err))
                            return false;
                        if(lo < 0xDC00 || lo > 0xDFFF)
                            return fail(err, "lone high surrogate", escAt);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(s, cp);
                    break;
                }
                default: return fail(err, "bad escape", escAt);
            }
        }
    }

    bool number(double& out, JsonError& err)
    {
        const std::size_t start = pos_;
        const auto        digitAt = [&](std::size_t i) { return i < t_.size() && t_[i] >= '0' && t_[i] <= '9'; };
        bool              negative = false;
        if(t_[pos_] == '-')
        {
            negative = true;
            ++pos_;
        }
        if(!digitAt(pos_))
            return fail(err, pos_ >= t_.size() ? "unexpected end of text" : "bad number", pos_);
        // Significant digits and the decimal exponent of the first non-zero digit.
        long firstNonZero = -1; // index among all mantissa digits
        long intDigits    = 0;
        long digitIndex   = 0;
        if(t_[pos_] == '0')
        {
            ++pos_;
            ++intDigits;
            ++digitIndex;
            if(digitAt(pos_))
                return fail(err, "leading zero in a number", start);
        }
        else
        {
            while(digitAt(pos_))
            {
                if(firstNonZero < 0 && t_[pos_] != '0')
                    firstNonZero = digitIndex;
                ++pos_;
                ++intDigits;
                ++digitIndex;
            }
        }
        if(pos_ < t_.size() && t_[pos_] == '.')
        {
            ++pos_;
            if(!digitAt(pos_))
                return fail(err, pos_ >= t_.size() ? "unexpected end of text" : "bad number", pos_);
            while(digitAt(pos_))
            {
                if(firstNonZero < 0 && t_[pos_] != '0')
                    firstNonZero = digitIndex;
                ++pos_;
                ++digitIndex;
            }
        }
        long exp10 = 0;
        if(pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E'))
        {
            ++pos_;
            bool expNeg = false;
            if(pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-'))
            {
                expNeg = t_[pos_] == '-';
                ++pos_;
            }
            if(!digitAt(pos_))
                return fail(err, pos_ >= t_.size() ? "unexpected end of text" : "bad number", pos_);
            while(digitAt(pos_))
            {
                if(exp10 < 100000000L)
                    exp10 = exp10 * 10 + (t_[pos_] - '0');
                ++pos_;
            }
            if(expNeg)
                exp10 = -exp10;
        }
        if(firstNonZero < 0)
        {
            out = negative ? -0.0 : 0.0; // every digit is zero
            return true;
        }
        // The value is d.ddd x 10^mag.
        const long mag = exp10 + (intDigits - 1 - firstNonZero);
        if(mag > 308)
            return fail(err, "number too large", start);
        if(mag < -300)
        {
            out = negative ? -0.0 : 0.0; // see Json.h: the same on every platform
            return true;
        }
        std::istringstream in(std::string(t_.substr(start, pos_ - start)));
        in.imbue(std::locale::classic());
        double v = 0.0;
        in >> v;
        if(in.fail() || !std::isfinite(v))
            return fail(err, "number too large", start);
        out = v;
        return true;
    }

    std::string_view t_;
    std::size_t      pos_   = 0;
    std::size_t      count_ = 0;
};
} // namespace

bool parseJson(std::string_view text, JsonValue& out, JsonError& error) noexcept
{
    try
    {
        out = JsonValue{};
        Parser p(text);
        return p.parse(out, error);
    }
    catch(...)
    {
        try
        {
            error.message = "out of memory";
        }
        catch(...)
        {
        }
        error.line   = 0;
        error.column = 0;
        return false;
    }
}

std::string jsonQuote(std::string_view s)
{
    std::string o;
    o.reserve(s.size() + 2);
    o.push_back('"');
    for(const char c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        switch(c)
        {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if(u < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(u));
                    o += buf;
                }
                else
                    o.push_back(c);
        }
    }
    o.push_back('"');
    return o;
}

std::string jsonFloat(float v)
{
    if(!std::isfinite(v))
        return "0";
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os.precision(9);
    os << static_cast<double>(v);
    return os.str();
}

} // namespace audreybrick
