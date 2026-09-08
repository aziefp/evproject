#include "serverworker.h"

#include "database.h"

#include <QFile>
#include <QHostAddress>
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

ServerWorker::ServerWorker(QObject *parent)
    : QObject(parent)
{
}

ServerWorker::~ServerWorker()
{
    delete database_;
}

void ServerWorker::start(const QString &listenAddress, quint16 port, const QString &databasePath,
                         const QString &keyFile, int simulationSpeed)
{
    database_ = new Database;
    QString error;
    if (!database_->open(databasePath, &error)) {
        emit started(false, QStringLiteral("数据库启动失败：%1").arg(error), 0);
        return;
    }

    QFile file(keyFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        mapKey_ = QString::fromUtf8(file.readAll()).trimmed();

    network_ = new QNetworkAccessManager(this);
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &ServerWorker::acceptConnections);
    QHostAddress address;
    if (listenAddress == "0.0.0.0")
        address = QHostAddress::AnyIPv4;
    else if (listenAddress == "::")
        address = QHostAddress::AnyIPv6;
    else if (!address.setAddress(listenAddress)) {
        emit started(false, QStringLiteral("监听地址无效：%1").arg(listenAddress), 0);
        return;
    }
    if (!server_->listen(address, port)) {
        emit started(false, QStringLiteral("监听失败：%1").arg(server_->errorString()), 0);
        return;
    }

    simulationSpeed_ = qBound(1, simulationSpeed, 3600);
    chargingTimer_ = new QTimer(this);
    chargingTimer_->setInterval(1000);
    connect(chargingTimer_, &QTimer::timeout, this, &ServerWorker::tickCharging);
    chargingTimer_->start();

    idleTimer_ = new QTimer(this);
    idleTimer_->setInterval(5000);
    connect(idleTimer_, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<QTcpSocket *> stale;
        for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
            if (now - it.value().lastSeenMs > 45000)
                stale.append(it.key());
        }
        for (QTcpSocket *socket : stale)
            socket->disconnectFromHost();
    });
    idleTimer_->start();

    emit statusChanged(QStringLiteral("服务器正在监听 %1:%2").arg(listenAddress).arg(server_->serverPort()));
    emit started(true, "服务器已启动", server_->serverPort());
}

void ServerWorker::stop()
{
    if (chargingTimer_)
        chargingTimer_->stop();
    if (idleTimer_)
        idleTimer_->stop();
    for (QTcpSocket *socket : clients_.keys()) {
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    clients_.clear();
    if (server_)
        server_->close();
}

void ServerWorker::acceptConnections()
{
    while (server_->hasPendingConnections()) {
        QTcpSocket *socket = server_->nextPendingConnection();
        ClientState state;
        state.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
        clients_.insert(socket, state);
        connect(socket, &QTcpSocket::readyRead, this, &ServerWorker::readClient);
        connect(socket, &QTcpSocket::disconnected, this, &ServerWorker::removeClient);
        emit statusChanged(QStringLiteral("客户端已连接：%1:%2（在线 %3）")
                               .arg(socket->peerAddress().toString())
                               .arg(socket->peerPort())
                               .arg(clients_.size()));
    }
}

void ServerWorker::readClient()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !clients_.contains(socket))
        return;
    ClientState &state = clients_[socket];
    state.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
    state.decoder.append(socket->readAll());
    QString error;
    const auto messages = state.decoder.takeAvailable(&error);
    if (!error.isEmpty()) {
        send(socket, ev::makeError({}, "PROTOCOL_INVALID_FRAME", error));
        socket->disconnectFromHost();
        return;
    }
    for (const QJsonObject &message : messages)
        dispatch(socket, message);
}

void ServerWorker::removeClient()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;
    clients_.remove(socket);
    socket->deleteLater();
    emit statusChanged(QStringLiteral("客户端已断开（在线 %1）").arg(clients_.size()));
}

void ServerWorker::send(QTcpSocket *socket, const QJsonObject &message)
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState)
        socket->write(ev::encodeFrame(message));
}

void ServerWorker::sendResponse(QTcpSocket *socket, const QJsonObject &request, const QJsonObject &message)
{
    const QString requestId = request.value("requestId").toString();
    if (!requestId.isEmpty() && clients_.contains(socket)) {
        ClientState &state = clients_[socket];
        if (state.responseCache.size() >= 200)
            state.responseCache.clear();
        state.responseCache.insert(requestId, message);
    }
    send(socket, message);
}

void ServerWorker::sendDbResult(QTcpSocket *socket, const QJsonObject &request,
                                const QString &resultType, const DbResult &result)
{
    if (result.ok)
        sendResponse(socket, request, ev::makeSuccess(request, resultType, result.data));
    else
        sendResponse(socket, request, ev::makeError(request, result.code, result.message));
}

void ServerWorker::sendOrderResult(QTcpSocket *socket, const QJsonObject &request,
                                   const QString &resultType, const DbResult &result)
{
    sendDbResult(socket, request, resultType, result);
    if (!result.ok)
        return;
    const QJsonObject order = result.data.value("order").toObject();
    if (!order.isEmpty())
        broadcastOrder(order);
    const QJsonObject profile = result.data.value("profile").toObject();
    if (!profile.isEmpty())
        broadcastProfile(static_cast<qint64>(profile.value("id").toDouble()), profile);
    broadcastStationsChanged();
}

bool ServerWorker::authenticate(QTcpSocket *socket, const QJsonObject &request)
{
    const ClientState state = clients_.value(socket);
    if (state.userId <= 0 || state.sessionToken.isEmpty()
        || request.value("sessionToken").toString() != state.sessionToken) {
        send(socket, ev::makeError(request, "UNAUTHENTICATED", "请先登录"));
        return false;
    }
    return true;
}

void ServerWorker::dispatch(QTcpSocket *socket, const QJsonObject &request)
{
    const QString requestId = request.value("requestId").toString();
    if (!requestId.isEmpty() && clients_.value(socket).responseCache.contains(requestId)) {
        send(socket, clients_.value(socket).responseCache.value(requestId));
        return;
    }
    if (request.value("v").toInt() != EV_PROTOCOL_VERSION) {
        send(socket, ev::makeError(request, "PROTOCOL_UNSUPPORTED_VERSION", "协议版本不受支持"));
        return;
    }
    const QString type = request.value("type").toString();
    const QJsonObject data = request.value("data").toObject();

    if (type == "ping") {
        send(socket, ev::makeSuccess(request, "pong"));
        return;
    }
    if (type == "session.loginByPhone") {
        // A failed re-login must never leave an older authenticated session active.
        clients_[socket].userId = 0;
        clients_[socket].sessionToken.clear();
        const QString phone = data.value("phone").toString();
        if (!QRegularExpression("^1[0-9]{10}$").match(phone).hasMatch()) {
            send(socket, ev::makeError(request, "REQUEST_INVALID", "请输入正确的 11 位手机号"));
            return;
        }
        DbResult result = database_->loginByPhone(phone);
        if (result.ok) {
            const qint64 userId = static_cast<qint64>(result.data.value("profile").toObject().value("id").toDouble());
            clients_[socket].userId = userId;
            clients_[socket].sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
            result.data.insert("sessionToken", clients_[socket].sessionToken);
        }
        sendDbResult(socket, request, "session.loginByPhone.result", result);
        return;
    }
    if (!authenticate(socket, request))
        return;

    if (type == "session.logout") {
        sendResponse(socket, request, ev::makeSuccess(request, "session.logout.result"));
        clients_[socket].userId = 0;
        clients_[socket].sessionToken.clear();
        clients_[socket].responseCache.clear();
        return;
    }

    const qint64 userId = clients_.value(socket).userId;
    if (type == "user.getProfile")
        sendDbResult(socket, request, "user.getProfile.result", database_->getProfile(userId));
    else if (type == "user.updateProfile") {
        const DbResult result = database_->updateProfile(
            userId, data.value("nickname").toString(), data.value("avatarBase64").toString());
        sendDbResult(socket, request, "user.updateProfile.result", result);
        if (result.ok)
            broadcastProfile(userId, result.data.value("profile").toObject());
    } else if (type == "wallet.recharge") {
        const DbResult result = database_->recharge(
            userId, static_cast<qint64>(data.value("amountCents").toDouble()));
        sendDbResult(socket, request, "wallet.recharge.result", result);
        if (result.ok)
            broadcastProfile(userId, result.data.value("profile").toObject());
    } else if (type == "location.geocode")
        geocode(socket, request, data.value("address").toString().trimmed());
    else if (type == "station.list")
        sendDbResult(socket, request, "station.list.result",
                     database_->stationList(data.value("latitude").toDouble(), data.value("longitude").toDouble()));
    else if (type == "station.get")
        sendDbResult(socket, request, "station.get.result",
                     database_->stationDetail(static_cast<qint64>(data.value("stationId").toDouble())));
    else if (type == "order.getActive")
        sendDbResult(socket, request, "order.getActive.result", database_->activeOrder(userId));
    else if (type == "order.listMine")
        sendDbResult(socket, request, "order.listMine.result", database_->userOrders(userId));
    else if (type == "order.reserve")
        sendOrderResult(socket, request, "order.reserve.result",
                        database_->reserveOrder(userId, static_cast<qint64>(data.value("chargerId").toDouble())));
    else if (type == "order.startCharging")
        sendOrderResult(socket, request, "order.startCharging.result",
                        database_->startOrder(userId, static_cast<qint64>(data.value("orderId").toDouble())));
    else if (type == "order.cancelReservation")
        sendOrderResult(socket, request, "order.cancelReservation.result",
                        database_->cancelOrder(userId, static_cast<qint64>(data.value("orderId").toDouble())));
    else if (type == "order.stopCharging")
        sendOrderResult(socket, request, "order.stopCharging.result",
                        database_->stopOrder(userId, static_cast<qint64>(data.value("orderId").toDouble())));
    else if (type == "order.settle")
        sendOrderResult(socket, request, "order.settle.result",
                        database_->settleOrder(userId, static_cast<qint64>(data.value("orderId").toDouble())));
    else
        send(socket, ev::makeError(request, "REQUEST_INVALID", "未知请求类型"));
}

void ServerWorker::geocode(QTcpSocket *socket, const QJsonObject &request, const QString &address)
{
    if (address.isEmpty()) {
        send(socket, ev::makeError(request, "REQUEST_INVALID", "地址不能为空"));
        return;
    }
    if (mapKey_.isEmpty()) {
        send(socket, ev::makeError(request, "MAP_SERVICE_UNAVAILABLE", "腾讯地图 Key 未配置"));
        return;
    }
    QUrl url("https://apis.map.qq.com/ws/geocoder/v1/");
    QUrlQuery query;
    query.addQueryItem("address", address);
    query.addQueryItem("key", mapKey_);
    url.setQuery(query);

    QPointer<QTcpSocket> guardedSocket(socket);
    QNetworkReply *reply = network_->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, guardedSocket, request, address]() {
        const QByteArray body = reply->readAll();
        const bool networkOk = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!guardedSocket)
            return;
        if (!networkOk) {
            send(guardedSocket, ev::makeError(request, "MAP_SERVICE_UNAVAILABLE", "地址解析网络请求失败", true));
            return;
        }
        QJsonParseError parseError;
        const QJsonObject payload = QJsonDocument::fromJson(body, &parseError).object();
        if (parseError.error != QJsonParseError::NoError || payload.value("status").toInt(-1) != 0) {
            send(guardedSocket, ev::makeError(request, "MAP_SERVICE_UNAVAILABLE",
                                              payload.value("message").toString("地址解析失败"), true));
            return;
        }
        const QJsonObject result = payload.value("result").toObject();
        const QJsonObject location = result.value("location").toObject();
        send(guardedSocket, ev::makeSuccess(request, "location.geocode.result",
                                            {{"address", address}, {"title", result.value("title")},
                                             {"latitude", location.value("lat")}, {"longitude", location.value("lng")}}));
    });
}

void ServerWorker::tickCharging()
{
    if (!database_)
        return;
    const QJsonArray changed = database_->tickCharging(simulationSpeed_);
    for (const QJsonValue &value : changed) {
        const QJsonObject order = value.toObject();
        const bool autoStopped = order.value("status").toString() == "pending_settlement";
        broadcastOrder(order, autoStopped ? "state" : "meter");
        if (autoStopped)
            broadcastStationsChanged();
    }
}

void ServerWorker::broadcastOrder(const QJsonObject &order, const QString &change)
{
    const qint64 userId = static_cast<qint64>(order.value("userId").toDouble());
    const QJsonObject event{{"v", EV_PROTOCOL_VERSION}, {"type", "order.updated"},
                            {"eventId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                            {"data", QJsonObject{{"order", order}, {"change", change}}}};
    for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
        if (it.value().userId == userId)
            send(it.key(), event);
    }
}

void ServerWorker::broadcastStationsChanged()
{
    const QJsonObject event{{"v", EV_PROTOCOL_VERSION}, {"type", "stations.changed"},
                            {"eventId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                            {"data", QJsonObject{}}};
    for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
        if (it.value().userId > 0)
            send(it.key(), event);
    }
}

void ServerWorker::broadcastProfile(qint64 userId, const QJsonObject &profile)
{
    const QJsonObject event{{"v", EV_PROTOCOL_VERSION}, {"type", "user.updated"},
                            {"eventId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                            {"data", QJsonObject{{"profile", profile}}}};
    for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
        if (it.value().userId == userId)
            send(it.key(), event);
    }
}

void ServerWorker::broadcastUserStatus(qint64 userId, const QString &status)
{
    const QJsonObject event{{"v", EV_PROTOCOL_VERSION}, {"type", "user.statusChanged"},
                            {"eventId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                            {"data", QJsonObject{{"status", status}}}};
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.value().userId != userId)
            continue;
        send(it.key(), event);
        if (status == "frozen") {
            it.value().userId = 0;
            it.value().sessionToken.clear();
        }
    }
}

void ServerWorker::handleAdminCommand(quint64 requestId, const QString &action, const QJsonObject &data)
{
    if (!database_) {
        emit adminResult(requestId, action, false, {}, "数据库尚未启动");
        return;
    }
    DbResult result;
    if (action == "admin.login")
        result = database_->adminLogin(data.value("username").toString(), data.value("password").toString());
    else if (action == "dashboard.get")
        result = database_->adminDashboard();
    else if (action == "stations.list")
        result = database_->adminStations();
    else if (action == "station.get")
        result = database_->stationDetail(static_cast<qint64>(data.value("stationId").toDouble()));
    else if (action == "chargers.list")
        result = database_->adminChargers(data.value("statusFilter").toString());
    else if (action == "users.list")
        result = database_->adminUsers(data.value("phoneFilter").toString());
    else if (action == "orders.list")
        result = database_->adminOrders(data.value("statusFilter").toString());
    else if (action == "station.add")
        result = database_->adminAddStation(data);
    else if (action == "user.setStatus")
        result = database_->adminSetUserStatus(static_cast<qint64>(data.value("userId").toDouble()), data.value("status").toString());
    else if (action == "charger.reportFault")
        result = database_->adminReportChargerFault(
            static_cast<qint64>(data.value("chargerId").toDouble()));
    else if (action == "charger.restart") {
        const qint64 chargerId = static_cast<qint64>(data.value("chargerId").toDouble());
        result = database_->adminRestartCharger(chargerId);
        if (result.ok) {
            QTimer::singleShot(1500, this, [this, chargerId]() {
                const DbResult finished = database_->finishRestart(chargerId);
                if (finished.ok)
                    broadcastStationsChanged();
                emit statusChanged(QStringLiteral("电桩 %1 远程重启完成").arg(chargerId));
            });
        }
    } else if (action == "order.settle")
        result = database_->adminSettleOrder(static_cast<qint64>(data.value("orderId").toDouble()));
    else
        result = {false, "REQUEST_INVALID", "未知管理操作", {}};

    if (result.ok) {
        if (action == "station.add" || action == "charger.reportFault" || action == "charger.restart")
            broadcastStationsChanged();
        else if (action == "user.setStatus")
            broadcastUserStatus(static_cast<qint64>(data.value("userId").toDouble()),
                                data.value("status").toString());
        else if (action == "order.settle") {
            const QJsonObject order = result.data.value("order").toObject();
            if (!order.isEmpty())
                broadcastOrder(order);
            const QJsonObject profile = result.data.value("profile").toObject();
            if (!profile.isEmpty())
                broadcastProfile(static_cast<qint64>(profile.value("id").toDouble()), profile);
            broadcastStationsChanged();
        }
    }

    emit adminResult(requestId, action, result.ok, result.data,
                     result.ok ? QString() : result.message);
}
