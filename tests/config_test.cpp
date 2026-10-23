#include "core/AppConfig.h"
#include "core/ReportSerializer.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);QTemporaryDir dir;if(!dir.isValid())return 1;
 const QString path=dir.filePath("config.json");QFile f(path);
 if(!f.open(QIODevice::WriteOnly))return 2;f.write("{\"timeoutMs\": 50}");f.close();
 steamdpi::AppConfig config;QString error;
 if(steamdpi::loadConfig(path,config,error)){std::cerr<<"Out-of-range timeout was accepted\n";return 3;}
 if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return 4;f.write("not-json");f.close();
 if(steamdpi::loadConfig(path,config,error)){std::cerr<<"Malformed JSON was accepted\n";return 5;}
 if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate))return 6;f.write("{\"timeoutMs\": 1800, \"maxConcurrentProbes\": 2}");f.close();
 if(!steamdpi::loadConfig(path,config,error)||config.timeoutMs!=1800||config.maxConcurrentProbes!=2){std::cerr<<"Valid configuration was rejected\n";return 7;}
 steamdpi::DiagnosticReport report;report.results.push_back({"localhost","TLS","fixture",steamdpi::ProbeStatus::Success,12,1760000000000LL});
 const auto json=QJsonDocument::fromJson(steamdpi::serializeReport(report));
 if(!json.isObject()||json.object().value("engineState").toString()!="NOT_IMPLEMENTED"||json.object().value("results").toArray().size()!=1||json.object().value("results").toArray().first().toObject().value("status").toString()!="SUCCESS")return 8;
 std::cout<<"Configuration and report serialization tests passed\n";return 0;
}
