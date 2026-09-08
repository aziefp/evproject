#include "database.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

class DatabaseTest : public QObject
{
    Q_OBJECT

private slots:
    void completeUserAndAdminFlow();
    void persistsAcrossRestart();
};

void DatabaseTest::completeUserAndAdminFlow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath("test.db"), &error), qPrintable(error));

    const DbResult login = database.loginByPhone("13900000001");
    QVERIFY2(login.ok, qPrintable(login.message));
    QCOMPARE(login.data.value("profile").toObject().value("nickname").toString(),
             QString("用户0001"));
    const qint64 userId = static_cast<qint64>(login.data.value("profile").toObject().value("id").toDouble());
    QVERIFY(userId > 0);
    QVERIFY(database.updateProfile(userId, "验收用户", "").ok);
    QVERIFY(database.recharge(userId, 50000).ok);

    const DbResult stations = database.stationList(22.543096, 114.057865);
    QVERIFY(stations.ok);
    const QJsonArray stationArray = stations.data.value("stations").toArray();
    QVERIFY(stationArray.size() >= 6);
    double previousDistance = -1;
    for (const QJsonValue &value : stationArray) {
        const double distance = value.toObject().value("distanceMeters").toDouble();
        QVERIFY(distance >= previousDistance);
        previousDistance = distance;
    }
    qint64 chargerId = 0;
    for (const QJsonValue &stationValue : stationArray) {
        const qint64 stationId = static_cast<qint64>(stationValue.toObject().value("id").toDouble());
        const DbResult detail = database.stationDetail(stationId);
        QVERIFY(detail.ok);
        for (const QJsonValue &chargerValue : detail.data.value("station").toObject().value("chargers").toArray()) {
            const QJsonObject charger = chargerValue.toObject();
            if (charger.value("status").toString() == "idle") {
                chargerId = static_cast<qint64>(charger.value("id").toDouble());
                break;
            }
        }
        if (chargerId > 0)
            break;
    }
    QVERIFY(chargerId > 0);

    const DbResult reserved = database.reserveOrder(userId, chargerId);
    QVERIFY2(reserved.ok, qPrintable(reserved.message));
    const qint64 orderId = static_cast<qint64>(reserved.data.value("order").toObject().value("id").toDouble());
    QVERIFY(!database.reserveOrder(userId, chargerId).ok);
    const DbResult secondLogin = database.loginByPhone("13900000002");
    QVERIFY(secondLogin.ok);
    const qint64 secondUserId = static_cast<qint64>(secondLogin.data.value("profile").toObject().value("id").toDouble());
    QVERIFY(!database.reserveOrder(secondUserId, chargerId).ok);
    QVERIFY(database.startOrder(userId, orderId).ok);
    QVERIFY(!database.adminReportChargerFault(chargerId).ok);
    const QJsonArray manualTick = database.tickCharging(60);
    QVERIFY(!manualTick.isEmpty());
    QJsonObject meteredOrder;
    for (const QJsonValue &value : manualTick) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == orderId) {
            meteredOrder = value.toObject();
            break;
        }
    }
    QVERIFY(!meteredOrder.isEmpty());
    QCOMPARE(static_cast<qint64>(meteredOrder.value("targetEnergyWh").toDouble()), qint64(20000));
    QVERIFY(meteredOrder.value("progressPercent").toInt() > 0);
    QVERIFY(database.stopOrder(userId, orderId).ok);
    const DbResult settled = database.settleOrder(userId, orderId);
    QVERIFY2(settled.ok, qPrintable(settled.message));
    QCOMPARE(settled.data.value("order").toObject().value("status").toString(), QString("settled"));
    const qint64 balanceAfterSettlement = static_cast<qint64>(
        settled.data.value("profile").toObject().value("balanceCents").toDouble());
    const DbResult repeatedSettlement = database.settleOrder(userId, orderId);
    QVERIFY(repeatedSettlement.ok);
    QCOMPARE(static_cast<qint64>(repeatedSettlement.data.value("profile").toObject()
                                     .value("balanceCents").toDouble()),
             balanceAfterSettlement);
    QVERIFY(database.activeOrder(userId).data.value("order").isNull());
    QVERIFY(!database.userOrders(userId).data.value("orders").toArray().isEmpty());

    const DbResult fullChargeLogin = database.loginByPhone("13900000003");
    QVERIFY(fullChargeLogin.ok);
    const qint64 fullChargeUserId = static_cast<qint64>(
        fullChargeLogin.data.value("profile").toObject().value("id").toDouble());
    QVERIFY(database.recharge(fullChargeUserId, 50000).ok);
    const DbResult fullChargeReserved = database.reserveOrder(fullChargeUserId, chargerId);
    QVERIFY(fullChargeReserved.ok);
    const qint64 fullChargeOrderId = static_cast<qint64>(
        fullChargeReserved.data.value("order").toObject().value("id").toDouble());
    QVERIFY(database.startOrder(fullChargeUserId, fullChargeOrderId).ok);
    const QJsonArray fullChargeTick = database.tickCharging(3600);
    QJsonObject fullChargeOrder;
    for (const QJsonValue &value : fullChargeTick) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == fullChargeOrderId) {
            fullChargeOrder = value.toObject();
            break;
        }
    }
    QVERIFY(!fullChargeOrder.isEmpty());
    QCOMPARE(fullChargeOrder.value("status").toString(), QString("pending_settlement"));
    QCOMPARE(static_cast<qint64>(fullChargeOrder.value("energyWh").toDouble()), qint64(20000));
    QCOMPARE(fullChargeOrder.value("progressPercent").toInt(), 100);
    QVERIFY(database.settleOrder(fullChargeUserId, fullChargeOrderId).ok);

    const DbResult lowBalanceLogin = database.loginByPhone("13900000004");
    QVERIFY(lowBalanceLogin.ok);
    const qint64 lowBalanceUserId = static_cast<qint64>(
        lowBalanceLogin.data.value("profile").toObject().value("id").toDouble());
    QVERIFY(database.recharge(lowBalanceUserId, 1).ok);
    const DbResult lowBalanceReserved = database.reserveOrder(lowBalanceUserId, chargerId);
    QVERIFY(lowBalanceReserved.ok);
    const qint64 lowBalanceOrderId = static_cast<qint64>(
        lowBalanceReserved.data.value("order").toObject().value("id").toDouble());
    QVERIFY(database.startOrder(lowBalanceUserId, lowBalanceOrderId).ok);
    const QJsonArray lowBalanceTick = database.tickCharging(3600);
    QVERIFY(!lowBalanceTick.isEmpty());
    QJsonObject lowBalanceOrder;
    for (const QJsonValue &value : lowBalanceTick) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == lowBalanceOrderId) {
            lowBalanceOrder = value.toObject();
            break;
        }
    }
    QVERIFY(!lowBalanceOrder.isEmpty());
    QCOMPARE(lowBalanceOrder.value("status").toString(), QString("pending_settlement"));
    QCOMPARE(static_cast<qint64>(lowBalanceOrder.value("amountCents").toDouble()), qint64(1));
    const DbResult lowBalanceSettled = database.settleOrder(lowBalanceUserId, lowBalanceOrderId);
    QVERIFY(lowBalanceSettled.ok);
    QCOMPARE(static_cast<qint64>(lowBalanceSettled.data.value("profile").toObject()
                                     .value("balanceCents").toDouble()),
             qint64(0));

    QVERIFY(database.adminLogin("admin", "123456").ok);
    QVERIFY(!database.adminLogin("admin", "bad").ok);
    const int beforeStations = database.adminStations().data.value("stations").toArray().size();
    const DbResult added = database.adminAddStation({
        {"name", "验收新增站"}, {"address", "深圳市测试路 1 号"},
        {"latitude", 22.55}, {"longitude", 114.06},
        {"priceCentsPerKwh", 149}, {"chargerCount", 3}});
    QVERIFY2(added.ok, qPrintable(added.message));
    QCOMPARE(database.adminStations().data.value("stations").toArray().size(), beforeStations + 1);
    const QJsonObject dashboard = database.adminDashboard().data;
    QCOMPARE(dashboard.value("trend").toArray().size(), 30);
    QCOMPARE(dashboard.value("stationRevenue").toArray().size(), 5);
    QVERIFY(!dashboard.value("faultChargers").toArray().isEmpty());
    QVERIFY(!database.adminUsers("1390000").data.value("users").toArray().isEmpty());
    QVERIFY(!database.adminOrders().data.value("orders").toArray().isEmpty());
    const DbResult settledOrders = database.adminOrders("settled");
    QVERIFY(settledOrders.ok);
    QVERIFY(!settledOrders.data.value("orders").toArray().isEmpty());
    for (const QJsonValue &value : settledOrders.data.value("orders").toArray())
        QCOMPARE(value.toObject().value("status").toString(), QString("settled"));
    QVERIFY(!database.adminOrders("unknown").ok);

    const DbResult idleChargers = database.adminChargers("idle");
    QVERIFY(idleChargers.ok);
    QVERIFY(!idleChargers.data.value("chargers").toArray().isEmpty());
    for (const QJsonValue &value : idleChargers.data.value("chargers").toArray())
        QCOMPARE(value.toObject().value("status").toString(), QString("idle"));
    QVERIFY(!database.adminChargers("unknown").ok);

    QVERIFY(database.adminReportChargerFault(chargerId).ok);
    QVERIFY(!database.adminReportChargerFault(chargerId).ok);
    QString faultStatus;
    for (const QJsonValue &value : database.adminChargers().data.value("chargers").toArray()) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == chargerId) {
            faultStatus = value.toObject().value("status").toString();
            break;
        }
    }
    QCOMPARE(faultStatus, QString("fault"));
    QVERIFY(database.adminRestartCharger(chargerId).ok);
    QString restartStatus;
    for (const QJsonValue &value : database.adminChargers().data.value("chargers").toArray()) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == chargerId) {
            restartStatus = value.toObject().value("status").toString();
            break;
        }
    }
    QCOMPARE(restartStatus, QString("restarting"));
    QVERIFY(database.finishRestart(chargerId).ok);
    QString finishedStatus;
    for (const QJsonValue &value : database.adminChargers().data.value("chargers").toArray()) {
        if (static_cast<qint64>(value.toObject().value("id").toDouble()) == chargerId) {
            finishedStatus = value.toObject().value("status").toString();
            break;
        }
    }
    QCOMPARE(finishedStatus, QString("idle"));
    QVERIFY(database.adminSetUserStatus(userId, "frozen").ok);
    QVERIFY(!database.loginByPhone("13900000001").ok);
    QVERIFY(database.adminSetUserStatus(userId, "active").ok);
}

void DatabaseTest::persistsAcrossRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("persistent.db");
    qint64 userId = 0;
    {
        Database database;
        QString error;
        QVERIFY2(database.open(path, &error), qPrintable(error));
        const DbResult login = database.loginByPhone("13900000003");
        QVERIFY(login.ok);
        userId = static_cast<qint64>(login.data.value("profile").toObject().value("id").toDouble());
        QVERIFY(database.recharge(userId, 12345).ok);
    }
    {
        Database database;
        QString error;
        QVERIFY2(database.open(path, &error), qPrintable(error));
        const DbResult profile = database.getProfile(userId);
        QVERIFY(profile.ok);
        QCOMPARE(static_cast<qint64>(profile.data.value("profile").toObject().value("balanceCents").toDouble()), qint64(12345));
    }
}

QTEST_MAIN(DatabaseTest)
#include "test_database.moc"
