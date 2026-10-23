#pragma once
#include "DiagnosticService.h"
namespace steamdpi { ProbeResult probeTls(const std::string& host, int timeoutMs, const std::atomic_bool& cancel, unsigned short port = 443); }
