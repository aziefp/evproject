#include "protocol.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QtEndian>

namespace ev {

QByteArray encodeFrame(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}

QJsonObject makeRequest(const QString &type,
                        const QString &requestId,
                        const QJsonObject &data,
                        const QString &sessionToken)
{
    QJsonObject request{
        {"v", EV_PROTOCOL_VERSION},
        {"type", type},
        {"requestId", requestId},
        {"timestampMs", QDateTime::currentMSecsSinceEpoch()},
        {"data", data},
    };
    if (!sessionToken.isEmpty())
        request.insert("sessionToken", sessionToken);
    return request;
}

QJsonObject makeSuccess(const QJsonObject &request,
                        const QString &resultType,
                        const QJsonObject &data)
{
    return {
        {"v", EV_PROTOCOL_VERSION},
        {"type", resultType},
        {"requestId", request.value("requestId").toString()},
        {"timestampMs", QDateTime::currentMSecsSinceEpoch()},
        {"ok", true},
        {"data", data},
    };
}

QJsonObject makeError(const QJsonObject &request,
                      const QString &code,
                      const QString &message,
                      bool retryable)
{
    return {
        {"v", EV_PROTOCOL_VERSION},
        {"type", "error"},
        {"requestId", request.value("requestId").toString()},
        {"timestampMs", QDateTime::currentMSecsSinceEpoch()},
        {"ok", false},
        {"error", QJsonObject{
             {"code", code},
             {"message", message},
             {"retryable", retryable},
         }},
    };
}

void FrameDecoder::append(const QByteArray &bytes)
{
    buffer_.append(bytes);
}

QList<QJsonObject> FrameDecoder::takeAvailable(QString *error)
{
    QList<QJsonObject> messages;
    if (error)
        error->clear();

    while (buffer_.size() >= static_cast<qsizetype>(sizeof(quint32))) {
        const quint32 payloadSize = qFromBigEndian<quint32>(buffer_.constData());
        if (payloadSize == 0 || payloadSize > MaxFrameBytes) {
            if (error)
                *error = QStringLiteral("invalid frame length: %1").arg(payloadSize);
            buffer_.clear();
            break;
        }

        const qsizetype frameSize = static_cast<qsizetype>(sizeof(quint32)) + payloadSize;
        if (buffer_.size() < frameSize)
            break;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            buffer_.mid(sizeof(quint32), payloadSize), &parseError);
        buffer_.remove(0, frameSize);

        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error)
                *error = QStringLiteral("invalid JSON payload: %1").arg(parseError.errorString());
            buffer_.clear();
            break;
        }
        messages.append(document.object());
    }

    return messages;
}

void FrameDecoder::clear()
{
    buffer_.clear();
}

qsizetype FrameDecoder::bufferedBytes() const
{
    return buffer_.size();
}

} // namespace ev
