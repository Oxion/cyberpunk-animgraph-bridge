#pragma once

#include <functional>
#include <string>

#include <RED4ext/RED4ext.hpp>

namespace Bridge
{
using FeaturePropFn = std::function<void(const std::string& property, const std::string& type, const std::string& valueJson)>;

/** Walk AnimFeature RTTI props (excluding IScriptable base) → typed JSON value fragments. */
void ForEachAnimFeatureProperty(RED4ext::IScriptable* feature, const FeaturePropFn& fn);
}
