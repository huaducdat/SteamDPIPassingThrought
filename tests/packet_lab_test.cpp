#include "packet/PacketFixtureLoader.h"
#include "packet/PacketParser.h"
#include "packet/PacketReplayRunner.h"

#include <algorithm>
#include <iostream>
#include <memory>

using namespace steamdpi::packet;
namespace {
bool check(bool v,const char* msg){if(!v)std::cerr<<"FAIL: "<<msg<<"\n";return v;}
class MutationFixture final : public PacketTransform {
 public: TransformationResult apply(const PacketBuffer&) const override { TransformationResult r;r.success=true;r.modified=true;return r; }
};
}
int main(){
  bool ok=true;PacketParser parser;
  const auto tcp4=PacketFixtureLoader::load(FixtureKind::ValidIpv4Tcp);
  const auto tcp6=PacketFixtureLoader::load(FixtureKind::ValidIpv6Tcp);
  const auto udp=PacketFixtureLoader::load(FixtureKind::ValidIpv4Udp);
  const auto a=parser.parse(tcp4),b=parser.parse(tcp6),c=parser.parse(udp);
  ok &= check(a.valid&&a.metadata.ipVersion==IpVersion::V4&&a.metadata.protocol==TransportProtocol::Tcp,"IPv4 TCP parse");
  ok &= check(a.metadata.sourcePort==40000&&a.metadata.destinationPort==443&&a.metadata.tcpSequence==0&&a.metadata.tcpFlags==0x18,"TCP metadata");
  ok &= check(b.valid&&b.metadata.ipVersion==IpVersion::V6&&b.metadata.transportChecksumValid,"IPv6 TCP checksum");
  ok &= check(c.valid&&c.metadata.protocol==TransportProtocol::Udp&&c.metadata.sourcePort==53000,"UDP parse/checksum");

  const std::pair<FixtureKind,ParseError> rejects[]={{FixtureKind::InvalidIpv4Header,ParseError::InvalidHeaderLength},
    {FixtureKind::InvalidTcpOffset,ParseError::InvalidHeaderLength},{FixtureKind::Truncated,ParseError::Truncated},
    {FixtureKind::InvalidPacketLength,ParseError::InvalidLength},{FixtureKind::InvalidUdpLength,ParseError::InvalidLength},
    {FixtureKind::InvalidChecksum,ParseError::InvalidChecksum},{FixtureKind::Ipv4Fragment,ParseError::UnsupportedFragment},
    {FixtureKind::Ipv6Extension,ParseError::UnsupportedExtensionHeader}};
  for(const auto& [kind,error]:rejects){auto r=parser.parse(PacketFixtureLoader::load(kind));ok &= check(!r.valid&&r.error==error,fixtureName(kind).data());}
  PacketParseOptions offloadHints;offloadHints.verifyIpv4HeaderChecksum=false;offloadHints.verifyTransportChecksum=false;
  const auto offloadParse=parser.parse(PacketFixtureLoader::load(FixtureKind::InvalidChecksum),offloadHints);
  ok &= check(offloadParse.valid&&!offloadParse.checksumsVerified,
              "explicit offload hints parse structure without claiming checksum verification");
  auto malformed1=PacketFixtureLoader::load(FixtureKind::RandomMalformed,42),malformed2=PacketFixtureLoader::load(FixtureKind::RandomMalformed,42);
  ok &= check(std::ranges::equal(malformed1.bytes(),malformed2.bytes()),"deterministic malformed fixture seed");

  const auto original=std::vector<std::uint8_t>(tcp4.bytes().begin(),tcp4.bytes().end());
  auto copy=tcp4.mutableCopy();copy.bytes()[40]^=0xff;
  ok &= check(std::ranges::equal(tcp4.bytes(),original),"original packet remains immutable after explicit copy mutation");
  ok &= check(!std::ranges::equal(copy.bytes(),tcp4.bytes()),"mutable copy is independent");
  PacketTransformPipeline noop;ok &= check(noop.add(std::make_unique<NoOpTransform>()),"add no-op transform");
  auto nr=noop.run(tcp4);ok &= check(nr.success&&!nr.modified&&!nr.modifiedCopy,"no-op result does not modify or copy packet");
  ok &= check(std::ranges::equal(tcp4.bytes(),original),"no-op byte-for-byte source equivalence");

  PacketTransformPipeline dry;dry.add(std::make_unique<DryRunTransform>());auto dr=dry.run(tcp6);
  ok &= check(dr.success&&dr.validation.valid,"dry run validates offline packet");
  PacketTransformPipeline validation;validation.add(std::make_unique<ValidationTransform>());
  auto vr=validation.run(PacketFixtureLoader::load(FixtureKind::InvalidChecksum));
  ok &= check(!vr.success&&!vr.error.empty(),"transformation validation error");
  PacketTransformPipeline malicious;malicious.add(std::make_unique<MutationFixture>());
  ok &= check(!malicious.run(tcp4).success,"pipeline fails closed if transform reports mutation");
  for(std::size_t i=1;i<PacketTransformPipeline::kMaxTransforms;++i) ok &= check(noop.add(std::make_unique<NoOpTransform>()),"bounded pipeline add");
  ok &= check(!noop.add(std::make_unique<NoOpTransform>()),"pipeline transform cap");
  noop.clear();ok &= check(noop.size()==0&&noop.add(std::make_unique<NoOpTransform>()),"pipeline clear and reuse lifecycle");

  auto corpus=PacketFixtureLoader::deterministicCorpus(7);PacketTransformPipeline replayPipeline;replayPipeline.add(std::make_unique<DryRunTransform>());
  PacketReplayRunner replay;const auto m1=replay.run(corpus,replayPipeline),m2=replay.run(corpus,replayPipeline);
  ok &= check(m1.packetsSeen==12&&m1.packetsSeen==m2.packetsSeen&&m1.packetsValid==m2.packetsValid&&m1.packetsRejected==m2.packetsRejected&&m1.bytesExamined==m2.bytesExamined,"deterministic replay metrics");
  ok &= check(m1.packetsValid==3&&m1.packetsRejected==9,"fixture replay result counts");
  std::vector<PacketBuffer> tooMany(PacketReplayRunner::kMaxReplayPackets+1,tcp4);
  ok &= check(replay.run(tooMany,replayPipeline).packetsSeen==0,"replay count cap rejects before processing");
  std::vector<std::uint8_t> oversized(kMaxPacketSize+1,0);PacketBuffer tooLarge(std::move(oversized));
  ok &= check(parser.parse(tooLarge).error==ParseError::TooLarge,"oversize input bounded and rejected");
  if(ok)std::cout<<"PASS: offline packet lab fixtures, checksums, immutability, bounds, pipeline, and replay\n";
  return ok?0:1;
}
