#ifndef VESCPROTOCOL_H
#define VESCPROTOCOL_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

struct VESCData {
    float voltage = 0.0f;
    float current = 0.0f;
    float rpm = 0.0f;
    float duty = 0.0f;
    float tempMOSFET = 0.0f;
    float tempMotor = 0.0f;
    float targetRPM;
};

struct PIDParams {
    float kp = 0.0005f;
    float ki = 0.00001f;
    float kd = 0.000001f;
};

class Protocol : public QObject {
    Q_OBJECT

public:
    explicit Protocol(QObject *parent = nullptr);
    ~Protocol();
    bool connectESP32(const QString &host, quint16 port = 8888);
    void disconnect();
    bool isConnected() const { return m_connected; }

    void sendTerminalCommand(const QString &cmd);
    void setPID(float kp, float ki, float kd);
    void requestRealTimeData();

signals:
    void connected();
    void disconnected();
    void dataReceived(const VESCData &data);
    void terminalOutput(const QString &text);
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void requestDataTimeout();

private:
    void parseResponse(const QByteArray &data);

    QTcpSocket *m_socket;    // TCP connection to the ESP32 bridge
    QTimer *m_requestTimer;  // periodic data-request timer
    bool m_connected;
    QByteArray m_buffer;     // unprocessed bytes carried over between reads
};

#endif