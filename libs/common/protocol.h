#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace ev {

constexpr quint32 MaxFrameBytes = 1024U * 1024U;

QByteArray encodeFrame(const QJsonObject &message);

QJsonObject makeRequest(const QString &type,
                        const QString &requestId,
                        const QJsonObject &data = {},
                        const QString &sessionToken = {});
QJsonObject makeSuccess(const QJsonObject &request,
                        const QString &resultType,
                        const QJsonObject &data = {});
QJsonObject makeError(const QJsonObject &request,
                      const QString &code,
                      const QString &message,
                      bool retryable = false);

class FrameDecoder
{
public:
    void append(const QByteArray &bytes);
    QList<QJsonObject> takeAvailable(QString *error = nullptr);
    void clear();
    qsizetype bufferedBytes() const;

private:
    QByteArray buffer_;
};

} // namespace ev
