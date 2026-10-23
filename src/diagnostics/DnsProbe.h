#pragma once
#include "DiagnosticService.h"
namespace steamdpi { ProbeResult probeDns(const std::string& host, int timeoutMs, const std::atomic_bool& cancel); }
