#pragma once
#include <QMainWindow>
class QLabel;
class QPushButton;
class QTextEdit;

namespace steamdpi {
class AppController;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private:
  AppController* controller_;
  QLabel* status_;
  QPushButton *run_, *cancel_;
  QPushButton *captureStart_, *captureStop_;
  QLabel *captureStatus_, *captureMetrics_, *captureAttribution_, *networkMetrics_;
  QTextEdit *dns_, *tcp_, *tls_, *log_;
};
}
