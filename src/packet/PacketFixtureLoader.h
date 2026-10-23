#pragma once

#include "packet/PacketBuffer.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace steamdpi::packet {
enum class FixtureKind { ValidIpv4Tcp, ValidIpv6Tcp, ValidIpv4Udp, InvalidIpv4Header,
  InvalidTcpOffset, Truncated, InvalidPacketLength, InvalidUdpLength, InvalidChecksum,
  Ipv4Fragment, Ipv6Extension, RandomMalformed };
class PacketFixtureLoader final {
 public:
  [[nodiscard]] static PacketBuffer load(FixtureKind kind, std::uint32_t seed = 0x5eed1234u);
  [[nodiscard]] static std::vector<PacketBuffer> deterministicCorpus(std::uint32_t seed = 0x5eed1234u);
};
[[nodiscard]] std::string_view fixtureName(FixtureKind kind) noexcept;
}
