#include "models/radiostate.h"
#include "network/catpushbroadcaster.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class TestCatPushBroadcaster : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testDefaultAiModeIsZero();
    void testSetAiModeUpdatesLevel();
    void testRemoveClientDropsSubscription();
    void testPushFrequencyOnRadioStateChange();
    void testPollingClientReceivesNothing();
    void testTwoClientsDifferentAiModes();
    void testCompoundProcessingDiff();
    void testRitXitCompoundEmitsAllThree();

private:
    struct SocketPair {
        QTcpSocket *clientSide;
        QTcpSocket *serverSide;
    };

    SocketPair makeSocketPair();
    QByteArray waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs = 500);

    QTcpServer *m_listener = nullptr;
    QList<QTcpSocket *> m_managedSockets;
    RadioState *m_radioState = nullptr;
    CatPushBroadcaster *m_broadcaster = nullptr;
};

void TestCatPushBroadcaster::initTestCase() {
    m_listener = new QTcpServer(this);
    QVERIFY(m_listener->listen(QHostAddress::LocalHost, 0));
}

void TestCatPushBroadcaster::cleanupTestCase() {
    if (m_listener) {
        m_listener->close();
        delete m_listener;
        m_listener = nullptr;
    }
}

void TestCatPushBroadcaster::init() {
    m_radioState = new RadioState();
    m_broadcaster = new CatPushBroadcaster(m_radioState);
}

void TestCatPushBroadcaster::cleanup() {
    delete m_broadcaster;
    m_broadcaster = nullptr;
    delete m_radioState;
    m_radioState = nullptr;

    for (QTcpSocket *sock : m_managedSockets) {
        if (!sock) {
            continue;
        }
        if (sock->state() == QAbstractSocket::ConnectedState) {
            sock->disconnectFromHost();
            if (sock->state() != QAbstractSocket::UnconnectedState) {
                sock->waitForDisconnected(200);
            }
        }
        sock->deleteLater();
    }
    m_managedSockets.clear();
    QCoreApplication::processEvents();
}

TestCatPushBroadcaster::SocketPair TestCatPushBroadcaster::makeSocketPair() {
    QTcpSocket *clientSide = new QTcpSocket();
    m_managedSockets.append(clientSide);
    clientSide->connectToHost(QHostAddress::LocalHost, m_listener->serverPort());
    [&]() { QVERIFY(clientSide->waitForConnected(500)); }();

    [&]() { QVERIFY(m_listener->waitForNewConnection(500)); }();
    QTcpSocket *serverSide = m_listener->nextPendingConnection();
    Q_ASSERT(serverSide);
    m_managedSockets.append(serverSide);

    QCoreApplication::processEvents();
    return {clientSide, serverSide};
}

QByteArray TestCatPushBroadcaster::waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (sock->bytesAvailable() < minBytes && timer.elapsed() < timeoutMs) {
        sock->waitForReadyRead(50);
        QCoreApplication::processEvents();
    }
    return sock->readAll();
}

void TestCatPushBroadcaster::testDefaultAiModeIsZero() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), 0);
    QCOMPARE(m_broadcaster->subscriberCount(), 1);
}

void TestCatPushBroadcaster::testSetAiModeUpdatesLevel() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);

    const int modes[] = {0, 1, 2, 4};
    for (int m : modes) {
        m_broadcaster->setClientAiMode(pair.serverSide, m);
        QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), m);
    }
}

void TestCatPushBroadcaster::testRemoveClientDropsSubscription() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);
    QCOMPARE(m_broadcaster->subscriberCount(), 1);

    m_broadcaster->removeClient(pair.serverSide);
    QCOMPARE(m_broadcaster->subscriberCount(), 0);
    QCOMPARE(m_broadcaster->clientAiMode(pair.serverSide), 0);
}

void TestCatPushBroadcaster::testPushFrequencyOnRadioStateChange() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 14);
    QCOMPARE(received, QByteArray("FA00014074000;"));
}

void TestCatPushBroadcaster::testPollingClientReceivesNothing() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pair.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pair.clientSide->bytesAvailable(), qint64(0));
}

void TestCatPushBroadcaster::testTwoClientsDifferentAiModes() {
    auto pairA = makeSocketPair();
    auto pairB = makeSocketPair();

    m_broadcaster->addClient(pairA.serverSide);
    m_broadcaster->addClient(pairB.serverSide);
    m_broadcaster->setClientAiMode(pairA.serverSide, 0);
    m_broadcaster->setClientAiMode(pairB.serverSide, 2);

    m_radioState->parseCATCommand("FA00014074000;");
    QCoreApplication::processEvents();

    QByteArray receivedB = waitForBytes(pairB.clientSide, 14);
    QCOMPARE(receivedB, QByteArray("FA00014074000;"));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pairA.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pairA.clientSide->bytesAvailable(), qint64(0));
}

void TestCatPushBroadcaster::testCompoundProcessingDiff() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("NB051;");
    QCoreApplication::processEvents();

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 100) {
        pair.clientSide->waitForReadyRead(20);
        QCoreApplication::processEvents();
    }
    QCOMPARE(pair.clientSide->bytesAvailable(), qint64(0));

    m_radioState->parseCATCommand("NB050;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 4);
    QCOMPARE(received, QByteArray("NB0;"));
}

void TestCatPushBroadcaster::testRitXitCompoundEmitsAllThree() {
    auto pair = makeSocketPair();
    m_broadcaster->addClient(pair.serverSide);
    m_broadcaster->setClientAiMode(pair.serverSide, 2);

    m_radioState->parseCATCommand("RT1;");
    QCoreApplication::processEvents();

    QByteArray received = waitForBytes(pair.clientSide, 16);
    QCOMPARE(received, QByteArray("RO+0000;RT1;XT0;"));
}

QTEST_MAIN(TestCatPushBroadcaster)
#include "test_catpushbroadcaster.moc"
