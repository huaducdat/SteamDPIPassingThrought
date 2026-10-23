#pragma once
#include <QString>
namespace steamdpi {
struct AppConfig {
  int timeoutMs = 5000;
  int maxConcurrentProbes = 4;
};
bool loadConfig(const QString& path, AppConfig& config, QString& error);
bool saveDefaultConfig(const QString& path, QString& error);
}
