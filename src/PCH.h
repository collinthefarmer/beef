#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logger = SKSE::log;
using namespace std::literals;

#include <atomic>
#include <format>
#include <functional>
#include <mutex>

namespace BetterEnchantmentEffects
{
	extern std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_logRing;
}
