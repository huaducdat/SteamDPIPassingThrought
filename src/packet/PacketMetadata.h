#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace steamdpi::packet {

enum class IpVersion : std::uint8_t { V4 = 4, V6 = 6 };
enum class TransportProtocol : std::uint8_t { Tcp = 6, Udp = 17 };

struct PacketMetadata final {
  IpVersion ipVersion{};
  TransportProtocol protocol{};
  std::array<std::uint8_t, 16> sourceAddress{};
  std::array<std::uint8_t, 16> destinationAddress{};
  std::uint16_t sourcePort{};
  std::uint16_t destinationPort{};
  std::uint32_t packetLength{};
  std::uint16_t ipHeaderLength{};
  std::uint16_t transportHeaderLength{};
  std::uint8_t tcpFlags{};
  std::uint32_t tcpSequence{};
  std::uint32_t tcpAcknowledgment{};
  bool ipv4ChecksumValid{true};
  bool transportChecksumValid{};
};

enum class ParseError {
  None,
  Empty,
  TooLarge,
  Truncated,
  InvalidVersion,
  InvalidLength,
  InvalidHeaderLength,
  UnsupportedProtocol,
  UnsupportedFragment,
  UnsupportedExtensionHeader,
  InvalidChecksum,
};

struct PacketValidation final {
  bool valid{};
  bool checksumsVerified{true};
  ParseError error{ParseError::None};
  PacketMetadata metadata{};
  std::string detail;
};

}
