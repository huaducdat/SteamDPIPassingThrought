#include "packet/PacketFixtureLoader.h"

#include <random>

namespace steamdpi::packet {
namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t p, std::uint16_t v) { b[p] = static_cast<std::uint8_t>(v >> 8); b[p + 1] = static_cast<std::uint8_t>(v); }
std::uint16_t finish(std::uint32_t sum) { while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16); return static_cast<std::uint16_t>(~sum); }
std::uint32_t sumWords(const std::vector<std::uint8_t>& b, std::size_t p, std::size_t n, std::uint32_t sum = 0) {
  for (std::size_t i = 0; i < n; i += 2) sum += (static_cast<std::uint32_t>(b[p + i]) << 8) | (i + 1 < n ? b[p + i + 1] : 0);
  return sum;
}
std::vector<std::uint8_t> ipv4Tcp() {
  std::vector<std::uint8_t> b(43); b[0] = 0x45; put16(b, 2, 43); b[8] = 64; b[9] = 6;
  b[12]=192;b[13]=0;b[14]=2;b[15]=1;b[16]=198;b[17]=51;b[18]=100;b[19]=2;
  put16(b,20,40000);put16(b,22,443);b[32]=0x50;b[33]=0x18;put16(b,34,4096);b[40]='O';b[41]='F';b[42]='F';
  put16(b,10,finish(sumWords(b,0,20)));
  auto s=sumWords(b,12,8);s+=6;s+=23;put16(b,36,finish(sumWords(b,20,23,s)));return b;
}
std::vector<std::uint8_t> ipv6Tcp() {
  std::vector<std::uint8_t> b(63);b[0]=0x60;put16(b,4,23);b[6]=6;b[7]=64;
  b[8]=0x20;b[9]=0x01;b[10]=0x0d;b[11]=0xb8;b[23]=1;b[24]=0x20;b[25]=1;b[26]=0x0d;b[27]=0xb8;b[39]=2;
  put16(b,40,40000);put16(b,42,443);b[52]=0x50;b[53]=0x18;put16(b,54,4096);b[60]='O';b[61]='F';b[62]='F';
  std::uint32_t s=sumWords(b,8,32);s+=23+6;put16(b,56,finish(sumWords(b,40,23,s)));return b;
}
std::vector<std::uint8_t> ipv4Udp() {
  std::vector<std::uint8_t> b(31);b[0]=0x45;put16(b,2,31);b[8]=64;b[9]=17;
  b[12]=192;b[15]=1;b[16]=198;b[19]=2;put16(b,20,53000);put16(b,22,53);put16(b,24,11);b[28]=1;b[29]=2;b[30]=3;
  auto s=sumWords(b,12,8);s+=17+11;put16(b,26,finish(sumWords(b,20,11,s)));put16(b,10,finish(sumWords(b,0,20)));return b;
}
}
PacketBuffer PacketFixtureLoader::load(FixtureKind k, std::uint32_t seed) {
  auto b = k == FixtureKind::ValidIpv6Tcp ? ipv6Tcp() : k == FixtureKind::ValidIpv4Udp ? ipv4Udp() : ipv4Tcp();
  switch(k) {
    case FixtureKind::InvalidIpv4Header: b[0]=0x44; break;
    case FixtureKind::InvalidTcpOffset: b[32]=0x40; break;
    case FixtureKind::Truncated: b.resize(18); break;
    case FixtureKind::InvalidPacketLength: put16(b,2,static_cast<std::uint16_t>(b.size()+100)); break;
    case FixtureKind::InvalidUdpLength: b=ipv4Udp();put16(b,24,10);break;
    case FixtureKind::InvalidChecksum: b[10]^=0x80; break;
    case FixtureKind::Ipv4Fragment: put16(b,6,0x2000);put16(b,10,0);put16(b,10,finish(sumWords(b,0,20)));break;
    case FixtureKind::Ipv6Extension: b=ipv6Tcp();b[6]=0;break;
    case FixtureKind::RandomMalformed: {
      std::mt19937 rng(seed);std::uniform_int_distribution<int> size(0,96), byte(0,255);b.resize(static_cast<std::size_t>(size(rng)));
      for(auto& x:b)x=static_cast<std::uint8_t>(byte(rng));break;
    }
    default: break;
  }
  return PacketBuffer(std::move(b));
}
std::vector<PacketBuffer> PacketFixtureLoader::deterministicCorpus(std::uint32_t seed) {
  std::vector<PacketBuffer> v;v.reserve(12);
  for(auto k:{FixtureKind::ValidIpv4Tcp,FixtureKind::ValidIpv6Tcp,FixtureKind::ValidIpv4Udp,FixtureKind::InvalidIpv4Header,
    FixtureKind::InvalidTcpOffset,FixtureKind::Truncated,FixtureKind::InvalidPacketLength,FixtureKind::InvalidUdpLength,
    FixtureKind::InvalidChecksum,FixtureKind::Ipv4Fragment,FixtureKind::Ipv6Extension,FixtureKind::RandomMalformed})
    v.push_back(load(k,seed));
  return v;
}
std::string_view fixtureName(FixtureKind k) noexcept {
  switch(k){case FixtureKind::ValidIpv4Tcp:return "valid_ipv4_tcp";case FixtureKind::ValidIpv6Tcp:return "valid_ipv6_tcp";case FixtureKind::ValidIpv4Udp:return "valid_udp";
    case FixtureKind::InvalidIpv4Header:return "invalid_ipv4_header";case FixtureKind::InvalidTcpOffset:return "invalid_tcp_offset";case FixtureKind::Truncated:return "truncated";
    case FixtureKind::InvalidPacketLength:return "invalid_packet_length";case FixtureKind::InvalidUdpLength:return "invalid_udp_length";case FixtureKind::InvalidChecksum:return "invalid_checksum";
    case FixtureKind::Ipv4Fragment:return "ipv4_fragment";case FixtureKind::Ipv6Extension:return "ipv6_extension";case FixtureKind::RandomMalformed:return "random_malformed";}return "unknown";
}
}
