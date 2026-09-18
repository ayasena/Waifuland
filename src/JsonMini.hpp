/**
 * @brief Minimal string-aware parser for flat JSON objects.
 *
 * Waifuland's IPC messages and config file are flat key/value objects.
 * The previous hand-rolled extractors (substring search for "\"key\"")
 * matched inside string values and could not tell an absent key from a
 * zero value, which made "optional field with a default" unrepresentable.
 * This tokenizer understands quotes and nesting, so has()/getters are
 * exact. No external dependency; C++14, STL only.
 *
 * Wire format is unchanged: this accepts everything the old code accepted
 * (and rejects less — it never throws, parse() just returns false).
 */

#pragma once

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class JsonMini
{
public:
    /// Raw value per key. Quoted strings are stored unescaped;
    /// numbers/booleans/null/objects/arrays are stored as raw source text.
    std::map<std::string, std::string> values;
    /// True when the value was a quoted JSON string.
    std::map<std::string, bool> isString;

    void Clear()
    {
        values.clear();
        isString.clear();
    }

    /**
     * @brief Parse one JSON object.
     * @param text  The object source, e.g. "{\"a\":1}". Leading whitespace
     *              is skipped; trailing garbage after the object is ignored.
     * @return true when a complete {...} object was consumed.
     */
    bool Parse(const std::string& text)
    {
        Clear();
        size_t pos = 0;
        SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != '{') return false;
        return ParseObject(text, pos);
    }

    bool Has(const std::string& key) const
    {
        return values.find(key) != values.end();
    }

    /// String value, or raw text for non-strings (matches legacy behavior
    /// where numbers were also readable as text). Default when absent.
    std::string GetString(const std::string& key, const std::string& def = "") const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        return (it == values.end()) ? def : it->second;
    }

    bool WasString(const std::string& key) const
    {
        std::map<std::string, bool>::const_iterator it = isString.find(key);
        return (it != isString.end()) ? it->second : false;
    }

    float GetFloat(const std::string& key, float def) const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        if (it == values.end() || it->second.empty()) return def;
        char* end = NULL;
        float v = strtof(it->second.c_str(), &end);
        return (end == it->second.c_str()) ? def : v;
    }

    int GetInt(const std::string& key, int def) const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        if (it == values.end() || it->second.empty()) return def;
        char* end = NULL;
        long v = strtol(it->second.c_str(), &end, 10);
        return (end == it->second.c_str()) ? def : (int)v;
    }

    bool GetBool(const std::string& key, bool def) const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        if (it == values.end()) return def;
        const std::string& v = it->second;
        if (v == "true" || v == "1") return true;
        if (v == "false" || v == "0") return false;
        return def;
    }

    /**
     * @brief Extract the quoted strings of a [...] array value.
     * @return true when the key exists and brackets balance (array may be
     *         empty). Non-string elements are skipped, as before.
     */
    bool GetStringArray(const std::string& key, std::vector<std::string>& out) const
    {
        out.clear();
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        if (it == values.end()) return false;
        return SplitStrings(it->second, out);
    }

    /**
     * @brief Split a [...] or {...} sequence into top-level element sources.
     * Used for the config "characters" array (elements are {...} objects).
     * @return true when brackets balance.
     */
    static bool SplitTopLevel(const std::string& text, std::vector<std::string>& out)
    {
        out.clear();
        size_t pos = 0;
        SkipWs(text, pos);
        if (pos >= text.size()) return false;
        char open = text[pos];
        char close = (open == '[') ? ']' : ((open == '{') ? '}' : '\0');
        if (close == '\0') return false;

        int depth = 0;
        bool inStr = false;
        size_t elemStart = std::string::npos;
        for (size_t i = pos; i < text.size(); i++)
        {
            char c = text[i];
            if (inStr)
            {
                if (c == '\\' && i + 1 < text.size()) { i++; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '[' || c == '{')
            {
                if (depth == 0 && c == open) { depth++; continue; }
                if (depth == 1 && elemStart == std::string::npos) elemStart = i;
                depth++;
            }
            else if (c == ']' || c == '}')
            {
                depth--;
                if (depth == 0)
                {
                    if (elemStart != std::string::npos)
                    {
                        out.push_back(TrimCopy(text.substr(elemStart, i - elemStart)));
                        elemStart = std::string::npos;
                    }
                    return c == close;
                }
                if (depth < 1) return false;
            }
            else if (c == ',' && depth == 1)
            {
                if (elemStart != std::string::npos)
                {
                    out.push_back(TrimCopy(text.substr(elemStart, i - elemStart)));
                    elemStart = std::string::npos;
                }
            }
            else if (depth == 1 && elemStart == std::string::npos &&
                     c != ' ' && c != '\t' && c != '\r' && c != '\n')
            {
                elemStart = i;
            }
        }
        return false;
    }

private:
    static void SkipWs(const std::string& s, size_t& pos)
    {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                                  s[pos] == '\r' || s[pos] == '\n')) pos++;
    }

    static std::string TrimCopy(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static void AppendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80)
        {
            out += (char)code;
        }
        else if (code < 0x800)
        {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 0x3F));
        }
        else
        {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
    }

    /// Parse a string starting at the opening quote. On success sets pos
    /// past the closing quote and returns true.
    static bool ParseString(const std::string& s, size_t& pos, std::string& out)
    {
        out.clear();
        if (pos >= s.size() || s[pos] != '"') return false;
        pos++;
        while (pos < s.size())
        {
            char c = s[pos];
            if (c == '"') { pos++; return true; }
            if (c == '\\' && pos + 1 < s.size())
            {
                char e = s[pos + 1];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                    if (pos + 5 < s.size())
                    {
                        char hex[5];
                        memcpy(hex, s.c_str() + pos + 2, 4);
                        hex[4] = '\0';
                        char* end = NULL;
                        unsigned code = (unsigned)strtoul(hex, &end, 16);
                        if (end != hex && *end == '\0')
                        {
                            AppendUtf8(out, code);
                            pos += 6;
                            continue;
                        }
                    }
                    out += "\\u";
                    pos += 2;
                    continue;
                default: out += e; break;
                }
                pos += 2;
                continue;
            }
            out += c;
            pos++;
        }
        return false;
    }

    /// Capture a balanced {...} or [...] value starting at pos. On success
    /// sets pos past the closing bracket and returns true.
    static bool CaptureBalanced(const std::string& s, size_t& pos, std::string& out)
    {
        size_t start = pos;
        char open = s[pos];
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool inStr = false;
        for (; pos < s.size(); pos++)
        {
            char c = s[pos];
            if (inStr)
            {
                if (c == '\\' && pos + 1 < s.size()) { pos++; continue; }
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == open) depth++;
            else if (c == close)
            {
                depth--;
                if (depth == 0)
                {
                    out = s.substr(start, pos - start + 1);
                    pos++;
                    return true;
                }
            }
        }
        return false;
    }

    static bool SplitStrings(const std::string& text, std::vector<std::string>& out)
    {
        out.clear();
        size_t pos = 0;
        SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != '[') return false;
        pos++;
        while (true)
        {
            SkipWs(text, pos);
            if (pos >= text.size()) return false;
            if (text[pos] == ']') { pos++; return true; }
            if (text[pos] == '"')
            {
                std::string v;
                if (!ParseString(text, pos, v)) return false;
                out.push_back(v);
            }
            else
            {
                // Skip non-string elements (numbers, nested values).
                if (text[pos] == '{' || text[pos] == '[')
                {
                    std::string ignored;
                    if (!CaptureBalanced(text, pos, ignored)) return false;
                }
                else
                {
                    while (pos < text.size() && text[pos] != ',' && text[pos] != ']') pos++;
                }
            }
            SkipWs(text, pos);
            if (pos < text.size() && text[pos] == ',') { pos++; continue; }
            if (pos < text.size() && text[pos] == ']') { pos++; return true; }
            return false;
        }
    }

    bool ParseObject(const std::string& s, size_t& pos)
    {
        pos++; // consume '{'
        while (true)
        {
            SkipWs(s, pos);
            if (pos >= s.size()) return false;
            if (s[pos] == '}') { pos++; return true; }

            std::string key;
            if (!ParseString(s, pos, key)) return false;
            SkipWs(s, pos);
            if (pos >= s.size() || s[pos] != ':') return false;
            pos++;
            SkipWs(s, pos);
            if (pos >= s.size()) return false;

            std::string value;
            bool str = false;
            if (s[pos] == '"')
            {
                if (!ParseString(s, pos, value)) return false;
                str = true;
            }
            else if (s[pos] == '{' || s[pos] == '[')
            {
                if (!CaptureBalanced(s, pos, value)) return false;
            }
            else
            {
                size_t start = pos;
                while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']') pos++;
                value = TrimCopy(s.substr(start, pos - start));
                if (value.empty()) return false;
            }

            values[key] = value;
            isString[key] = str;

            SkipWs(s, pos);
            if (pos >= s.size()) return false;
            if (s[pos] == ',') { pos++; continue; }
            if (s[pos] == '}') { pos++; return true; }
            return false;
        }
    }
};
