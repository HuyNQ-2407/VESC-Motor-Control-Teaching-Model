#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFormLayout>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),
    m_remoteControlEnabled(false),
    m_maxRPM(1500),
    m_time(0.0),
    m_maxPlotPoints(1500) // max points shown on the graph

{
    m_Protocol = new Protocol(this);
    connect(m_Protocol, &Protocol::dataReceived, this, &MainWindow::onDataReceived);
    connect(m_Protocol, &Protocol::terminalOutput, this, &MainWindow::onTerminalOutput);
    connect(m_Protocol, &Protocol::connected, this, [this]() { onConnectionChanged(true); });
    connect(m_Protocol, &Protocol::disconnected, this, [this]() { onConnectionChanged(false); });

    setupUI();
    m_chartUpdateTimer = new QTimer(this);
    connect(m_chartUpdateTimer, &QTimer::timeout, this, [this]() {
        updateRpmGraph(m_latestActualRPM, m_latestTargetRPM);
    });
    m_chartUpdateTimer->start(33);
    setWindowTitle("AML-VESC");
    resize(2560, 1440);
}

MainWindow::~MainWindow() {}

// SETUP UI
void MainWindow::setupUI()
{
    QWidget *central = new QWidget(this);
    // Main layout
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    //Header layout
    QHBoxLayout *headerLayout = new QHBoxLayout();
    //Title
    QLabel *title = new QLabel("AML - VESC");
    title->setAlignment(Qt::AlignLeft);
    title->setStyleSheet( "font-family: impact; font-size: 65px; font-style: italic; color: #AEAFD4");
    headerLayout->addWidget(title, 1);
    //Connection
    QLabel *connectionTitle = new QLabel("BOARD:");
    connectionTitle->setStyleSheet("font-size: 25px; font-weight: bold; color: #3F918A");
    //Text box
    m_editHost = new QLineEdit("");
    m_editHost->setPlaceholderText("IP Address");
    m_editHost->setStyleSheet("background-color:white ; font-size: 15; font-style: italic; border: 1px solid #000000; padding: 2px");
    m_editHost->setFixedWidth(350);
    m_editHost->setFixedHeight(35);
    //Button
    m_buttonConnect = new QPushButton("CONNECT");
    m_buttonConnect->setFixedWidth(150);
    m_buttonConnect->setFixedHeight(35);
    m_buttonConnect->setStyleSheet("font-weight: bold; padding: 5px; font-size: 20; background-color: grey");
    connect(m_buttonConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    //Add to header
    headerLayout->addWidget(connectionTitle);
    headerLayout->addWidget(m_editHost);
    headerLayout->addWidget(m_buttonConnect);
    mainLayout->addLayout(headerLayout);
    // Tabs layout
    m_tabWidget = new QTabWidget();
    setupMotorTab();
    setupDataTab();
    setupGraphTab();
    // Connect checkbox: DATA -> GRAPH
    connect(m_checkRemoteControl_data, &QCheckBox::toggled, this, [this](bool checked) {
        m_checkRemoteControl_graph->blockSignals(true);
        m_checkRemoteControl_graph->setChecked(checked);
        m_checkRemoteControl_graph->blockSignals(false);
    });
    // Connect checkbox: GRAPH -> DATA
    connect(m_checkRemoteControl_graph, &QCheckBox::toggled, this, [this](bool checked) {
        m_checkRemoteControl_data->blockSignals(true);
        m_checkRemoteControl_data->setChecked(checked);
        m_checkRemoteControl_data->blockSignals(false);
    });
    // Connect slider: DATA -> GRAPH
    connect(m_sliderRPM_data, &QSlider::valueChanged, this, [this](int value) {
        m_sliderRPM_graph->blockSignals(true);
        m_sliderRPM_graph->setValue(value);
        m_sliderRPM_graph->blockSignals(false);
    });
    // Connect slider: GRAPH -> DATA
    connect(m_sliderRPM_graph, &QSlider::valueChanged, this, [this](int value) {
        m_sliderRPM_data->blockSignals(true);
        m_sliderRPM_data->setValue(value);
        m_sliderRPM_data->blockSignals(false);
    });
    // Connect spinBox: DATA -> GRAPH
    connect(m_spinRPM_data, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_spinRPM_graph->blockSignals(true);
        m_spinRPM_graph->setValue(value);
        m_spinRPM_graph->blockSignals(false);
    });
    // Connect spinBox: GRAPH -> DATA
    connect(m_spinRPM_graph, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_spinRPM_data->blockSignals(true);
        m_spinRPM_data->setValue(value);
        m_spinRPM_data->blockSignals(false);
    });
    // Stop button GRAPH
    connect(m_buttonStop_graph, &QPushButton::clicked, this, &MainWindow::onStopMotor);
    // Tab style
    m_tabWidget->setStyleSheet(
        "QTabBar::tab { "
        "   background: #1B1B2F;"
        "   color: #BDC3C7;"
        "   padding: 6px 12px;"
        "   font-weight: bold;"
        "   border: 1px solid #2C3E50;"
        "   border-bottom: none;"
        "   font-size: 25px ;"
        "}"
        "QTabBar::tab:selected { "
        "   background: #2980B9;"
        "   color: #FFFFFF;"
        "}"
        "QTabBar::tab:hover { "
        "   background: #34495E;"
        "   color: #ECF0F1;"
        "}"
        "QTabWidget::pane { "
        "   border: 1px solid #2C3E50;"
        "   background: #0F172A;"
        "}"
        );
    // Add widget to layout
    mainLayout->addWidget(m_tabWidget);
    setCentralWidget(central);
}

// CONNECT BUTTON - CLICKED
void MainWindow::onConnectClicked()
{
    if (m_Protocol->isConnected()) {
        m_Protocol->disconnect();
    } else {
        QString host = m_editHost->text();
        if (m_Protocol->connectESP32(host)) {
            m_terminal->append("Connected to " + host + "!");
        } else {
            m_terminal->append("Connection FAILED!");
        }
    }
}

// CONNECT STATUS - CHANGED
void MainWindow::onConnectionChanged(bool connected)
{
    if (connected) {
        m_buttonConnect->setText("CONNECTED");
        m_buttonConnect->setStyleSheet("font-weight: bold; padding: 5px; font-size: 20; color: white; background-color: green");
        m_editHost->setEnabled(false);
        m_terminal->append("Connected to VESC");
    } else {
        m_buttonConnect->setText("DISCONNECTED");
        m_buttonConnect->setStyleSheet("font-weight: bold; padding: 5px; font-size: 20; color: white; background-color: red");
        m_editHost->setEnabled(true);
        m_checkRemoteControl_data->setChecked(false);
        m_terminal->append("Disconnected from VESC");
    }
}

// Motor Tab
void MainWindow::setupMotorTab()
{
    QWidget *motorWidget = new QWidget();
    motorWidget->setStyleSheet("background-color: #0f172a;");
    // Main layout
    QVBoxLayout *layout = new QVBoxLayout(motorWidget);
    layout->setSpacing(20);
    layout->setContentsMargins(15, 15, 15, 15);
    // Motor info group
    QGroupBox *infoGroup = new QGroupBox("MOTOR CONFIGURATION");
    infoGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-size: 22px; "
        "   font-weight: bold; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "   padding: 0 12px; "
        "}"
        );
    //Motor info layout
    QGridLayout *infoLayout = new QGridLayout();
    infoLayout->setSpacing(20);
    infoLayout->setContentsMargins(10, 30, 10, 10);
    //Lambda function to create label
    auto createLabel = [](const QString &text) {
        QLabel *lbl = new QLabel(text);
        lbl->setStyleSheet("color: #94a3b8; font-size: 22px; font-weight: bold; border-radius: 6px; padding: 8px");
        lbl->setMaximumWidth(150);
        lbl->setAlignment(Qt::AlignCenter);
        return lbl;
    };
    //Lambda function to create value label
    auto createValueLabel = [](QLabel **label) {
        *label = new QLabel("----");
        (*label)->setStyleSheet("color: #e2e8f0; font-size: 20px; background-color: #334155; "
                                "padding: 8px 15px; border-radius: 6px;");
        (*label)->setMinimumWidth(150);
        (*label)->setAlignment(Qt::AlignCenter);

    };
    //Create label value
    createValueLabel(&m_labelMotorType);
    createValueLabel(&m_labelMotorPoles);
    createValueLabel(&m_labelMaxRPM);
    createValueLabel(&m_labelMaxCurrent);
    //Create stop button
    QPushButton *buttonReadConfig = new QPushButton("Read Motor Configuration");
    buttonReadConfig->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; font-weight: bold; "
        "font-size: 22px; padding: 10px; border-radius: 10px; border: none; }"
        "QPushButton:hover { background-color: #2563eb; }"
        );
    buttonReadConfig->setMinimumHeight(45);
    connect(buttonReadConfig, &QPushButton::clicked, this, [this]() {
        m_Protocol->sendTerminalCommand("motorinfo");
    });
    //Add to info layout
    infoLayout->addWidget(createLabel("Motor Type:"), 0, 0);
    infoLayout->addWidget(m_labelMotorType, 0, 1);
    infoLayout->addWidget(createLabel("Motor Poles:"), 0, 2);
    infoLayout->addWidget(m_labelMotorPoles, 0, 3);
    infoLayout->addWidget(createLabel("Max RPM:"), 1, 0);
    infoLayout->addWidget(m_labelMaxRPM, 1, 1);
    infoLayout->addWidget(createLabel("Max Current:"), 1, 2);
    infoLayout->addWidget(m_labelMaxCurrent, 1, 3);
    infoLayout->addWidget(buttonReadConfig, 2, 0, 1, 4);
    infoGroup->setLayout(infoLayout);
    // Seperate layout
    QHBoxLayout *sideBySideLayout = new QHBoxLayout();
    sideBySideLayout->setSpacing(20);
    // Safety limit group
    QGroupBox *limitsGroup = new QGroupBox("SAFETY LIMITS");
    limitsGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-size: 22px; "
        "   font-weight: bold; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 20px; "
        "   padding: 0 10px; "
        "}"
        );
    // Limit layout
    QVBoxLayout *limitsLayout = new QVBoxLayout();
    limitsLayout->setSpacing(20);
    limitsLayout->setContentsMargins(10, 30, 10, 10);
    // Max RPM layout
    QHBoxLayout *rpmLayout = new QHBoxLayout();
    // Text
    QLabel *rpmLabel = new QLabel("Max RPM:");
    rpmLabel->setStyleSheet("color: #94a3b8; font-size: 22px; font-weight: bold; border-radius: 6px; padding: 8px");
    rpmLabel->setMinimumWidth(110);
    rpmLabel->setMaximumWidth(150);
    rpmLabel->setAlignment(Qt::AlignCenter);
    rpmLayout->addWidget(rpmLabel);
    // Value
    m_spinMaxRPM = new QSpinBox();
    m_spinMaxRPM->setRange(0, 10000);
    m_spinMaxRPM->setValue(0);
    m_spinMaxRPM->setSuffix(" RPM");
    m_spinMaxRPM->setSpecialValueText("HW Default");
    m_spinMaxRPM->setStyleSheet(
        "QSpinBox { background-color: #334155; color: #e2e8f0; border: 1px solid #475569; "
        "border-radius: 6px; padding: 8px; font-size: 20px; }"
        );
    m_spinMaxRPM->setMinimumWidth(250);
    m_spinMaxRPM->setMaximumWidth(1000);
    m_spinMaxRPM->setAlignment(Qt::AlignCenter);
    rpmLayout->addWidget(m_spinMaxRPM);
    // Button
    QPushButton *buttonSetMaxRPM = new QPushButton("APPLY");
    buttonSetMaxRPM->setStyleSheet(
        "QPushButton { background-color: #f59e0b; color: white; font-weight: bold; "
        "padding: 8px 20px; border-radius: 6px; border: none; }"
        "QPushButton:hover { background-color: #d97706; }"
        );
    buttonSetMaxRPM->setFixedWidth(400);
    connect(buttonSetMaxRPM, &QPushButton::clicked, this, [this]() {
        int value = m_spinMaxRPM->value();
        if (value > 0) {
            m_Protocol->sendTerminalCommand(QString("setlimit maxrpm %1").arg(value));
            m_terminal->append(QString("> Runtime Max RPM: %1").arg(value));
        } else {
            m_terminal->append("> Using hardware default RPM");
        }
    });
    //Add to max rpm
    rpmLayout->addWidget(buttonSetMaxRPM);
    // Max current layout
    QHBoxLayout *currentLayout = new QHBoxLayout();
    // Text
    QLabel *currentLabel = new QLabel("Max Current:");
    currentLabel->setStyleSheet("color: #94a3b8; font-size: 22px; font-weight: bold; border-radius: 6px; padding: 8px");
    currentLabel->setMinimumWidth(110);
    currentLabel->setMaximumWidth(150);
    currentLabel->setAlignment(Qt::AlignCenter);
    currentLayout->addWidget(currentLabel);
    // Value
    m_spinMaxCurrent = new QDoubleSpinBox();
    m_spinMaxCurrent->setRange(0, 200);
    m_spinMaxCurrent->setValue(0);
    m_spinMaxCurrent->setDecimals(1);
    m_spinMaxCurrent->setSuffix(" A");
    m_spinMaxCurrent->setSpecialValueText("HW Default");
    m_spinMaxCurrent->setSingleStep(0.5);
    m_spinMaxCurrent->setStyleSheet(
        "QDoubleSpinBox { background-color: #334155; color: #e2e8f0; border: 1px solid #475569; "
        "border-radius: 6px; padding: 8px; font-size: 20px; }"
        );
    m_spinMaxCurrent->setMinimumWidth(250);
    m_spinMaxCurrent->setMaximumWidth(1000);
    m_spinMaxCurrent->setAlignment(Qt::AlignCenter);
    currentLayout->addWidget(m_spinMaxCurrent);
    // Button
    QPushButton *buttonSetMaxCurrent = new QPushButton("APPLY");
    buttonSetMaxCurrent->setStyleSheet(
        "QPushButton { background-color: #f59e0b; color: white; font-weight: bold; "
        "padding: 8px 20px; border-radius: 6px; border: none; }"
        "QPushButton:hover { background-color: #d97706; }"
        );
    buttonSetMaxCurrent->setFixedWidth(400);
    connect(buttonSetMaxCurrent, &QPushButton::clicked, this, [this]() {
        double value = m_spinMaxCurrent->value();
        if (value > 0) {
            m_Protocol->sendTerminalCommand(QString("setlimit maxcurrent %1").arg(value, 0, 'f', 1));
            m_terminal->append(QString("> Runtime Max Current: %1 A").arg(value, 0, 'f', 1));
        } else {
            m_terminal->append("> Using hardware default current");
        }
    });
    // Add to max current
    currentLayout->addWidget(buttonSetMaxCurrent);
    // Show Limits Button
    QPushButton *buttonShowLimits = new QPushButton("SHOW ACTIVE LIMITS");
    buttonShowLimits->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; font-weight: bold; "
        "font-size: 18px; padding: 10px; border-radius: 8px; border: none; }"
        "QPushButton:hover { background-color: #2563eb; }"
        );
    buttonShowLimits->setMinimumHeight(45);
    connect(buttonShowLimits, &QPushButton::clicked, this, [this]() {
        m_Protocol->sendTerminalCommand("setlimit");
        m_terminal->append("> Reloaded limits from VESC");
    });
    // Add to limits layout and group
    limitsLayout->addLayout(currentLayout);
    limitsLayout->addLayout(rpmLayout);
    limitsLayout->addWidget(buttonShowLimits);
    limitsGroup->setLayout(limitsLayout);
    // Temperature Group
    QGroupBox *tempGroup = new QGroupBox("TEMPERATURE MONITORING");
    tempGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-size: 22px; "
        "   font-weight: bold; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left ; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 20px; "
        "   padding: 0 10px; "
        "}"
        );
    tempGroup->setMinimumSize(350, 180);
    tempGroup->setMaximumWidth(400);
    // Temperature Layout
    QHBoxLayout *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(20);
    tempLayout->setContentsMargins(10, 30, 10, 10);
    //Lambda to create card
    auto createTempCard = [](const QString &title, QLabel **valueLabel) {
        QVBoxLayout *cardLayout = new QVBoxLayout();
        cardLayout->setSpacing(10);
        cardLayout->setAlignment(Qt::AlignCenter);
        QLabel *titleLbl = new QLabel(title);
        titleLbl->setStyleSheet("color: #94a3b8; font-size: 22px; font-weight: bold;");
        titleLbl->setAlignment(Qt::AlignCenter);
        *valueLabel = new QLabel("-- °C");
        (*valueLabel)->setStyleSheet("color: #60a5fa; font-size: 20px; font-weight: bold;");
        (*valueLabel)->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(titleLbl);
        cardLayout->addWidget(*valueLabel);
        QWidget *card = new QWidget();
        card->setStyleSheet("background-color: #334155; border-radius: 10px; padding: 20px;");
        card->setLayout(cardLayout);
        card->setMinimumSize(150, 120);
        return card;
    };
    // Add cards to temp layout and group
    tempLayout->addWidget(createTempCard("MOSFET", &m_labelTempMOSFET));
    tempLayout->addWidget(createTempCard("MOTOR", &m_labelTempMotor));
    tempGroup->setLayout(tempLayout);
    // Add to seperate group
    sideBySideLayout->addWidget(limitsGroup);
    sideBySideLayout->addWidget(tempGroup);
    // Warning group
    QLabel *warningInfo = new QLabel(
        "<span style='color: red; font-weight: bold; font-size: 23px;'>NOTES</span><br>"
        "• Runtime limits are <b>temporary</b> and reset on VESC reboot<br>"
        "• Set to 0 to use hardware configuration values<br>"
        "• Use <b>VESC Tool</b> to change motor configuration permanently<br>"
        "• This app is for runtime control and monitoring only"
        );
    warningInfo->setStyleSheet(
        "QLabel { "
        "color: #374151; "
        "background-color: #CCFFFF; "
        "border: 1.5px solid #f59e0b; "
        "border-radius: 10px; "
        "padding: 3px; "
        "font-size: 20px;"
        "}"
        );
    warningInfo->setWordWrap(true);
    layout->addWidget(infoGroup);
    layout->addLayout(sideBySideLayout);
    layout->addWidget(warningInfo);
    layout->addStretch();
    m_tabWidget->addTab(motorWidget,"MOTOR STATUS");
}

//Data Tab
void MainWindow::setupDataTab() {
    QWidget *dataWidget = new QWidget();
    dataWidget->setStyleSheet("background-color: #0f172a;");
    // Main layout
    QVBoxLayout *layout = new QVBoxLayout(dataWidget);
    layout->setSpacing(20);
    layout->setContentsMargins(15, 15, 15, 15);
    //Control group
    QGroupBox *controlGroup = new QGroupBox("MOTOR CONTROL");
    controlGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "   padding: 0 12px; "
        "}"
        );
    //Control layout
    QVBoxLayout *controlLayout = new QVBoxLayout();
    // Remote layout
    QHBoxLayout *remoteLayout = new QHBoxLayout();
    // Enable Remote Control
    m_checkRemoteControl_data = new QCheckBox("Enable Remote Control");
    m_checkRemoteControl_data->setStyleSheet(
        "QCheckBox { color: #e2e8f0; font-size: 20px; font-weight: bold; background: transparent; }"
        "QCheckBox::indicator { width: 22px; height: 22px; }"
        );
    connect(m_checkRemoteControl_data, &QCheckBox::toggled, this, &MainWindow::onRemoteControlChosen);
    // Mode
    m_labelControlMode_data = new QLabel("<span style='font-weight:bold; color:#e2e8f0;'>Running Mode: </span>"
                                    "<span style='color:red;font-weight:bold'>ADC SIGNAL</span>");
    m_labelControlMode_data->setStyleSheet("background: transparent; font-size: 20px; font-weight: bold;");
    // Add remote layout
    remoteLayout->addWidget(m_checkRemoteControl_data);
    remoteLayout->addStretch();
    remoteLayout->addWidget(m_labelControlMode_data);
    // RPM Slider
    QLabel *labelSlider = new QLabel("Target RPM:");
    labelSlider->setStyleSheet("color: #66FFFF; font-size: 22px; font-weight: bold; background: transparent;");
    // Slider and spinbox value
    QHBoxLayout *sliderLayout = new QHBoxLayout();
    m_sliderRPM_data = new QSlider(Qt::Horizontal);
    m_sliderRPM_data->setRange(-m_maxRPM, m_maxRPM);
    m_sliderRPM_data->setValue(0);
    m_sliderRPM_data->setEnabled(false);
    m_sliderRPM_data->setStyleSheet(
        "QSlider::groove:horizontal { height: 8px; background: #334155; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #3b82f6; width: 20px; margin: -6px 0; border-radius: 10px; }"
        );
    connect(m_sliderRPM_data, &QSlider::valueChanged, this, &MainWindow::onRPMSliderChanged);
    sliderLayout->addWidget(m_sliderRPM_data);
    m_spinRPM_data = new QSpinBox();
    m_spinRPM_data->setRange(-m_maxRPM,m_maxRPM);
    m_spinRPM_data->setValue(0);
    m_spinRPM_data->setSuffix(" RPM");
    m_spinRPM_data->setEnabled(false);
    m_spinRPM_data->setMinimumWidth(150);
    m_spinRPM_data->setStyleSheet("background-color: #334155; color: #e2e8f0; font-weight: bold; font-size: 16px; border-radius: 6px; padding: 5px;");
    connect(m_spinRPM_data, &QSpinBox::valueChanged, this, &MainWindow::onRPMSpinChanged);
    sliderLayout->addWidget(m_spinRPM_data);
    // STOP button
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_buttonStop_data = new QPushButton("STOP");
    m_buttonStop_data->setStyleSheet(
        "QPushButton { background-color: #ef4444; color: white; font-weight: bold; "
        "font-size: 18px; padding: 6px 12px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #dc2626; }"
        );
    m_buttonStop_data->setEnabled(false);
    m_buttonStop_data->setFixedHeight(40);
    connect(m_buttonStop_data, &QPushButton::clicked, this, &MainWindow::onStopMotor);
    buttonLayout->addWidget(m_buttonStop_data);
    // Add control layout and group
    controlLayout->addLayout(remoteLayout);
    controlLayout->addWidget(labelSlider);
    controlLayout->addLayout(sliderLayout);
    controlLayout->addLayout(buttonLayout);
    controlGroup->setLayout(controlLayout);
    // Status cards
    QHBoxLayout *statusLayout = new QHBoxLayout();
    auto createStatusCard = [](const QString &title, QLabel **valueLabel) {
        QVBoxLayout *cardLayout = new QVBoxLayout();
        cardLayout->setSpacing(10);
        cardLayout->setAlignment(Qt::AlignCenter);

        QLabel *titleLbl = new QLabel(title);
        titleLbl->setStyleSheet("color: #94a3b8; font-size: 22px; font-weight: bold;");
        titleLbl->setAlignment(Qt::AlignCenter);

        *valueLabel = new QLabel("0.0");
        (*valueLabel)->setStyleSheet("color: #60a5fa; font-size: 20px; font-weight: bold;");
        (*valueLabel)->setAlignment(Qt::AlignCenter);

        cardLayout->addWidget(titleLbl);
        cardLayout->addWidget(*valueLabel);

        QWidget *card = new QWidget();
        card->setStyleSheet("background-color: #334155; border-radius: 6px; padding: 6px;");
        card->setLayout(cardLayout);
        card->setMinimumSize(130, 100);
        return card;
    };

    statusLayout->addWidget(createStatusCard("Voltage (V)", &m_labelVoltage));
    statusLayout->addWidget(createStatusCard("Current (A)", &m_labelCurrent));
    statusLayout->addWidget(createStatusCard("RPM", &m_labelRPM));
    statusLayout->addWidget(createStatusCard("Duty (%)", &m_labelDuty));

    // PID
    QGroupBox *pidGroup = new QGroupBox("PID PARAMETERS");
    pidGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "   padding: 0 12px; "
        "}"
        );
    QHBoxLayout *pidLayout = new QHBoxLayout();
    auto createPIDEdit = [](const QString &label, QLineEdit **edit, const QString &defVal) {
        QLabel *lbl = new QLabel(label);
        lbl->setStyleSheet("color: #94a3b8; font-weight: bold; font-size: 22px");
        *edit = new QLineEdit(defVal);
        (*edit)->setMaximumWidth(200);
        (*edit)->setStyleSheet("background-color: #334155; color: #e2e8f0; border-radius: 6px; padding: 3px;");
        (*edit)->setAlignment(Qt::AlignCenter);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->addWidget(lbl);
        hl->addWidget(*edit);
        QWidget *w = new QWidget();
        w->setLayout(hl);
        return w;
    };

    pidLayout->addWidget(createPIDEdit("Kp:", &m_editKp, "0.001"));
    pidLayout->addWidget(createPIDEdit("Ki:", &m_editKi, "0.0001"));
    pidLayout->addWidget(createPIDEdit("Kd:", &m_editKd, "0.00001"));

    QPushButton *buttonSetPID = new QPushButton("Apply PID");
    buttonSetPID->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; font-weight: bold; border-radius: 6px; padding: 6px 15px; }"
        "QPushButton:hover { background-color: #2563eb; }"
        );
    connect(buttonSetPID, &QPushButton::clicked, this, &MainWindow::onSetPIDClicked);
    pidLayout->addWidget(buttonSetPID);

    QPushButton *buttonResetPID = new QPushButton("Reset");
    buttonResetPID->setStyleSheet(
        "QPushButton { background-color: #ef4444; color: white; font-weight: bold; border-radius: 6px; padding: 6px 15px; }"
        "QPushButton:hover { background-color: #dc2626; }"
        );
    connect(buttonResetPID, &QPushButton::clicked, this, [this]() {
        m_Protocol->sendTerminalCommand("reset");
        m_terminal->append("> PID reset");
    });

    pidLayout->addWidget(buttonResetPID);

    pidLayout->addStretch();
    pidGroup->setLayout(pidLayout);
    // Terminal
    QGroupBox *terminalGroup = new QGroupBox("TERMINAL");
    terminalGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "   padding: 0 12px; "
        "}"
        );
    QVBoxLayout *terminalLayout = new QVBoxLayout();

    m_terminal = new QTextEdit();
    m_terminal->setReadOnly(true);
    m_terminal->setMaximumHeight(180);
    m_terminal->setStyleSheet("background-color: #0f172a; color: #10b981; "
                              "font-family: 'Courier New', monospace; font-size: 16px; "
                              "border: 1px solid #3b82f6; border-radius: 6px; padding: 5px;");
    terminalLayout->addWidget(m_terminal);

    QHBoxLayout *terminalInputLayout = new QHBoxLayout();
    m_editTerminal = new QLineEdit();
    m_editTerminal->setPlaceholderText("Enter command (e.g., status, kp 0.002)");
    m_editTerminal->setStyleSheet("background-color: #334155; color: #e2e8f0; border-radius: 6px; padding: 4px;");
    connect(m_editTerminal, &QLineEdit::returnPressed, this, &MainWindow::onSendTerminalClicked);
    terminalInputLayout->addWidget(m_editTerminal);

    QPushButton *buttonSend = new QPushButton("Send");
    buttonSend->setStyleSheet("background-color: #3b82f6; color: white; font-weight: bold; border-radius: 6px; padding: 6px 15px;");
    connect(buttonSend, &QPushButton::clicked, this, &MainWindow::onSendTerminalClicked);
    terminalInputLayout->addWidget(buttonSend);
    terminalLayout->addLayout(terminalInputLayout);

    terminalGroup->setLayout(terminalLayout);

    layout->addWidget(controlGroup);
    layout->addLayout(statusLayout);
    layout->addWidget(pidGroup);
    layout->addWidget(terminalGroup);
    layout->addStretch();
    m_tabWidget->addTab(dataWidget, "MOTOR CONTROL");
}

//Graph Tab
void MainWindow::setupGraphTab()
{
    QWidget *graphWidget = new QWidget();
    graphWidget->setStyleSheet("background-color:#0f172a;");
    QVBoxLayout *layout = new QVBoxLayout(graphWidget);
    layout->setSpacing(20);
    layout->setContentsMargins(15,15,15,15);
    //Control group
    QGroupBox *controlGroup = new QGroupBox("MOTOR CONTROL");
    controlGroup->setStyleSheet(
        "QGroupBox { "
        "   border: 2px solid #3b82f6; "
        "   border-radius: 10px; "
        "   margin-top: 16px; "
        "   background-color: #1e293b; "
        "   padding: 10px; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "}"
        "QGroupBox::title { "
        "   subcontrol-origin: margin; "
        "   subcontrol-position: top left; "
        "   background-color: #1e293b; "
        "   color: #60a5fa; "
        "   font-weight: bold; "
        "   font-size: 22px; "
        "   padding: 0 12px; "
        "}"
        );
    // Control layout
    QVBoxLayout *controlLayout = new QVBoxLayout();
    // Remote layout
    QHBoxLayout *remoteLayout = new QHBoxLayout();
    // Enable Remote Control
    m_checkRemoteControl_graph = new QCheckBox("Enable Remote Control");
    m_checkRemoteControl_graph->setStyleSheet(
        "QCheckBox { color: #e2e8f0; font-size: 20px; font-weight: bold; background: transparent; }"
        "QCheckBox::indicator { width: 22px; height: 22px; }"
        );
    connect(m_checkRemoteControl_graph, &QCheckBox::toggled, this, &MainWindow::onRemoteControlChosen);

    // Mode
    m_labelControlMode_graph = new QLabel("<span style='font-weight:bold; color:#e2e8f0;'>Running Mode: </span>"
                                         "<span style='color:red;font-weight:bold'>ADC SIGNAL</span>");
    m_labelControlMode_graph->setStyleSheet("background: transparent; font-size: 20px; font-weight: bold;");
    // Add remote layout
    remoteLayout->addWidget(m_checkRemoteControl_graph);
    remoteLayout->addStretch();
    remoteLayout->addWidget(m_labelControlMode_graph);
    // RPM Slider
    QLabel *labelSlider = new QLabel("Target RPM:");
    labelSlider->setStyleSheet("color: #66FFFF; font-size: 22px; font-weight: bold; background: transparent;");
    // Slider and spinbox value
    QHBoxLayout *sliderLayout = new QHBoxLayout();
    m_sliderRPM_graph = new QSlider(Qt::Horizontal);
    m_sliderRPM_graph->setRange(-m_maxRPM, m_maxRPM);
    m_sliderRPM_graph->setValue(0);
    m_sliderRPM_graph->setEnabled(false);
    m_sliderRPM_graph->setStyleSheet(
        "QSlider::groove:horizontal { height: 8px; background: #334155; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #3b82f6; width: 20px; margin: -6px 0; border-radius: 10px; }"
        );
    connect(m_sliderRPM_graph, &QSlider::valueChanged, this, &MainWindow::onRPMSliderChanged);
    sliderLayout->addWidget(m_sliderRPM_graph);
    m_spinRPM_graph = new QSpinBox();
    m_spinRPM_graph->setRange(-m_maxRPM,m_maxRPM);
    m_spinRPM_graph->setValue(0);
    m_spinRPM_graph->setSuffix(" RPM");
    m_spinRPM_graph->setEnabled(false);
    m_spinRPM_graph->setMinimumWidth(150);
    m_spinRPM_graph->setStyleSheet("background-color: #334155; color: #e2e8f0; font-weight: bold; font-size: 16px; border-radius: 6px; padding: 5px;");
    connect(m_spinRPM_graph, &QSpinBox::valueChanged, this, &MainWindow::onRPMSpinChanged);
    sliderLayout->addWidget(m_spinRPM_graph);
    // STOP button
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_buttonStop_graph = new QPushButton("STOP");
    m_buttonStop_graph->setStyleSheet(
        "QPushButton { background-color: #ef4444; color: white; font-weight: bold; "
        "font-size: 18px; padding: 6px 12px; border-radius: 6px; }"
        "QPushButton:hover { background-color: #dc2626; }"
        );
    m_buttonStop_graph->setEnabled(false);
    m_buttonStop_graph->setFixedHeight(40);
    connect(m_buttonStop_graph, &QPushButton::clicked, this, &MainWindow::onStopMotor);
    buttonLayout->addWidget(m_buttonStop_graph);
    // Add control layout and group
    controlLayout->addLayout(remoteLayout);
    controlLayout->addWidget(labelSlider);
    controlLayout->addLayout(sliderLayout);
    controlLayout->addLayout(buttonLayout);
    controlGroup->setLayout(controlLayout);
    layout->addWidget(controlGroup);

    // Ctrl
    QHBoxLayout *ctrl = new QHBoxLayout();
    // Auto scroll
    QCheckBox *chkFollow = new QCheckBox("Auto Scroll");
    chkFollow->setChecked(true);
    chkFollow->setStyleSheet("color:#e2e8f0; font-weight:bold; font-size: 20");
    connect(chkFollow, &QCheckBox::toggled, this, [this](bool on){
        m_followTime = on;
    });
    // Time range
    QLabel *lblWin = new QLabel("Time range (s):");
    lblWin->setStyleSheet("color:#e2e8f0; font-weight:bold; font-size: 20");
    QComboBox *cbWin = new QComboBox();
    cbWin->addItems({"5","10","20","30","60"});
    cbWin->setCurrentText("5");
    cbWin->setMaximumWidth(80);
    cbWin->setStyleSheet("background:#334155; color:#e2e8f0;");
    connect(cbWin, &QComboBox::currentTextChanged, this, [this](const QString &s){
        m_fixedWindowSec = s.toDouble();
        if (m_axisX) {
            if (m_followTime && m_time > m_fixedWindowSec) {
                m_axisX->setRange(m_time - m_fixedWindowSec, m_time);
            } else {
                m_axisX->setRange(0.0, m_fixedWindowSec);
            }
        }
    });
    // Auto Y
    QCheckBox *chkAutoY = new QCheckBox("Auto Scale Y");
    chkAutoY->setChecked(true);
    chkAutoY->setStyleSheet("color:#e2e8f0; font-weight:bold; font-size: 20");
    connect(chkAutoY, &QCheckBox::toggled, this, [this](bool on){
        m_autoScaleY = on;
    });
    // Fixed Y inputs
    QLabel *lblYR = new QLabel("Fixed Y range:");
    lblYR->setStyleSheet("color:#e2e8f0; font-weight:bold; font-size: 20");
    QComboBox *cbY = new QComboBox();
    cbY->addItems({"± 2000","± 3000", "± 4000", "± 5000","± MaxRPM"});
    cbY->setCurrentText("± MaxRPM");
    cbY->setMaximumWidth(120);
    cbY->setStyleSheet("background:#334155; color:#e2e8f0;");
    connect(cbY, &QComboBox::currentTextChanged, this, [this](const QString &s){
        if (s.startsWith("± MaxRPM")) {
            m_fixedYmin = -(m_maxRPM + 500);
            m_fixedYmax = +(m_maxRPM + 500);
        } else {
            int val = s.mid(1).toInt();
            m_fixedYmin = -val;
            m_fixedYmax =  val;
        }
        if (!m_autoScaleY && m_axisY) {
            m_axisY->setRange(m_fixedYmin, m_fixedYmax);
        }
    });

    // Rescale Y
    QPushButton *buttonFitY = new QPushButton("Rescale");
    buttonFitY->setStyleSheet("background:#3b82f6; color:#fff; font-weight:bold; padding:6px 12px; border-radius:6px;");
    connect(buttonFitY, &QPushButton::clicked, this, [this](){
        if (!m_axisY || !m_seriesActual) return;
        double ymax = m_maxRPM;
        for (const QPointF &pt : m_seriesActual->points()) {
            ymax = std::max(ymax, std::abs(pt.y()));
        }
        for (const QPointF &pt : m_seriesTarget->points()) {
            ymax = std::max(ymax, std::abs(pt.y()));
        }
        m_axisY->setRange(-ymax - 200, ymax + 200);
    });
    // Pack controls
    ctrl->addWidget(chkFollow);
    ctrl->addSpacing(8);
    ctrl->addWidget(lblWin);
    ctrl->addWidget(cbWin);
    ctrl->addSpacing(16);
    ctrl->addWidget(chkAutoY);
    ctrl->addSpacing(8);
    ctrl->addWidget(lblYR);
    ctrl->addWidget(cbY);
    ctrl->addSpacing(16);
    ctrl->addWidget(buttonFitY);
    ctrl->addStretch();

    // Live value display
    m_labelLiveActual = new QLabel("Actual: 0 RPM");
    m_labelLiveActual->setStyleSheet(
        "color:#10b981; font-size:18px; font-weight:bold; "
        "background:#1e293b; padding:8px 16px; border-radius:6px; border:2px solid #10b981;"
        );
    m_labelLiveTarget = new QLabel("Target: 0 RPM");
    m_labelLiveTarget->setStyleSheet(
        "color:#ef4444; font-size:18px; font-weight:bold; "
        "background:#1e293b; padding:8px 16px; border-radius:6px; border:2px solid #ef4444;"
        );
    m_labelLiveError = new QLabel("Error: 0 RPM");
    m_labelLiveError->setStyleSheet(
        "color:#ffffff; font-size:18px; font-weight:bold; "
        "background:#1e293b; padding:8px 16px; border-radius:6px; border:2px solid #ffffff;"
        );

    ctrl->addWidget(m_labelLiveActual);
    ctrl->addWidget(m_labelLiveTarget);
    ctrl->addWidget(m_labelLiveError);

    QPushButton *buttonClear = new QPushButton("Clear Graph");
    buttonClear->setStyleSheet(
        "QPushButton{background:#ef4444; color:#fff; font-weight:bold; "
        "padding:5px 20px; border-radius:8px; font-size:20px;}"
        "QPushButton:hover{background:#dc2626;}"
        );
    buttonClear->setMinimumWidth(140);
    connect(buttonClear, &QPushButton::clicked, this, [this]() {
        m_time = 0.0;
        if (m_seriesActual) m_seriesActual->clear();
        if (m_seriesTarget) m_seriesTarget->clear();
        if (m_axisX) m_axisX->setRange(0, 10);
        if (m_chart) m_chart->update();
        if (m_terminal) m_terminal->append("> Graph cleared");
    });
    ctrl->addWidget(buttonClear);
    layout->addLayout(ctrl);

    // Chart setup
    m_chart = new QChart();
    m_chart->setTitle("MOTOR REAL-TIME RPM");
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->setBackgroundBrush(QBrush(QColor(230, 255, 255)));
    m_chart->setTitleBrush(QBrush(QColor(0, 0, 204)));
    QFont titleFont;
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    m_chart->setTitleFont(titleFont);
    m_chart->setMargins(QMargins(10, 5, 10, 10));

    // Series
    m_seriesActual = new QLineSeries();
    m_seriesActual->setName("Actual RPM");
    m_seriesActual->setPen(QPen(QColor(0,204,102), 3.5));
    m_seriesTarget = new QLineSeries();
    m_seriesTarget->setName("Target RPM");
    QPen redDash(QColor(239, 68, 68), 3.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
    m_seriesTarget->setPen(redDash);
    m_seriesActual->setUseOpenGL(true);
    m_seriesTarget->setUseOpenGL(true);

    m_chart->addSeries(m_seriesActual);
    m_chart->addSeries(m_seriesTarget);

    //legend
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setLabelColor(QColor("#000000"));
    QFont legendFont;
    legendFont.setPointSize(20);

    // Axes with better styling
    m_axisX = new QValueAxis();
    m_axisY = new QValueAxis();
    m_axisX->setTitleText("<span style='font-size:14px; font-weight:bold; color:#370EA8;'>Time(s)</span>");
    m_axisY->setTitleText("<span style='font-size:14px; font-weight:bold; color:#370EA8;'>RPM</span>");
    m_axisX->setRange(0, 10);
    m_axisY->setRange(-m_maxRPM - 500, m_maxRPM + 500);
    m_axisX->setTickCount(11);
    m_axisY->setTickCount(9);
    m_axisX->setMinorTickCount(1);

    // Axis colors
    QFont axisFont;
    axisFont.setPointSize(11);
    axisFont.setBold(true);
    m_axisX->setLabelsFont(axisFont);
    m_axisY->setLabelsFont(axisFont);

    m_axisX->setLabelsBrush(QBrush(QColor(0,0,102)));
    m_axisY->setLabelsBrush(QBrush(QColor(0,0,102)));
    m_axisX->setTitleBrush(QBrush(QColor(96, 165, 250)));
    m_axisY->setTitleBrush(QBrush(QColor(96, 165, 250)));
    m_axisX->setGridLineColor(QColor(71, 85, 105));
    m_axisY->setGridLineColor(QColor(71, 85, 105));
    m_axisX->setLinePen(QPen(QColor(96, 165, 250), 2));
    m_axisY->setLinePen(QPen(QColor(96, 165, 250), 2));

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_seriesActual->attachAxis(m_axisX);
    m_seriesActual->attachAxis(m_axisY);
    m_seriesTarget->attachAxis(m_axisX);
    m_seriesTarget->attachAxis(m_axisY);

    //
    m_chartView = new CustomChartView(m_chart); // custom subclass adds mouse-tracking
    m_chartView->setRenderHint(QPainter::Antialiasing, true);
    m_chartView->setStyleSheet(
        "QChartView { background:#0f172a; border:2px solid #3b82f6; border-radius:12px; }"
        );

    layout->addWidget(m_chartView, 1);
    m_tabWidget->addTab(graphWidget, "GRAPH");
}

void MainWindow::onTerminalOutput(const QString &text)
{
    m_terminal->append(text);
    m_terminal->moveCursor(QTextCursor::End);

    // Parse motor info responses
    if (text.startsWith("Motor Type:")) {
        QString type = text.mid(11).trimmed();
        m_labelMotorType->setText(type);
    }
    else if (text.startsWith("Motor Poles:")) {
        QString poles = text.mid(12).trimmed();
        const int int_poles = text.mid(12).trimmed().toInt();
        m_motorPoles = int_poles; // used by the ERPM <-> RPM conversion helpers
        m_labelMotorPoles->setText(poles);
    }
    else if (text.startsWith("Max RPM:")) {
        QString rpm = text.mid(8).trimmed();
        m_labelMaxRPM->setText(rpm);

        // Update slider range to match
        bool ok;
        int maxRPM = rpm.toInt(&ok);
        if (ok) {
            m_maxRPM = maxRPM;
            m_sliderRPM_data->setRange(-m_maxRPM, m_maxRPM);
            m_spinRPM_data->setRange(-m_maxRPM, m_maxRPM);
            m_sliderRPM_graph->setRange(-m_maxRPM, m_maxRPM);
            m_spinRPM_graph->setRange(-m_maxRPM, m_maxRPM);

            if (!m_autoScaleY) {
                // nếu đang khóa theo ±MaxRPM thì cập nhật cho khớp
                m_fixedYmin = -(m_maxRPM + 500);
                m_fixedYmax = +(m_maxRPM + 500);
                if (m_axisY) m_axisY->setRange(m_fixedYmin, m_fixedYmax);
            } else {
                // auto-fit sẽ tự xử lý ở lần tick tiếp theo
            }
        }

    }
    else if (text.startsWith("Max Current:")) {
        QString current = text.mid(12).trimmed();
        m_labelMaxCurrent->setText(current);
    }

    if (text.startsWith("Runtime limits:")) {
        m_parsingLimits = true;
        return;
    }
    if (!m_parsingLimits) return;

    //
    if (text.startsWith("Runtime Max RPM:")) {
        QRegularExpression re(R"(Runtime Max RPM:\s*([\-]?\d+))");
        auto m = re.match(text);
        if (m.hasMatch()) {
            int rpm = m.captured(1).toInt();
            bool active = text.contains("(ACTIVE)");
            // Nếu ACTIVE -> set đúng giá trị; nếu không -> trả về 0 (HW default)
            m_spinMaxRPM->blockSignals(true);
            m_spinMaxRPM->setValue(active ? rpm : 0);
            m_spinMaxRPM->blockSignals(false);
        }
        return;
    }

    // Runtime Max Current: 25.0 A (ACTIVE) | ... (using hardware)
    if (text.startsWith("Runtime Max Current:")) {
        QRegularExpression re(R"(Runtime Max Current:\s*([0-9]+(?:\.[0-9]+)?)\s*A)");
        auto m = re.match(text);
        if (m.hasMatch()) {
            double a = m.captured(1).toDouble();
            bool active = text.contains("(ACTIVE)");
            m_spinMaxCurrent->blockSignals(true);
            m_spinMaxCurrent->setValue(active ? a : 0.0);
            m_spinMaxCurrent->blockSignals(false);
        }
        return;
    }

    // khi hết block (dòng nào khác sau 2 dòng trên) thì đóng cờ
    if (!text.startsWith("Hardware Max RPM:")
        && !text.startsWith("Hardware Max Current:")
        && !text.startsWith("Runtime Max RPM:")
        && !text.startsWith("Runtime Max Current:")
        && !text.startsWith("Runtime limits:")) {
        m_parsingLimits = false;
    }
}

void MainWindow::onDataReceived(const VESCData &data) {
    // Status cards
    m_labelVoltage->setText(QString::number(data.voltage, 'f', 2));
    m_labelCurrent->setText(QString::number(data.current, 'f', 2));
    m_labelRPM->setText(QString::number(data.rpm, 'f', 0));
    m_labelDuty->setText(QString::number(data.duty * 100.0f, 'f', 1)); // %

    // Temperature, etc.
    updateMotorStatus(data);

    // Throttle graph update (10 Hz)
    m_latestActualRPM = data.rpm;
    m_latestTargetRPM = (m_remoteControlEnabled ? m_spinRPM_data->value() : data.targetRPM);
}

void MainWindow::onRemoteControlChosen(bool enabled) {
    m_remoteControlEnabled = enabled;

    if (enabled) {
        if (!m_Protocol->isConnected()) {
            QMessageBox::warning(this, "Error", "Please connect to VESC first!");
            m_checkRemoteControl_data->setChecked(false);
            m_checkRemoteControl_graph->setChecked(false);
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setWindowTitle("Enable Remote Control");
        msgBox.setText(
            "Enable remote control from Qt App?\n\n"
            "Motor will be controlled by slider/buttons\n"
            "Hardware ADC will be disabled\n"
            "Auto-disables after 3 seconds of inactivity\n\n"
            "Continue?"
            );
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);

        msgBox.setStyleSheet(
            "QLabel { color: white; font-size: 16px; }"
            "QPushButton { background-color: #3b82f6; color: white; "
            "  font-weight: bold; padding: 8px 20px; border-radius: 6px; min-width: 80px; }"
            "QPushButton:hover { background-color: #2563eb; }"
            );

        QMessageBox::StandardButton reply =
            static_cast<QMessageBox::StandardButton>(msgBox.exec());

        if (reply == QMessageBox::Yes) {
            m_Protocol->sendTerminalCommand("remote on");

            // Enable DATA tab
            m_sliderRPM_data->setEnabled(true);
            m_spinRPM_data->setEnabled(true);
            m_buttonStop_data->setEnabled(true);
            m_labelControlMode_data->setText("<span style='font-weight:bold; color:white;'>Running Mode: </span>"
                                             "<span style='color:green;font-weight:bold'>REMOTE APP</span>");

            // Mirror to GRAPH tab
            m_sliderRPM_graph->setEnabled(true);
            m_spinRPM_graph->setEnabled(true);
            m_buttonStop_graph->setEnabled(true);
            m_labelControlMode_graph->setText("<span style='font-weight:bold; color:white;'>Running Mode: </span>"
                                              "<span style='color:green;font-weight:bold'>REMOTE APP</span>");

            m_terminal->append("> Remote control ENABLED");
        } else {
            m_checkRemoteControl_data->setChecked(false);
            m_checkRemoteControl_graph->setChecked(false);
        }
    } else {
        m_Protocol->sendTerminalCommand("remote off");

        // Disable DATA tab
        m_sliderRPM_data->setEnabled(false);
        m_sliderRPM_data->setValue(0);
        m_spinRPM_data->setEnabled(false);
        m_spinRPM_data->setValue(0);
        m_buttonStop_data->setEnabled(false);
        m_labelControlMode_data->setText("<span style='font-weight:bold; color:white;'>Running Mode: </span>"
                                         "<span style='color:red;font-weight:bold'>ADC SIGNAL</span>");

        // Mirror to GRAPH tab
        m_sliderRPM_graph->setEnabled(false);
        m_sliderRPM_graph->setValue(0);
        m_spinRPM_graph->setEnabled(false);
        m_spinRPM_graph->setValue(0);
        m_buttonStop_graph->setEnabled(false);
        m_labelControlMode_graph->setText("<span style='font-weight:bold; color:white;'>Running Mode: </span>"
                                          "<span style='color:red;font-weight:bold'>ADC SIGNAL</span>");

        m_terminal->append("> Remote control DISABLED");
    }
}

void MainWindow::onRPMSliderChanged(int value) {
    // Sync spinbox DATA tab
    m_spinRPM_data->blockSignals(true);
    m_spinRPM_data->setValue(value);
    m_spinRPM_data->blockSignals(false);

    // Mirror to GRAPH tab
    m_spinRPM_graph->blockSignals(true);
    m_spinRPM_graph->setValue(value);
    m_spinRPM_graph->blockSignals(false);

    m_labelLiveTarget->setText(QString("Target: %1 RPM").arg(value));

    if (m_remoteControlEnabled && m_Protocol->isConnected()) {
        const int erpm = elecFromMech(value);
        m_Protocol->sendTerminalCommand(QString("setrpm %1").arg(erpm));
    }
}

void MainWindow::onRPMSpinChanged(int value) {
    // Sync slider DATA tab
    m_sliderRPM_data->blockSignals(true);
    m_sliderRPM_data->setValue(value);
    m_sliderRPM_data->blockSignals(false);

    // Mirror to GRAPH tab
    m_sliderRPM_graph->blockSignals(true);
    m_sliderRPM_graph->setValue(value);
    m_sliderRPM_graph->blockSignals(false);

    m_labelLiveTarget->setText(QString("Target: %1 RPM").arg(value));

    if (m_remoteControlEnabled && m_Protocol->isConnected()) {
        const int erpm = elecFromMech(value);
        m_Protocol->sendTerminalCommand(QString("setrpm %1").arg(erpm));
    }
}

void MainWindow::onStopMotor() {
    // Reset DATA tab
    m_sliderRPM_data->setValue(0);
    m_spinRPM_data->setValue(0);

    // Reset GRAPH tab (auto-synced via the existing connect())
    m_sliderRPM_graph->setValue(0);
    m_spinRPM_graph->setValue(0);

    m_terminal->append("> Motor STOPPED");
}

void MainWindow::onSendTerminalClicked() {
    QString cmd = m_editTerminal->text().trimmed();
    if (!cmd.isEmpty()) {
        m_terminal->append("> " + cmd);
        m_Protocol->sendTerminalCommand(cmd);
        m_editTerminal->clear();
    }
}

void MainWindow::onSetPIDClicked() {
    float kp = m_editKp->text().toFloat();
    float ki = m_editKi->text().toFloat();
    float kd = m_editKd->text().toFloat();

    m_Protocol->setPID(kp, ki, kd);
    m_terminal->append(QString("> PID updated: Kp=%1, Ki=%2, Kd=%3").arg(kp, 0, 'f', 7).arg(ki, 0, 'f', 7).arg(kd, 0, 'f', 7));
}

void MainWindow::updateMotorStatus(const VESCData &data) {
    m_labelTempMOSFET->setText(QString::number(data.tempMOSFET, 'f', 1) + " C");
    m_labelTempMotor->setText(QString::number(data.tempMotor, 'f', 1) + " C");
}


void MainWindow::updateRpmGraph(double actualRPM, double targetRPM) {
    m_time += 0.05;

    // Update live labels (lightweight, always update)
    m_labelLiveActual->setText(QString("Actual: %1 RPM").arg(actualRPM, 0, 'f', 0));
    if (!m_remoteControlEnabled) {
        m_labelLiveTarget->setText(QString("Target: %1 RPM").arg(targetRPM, 0, 'f', 0));
    }

    // Remove old points BEFORE appending (more efficient)
    if (m_seriesActual->count() >= m_maxPlotPoints) {
        m_seriesActual->remove(0);
        m_seriesTarget->remove(0);
    }

    // Append new data (O(1) operation, much faster than replace)
    m_seriesActual->append(m_time, actualRPM);
    m_seriesTarget->append(m_time, targetRPM);
    double error = targetRPM - actualRPM;
    m_labelLiveError->setText(QString("Error: %1 RPM").arg(error, 0, 'f', 0));
    // Auto-scroll X axis
    if (m_axisX && m_followTime) {
        if (m_time > m_fixedWindowSec) {
            m_axisX->setRange(m_time - m_fixedWindowSec, m_time);
        } else {
            m_axisX->setRange(0.0, m_fixedWindowSec);
        }
    }

    // Smart Y-scale with exponential moving average (no buffer scan)
    if (m_axisY && m_autoScaleY) {
        static double smoothMax = 0.0;
        if (smoothMax == 0.0) smoothMax = m_maxRPM;  // Initialize once

        double instant = std::max({std::abs(actualRPM), std::abs(targetRPM), (double)m_maxRPM});
        smoothMax = smoothMax * 0.95 + instant * 0.05;  // EMA filter

        m_axisY->setRange(-smoothMax - 200, smoothMax + 200);
    } else if (m_axisY && !m_autoScaleY) {
        // Only update if needed
        static double lastMin = 0, lastMax = 0;
        if (lastMin != m_fixedYmin || lastMax != m_fixedYmax) {
            m_axisY->setRange(m_fixedYmin, m_fixedYmax);
            lastMin = m_fixedYmin;
            lastMax = m_fixedYmax;
        }
    }
}