#include "TcpProbe.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <algorithm>
namespace steamdpi {
ProbeResult probeTcp(const std::string& host,int timeoutMs,const std::atomic_bool& cancel,unsigned short port){
 ProbeResult r{host,"TCP",{},ProbeStatus::Failure,0,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}; WSADATA w{}; if(WSAStartup(MAKEWORD(2,2),&w)){r.detail="Winsock initialization failed";return r;}
 addrinfo hints{}; hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP; addrinfo* list=nullptr;
 auto start=std::chrono::steady_clock::now(); const auto service=std::to_string(port); int e=getaddrinfo(host.c_str(),service.c_str(),&hints,&list); if(e){r.detail="Address lookup failed";r.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();WSACleanup();return r;}
 auto until=start+std::chrono::milliseconds(timeoutMs); bool refused=false;
 for(auto p=list;p && std::chrono::steady_clock::now()<until;p=p->ai_next){if(cancel){r.status=ProbeStatus::Cancelled;r.detail="Cancelled";break;}
  SOCKET s=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(s==INVALID_SOCKET)continue;
  WSAEVENT event=WSACreateEvent();if(event==WSA_INVALID_EVENT){closesocket(s);continue;}
  if(WSAEventSelect(s,event,FD_CONNECT)==SOCKET_ERROR){WSACloseEvent(event);closesocket(s);continue;}
  int c=connect(s,p->ai_addr,static_cast<int>(p->ai_addrlen));int connectError=c==SOCKET_ERROR?WSAGetLastError():0;
  if(c==SOCKET_ERROR&&connectError!=WSAEWOULDBLOCK&&connectError!=WSAEINPROGRESS){if(connectError==WSAECONNREFUSED)refused=true;WSACloseEvent(event);closesocket(s);continue;}
  int soerr=0; bool connected=false;
  if(c==0)connected=true;
  while(!connected&&!cancel&&std::chrono::steady_clock::now()<until){
   auto left=std::chrono::duration_cast<std::chrono::milliseconds>(until-std::chrono::steady_clock::now()).count();DWORD slice=static_cast<DWORD>(std::min<long long>(left,50));
   const DWORD wait=WSAWaitForMultipleEvents(1,&event,FALSE,slice,FALSE);
   if(wait==WSA_WAIT_EVENT_0){WSANETWORKEVENTS events{};if(WSAEnumNetworkEvents(s,event,&events)==SOCKET_ERROR)break;soerr=events.iErrorCode[FD_CONNECT_BIT];connected=soerr==0;if(soerr==WSAECONNREFUSED)refused=true;break;}
   if(wait!=WSA_WAIT_TIMEOUT)break;
  }
  if(cancel){r.status=ProbeStatus::Cancelled;r.detail="Cancelled";}
  if(connected){r.status=ProbeStatus::Success;r.detail="TCP port "+std::to_string(port)+" reachable";}
  WSACloseEvent(event);closesocket(s);
 }
 if(r.status!=ProbeStatus::Success&&r.status!=ProbeStatus::Cancelled){r.status=std::chrono::steady_clock::now()>=until?ProbeStatus::Timeout:ProbeStatus::Failure;r.detail=refused?"Connection refused":"TCP connection failed or timed out";}
 if(r.status==ProbeStatus::Cancelled)r.detail="Cancelled";
 r.latencyMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();if(list)freeaddrinfo(list);WSACleanup();return r;
}
}
