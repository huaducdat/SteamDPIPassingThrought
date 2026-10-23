#include "packet/PacketParser.h"

#include <algorithm>
#include <limits>

namespace steamdpi::packet {
namespace {
std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[p]) << 8) | b[p + 1]);
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t p) {
  return (static_cast<std::uint32_t>(b[p]) << 24) | (static_cast<std::uint32_t>(b[p + 1]) << 16) |
         (static_cast<std::uint32_t>(b[p + 2]) << 8) | b[p + 3];
}
std::uint32_t addWords(std::span<const std::uint8_t> b, std::uint32_t sum = 0) {
  std::size_t i = 0;
  for (; i + 1 < b.size(); i += 2) sum += (static_cast<std::uint32_t>(b[i]) << 8) | b[i + 1];
  if (i < b.size()) sum += static_cast<std::uint32_t>(b[i]) << 8;
  return sum;
}
bool checksumValid(std::uint32_t sum) {
  while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
  return static_cast<std::uint16_t>(sum) == 0xffffu;
}
PacketValidation fail(ParseError e, const char* message) {
  PacketValidation out; out.error = e; out.detail = message; return out;
}
bool knownExtension(std::uint8_t n) {
  return n == 0 || n == 43 || n == 44 || n == 50 || n == 51 || n == 60 || n == 135 || n == 139 || n == 140;
}
}

PacketBuffer::PacketBuffer(std::span<const std::uint8_t> bytes) {
  exceedsLimit_ = bytes.size() > kMaxPacketSize;
  if (!exceedsLimit_) bytes_.assign(bytes.begin(), bytes.end());
}
PacketBuffer::PacketBuffer(std::vector<std::uint8_t> bytes) {
  exceedsLimit_ = bytes.size() > kMaxPacketSize;
  if (!exceedsLimit_) bytes_ = std::move(bytes);
}

PacketValidation PacketParser::parse(const PacketBuffer& packet,PacketParseOptions options) const {
  const auto b = packet.bytes();
  if (packet.exceedsLimit()) return fail(ParseError::TooLarge, "packet exceeds configured maximum");
  if (b.empty()) return fail(ParseError::Empty, "empty packet");
  if (b.size() > kMaxPacketSize) return fail(ParseError::TooLarge, "packet exceeds configured maximum");
  const auto version = static_cast<std::uint8_t>(b[0] >> 4);
  std::size_t ipLen = 0, totalLen = 0, transport = 0;
  std::uint8_t next = 0;
  PacketMetadata m{};
  if (version == 4) {
    if (b.size() < 20) return fail(ParseError::Truncated, "truncated IPv4 header");
    ipLen = static_cast<std::size_t>(b[0] & 0x0f) * 4;
    totalLen = be16(b, 2);
    if (ipLen < 20 || ipLen > 60 || ipLen > b.size()) return fail(ParseError::InvalidHeaderLength, "invalid IPv4 IHL");
    if (totalLen < ipLen || totalLen > b.size()) return fail(ParseError::InvalidLength, "IPv4 total length is inconsistent with buffer");
    const auto fragment = be16(b, 6);
    if ((fragment & 0x3fffu) != 0) return fail(ParseError::UnsupportedFragment, "IPv4 fragmentation is unsupported in V1");
    if (options.verifyIpv4HeaderChecksum&&!checksumValid(addWords(b.first(ipLen)))) return fail(ParseError::InvalidChecksum, "invalid IPv4 header checksum");
    m.ipVersion = IpVersion::V4;
    std::copy_n(b.begin() + 12, 4, m.sourceAddress.begin());
    std::copy_n(b.begin() + 16, 4, m.destinationAddress.begin());
    next = b[9]; transport = ipLen;
    m.ipv4ChecksumValid = options.verifyIpv4HeaderChecksum;
  } else if (version == 6) {
    if (b.size() < 40) return fail(ParseError::Truncated, "truncated IPv6 header");
    const auto payload = be16(b, 4);
    if (payload == 0) return fail(ParseError::InvalidLength, "IPv6 jumbograms are unsupported");
    totalLen = 40u + payload;
    if (totalLen > b.size()) return fail(ParseError::InvalidLength, "IPv6 payload length exceeds buffer");
    next = b[6]; transport = 40;
    m.ipVersion = IpVersion::V6;
    std::copy_n(b.begin() + 8, 16, m.sourceAddress.begin());
    std::copy_n(b.begin() + 24, 16, m.destinationAddress.begin());
    if (knownExtension(next)) {
      if (next == 44) return fail(ParseError::UnsupportedFragment, "IPv6 fragment header is unsupported in V1");
      return fail(ParseError::UnsupportedExtensionHeader, "IPv6 extension headers are unsupported in V1");
    }
  } else {
    return fail(ParseError::InvalidVersion, "unsupported IP version");
  }
  if (next != static_cast<std::uint8_t>(TransportProtocol::Tcp) && next != static_cast<std::uint8_t>(TransportProtocol::Udp))
    return fail(ParseError::UnsupportedProtocol, "only TCP and UDP are supported");
  m.protocol = static_cast<TransportProtocol>(next);
  m.packetLength = static_cast<std::uint32_t>(totalLen);
  m.ipHeaderLength = static_cast<std::uint16_t>(ipLen == 0 ? 40 : ipLen);
  const auto end = totalLen;
  if (next == static_cast<std::uint8_t>(TransportProtocol::Tcp)) {
    if (end - transport < 20) return fail(ParseError::Truncated, "truncated TCP header");
    const auto tcpLen = static_cast<std::size_t>(b[transport + 12] >> 4) * 4;
    if (tcpLen < 20 || tcpLen > 60 || tcpLen > end - transport) return fail(ParseError::InvalidHeaderLength, "invalid TCP data offset");
    m.transportHeaderLength = static_cast<std::uint16_t>(tcpLen);
    m.sourcePort = be16(b, transport); m.destinationPort = be16(b, transport + 2);
    m.tcpSequence = be32(b, transport + 4); m.tcpAcknowledgment = be32(b, transport + 8);
    m.tcpFlags = b[transport + 13];
    std::uint32_t sum = 0;
    if (m.ipVersion == IpVersion::V4) {
      sum = addWords(b.subspan(12, 8));
      sum += next; sum += static_cast<std::uint16_t>(end - transport);
    } else {
      sum = addWords(b.subspan(8, 32));
      const auto len = static_cast<std::uint32_t>(end - transport);
      sum += (len >> 16) + (len & 0xffffu) + next;
    }
    m.transportChecksumValid = options.verifyTransportChecksum&&checksumValid(addWords(b.subspan(transport, end - transport), sum));
    if(!options.verifyTransportChecksum) m.transportChecksumValid=false;
  } else {
    if (end - transport < 8) return fail(ParseError::Truncated, "truncated UDP header");
    const auto udpLen = be16(b, transport + 4);
    if (udpLen < 8 || udpLen != end - transport) return fail(ParseError::InvalidLength, "UDP length does not match IP payload");
    m.transportHeaderLength = 8;
    m.sourcePort = be16(b, transport); m.destinationPort = be16(b, transport + 2);
    const auto checksum = be16(b, transport + 6);
    if (m.ipVersion == IpVersion::V4 && checksum == 0) m.transportChecksumValid = true;
    else if(!options.verifyTransportChecksum) m.transportChecksumValid=false;
    else {
      std::uint32_t sum = 0;
      if (m.ipVersion == IpVersion::V4) {
        sum = addWords(b.subspan(12, 8)); sum += next; sum += udpLen;
      } else {
        sum = addWords(b.subspan(8, 32)); sum += udpLen + next;
      }
      m.transportChecksumValid = checksumValid(addWords(b.subspan(transport, udpLen), sum));
    }
  }
  if (options.verifyTransportChecksum&&!m.transportChecksumValid) return fail(ParseError::InvalidChecksum, "invalid transport checksum");
  PacketValidation result; result.valid = true; result.checksumsVerified=options.verifyTransportChecksum&&(m.ipVersion==IpVersion::V6||options.verifyIpv4HeaderChecksum); result.metadata = m; return result;
}

PacketValidation PacketValidator::validate(const PacketBuffer& packet) const { return PacketParser{}.parse(packet); }

}
