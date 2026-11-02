#pragma once

#include <cstdint>
#include <string>

#include "Bridge/CNameUtil.hpp"

namespace Bridge::CustomParam
{
using Bridge::SafeCName;

inline std::string KindFromTypeName(const char* typeName)
{
    std::string kind = typeName ? typeName : "";
    if (kind.rfind("anim", 0) == 0)
        kind = kind.substr(4);
    return kind;
}

struct DynArrayView
{
    void* entries{};
    uint32_t size{};
    uint32_t capacity{};
};
static_assert(sizeof(DynArrayView) == 0x10);
}
