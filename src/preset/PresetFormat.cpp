// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/PresetFormat.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "core/ParamTable.h"
#include "preset/Json.h"

namespace audreybrick
{
namespace
{
const char* typeName(JsonValue::Type t) noexcept
{
    switch(t)
    {
        case JsonValue::Type::Null: return "null";
        case JsonValue::Type::Bool: return "a boolean";
        case JsonValue::Type::Number: return "a number";
        case JsonValue::Type::String: return "a string";
        case JsonValue::Type::Array: return "an array";
        case JsonValue::Type::Object: return "an object";
    }
    return "?";
}

bool setError(PresetError& e, const char* reason, const std::string& detail)
{
    e.reason = reason;
    e.detail = detail;
    return false;
}

bool wrongType(PresetError& e, const std::string& path, const char* expected, const JsonValue& v)
{
    return setError(e, "bad value", path + " is " + typeName(v.type) + ", not " + expected);
}

float clampTo(double v, double lo, double hi) noexcept
{
    return static_cast<float>(v < lo ? lo : (v > hi ? hi : v));
}

bool decodeSlot(const JsonValue& o, std::size_t index, ModSlotSettings& s, PresetError& e)
{
    char pathBuf[16];
    std::snprintf(pathBuf, sizeof pathBuf, "mod[%zu]", index);
    const std::string path = pathBuf;
    if(!o.isObject())
        return wrongType(e, path, "an object", o);
    for(std::size_t i = 0; i < o.keys.size(); ++i)
    {
        const std::string& k = o.keys[i];
        const JsonValue&   v = o.items[i];
        const std::string  p = path + "." + k;
        if(k == "shape" || k == "target" || k == "polarity")
        {
            if(v.type != JsonValue::Type::String)
                return wrongType(e, p, "a string", v);
            bool ok = false;
            if(k == "shape")
                ok = modShapeFromName(v.string, s.shape);
            else if(k == "target")
                ok = modTargetFromName(v.string, s.target);
            else
                ok = modPolarityFromName(v.string, s.polarity);
            if(!ok)
                return setError(e, "bad value", p + ": unknown name \"" + v.string + "\"");
        }
        else if(k == "rate" || k == "depth")
        {
            if(v.type != JsonValue::Type::Number)
                return wrongType(e, p, "a number", v);
            if(k == "rate")
                s.rateNorm = clampTo(v.number, 0.0, 1.0);
            else
                s.depth = clampTo(v.number, -1.0, 1.0);
        }
        // Anything else: ignored.
    }
    s = sanitizeModSlot(s);
    return true;
}

bool decodeTree(const JsonValue& root, PresetData& out, PresetError& e)
{
    if(!root.isObject())
        return setError(e, "not a preset", std::string("the file holds ") + typeName(root.type) + ", not an object");

    const JsonValue* format = root.find("format");
    if(format == nullptr)
        return setError(e, "not a preset", "no \"format\" key");
    if(format->type != JsonValue::Type::String || format->string != kPresetFormatMarker)
        return setError(e, "not a preset", "\"format\" is not \"audreybrick-preset\"");

    const JsonValue* version = root.find("version");
    if(version == nullptr)
        return setError(e, "bad version", "no \"version\" key");
    if(version->type != JsonValue::Type::Number)
        return wrongType(e, "version", "a number", *version);
    const double ver = version->number;
    if(ver != std::floor(ver) || ver < 1.0)
        return setError(e, "bad version", "\"version\" must be a whole number of at least 1");
    if(ver > static_cast<double>(kPresetFormatVersion))
    {
        char buf[64];
        std::snprintf(buf, sizeof buf, "version %.0f; this app reads version %d", ver, kPresetFormatVersion);
        return setError(e, "made by a newer version", buf);
    }

    PresetData p = presetDefaults();

    if(const JsonValue* app = root.find("app"))
        if(app->type != JsonValue::Type::String)
            return wrongType(e, "app", "a string", *app);

    if(const JsonValue* knobs = root.find("knobs"))
    {
        if(!knobs->isObject())
            return wrongType(e, "knobs", "an object", *knobs);
        bool seen[kKnobCount] = {};
        for(std::size_t i = 0; i < knobs->keys.size(); ++i)
        {
            Knob k;
            if(!knobFromId(knobs->keys[i], k))
                continue; // unknown: ignored
            const std::size_t ki   = static_cast<std::size_t>(k);
            const std::string path = "knobs." + knobs->keys[i];
            if(seen[ki])
                return setError(e, "bad value", path + " names " + presetKnobName(k) + " a second time");
            seen[ki]               = true;
            const JsonValue& v = knobs->items[i];
            if(v.type != JsonValue::Type::Number)
                return wrongType(e, path, "a number", v);
            p.knobs[ki] = clampTo(v.number, 0.0, 1.0);
        }
    }

    if(const JsonValue* sw = root.find("delaySwitch"))
    {
        if(sw->type != JsonValue::Type::Bool)
            return wrongType(e, "delaySwitch", "a boolean", *sw);
        p.delaySwitch = sw->boolean;
    }

    if(const JsonValue* mod = root.find("mod"))
    {
        if(mod->type != JsonValue::Type::Array)
            return wrongType(e, "mod", "an array", *mod);
        for(std::size_t i = 0; i < mod->items.size() && i < kModSlots; ++i)
            if(!decodeSlot(mod->items[i], i, p.mod[i], e))
                return false;
    }

    out = sanitizePreset(p);
    return true;
}
} // namespace

std::string encodePreset(const PresetData& in, std::string_view appVersion)
{
    const PresetData p = sanitizePreset(in);
    std::string      s;
    s += "{\n";
    s += "  \"format\": \"";
    s += kPresetFormatMarker;
    s += "\",\n";
    s += "  \"version\": " + std::to_string(kPresetFormatVersion) + ",\n";
    s += "  \"app\": " + jsonQuote(appVersion) + ",\n";
    s += "  \"knobs\": {\n";
    for(std::size_t i = 0; i < kKnobCount; ++i)
    {
        s += "    ";
        s += jsonQuote(presetKnobName(static_cast<Knob>(i)));
        s += ": " + jsonFloat(p.knobs[i]);
        s += i + 1 < kKnobCount ? ",\n" : "\n";
    }
    s += "  },\n";
    s += std::string("  \"delaySwitch\": ") + (p.delaySwitch ? "true" : "false") + ",\n";
    s += "  \"mod\": [\n";
    for(std::size_t i = 0; i < kModSlots; ++i)
    {
        const ModSlotSettings& m = p.mod[i];
        s += "    { \"shape\": ";
        s += jsonQuote(modShapeName(m.shape));
        s += ", \"target\": ";
        s += jsonQuote(modTargetName(m.target));
        s += ", \"rate\": " + jsonFloat(m.rateNorm);
        s += ", \"depth\": " + jsonFloat(m.depth);
        s += ", \"polarity\": ";
        s += jsonQuote(modPolarityName(m.polarity));
        s += i + 1 < kModSlots ? " },\n" : " }\n";
    }
    s += "  ]\n";
    s += "}\n";
    return s;
}

bool decodePreset(std::string_view text, PresetData& out, PresetError& error) noexcept
{
    try
    {
        if(text.size() > kMaxPresetBytes)
            return setError(error, "too large", "more than 64 KiB");
        JsonValue root;
        JsonError je;
        if(!parseJson(text, root, je))
        {
            if(je.line == 0)
                return setError(error, "syntax error", je.message);
            return setError(error, "syntax error",
                            "line " + std::to_string(je.line) + ", column " + std::to_string(je.column) + ": " +
                                je.message);
        }
        PresetData p;
        if(!decodeTree(root, p, error))
            return false;
        out = p;
        return true;
    }
    catch(...)
    {
        try
        {
            error.reason = "out of memory";
            error.detail = "out of memory";
        }
        catch(...)
        {
        }
        return false;
    }
}

} // namespace audreybrick
