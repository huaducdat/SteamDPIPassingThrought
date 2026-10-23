#pragma once
#include "../diagnostics/DiagnosticService.h"
#include "AppConfig.h"
#include "../capture/CaptureController.h"
#include <QObject>
#include <atomic>
#include <memory>
#include <thread>
namespace steamdpi {
class AppController : public QObject {
 Q_OBJECT
 public:
 explicit AppController(QObject* parent=nullptr); ~AppController() override;
 bool running() const { return running_.load(); }
 const AppConfig& config() const { return config_; }
 public slots: void runDiagnostics(); void cancelDiagnostics(); void exportReport();
 void startCapture(); void stopCapture();
 signals: void logMessage(const QString&); void resultMessage(const QString&); void runningChanged(bool); void networkStatus(const QString&);
 void captureSnapshotChanged(const QString&, qulonglong, qulonglong, qulonglong,
                             qulonglong, qulonglong, qulonglong, qulonglong,
                             qulonglong, qulonglong, qulonglong, const QString&);
 private:
 AppConfig config_; std::atomic_bool cancel_{false},running_{false}; std::thread worker_; DiagnosticReport report_;
 std::unique_ptr<CaptureController> capture_;
 std::thread captureStartWorker_; std::atomic_bool captureStartPending_{false};
};
}
