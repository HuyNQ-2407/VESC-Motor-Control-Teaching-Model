#include "mainwindow.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyleSheet("QWidget { background-color: #30304D; font-family: Calibri; font-size: 15pt; color: black }");
    app.setWindowIcon(QIcon(":/icons/icons/logolab.png"));
    MainWindow window;
    window.show();
    return app.exec();
}
