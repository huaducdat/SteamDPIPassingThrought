#pragma once
#include "DiagnosticService.h"
namespace steamdpi { ProbeResult probeTcp(const std::string& host, int timeoutMs, const std::atomic_bool& cancel, unsigned short port = 443); }
