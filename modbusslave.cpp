#include "modbusslave.h"
#include <QDataStream>
#include <QDateTime>
#include <QtEndian>
#include <QRandomGenerator>
#include <QJsonDocument>

ModbusSlave::ModbusSlave(QObject *parent) : QObject(parent), server(new QTcpServer(this)), holding(2001, 0), unit(1)
{
    connect(server, &QTcpServer::newConnection, this, &ModbusSlave::onNewConnection);
    fixedDelayMs = 0;
    dropProbability = 0.0;
}

ModbusSlave::~ModbusSlave()
{
    stop();
}

QVector<quint16> ModbusSlave::exportHolding() const
{
    QMutexLocker locker(&mutex);
    return holding;
}

void ModbusSlave::loadHolding(const QVector<quint16> &data)
{
    QMutexLocker locker(&mutex);
    int n = qMin(data.size(), holding.size());
    for (int i = 0; i < n; ++i) holding[i] = data[i];
}

void ModbusSlave::setFixedDelayMs(int ms)
{
    fixedDelayMs = qMax(0, ms);
}

void ModbusSlave::setDropProbability(double prob)
{
    if (prob < 0.0) prob = 0.0;
    if (prob > 1.0) prob = 1.0;
    dropProbability = prob;
}

void ModbusSlave::injectExceptionForFunction(quint8 func, quint8 exceptionCode)
{
    funcExceptions[func] = exceptionCode;
}

void ModbusSlave::injectExceptionForAddress(quint16 addr, quint8 exceptionCode)
{
    addrExceptions[addr] = exceptionCode;
}

void ModbusSlave::clearInjectedExceptions()
{
    funcExceptions.clear();
    addrExceptions.clear();
}

bool ModbusSlave::start(quint16 port, quint8 unitId, const QString &bindAddr)
{
    if (server->isListening()) server->close();
    unit = unitId;
    bool ok = false;
    if (!bindAddr.isEmpty()) {
        QHostAddress addr(bindAddr);
        ok = server->listen(addr, port);
    } else {
        ok = server->listen(QHostAddress::Any, port);
    }
    emit logMessage(QString("Modbus 从站启动: 端口 %1, 单元ID %2, 成功=%3").arg(port).arg(unitId).arg(ok));
    return ok;
}

void ModbusSlave::stop()
{
    for (QTcpSocket *c : clients) {
        c->disconnectFromHost();
        c->deleteLater();
    }
    clients.clear();
    if (server->isListening()) server->close();
    emit logMessage("Modbus 从站停止");
}

bool ModbusSlave::isRunning() const { return server->isListening(); }

bool ModbusSlave::setRegister(quint16 addr, quint16 value)
{
    {
        QMutexLocker locker(&mutex);
        if (addr >= (quint16)holding.size()) return false;
        holding[addr] = value;
    }
    emit registerOperation(addr, value, QString("write"));
    return true;
}

quint16 ModbusSlave::getRegister(quint16 addr) const
{
    QMutexLocker locker(&mutex);
    if (addr >= (quint16)holding.size()) return 0;
    return holding[addr];
}

bool ModbusSlave::setRegisterBit(quint16 addr, int bitIndex, bool value)
{
    if (bitIndex < 0 || bitIndex > 15) return false;
    quint16 v = 0;
    {
        QMutexLocker locker(&mutex);
        if (addr >= (quint16)holding.size()) return false;
        v = holding[addr];
        if (value) v |= (1 << bitIndex); else v &= ~(1 << bitIndex);
        holding[addr] = v;
    }
    emit registerOperation(addr, v, QString("write_bit"));
    return true;
}

bool ModbusSlave::setFloat(quint16 addr, float value)
{
    quint32 u;
    memcpy(&u, &value, sizeof(float));
    // IEEE754 32-bit: Modbus 字序 CDAB（先低字后高字）
    quint16 high = (u >> 16) & 0xFFFF;
    quint16 low = u & 0xFFFF;
    return setRegister(addr, low) && setRegister(addr + 1, high);
}

float ModbusSlave::getFloat(quint16 addr) const
{
    QMutexLocker locker(&mutex);
    if (addr+1 >= (quint16)holding.size()) return 0.0f;
    quint32 low = holding[addr];
    quint32 high = holding[addr+1];
    quint32 u = (high << 16) | low;
    float f;
    memcpy(&f, &u, sizeof(float));
    return f;
}

void ModbusSlave::onNewConnection()
{
    while (server->hasPendingConnections()) {
        QTcpSocket *sock = server->nextPendingConnection();
        clients.append(sock);
        connect(sock, &QTcpSocket::readyRead, this, &ModbusSlave::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &ModbusSlave::onClientDisconnected);
        emit logMessage(QString("客户端已连接: %1:%2").arg(sock->peerAddress().toString()).arg(sock->peerPort()));
        emit clientConnected();
    }
}

void ModbusSlave::onClientDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    clients.removeAll(sock);
    emit logMessage(QString("客户端断开: %1:%2").arg(sock->peerAddress().toString()).arg(sock->peerPort()));
    emit clientDisconnected();
    sock->deleteLater();
}

void ModbusSlave::onReadyRead()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    while (sock->bytesAvailable() >= 7) {
        QByteArray header = sock->peek(7);
        if (header.size() < 7) return;
        const unsigned char *data = reinterpret_cast<const unsigned char*>(header.constData());
        quint16 trans = (data[0] << 8) | data[1];
        quint16 proto = (data[2] << 8) | data[3];
        quint16 len = (data[4] << 8) | data[5];
        quint8 unitId = data[6];
        if (sock->bytesAvailable() < 6 + len) return; // wait full
        QByteArray packet = sock->read(6 + len);
        processRequest(sock, packet);
    }
}

void ModbusSlave::processRequest(QTcpSocket *sock, const QByteArray &data)
{
    if (data.size() < 9) return; // minimal MBAP + 2
    const unsigned char *d = reinterpret_cast<const unsigned char*>(data.constData());
    quint16 trans = (d[0] << 8) | d[1];
    // quint16 proto = (d[2]<<8) | d[3];
    // length = d[4]<<8 | d[5]
    quint8 unitId = d[6];
    quint8 func = d[7];

    // Only respond when unit matches or unit==0 (broadcast)?? We'll accept any
    QByteArray responsePDU;

    if (func == 3 || func == 4) { // Read Holding or Input Registers
        if (data.size() < 12) return;
        quint16 start = (d[8] << 8) | d[9];
        quint16 qty = (d[10] << 8) | d[11];
        if (qty == 0 || qty > 125) {
            // exception
            QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(3));
            QByteArray resp = buildResponse(trans, unitId, ex);
            // respect delay/drop
            auto deliver = [sock, resp, this]() {
                if (!sock) return;
                sock->write(resp);
            };
            if (shouldDrop()) {
                emit logMessage(QString("丢弃响应: Read Registers qty=%1 (injected)").arg(qty));
                return;
            }
            if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver);
            else deliver();
            emit logMessage(QString("异常请求: Read Registers qty=%1").arg(qty));
            return;
        }
        // check for injected exception (by function or address)
        if (funcExceptions.contains(func)) {
            quint8 exCode = funcExceptions.value(func);
            QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(exCode));
            QByteArray resp = buildResponse(trans, unitId, ex);
            if (shouldDrop()) { emit logMessage("丢弃注入的功能异常响应"); return; }
            auto deliver = [sock, resp]() { if (sock) sock->write(resp); };
            if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver); else deliver();
            emit logMessage(QString("注入功能异常: func=%1 code=%2").arg(func).arg(exCode));
            return;
        }

        QByteArray payload;
        payload.append(char(func));
        payload.append(char(qty * 2));
        QVector<QPair<quint16, quint16>> readOps;
        {
            QMutexLocker locker(&mutex);
            for (quint16 i = 0; i < qty; ++i) {
                quint16 addr = start + i;
                // per-address exception
                if (addrExceptions.contains(addr)) {
                    quint8 exCode = addrExceptions.value(addr);
                    QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(exCode));
                    QByteArray resp = buildResponse(trans, unitId, ex);
                    if (shouldDrop()) { emit logMessage("丢弃注入的地址异常响应"); return; }
                    auto deliverEx = [sock, resp]() { if (sock) sock->write(resp); };
                    if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliverEx); else deliverEx();
                    emit logMessage(QString("注入地址异常: addr=%1 code=%2").arg(addr).arg(exCode));
                    return;
                }
                quint16 val = 0;
                if (addr < (quint16)holding.size()) val = holding[addr];
                readOps.append(qMakePair(addr, val));
                payload.append(char((val >> 8) & 0xFF));
                payload.append(char(val & 0xFF));
            }
        }
        for (const auto &op : readOps) {
            emit registerOperation(op.first, op.second, QString("read"));
        }
        QByteArray resp = buildResponse(trans, unitId, payload);
        if (shouldDrop()) { emit logMessage("丢弃 Read Registers 响应 (随机丢包)"); return; }
        auto deliver = [sock, resp]() { if (sock) sock->write(resp); };
        if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver); else deliver();
        emit logMessage(QString("Read Registers: func=%1 start=%2 qty=%3 from %4:%5").arg(func).arg(start).arg(qty).arg(sock->peerAddress().toString()).arg(sock->peerPort()));
        return;
    }
    else if (func == 6) { // Write Single Register
        if (data.size() < 12) return;
        quint16 addr = (d[8] << 8) | d[9];
        quint16 val = (d[10] << 8) | d[11];
        setRegister(addr, val);
        // check exception injection for this address or function
        if (addrExceptions.contains(addr)) {
            quint8 exCode = addrExceptions.value(addr);
            QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(exCode));
            QByteArray resp = buildResponse(trans, unitId, ex);
            if (shouldDrop()) { emit logMessage("丢弃注入的地址异常响应(写单寄存器)"); return; }
            auto deliverEx = [sock, resp]() { if (sock) sock->write(resp); };
            if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliverEx); else deliverEx();
            emit logMessage(QString("注入地址异常(写单): addr=%1 code=%2").arg(addr).arg(exCode));
            return;
        }

        QByteArray payload;
        payload.append(char(func));
        payload.append(char((addr >> 8) & 0xFF)); payload.append(char(addr & 0xFF));
        payload.append(char((val >> 8) & 0xFF)); payload.append(char(val & 0xFF));
        QByteArray resp = buildResponse(trans, unitId, payload);
        if (shouldDrop()) { emit logMessage("丢弃 Write Single Reg 响应 (随机丢包)"); return; }
        auto deliver = [sock, resp]() { if (sock) sock->write(resp); };
        if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver); else deliver();
        emit logMessage(QString("Write Single Reg: addr=%1 val=%2 from %3:%4").arg(addr).arg(val).arg(sock->peerAddress().toString()).arg(sock->peerPort()));
        return;
    }
    else if (func == 16) { // Write Multiple Registers
        if (data.size() < 13) return;
        quint16 start = (d[8] << 8) | d[9];
        quint16 qty = (d[10] << 8) | d[11];
        quint8 byteCount = d[12];
        if (data.size() < 13 + byteCount) return;
        QMutexLocker locker(&mutex);
        for (int i = 0; i < qty; ++i) {
            int idx = 13 + i*2;
            if (idx + 1 >= data.size()) break;
            quint16 v = ( (unsigned char)d[idx] << 8) | (unsigned char)d[idx+1];
            quint16 addr = start + i;
            if (addr < (quint16)holding.size()) holding[addr] = v;
        }
        QByteArray payload;
        payload.append(char(func));
        payload.append(char((start >> 8) & 0xFF)); payload.append(char(start & 0xFF));
        payload.append(char((qty >> 8) & 0xFF)); payload.append(char(qty & 0xFF));
        QByteArray resp = buildResponse(trans, unitId, payload);
        if (shouldDrop()) { emit logMessage("丢弃 Write Multiple Regs 响应 (随机丢包)"); return; }
        auto deliver = [sock, resp]() { if (sock) sock->write(resp); };
        if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver); else deliver();
        emit logMessage(QString("Write Multiple Regs: start=%1 qty=%2 from %3:%4").arg(start).arg(qty).arg(sock->peerAddress().toString()).arg(sock->peerPort()));
        return;
    }
    else {
        // Function not supported - send exception 1
        // support injection for unsupported functions too
        if (funcExceptions.contains(func)) {
            quint8 exCode = funcExceptions.value(func);
            QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(exCode));
            QByteArray resp = buildResponse(trans, unitId, ex);
            if (shouldDrop()) { emit logMessage("丢弃注入的功能异常响应(不支持功能)"); return; }
            auto deliverEx = [sock, resp]() { if (sock) sock->write(resp); };
            if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliverEx); else deliverEx();
            emit logMessage(QString("注入功能异常(不支持): func=%1 code=%2").arg(func).arg(exCode));
            return;
        }

        QByteArray ex; ex.append(char(func | 0x80)); ex.append(char(1));
        QByteArray resp = buildResponse(trans, unitId, ex);
        if (shouldDrop()) { emit logMessage("丢弃 不支持功能 的异常响应 (随机丢包)"); return; }
        auto deliver = [sock, resp]() { if (sock) sock->write(resp); };
        if (fixedDelayMs > 0) QTimer::singleShot(fixedDelayMs, this, deliver); else deliver();
        emit logMessage(QString("不支持的功能码: %1").arg(func));
        return;
    }
}

QByteArray ModbusSlave::buildResponse(quint16 transactionId, quint8 unitId, const QByteArray &pdu)
{
    QByteArray mbap;
    mbap.append(char((transactionId >> 8) & 0xFF));
    mbap.append(char(transactionId & 0xFF));
    mbap.append(char(0)); mbap.append(char(0)); // Protocol
    quint16 len = pdu.size() + 1; // unit
    mbap.append(char((len >> 8) & 0xFF)); mbap.append(char(len & 0xFF));
    mbap.append(char(unitId));
    QByteArray resp = mbap + pdu;
    return resp;
}

bool ModbusSlave::shouldDrop() const
{
    if (dropProbability <= 0.0) return false;
    // generate random double in [0,1)
    double v = QRandomGenerator::global()->bounded(1000000) / 1000000.0;
    return v < dropProbability;
}
