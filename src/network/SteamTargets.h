#pragma once
#include <string_view>
#include <vector>
namespace steamdpi {
struct SteamTarget { std::string_view host; };
const std::vector<SteamTarget>& steamTargets();
}
