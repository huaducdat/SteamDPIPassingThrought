#pragma once
#include <cstddef>
#include <cstdint>
namespace steamdpi::packet {
struct PacketMetrics final {
  std::uint64_t packetsSeen{};
  std::uint64_t packetsValid{};
  std::uint64_t packetsRejected{};
  std::uint64_t bytesExamined{};
  std::uint64_t transformErrors{};
};
}
