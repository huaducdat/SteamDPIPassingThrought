#include "ReportSerializer.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
namespace steamdpi {
QByteArray serializeReport(const DiagnosticReport& report){
 QJsonArray rows;
 for(const auto& r:report.results)rows.append(QJsonObject{
  {"host",QString::fromStdString(r.host)},
  {"stage",QString::fromStdString(r.stage)},
  {"status",probeStatusName(r.status)},
  {"detail",QString::fromStdString(r.detail)},
  {"latencyMs",static_cast<qint64>(r.latencyMs)},
  {"checkedAtUtc",QDateTime::fromMSecsSinceEpoch(r.checkedAtUnixMs,QTimeZone::UTC).toString(Qt::ISODateWithMs)}});
 return QJsonDocument(QJsonObject{
  {"engineState","NOT_IMPLEMENTED"},
  {"generatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
  {"results",rows}}).toJson(QJsonDocument::Indented);
}
}
