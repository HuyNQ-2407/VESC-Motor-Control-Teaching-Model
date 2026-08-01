#include "protocol.h"
#include <QDebug>

Protocol::Protocol(QObject *parent) : QObject(parent), m_connected(false) {
    m_socket = new QTcpSocket(this);
    m_requestTimer = new QTimer(this);

    connect(m_socket, &QTcpSocket::connected, this, &Protocol::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Protocol::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &Protocol::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &Protocol::onError);
}

Protocol::~Protocol() {
    disconnect();
}

bool Protocol::connectESP32(const QString &host, quint16 port) {
    qDebug() << "Connecting to" << host << ":" << port;
    m_socket->connectToHost(host, port);
    return true;
}

void Protocol::disconnect() {
    m_requestTimer->stop();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
    m_connected = false;
}

void Protocol::onConnected() {
    qDebug() << "Connected to ESP32";
    m_connected = true;
    m_requestTimer->start(50); // 20 Hz
    emit connected();
}

void Protocol::onDisconnected() {
    qDebug() << "Disconnected from ESP32";
    m_connected = false;
    m_requestTimer->stop();
    emit disconnected();
}

void Protocol::onReadyRead() {
    m_buffer.append(m_socket->readAll());

    while (m_buffer.contains('\n')) {
        int idx = m_buffer.indexOf('\n');
        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        parseResponse(line);
    }
}

void Protocol::parseResponse(const QByteArray &data) {
    QString str = QString::fromUtf8(data).trimmed();
    if (str.isEmpty()) return;

    // Terminal-style text output from the firmware (as opposed to CSV telemetry)
    if (str.startsWith("Battery") || str.startsWith("ADC") ||
        str.startsWith("Set") || str.startsWith("P:") ||
        str.startsWith("Status:") || str.startsWith("Motor") ||
        str.startsWith("Remote") || str.startsWith("Runtime") ||
        str.startsWith("Kp") || str.startsWith("Ki") ||
        str.startsWith("Kd") || str.startsWith("Error:") ||
        str.startsWith(">") || str.startsWith("Hardware") ||
        str.startsWith("Usage:") || str.startsWith("FAULT:") ||
        str.startsWith("Integrator")) {
        emit terminalOutput(str);
        return;
    }

    // Otherwise expect a 7-field CSV telemetry frame
    QStringList parts = str.split(',');
    if (parts.size() >= 7) {
        VESCData vescData;
        vescData.voltage = parts[0].toFloat();
        vescData.current = parts[1].toFloat();
        vescData.duty = parts[2].toFloat();
        vescData.rpm = parts[4].toFloat();
        vescData.tempMOSFET = parts[5].toFloat();
        vescData.tempMotor = parts[6].toFloat();
        vescData.targetRPM = parts[3].toFloat();
        emit dataReceived(vescData);
    } else {
        // Doesn't look like telemetry either - treat as terminal text
        emit terminalOutput(str);
    }
}

void Protocol::onError(QAbstractSocket::SocketError error) {
    emit errorOccurred(m_socket->errorString());
}

void Protocol::sendTerminalCommand(const QString &cmd) {
    if (!m_connected) return;
    QString fullCmd = cmd + "\n";
    m_socket->write(fullCmd.toUtf8());
    m_socket->flush();
}

void Protocol::setPID(float kp, float ki, float kd) {
    sendTerminalCommand(QString("kp %1").arg(kp, 0, 'f', 7));
    sendTerminalCommand(QString("ki %1").arg(ki, 0, 'f', 7));
    sendTerminalCommand(QString("kd %1").arg(kd, 0, 'f', 7));
}

void Protocol::requestRealTimeData() {
    // Data is already streamed continuously by the ESP32's UART thread
}

void Protocol::requestDataTimeout() {
    // Reserved for a future keep-alive / status request
}