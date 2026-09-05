#include "userwindow.h"

#include "clientconnection.h"
#include "mapdialog.h"

#include <QBuffer>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QPixmap>
#include <QProgressBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QVariant>

namespace {

QString money(qint64 cents)
{
    return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2);
}

QString statusText(const QString &value)
{
    static const QHash<QString, QString> labels{
        {"idle", "空闲"}, {"reserved", "已预约"}, {"charging", "充电中"},
        {"pending_settlement", "待结算"}, {"settled", "已结算"}, {"cancelled", "已取消"},
        {"fault", "故障"}, {"offline", "离线"}, {"restarting", "重启中"}};
    return labels.value(value, value);
}

QString dateText(qint64 seconds)
{
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds).toString("MM-dd hh:mm") : "-";
}

} // namespace

UserWindow::UserWindow(const QString &host, quint16 port, const QString &mapKey, QWidget *parent)
    : QMainWindow(parent), connection_(new ClientConnection(this)), host_(host), port_(port), mapKey_(mapKey)
{
    setWindowTitle("BIT比特充电 · 用户端");
    setMinimumSize(380, 640);
    resize(420, 720);
    stack_ = new QStackedWidget(this);
    setCentralWidget(stack_);
    buildLoginPage();
    buildMainPage();

    setStyleSheet(R"(
        QMainWindow, QDialog { background: #ffffff; }
        QWidget { color: #101828; font-size: 13px; }
        QLineEdit, QComboBox { background: white; border: 1px solid #98a2b3; border-radius: 0; padding: 7px; selection-background-color:#ff5a00; }
        QLineEdit:focus, QComboBox:focus { border: 2px solid #ff5a00; }
        QComboBox QAbstractItemView { background: white; color: #101828; selection-background-color: #ff5a00; selection-color:white; }
        QListWidget { background: white; color: #101828; alternate-background-color: white; border: 0; outline: 0; }
        QListWidget::item { background: white; color: #101828; border: 0; border-bottom: 1px solid #d0d5dd; border-radius: 0; margin: 0; padding: 8px; }
        QListWidget::item:alternate { background: white; }
        QListWidget::item:selected { background: #ff5a00; color: white; }
        QListWidget#stationList::item { background:white; border:0; margin:3px 0; padding:0; }
        QFrame#stationCard { background: white; border: 1px solid #98a2b3; border-radius: 0; }
        QFrame#stationCard QLabel { background: transparent; }
        QLabel#stationName { color: #101828; font-size: 15px; font-weight: 700; }
        QLabel#stationMeta { color: #475467; }
        QPushButton { background: #ff5a00; color: white; border: 0; border-radius: 0; padding: 8px 12px; font-weight: 700; }
        QPushButton:hover { background: #e64f00; }
        QPushButton:pressed { background: #c94300; }
        QPushButton:disabled { background: #e4e7ec; color: #98a2b3; }
        QPushButton#secondary { background: white; color: #d94c00; border: 1px solid #ff5a00; }
        QPushButton#secondary:hover { background: #ff5a00; color:white; }
        QProgressBar { background:#eaecf0; color:#101828; border:1px solid #98a2b3; border-radius:0; text-align:center; min-height:20px; font-weight:700; }
        QProgressBar::chunk { background:#ff5a00; }
        QTabWidget::pane { border: 0; background:transparent; }
        QTabWidget::tab-bar { alignment: center; }
        QTabBar { background: transparent; }
        QTabBar::tab { background: white; color:#344054; padding: 10px 15px; min-width: 76px; margin: 0; border: 1px solid #d0d5dd; border-radius: 0; }
        QTabBar::tab:selected { background: #ff5a00; border-color:#ff5a00; color: white; }
        QGroupBox { background:white; border: 0; border-radius: 0; margin-top: 12px; padding-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 0; color:#d94c00; font-weight:700; background:transparent; }
        QMenu { background:white; color:#101828; border:1px solid #98a2b3; }
        QMenu::item:selected { background:#ff5a00; color:white; }
    )");

    connect(connection_, &ClientConnection::messageReceived, this, &UserWindow::handleMessage);
    connect(connection_, &ClientConnection::statusChanged, this, [this](const QString &text, bool connected) {
        connectionStatus_->setText(text);
        loginStatus_->setText(text);
        if (connected && !lastPhone_.isEmpty() && (stack_->currentIndex() == 1 || loginPending_))
            connection_->sendRequest("session.loginByPhone", {{"phone", lastPhone_}});
    });
    connection_->connectToServer(host_, port_);

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(5000);
    connect(pollTimer_, &QTimer::timeout, this, [this] {
        if (stack_->currentIndex() == 1 && connection_->isConnected())
            connection_->sendRequest("order.getActive");
    });
}

void UserWindow::buildLoginPage()
{
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->addStretch();
    auto *panel = new QGroupBox("手机快捷登录 / 自动注册", page);
    panel->setMaximumWidth(350);
    auto *form = new QFormLayout(panel);
    auto *title = new QLabel("BIT比特充电", panel);
    title->setStyleSheet("font-size: 30px; font-weight: 700; color: #ff5a00; background:transparent;");
    phoneEdit_ = new QLineEdit("13800138001", panel);
    phoneEdit_->setPlaceholderText("请输入 11 位手机号");
    auto *button = new QPushButton("进入充电平台", panel);
    loginStatus_ = new QLabel("等待连接服务器…", panel);
    loginStatus_->setWordWrap(true);
    form->addRow(title);
    form->addRow("手机号", phoneEdit_);
    form->addRow(button);
    form->addRow(loginStatus_);
    outer->addWidget(panel, 0, Qt::AlignHCenter);
    outer->addStretch();
    connect(button, &QPushButton::clicked, this, &UserWindow::login);
    connect(phoneEdit_, &QLineEdit::returnPressed, this, &UserWindow::login);
    stack_->addWidget(page);
}

void UserWindow::buildMainPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *top = new QHBoxLayout;
    auto *brand = new QLabel("BIT比特充电", page);
    brand->setStyleSheet("font-size: 22px; font-weight: 700; color: #ff5a00;");
    connectionStatus_ = new QLabel("未连接", page);
    top->addWidget(brand);
    top->addStretch();
    top->addWidget(connectionStatus_);
    layout->addLayout(top);

    tabs_ = new QTabWidget(page);
    tabs_->setTabPosition(QTabWidget::South);
    tabs_->tabBar()->setExpanding(false);
    layout->addWidget(tabs_, 1);

    auto *stationsPage = new QWidget(tabs_);
    auto *stationsLayout = new QVBoxLayout(stationsPage);
    auto *presetRow = new QHBoxLayout;
    auto *regionCombo = new QComboBox(stationsPage);
    regionCombo->addItem("预设区域", QVariantMap{});
    regionCombo->addItem("福田中心区", QVariantMap{{"address", "深圳市民中心"}, {"lat", 22.543687}, {"lng", 114.059625}});
    regionCombo->addItem("南山科技园", QVariantMap{{"address", "深圳市南山区科技园"}, {"lat", 22.535536}, {"lng", 113.950671}});
    regionCombo->addItem("宝安中心区", QVariantMap{{"address", "深圳市宝安中心区"}, {"lat", 22.554632}, {"lng", 113.883053}});
    addressEdit_ = new QLineEdit("深圳市民中心", stationsPage);
    auto *locateButton = new QPushButton("搜索附近", stationsPage);
    auto *refreshButton = new QPushButton("刷新", stationsPage);
    locationLabel_ = new QLabel("当前位置：深圳市民中心（默认演示坐标）", stationsPage);
    locationLabel_->setWordWrap(true);
    presetRow->addWidget(regionCombo, 1);
    presetRow->addWidget(refreshButton);
    auto *addressRow = new QHBoxLayout;
    addressRow->addWidget(addressEdit_, 1);
    addressRow->addWidget(locateButton);
    stationsLayout->addLayout(presetRow);
    stationsLayout->addLayout(addressRow);
    stationsLayout->addWidget(locationLabel_);
    stationList_ = new QListWidget(stationsPage);
    stationList_->setObjectName("stationList");
    stationList_->setSelectionMode(QAbstractItemView::NoSelection);
    stationList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    stationList_->setSpacing(4);
    stationsLayout->addWidget(stationList_, 1);
    connect(locateButton, &QPushButton::clicked, this, &UserWindow::locateAddress);
    connect(regionCombo, &QComboBox::currentIndexChanged, this, [this, regionCombo](int index) {
        if (index <= 0)
            return;
        const QVariantMap preset = regionCombo->currentData().toMap();
        addressEdit_->setText(preset.value("address").toString());
        latitude_ = preset.value("lat").toDouble();
        longitude_ = preset.value("lng").toDouble();
        locationLabel_->setText(QStringLiteral("当前位置：%1（预设区域）").arg(addressEdit_->text()));
        connection_->sendRequest("station.list", {{"latitude", latitude_}, {"longitude", longitude_}});
    });
    connect(refreshButton, &QPushButton::clicked, this, [this] {
        connection_->sendRequest("station.list", {{"latitude", latitude_}, {"longitude", longitude_}});
    });
    tabs_->addTab(stationsPage, "📍 附近");

    auto *chargingPage = new QWidget(tabs_);
    auto *chargingLayout = new QVBoxLayout(chargingPage);
    auto *activeBox = new QGroupBox("当前充电订单", chargingPage);
    auto *activeForm = new QFormLayout(activeBox);
    activeState_ = new QLabel("无进行中订单", activeBox);
    activeStation_ = new QLabel("-", activeBox);
    activeCharger_ = new QLabel("-", activeBox);
    activeEnergy_ = new QLabel("0.000 kWh", activeBox);
    activeAmount_ = new QLabel("¥0.00", activeBox);
    activeDuration_ = new QLabel("00:00:00", activeBox);
    activeProgress_ = new QProgressBar(activeBox);
    activeProgress_->setRange(0, 100);
    activeProgress_->setValue(0);
    activeProgress_->setFormat("%p%");
    activeProgressText_ = new QLabel("演示目标 20.0 kWh · 充满自动停止", activeBox);
    activeProgressText_->setStyleSheet("color:#475467;");
    activeForm->addRow("状态", activeState_);
    activeForm->addRow("电站", activeStation_);
    activeForm->addRow("电桩", activeCharger_);
    activeForm->addRow("已充电量", activeEnergy_);
    activeForm->addRow("充电时长", activeDuration_);
    activeForm->addRow("当前费用", activeAmount_);
    activeForm->addRow("充电进度", activeProgress_);
    activeForm->addRow("", activeProgressText_);
    auto *actions = new QGridLayout;
    chargeToggleButton_ = new QPushButton("开始充电", activeBox);
    cancelButton_ = new QPushButton("取消预约", activeBox);
    settleButton_ = new QPushButton("余额结算", activeBox);
    actions->addWidget(chargeToggleButton_, 0, 0, 1, 2);
    actions->addWidget(cancelButton_, 1, 0);
    actions->addWidget(settleButton_, 1, 1);
    activeForm->addRow(actions);
    chargingLayout->addWidget(activeBox);
    chargingLayout->addStretch();
    connect(chargeToggleButton_, &QPushButton::clicked, this, [this] {
        const QString action = chargeToggleButton_->property("orderAction").toString();
        if (!action.isEmpty())
            actOnOrder(action);
    });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { actOnOrder("order.cancelReservation"); });
    connect(settleButton_, &QPushButton::clicked, this, [this] { actOnOrder("order.settle"); });
    tabs_->addTab(chargingPage, "⚡ 充电");

    auto *profilePage = new QWidget(tabs_);
    auto *profileLayout = new QVBoxLayout(profilePage);
    auto *profileBox = new QGroupBox("个人资料与钱包", profilePage);
    auto *profileForm = new QFormLayout(profileBox);
    avatarLabel_ = new QLabel("暂无头像", profileBox);
    avatarLabel_->setFixedSize(96, 96);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setStyleSheet("background:white; color:#475467; border:2px solid #ff5a00; border-radius:0;");
    auto *avatarButton = new QPushButton("选择头像", profileBox);
    auto *avatarRow = new QHBoxLayout;
    avatarRow->addWidget(avatarLabel_);
    avatarRow->addWidget(avatarButton);
    avatarRow->addStretch();
    phoneLabel_ = new QLabel("-", profileBox);
    balanceLabel_ = new QLabel("¥0.00", profileBox);
    balanceLabel_->setStyleSheet("font-size: 20px; color: #ff5a00; font-weight: 700; background:transparent;");
    nicknameEdit_ = new QLineEdit(profileBox);
    auto *saveButton = new QPushButton("保存", profileBox);
    auto *nicknameRow = new QHBoxLayout;
    nicknameRow->addWidget(nicknameEdit_, 1);
    nicknameRow->addWidget(saveButton);
    rechargeEdit_ = new QLineEdit("100.00", profileBox);
    auto *rechargeButton = new QPushButton("充值", profileBox);
    auto *rechargeRow = new QHBoxLayout;
    rechargeRow->addWidget(rechargeEdit_);
    rechargeRow->addWidget(rechargeButton);
    profileForm->addRow("头像", avatarRow);
    profileForm->addRow("手机号", phoneLabel_);
    profileForm->addRow("昵称", nicknameRow);
    profileForm->addRow("账户余额", balanceLabel_);
    profileForm->addRow("充值金额（元）", rechargeRow);
    profileLayout->addWidget(profileBox);
    auto *historyBox = new QGroupBox("充电订单记录", profilePage);
    auto *historyLayout = new QVBoxLayout(historyBox);
    historyList_ = new QListWidget(historyBox);
    historyList_->setObjectName("historyList");
    historyList_->setAlternatingRowColors(true);
    historyList_->setSelectionMode(QAbstractItemView::NoSelection);
    historyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    historyLayout->addWidget(historyList_);
    profileLayout->addWidget(historyBox, 1);
    connect(avatarButton, &QPushButton::clicked, this, &UserWindow::chooseAvatar);
    connect(saveButton, &QPushButton::clicked, this, &UserWindow::saveProfile);
    connect(nicknameEdit_, &QLineEdit::returnPressed, saveButton, &QPushButton::click);
    connect(rechargeButton, &QPushButton::clicked, this, &UserWindow::recharge);
    tabs_->addTab(profilePage, "👤 我的");
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && connection_->isConnected())
            connection_->sendRequest("order.getActive");
    });
    stack_->addWidget(page);
}

void UserWindow::login()
{
    const QString phone = phoneEdit_->text().trimmed();
    if (phone.size() != 11) {
        loginStatus_->setText("请输入正确的 11 位手机号");
        return;
    }
    lastPhone_ = phone;
    loginPending_ = true;
    if (connection_->isConnected()) {
        connection_->sendRequest("session.loginByPhone", {{"phone", phone}});
        loginStatus_->setText("正在登录…");
    } else {
        loginStatus_->setText("等待连接服务器，连接成功后将自动登录…");
    }
}

void UserWindow::handleMessage(const QJsonObject &message)
{
    if (message.value("ok").isBool() && !message.value("ok").toBool()) {
        showError(message);
        return;
    }
    const QString type = message.value("type").toString();
    const QJsonObject data = message.value("data").toObject();
    if (type == "session.loginByPhone.result") {
        loginPending_ = false;
        connection_->setSessionToken(data.value("sessionToken").toString());
        updateProfile(data.value("profile").toObject());
        stack_->setCurrentIndex(1);
        promptedActiveOrderId_ = 0;
        pollTimer_->start();
        requestInitialData();
    } else if (type == "user.getProfile.result" || type == "user.updateProfile.result" || type == "wallet.recharge.result") {
        updateProfile(data.value("profile").toObject());
    } else if (type == "location.geocode.result") {
        latitude_ = data.value("latitude").toDouble();
        longitude_ = data.value("longitude").toDouble();
        locationLabel_->setText(QStringLiteral("当前位置：%1（%2, %3）")
                                    .arg(data.value("title").toString(), QString::number(latitude_, 'f', 5),
                                         QString::number(longitude_, 'f', 5)));
        connection_->sendRequest("station.list", {{"latitude", latitude_}, {"longitude", longitude_}});
    } else if (type == "station.list.result") {
        updateStations(data);
    } else if (type == "station.get.result") {
        const QJsonObject station = data.value("station").toObject();
        QDialog dialog(this);
        dialog.setWindowTitle(station.value("name").toString());
        dialog.setMinimumSize(360, 560);
        dialog.resize(400, 620);
        auto *layout = new QVBoxLayout(&dialog);
        auto *summary = new QLabel(QStringLiteral("%1\n%2 元/度")
                                       .arg(station.value("address").toString())
                                       .arg(station.value("priceCentsPerKwh").toInt() / 100.0, 0, 'f', 2), &dialog);
        summary->setWordWrap(true);
        layout->addWidget(summary);
        auto *list = new QListWidget(&dialog);
        list->setObjectName("detailList");
        list->setAlternatingRowColors(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        const QJsonArray chargers = station.value("chargers").toArray();
        for (const QJsonValue &value : chargers) {
            const QJsonObject charger = value.toObject();
            auto *entry = new QListWidgetItem(
                QStringLiteral("%1  ·  %2  ·  %3 kW\n状态：%4    累计：%5 次")
                    .arg(charger.value("code").toString(),
                         charger.value("type").toString() == "fast" ? "快充" : "慢充")
                    .arg(charger.value("powerWatts").toInt() / 1000.0, 0, 'f', 0)
                    .arg(statusText(charger.value("status").toString()))
                    .arg(charger.value("totalSessions").toInt()),
                list);
            entry->setData(Qt::UserRole, charger.value("id").toVariant());
            entry->setData(Qt::UserRole + 1, charger.value("status").toString());
            entry->setSizeHint(QSize(0, 62));
        }
        layout->addWidget(list, 1);
        auto *buttons = new QGridLayout;
        auto *drive = new QPushButton("驾车导航", &dialog);
        auto *walk = new QPushButton("步行导航", &dialog);
        auto *reserve = new QPushButton("预约所选空闲电桩", &dialog);
        auto *close = new QPushButton("关闭", &dialog);
        buttons->addWidget(drive, 0, 0);
        buttons->addWidget(walk, 0, 1);
        buttons->addWidget(reserve, 1, 0);
        buttons->addWidget(close, 1, 1);
        layout->addLayout(buttons);
        auto openMap = [this, station](const QString &mode) {
            MapDialog map(mapKey_, mode, "当前位置", latitude_, longitude_,
                          station.value("name").toString(), station.value("latitude").toDouble(),
                          station.value("longitude").toDouble(), this);
            map.exec();
        };
        connect(drive, &QPushButton::clicked, &dialog, [openMap] { openMap("drive"); });
        connect(walk, &QPushButton::clicked, &dialog, [openMap] { openMap("walk"); });
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        reserve->setEnabled(false);
        connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, reserve] {
            const QListWidgetItem *entry = list->currentItem();
            reserve->setEnabled(entry && entry->data(Qt::UserRole + 1).toString() == "idle");
        });
        connect(reserve, &QPushButton::clicked, &dialog, [this, list, &dialog] {
            const QListWidgetItem *entry = list->currentItem();
            if (!entry || entry->data(Qt::UserRole + 1).toString() != "idle") {
                QMessageBox::information(&dialog, "预约", "请先选择一个空闲电桩");
                return;
            }
            const qint64 chargerId = entry->data(Qt::UserRole).toLongLong();
            connection_->sendRequest("order.reserve", {{"chargerId", chargerId}});
            dialog.accept();
        });
        dialog.exec();
    } else if (type == "order.getActive.result") {
        const QJsonObject order = data.value("order").toObject();
        updateActiveOrder(data.value("order"));
        const qint64 orderId = static_cast<qint64>(order.value("id").toDouble());
        if (orderId > 0 && orderId != promptedActiveOrderId_) {
            promptedActiveOrderId_ = orderId;
            tabs_->setCurrentIndex(1);
            if (suppressNextActivePrompt_)
                suppressNextActivePrompt_ = false;
            else
                QMessageBox::information(this, "未完成订单",
                                         "您有未完成的充电订单，请先处理或结算");
        }
    } else if (type == "order.listMine.result") {
        updateOrderHistory(data.value("orders").toArray());
    } else if (type == "order.updated") {
        // Metering updates arrive once per second. Keep this path deliberately
        // lightweight and never take over the user's current tab.
        updateActiveOrder(data.value("order"));
        if (data.value("change").toString() == "state")
            connection_->sendRequest("order.listMine");
    } else if (type == "stations.changed") {
        connection_->sendRequest("station.list", {{"latitude", latitude_}, {"longitude", longitude_}});
    } else if (type == "user.updated") {
        updateProfile(data.value("profile").toObject());
    } else if (type == "user.statusChanged") {
        if (data.value("status").toString() == "frozen") {
            pollTimer_->stop();
            connection_->setSessionToken({});
            stack_->setCurrentIndex(0);
            loginStatus_->setText("账号已被管理员冻结，请联系管理员解冻");
            QMessageBox::warning(this, "账号状态", "账号已被管理员冻结，当前会话已退出");
        }
    } else if (type == "order.reserve.result" || type == "order.startCharging.result"
               || type == "order.cancelReservation.result" || type == "order.stopCharging.result"
               || type == "order.settle.result") {
        updateActiveOrder(data.value("order"));
        const qint64 orderId = static_cast<qint64>(data.value("order").toObject().value("id").toDouble());
        if (orderId > 0)
            promptedActiveOrderId_ = orderId;
        if (data.contains("profile"))
            updateProfile(data.value("profile").toObject());
        if (type == "order.reserve.result" || type == "order.startCharging.result"
            || type == "order.stopCharging.result") {
            tabs_->setCurrentIndex(1);
        }
    }
}

void UserWindow::showError(const QJsonObject &message)
{
    const QJsonObject error = message.value("error").toObject();
    const QString text = error.value("message").toString("操作失败");
    if (stack_->currentIndex() == 0) {
        loginPending_ = false;
        loginStatus_->setText(text);
    }
    else {
        if (error.value("code").toString() == "ACTIVE_ORDER_EXISTS") {
            suppressNextActivePrompt_ = true;
            tabs_->setCurrentIndex(1);
            connection_->sendRequest("order.getActive");
        }
        QMessageBox::warning(this, "操作未完成", text);
    }
}

void UserWindow::updateProfile(const QJsonObject &profile)
{
    phoneLabel_->setText(profile.value("phone").toString());
    nicknameEdit_->setText(profile.value("nickname").toString());
    balanceLabel_->setText(money(static_cast<qint64>(profile.value("balanceCents").toDouble())));
    avatarBase64_ = profile.value("avatarBase64").toString();
    if (!avatarBase64_.isEmpty()) {
        QPixmap pixmap;
        if (pixmap.loadFromData(QByteArray::fromBase64(avatarBase64_.toLatin1())))
            avatarLabel_->setPixmap(pixmap.scaled(92, 92, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        avatarLabel_->clear();
        avatarLabel_->setText("暂无头像");
    }
}

void UserWindow::updateStations(const QJsonObject &data)
{
    const QJsonArray stations = data.value("stations").toArray();
    stationList_->clear();
    for (const QJsonValue &value : stations) {
        const QJsonObject station = value.toObject();
        auto *entry = new QListWidgetItem(stationList_);
        entry->setSizeHint(QSize(0, 126));
        auto *card = new QFrame(stationList_);
        card->setObjectName("stationCard");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(11, 8, 11, 8);
        cardLayout->setSpacing(5);
        auto *titleRow = new QHBoxLayout;
        auto *name = new QLabel(station.value("name").toString(), card);
        name->setObjectName("stationName");
        auto *price = new QLabel(QStringLiteral("¥%1/度").arg(
                                     station.value("priceCentsPerKwh").toInt() / 100.0, 0, 'f', 2), card);
        price->setStyleSheet("color:#ff5a00;font-weight:700;");
        titleRow->addWidget(name, 1);
        titleRow->addWidget(price);
        auto *address = new QLabel(station.value("address").toString(), card);
        address->setObjectName("stationMeta");
        address->setWordWrap(true);
        auto *bottomRow = new QHBoxLayout;
        auto *availability = new QLabel(
            QStringLiteral("空闲 %1 / 共 %2").arg(station.value("idleCount").toInt())
                .arg(station.value("chargerCount").toInt()), card);
        auto *detail = new QPushButton("详情", card);
        detail->setObjectName("secondary");
        auto *navigate = new QPushButton(
            QStringLiteral("%1 km · 导航").arg(
                station.value("distanceMeters").toDouble() / 1000.0, 0, 'f', 2), card);
        bottomRow->addWidget(availability, 1);
        bottomRow->addWidget(detail);
        bottomRow->addWidget(navigate);
        cardLayout->addLayout(titleRow);
        cardLayout->addWidget(address);
        cardLayout->addLayout(bottomRow);
        const qint64 stationId = static_cast<qint64>(station.value("id").toDouble());
        connect(detail, &QPushButton::clicked, this, [this, stationId] {
            connection_->sendRequest("station.get", {{"stationId", stationId}});
        });
        connect(navigate, &QPushButton::clicked, this, [this, station, navigate] {
            openNavigation(station, navigate);
        });
        stationList_->setItemWidget(entry, card);
    }
}

void UserWindow::updateActiveOrder(const QJsonValue &value)
{
    const QJsonObject order = value.toObject();
    const QString status = order.value("status").toString();
    activeOrderId_ = static_cast<qint64>(order.value("id").toDouble());
    const bool reserved = status == "reserved";
    const bool charging = status == "charging";
    const bool pending = status == "pending_settlement";
    const qint64 energyWh = static_cast<qint64>(order.value("energyWh").toDouble());
    const qint64 targetEnergyWh = qMax<qint64>(1, static_cast<qint64>(order.value("targetEnergyWh").toDouble(20000)));
    const int progress = qBound(0, order.value("progressPercent").toInt(
                                       static_cast<int>(energyWh * 100 / targetEnergyWh)), 100);
    if (activeOrderId_ == 0 || (!reserved && !charging && !pending)) {
        activeOrderId_ = 0;
        activeState_->setText("无进行中订单");
        activeStation_->setText("-");
        activeCharger_->setText("-");
        activeEnergy_->setText("0.000 kWh");
        activeDuration_->setText("00:00:00");
        activeAmount_->setText("¥0.00");
        activeProgress_->setValue(0);
        activeProgressText_->setText("演示目标 20.0 kWh · 充满自动停止");
    } else {
        activeState_->setText(pending && progress >= 100 ? "已充满 · 待结算" : statusText(status));
        activeStation_->setText(order.value("stationName").toString());
        activeCharger_->setText(order.value("chargerCode").toString());
        activeEnergy_->setText(QStringLiteral("%1 kWh").arg(energyWh / 1000.0, 0, 'f', 3));
        const qint64 startedAt = static_cast<qint64>(order.value("startedAt").toDouble());
        const qint64 endedAt = static_cast<qint64>(order.value("endedAt").toDouble());
        const qint64 duration = startedAt > 0 ? qMax<qint64>(0, (endedAt > 0 ? endedAt : QDateTime::currentSecsSinceEpoch()) - startedAt) : 0;
        activeDuration_->setText(QStringLiteral("%1:%2:%3")
                                     .arg(duration / 3600, 2, 10, QLatin1Char('0'))
                                     .arg((duration / 60) % 60, 2, 10, QLatin1Char('0'))
                                     .arg(duration % 60, 2, 10, QLatin1Char('0')));
        activeAmount_->setText(money(static_cast<qint64>(order.value("amountCents").toDouble())));
        activeProgress_->setValue(progress);
        activeProgressText_->setText(QStringLiteral("已充 %1 / %2 kWh · 充满自动停止")
                                         .arg(energyWh / 1000.0, 0, 'f', 1)
                                         .arg(targetEnergyWh / 1000.0, 0, 'f', 1));
    }
    if (reserved) {
        chargeToggleButton_->setText("开始充电");
        chargeToggleButton_->setProperty("orderAction", "order.startCharging");
        chargeToggleButton_->setEnabled(true);
    } else if (charging) {
        chargeToggleButton_->setText("停止充电");
        chargeToggleButton_->setProperty("orderAction", "order.stopCharging");
        chargeToggleButton_->setEnabled(true);
    } else {
        chargeToggleButton_->setText(pending ? "充电已停止" : "开始充电");
        chargeToggleButton_->setProperty("orderAction", QString());
        chargeToggleButton_->setEnabled(false);
    }
    cancelButton_->setEnabled(reserved);
    settleButton_->setEnabled(pending);
}

void UserWindow::updateOrderHistory(const QJsonArray &orders)
{
    historyList_->clear();
    for (const QJsonValue &value : orders) {
        const QJsonObject order = value.toObject();
        auto *entry = new QListWidgetItem(
            QStringLiteral("%1  ·  %2\n%3  ·  %4 kWh  ·  %5  ·  %6")
                .arg(order.value("stationName").toString(), order.value("chargerCode").toString(),
                     statusText(order.value("status").toString()))
                .arg(order.value("energyWh").toDouble() / 1000.0, 0, 'f', 2)
                .arg(money(static_cast<qint64>(order.value("amountCents").toDouble())),
                     dateText(static_cast<qint64>(order.value("reservedAt").toDouble()))),
            historyList_);
        entry->setToolTip(order.value("orderNo").toString());
        entry->setSizeHint(QSize(0, 58));
    }
}

void UserWindow::requestInitialData()
{
    connection_->sendRequest("user.getProfile");
    connection_->sendRequest("station.list", {{"latitude", latitude_}, {"longitude", longitude_}});
    connection_->sendRequest("order.getActive");
    connection_->sendRequest("order.listMine");
}

void UserWindow::locateAddress()
{
    connection_->sendRequest("location.geocode", {{"address", addressEdit_->text().trimmed()}});
}

void UserWindow::openNavigation(const QJsonObject &station, QWidget *anchor)
{
    QMenu modes(this);
    QAction *drive = modes.addAction("驾车路线");
    QAction *walk = modes.addAction("步行路线");
    const QPoint position = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos();
    QAction *selected = modes.exec(position);
    if (selected != drive && selected != walk)
        return;
    MapDialog map(mapKey_, selected == walk ? "walk" : "drive", "当前位置",
                  latitude_, longitude_, station.value("name").toString(),
                  station.value("latitude").toDouble(), station.value("longitude").toDouble(), this);
    map.exec();
}

void UserWindow::saveProfile()
{
    connection_->sendRequest("user.updateProfile", {{"nickname", nicknameEdit_->text().trimmed()}, {"avatarBase64", avatarBase64_}});
}

void UserWindow::chooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(this, "选择头像", {}, "图片 (*.png *.jpg *.jpeg)");
    if (path.isEmpty())
        return;
    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, "头像", "无法读取该图片");
        return;
    }
    image = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPG", 82);
    avatarBase64_ = QString::fromLatin1(bytes.toBase64());
    avatarLabel_->setPixmap(QPixmap::fromImage(image).scaled(92, 92, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    saveProfile();
}

void UserWindow::recharge()
{
    bool ok = false;
    const double amount = rechargeEdit_->text().toDouble(&ok);
    if (!ok || amount <= 0) {
        QMessageBox::information(this, "充值", "请输入有效充值金额");
        return;
    }
    connection_->sendRequest("wallet.recharge", {{"amountCents", qRound64(amount * 100.0)}});
}

void UserWindow::actOnOrder(const QString &action)
{
    if (activeOrderId_ > 0)
        connection_->sendRequest(action, {{"orderId", activeOrderId_}});
}
