#pragma once

#include <RED4ext/CName.hpp>

namespace Bridge
{
inline const char* SafeCName(RED4ext::CName name)
{
    const char* text = name.ToString();
    return text ? text : "";
}
}
