#pragma once
#include "../diagnostics/DiagnosticService.h"
#include <QByteArray>
namespace steamdpi { QByteArray serializeReport(const DiagnosticReport& report); }
