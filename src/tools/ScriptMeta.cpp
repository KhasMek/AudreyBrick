// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "tools/ScriptMeta.h"

#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <vector>

namespace audreybrick::tools
{
namespace
{
bool plainDecimal(const std::string& t)
{
    std::size_t i      = 0;
    const auto  digits = [&]() {
        const std::size_t start = i;
        while(i < t.size() && t[i] >= '0' && t[i] <= '9')
            ++i;
        return i > start;
    };
    if(i < t.size() && (t[i] == '+' || t[i] == '-'))
        ++i;
    if(!digits())
        return false;
    if(i < t.size() && t[i] == '.')
    {
        ++i;
        if(!digits())
            return false;
    }
    if(i < t.size() && (t[i] == 'e' || t[i] == 'E'))
    {
        ++i;
        if(i < t.size() && (t[i] == '+' || t[i] == '-'))
            ++i;
        if(!digits())
            return false;
    }
    return i == t.size();
}
} // namespace

bool parsePlainDecimal(const std::string& token, double& out)
{
    if(!plainDecimal(token))
        return false;
    std::istringstream in(token);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if(in.fail() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

bool isPlainFileName(const std::string& name)
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos
           && name.find('\\') == std::string::npos;
}

bool parseScriptMeta(const std::string& scriptText, ScriptMeta& out, std::string& error)
{
    ScriptMeta         meta;
    bool               haveDuration = false, haveInput = false, haveJump = false, haveClick = false,
                       haveClickNear = false;
    std::istringstream lines(scriptText);
    std::string        raw;
    int                lineNo = 0;
    while(std::getline(lines, raw))
    {
        ++lineNo;
        std::istringstream       fields(raw);
        std::vector<std::string> tok;
        for(std::string t; fields >> t;)
            tok.push_back(t);
        // "# @name value" or "#@name value"
        if(tok.empty() || tok[0][0] != '#')
            continue;
        if(tok[0] == "#")
            tok.erase(tok.begin());
        else
            tok[0].erase(0, 1);
        if(tok.empty() || tok[0].empty() || tok[0][0] != '@')
            continue;

        const std::string where = "line " + std::to_string(lineNo) + ": ";
        const std::string name  = tok[0];
        if(tok.size() != 2)
        {
            error = where + name + " takes exactly one value";
            return false;
        }
        const std::string& value = tok[1];
        if(name == "@duration")
        {
            if(haveDuration || !parsePlainDecimal(value, meta.durationSeconds)
               || meta.durationSeconds <= 0.0 || meta.durationSeconds > 3600.0)
            {
                error = where + (haveDuration ? "duplicate @duration"
                                              : "bad @duration '" + value + "' (seconds, 0 < d <= 3600)");
                return false;
            }
            haveDuration = true;
        }
        else if(name == "@input")
        {
            if(haveInput || !isPlainFileName(value))
            {
                error = where + (haveInput ? "duplicate @input"
                                           : "@input '" + value
                                                 + "' must be a plain file name (no path separator, not . or ..)");
                return false;
            }
            meta.inputFile = value;
            haveInput      = true;
        }
        else if(name == "@jump")
        {
            if(haveJump || !parsePlainDecimal(value, meta.jumpThreshold) || meta.jumpThreshold <= 0.0)
            {
                error = where + (haveJump ? "duplicate @jump" : "bad @jump '" + value + "' (must be > 0)");
                return false;
            }
            haveJump = true;
        }
        else if(name == "@click")
        {
            if(haveClick || !parsePlainDecimal(value, meta.clickThreshold) || meta.clickThreshold <= 0.0)
            {
                error = where + (haveClick ? "duplicate @click" : "bad @click '" + value + "' (must be > 0)");
                return false;
            }
            haveClick = true;
        }
        else if(name == "@click-near")
        {
            if(haveClickNear || !parsePlainDecimal(value, meta.clickNearThreshold) || meta.clickNearThreshold <= 0.0)
            {
                error = where + (haveClickNear ? "duplicate @click-near" : "bad @click-near '" + value + "' (must be > 0)");
                return false;
            }
            haveClickNear = true;
        }
        else
        {
            error = where + "unknown directive " + name;
            return false;
        }
    }
    meta.hasDuration = haveDuration;
    out              = meta;
    return true;
}

bool readScriptMeta(const std::string& path, ScriptMeta& out, std::string& error)
{
    std::ifstream file(path);
    if(!file)
    {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    if(!parseScriptMeta(text.str(), out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace audreybrick::tools
