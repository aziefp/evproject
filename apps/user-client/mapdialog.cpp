#include "mapdialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

MapDialog::MapDialog(const QString &key, const QString &mode,
                     const QString &fromName, double fromLat, double fromLng,
                     const QString &toName, double toLat, double toLng,
                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(mode == "walk" ? "步行导航" : "驾车导航");
    setMinimumSize(360, 600);
    resize(420, 720);
    auto *layout = new QVBoxLayout(this);
    status_ = new QLabel("正在加载腾讯地图路线…", this);
    status_->setWordWrap(true);
    auto *toolbar = new QHBoxLayout;
    auto *reload = new QPushButton("重新加载", this);
    auto *browser = new QPushButton("在浏览器中打开", this);
    toolbar->addStretch();
    toolbar->addWidget(reload);
    toolbar->addWidget(browser);
    profile_ = new QWebEngineProfile(this);
    profile_->setHttpUserAgent(
        "Mozilla/5.0 (Linux; Android 13; Mobile) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/131.0 Mobile Safari/537.36");
    view_ = new QWebEngineView(this);
    view_->setPage(new QWebEnginePage(profile_, view_));
    layout->addWidget(status_);
    layout->addLayout(toolbar);
    layout->addWidget(view_, 1);

    routeUrl_ = QUrl("https://apis.map.qq.com/uri/v1/routeplan");
    QUrlQuery query;
    query.addQueryItem("type", mode == "walk" ? "walk" : "drive");
    query.addQueryItem("from", fromName);
    query.addQueryItem("fromcoord", QString::number(fromLat, 'f', 6) + "," + QString::number(fromLng, 'f', 6));
    query.addQueryItem("to", toName);
    query.addQueryItem("tocoord", QString::number(toLat, 'f', 6) + "," + QString::number(toLng, 'f', 6));
    query.addQueryItem("referer", key);
    routeUrl_.setQuery(query);

    connect(reload, &QPushButton::clicked, this, &MapDialog::loadRoute);
    connect(browser, &QPushButton::clicked, this, [this] {
        if (!QDesktopServices::openUrl(routeUrl_))
            status_->setText("无法打开系统浏览器，请检查桌面环境设置");
    });
    connect(view_, &QWebEngineView::loadStarted, this, [this] {
        status_->setText("正在连接腾讯地图…");
    });
    connect(view_, &QWebEngineView::loadProgress, this, [this](int progress) {
        status_->setText(QStringLiteral("正在加载腾讯地图… %1%").arg(progress));
    });
    connect(view_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        status_->setText(ok ? "路线已加载，可在地图中缩放和查看路线"
                            : "地图加载失败；可重新加载或在浏览器中打开");
    });
    connect(view_->page(), &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus, int) {
                status_->setText("地图渲染进程异常退出；请点击“重新加载”");
            });
    loadRoute();
}

MapDialog::~MapDialog()
{
    // The custom profile must outlive every page that uses it.
    delete view_;
    view_ = nullptr;
    delete profile_;
    profile_ = nullptr;
}

void MapDialog::loadRoute()
{
    view_->load(routeUrl_);
}
