#include "database.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>
#include <QtMath>

namespace {

constexpr qint64 kDemoTargetEnergyWh = 20000;

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double earthRadius = 6371000.0;
    const double phi1 = qDegreesToRadians(lat1);
    const double phi2 = qDegreesToRadians(lat2);
    const double dPhi = qDegreesToRadians(lat2 - lat1);
    const double dLambda = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dPhi / 2) * qSin(dPhi / 2)
        + qCos(phi1) * qCos(phi2) * qSin(dLambda / 2) * qSin(dLambda / 2);
    return earthRadius * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}

QString passwordHash(const QString &salt, const QString &password)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        (salt + ':' + password).toUtf8(), QCryptographicHash::Sha256).toHex());
}

double jsonNumber(qint64 value)
{
    return static_cast<double>(value);
}

bool execSql(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;
    if (error)
        *error = query.lastError().text();
    return false;
}

} // namespace

Database::Database()
    : connectionName_(QStringLiteral("ev-db-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

Database::~Database()
{
    if (db_.isValid())
        db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName_);
}

bool Database::open(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建数据库目录：%1").arg(info.absolutePath());
        return false;
    }

    db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db_.setDatabaseName(info.absoluteFilePath());
    if (!db_.open()) {
        if (error)
            *error = db_.lastError().text();
        return false;
    }

    if (!execSql(db_, "PRAGMA foreign_keys = ON", error)
        || !execSql(db_, "PRAGMA journal_mode = WAL", error)
        || !execSql(db_, "PRAGMA busy_timeout = 5000", error))
        return false;

    return createSchema(error) && seed(error);
}

bool Database::createSchema(QString *error)
{
    const QStringList statements{
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, phone TEXT NOT NULL UNIQUE, nickname TEXT NOT NULL, "
        "avatar_base64 TEXT NOT NULL DEFAULT '', balance_cents INTEGER NOT NULL DEFAULT 0 CHECK(balance_cents >= 0), "
        "status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active','frozen')), "
        "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",

        "CREATE TABLE IF NOT EXISTS admins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE, password_salt TEXT NOT NULL, "
        "password_hash TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'active', created_at INTEGER NOT NULL, "
        "last_login_at INTEGER)",

        "CREATE TABLE IF NOT EXISTS stations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, address TEXT NOT NULL, "
        "latitude REAL NOT NULL, longitude REAL NOT NULL, price_cents_per_kwh INTEGER NOT NULL CHECK(price_cents_per_kwh > 0), "
        "enabled INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",

        "CREATE TABLE IF NOT EXISTS chargers ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT NOT NULL UNIQUE, station_id INTEGER NOT NULL REFERENCES stations(id), "
        "type TEXT NOT NULL CHECK(type IN ('fast','slow')), power_watts INTEGER NOT NULL CHECK(power_watts > 0), "
        "status TEXT NOT NULL CHECK(status IN ('idle','reserved','charging','fault','restarting','offline')), "
        "total_sessions INTEGER NOT NULL DEFAULT 0, total_duration_seconds INTEGER NOT NULL DEFAULT 0, "
        "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",

        "CREATE TABLE IF NOT EXISTS orders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, order_no TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES users(id), "
        "station_id INTEGER NOT NULL REFERENCES stations(id), charger_id INTEGER NOT NULL REFERENCES chargers(id), "
        "status TEXT NOT NULL CHECK(status IN ('reserved','charging','pending_settlement','settled','cancelled')), "
        "reserved_at INTEGER NOT NULL, started_at INTEGER, ended_at INTEGER, settled_at INTEGER, "
        "power_watts_snapshot INTEGER NOT NULL, price_cents_snapshot INTEGER NOT NULL, energy_wh INTEGER NOT NULL DEFAULT 0, "
        "amount_cents INTEGER NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)",

        "CREATE UNIQUE INDEX IF NOT EXISTS ux_active_order_user ON orders(user_id) "
        "WHERE status IN ('reserved','charging','pending_settlement')",
        "CREATE UNIQUE INDEX IF NOT EXISTS ux_active_order_charger ON orders(charger_id) "
        "WHERE status IN ('reserved','charging','pending_settlement')",

        "CREATE TABLE IF NOT EXISTS wallet_transactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, user_id INTEGER NOT NULL REFERENCES users(id), "
        "order_id INTEGER REFERENCES orders(id), type TEXT NOT NULL, amount_cents INTEGER NOT NULL, "
        "balance_after_cents INTEGER NOT NULL, created_at INTEGER NOT NULL, note TEXT NOT NULL DEFAULT '')",

        "CREATE TABLE IF NOT EXISTS operation_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, admin_id INTEGER, action TEXT NOT NULL, target_type TEXT NOT NULL, "
        "target_id INTEGER, detail_json TEXT NOT NULL DEFAULT '{}', created_at INTEGER NOT NULL)"
    };

    for (const QString &statement : statements) {
        if (!execSql(db_, statement, error))
            return false;
    }
    return true;
}

bool Database::seed(QString *error)
{
    QSqlQuery count(db_);
    if (!count.exec("SELECT COUNT(*) FROM stations") || !count.next()) {
        if (error)
            *error = count.lastError().text();
        return false;
    }
    if (count.value(0).toInt() > 0)
        return true;

    if (!db_.transaction()) {
        if (error)
            *error = db_.lastError().text();
        return false;
    }

    const qint64 now = nowSeconds();
    QSqlQuery query(db_);
    query.prepare("INSERT INTO admins(username,password_salt,password_hash,status,created_at) VALUES(?,?,?,?,?)");
    const QString salt = "ev-course-admin";
    query.addBindValue("admin");
    query.addBindValue(salt);
    query.addBindValue(passwordHash(salt, "123456"));
    query.addBindValue("active");
    query.addBindValue(now);
    if (!query.exec()) {
        if (error) *error = query.lastError().text();
        db_.rollback();
        return false;
    }

    struct StationSeed { const char *name; const char *address; double lat; double lon; int price; int count; };
    const QList<StationSeed> stations{
        {"深圳市民中心充电站", "深圳市福田区福中三路市民中心停车场", 22.543687, 114.059625, 120, 6},
        {"深圳湾公园充电站", "深圳市南山区滨海大道深圳湾公园", 22.515502, 113.945931, 150, 4},
        {"南山科技园充电站", "深圳市南山区高新南一道", 22.535536, 113.950671, 130, 8},
        {"福田CBD充电站", "深圳市福田区益田路卓越世纪中心", 22.535189, 114.054403, 160, 6},
        {"宝安中心充电站", "深圳市宝安区新湖路宝安中心广场", 22.554632, 113.883053, 110, 4},
        {"龙岗大运中心充电站", "深圳市龙岗区龙翔大道大运中心", 22.695401, 114.228936, 100, 4},
    };

    query.prepare("INSERT INTO stations(name,address,latitude,longitude,price_cents_per_kwh,created_at,updated_at) "
                  "VALUES(?,?,?,?,?,?,?)");
    for (const auto &station : stations) {
        query.bindValue(0, station.name);
        query.bindValue(1, station.address);
        query.bindValue(2, station.lat);
        query.bindValue(3, station.lon);
        query.bindValue(4, station.price);
        query.bindValue(5, now);
        query.bindValue(6, now);
        if (!query.exec()) {
            if (error) *error = query.lastError().text();
            db_.rollback();
            return false;
        }
    }

    query.prepare("INSERT INTO chargers(code,station_id,type,power_watts,status,total_sessions,total_duration_seconds,created_at,updated_at) "
                  "VALUES(?,?,?,?,?,?,?,?,?)");
    int chargerId = 0;
    for (int stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
        for (int i = 1; i <= stations.at(stationIndex).count; ++i) {
            ++chargerId;
            const bool fast = i <= qMax(1, stations.at(stationIndex).count * 2 / 3);
            QString status = "idle";
            if (chargerId == 3 || chargerId == 10 || chargerId == 20)
                status = "fault";
            query.bindValue(0, QStringLiteral("SZ%1-%2").arg(stationIndex + 1, 3, 10, QLatin1Char('0')).arg(i, 2, 10, QLatin1Char('0')));
            query.bindValue(1, stationIndex + 1);
            query.bindValue(2, fast ? "fast" : "slow");
            query.bindValue(3, fast ? 120000 : 7000);
            query.bindValue(4, status);
            query.bindValue(5, 120 + chargerId * 13);
            query.bindValue(6, 7200 + chargerId * 389);
            query.bindValue(7, now);
            query.bindValue(8, now);
            if (!query.exec()) {
                if (error) *error = query.lastError().text();
                db_.rollback();
                return false;
            }
        }
    }

    query.prepare("INSERT INTO users(phone,nickname,balance_cents,status,created_at,updated_at) VALUES(?,?,?,?,?,?)");
    for (int i = 1; i <= 8; ++i) {
        const QString phone = QStringLiteral("138001380%1").arg(i, 2, 10, QLatin1Char('0'));
        query.bindValue(0, phone);
        query.bindValue(1, QStringLiteral("用户%1").arg(phone.right(4)));
        query.bindValue(2, i == 4 ? 500 : 5000 + i * 1500);
        query.bindValue(3, i == 6 ? "frozen" : "active");
        query.bindValue(4, now - i * 86400);
        query.bindValue(5, now);
        if (!query.exec()) {
            if (error) *error = query.lastError().text();
            db_.rollback();
            return false;
        }
    }

    query.prepare("INSERT INTO orders(order_no,user_id,station_id,charger_id,status,reserved_at,started_at,ended_at,settled_at,"
                  "power_watts_snapshot,price_cents_snapshot,energy_wh,amount_cents,created_at,updated_at) "
                  "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (int i = 0; i < 60; ++i) {
        const int userId = i % 8 + 1;
        const int stationId = i % 6 + 1;
        const int charger = (i % 32) + 1;
        const qint64 ended = now - (i % 30) * 86400 - (i % 8) * 3600;
        const qint64 started = ended - 900 - (i % 5) * 600;
        const int energyWh = 12000 + (i % 9) * 3100;
        const int price = stations.at(stationId - 1).price;
        const int amount = (energyWh * price + 500) / 1000;
        query.bindValue(0, QStringLiteral("H%1%2").arg(ended).arg(i, 3, 10, QLatin1Char('0')));
        query.bindValue(1, userId);
        query.bindValue(2, stationId);
        query.bindValue(3, charger);
        query.bindValue(4, "settled");
        query.bindValue(5, started - 120);
        query.bindValue(6, started);
        query.bindValue(7, ended);
        query.bindValue(8, ended + 30);
        query.bindValue(9, charger <= 22 ? 120000 : 7000);
        query.bindValue(10, price);
        query.bindValue(11, energyWh);
        query.bindValue(12, amount);
        query.bindValue(13, started - 120);
        query.bindValue(14, ended + 30);
        if (!query.exec()) {
            if (error) *error = query.lastError().text();
            db_.rollback();
            return false;
        }
    }

    const auto insertActive = [&](int userId, int stationId, int charger, const QString &status, int energyWh) {
        QSqlQuery active(db_);
        active.prepare("INSERT INTO orders(order_no,user_id,station_id,charger_id,status,reserved_at,started_at,ended_at,"
                       "power_watts_snapshot,price_cents_snapshot,energy_wh,amount_cents,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        active.addBindValue(QStringLiteral("A%1%2").arg(now).arg(userId));
        active.addBindValue(userId);
        active.addBindValue(stationId);
        active.addBindValue(charger);
        active.addBindValue(status);
        active.addBindValue(now - 600);
        active.addBindValue(now - 480);
        active.addBindValue(status == "pending_settlement" ? now - 30 : QVariant());
        active.addBindValue(120000);
        active.addBindValue(stations.at(stationId - 1).price);
        active.addBindValue(energyWh);
        active.addBindValue((energyWh * stations.at(stationId - 1).price + 500) / 1000);
        active.addBindValue(now - 600);
        active.addBindValue(now);
        return active.exec();
    };

    if (!insertActive(2, 1, 2, "charging", 8000)
        || !insertActive(3, 1, 4, "pending_settlement", 18000)
        || !execSql(db_, "UPDATE chargers SET status='charging' WHERE id=2", error)
        || !execSql(db_, "UPDATE chargers SET status='reserved' WHERE id=4", error)) {
        if (error && error->isEmpty()) *error = db_.lastError().text();
        db_.rollback();
        return false;
    }

    if (!db_.commit()) {
        if (error) *error = db_.lastError().text();
        return false;
    }
    return true;
}

DbResult Database::failure(const QString &code, const QString &message) const
{
    return {false, code, message, {}};
}

DbResult Database::success(const QJsonObject &data) const
{
    return {true, {}, {}, data};
}

QString Database::lastError() const
{
    return db_.lastError().text();
}

QJsonObject Database::profileObject(qint64 userId)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id,phone,nickname,avatar_base64,balance_cents,status,created_at FROM users WHERE id=?");
    query.addBindValue(userId);
    if (!query.exec() || !query.next())
        return {};
    return {
        {"id", jsonNumber(query.value(0).toLongLong())},
        {"phone", query.value(1).toString()},
        {"nickname", query.value(2).toString()},
        {"avatarBase64", query.value(3).toString()},
        {"balanceCents", jsonNumber(query.value(4).toLongLong())},
        {"status", query.value(5).toString()},
        {"createdAt", jsonNumber(query.value(6).toLongLong())},
    };
}

DbResult Database::loginByPhone(const QString &phone)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id,status FROM users WHERE phone=?");
    query.addBindValue(phone);
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());

    qint64 userId = 0;
    if (query.next()) {
        if (query.value(1).toString() == "frozen")
            return failure("USER_FROZEN", "账号已被冻结，请联系管理员");
        userId = query.value(0).toLongLong();
    } else {
        query.prepare("INSERT INTO users(phone,nickname,balance_cents,status,created_at,updated_at) VALUES(?,?,?,?,?,?)");
        const qint64 now = nowSeconds();
        query.addBindValue(phone);
        query.addBindValue(QStringLiteral("用户%1").arg(phone.right(4)));
        query.addBindValue(0);
        query.addBindValue("active");
        query.addBindValue(now);
        query.addBindValue(now);
        if (!query.exec())
            return failure("DATABASE_UNAVAILABLE", query.lastError().text());
        userId = query.lastInsertId().toLongLong();
    }

    return success({{"profile", profileObject(userId)}});
}

DbResult Database::getProfile(qint64 userId)
{
    const QJsonObject profile = profileObject(userId);
    return profile.isEmpty() ? failure("USER_NOT_FOUND", "用户不存在")
                             : success({{"profile", profile}});
}

DbResult Database::updateProfile(qint64 userId, const QString &nickname, const QString &avatarBase64)
{
    if (nickname.trimmed().isEmpty() || nickname.trimmed().size() > 30)
        return failure("REQUEST_INVALID", "昵称长度应为 1～30 个字符");
    if (avatarBase64.toUtf8().size() > 700 * 1024)
        return failure("REQUEST_INVALID", "头像文件过大");

    QSqlQuery query(db_);
    query.prepare("UPDATE users SET nickname=?, avatar_base64=CASE WHEN ?='' THEN avatar_base64 ELSE ? END, updated_at=? WHERE id=?");
    query.addBindValue(nickname.trimmed());
    query.addBindValue(avatarBase64);
    query.addBindValue(avatarBase64);
    query.addBindValue(nowSeconds());
    query.addBindValue(userId);
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    return getProfile(userId);
}

DbResult Database::recharge(qint64 userId, qint64 amountCents)
{
    if (amountCents <= 0 || amountCents > 100000000)
        return failure("REQUEST_INVALID", "充值金额无效");
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());

    QSqlQuery query(db_);
    query.prepare("UPDATE users SET balance_cents=balance_cents+?,updated_at=? WHERE id=? AND status='active'");
    query.addBindValue(amountCents);
    query.addBindValue(nowSeconds());
    query.addBindValue(userId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        db_.rollback();
        return failure("USER_FROZEN", "账号不可充值");
    }
    query.prepare("INSERT INTO wallet_transactions(user_id,type,amount_cents,balance_after_cents,created_at,note) "
                  "SELECT id,'recharge',?,balance_cents,?,'模拟充值' FROM users WHERE id=?");
    query.addBindValue(amountCents);
    query.addBindValue(nowSeconds());
    query.addBindValue(userId);
    if (!query.exec() || !db_.commit()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    return getProfile(userId);
}

DbResult Database::stationList(double latitude, double longitude)
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT s.id,s.name,s.address,s.latitude,s.longitude,s.price_cents_per_kwh,"
                    "COUNT(c.id),SUM(CASE WHEN c.status='idle' THEN 1 ELSE 0 END),"
                    "SUM(CASE WHEN c.status!='offline' THEN 1 ELSE 0 END) "
                    "FROM stations s LEFT JOIN chargers c ON c.station_id=s.id "
                    "WHERE s.enabled=1 GROUP BY s.id"))
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());

    struct Entry { double distance; QJsonObject object; };
    QList<Entry> entries;
    while (query.next()) {
        const double lat = query.value(3).toDouble();
        const double lon = query.value(4).toDouble();
        const double distance = haversineMeters(latitude, longitude, lat, lon);
        entries.append({distance, {
            {"id", jsonNumber(query.value(0).toLongLong())},
            {"name", query.value(1).toString()},
            {"address", query.value(2).toString()},
            {"latitude", lat},
            {"longitude", lon},
            {"priceCentsPerKwh", query.value(5).toInt()},
            {"chargerCount", query.value(6).toInt()},
            {"idleCount", query.value(7).toInt()},
            {"onlineCount", query.value(8).toInt()},
            {"distanceMeters", qRound64(distance)},
        }});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.distance < b.distance; });
    QJsonArray array;
    for (const Entry &entry : entries)
        array.append(entry.object);
    return success({{"stations", array}});
}

DbResult Database::stationDetail(qint64 stationId)
{
    QSqlQuery station(db_);
    station.prepare("SELECT id,name,address,latitude,longitude,price_cents_per_kwh FROM stations WHERE id=? AND enabled=1");
    station.addBindValue(stationId);
    if (!station.exec() || !station.next())
        return failure("STATION_NOT_FOUND", "充电站不存在");

    QJsonObject object{
        {"id", jsonNumber(station.value(0).toLongLong())},
        {"name", station.value(1).toString()},
        {"address", station.value(2).toString()},
        {"latitude", station.value(3).toDouble()},
        {"longitude", station.value(4).toDouble()},
        {"priceCentsPerKwh", station.value(5).toInt()},
    };

    QSqlQuery chargers(db_);
    chargers.prepare("SELECT id,code,type,power_watts,status,total_sessions,total_duration_seconds FROM chargers WHERE station_id=? ORDER BY code");
    chargers.addBindValue(stationId);
    if (!chargers.exec())
        return failure("DATABASE_UNAVAILABLE", chargers.lastError().text());
    QJsonArray array;
    while (chargers.next()) {
        array.append(QJsonObject{
            {"id", jsonNumber(chargers.value(0).toLongLong())},
            {"code", chargers.value(1).toString()},
            {"type", chargers.value(2).toString()},
            {"powerWatts", chargers.value(3).toInt()},
            {"status", chargers.value(4).toString()},
            {"totalSessions", chargers.value(5).toInt()},
            {"totalDurationSeconds", jsonNumber(chargers.value(6).toLongLong())},
        });
    }
    object.insert("chargers", array);
    return success({{"station", object}});
}

QJsonObject Database::orderById(qint64 orderId)
{
    QSqlQuery query(db_);
    query.prepare("SELECT o.id,o.order_no,o.user_id,o.station_id,s.name,o.charger_id,c.code,o.status,o.reserved_at,"
                  "o.started_at,o.ended_at,o.settled_at,o.energy_wh,o.amount_cents,o.price_cents_snapshot,o.power_watts_snapshot "
                  "FROM orders o JOIN stations s ON s.id=o.station_id JOIN chargers c ON c.id=o.charger_id WHERE o.id=?");
    query.addBindValue(orderId);
    if (!query.exec() || !query.next())
        return {};
    const qint64 energyWh = query.value(12).toLongLong();
    const int progressPercent = static_cast<int>(qBound(
        qint64(0), energyWh * 100 / kDemoTargetEnergyWh, qint64(100)));
    return {
        {"id", jsonNumber(query.value(0).toLongLong())},
        {"orderNo", query.value(1).toString()},
        {"userId", jsonNumber(query.value(2).toLongLong())},
        {"stationId", jsonNumber(query.value(3).toLongLong())},
        {"stationName", query.value(4).toString()},
        {"chargerId", jsonNumber(query.value(5).toLongLong())},
        {"chargerCode", query.value(6).toString()},
        {"status", query.value(7).toString()},
        {"reservedAt", jsonNumber(query.value(8).toLongLong())},
        {"startedAt", jsonNumber(query.value(9).toLongLong())},
        {"endedAt", jsonNumber(query.value(10).toLongLong())},
        {"settledAt", jsonNumber(query.value(11).toLongLong())},
        {"energyWh", jsonNumber(energyWh)},
        {"targetEnergyWh", jsonNumber(kDemoTargetEnergyWh)},
        {"progressPercent", progressPercent},
        {"amountCents", jsonNumber(query.value(13).toLongLong())},
        {"priceCentsPerKwh", query.value(14).toInt()},
        {"powerWatts", query.value(15).toInt()},
    };
}

DbResult Database::activeOrder(qint64 userId)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id FROM orders WHERE user_id=? AND status IN ('reserved','charging','pending_settlement') ORDER BY id DESC LIMIT 1");
    query.addBindValue(userId);
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    if (!query.next())
        return success({{"order", QJsonValue::Null}});
    return success({{"order", orderById(query.value(0).toLongLong())}});
}

DbResult Database::userOrders(qint64 userId)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id FROM orders WHERE user_id=? ORDER BY id DESC LIMIT 50");
    query.addBindValue(userId);
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    QJsonArray orders;
    while (query.next())
        orders.append(orderById(query.value(0).toLongLong()));
    return success({{"orders", orders}});
}

DbResult Database::reserveOrder(qint64 userId, qint64 chargerId)
{
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    QSqlQuery query(db_);
    query.prepare("SELECT status FROM users WHERE id=?");
    query.addBindValue(userId);
    if (!query.exec() || !query.next() || query.value(0).toString() != "active") {
        db_.rollback();
        return failure("USER_FROZEN", "账号不可预约充电");
    }
    query.prepare("SELECT id FROM orders WHERE user_id=? AND status IN ('reserved','charging','pending_settlement')");
    query.addBindValue(userId);
    if (!query.exec() || query.next()) {
        db_.rollback();
        return failure("ACTIVE_ORDER_EXISTS", "您有未完成的充电订单，请先处理");
    }
    query.prepare("SELECT c.station_id,c.status,c.power_watts,s.price_cents_per_kwh FROM chargers c JOIN stations s ON s.id=c.station_id WHERE c.id=?");
    query.addBindValue(chargerId);
    if (!query.exec() || !query.next()) {
        db_.rollback();
        return failure("CHARGER_NOT_FOUND", "电桩不存在");
    }
    if (query.value(1).toString() != "idle") {
        db_.rollback();
        return failure("CHARGER_NOT_AVAILABLE", "该电桩当前不可预约");
    }
    const qint64 stationId = query.value(0).toLongLong();
    const int power = query.value(2).toInt();
    const int price = query.value(3).toInt();
    const qint64 now = nowSeconds();
    query.prepare("INSERT INTO orders(order_no,user_id,station_id,charger_id,status,reserved_at,power_watts_snapshot,"
                  "price_cents_snapshot,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?)");
    query.addBindValue(QStringLiteral("CD%1%2").arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz")).arg(chargerId));
    query.addBindValue(userId);
    query.addBindValue(stationId);
    query.addBindValue(chargerId);
    query.addBindValue("reserved");
    query.addBindValue(now);
    query.addBindValue(power);
    query.addBindValue(price);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "预约失败，电桩可能已被占用");
    }
    const qint64 orderId = query.lastInsertId().toLongLong();
    query.prepare("UPDATE chargers SET status='reserved',updated_at=? WHERE id=? AND status='idle'");
    query.addBindValue(now);
    query.addBindValue(chargerId);
    if (!query.exec() || query.numRowsAffected() != 1 || !db_.commit()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "预约状态更新失败");
    }
    return success({{"order", orderById(orderId)}});
}

DbResult Database::startOrder(qint64 userId, qint64 orderId)
{
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    QSqlQuery query(db_);
    query.prepare("SELECT charger_id FROM orders WHERE id=? AND user_id=? AND status='reserved'");
    query.addBindValue(orderId);
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "订单当前不能开始充电");
    }
    const qint64 chargerId = query.value(0).toLongLong();
    const qint64 now = nowSeconds();
    query.prepare("UPDATE orders SET status='charging',started_at=?,updated_at=?,version=version+1 WHERE id=? AND status='reserved'");
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(orderId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "订单状态已变化");
    }
    query.prepare("UPDATE chargers SET status='charging',updated_at=? WHERE id=? AND status='reserved'");
    query.addBindValue(now);
    query.addBindValue(chargerId);
    if (!query.exec() || query.numRowsAffected() != 1 || !db_.commit()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "电桩状态已变化");
    }
    return success({{"order", orderById(orderId)}});
}

DbResult Database::cancelOrder(qint64 userId, qint64 orderId)
{
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    QSqlQuery query(db_);
    query.prepare("SELECT charger_id FROM orders WHERE id=? AND user_id=? AND status='reserved'");
    query.addBindValue(orderId);
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "该预约不能取消");
    }
    const qint64 chargerId = query.value(0).toLongLong();
    const qint64 now = nowSeconds();
    query.prepare("UPDATE orders SET status='cancelled',ended_at=?,updated_at=? WHERE id=?");
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(orderId);
    if (!query.exec()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    query.prepare("UPDATE chargers SET status='idle',updated_at=? WHERE id=?");
    query.addBindValue(now);
    query.addBindValue(chargerId);
    if (!query.exec() || !db_.commit()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    return success({{"order", orderById(orderId)}});
}

DbResult Database::stopOrder(qint64 userId, qint64 orderId)
{
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    QSqlQuery query(db_);
    query.prepare("SELECT charger_id,started_at FROM orders WHERE id=? AND user_id=? AND status='charging'");
    query.addBindValue(orderId);
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "订单当前不能停止");
    }
    const qint64 chargerId = query.value(0).toLongLong();
    const qint64 startedAt = query.value(1).toLongLong();
    const qint64 now = nowSeconds();
    query.prepare("UPDATE orders SET status='pending_settlement',ended_at=?,updated_at=?,version=version+1 WHERE id=?");
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(orderId);
    if (!query.exec()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    query.prepare("UPDATE chargers SET status='reserved',total_duration_seconds=total_duration_seconds+?,updated_at=? WHERE id=?");
    query.addBindValue(qMax<qint64>(0, now - startedAt));
    query.addBindValue(now);
    query.addBindValue(chargerId);
    if (!query.exec() || !db_.commit()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    return success({{"order", orderById(orderId)}});
}

DbResult Database::settleOrder(qint64 userId, qint64 orderId)
{
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    QSqlQuery query(db_);
    query.prepare("SELECT o.status,o.amount_cents,o.charger_id,u.balance_cents FROM orders o JOIN users u ON u.id=o.user_id "
                  "WHERE o.id=? AND o.user_id=?");
    query.addBindValue(orderId);
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        db_.rollback();
        return failure("ORDER_NOT_FOUND", "订单不存在");
    }
    const QString status = query.value(0).toString();
    if (status == "settled") {
        db_.rollback();
        return success({{"order", orderById(orderId)}, {"profile", profileObject(userId)}});
    }
    if (status != "pending_settlement") {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "订单当前不能结算");
    }
    const qint64 amount = query.value(1).toLongLong();
    const qint64 chargerId = query.value(2).toLongLong();
    const qint64 balance = query.value(3).toLongLong();
    if (balance < amount) {
        db_.rollback();
        return failure("INSUFFICIENT_BALANCE", "余额不足，请先充值");
    }
    const qint64 now = nowSeconds();
    query.prepare("UPDATE users SET balance_cents=balance_cents-?,updated_at=? WHERE id=? AND balance_cents>=?");
    query.addBindValue(amount);
    query.addBindValue(now);
    query.addBindValue(userId);
    query.addBindValue(amount);
    if (!query.exec() || query.numRowsAffected() != 1) {
        db_.rollback();
        return failure("INSUFFICIENT_BALANCE", "余额不足，请先充值");
    }
    query.prepare("UPDATE orders SET status='settled',settled_at=?,updated_at=?,version=version+1 WHERE id=? AND status='pending_settlement'");
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(orderId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        db_.rollback();
        return failure("ORDER_STATE_CONFLICT", "订单已被其他操作处理");
    }
    query.prepare("UPDATE chargers SET status='idle',total_sessions=total_sessions+1,updated_at=? WHERE id=?");
    query.addBindValue(now);
    query.addBindValue(chargerId);
    if (!query.exec()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    query.prepare("INSERT INTO wallet_transactions(user_id,order_id,type,amount_cents,balance_after_cents,created_at,note) "
                  "SELECT id,?,'charge_payment',?,balance_cents,?,'充电结算' FROM users WHERE id=?");
    query.addBindValue(orderId);
    query.addBindValue(-amount);
    query.addBindValue(now);
    query.addBindValue(userId);
    if (!query.exec() || !db_.commit()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    return success({{"order", orderById(orderId)}, {"profile", profileObject(userId)}});
}

QJsonArray Database::tickCharging(int simulationSpeed)
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT o.id,o.energy_wh,o.price_cents_snapshot,o.power_watts_snapshot,o.user_id,u.balance_cents,o.charger_id,o.started_at "
                    "FROM orders o JOIN users u ON u.id=o.user_id WHERE o.status='charging'"))
        return {};
    struct Update { qint64 id; qint64 energy; qint64 amount; qint64 balance; qint64 charger; qint64 duration; bool stop; };
    QList<Update> updates;
    const qint64 tickTime = nowSeconds();
    while (query.next()) {
        const qint64 deltaWh = qMax<qint64>(1, query.value(3).toLongLong() * simulationSpeed / 3600);
        const qint64 energy = qMin(kDemoTargetEnergyWh, query.value(1).toLongLong() + deltaWh);
        const qint64 calculated = (energy * query.value(2).toLongLong() + 500) / 1000;
        const qint64 balance = query.value(5).toLongLong();
        updates.append({query.value(0).toLongLong(), energy, qMin(calculated, balance), balance,
                        query.value(6).toLongLong(), qMax<qint64>(0, tickTime - query.value(7).toLongLong()),
                        calculated >= balance || energy >= kDemoTargetEnergyWh});
    }
    if (updates.isEmpty() || !db_.transaction())
        return {};
    QJsonArray changed;
    for (const Update &update : updates) {
        QSqlQuery write(db_);
        if (update.stop) {
            write.prepare("UPDATE orders SET energy_wh=?,amount_cents=?,status='pending_settlement',ended_at=?,updated_at=?,version=version+1 "
                          "WHERE id=? AND status='charging'");
            write.addBindValue(update.energy);
            write.addBindValue(update.amount);
            write.addBindValue(tickTime);
            write.addBindValue(tickTime);
            write.addBindValue(update.id);
        } else {
            write.prepare("UPDATE orders SET energy_wh=?,amount_cents=?,updated_at=?,version=version+1 WHERE id=? AND status='charging'");
            write.addBindValue(update.energy);
            write.addBindValue(update.amount);
            write.addBindValue(tickTime);
            write.addBindValue(update.id);
        }
        if (!write.exec()) {
            db_.rollback();
            return {};
        }
        if (update.stop) {
            write.prepare("UPDATE chargers SET status='reserved',total_duration_seconds=total_duration_seconds+?,updated_at=? WHERE id=?");
            write.addBindValue(update.duration);
            write.addBindValue(tickTime);
            write.addBindValue(update.charger);
            if (!write.exec()) {
                db_.rollback();
                return {};
            }
        }
        changed.append(orderById(update.id));
    }
    if (!db_.commit())
        return {};
    return changed;
}

DbResult Database::adminLogin(const QString &username, const QString &password)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id,password_salt,password_hash,status FROM admins WHERE username=?");
    query.addBindValue(username);
    if (!query.exec() || !query.next() || query.value(3).toString() != "active"
        || passwordHash(query.value(1).toString(), password) != query.value(2).toString())
        return failure("UNAUTHENTICATED", "管理员账号或密码错误");
    QSqlQuery update(db_);
    update.prepare("UPDATE admins SET last_login_at=? WHERE id=?");
    update.addBindValue(nowSeconds());
    update.addBindValue(query.value(0));
    update.exec();
    return success({{"username", username}});
}

DbResult Database::adminDashboard()
{
    QJsonObject metrics;
    QSqlQuery query(db_);
    const QString metricSql =
        "SELECT "
        "COALESCE(SUM(CASE WHEN date(settled_at,'unixepoch','localtime')=date('now','localtime') THEN amount_cents ELSE 0 END),0),"
        "COALESCE(SUM(CASE WHEN strftime('%Y-%m',settled_at,'unixepoch','localtime')=strftime('%Y-%m','now','localtime') THEN amount_cents ELSE 0 END),0),"
        "COALESCE(SUM(amount_cents),0),"
        "SUM(CASE WHEN date(created_at,'unixepoch','localtime')=date('now','localtime') THEN 1 ELSE 0 END) "
        "FROM orders WHERE status='settled'";
    if (!query.exec(metricSql) || !query.next())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    metrics.insert("todayRevenueCents", jsonNumber(query.value(0).toLongLong()));
    metrics.insert("monthRevenueCents", jsonNumber(query.value(1).toLongLong()));
    metrics.insert("totalRevenueCents", jsonNumber(query.value(2).toLongLong()));
    metrics.insert("todayOrders", query.value(3).toInt());
    if (query.exec("SELECT COUNT(*) FROM users") && query.next())
        metrics.insert("userCount", query.value(0).toInt());

    QJsonArray trend;
    query.prepare("WITH RECURSIVE days(d) AS (SELECT date('now','localtime','-29 day') UNION ALL SELECT date(d,'+1 day') FROM days WHERE d<date('now','localtime')) "
                  "SELECT d,COALESCE(SUM(o.amount_cents),0) FROM days LEFT JOIN orders o ON date(o.settled_at,'unixepoch','localtime')=d AND o.status='settled' GROUP BY d ORDER BY d");
    if (query.exec()) {
        while (query.next())
            trend.append(QJsonObject{{"date", query.value(0).toString()}, {"revenueCents", jsonNumber(query.value(1).toLongLong())}});
    }

    QJsonObject chargerCounts{{"idle", 0}, {"reserved", 0}, {"charging", 0}, {"fault", 0}, {"restarting", 0}, {"offline", 0}};
    if (query.exec("SELECT status,COUNT(*) FROM chargers GROUP BY status")) {
        while (query.next())
            chargerCounts.insert(query.value(0).toString(), query.value(1).toInt());
    }

    QJsonArray faultChargers;
    if (query.exec("SELECT c.id,c.code,s.name FROM chargers c JOIN stations s ON s.id=c.station_id "
                   "WHERE c.status='fault' ORDER BY c.code")) {
        while (query.next()) {
            faultChargers.append(QJsonObject{{"id", jsonNumber(query.value(0).toLongLong())},
                                              {"code", query.value(1).toString()},
                                              {"stationName", query.value(2).toString()}});
        }
    }

    QJsonArray stationRevenue;
    if (query.exec("SELECT s.id,s.name,COALESCE(SUM(CASE WHEN o.status='settled' THEN o.amount_cents ELSE 0 END),0) revenue "
                   "FROM stations s LEFT JOIN orders o ON o.station_id=s.id "
                   "GROUP BY s.id,s.name ORDER BY revenue DESC,s.id LIMIT 5")) {
        while (query.next()) {
            stationRevenue.append(QJsonObject{{"id", jsonNumber(query.value(0).toLongLong())},
                                               {"name", query.value(1).toString()},
                                               {"revenueCents", jsonNumber(query.value(2).toLongLong())}});
        }
    }
    return success({{"metrics", metrics}, {"trend", trend}, {"chargerCounts", chargerCounts},
                    {"faultChargers", faultChargers}, {"stationRevenue", stationRevenue}});
}

DbResult Database::adminStations()
{
    return stationList(22.543687, 114.059625);
}

DbResult Database::adminChargers()
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT c.id,c.code,s.name,c.type,c.power_watts,c.status,c.total_sessions,c.total_duration_seconds "
                    "FROM chargers c JOIN stations s ON s.id=c.station_id ORDER BY c.code"))
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    QJsonArray array;
    while (query.next()) {
        array.append(QJsonObject{{"id", jsonNumber(query.value(0).toLongLong())}, {"code", query.value(1).toString()},
                                 {"stationName", query.value(2).toString()}, {"type", query.value(3).toString()},
                                 {"powerWatts", query.value(4).toInt()}, {"status", query.value(5).toString()},
                                 {"totalSessions", query.value(6).toInt()},
                                 {"totalDurationSeconds", jsonNumber(query.value(7).toLongLong())}});
    }
    return success({{"chargers", array}});
}

DbResult Database::adminUsers(const QString &phoneFilter)
{
    QSqlQuery query(db_);
    query.prepare("SELECT id,phone,nickname,balance_cents,created_at,status FROM users WHERE phone LIKE ? ORDER BY id");
    query.addBindValue('%' + phoneFilter + '%');
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    QJsonArray array;
    while (query.next()) {
        array.append(QJsonObject{{"id", jsonNumber(query.value(0).toLongLong())}, {"phone", query.value(1).toString()},
                                 {"nickname", query.value(2).toString()}, {"balanceCents", jsonNumber(query.value(3).toLongLong())},
                                 {"createdAt", jsonNumber(query.value(4).toLongLong())}, {"status", query.value(5).toString()}});
    }
    return success({{"users", array}});
}

DbResult Database::adminOrders()
{
    QSqlQuery query(db_);
    if (!query.exec("SELECT id FROM orders ORDER BY id DESC LIMIT 100"))
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    QJsonArray array;
    while (query.next())
        array.append(orderById(query.value(0).toLongLong()));
    return success({{"orders", array}});
}

DbResult Database::adminAddStation(const QJsonObject &data)
{
    const QString name = data.value("name").toString().trimmed();
    const QString address = data.value("address").toString().trimmed();
    const double lat = data.value("latitude").toDouble(999);
    const double lon = data.value("longitude").toDouble(999);
    const int price = data.value("priceCentsPerKwh").toInt();
    const int count = data.value("chargerCount").toInt();
    if (name.isEmpty() || address.isEmpty() || qAbs(lat) > 90 || qAbs(lon) > 180 || price <= 0 || count < 1 || count > 50)
        return failure("REQUEST_INVALID", "新增电站参数无效");
    if (!db_.transaction())
        return failure("DATABASE_UNAVAILABLE", lastError());
    const qint64 now = nowSeconds();
    QSqlQuery query(db_);
    query.prepare("INSERT INTO stations(name,address,latitude,longitude,price_cents_per_kwh,created_at,updated_at) VALUES(?,?,?,?,?,?,?)");
    query.addBindValue(name); query.addBindValue(address); query.addBindValue(lat); query.addBindValue(lon);
    query.addBindValue(price); query.addBindValue(now); query.addBindValue(now);
    if (!query.exec()) {
        db_.rollback();
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    }
    const qint64 stationId = query.lastInsertId().toLongLong();
    query.prepare("INSERT INTO chargers(code,station_id,type,power_watts,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?)");
    for (int i = 1; i <= count; ++i) {
        query.bindValue(0, QStringLiteral("SZ%1-%2").arg(stationId, 3, 10, QLatin1Char('0')).arg(i, 2, 10, QLatin1Char('0')));
        query.bindValue(1, stationId);
        query.bindValue(2, i <= count * 2 / 3 ? "fast" : "slow");
        query.bindValue(3, i <= count * 2 / 3 ? 120000 : 7000);
        query.bindValue(4, "idle");
        query.bindValue(5, now); query.bindValue(6, now);
        if (!query.exec()) {
            db_.rollback();
            return failure("DATABASE_UNAVAILABLE", query.lastError().text());
        }
    }
    if (!db_.commit())
        return failure("DATABASE_UNAVAILABLE", lastError());
    return success({{"stationId", jsonNumber(stationId)}});
}

DbResult Database::adminSetUserStatus(qint64 userId, const QString &status)
{
    if (status != "active" && status != "frozen")
        return failure("REQUEST_INVALID", "用户状态无效");
    QSqlQuery query(db_);
    query.prepare("UPDATE users SET status=?,updated_at=? WHERE id=?");
    query.addBindValue(status); query.addBindValue(nowSeconds()); query.addBindValue(userId);
    if (!query.exec() || query.numRowsAffected() != 1)
        return failure("USER_NOT_FOUND", "用户不存在");
    return success();
}

DbResult Database::adminRestartCharger(qint64 chargerId)
{
    QSqlQuery query(db_);
    query.prepare("UPDATE chargers SET status='restarting',updated_at=? WHERE id=? AND status='fault'");
    query.addBindValue(nowSeconds()); query.addBindValue(chargerId);
    if (!query.exec() || query.numRowsAffected() != 1)
        return failure("CHARGER_NOT_AVAILABLE", "仅故障电桩可以远程重启");
    return success();
}

DbResult Database::finishRestart(qint64 chargerId)
{
    QSqlQuery query(db_);
    query.prepare("UPDATE chargers SET status='idle',updated_at=? WHERE id=? AND status='restarting'");
    query.addBindValue(nowSeconds()); query.addBindValue(chargerId);
    if (!query.exec())
        return failure("DATABASE_UNAVAILABLE", query.lastError().text());
    return success();
}

DbResult Database::adminSettleOrder(qint64 orderId)
{
    const QJsonObject order = orderById(orderId);
    if (order.isEmpty())
        return failure("ORDER_NOT_FOUND", "订单不存在");
    return settleOrder(static_cast<qint64>(order.value("userId").toDouble()), orderId);
}
