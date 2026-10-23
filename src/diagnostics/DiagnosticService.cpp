#include "DiagnosticService.h"
#include "DnsProbe.h"
#include "TcpProbe.h"
#include "TlsProbe.h"
#include <algorithm>
#include <mutex>
#include <thread>
namespace steamdpi {
DiagnosticReport DiagnosticService::run(const std::vector<std::string>& hosts,int timeoutMs,int concurrency,const std::atomic_bool& cancel,Progress progress) const {
 DiagnosticReport report; const int limit=std::clamp(concurrency,1,8); std::mutex mutex; std::size_t next=0;
 std::vector<std::thread> workers;
 const auto tcpDefault=[](const std::string& host,int timeout,const std::atomic_bool& stop){return probeTcp(host,timeout,stop);};
 const auto tlsDefault=[](const std::string& host,int timeout,const std::atomic_bool& stop){return probeTls(host,timeout,stop);};
 using ProbeFunction=ProbeResult(*)(const std::string&,int,const std::atomic_bool&);
 const ProbeFunction probes[]{probeDns,tcpDefault,tlsDefault};
 auto worker=[&]{ for(;;){std::size_t i;{std::lock_guard lock(mutex);if(next>=hosts.size()||cancel)return;i=next++;}
   const auto& h=hosts[i];for(auto fn:probes){if(cancel)break;auto res=fn(h,timeoutMs,cancel);{std::lock_guard lock(mutex);report.results.push_back(res);}if(progress)progress(res);}
 }};
 for(int i=0;i<limit&&i<static_cast<int>(hosts.size());++i)workers.emplace_back(worker);
 for (auto& thread : workers) thread.join();
 return report;
}
}
