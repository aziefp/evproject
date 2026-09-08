#pragma once

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;
class QChartView;
class QComboBox;

class AdminWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit AdminWindow(QWidget *parent = nullptr);

signals:
    void adminCommand(quint64 requestId, const QString &action, const QJsonObject &data);

public slots:
    void handleAdminResult(quint64 requestId, const QString &action, bool ok,
                           const QJsonObject &data, const QString &message);
    void setServerStatus(const QString &message);

private:
    quint64 sendCommand(const QString &action, const QJsonObject &data = {});
    void buildLoginPage();
    void buildMainPage();
    void refreshAll();
    void updateDashboard(const QJsonObject &data);
    void updateStations(const QJsonObject &data);
    void showStationDetail(const QJsonObject &data);
    void updateChargers(const QJsonObject &data);
    void updateUsers(const QJsonObject &data);
    void updateOrders(const QJsonObject &data);
    void addStation();

    QStackedWidget *stack_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLabel *loginMessage_ = nullptr;
    QLabel *loginServerStatus_ = nullptr;
    QLabel *serverStatus_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QLabel *todayRevenue_ = nullptr;
    QLabel *monthRevenue_ = nullptr;
    QLabel *totalRevenue_ = nullptr;
    QLabel *todayOrders_ = nullptr;
    QLabel *userCount_ = nullptr;
    QChartView *chargerStatusChart_ = nullptr;
    QListWidget *faultChargerList_ = nullptr;
    QChartView *revenueChart_ = nullptr;
    QChartView *stationRevenueChart_ = nullptr;
    QComboBox *trendRange_ = nullptr;
    QComboBox *chargerStatusFilter_ = nullptr;
    QComboBox *orderStatusFilter_ = nullptr;
    QJsonObject lastDashboard_;

    QTableWidget *stationsTable_ = nullptr;
    QTableWidget *chargersTable_ = nullptr;
    QTableWidget *usersTable_ = nullptr;
    QTableWidget *ordersTable_ = nullptr;
    QLineEdit *userSearch_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    quint64 nextRequestId_ = 1;
    bool loggedIn_ = false;
};
