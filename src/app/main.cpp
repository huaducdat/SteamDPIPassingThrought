#include "MainWindow.h"
#include <QApplication>
int main(int argc,char** argv){QApplication app(argc,argv);QApplication::setApplicationName("SteamDPI");QApplication::setOrganizationName("SteamDPI");steamdpi::MainWindow window;window.show();return app.exec();}
