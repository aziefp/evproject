#include "adminwindow.h"
#include "serverworker.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QThread>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("EV Admin Server");

    QCommandLineParser parser;
    parser.setApplicationDescription("电动汽车充电桩管理服务器");
    parser.addHelpOption();
    parser.addOption({"listen-address", "TCP 监听地址", "address", "0.0.0.0"});
    parser.addOption({"port", "TCP 监听端口", "port", "45454"});
    parser.addOption({"db", "SQLite 数据库路径", "path", "runtime/evplatform.db"});
    parser.addOption({"key-file", "腾讯位置服务 Key 文件", "path", "key.txt"});
    parser.addOption({"simulation-speed", "充电模拟倍率", "value", "60"});
    parser.process(app);

    AdminWindow window;
    QThread workerThread;
    auto *worker = new ServerWorker;
    worker->moveToThread(&workerThread);
    QObject::connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(&window, &AdminWindow::adminCommand,
                     worker, &ServerWorker::handleAdminCommand, Qt::QueuedConnection);
    QObject::connect(worker, &ServerWorker::adminResult,
                     &window, &AdminWindow::handleAdminResult, Qt::QueuedConnection);
    QObject::connect(worker, &ServerWorker::statusChanged,
                     &window, &AdminWindow::setServerStatus, Qt::QueuedConnection);
    QObject::connect(worker, &ServerWorker::started, &window,
                     [&window](bool ok, const QString &message, quint16) {
                         window.setServerStatus(message);
                     });
    workerThread.start();

    const quint16 port = static_cast<quint16>(parser.value("port").toUShort());
    const QString listenAddress = parser.value("listen-address");
    const QString databasePath = QDir::current().absoluteFilePath(parser.value("db"));
    const QString keyFile = QDir::current().absoluteFilePath(parser.value("key-file"));
    const int simulationSpeed = parser.value("simulation-speed").toInt();
    QMetaObject::invokeMethod(worker, "start", Qt::QueuedConnection,
                              Q_ARG(QString, listenAddress), Q_ARG(quint16, port), Q_ARG(QString, databasePath),
                              Q_ARG(QString, keyFile), Q_ARG(int, simulationSpeed));

    window.show();
    const int result = app.exec();
    QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection);
    workerThread.quit();
    workerThread.wait();
    return result;
}
