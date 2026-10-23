#include "packet/PacketReplayRunner.h"

namespace steamdpi::packet {
PacketMetrics PacketReplayRunner::run(std::span<const PacketBuffer> packets, const PacketTransformPipeline& pipeline) const {
  PacketMetrics m;
  if (packets.size() > kMaxReplayPackets) { m.transformErrors = 1; return m; }
  for (const auto& packet : packets) {
    if (packet.size() > kMaxReplayBytes - m.bytesExamined) { m.transformErrors = 1; break; }
    ++m.packetsSeen; m.bytesExamined += packet.size();
    const auto result = pipeline.run(packet);
    if (!result.success) { ++m.packetsRejected; ++m.transformErrors; }
    else if (result.validation.valid) ++m.packetsValid;
    else ++m.packetsRejected;
  }
  return m;
}
}
