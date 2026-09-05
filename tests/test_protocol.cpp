#include "protocol.h"

#include <QJsonObject>
#include <QtTest>
#include <QtEndian>

class ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void decodesSplitFrame();
    void decodesMultipleFrames();
    void rejectsInvalidLength();
    void rejectsInvalidJson();
};

void ProtocolTest::roundTrip()
{
    const QJsonObject original{{"type", "ping"}, {"value", 42}};
    ev::FrameDecoder decoder;
    decoder.append(ev::encodeFrame(original));
    QString error;
    const auto frames = decoder.takeAvailable(&error);
    QCOMPARE(error, QString());
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first(), original);
}

void ProtocolTest::decodesSplitFrame()
{
    const QByteArray frame = ev::encodeFrame(QJsonObject{{"type", "split"}});
    ev::FrameDecoder decoder;
    decoder.append(frame.left(3));
    QCOMPARE(decoder.takeAvailable().size(), 0);
    decoder.append(frame.mid(3));
    QCOMPARE(decoder.takeAvailable().size(), 1);
}

void ProtocolTest::decodesMultipleFrames()
{
    ev::FrameDecoder decoder;
    decoder.append(ev::encodeFrame(QJsonObject{{"n", 1}})
                   + ev::encodeFrame(QJsonObject{{"n", 2}}));
    const auto frames = decoder.takeAvailable();
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).value("n").toInt(), 1);
    QCOMPARE(frames.at(1).value("n").toInt(), 2);
}

void ProtocolTest::rejectsInvalidLength()
{
    QByteArray invalid(sizeof(quint32), '\0');
    ev::FrameDecoder decoder;
    decoder.append(invalid);
    QString error;
    QCOMPARE(decoder.takeAvailable(&error).size(), 0);
    QVERIFY(!error.isEmpty());
    QCOMPARE(decoder.bufferedBytes(), 0);
}

void ProtocolTest::rejectsInvalidJson()
{
    const QByteArray payload("{not-json}");
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), frame.data());
    frame.append(payload);

    ev::FrameDecoder decoder;
    decoder.append(frame);
    QString error;
    QCOMPARE(decoder.takeAvailable(&error).size(), 0);
    QVERIFY(error.contains("invalid JSON"));
    QCOMPARE(decoder.bufferedBytes(), 0);
}

QTEST_MAIN(ProtocolTest)
#include "test_protocol.moc"
