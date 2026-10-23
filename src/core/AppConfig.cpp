#include "AppConfig.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace steamdpi {
bool loadConfig(const QString& path, AppConfig& config, QString& error) {
  QFile file(path); if (!file.exists()) return true;
  if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
  QJsonParseError parse{}; const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) { error = "Invalid JSON configuration"; return false; }
  const auto obj = doc.object(); AppConfig next;
  const auto timeout = obj.value("timeoutMs"); const auto concurrency = obj.value("maxConcurrentProbes");
  if (!timeout.isUndefined()) { if (!timeout.isDouble() || timeout.toInt(-1) < 250 || timeout.toInt() > 30000) { error = "timeoutMs must be 250..30000"; return false; } next.timeoutMs = timeout.toInt(); }
  if (!concurrency.isUndefined()) { if (!concurrency.isDouble() || concurrency.toInt(-1) < 1 || concurrency.toInt() > 8) { error = "maxConcurrentProbes must be 1..8"; return false; } next.maxConcurrentProbes = concurrency.toInt(); }
  for (auto it=obj.begin();it!=obj.end();++it) if(it.key()!="timeoutMs"&&it.key()!="maxConcurrentProbes"){error="Unknown configuration field: "+it.key();return false;}
  config = next; return true;
}
bool saveDefaultConfig(const QString& path, QString& error) {
  QFile f(path); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = f.errorString(); return false; }
  f.write(QJsonDocument(QJsonObject{{"timeoutMs",5000},{"maxConcurrentProbes",4}}).toJson(QJsonDocument::Indented)); return true;
}
}
