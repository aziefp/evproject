#pragma once

#include "protocol.h"

#include <QJsonObject>
#include <QObject>

class QTcpSocket;
class QTimer;

class ClientConnection : public QObject
{
    Q_OBJECT

public:
    explicit ClientConnection(QObject *parent = nullptr);
    void connectToServer(const QString &host, quint16 port);
    QString sendRequest(const QString &type, const QJsonObject &data = {});
    void setSessionToken(const QString &token);
    bool isConnected() const;

signals:
    void messageReceived(const QJsonObject &message);
    void statusChanged(const QString &text, bool connected);

private slots:
    void readIncoming();
    void scheduleReconnect();

private:
    void openSocket();

    QTcpSocket *socket_ = nullptr;
    QTimer *reconnectTimer_ = nullptr;
    QTimer *heartbeatTimer_ = nullptr;
    ev::FrameDecoder decoder_;
    QString host_;
    quint16 port_ = 0;
    QString sessionToken_;
    int reconnectAttempt_ = 0;
};
