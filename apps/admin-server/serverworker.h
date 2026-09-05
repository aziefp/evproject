#pragma once

#include "protocol.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

class Database;
struct DbResult;
class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;
class QTimer;

class ServerWorker : public QObject
{
    Q_OBJECT

public:
    explicit ServerWorker(QObject *parent = nullptr);
    ~ServerWorker() override;

public slots:
    void start(const QString &listenAddress, quint16 port, const QString &databasePath,
               const QString &keyFile, int simulationSpeed);
    void stop();
    void handleAdminCommand(quint64 requestId, const QString &action, const QJsonObject &data);

signals:
    void started(bool ok, const QString &message, quint16 port);
    void statusChanged(const QString &message);
    void adminResult(quint64 requestId, const QString &action, bool ok,
                     const QJsonObject &data, const QString &message);

private slots:
    void acceptConnections();
    void readClient();
    void removeClient();
    void tickCharging();

private:
    struct ClientState {
        ev::FrameDecoder decoder;
        qint64 userId = 0;
        QString sessionToken;
        qint64 lastSeenMs = 0;
        QHash<QString, QJsonObject> responseCache;
    };

    void dispatch(QTcpSocket *socket, const QJsonObject &request);
    void send(QTcpSocket *socket, const QJsonObject &message);
    void sendResponse(QTcpSocket *socket, const QJsonObject &request, const QJsonObject &message);
    void sendDbResult(QTcpSocket *socket, const QJsonObject &request,
                      const QString &resultType, const DbResult &result);
    void sendOrderResult(QTcpSocket *socket, const QJsonObject &request,
                         const QString &resultType, const DbResult &result);
    bool authenticate(QTcpSocket *socket, const QJsonObject &request);
    void geocode(QTcpSocket *socket, const QJsonObject &request, const QString &address);
    void broadcastOrder(const QJsonObject &order, const QString &change = "state");
    void broadcastStationsChanged();
    void broadcastProfile(qint64 userId, const QJsonObject &profile);
    void broadcastUserStatus(qint64 userId, const QString &status);

    QTcpServer *server_ = nullptr;
    QNetworkAccessManager *network_ = nullptr;
    QTimer *chargingTimer_ = nullptr;
    QTimer *idleTimer_ = nullptr;
    Database *database_ = nullptr;
    QHash<QTcpSocket *, ClientState> clients_;
    QString mapKey_;
    int simulationSpeed_ = 60;
};
