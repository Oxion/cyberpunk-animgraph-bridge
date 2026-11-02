#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <semver.hpp>

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/entAnimationControllerComponent.hpp>
#include <RED4ext/Scripting/Natives/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Vector4.hpp>

#include "App/Project.hpp"
