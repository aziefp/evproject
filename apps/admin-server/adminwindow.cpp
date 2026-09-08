#include "adminwindow.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPieSeries>
#include <QPieSlice>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QValueAxis>
#include <QVBoxLayout>

namespace {

QString money(qint64 cents)
{
    return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2);
}

QString statusText(const QString &status)
{
    static const QHash<QString, QString> names{
        {"idle", "闲置"}, {"reserved", "已预约"}, {"charging", "充电中"},
        {"fault", "故障"}, {"restarting", "重启中"}, {"offline", "离线"},
        {"active", "正常"}, {"frozen", "冻结"}, {"pending_settlement", "待结算"},
        {"settled", "已结算"}, {"cancelled", "已取消"}
    };
    return names.value(status, status);
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setMinimumSectionSize(40);
    table->verticalHeader()->setDefaultSectionSize(44);
    table->setWordWrap(false);
    table->setAlternatingRowColors(true);
    return table;
}

QWidget *metricCard(const QString &title, QLabel **value)
{
    auto *frame = new QFrame;
    frame->setObjectName("metricCard");
    auto *layout = new QVBoxLayout(frame);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName("muted");
    *value = new QLabel("--");
    (*value)->setObjectName("metricValue");
    layout->addWidget(titleLabel);
    layout->addWidget(*value);
    return frame;
}

void prepareChart(QChart *chart)
{
    chart->setTheme(QChart::ChartThemeDark);
    chart->setBackgroundBrush(QColor("#232323"));
    chart->setBackgroundRoundness(0);
    chart->setMargins(QMargins(8, 8, 8, 8));
}

void replaceChart(QChartView *view, QChart *chart)
{
    QChart *oldChart = view->chart();
    view->setChart(chart);
    if (oldChart)
        oldChart->deleteLater();
}

} // namespace

AdminWindow::AdminWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("充电桩运营管理后台");
    resize(1280, 800);
    stack_ = new QStackedWidget;
    setCentralWidget(stack_);
    buildLoginPage();
    buildMainPage();
    stack_->setCurrentIndex(0);

    setStyleSheet(R"(
        QMainWindow, QDialog, QWidget { background:#181818; color:#f2f2f2; font-size:14px; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            background:#292929; color:#f2f2f2; border:1px solid #555555;
            border-radius:2px; padding:8px; selection-background-color:#ff5a00;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border:1px solid #ff5a00; }
        QComboBox QAbstractItemView { background:#292929; color:#f2f2f2; selection-background-color:#ff5a00; }
        QPushButton { background:#ff5a00; color:white; border:0; border-radius:2px; padding:8px 16px; font-weight:700; }
        QPushButton:hover { background:#ff7426; }
        QPushButton:pressed { background:#d94c00; }
        QPushButton:disabled { background:#3a3a3a; color:#858585; }
        QPushButton#danger { background:#b42318; }
        QPushButton#danger:hover { background:#d13a2e; }
        QWidget#tableActions { background:transparent; }
        QPushButton#tableAction { min-width:76px; padding:6px 10px; }
        QPushButton#tableDanger { min-width:76px; padding:6px 10px; background:#b42318; }
        QPushButton#tableDanger:hover { background:#d13a2e; }
        QTabWidget::pane { border:1px solid #3b3b3b; top:-1px; }
        QTabBar::tab { background:#242424; color:#bdbdbd; padding:11px 22px; border-right:1px solid #3b3b3b; }
        QTabBar::tab:selected { background:#ff5a00; color:white; }
        QTabBar::tab:hover:!selected { background:#333333; color:#ffffff; }
        QTableWidget { background:#202020; alternate-background-color:#272727; gridline-color:#3b3b3b; border:1px solid #3b3b3b; }
        QTableWidget::item { padding:5px; }
        QTableWidget::item:selected { background:#9f3b08; color:white; }
        QListWidget { background:#202020; border:1px solid #3b3b3b; border-radius:0; padding:4px; }
        QListWidget::item { background:#292929; border-radius:0; margin:3px; padding:8px; }
        QListWidget::item:selected { background:#9f3b08; color:white; }
        QHeaderView::section { background:#303030; color:#f0f0f0; padding:8px; border:0; border-right:1px solid #474747; }
        QFrame#metricCard { background:#242424; border:1px solid #444444; border-radius:2px; }
        QFrame#metricCard QLabel { background:transparent; }
        QFrame#dashboardPanel { background:#232323; border:1px solid #444444; border-radius:2px; }
        QLabel#metricValue { color:#ff6a16; font-size:24px; font-weight:700; }
        QLabel#faultTitle { color:#ff665c; background:transparent; font-size:17px; font-weight:700; }
        QLabel#muted { color:#a0a0a0; }
    )");
}

void AdminWindow::buildLoginPage()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->addStretch();
    auto *panel = new QFrame;
    panel->setMaximumWidth(420);
    panel->setObjectName("metricCard");
    auto *form = new QVBoxLayout(panel);
    auto *title = new QLabel("充电桩运营管理后台");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:26px;font-weight:700;color:#ff5a00;padding:18px;");
    usernameEdit_ = new QLineEdit("admin");
    usernameEdit_->setPlaceholderText("管理员账号");
    passwordEdit_ = new QLineEdit("123456");
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText("密码");
    auto *button = new QPushButton("登录");
    loginMessage_ = new QLabel;
    loginMessage_->setAlignment(Qt::AlignCenter);
    loginServerStatus_ = new QLabel("服务器正在启动…");
    loginServerStatus_->setObjectName("muted");
    loginServerStatus_->setAlignment(Qt::AlignCenter);
    form->addWidget(title);
    form->addWidget(usernameEdit_);
    form->addWidget(passwordEdit_);
    form->addWidget(button);
    form->addWidget(loginMessage_);
    form->addWidget(loginServerStatus_);
    outer->addWidget(panel, 0, Qt::AlignHCenter);
    outer->addStretch();
    stack_->addWidget(page);

    connect(button, &QPushButton::clicked, this, [this]() {
        loginMessage_->setText("正在验证…");
        sendCommand("admin.login", {{"username", usernameEdit_->text()}, {"password", passwordEdit_->text()}});
    });
    connect(passwordEdit_, &QLineEdit::returnPressed, button, &QPushButton::click);
}

void AdminWindow::buildMainPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    auto *top = new QHBoxLayout;
    auto *title = new QLabel("充电运营管理");
    title->setStyleSheet("font-size:21px;font-weight:700;color:#ff5a00;");
    auto *status = new QLabel("服务状态将在登录后显示");
    status->setObjectName("muted");
    serverStatus_ = status;
    auto *refresh = new QPushButton("刷新全部");
    top->addWidget(title);
    top->addStretch();
    top->addWidget(status);
    top->addWidget(refresh);
    root->addLayout(top);

    tabs_ = new QTabWidget;
    root->addWidget(tabs_);

    auto *dashboard = new QWidget;
    auto *dashboardLayout = new QVBoxLayout(dashboard);
    auto *metrics = new QHBoxLayout;
    metrics->addWidget(metricCard("今日营收", &todayRevenue_));
    metrics->addWidget(metricCard("本月营收", &monthRevenue_));
    metrics->addWidget(metricCard("累计营收", &totalRevenue_));
    metrics->addWidget(metricCard("今日订单", &todayOrders_));
    metrics->addWidget(metricCard("注册用户", &userCount_));
    dashboardLayout->addLayout(metrics);

    auto *statusRow = new QHBoxLayout;
    chargerStatusChart_ = new QChartView;
    chargerStatusChart_->setRenderHint(QPainter::Antialiasing);
    chargerStatusChart_->setMinimumHeight(220);
    statusRow->addWidget(chargerStatusChart_, 3);
    auto *faultPanel = new QFrame(dashboard);
    faultPanel->setObjectName("dashboardPanel");
    auto *faultLayout = new QVBoxLayout(faultPanel);
    auto *faultTitle = new QLabel("故障电桩", faultPanel);
    faultTitle->setObjectName("faultTitle");
    faultChargerList_ = new QListWidget(faultPanel);
    faultChargerList_->setSelectionMode(QAbstractItemView::NoSelection);
    faultLayout->addWidget(faultTitle);
    faultLayout->addWidget(faultChargerList_);
    statusRow->addWidget(faultPanel, 2);
    dashboardLayout->addLayout(statusRow);

    auto *chartRow = new QHBoxLayout;
    auto *trendPanel = new QWidget(dashboard);
    auto *trendLayout = new QVBoxLayout(trendPanel);
    trendLayout->setContentsMargins(0, 0, 0, 0);
    auto *trendTools = new QHBoxLayout;
    trendRange_ = new QComboBox(dashboard);
    trendRange_->addItem("近 7 日", 7);
    trendRange_->addItem("近 30 日", 30);
    trendRange_->setCurrentIndex(1);
    trendTools->addWidget(new QLabel("营收趋势范围：", dashboard));
    trendTools->addWidget(trendRange_);
    trendTools->addStretch();
    trendLayout->addLayout(trendTools);
    revenueChart_ = new QChartView;
    revenueChart_->setRenderHint(QPainter::Antialiasing);
    revenueChart_->setMinimumHeight(330);
    trendLayout->addWidget(revenueChart_);
    chartRow->addWidget(trendPanel, 1);
    stationRevenueChart_ = new QChartView;
    stationRevenueChart_->setRenderHint(QPainter::Antialiasing);
    stationRevenueChart_->setMinimumWidth(420);
    stationRevenueChart_->setMinimumHeight(330);
    chartRow->addWidget(stationRevenueChart_, 1);
    dashboardLayout->addLayout(chartRow);
    tabs_->addTab(dashboard, "数据总览");

    auto *stationsPage = new QWidget;
    auto *stationsLayout = new QVBoxLayout(stationsPage);
    auto *addStationButton = new QPushButton("新增电站");
    stationsTable_ = makeTable({"ID", "电站名称", "地址", "纬度", "经度", "电价", "总桩数", "在线率", "操作"});
    auto *stationActions = new QHBoxLayout;
    auto *stationHint = new QLabel("点击行末“查看详情”查看站内电桩", stationsPage);
    stationHint->setObjectName("muted");
    stationActions->addWidget(addStationButton);
    stationActions->addWidget(stationHint);
    stationActions->addStretch();
    stationsLayout->addLayout(stationActions);
    stationsLayout->addWidget(stationsTable_);
    tabs_->addTab(stationsPage, "电站管理");

    auto *chargersPage = new QWidget;
    auto *chargersLayout = new QVBoxLayout(chargersPage);
    auto *chargerTools = new QHBoxLayout;
    auto *chargerFilterLabel = new QLabel("电桩状态：", chargersPage);
    chargerStatusFilter_ = new QComboBox(chargersPage);
    chargerStatusFilter_->addItem("全部状态", "");
    chargerStatusFilter_->addItem("闲置", "idle");
    chargerStatusFilter_->addItem("已预约", "reserved");
    chargerStatusFilter_->addItem("充电中", "charging");
    chargerStatusFilter_->addItem("故障", "fault");
    chargerStatusFilter_->addItem("重启中", "restarting");
    chargerStatusFilter_->addItem("离线", "offline");
    auto *chargerHint = new QLabel("空闲/离线电桩可报告故障，故障电桩可远程重启；运行中电桩不允许直接改状态", chargersPage);
    chargerHint->setObjectName("muted");
    chargerTools->addWidget(chargerFilterLabel);
    chargerTools->addWidget(chargerStatusFilter_);
    chargerTools->addWidget(chargerHint);
    chargerTools->addStretch();
    chargersLayout->addLayout(chargerTools);
    chargersTable_ = makeTable({"ID", "电桩编号", "所属电站", "类型", "功率", "状态", "累计次数", "累计时长", "操作"});
    chargersLayout->addWidget(chargersTable_);
    tabs_->addTab(chargersPage, "电桩管理");

    auto *ordersPage = new QWidget;
    auto *ordersLayout = new QVBoxLayout(ordersPage);
    auto *orderTools = new QHBoxLayout;
    auto *orderFilterLabel = new QLabel("订单状态：", ordersPage);
    orderStatusFilter_ = new QComboBox(ordersPage);
    orderStatusFilter_->addItem("全部状态", "");
    orderStatusFilter_->addItem("已预约", "reserved");
    orderStatusFilter_->addItem("充电中", "charging");
    orderStatusFilter_->addItem("待结算", "pending_settlement");
    orderStatusFilter_->addItem("已结算", "settled");
    orderStatusFilter_->addItem("已取消", "cancelled");
    auto *orderHint = new QLabel("待结算订单可在行末代结算", ordersPage);
    orderHint->setObjectName("muted");
    orderTools->addWidget(orderFilterLabel);
    orderTools->addWidget(orderStatusFilter_);
    orderTools->addWidget(orderHint);
    orderTools->addStretch();
    ordersLayout->addLayout(orderTools);
    ordersTable_ = makeTable({"ID", "订单号", "电站", "电桩", "状态", "电量", "金额", "开始时间", "结束时间", "操作"});
    ordersLayout->addWidget(ordersTable_);
    tabs_->addTab(ordersPage, "订单管理");

    auto *usersPage = new QWidget;
    auto *usersLayout = new QVBoxLayout(usersPage);
    auto *userTools = new QHBoxLayout;
    userSearch_ = new QLineEdit;
    userSearch_->setPlaceholderText("按手机号搜索");
    auto *searchButton = new QPushButton("查询");
    userTools->addWidget(userSearch_);
    userTools->addWidget(searchButton);
    auto *userHint = new QLabel("可在行末冻结或解冻用户", usersPage);
    userHint->setObjectName("muted");
    userTools->addWidget(userHint);
    userTools->addStretch();
    usersLayout->addLayout(userTools);
    usersTable_ = makeTable({"ID", "手机号", "昵称", "钱包余额", "注册时间", "状态", "操作"});
    usersLayout->addWidget(usersTable_);
    tabs_->addTab(usersPage, "用户管理");

    stack_->addWidget(page);
    connect(refresh, &QPushButton::clicked, this, &AdminWindow::refreshAll);
    connect(addStationButton, &QPushButton::clicked, this, &AdminWindow::addStation);
    connect(searchButton, &QPushButton::clicked, this, [this]() { sendCommand("users.list", {{"phoneFilter", userSearch_->text()}}); });
    connect(userSearch_, &QLineEdit::returnPressed, searchButton, &QPushButton::click);
    connect(trendRange_, &QComboBox::currentIndexChanged, this, [this] {
        if (!lastDashboard_.isEmpty())
            updateDashboard(lastDashboard_);
    });
    connect(orderStatusFilter_, &QComboBox::currentIndexChanged, this, [this] {
        if (loggedIn_)
            sendCommand("orders.list", {{"statusFilter", orderStatusFilter_->currentData().toString()}});
    });
    connect(chargerStatusFilter_, &QComboBox::currentIndexChanged, this, [this] {
        if (loggedIn_)
            sendCommand("chargers.list", {{"statusFilter", chargerStatusFilter_->currentData().toString()}});
    });
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(3000);
    connect(refreshTimer_, &QTimer::timeout, this, &AdminWindow::refreshAll);
}

quint64 AdminWindow::sendCommand(const QString &action, const QJsonObject &data)
{
    const quint64 id = nextRequestId_++;
    emit adminCommand(id, action, data);
    return id;
}

void AdminWindow::handleAdminResult(quint64, const QString &action, bool ok,
                                    const QJsonObject &data, const QString &message)
{
    if (!ok) {
        if (action == "admin.login")
            loginMessage_->setText(message);
        else
            QMessageBox::warning(this, "操作失败", message);
        return;
    }
    if (action == "admin.login") {
        loggedIn_ = true;
        stack_->setCurrentIndex(1);
        refreshTimer_->start();
        refreshAll();
    } else if (action == "dashboard.get") {
        updateDashboard(data);
    } else if (action == "stations.list") {
        updateStations(data);
    } else if (action == "station.get") {
        showStationDetail(data);
    } else if (action == "chargers.list") {
        updateChargers(data);
    } else if (action == "users.list") {
        updateUsers(data);
    } else if (action == "orders.list") {
        updateOrders(data);
    } else {
        refreshAll();
    }
}

void AdminWindow::setServerStatus(const QString &message)
{
    if (loginServerStatus_)
        loginServerStatus_->setText(message);
    if (serverStatus_)
        serverStatus_->setText(message);
}

void AdminWindow::refreshAll()
{
    if (!loggedIn_)
        return;
    sendCommand("dashboard.get");
    sendCommand("stations.list");
    sendCommand("chargers.list", {{"statusFilter", chargerStatusFilter_->currentData().toString()}});
    sendCommand("users.list", {{"phoneFilter", userSearch_->text()}});
    sendCommand("orders.list", {{"statusFilter", orderStatusFilter_->currentData().toString()}});
}

void AdminWindow::updateDashboard(const QJsonObject &data)
{
    lastDashboard_ = data;
    const QJsonObject metrics = data.value("metrics").toObject();
    todayRevenue_->setText(money(static_cast<qint64>(metrics.value("todayRevenueCents").toDouble())));
    monthRevenue_->setText(money(static_cast<qint64>(metrics.value("monthRevenueCents").toDouble())));
    totalRevenue_->setText(money(static_cast<qint64>(metrics.value("totalRevenueCents").toDouble())));
    todayOrders_->setText(QString::number(metrics.value("todayOrders").toInt()));
    userCount_->setText(QString::number(metrics.value("userCount").toInt()));
    const QJsonObject counts = data.value("chargerCounts").toObject();
    const int idle = counts.value("idle").toInt();
    const int charging = counts.value("charging").toInt();
    const int reserved = counts.value("reserved").toInt();
    const int fault = counts.value("fault").toInt();
    const int restarting = counts.value("restarting").toInt();
    const int offline = counts.value("offline").toInt();
    const QList<QPair<QString, int>> statusRows{
        {"在用（充电/预约）", charging + reserved},
        {"闲置", idle},
        {"故障", fault},
        {"离线/重启中", offline + restarting}};

    auto *statusSeries = new QPieSeries;
    statusSeries->setHoleSize(0.38);
    const QStringList statusColors{"#ff5a00", "#35b66b", "#ff5d52", "#858585"};
    for (int i = 0; i < statusRows.size(); ++i) {
        const auto &entry = statusRows.at(i);
        auto *slice = statusSeries->append(entry.first, entry.second);
        slice->setColor(QColor(statusColors.at(i)));
        slice->setLabel(QStringLiteral("%1  %2").arg(entry.first).arg(entry.second));
    }
    auto *statusChart = new QChart;
    prepareChart(statusChart);
    statusChart->setTitle("电桩状态分布");
    statusChart->addSeries(statusSeries);
    statusChart->legend()->setAlignment(Qt::AlignBottom);
    replaceChart(chargerStatusChart_, statusChart);

    faultChargerList_->clear();
    const QJsonArray faultChargers = data.value("faultChargers").toArray();
    if (faultChargers.isEmpty()) {
        faultChargerList_->addItem("✓ 当前无故障电桩");
    } else {
        for (const QJsonValue &value : faultChargers) {
            const QJsonObject charger = value.toObject();
            faultChargerList_->addItem(QStringLiteral("●  %1\n    %2")
                                           .arg(charger.value("code").toString(),
                                                charger.value("stationName").toString()));
        }
    }

    auto *series = new QLineSeries;
    const QJsonArray trend = data.value("trend").toArray();
    const int days = trendRange_ ? trendRange_->currentData().toInt() : 30;
    const int first = qMax(0, trend.size() - days);
    qreal maxValue = 1;
    for (int i = first; i < trend.size(); ++i) {
        const qreal value = trend.at(i).toObject().value("revenueCents").toDouble() / 100.0;
        series->append(i - first, value);
        maxValue = qMax(maxValue, value);
    }
    series->setName("营收（元）");
    QPen trendPen(QColor("#ff6a16"));
    trendPen.setWidthF(3.2);
    series->setPen(trendPen);
    auto *chart = new QChart;
    prepareChart(chart);
    chart->setTitle(QStringLiteral("近 %1 日营收趋势").arg(days));
    chart->addSeries(series);
    auto *axisX = new QValueAxis;
    axisX->setRange(0, qMax(1, trend.size() - first - 1));
    axisX->setLabelFormat("%d");
    axisX->setTitleText("时间（日）");
    auto *axisY = new QValueAxis;
    axisY->setRange(0, maxValue * 1.2);
    axisY->setTitleText("元");
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);
    replaceChart(revenueChart_, chart);

    auto *revenueSet = new QBarSet("营收（元）");
    revenueSet->setColor(QColor("#ff7a2f"));
    QStringList stationNames;
    qreal maxStationRevenue = 1;
    const QJsonArray ranking = data.value("stationRevenue").toArray();
    for (const QJsonValue &value : ranking) {
        const QJsonObject station = value.toObject();
        QString name = station.value("name").toString();
        name.remove("深圳市");
        name.remove("充电站");
        if (name.size() > 7)
            name = name.left(7) + "…";
        const qreal revenue = station.value("revenueCents").toDouble() / 100.0;
        stationNames.append(name);
        *revenueSet << revenue;
        maxStationRevenue = qMax(maxStationRevenue, revenue);
    }
    auto *rankingSeries = new QBarSeries;
    rankingSeries->append(revenueSet);
    rankingSeries->setBarWidth(0.62);
    auto *rankingChart = new QChart;
    prepareChart(rankingChart);
    rankingChart->setTitle("电站营收排名 TOP 5");
    rankingChart->addSeries(rankingSeries);
    rankingChart->legend()->hide();
    auto *rankingAxisX = new QBarCategoryAxis;
    rankingAxisX->append(stationNames);
    QFont rankingLabelFont = rankingAxisX->labelsFont();
    rankingLabelFont.setPointSize(8);
    rankingAxisX->setLabelsFont(rankingLabelFont);
    rankingAxisX->setLabelsAngle(-40);
    auto *rankingAxisY = new QValueAxis;
    rankingAxisY->setRange(0, maxStationRevenue * 1.2);
    rankingAxisY->setLabelFormat("%.0f");
    rankingAxisY->setTitleText("元");
    rankingChart->addAxis(rankingAxisX, Qt::AlignBottom);
    rankingChart->addAxis(rankingAxisY, Qt::AlignLeft);
    rankingSeries->attachAxis(rankingAxisX);
    rankingSeries->attachAxis(rankingAxisY);
    replaceChart(stationRevenueChart_, rankingChart);
}

void AdminWindow::updateStations(const QJsonObject &data)
{
    const QJsonArray array = data.value("stations").toArray();
    stationsTable_->setRowCount(array.size());
    for (int row = 0; row < array.size(); ++row) {
        const QJsonObject item = array.at(row).toObject();
        const QStringList values{
            QString::number(static_cast<qint64>(item.value("id").toDouble())), item.value("name").toString(),
            item.value("address").toString(), QString::number(item.value("latitude").toDouble(), 'f', 6),
            QString::number(item.value("longitude").toDouble(), 'f', 6), money(item.value("priceCentsPerKwh").toInt()),
            QString::number(item.value("chargerCount").toInt()),
            QStringLiteral("%1%").arg(item.value("chargerCount").toInt() > 0
                                           ? qRound(item.value("onlineCount").toInt() * 100.0 / item.value("chargerCount").toInt()) : 0)
        };
        for (int column = 0; column < values.size(); ++column)
            stationsTable_->setItem(row, column, new QTableWidgetItem(values.at(column)));
        auto *detail = new QPushButton("查看详情", stationsTable_);
        detail->setObjectName("tableAction");
        const qint64 stationId = static_cast<qint64>(item.value("id").toDouble());
        connect(detail, &QPushButton::clicked, this, [this, stationId] {
            sendCommand("station.get", {{"stationId", stationId}});
        });
        stationsTable_->setCellWidget(row, 8, detail);
    }
    stationsTable_->resizeColumnsToContents();
    stationsTable_->horizontalHeader()->setStretchLastSection(false);
    stationsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    stationsTable_->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    stationsTable_->setColumnWidth(8, 124);
}

void AdminWindow::showStationDetail(const QJsonObject &data)
{
    const QJsonObject station = data.value("station").toObject();
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 · 站内电桩").arg(station.value("name").toString()));
    dialog.resize(760, 430);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(station.value("address").toString(), &dialog));
    auto *table = makeTable({"编号", "类型", "功率", "状态", "累计次数", "累计时长"});
    const QJsonArray chargers = station.value("chargers").toArray();
    table->setRowCount(chargers.size());
    for (int row = 0; row < chargers.size(); ++row) {
        const QJsonObject charger = chargers.at(row).toObject();
        const QStringList values{
            charger.value("code").toString(), charger.value("type").toString() == "fast" ? "快充" : "慢充",
            QStringLiteral("%1 kW").arg(charger.value("powerWatts").toInt() / 1000.0, 0, 'f', 1),
            statusText(charger.value("status").toString()), QString::number(charger.value("totalSessions").toInt()),
            QStringLiteral("%1 h").arg(charger.value("totalDurationSeconds").toDouble() / 3600.0, 0, 'f', 1)};
        for (int column = 0; column < values.size(); ++column)
            table->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void AdminWindow::updateChargers(const QJsonObject &data)
{
    const QJsonArray array = data.value("chargers").toArray();
    chargersTable_->setRowCount(array.size());
    for (int row = 0; row < array.size(); ++row) {
        const QJsonObject item = array.at(row).toObject();
        const QStringList values{
            QString::number(static_cast<qint64>(item.value("id").toDouble())), item.value("code").toString(),
            item.value("stationName").toString(), item.value("type").toString() == "fast" ? "快充" : "慢充",
            QStringLiteral("%1 kW").arg(item.value("powerWatts").toInt() / 1000.0, 0, 'f', 1),
            statusText(item.value("status").toString()), QString::number(item.value("totalSessions").toInt()),
            QStringLiteral("%1 h").arg(item.value("totalDurationSeconds").toDouble() / 3600.0, 0, 'f', 1)
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *cell = new QTableWidgetItem(values.at(column));
            cell->setData(Qt::UserRole, item.value("status").toString());
            chargersTable_->setItem(row, column, cell);
        }
        const QString chargerStatus = item.value("status").toString();
        const qint64 chargerId = static_cast<qint64>(item.value("id").toDouble());
        const QString chargerCode = item.value("code").toString();
        auto *actions = new QWidget(chargersTable_);
        actions->setObjectName("tableActions");
        auto *actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(3, 2, 3, 2);
        actionLayout->setSpacing(5);
        auto *reportFault = new QPushButton("报告故障", actions);
        reportFault->setObjectName("tableDanger");
        reportFault->setEnabled(chargerStatus == "idle" || chargerStatus == "offline");
        auto *restart = new QPushButton("远程重启", actions);
        restart->setObjectName("tableAction");
        restart->setEnabled(chargerStatus == "fault");
        connect(reportFault, &QPushButton::clicked, this, [this, chargerId, chargerCode] {
            if (QMessageBox::question(this, "报告电桩故障",
                                      QStringLiteral("确认将电桩 %1 设为故障状态？").arg(chargerCode),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes) {
                sendCommand("charger.reportFault", {{"chargerId", chargerId}});
            }
        });
        connect(restart, &QPushButton::clicked, this, [this, chargerId] {
            sendCommand("charger.restart", {{"chargerId", chargerId}});
        });
        actionLayout->addWidget(reportFault);
        actionLayout->addWidget(restart);
        chargersTable_->setCellWidget(row, 8, actions);
    }
    chargersTable_->resizeColumnsToContents();
    chargersTable_->horizontalHeader()->setStretchLastSection(false);
    chargersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    chargersTable_->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    chargersTable_->setColumnWidth(8, 206);
}

void AdminWindow::updateUsers(const QJsonObject &data)
{
    const QJsonArray array = data.value("users").toArray();
    usersTable_->setRowCount(array.size());
    for (int row = 0; row < array.size(); ++row) {
        const QJsonObject item = array.at(row).toObject();
        const QStringList values{
            QString::number(static_cast<qint64>(item.value("id").toDouble())), item.value("phone").toString(),
            item.value("nickname").toString(), money(static_cast<qint64>(item.value("balanceCents").toDouble())),
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(item.value("createdAt").toDouble())).toString("yyyy-MM-dd HH:mm"),
            statusText(item.value("status").toString())
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *cell = new QTableWidgetItem(values.at(column));
            cell->setData(Qt::UserRole, item.value("status").toString());
            usersTable_->setItem(row, column, cell);
        }
        const QString userStatus = item.value("status").toString();
        auto *toggle = new QPushButton(userStatus == "frozen" ? "解冻" : "冻结", usersTable_);
        toggle->setObjectName(userStatus == "frozen" ? "tableAction" : "tableDanger");
        const qint64 userId = static_cast<qint64>(item.value("id").toDouble());
        connect(toggle, &QPushButton::clicked, this, [this, userId, userStatus] {
            sendCommand("user.setStatus", {{"userId", userId},
                                             {"status", userStatus == "frozen" ? "active" : "frozen"}});
        });
        usersTable_->setCellWidget(row, 6, toggle);
    }
    usersTable_->resizeColumnsToContents();
    usersTable_->horizontalHeader()->setStretchLastSection(false);
    usersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    usersTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    usersTable_->setColumnWidth(6, 108);
}

void AdminWindow::updateOrders(const QJsonObject &data)
{
    const QJsonArray array = data.value("orders").toArray();
    ordersTable_->setRowCount(array.size());
    for (int row = 0; row < array.size(); ++row) {
        const QJsonObject item = array.at(row).toObject();
        auto time = [&item](const char *key) {
            const qint64 seconds = static_cast<qint64>(item.value(key).toDouble());
            return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds).toString("MM-dd HH:mm") : "--";
        };
        const QStringList values{
            QString::number(static_cast<qint64>(item.value("id").toDouble())), item.value("orderNo").toString(),
            item.value("stationName").toString(), item.value("chargerCode").toString(), statusText(item.value("status").toString()),
            QStringLiteral("%1 kWh").arg(item.value("energyWh").toDouble() / 1000.0, 0, 'f', 2),
            money(static_cast<qint64>(item.value("amountCents").toDouble())), time("startedAt"), time("endedAt")
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *cell = new QTableWidgetItem(values.at(column));
            cell->setData(Qt::UserRole, item.value("status").toString());
            ordersTable_->setItem(row, column, cell);
        }
        const QString orderStatus = item.value("status").toString();
        auto *settle = new QPushButton("代结算", ordersTable_);
        settle->setObjectName("tableAction");
        settle->setEnabled(orderStatus == "pending_settlement");
        const qint64 orderId = static_cast<qint64>(item.value("id").toDouble());
        connect(settle, &QPushButton::clicked, this, [this, orderId] {
            sendCommand("order.settle", {{"orderId", orderId}});
        });
        ordersTable_->setCellWidget(row, 9, settle);
    }
    ordersTable_->resizeColumnsToContents();
    ordersTable_->horizontalHeader()->setStretchLastSection(false);
    ordersTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ordersTable_->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    ordersTable_->setColumnWidth(9, 112);
}

void AdminWindow::addStation()
{
    QDialog dialog(this);
    dialog.setWindowTitle("新增充电站");
    auto *layout = new QFormLayout(&dialog);
    QLineEdit name;
    QLineEdit address;
    QDoubleSpinBox latitude;
    QDoubleSpinBox longitude;
    QDoubleSpinBox price;
    QSpinBox count;
    latitude.setRange(-90, 90); latitude.setDecimals(6); latitude.setValue(22.543687);
    longitude.setRange(-180, 180); longitude.setDecimals(6); longitude.setValue(114.059625);
    price.setRange(0.01, 99.99); price.setDecimals(2); price.setValue(1.20); price.setSuffix(" 元/度");
    count.setRange(1, 50); count.setValue(6);
    layout->addRow("站名", &name); layout->addRow("地址", &address); layout->addRow("纬度", &latitude);
    layout->addRow("经度", &longitude); layout->addRow("价格", &price); layout->addRow("电桩数量", &count);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    sendCommand("station.add", {{"name", name.text()}, {"address", address.text()},
                                 {"latitude", latitude.value()}, {"longitude", longitude.value()},
                                 {"priceCentsPerKwh", qRound(price.value() * 100)}, {"chargerCount", count.value()}});
}
