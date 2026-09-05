#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QWebEngineProfile;
class QWebEngineView;

class MapDialog : public QDialog
{
    Q_OBJECT

public:
    MapDialog(const QString &key, const QString &mode,
              const QString &fromName, double fromLat, double fromLng,
              const QString &toName, double toLat, double toLng,
              QWidget *parent = nullptr);
    ~MapDialog() override;

private:
    void loadRoute();

    QLabel *status_ = nullptr;
    QWebEngineProfile *profile_ = nullptr;
    QWebEngineView *view_ = nullptr;
    QUrl routeUrl_;
};
