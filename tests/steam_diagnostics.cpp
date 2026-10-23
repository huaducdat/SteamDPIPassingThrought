#include "diagnostics/DiagnosticService.h"
#include "network/SteamTargets.h"
#include <atomic>
#include <iostream>
#include <vector>

int main(int argc,char** argv){
  std::vector<std::string> hosts;
  if(argc>1){for(int i=1;i<argc;++i)hosts.emplace_back(argv[i]);}
  else {for(const auto& target:steamdpi::steamTargets())hosts.emplace_back(target.host);}
  std::atomic_bool cancel{false};steamdpi::DiagnosticService service;
  const auto report=service.run(hosts,5000,4,cancel,[](const steamdpi::ProbeResult& r){
    std::cout<<r.host<<" | "<<r.stage<<" | "<<steamdpi::probeStatusName(r.status)<<" | "<<r.latencyMs<<" ms | "<<r.detail<<'\n';
  });
  return report.results.empty()?1:0;
}
