#include "MmsPrototypeWindow.hpp"
#include <QApplication>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("MmsInteractionPrototype"));
    iec61850sim::prototype::MmsPrototypeWindow window;
    window.show();
    return application.exec();
}
