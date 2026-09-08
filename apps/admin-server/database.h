#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

struct DbResult
{
    bool ok = false;
    QString code;
    QString message;
    QJsonObject data;
};

class Database
{
public:
    Database();
    ~Database();

    bool open(const QString &path, QString *error);

    DbResult loginByPhone(const QString &phone);
    DbResult getProfile(qint64 userId);
    DbResult updateProfile(qint64 userId, const QString &nickname, const QString &avatarBase64);
    DbResult recharge(qint64 userId, qint64 amountCents);

    DbResult stationList(double latitude, double longitude);
    DbResult stationDetail(qint64 stationId);

    DbResult activeOrder(qint64 userId);
    DbResult userOrders(qint64 userId);
    DbResult reserveOrder(qint64 userId, qint64 chargerId);
    DbResult startOrder(qint64 userId, qint64 orderId);
    DbResult cancelOrder(qint64 userId, qint64 orderId);
    DbResult stopOrder(qint64 userId, qint64 orderId);
    DbResult settleOrder(qint64 userId, qint64 orderId);
    QJsonArray tickCharging(int simulationSpeed);

    DbResult adminLogin(const QString &username, const QString &password);
    DbResult adminDashboard();
    DbResult adminStations();
    DbResult adminChargers(const QString &statusFilter = {});
    DbResult adminUsers(const QString &phoneFilter);
    DbResult adminOrders(const QString &statusFilter = {});
    DbResult adminAddStation(const QJsonObject &data);
    DbResult adminSetUserStatus(qint64 userId, const QString &status);
    DbResult adminReportChargerFault(qint64 chargerId);
    DbResult adminRestartCharger(qint64 chargerId);
    DbResult finishRestart(qint64 chargerId);
    DbResult adminSettleOrder(qint64 orderId);

private:
    bool createSchema(QString *error);
    bool seed(QString *error);
    QJsonObject orderById(qint64 orderId);
    QJsonObject profileObject(qint64 userId);
    DbResult failure(const QString &code, const QString &message) const;
    DbResult success(const QJsonObject &data = {}) const;
    QString lastError() const;

    QString connectionName_;
    QSqlDatabase db_;
};
