#include <QApplication>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);
    a.setApplicationName("IDE Embedded");
    a.setApplicationVersion("1.0.1"); // Fresh Re-index Build Version
    a.setWindowIcon(QIcon(":/icon.png"));

    MainWindow w;
    w.showMaximized();
    return a.exec();
}
