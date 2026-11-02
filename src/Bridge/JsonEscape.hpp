#pragma once

#include <string>

namespace Bridge
{
inline std::string JsonEscape(const char* text)
{
    if (!text)
        return {};

    std::string out;
    out.reserve(std::strlen(text) + 8);
    for (const char* cursor = text; *cursor; ++cursor)
    {
        const unsigned char character = static_cast<unsigned char>(*cursor);
        switch (character)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (character < 0x20)
            {
                char unicodeBuf[8];
                std::snprintf(unicodeBuf, sizeof(unicodeBuf), "\\u%04x", character);
                out += unicodeBuf;
            }
            else
            {
                out += static_cast<char>(character);
            }
            break;
        }
    }
    return out;
}

inline std::string JsonEscape(const std::string& text)
{
    return JsonEscape(text.c_str());
}
}
