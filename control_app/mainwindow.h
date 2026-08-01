#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QCheckBox>
#include "protocol.h"
#include <QtCharts>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

class CustomChartView : public QChartView {
    Q_OBJECT
public:
    explicit CustomChartView(QChart *chart, QWidget *parent = nullptr)
        : QChartView(chart, parent) {
        setMouseTracking(true);
    }

signals:
    void mouseMovedOnChart(QPointF scenePos);

protected:
    void mouseMoveEvent(QMouseEvent *event) override {
        emit mouseMovedOnChart(event->pos());
        QChartView::mouseMoveEvent(event);
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onDataReceived(const VESCData &data);
    void onTerminalOutput(const QString &text);
    void onSendTerminalClicked();
    void onSetPIDClicked();
    void onConnectionChanged(bool connected);
    void onRemoteControlChosen(bool enabled);
    void onRPMSliderChanged(int value);
    void onRPMSpinChanged(int value);
    void onStopMotor();
    void updateRpmGraph(double actualRPM, double targetRPM);

private:
    void setupUI();
    void setupMotorTab();
    void setupGraphTab();
    void setupDataTab();
    void updateChart(float rpm);
    void updateMotorStatus(const VESCData &data);

    Protocol *m_Protocol;
    QTabWidget *m_tabWidget;

    // Connect bar
    QPushButton *m_buttonConnect;
    QLineEdit *m_editHost;

    // Motor tab
    QLabel *m_labelMotorType;
    QLabel *m_labelMotorPoles;
    QLabel *m_labelMaxRPM;
    QLabel *m_labelMaxCurrent;
    QLabel *m_labelTempMOSFET;
    QLabel *m_labelTempMotor;

    // Control tab
    QCheckBox *m_checkRemoteControl_data;
    QSlider *m_sliderRPM_data;
    QSpinBox *m_spinRPM_data;
    QPushButton *m_buttonStop_data;
    QLabel *m_labelControlMode_data;

    // Data tab - status
    QLabel *m_labelVoltage;
    QLabel *m_labelCurrent;
    QLabel *m_labelRPM;
    QLabel *m_labelDuty;
    QTextEdit *m_terminal;
    QLineEdit *m_editTerminal;
    QCheckBox *m_checkRemoteControl_graph;
    QSlider *m_sliderRPM_graph;
    QSpinBox *m_spinRPM_graph;
    QPushButton *m_buttonStop_graph;
    QLabel *m_labelControlMode_graph;

    // PID
    QLineEdit *m_editKp;
    QLineEdit *m_editKi;
    QLineEdit *m_editKd;

    // Internal state
    bool m_remoteControlEnabled;
    int m_maxRPM;

    // Runtime limits
    QSpinBox *m_spinMaxRPM;
    QDoubleSpinBox *m_spinMaxCurrent;

    // Chart
    CustomChartView *m_chartView;
    QChart *m_chart;
    QValueAxis *m_axisX;
    QValueAxis *m_axisY;
    QLineSeries *m_seriesActual = nullptr;
    QLineSeries *m_seriesTarget = nullptr;
    QLabel *m_labelLiveActual = nullptr;
    QLabel *m_labelLiveTarget = nullptr;
    QLabel *m_labelLiveError = nullptr;
    double m_time;
    int m_maxPlotPoints;

    // ERPM <-> mechanical RPM conversion
    int m_motorPoles = 2;
    int mechFromElec(int erpm) const { return (m_motorPoles > 0) ? int(erpm / (m_motorPoles / 2.0)) : erpm; }
    int elecFromMech(int rpm)  const { return (m_motorPoles > 0) ? int(rpm * (m_motorPoles / 2.0)) : rpm; }

    bool m_parsingLimits = false;

    // Graph behavior
    bool m_followTime = true;
    bool m_autoScaleY = true;
    double m_fixedWindowSec = 5.0;
    double m_fixedYmin = -2500;
    double m_fixedYmax = 2500;

    QTimer *m_chartUpdateTimer;
    double m_latestActualRPM = 0.0;
    double m_latestTargetRPM = 0.0;
};

#endif