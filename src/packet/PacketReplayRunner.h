#pragma once
#include "packet/PacketMetrics.h"
#include "packet/PacketTransformPipeline.h"
#include <span>

namespace steamdpi::packet {
class PacketReplayRunner final {
 public:
  static constexpr std::size_t kMaxReplayPackets = 4096;
  static constexpr std::size_t kMaxReplayBytes = 64 * 1024 * 1024;
  [[nodiscard]] PacketMetrics run(std::span<const PacketBuffer> packets, const PacketTransformPipeline& pipeline) const;
};
}
