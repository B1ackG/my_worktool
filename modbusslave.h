#ifndef MODBUSSLAVE_H
#define MODBUSSLAVE_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>
#include <QMutex>
#include <QMap>
#include <QTimer>

class ModbusSlave : public QObject
{
    Q_OBJECT
public:
    explicit ModbusSlave(QObject *parent = nullptr);
    ~ModbusSlave();

    bool start(quint16 port, quint8 unitId = 1, const QString &bindAddr = QString());
    void stop();
    bool isRunning() const;
    int clientCount() const { return clients.size(); }

    // Register access
    bool setRegister(quint16 addr, quint16 value);
    quint16 getRegister(quint16 addr) const;
    bool setRegisterBit(quint16 addr, int bitIndex, bool value);
    bool setFloat(quint16 addr, float value);
    float getFloat(quint16 addr) const;

    // Export / Import full holding register snapshot
    QVector<quint16> exportHolding() const;
    void loadHolding(const QVector<quint16> &data);

    // Network fault injection
    void setFixedDelayMs(int ms);
    void setDropProbability(double prob); // 0.0 - 1.0

    // Exception injection (per-function or per-address)
    void injectExceptionForFunction(quint8 func, quint8 exceptionCode);
    void injectExceptionForAddress(quint16 addr, quint8 exceptionCode);
    void clearInjectedExceptions();

signals:
    void logMessage(const QString &msg);
    void clientConnected();
    void clientDisconnected();
    void registerOperation(quint16 addr, quint16 value, const QString &opType);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    QTcpServer *server;
    QList<QTcpSocket*> clients;
    QVector<quint16> holding;
    mutable QMutex mutex;
    quint8 unit;

    // Fault injection / network simulation
    int fixedDelayMs;
    double dropProbability;
    QMap<quint8, quint8> funcExceptions;
    QMap<quint16, quint8> addrExceptions;

    bool shouldDrop() const;

    void processRequest(QTcpSocket *sock, const QByteArray &data);
    QByteArray buildResponse(quint16 transactionId, quint8 unitId, const QByteArray &pdu);
};

#endif // MODBUSSLAVE_H
