#include "SteamTargets.h"
namespace steamdpi {
const std::vector<SteamTarget>& steamTargets() {
  static const std::vector<SteamTarget> targets{{"store.steampowered.com"}, {"steamcommunity.com"}, {"help.steampowered.com"}, {"api.steampowered.com"}};
  return targets;
}
}
