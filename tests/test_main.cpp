#include "diagnostics/DiagnosticService.h"
#include "capture/BoundedEventQueue.h"
#include "capture/CaptureController.h"
#include "capture/FlowTracker.h"
#include "capture/ProcessIdentityResolver.h"
#include "capture/CaptureAdapter.h"
#include "capture/AddressNormalization.h"
#include "diagnostics/DnsProbe.h"
#include "diagnostics/TcpProbe.h"
#include "diagnostics/TlsProbe.h"
#include "network/SteamTargets.h"
#include "packet/PacketFixtureLoader.h"
#include "packet/PacketParser.h"
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
unsigned short freeLoopbackPort(SOCKET& bound) {
  bound=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(bound==INVALID_SOCKET)return 0;
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
  if(bind(bound,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR)return 0;
  int size=sizeof(address);if(getsockname(bound,reinterpret_cast<sockaddr*>(&address),&size)==SOCKET_ERROR)return 0;
  return ntohs(address.sin_port);
}
bool require(bool ok,const char* message){if(!ok)std::cerr<<message<<"\n";return ok;}

class FakeCaptureAdapter final : public steamdpi::CaptureAdapter {
 public:
  bool failNext = false;
  std::uint32_t fakeStopError = 0;
  int starts = 0;
  int stops = 0;
  std::shared_ptr<std::atomic_int> externalStops;
  EventCallback onEvent;
  ErrorCallback onError;
  bool start(EventCallback event, ErrorCallback error, std::string& message) override {
    ++starts;
    if (failNext) { failNext = false; message = "fixture open failure"; return false; }
    onEvent = std::move(event); onError = std::move(error); return true;
  }
  void stop() override { ++stops;if(externalStops)++*externalStops; }
  std::uint32_t lastStopError() const override { return fakeStopError; }
};

class TlsStallServer {
 public:
  TlsStallServer() {
    port_ = freeLoopbackPort(listener_);
    acceptedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    releaseEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    valid_ = listener_ != INVALID_SOCKET && port_ != 0 && acceptedEvent_ && releaseEvent_ &&
             listen(listener_, 1) != SOCKET_ERROR;
    if (!valid_) return;
    thread_ = std::thread([this] {
      const SOCKET client = accept(listener_, nullptr, nullptr);
      if (client == INVALID_SOCKET) return;
      SetEvent(acceptedEvent_);
      WaitForSingleObject(releaseEvent_, 10000);
      shutdown(client, SD_BOTH);
      closesocket(client);
    });
  }

  ~TlsStallServer() {
    if (releaseEvent_) SetEvent(releaseEvent_);
    if (thread_.joinable()) thread_.join();
    if (listener_ != INVALID_SOCKET) closesocket(listener_);
    if (acceptedEvent_) CloseHandle(acceptedEvent_);
    if (releaseEvent_) CloseHandle(releaseEvent_);
  }

  bool valid() const { return valid_; }
  unsigned short port() const { return port_; }
  bool waitForClient(DWORD timeoutMs) const {
    return WaitForSingleObject(acceptedEvent_, timeoutMs) == WAIT_OBJECT_0;
  }

 private:
  SOCKET listener_ = INVALID_SOCKET;
  unsigned short port_ = 0;
  HANDLE acceptedEvent_ = nullptr;
  HANDLE releaseEvent_ = nullptr;
  std::thread thread_;
  bool valid_ = false;
};
}

int main(){
  WSADATA wsa{};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return 1;
  bool ok=true;
  ok &= require(!steamdpi::steamTargets().empty(),"Steam target registry is empty");
  for(const auto& target:steamdpi::steamTargets())ok &= require(target.host.find('/')==std::string_view::npos,"Registry contains non-host target");

  std::atomic_bool cancel{false};
  auto dnsOk=steamdpi::probeDns("localhost",1500,cancel);
  if(dnsOk.status!=steamdpi::ProbeStatus::Success)std::cerr<<"DNS fixture: "<<steamdpi::probeStatusName(dnsOk.status)<<" / "<<dnsOk.detail<<"\n";
  ok &= require(dnsOk.status==steamdpi::ProbeStatus::Success,"localhost DNS A/AAAA resolution failed");
  auto dnsFail=steamdpi::probeDns("bad host name",1500,cancel);
  ok &= require(dnsFail.status==steamdpi::ProbeStatus::Failure,"Malformed hostname should fail DNS resolution");
  auto dnsNxDomain=steamdpi::probeDns("steamdpiphase11-fixture.invalid",3000,cancel);
  ok &= require(dnsNxDomain.status==steamdpi::ProbeStatus::Failure||dnsNxDomain.status==steamdpi::ProbeStatus::Timeout,"Reserved .invalid hostname did not produce a DNS failure or timeout");

  SOCKET listener=INVALID_SOCKET;const auto port=freeLoopbackPort(listener);
  if(!port||listen(listener,1)==SOCKET_ERROR){std::cerr<<"Unable to start TCP fixture\n";WSACleanup();return 1;}
  std::thread acceptThread([&]{SOCKET client=accept(listener,nullptr,nullptr);if(client!=INVALID_SOCKET)closesocket(client);});
  cancel=false;auto connected=steamdpi::probeTcp("127.0.0.1",1500,cancel,port);
  acceptThread.join();closesocket(listener);
  ok &= require(connected.status==steamdpi::ProbeStatus::Success,"TCP local fixture did not connect");

  SOCKET reserved=INVALID_SOCKET;const auto refusedPort=freeLoopbackPort(reserved);if(reserved!=INVALID_SOCKET)closesocket(reserved);
  cancel=false;auto refused=steamdpi::probeTcp("127.0.0.1",5000,cancel,refusedPort);
  if(refused.status!=steamdpi::ProbeStatus::Failure||refused.detail.find("refused")==std::string::npos)std::cerr<<"TCP refused fixture: "<<steamdpi::probeStatusName(refused.status)<<" / "<<refused.detail<<" port="<<refusedPort<<"\n";
  ok &= require(refused.status==steamdpi::ProbeStatus::Failure&&refused.detail.find("refused")!=std::string::npos,"TCP refused fixture was not classified");
  cancel=false;auto expired=steamdpi::probeTcp("127.0.0.1",0,cancel,refusedPort);
  ok &= require(expired.status==steamdpi::ProbeStatus::Timeout,"Expired TCP deadline was not classified as timeout");

  cancel=true;auto cancelled=steamdpi::probeTcp("127.0.0.1",1500,cancel,port);
  ok &= require(cancelled.status==steamdpi::ProbeStatus::Cancelled,"Pre-cancelled TCP probe did not cancel");
  steamdpi::DiagnosticService service;cancel=false;
  auto partial=service.run({"localhost"},1500,1,cancel,[&](const steamdpi::ProbeResult&){cancel=true;});
  ok &= require(cancel&&partial.results.size()==1,"Orchestrator did not stop between stages after cancellation");

  {
    TlsStallServer tlsServer;
    ok &= require(tlsServer.valid(),"Unable to start TLS stall fixture");
    if (tlsServer.valid()) {
      std::atomic_bool tlsCancel{false};
      steamdpi::ProbeResult tlsResult;
      std::chrono::steady_clock::time_point cancellationRequested;
      std::thread tlsWorker([&] { tlsResult = steamdpi::probeTls("127.0.0.1", 5000, tlsCancel, tlsServer.port()); });
      const bool acceptedClient = tlsServer.waitForClient(3000);
      if (acceptedClient) {
        cancellationRequested = std::chrono::steady_clock::now();
        tlsCancel = true;
      }
      tlsWorker.join();
      const auto cancelLatency = acceptedClient ? std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - cancellationRequested).count() : 0;
      ok &= require(acceptedClient,"TLS cancellation fixture did not accept a client");
      ok &= require(tlsResult.status==steamdpi::ProbeStatus::Cancelled,
                    "In-flight TLS cancellation was not reported as cancelled");
      ok &= require(cancelLatency<5000,"In-flight TLS cancellation exceeded the configured probe timeout");
    }
  }

  {
    TlsStallServer tlsServer;
    ok &= require(tlsServer.valid(),"Unable to start TLS timeout fixture");
    if (tlsServer.valid()) {
      std::atomic_bool noCancel{false};
      const auto started = std::chrono::steady_clock::now();
      const auto tlsResult = steamdpi::probeTls("127.0.0.1", 500, noCancel, tlsServer.port());
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      ok &= require(tlsServer.waitForClient(100),"TLS timeout fixture did not accept a client");
      ok &= require(tlsResult.status==steamdpi::ProbeStatus::Timeout,
                    "TLS stall fixture was not classified as timeout");
      const auto timeoutMessage = std::string("TLS stall fixture exceeded its bounded timeout: ") +
                                  std::to_string(elapsed) + " ms";
      ok &= require(elapsed<5000,timeoutMessage.c_str());
    }
  }

  WSACleanup();
  {
    const std::vector<std::wstring> roots{L"C:\\Program Files (x86)\\Steam"};
    const auto normalizedIpv4=steamdpi::canonicalWinDivertAddress({0,0,0x0000ffffu,0xc0000201u});
    ok &= require(normalizedIpv4[10]==0xff&&normalizedIpv4[11]==0xff&&
                  normalizedIpv4[12]==192&&normalizedIpv4[13]==0&&
                  normalizedIpv4[14]==2&&normalizedIpv4[15]==1,
                  "WinDivert IPv4-mapped FLOW address words were not normalized to canonical network bytes");
    const auto normalizedIpv6=steamdpi::canonicalWinDivertAddress({0x20010db8u,0,0,1});
    ok &= require(normalizedIpv6[0]==0x20&&normalizedIpv6[1]==0x01&&
                  normalizedIpv6[2]==0x0d&&normalizedIpv6[3]==0xb8&&normalizedIpv6[15]==1,
                  "WinDivert IPv6 FLOW address words were not normalized to canonical network bytes");
    ok &= require(steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Program Files (x86)\\Steam\\steam.exe",roots,true),
        "Verified Steam client path was rejected");
    ok &= require(steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe",roots,true),
        "Verified Steam WebHelper path was rejected");
    ok &= require(!steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Program Files (x86)\\Steam\\steam.exe",roots,false),
        "Unsigned Steam-named executable was accepted");
    ok &= require(!steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Temp\\steam.exe",roots,true),"Steam executable outside registered root was accepted");
    ok &= require(!steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Program Files (x86)\\Steam\\steam.exe.bak",roots,true),
        "Non-executable Steam lookalike was accepted");
    ok &= require(!steamdpi::ProcessIdentityResolver::isVerifiedSteamImagePath(
        L"C:\\Program Files (x86)\\Steam\\GameOverlayUI.exe",roots,true),
        "Untracked Steam executable was attributed as steam.exe or steamwebhelper.exe");

    steamdpi::FlowTracker tracker;
    const steamdpi::ProcessIdentity identity{42,L"C:\\Program Files (x86)\\Steam\\steam.exe",true};
    steamdpi::CaptureEvent socket{};socket.layer=steamdpi::CaptureLayer::Socket;socket.kind=steamdpi::CaptureEventKind::Connect;socket.endpointId=100;socket.parentEndpointId=7;socket.processId=42;socket.tuple.protocol=6;
    steamdpi::CaptureEvent flow=socket;flow.layer=steamdpi::CaptureLayer::Flow;flow.kind=steamdpi::CaptureEventKind::Established;
    tracker.process(socket,identity);tracker.process(flow,identity);
    auto metrics=tracker.snapshot();
    ok &= require(metrics.steamFlowsObserved==1&&metrics.steamExeFlows==1&&
                  metrics.tcpFlows==1&&metrics.ipv4Flows==1,
                  "Correlated IPv4 TCP Steam flow was not counted");
    steamdpi::CaptureEvent udp=flow;udp.endpointId=101;udp.tuple.protocol=17;udp.tuple.ipv6=true;
    steamdpi::CaptureEvent udpSocket=udp;udpSocket.layer=steamdpi::CaptureLayer::Socket;udpSocket.kind=steamdpi::CaptureEventKind::Accept;
    tracker.process(udp,identity);tracker.process(udpSocket,identity);
    metrics=tracker.snapshot();
    ok &= require(metrics.steamFlowsObserved==2&&metrics.steamExeFlows==2&&
                  metrics.udpFlows==1&&metrics.ipv6Flows==1,
                  "Correlated IPv6 UDP Steam flow was not counted");

    const steamdpi::ProcessIdentity webHelper{43,L"C:\\Program Files (x86)\\Steam\\bin\\cef\\cef.win64\\steamwebhelper.exe",true,333};
    auto webFlow=flow;webFlow.endpointId=104;webFlow.processId=43;
    auto webSocket=webFlow;webSocket.layer=steamdpi::CaptureLayer::Socket;
    webSocket.kind=steamdpi::CaptureEventKind::Connect;
    tracker.process(webSocket,webHelper);tracker.process(webFlow,webHelper);
    metrics=tracker.snapshot();
    ok &= require(metrics.steamFlowsObserved==3&&metrics.steamWebHelperFlows==1&&
                  metrics.steamExeFlows==2,
                  "Steam WebHelper attribution counter did not increment independently");

    auto unknownFlow=flow;unknownFlow.endpointId=105;unknownFlow.processId=9999;
    auto unknownSocket=unknownFlow;unknownSocket.layer=steamdpi::CaptureLayer::Socket;
    unknownSocket.kind=steamdpi::CaptureEventKind::Connect;
    tracker.process(unknownFlow,std::nullopt);tracker.process(unknownSocket,std::nullopt);
    metrics=tracker.snapshot();
    ok &= require(metrics.steamFlowsObserved==3&&metrics.unknownOrAmbiguousFlows==1,
                  "Unknown PID was not isolated from Steam counters");

    steamdpi::CaptureEvent ambiguous=flow;ambiguous.endpointId=102;ambiguous.tuple.localPort=1;
    steamdpi::CaptureEvent ambiguousSocket=ambiguous;ambiguousSocket.layer=steamdpi::CaptureLayer::Socket;ambiguousSocket.kind=steamdpi::CaptureEventKind::Connect;ambiguousSocket.tuple.localPort=2;
    tracker.process(ambiguous,identity);tracker.process(ambiguousSocket,identity);
    steamdpi::CaptureEvent reusedFlow=flow;reusedFlow.endpointId=103;
    steamdpi::CaptureEvent reusedSocket=reusedFlow;reusedSocket.layer=steamdpi::CaptureLayer::Socket;reusedSocket.kind=steamdpi::CaptureEventKind::Connect;
    auto firstProcessInstance=identity;firstProcessInstance.processCreationTime=111;
    auto reusedProcessInstance=identity;reusedProcessInstance.processCreationTime=222;
    tracker.process(reusedFlow,firstProcessInstance);tracker.process(reusedSocket,reusedProcessInstance);
    // A late FLOW/SOCKET pair after PID termination has no live identity and stays unattributed.
    auto terminatingFlow=flow;terminatingFlow.endpointId=106;
    auto terminatingSocket=terminatingFlow;terminatingSocket.layer=steamdpi::CaptureLayer::Socket;
    terminatingSocket.kind=steamdpi::CaptureEventKind::Connect;
    tracker.process(terminatingFlow,identity);tracker.process(terminatingSocket,std::nullopt);
    metrics=tracker.snapshot();
    ok &= require(metrics.steamFlowsObserved==3&&metrics.unknownOrAmbiguousFlows==4,
                  "Ambiguous or post-termination flows were not kept out of Steam counters");

    steamdpi::FlowTracker networkTracker;
    auto fixture=steamdpi::packet::PacketFixtureLoader::load(steamdpi::packet::FixtureKind::ValidIpv4Tcp);
    const auto packetMetadata=steamdpi::packet::PacketParser{}.parse(fixture).metadata;
#ifdef _DEBUG
    std::cout << "Synthetic canonical endpoint fixtures: IPv4 ::ffff:c000:0201 -> ::ffff:c633:6402; "
                 "IPv6 2001:db8::1 -> 2001:db8::2\n";
#endif
    auto makeTuple=[](const steamdpi::packet::PacketMetadata& metadata){steamdpi::FlowTuple tuple{};tuple.protocol=static_cast<std::uint8_t>(metadata.protocol);tuple.localPort=metadata.sourcePort;tuple.remotePort=metadata.destinationPort;
      tuple.ipv6=metadata.ipVersion==steamdpi::packet::IpVersion::V6;
      if(tuple.ipv6){tuple.localAddress=metadata.sourceAddress;tuple.remoteAddress=metadata.destinationAddress;}
      else{tuple.localAddress[10]=tuple.remoteAddress[10]=0xff;tuple.localAddress[11]=tuple.remoteAddress[11]=0xff;
        std::copy_n(metadata.sourceAddress.begin(),4,tuple.localAddress.begin()+12);std::copy_n(metadata.destinationAddress.begin(),4,tuple.remoteAddress.begin()+12);}return tuple;};
    auto networkSocket=socket;networkSocket.endpointId=200;networkSocket.tuple=makeTuple(packetMetadata);networkSocket.timestampNs=1000000000ull;
    auto networkFlow=networkSocket;networkFlow.layer=steamdpi::CaptureLayer::Flow;networkFlow.kind=steamdpi::CaptureEventKind::Established;
    networkTracker.process(networkSocket,identity);networkTracker.process(networkFlow,identity);
    ok &= require(networkTracker.matchNetworkPacket(packetMetadata,true,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "Validated outbound NETWORK packet failed exact Steam flow correlation");
    const auto endpointCandidates=networkTracker.diagnoseNetworkPacket(packetMetadata,true,1100000000ull);
    ok &= require(endpointCandidates.steamLocalAddressCandidates>0&&
                  endpointCandidates.steamRemoteAddressCandidates>0&&
                  endpointCandidates.steamAddressCandidates>0,
                  "Steam endpoint-side address diagnostics did not identify an exact fixture tuple");
    ok &= require(networkTracker.matchNetworkPacket(packetMetadata,false,1100000000ull)==steamdpi::NetworkFlowMatch::Unmatched,
                  "Inbound packet direction was not included in exact flow matching");
    const auto ipv6Packet=steamdpi::packet::PacketParser{}.parse(steamdpi::packet::PacketFixtureLoader::load(steamdpi::packet::FixtureKind::ValidIpv6Tcp)).metadata;
    auto ipv6Socket=networkSocket;ipv6Socket.endpointId=204;ipv6Socket.tuple=makeTuple(ipv6Packet);
    auto ipv6Flow=ipv6Socket;ipv6Flow.layer=steamdpi::CaptureLayer::Flow;ipv6Flow.kind=steamdpi::CaptureEventKind::Established;
    networkTracker.process(ipv6Socket,identity);networkTracker.process(ipv6Flow,identity);
    ok &= require(networkTracker.matchNetworkPacket(ipv6Packet,true,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "Validated IPv6 NETWORK packet failed exact Steam flow correlation");
    auto inbound4=packetMetadata;
    std::swap(inbound4.sourceAddress,inbound4.destinationAddress);
    std::swap(inbound4.sourcePort,inbound4.destinationPort);
    ok &= require(networkTracker.matchNetworkPacket(inbound4,false,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "Reversed IPv4 TCP inbound tuple failed exact correlation");
    auto inbound6=ipv6Packet;
    std::swap(inbound6.sourceAddress,inbound6.destinationAddress);
    std::swap(inbound6.sourcePort,inbound6.destinationPort);
    ok &= require(networkTracker.matchNetworkPacket(inbound6,false,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "Reversed IPv6 TCP inbound tuple failed exact correlation");
    const auto udp4=steamdpi::packet::PacketParser{}.parse(
        steamdpi::packet::PacketFixtureLoader::load(steamdpi::packet::FixtureKind::ValidIpv4Udp)).metadata;
    auto udpTuple=makeTuple(udp4);udpTuple.protocol=17;
    auto networkUdpSocket=networkSocket;networkUdpSocket.endpointId=205;networkUdpSocket.tuple=udpTuple;
    auto udpFlow=networkUdpSocket;udpFlow.layer=steamdpi::CaptureLayer::Flow;udpFlow.kind=steamdpi::CaptureEventKind::Established;
    networkTracker.process(udpFlow,identity);networkTracker.process(networkUdpSocket,identity);
    ok &= require(networkTracker.matchNetworkPacket(udp4,true,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "IPv4 UDP outbound packet failed correlation when FLOW arrived before SOCKET");
    auto udp4in=udp4;std::swap(udp4in.sourceAddress,udp4in.destinationAddress);std::swap(udp4in.sourcePort,udp4in.destinationPort);
    ok &= require(networkTracker.matchNetworkPacket(udp4in,false,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "IPv4 UDP inbound packet failed reversed endpoint correlation");
    const auto ipv6TcpFixture=steamdpi::packet::PacketFixtureLoader::load(
        steamdpi::packet::FixtureKind::ValidIpv6Tcp);
    auto ipv6UdpBytes=std::vector<std::uint8_t>(ipv6TcpFixture.bytes().begin(),ipv6TcpFixture.bytes().end());
    ipv6UdpBytes[6]=17;ipv6UdpBytes[40]=0x13;ipv6UdpBytes[41]=0x89;ipv6UdpBytes[42]=0x00;ipv6UdpBytes[43]=0x35;
    ipv6UdpBytes[44]=0;ipv6UdpBytes[45]=23;ipv6UdpBytes[46]=0;ipv6UdpBytes[47]=0;
    std::uint32_t udp6ChecksumSum=0;
    const auto addChecksumWords=[&](std::size_t begin,std::size_t length){for(std::size_t i=begin;i<begin+length;i+=2)udp6ChecksumSum+=(static_cast<std::uint32_t>(ipv6UdpBytes[i])<<8)|ipv6UdpBytes[i+1];};
    addChecksumWords(8,32);udp6ChecksumSum+=23+17;addChecksumWords(40,22);udp6ChecksumSum+=(static_cast<std::uint32_t>(ipv6UdpBytes[62])<<8);
    while(udp6ChecksumSum>>16)udp6ChecksumSum=(udp6ChecksumSum&0xffff)+(udp6ChecksumSum>>16);
    auto udp6Checksum=static_cast<std::uint16_t>(~udp6ChecksumSum);if(udp6Checksum==0)udp6Checksum=0xffff;
    ipv6UdpBytes[46]=static_cast<std::uint8_t>(udp6Checksum>>8);ipv6UdpBytes[47]=static_cast<std::uint8_t>(udp6Checksum);
    const auto udp6Parse=steamdpi::packet::PacketParser{}.parse(steamdpi::packet::PacketBuffer(std::move(ipv6UdpBytes)));
    ok &= require(udp6Parse.valid,"Synthetic IPv6 UDP packet fixture did not parse");
    const auto udp6=udp6Parse.metadata;
    auto udp6Tuple=makeTuple(udp6);udp6Tuple.protocol=17;
    auto udp6Socket=networkSocket;udp6Socket.endpointId=206;udp6Socket.tuple=udp6Tuple;
    auto udp6Flow=udp6Socket;udp6Flow.layer=steamdpi::CaptureLayer::Flow;udp6Flow.kind=steamdpi::CaptureEventKind::Established;
    networkTracker.process(udp6Socket,identity);networkTracker.process(udp6Flow,identity);
    const auto udp6outResult=networkTracker.diagnoseNetworkPacket(udp6,true,1100000000ull);
    ok &= require(udp6outResult.match==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "IPv6 UDP outbound packet failed exact correlation");
    auto udp6in=udp6;std::swap(udp6in.sourceAddress,udp6in.destinationAddress);std::swap(udp6in.sourcePort,udp6in.destinationPort);
    ok &= require(networkTracker.matchNetworkPacket(udp6in,false,1100000000ull)==steamdpi::NetworkFlowMatch::MatchedSteam,
                  "IPv6 UDP inbound packet failed reversed endpoint correlation");
    steamdpi::FlowTracker delayedTracker;
    auto lateFlowSocket=networkSocket;lateFlowSocket.endpointId=207;lateFlowSocket.timestampNs=2000000000ull;
    auto delayedFlow=lateFlowSocket;delayedFlow.layer=steamdpi::CaptureLayer::Flow;delayedFlow.kind=steamdpi::CaptureEventKind::Established;
    ok &= require(delayedTracker.diagnoseNetworkPacket(packetMetadata,true,1999000000ull).reason==
                  steamdpi::NetworkMatchReason::NoCorrespondingFlow,
                  "Packet without a corresponding flow was not diagnosed distinctly");
    delayedTracker.process(delayedFlow,identity);delayedTracker.process(lateFlowSocket,identity);
    ok &= require(delayedTracker.matchNetworkPacket(packetMetadata,true,1999000000ull)==
                  steamdpi::NetworkFlowMatch::MatchedSteam,
                  "Packet-before-FLOW timestamp skew was not accepted for a subsequently observed exact flow");
    ok &= require(delayedTracker.diagnoseNetworkPacket(packetMetadata,true,1000000000ull).reason==
                  steamdpi::NetworkMatchReason::BeforeFlowWindow,
                  "Old packet outside the bounded flow-start skew was not rejected");
    auto wrongProtocol=packetMetadata;wrongProtocol.protocol=steamdpi::packet::TransportProtocol::Udp;
    ok &= require(delayedTracker.diagnoseNetworkPacket(wrongProtocol,true,2100000000ull).reason==
                  steamdpi::NetworkMatchReason::ProtocolMismatch,
                  "Protocol mismatch was not classified");
    auto wrongAddress=packetMetadata;wrongAddress.sourceAddress[0]^=0x80;
    ok &= require(delayedTracker.diagnoseNetworkPacket(wrongAddress,true,2100000000ull).reason==
                  steamdpi::NetworkMatchReason::LocalAddressMismatch,
                  "Address representation/tuple mismatch was not classified");
    auto wrongRemoteAddress=packetMetadata;wrongRemoteAddress.destinationAddress[0]^=0x01;
    ok &= require(delayedTracker.diagnoseNetworkPacket(wrongRemoteAddress,true,2100000000ull).reason==
                  steamdpi::NetworkMatchReason::RemoteAddressMismatch,
                  "Remote endpoint/NAT mismatch was not classified");
    auto wrongPort=packetMetadata;wrongPort.sourcePort=static_cast<std::uint16_t>(wrongPort.sourcePort+1);
    ok &= require(delayedTracker.diagnoseNetworkPacket(wrongPort,true,2100000000ull).reason==
                  steamdpi::NetworkMatchReason::LocalPortMismatch,
                  "Port mismatch was not classified");
    auto wrongRemotePort=packetMetadata;wrongRemotePort.destinationPort=static_cast<std::uint16_t>(wrongRemotePort.destinationPort+1);
    ok &= require(delayedTracker.diagnoseNetworkPacket(wrongRemotePort,true,2100000000ull).reason==
                  steamdpi::NetworkMatchReason::RemotePortMismatch,
                  "Remote port mismatch was not classified");
    auto unrelatedMetadata=packetMetadata;unrelatedMetadata.destinationAddress[0]^=0x02;
    auto unrelatedSocket=networkSocket;unrelatedSocket.endpointId=299;unrelatedSocket.processId=999999;
    unrelatedSocket.tuple=makeTuple(unrelatedMetadata);
    auto unrelatedFlow=unrelatedSocket;unrelatedFlow.layer=steamdpi::CaptureLayer::Flow;
    unrelatedFlow.kind=steamdpi::CaptureEventKind::Established;
    delayedTracker.process(unrelatedSocket,std::nullopt);delayedTracker.process(unrelatedFlow,std::nullopt);
    ok &= require(delayedTracker.matchNetworkPacket(unrelatedMetadata,true,2100000000ull)==
                  steamdpi::NetworkFlowMatch::Ambiguous,
                  "Unrelated flow sharing the Steam remote port was attributed as Steam");
    steamdpi::FlowTracker identityTracker;
    auto unknownNetworkSocket=networkSocket;unknownNetworkSocket.endpointId=208;unknownNetworkSocket.processId=0;
    auto unknownNetworkFlow=unknownNetworkSocket;unknownNetworkFlow.layer=steamdpi::CaptureLayer::Flow;
    unknownNetworkFlow.kind=steamdpi::CaptureEventKind::Established;
    identityTracker.process(unknownNetworkSocket,std::nullopt);identityTracker.process(unknownNetworkFlow,std::nullopt);
    const auto identityResult=identityTracker.diagnoseNetworkPacket(packetMetadata,true,1100000000ull);
    ok &= require(identityResult.match==steamdpi::NetworkFlowMatch::Ambiguous&&
                  identityResult.reason==steamdpi::NetworkMatchReason::UnverifiedIdentity,
                  "Exact tuple with unverified executable identity was not excluded from Steam attribution");
    steamdpi::FlowTracker reusedPidTracker;
    auto reusedPidSocket=networkSocket;reusedPidSocket.endpointId=209;
    auto reusedPidFlow=reusedPidSocket;reusedPidFlow.layer=steamdpi::CaptureLayer::Flow;
    reusedPidFlow.kind=steamdpi::CaptureEventKind::Established;
    auto oldInstance=identity,newInstance=identity;oldInstance.processCreationTime=11;newInstance.processCreationTime=22;
    reusedPidTracker.process(reusedPidSocket,oldInstance);reusedPidTracker.process(reusedPidFlow,newInstance);
    const auto reusedPidResult=reusedPidTracker.diagnoseNetworkPacket(packetMetadata,true,1100000000ull);
    ok &= require(reusedPidResult.reason==steamdpi::NetworkMatchReason::UnverifiedIdentity,
                  "PID reuse with mismatched process creation times was attributed to Steam");
    steamdpi::FlowTracker closeTracker;
    auto closeSocket=networkSocket;closeSocket.endpointId=210;
    auto closeFlow=closeSocket;closeFlow.layer=steamdpi::CaptureLayer::Flow;
    closeFlow.kind=steamdpi::CaptureEventKind::Established;
    closeTracker.process(closeSocket,identity);closeTracker.process(closeFlow,identity);
    auto closeEvent=closeFlow;closeEvent.kind=steamdpi::CaptureEventKind::Deleted;
    closeEvent.timestampNs=1200000000ull;closeTracker.process(closeEvent,std::nullopt);
    ok &= require(closeTracker.matchNetworkPacket(packetMetadata,true,1100000000ull)==
                  steamdpi::NetworkFlowMatch::MatchedSteam&&
                  closeTracker.diagnoseNetworkPacket(packetMetadata,true,1300000000ull).reason==
                  steamdpi::NetworkMatchReason::AfterFlowClosed,
                  "Recently closed flow was not correlated only to packets within its timestamped lifetime");

    auto secondSocket=networkSocket;secondSocket.endpointId=201;auto secondFlow=networkFlow;secondFlow.endpointId=201;
    networkTracker.process(secondSocket,identity);networkTracker.process(secondFlow,identity);
    ok &= require(networkTracker.matchNetworkPacket(packetMetadata,true,1200000000ull)==steamdpi::NetworkFlowMatch::Ambiguous,
                  "Multiple matching endpoints did not fail closed as ambiguous");
    steamdpi::FlowTracker expirationTracker;
    auto expirationSocket=networkSocket;expirationSocket.endpointId=202;expirationSocket.tuple.localPort=45001;expirationSocket.timestampNs=1000000000ull;
    auto expirationFlow=expirationSocket;expirationFlow.layer=steamdpi::CaptureLayer::Flow;expirationFlow.kind=steamdpi::CaptureEventKind::Established;
    expirationTracker.process(expirationSocket,identity);expirationTracker.process(expirationFlow,identity);
    auto expiredMetadata=packetMetadata;expiredMetadata.sourcePort=45001;
    ok &= require(expirationTracker.diagnoseNetworkPacket(expiredMetadata,true,3602000000000ull).reason==steamdpi::NetworkMatchReason::ExpiredFlow,
                  "Expired endpoint was retained as a Steam match");

    auto nearShutdown=flow;nearShutdown.endpointId=107;
    tracker.process(nearShutdown,identity);
    auto closing=nearShutdown;closing.kind=steamdpi::CaptureEventKind::Deleted;
    tracker.process(closing,identity);
    auto delayedSocket=nearShutdown;delayedSocket.layer=steamdpi::CaptureLayer::Socket;
    delayedSocket.kind=steamdpi::CaptureEventKind::Connect;
    tracker.process(delayedSocket,std::nullopt);
    ok &= require(tracker.snapshot().steamFlowsObserved==3&&
                  tracker.snapshot().unknownOrAmbiguousFlows==4,
                  "Late socket event resurrected a flow after endpoint shutdown");

    auto deleted=flow;deleted.kind=steamdpi::CaptureEventKind::Deleted;tracker.process(deleted,identity);
    ok &= require(tracker.snapshot().activeSteamFlows==2,"Flow deletion did not reduce active flow count");

    steamdpi::BoundedEventQueue<int> queue(2);
    ok &= require(queue.push(1)&&queue.push(2)&&!queue.push(3),"Bounded event queue did not enforce capacity");
    ok &= require(queue.waitPop()==1&&queue.waitPop()==2,"Bounded event queue order was not preserved");
    ok &= require(queue.highWaterMark()==2&&queue.pushes()==2&&queue.pops()==2,
                  "Bounded queue occupancy metrics were incorrect");
    queue.close();ok &= require(!queue.waitPop(),"Closed empty event queue did not stop cleanly");

    auto fake=std::make_unique<FakeCaptureAdapter>();auto* fakePointer=fake.get();fakePointer->failNext=true;
    steamdpi::CaptureController controller(std::move(fake),[identity](std::uint32_t pid)->std::optional<steamdpi::ProcessIdentity>{return pid==identity.processId?std::optional(identity):std::nullopt;},false,
      steamdpi::NetworkCaptureProfile::AllTcpUdp);
    std::string startError;
    ok &= require(!controller.start(startError)&&controller.snapshot().state==steamdpi::CaptureState::Error,
                  "Capture-open failure was not reported");
    for(int cycle=0;cycle<4;++cycle){
      ok &= require(controller.start(startError),"Capture controller did not recover after open failure");
      if(cycle==0){
        auto attributedSocket=socket;attributedSocket.tuple=makeTuple(packetMetadata);
        auto attributedFlow=attributedSocket;attributedFlow.layer=steamdpi::CaptureLayer::Flow;attributedFlow.kind=steamdpi::CaptureEventKind::Established;
        steamdpi::CaptureEvent packetEvent{};packetEvent.layer=steamdpi::CaptureLayer::Network;packetEvent.kind=steamdpi::CaptureEventKind::NetworkPacket;
        packetEvent.packetBytes.assign(fixture.bytes().begin(),fixture.bytes().end());packetEvent.timestampNs=1000000000ull;packetEvent.outbound=true;
        packetEvent.ipv4ChecksumValid=true;packetEvent.transportChecksumValid=true;
        fakePointer->onEvent(packetEvent);
        Sleep(30); // Force NETWORK parsing to race ahead of the attribution events.
        fakePointer->onEvent(attributedSocket);fakePointer->onEvent(attributedFlow);
        auto invalidPacket=packetEvent;const auto invalidFixture=steamdpi::packet::PacketFixtureLoader::load(steamdpi::packet::FixtureKind::InvalidChecksum);
        invalidPacket.packetBytes.assign(invalidFixture.bytes().begin(),invalidFixture.bytes().end());
        fakePointer->onEvent(std::move(invalidPacket));
      }
      Sleep(30);
      controller.stop();
      if(cycle==0){const auto packetMetrics=controller.snapshot().metrics;
        ok &= require(packetMetrics.networkPacketsObserved==2&&packetMetrics.networkPacketsParsed==1&&
                      packetMetrics.networkPacketsRejected==1&&packetMetrics.networkPacketsMatchedSteam==1&&
                      packetMetrics.networkPacketsUnmatched==0,
                      "Read-only NETWORK parser metrics or malformed packet rejection failed");}
      ok &= require(controller.snapshot().state==steamdpi::CaptureState::Stopped,
                    "Capture controller did not stop cleanly");
    }
    ok &= require(fakePointer->starts==5&&fakePointer->stops>=4,
                  "Repeated capture start/stop lifecycle did not complete");
    {
      auto strictAdapter=std::make_unique<FakeCaptureAdapter>();auto* strictPointer=strictAdapter.get();
      steamdpi::CaptureController strictController(std::move(strictAdapter),
        [identity](std::uint32_t pid)->std::optional<steamdpi::ProcessIdentity>{return pid==identity.processId?std::optional(identity):std::nullopt;},
        false,steamdpi::NetworkCaptureProfile::VerifiedSteamEndpointCandidates);
      ok &= require(strictController.start(startError),"Strict candidate profile failed to start");
      auto strictSocket=networkSocket;strictSocket.endpointId=901;strictSocket.timestampNs=3000000000ull;
      auto strictFlow=strictSocket;strictFlow.layer=steamdpi::CaptureLayer::Flow;strictFlow.kind=steamdpi::CaptureEventKind::Established;
      strictPointer->onEvent(strictSocket);strictPointer->onEvent(strictFlow);Sleep(40);
      steamdpi::CaptureEvent relevant{};relevant.layer=steamdpi::CaptureLayer::Network;relevant.kind=steamdpi::CaptureEventKind::NetworkPacket;
      relevant.packetBytes.assign(fixture.bytes().begin(),fixture.bytes().end());relevant.timestampNs=3100000000ull;relevant.outbound=true;
      relevant.ipv4ChecksumValid=true;relevant.transportChecksumValid=true;strictPointer->onEvent(relevant);
      auto irrelevant=relevant;const auto unrelated=steamdpi::packet::PacketFixtureLoader::load(steamdpi::packet::FixtureKind::ValidIpv4Udp);
      irrelevant.packetBytes.assign(unrelated.bytes().begin(),unrelated.bytes().end());strictPointer->onEvent(std::move(irrelevant));
      Sleep(40);strictController.stop();const auto strictMetrics=strictController.snapshot().metrics;
      ok &= require(strictMetrics.networkPacketsObserved==2&&strictMetrics.networkPacketsFilteredBeforeParser==1&&
                    strictMetrics.networkFilteredOutsideEndpointScope==1&&strictMetrics.networkPacketsParsed==1&&
                    strictMetrics.networkPacketsMatchedSteam==1&&strictMetrics.networkCorrelationQueueDrops==0,
                    "Verified Steam candidate profile did not reject unrelated traffic before parsing");
      ok &= require(strictMetrics.networkQueueHighWater>0&&strictMetrics.networkCorrelationQueueHighWater>0,
                    "Queue high-water metrics were not recorded");
    }
    {
      auto overloadAdapter=std::make_unique<FakeCaptureAdapter>();auto* overloadPointer=overloadAdapter.get();
      steamdpi::CaptureController overloadController(std::move(overloadAdapter),{},false,
        steamdpi::NetworkCaptureProfile::AllTcpUdp);
      ok &= require(overloadController.start(startError),"Overload controller failed to start");
      steamdpi::CaptureEvent load{};load.layer=steamdpi::CaptureLayer::Network;load.kind=steamdpi::CaptureEventKind::NetworkPacket;
      load.packetBytes.assign(fixture.bytes().begin(),fixture.bytes().end());load.timestampNs=3200000000ull;
      for(int packet=0;packet<2000;++packet)overloadPointer->onEvent(load);
      overloadController.stop();const auto loadMetrics=overloadController.snapshot().metrics;
      ok &= require(loadMetrics.networkCaptureQueueDrops>0&&loadMetrics.networkQueueHighWater<=128&&
                    loadMetrics.networkPacketsObserved==2000&&
                    loadMetrics.networkPacketsFilteredBeforeParser+loadMetrics.networkCaptureQueueDrops+
                      loadMetrics.networkPacketsParsed+loadMetrics.networkPacketsRejected==2000,
                    "Overload behavior was unbounded or captured-packet accounting did not balance");
    }
    ok &= require(controller.start(startError),"Capture controller could not start for receive-failure recovery test");
    fakePointer->onError(1234);
    for(int wait=0;wait<100&&controller.snapshot().state!=steamdpi::CaptureState::Error;++wait)Sleep(10);
    ok &= require(controller.snapshot().state==steamdpi::CaptureState::Error&&
                  controller.snapshot().metrics.observationErrors>0,
                  "Capture receive error did not enter recoverable error state");
    ok &= require(controller.start(startError),"Capture controller did not recover after receive failure");
    controller.stop();
    fakePointer->fakeStopError=1235;
    ok &= require(controller.start(startError),"Capture controller did not start for close failure fixture");
    controller.stop();
    ok &= require(controller.snapshot().state==steamdpi::CaptureState::Error&&
                  controller.snapshot().lastError==1235,
                  "Capture close failure was not surfaced to the controller");
    fakePointer->fakeStopError=0;
    ok &= require(controller.start(startError),"Capture controller did not recover after close failure");
    controller.stop();
    ok &= require(controller.snapshot().state==steamdpi::CaptureState::Stopped,
                  "Capture controller did not stop normally after close failure recovery");
    ok &= require(controller.start(startError),"Capture controller did not start for pending-correlation timeout fixture");
    steamdpi::CaptureEvent orphanPacket{};orphanPacket.layer=steamdpi::CaptureLayer::Network;
    orphanPacket.kind=steamdpi::CaptureEventKind::NetworkPacket;
    orphanPacket.packetBytes.assign(fixture.bytes().begin(),fixture.bytes().end());
    orphanPacket.timestampNs=2000000000ull;orphanPacket.outbound=true;
    orphanPacket.ipv4ChecksumValid=true;orphanPacket.transportChecksumValid=true;
    fakePointer->onEvent(std::move(orphanPacket));Sleep(750);
    const auto timedOutCorrelation=controller.snapshot().metrics;
    ok &= require(timedOutCorrelation.networkPacketsUnmatched==1&&
                  timedOutCorrelation.networkNoCorrespondingFlow==1,
                  "Metadata-only pending correlation did not expire and classify unmatched packets");
    controller.stop();

    auto shutdownStops=std::make_shared<std::atomic_int>(0);
    {
      auto shutdownAdapter=std::make_unique<FakeCaptureAdapter>();
      shutdownAdapter->externalStops=shutdownStops;
      steamdpi::CaptureController shutdownController(std::move(shutdownAdapter),{},false);
      ok &= require(shutdownController.start(startError),"Shutdown fixture capture did not start");
    }
    ok &= require(*shutdownStops>=1,"Controller destruction did not stop active observation");
  }
  if(!ok)return 1;
  std::cout<<"SteamDPI local network fixture tests passed\n";return 0;
}
