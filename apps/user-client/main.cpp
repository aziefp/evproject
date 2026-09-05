#include "userwindow.h"
#include "mapdialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>

namespace {

bool isVmwareGuest()
{
    const QStringList paths{
        QStringLiteral("/sys/class/dmi/id/sys_vendor"),
        QStringLiteral("/sys/class/dmi/id/product_name")};
    for (const QString &path : paths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)
            && QString::fromUtf8(file.readAll()).contains("vmware", Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void configureWebEngineRendering()
{
    if (qEnvironmentVariableIsSet("EV_WEBENGINE_COMPAT")
        && qEnvironmentVariableIntValue("EV_WEBENGINE_COMPAT") == 0) {
        return;
    }

    const bool explicitlyForced = qEnvironmentVariableIntValue("EV_WEBENGINE_SOFTWARE") == 1;
    const bool affectedVmwareWayland = qEnvironmentVariable("XDG_SESSION_TYPE").compare(
                                           "wayland", Qt::CaseInsensitive) == 0
                                       && isVmwareGuest();
    if (!explicitlyForced && !affectedVmwareWayland)
        return;

    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (!flags.contains("--disable-gpu")) {
        if (!flags.trimmed().isEmpty())
            flags.append(' ');
        flags.append("--disable-gpu");
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    }
    qInfo() << "Qt WebEngine compatibility rendering enabled"
            << "platform=" << qgetenv("QT_QPA_PLATFORM");
}

} // namespace

int main(int argc, char *argv[])
{
    // Qt chooses both the window-system backend and Chromium graphics backend
    // while QApplication is created, so the compatibility decision must happen first.
    configureWebEngineRendering();
    QApplication app(argc, argv);
    QApplication::setApplicationName("EV User Client");
    QCommandLineParser parser;
    parser.setApplicationDescription("电动汽车充电服务用户端");
    parser.addHelpOption();
    parser.addOption({"host", "服务器地址", "address", "127.0.0.1"});
    parser.addOption({"port", "服务器端口", "port", "45454"});
    parser.addOption({"key-file", "腾讯位置服务 Key 文件", "path", "key.txt"});
    parser.addOption({"map-test", "不连接服务器，直接打开地图渲染测试页"});
    parser.process(app);

    QFile keyFile(QDir::current().absoluteFilePath(parser.value("key-file")));
    QString key;
    if (keyFile.open(QIODevice::ReadOnly | QIODevice::Text))
        key = QString::fromUtf8(keyFile.readAll()).trimmed();

    if (parser.isSet("map-test")) {
        MapDialog map(key, "drive", "深圳市民中心", 22.543687, 114.059625,
                      "深圳北站", 22.60999, 114.02985);
        map.show();
        return app.exec();
    }

    UserWindow window(parser.value("host"), static_cast<quint16>(parser.value("port").toUShort()), key);
    window.show();
    return app.exec();
}
