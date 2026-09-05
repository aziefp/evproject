#include "clientconnection.h"

#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

ClientConnection::ClientConnection(QObject *parent)
    : QObject(parent), socket_(new QTcpSocket(this)), reconnectTimer_(new QTimer(this)),
      heartbeatTimer_(new QTimer(this))
{
    reconnectTimer_->setSingleShot(true);
    heartbeatTimer_->setInterval(15000);
    connect(reconnectTimer_, &QTimer::timeout, this, &ClientConnection::openSocket);
    connect(heartbeatTimer_, &QTimer::timeout, this, [this] {
        if (isConnected())
            sendRequest("ping");
    });
    connect(socket_, &QTcpSocket::connected, this, [this] {
        reconnectAttempt_ = 0;
        decoder_.clear();
        heartbeatTimer_->start();
        emit statusChanged(QStringLiteral("已连接 %1:%2").arg(host_).arg(port_), true);
    });
    connect(socket_, &QTcpSocket::readyRead, this, &ClientConnection::readIncoming);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientConnection::scheduleReconnect);
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit statusChanged(QStringLiteral("连接异常：%1").arg(socket_->errorString()), false);
    });
}

void ClientConnection::connectToServer(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
    reconnectAttempt_ = 0;
    reconnectTimer_->stop();
    openSocket();
}

void ClientConnection::openSocket()
{
    if (host_.isEmpty() || port_ == 0 || socket_->state() != QAbstractSocket::UnconnectedState)
        return;
    emit statusChanged(QStringLiteral("正在连接 %1:%2…").arg(host_).arg(port_), false);
    socket_->connectToHost(host_, port_);
}

QString ClientConnection::sendRequest(const QString &type, const QJsonObject &data)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (isConnected())
        socket_->write(ev::encodeFrame(ev::makeRequest(type, requestId, data, sessionToken_)));
    else
        emit statusChanged("服务器未连接", false);
    return requestId;
}

void ClientConnection::setSessionToken(const QString &token)
{
    sessionToken_ = token;
}

bool ClientConnection::isConnected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void ClientConnection::readIncoming()
{
    decoder_.append(socket_->readAll());
    QString error;
    const auto messages = decoder_.takeAvailable(&error);
    if (!error.isEmpty()) {
        emit statusChanged(QStringLiteral("协议错误：%1").arg(error), false);
        socket_->abort();
        return;
    }
    for (const QJsonObject &message : messages)
        emit messageReceived(message);
}

void ClientConnection::scheduleReconnect()
{
    heartbeatTimer_->stop();
    sessionToken_.clear();
    emit statusChanged("与服务器断开，正在重连…", false);
    const int delay = qMin(10000, 1000 * (1 << qMin(reconnectAttempt_, 3)));
    ++reconnectAttempt_;
    reconnectTimer_->start(delay);
}
