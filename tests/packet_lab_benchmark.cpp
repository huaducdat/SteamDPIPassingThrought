#include "packet/PacketFixtureLoader.h"
#include "packet/PacketTransformPipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

int main(){
  using namespace steamdpi::packet;using Clock=std::chrono::steady_clock;
  SYSTEM_INFO si{};GetSystemInfo(&si);MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);GlobalMemoryStatusEx(&memory);
  const char* cpu=std::getenv("PROCESSOR_IDENTIFIER");
  std::cout<<"machine_cpu="<<(cpu?cpu:"unknown")<<" logical_processors="<<si.dwNumberOfProcessors
           <<" physical_memory_gib="<<std::fixed<<std::setprecision(1)<<(static_cast<double>(memory.ullTotalPhys)/(1024.0*1024*1024))<<"\n";
  std::vector<PacketBuffer> fixtures{PacketFixtureLoader::load(FixtureKind::ValidIpv4Tcp),PacketFixtureLoader::load(FixtureKind::ValidIpv6Tcp),PacketFixtureLoader::load(FixtureKind::ValidIpv4Udp)};
  PacketTransformPipeline pipeline;pipeline.add(std::make_unique<DryRunTransform>());
  std::uint64_t valid=0;
  for(int i=0;i<5000;++i)valid+=pipeline.run(fixtures[static_cast<std::size_t>(i)%fixtures.size()]).validation.valid;
  constexpr int batchSize=100, measuredBatches=1000;
  std::vector<double> ns;ns.reserve(measuredBatches);
  for(int batch=0;batch<measuredBatches;++batch){const auto start=Clock::now();for(int j=0;j<batchSize;++j){const auto index=static_cast<std::size_t>(batch*batchSize+j)%fixtures.size();valid+=pipeline.run(fixtures[index]).validation.valid;}const auto end=Clock::now();
    ns.push_back(std::chrono::duration<double,std::nano>(end-start).count()/batchSize);}
  std::sort(ns.begin(),ns.end());const auto median=ns[ns.size()/2];const auto p95=ns[static_cast<std::size_t>((ns.size()-1)*0.95)];
  std::cout<<"mode=offline_synthetic_packets iterations="<<measuredBatches*batchSize<<" valid="<<valid<<" median_ns="<<std::setprecision(1)<<median<<" p95_ns="<<p95
           <<" median_us="<<median/1000.0<<" p95_us="<<p95/1000.0<<"\n";
  return valid==105000?0:1;
}
