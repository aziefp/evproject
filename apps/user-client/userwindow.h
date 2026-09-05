#pragma once

#include <QJsonObject>
#include <QMainWindow>

class ClientConnection;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTimer;

class UserWindow : public QMainWindow
{
    Q_OBJECT

public:
    UserWindow(const QString &host, quint16 port, const QString &mapKey,
               QWidget *parent = nullptr);

private:
    void buildLoginPage();
    void buildMainPage();
    void login();
    void handleMessage(const QJsonObject &message);
    void showError(const QJsonObject &message);
    void updateProfile(const QJsonObject &profile);
    void updateStations(const QJsonObject &data);
    void updateActiveOrder(const QJsonValue &value);
    void updateOrderHistory(const QJsonArray &orders);
    void requestInitialData();
    void locateAddress();
    void openNavigation(const QJsonObject &station, QWidget *anchor);
    void saveProfile();
    void chooseAvatar();
    void recharge();
    void actOnOrder(const QString &action);

    ClientConnection *connection_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QLineEdit *phoneEdit_ = nullptr;
    QLabel *loginStatus_ = nullptr;
    QLabel *connectionStatus_ = nullptr;
    QTabWidget *tabs_ = nullptr;

    QLineEdit *addressEdit_ = nullptr;
    QLabel *locationLabel_ = nullptr;
    QListWidget *stationList_ = nullptr;

    QLabel *activeState_ = nullptr;
    QLabel *activeStation_ = nullptr;
    QLabel *activeCharger_ = nullptr;
    QLabel *activeEnergy_ = nullptr;
    QLabel *activeAmount_ = nullptr;
    QLabel *activeDuration_ = nullptr;
    QProgressBar *activeProgress_ = nullptr;
    QLabel *activeProgressText_ = nullptr;
    QPushButton *chargeToggleButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QPushButton *settleButton_ = nullptr;

    QLabel *avatarLabel_ = nullptr;
    QLabel *phoneLabel_ = nullptr;
    QLabel *balanceLabel_ = nullptr;
    QLineEdit *nicknameEdit_ = nullptr;
    QLineEdit *rechargeEdit_ = nullptr;
    QListWidget *historyList_ = nullptr;
    QTimer *pollTimer_ = nullptr;

    QString host_;
    quint16 port_ = 0;
    QString mapKey_;
    QString lastPhone_;
    QString avatarBase64_;
    double latitude_ = 22.543096;
    double longitude_ = 114.057865;
    qint64 activeOrderId_ = 0;
    qint64 promptedActiveOrderId_ = 0;
    bool loginPending_ = false;
    bool suppressNextActivePrompt_ = false;
};
