#include "mainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QDataStream>
#include <QDebug>
#include <QIntValidator>
#include <bitset>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tcpSocket(nullptr)
    , continuousTimer(nullptr)
    , transactionId(0)
    , displayFormat(FormatDecimal)
    , serialPort(nullptr)
{
    setWindowTitle("李晨阳的linux工作助手");
    resize(1200, 720);

    // Initialize Objects
    tcpSocket = new QTcpSocket(this);
    continuousTimer = new QTimer(this);
    serialPort = new QSerialPort(this);

    // Create UI
    createWidgets();
    createLayouts();
    createConnections();
    
    // Load History
    loadConnectionHistory();
    loadGitHistory();
    loadRegisterTables();

    // Initial State
    updateConnectionStatus(false);
    updateSerialStatus(false);
    
    // Select first page
    navWidget->setCurrentRow(0);
}

MainWindow::~MainWindow()
{
    saveRegisterTables();

    if (tcpSocket && tcpSocket->state() == QAbstractSocket::ConnectedState)
        tcpSocket->disconnectFromHost();
        
    if (serialPort && serialPort->isOpen())
        serialPort->close();
}

void MainWindow::createWidgets()
{
    // --- Navigation ---
    navWidget = new QListWidget();
    navWidget->addItem("Modbus TCP 助手");
    navWidget->addItem("串口调试助手");
    navWidget->addItem("Git 工作流助手");
    navWidget->setFixedWidth(160);
    navWidget->setStyleSheet("QListWidget::item { height: 50px; padding-left: 10px; font-size: 14px; } "
                             "QListWidget::item:selected { background-color: #3399ff; color: white; }");

    // --- Page 1: Modbus TCP Widgets ---
    
    // Connection
    lblIP = new QLabel("IP地址:");
    cmbIP = new QComboBox();
    cmbIP->setEditable(true);
    cmbIP->setMinimumWidth(150);
    
    lblPort = new QLabel("端口:");
    txtPort = new QLineEdit("502");
    txtPort->setValidator(new QIntValidator(1, 65535, this));
    txtPort->setMaximumWidth(60);

    lblSlaveID = new QLabel("从站ID:");
    txtSlaveID = new QLineEdit("1");
    txtSlaveID->setValidator(new QIntValidator(1, 247, this));
    txtSlaveID->setMaximumWidth(40);

    lblTimeout = new QLabel("超时(ms):");
    spinTimeout = new QSpinBox();
    spinTimeout->setRange(100, 10000);
    spinTimeout->setValue(3000);
    spinTimeout->setMaximumWidth(80);

    lblRetries = new QLabel("重试:");
    spinRetries = new QSpinBox();
    spinRetries->setRange(0, 10);
    spinRetries->setValue(3);
    spinRetries->setMaximumWidth(50);

    lblStatus = new QLabel("状态:");
    lblStatusText = new QLabel("未连接");
    lblStatusText->setStyleSheet("color: red; font-weight: bold;");

    btnConnect = new QPushButton("连接");
    btnConnect->setStyleSheet("font-weight: bold; background-color: #e6f3ff;");
    btnDisconnect = new QPushButton("断开");
    btnDisconnect->setEnabled(false);

    // Register Map Tables
    tabRegisterMaps = new QTabWidget();
    tblAGV = new QTableWidget();
    tblRobot = new QTableWidget();
    setupRegisterTable(tblAGV);
    setupRegisterTable(tblRobot);
    tabRegisterMaps->addTab(tblAGV, "AGV");
    tabRegisterMaps->addTab(tblRobot, "机器人");

    // Read Group
    lblReadStartAddr = new QLabel("起始地址:");
    spinReadStartAddr = new QSpinBox();
    spinReadStartAddr->setRange(0, 65535);
    
    lblReadQuantity = new QLabel("数量:");
    spinReadQuantity = new QSpinBox();
    spinReadQuantity->setRange(1, 125);
    spinReadQuantity->setValue(10);
    
    chkContinuousRead = new QCheckBox("连续");
    lblReadInterval = new QLabel("间隔(ms):");
    spinReadInterval = new QSpinBox();
    spinReadInterval->setRange(100, 10000);
    spinReadInterval->setValue(1000);
    spinReadInterval->setEnabled(false); // Default disabled until checked

    lblDisplayFormat = new QLabel("格式:");
    cmbDisplayFormat = new QComboBox();
    cmbDisplayFormat->addItems(QStringList() << "十进制" << "十六进制" << "二进制");

    btnReadCoils = new QPushButton("读线圈(01)");
    btnReadInputs = new QPushButton("读输入(02)");
    btnReadHoldingRegisters = new QPushButton("读保持(03)");
    btnReadInputRegisters = new QPushButton("读输入Reg(04)");

    // Write Group
    lblWriteStartAddr = new QLabel("起始地址:");
    spinWriteStartAddr = new QSpinBox();
    spinWriteStartAddr->setRange(0, 65535);
    
    lblWriteQuantity = new QLabel("数量:");
    spinWriteQuantity = new QSpinBox();
    spinWriteQuantity->setRange(1, 120);
    spinWriteQuantity->setValue(1);
    
    lblWriteValue = new QLabel("单值:");
    spinWriteValue = new QSpinBox();
    spinWriteValue->setRange(0, 65535); 
    
    chkWriteCoil = new QCheckBox("线圈ON");
    
    lblWriteValues = new QLabel("多值(逗号隔开):");
    txtWriteValues = new QLineEdit();
    txtWriteValues->setPlaceholderText("e.g. 100,200,300");

    btnWriteSingleCoil = new QPushButton("写单线圈(05)");
    btnWriteSingleRegister = new QPushButton("写单Reg(06)");
    btnWriteMultipleCoils = new QPushButton("写多线圈(15)");
    btnWriteMultipleRegisters = new QPushButton("写多Reg(16)");

    // Logs
    txtResult = new QTextEdit();
    txtResult->setReadOnly(true);
    txtResult->setPlaceholderText("读取数据将显示在此处...");
    
    txtLog = new QTextEdit();
    txtLog->setReadOnly(true);
    txtLog->setPlaceholderText("通讯日志...");
    
    btnClearLog = new QPushButton("清空日志");

    // --- Page 2: Serial Widgets ---
    lblSerialPort = new QLabel("串口:");
    cmbSerialPort = new QComboBox();
    btnRefreshPorts = new QPushButton("刷新");
    
    lblBaudRate = new QLabel("波特率:");
    cmbBaudRate = new QComboBox();
    cmbBaudRate->addItems(QStringList() << "9600" << "19200" << "38400" << "57600" << "115200" << "230400" << "460800" << "921600");
    cmbBaudRate->setCurrentText("115200");
    
    lblDataBits = new QLabel("数据位:");
    cmbDataBits = new QComboBox();
    cmbDataBits->addItems(QStringList() << "8" << "7" << "6" << "5");
    
    lblParity = new QLabel("校验位:");
    cmbParity = new QComboBox();
    cmbParity->addItems(QStringList() << "None" << "Even" << "Odd" << "Space" << "Mark");
    
    lblStopBits = new QLabel("停止位:");
    cmbStopBits = new QComboBox();
    cmbStopBits->addItems(QStringList() << "1" << "1.5" << "2");
    
    btnSerialOpen = new QPushButton("打开串口");
    btnSerialClose = new QPushButton("关闭串口");
    btnSerialClose->setEnabled(false);
    lblSerialStatus = new QLabel("串口关闭");
    lblSerialStatus->setStyleSheet("color: red; font-weight: bold;");

    chkHexSend = new QCheckBox("Hex发送");
    chkHexDisplay = new QCheckBox("Hex显示");

    txtSerialRecv = new QTextEdit();
    txtSerialRecv->setReadOnly(true);
    
    txtSerialSend = new QTextEdit();
    txtSerialSend->setMaximumHeight(100);
    
    btnSerialSend = new QPushButton("发送");
    btnSerialClearRecv = new QPushButton("清空接收");
    
    refreshSerialPorts();

    // --- Page 3: Git Widgets ---
    // txtGitDir removed, using cmbGitDir
    // cmbGitDir initialized in createGitPage or here?
    // Let's initialize here to be safe and consistent
    cmbGitDir = new QComboBox();
    cmbGitDir->setEditable(true);
    cmbGitDir->setPlaceholderText("选择Git仓库路径...");
    // loadGitHistory(); // Call this later or here? Better here.
    
    btnGitSelectDir = new QPushButton("选择目录");
    
    cmbGitBranches = new QComboBox();
    btnGitRefreshBranches = new QPushButton("刷新分支");
    btnGitCheckout = new QPushButton("切换分支");
    
    txtGitCommitMsg = new QLineEdit();
    txtGitCommitMsg->setPlaceholderText("Git Commit Message...");
    
    btnGitAdd = new QPushButton("git add .");
    btnGitCommit = new QPushButton("git commit");
    btnGitPush = new QPushButton("git push");
    btnGitPull = new QPushButton("git pull");
    btnGitMerge = new QPushButton("git merge");
    btnGitStatus = new QPushButton("git status");
    
    cmbGitHistory = new QComboBox();
    btnGitRefreshLog = new QPushButton("刷新历史");
    btnGitReset = new QPushButton("硬回退 (Reset)");
    btnGitReset->setStyleSheet("color: red; font-weight: bold;");
    
    txtGitLog = new QTextEdit();
    txtGitLog->setReadOnly(true);
    txtGitLog->setStyleSheet("background: #1e1e1e; color: #d4d4d4; font-family: Monospace; font-size: 12px;");
}

QWidget* MainWindow::createModbusPage()
{
    QWidget *page = new QWidget();
    QHBoxLayout *mainPageLayout = new QHBoxLayout(page); // Split Left/Right
    
    // LEFT SIDE: Controls
    QWidget *leftWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(leftWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 1. Connection Group
    QGroupBox *grpConn = new QGroupBox("连接设置 (记忆最后10个地址)");
    QGridLayout *layConn = new QGridLayout();
    layConn->addWidget(lblIP, 0, 0); layConn->addWidget(cmbIP, 0, 1);
    layConn->addWidget(lblPort, 0, 2); layConn->addWidget(txtPort, 0, 3);
    layConn->addWidget(lblSlaveID, 0, 4); layConn->addWidget(txtSlaveID, 0, 5);
    
    layConn->addWidget(lblTimeout, 1, 0); layConn->addWidget(spinTimeout, 1, 1);
    layConn->addWidget(lblRetries, 1, 2); layConn->addWidget(spinRetries, 1, 3);
    layConn->addWidget(lblStatus, 1, 4); layConn->addWidget(lblStatusText, 1, 5);

    QHBoxLayout *layBtns = new QHBoxLayout();
    layBtns->addWidget(btnConnect);
    layBtns->addWidget(btnDisconnect);
    layBtns->addStretch();
    layConn->addLayout(layBtns, 2, 0, 1, 6);
    
    grpConn->setLayout(layConn);
    layout->addWidget(grpConn);

    // 2. Operations (Horizontal)
    QHBoxLayout *layOps = new QHBoxLayout();
    
    // Read Group
    QGroupBox *grpRead = new QGroupBox("读取操作");
    QVBoxLayout *layRead = new QVBoxLayout();
    
    QHBoxLayout *r1 = new QHBoxLayout();
    r1->addWidget(lblReadStartAddr); r1->addWidget(spinReadStartAddr);
    r1->addWidget(lblReadQuantity); r1->addWidget(spinReadQuantity);
    
    QHBoxLayout *r2 = new QHBoxLayout();
    r2->addWidget(chkContinuousRead);
    r2->addWidget(lblReadInterval); r2->addWidget(spinReadInterval);
    
    QHBoxLayout *r3 = new QHBoxLayout();
    r3->addWidget(lblDisplayFormat); r3->addWidget(cmbDisplayFormat);
    
    QGridLayout *r4 = new QGridLayout();
    r4->addWidget(btnReadCoils, 0, 0); r4->addWidget(btnReadInputs, 0, 1);
    r4->addWidget(btnReadHoldingRegisters, 1, 0); r4->addWidget(btnReadInputRegisters, 1, 1);
    
    layRead->addLayout(r1); layRead->addLayout(r2); layRead->addLayout(r3); layRead->addLayout(r4);
    grpRead->setLayout(layRead);
    layOps->addWidget(grpRead);

    // Write Group
    QGroupBox *grpWrite = new QGroupBox("写入操作");
    QVBoxLayout *layWrite = new QVBoxLayout();
    
    QHBoxLayout *w1 = new QHBoxLayout();
    w1->addWidget(lblWriteStartAddr); w1->addWidget(spinWriteStartAddr);
    w1->addWidget(lblWriteQuantity); w1->addWidget(spinWriteQuantity);
    
    QHBoxLayout *w2 = new QHBoxLayout();
    w2->addWidget(lblWriteValue); w2->addWidget(spinWriteValue);
    w2->addWidget(chkWriteCoil);
    
    QHBoxLayout *w3 = new QHBoxLayout();
    w3->addWidget(lblWriteValues); w3->addWidget(txtWriteValues);
    
    QGridLayout *w4 = new QGridLayout();
    w4->addWidget(btnWriteSingleCoil, 0, 0); w4->addWidget(btnWriteSingleRegister, 0, 1);
    w4->addWidget(btnWriteMultipleCoils, 1, 0); w4->addWidget(btnWriteMultipleRegisters, 1, 1);
    
    layWrite->addLayout(w1); layWrite->addLayout(w2); layWrite->addLayout(w3); layWrite->addLayout(w4);
    grpWrite->setLayout(layWrite);
    layOps->addWidget(grpWrite);
    
    layout->addLayout(layOps);

    // 3. Results & Logs
    QHBoxLayout *layBottom = new QHBoxLayout();
    
    QGroupBox *grpResult = new QGroupBox("读取结果");
    QVBoxLayout *lres = new QVBoxLayout();
    lres->addWidget(txtResult);
    grpResult->setLayout(lres);
    layBottom->addWidget(grpResult);
    
    QGroupBox *grpLog = new QGroupBox("日志");
    QVBoxLayout *llog = new QVBoxLayout();
    llog->addWidget(txtLog);
    llog->addWidget(btnClearLog, 0, Qt::AlignRight);
    grpLog->setLayout(llog);
    layBottom->addWidget(grpLog);

    layout->addLayout(layBottom);
    
    // Stretch
    layout->setStretch(0, 0);
    layout->setStretch(1, 0);
    layout->setStretch(2, 1);
    
    // RIGHT SIDE: Register Maps
    QGroupBox *grpMaps = new QGroupBox("地址映射表 (点击行自动填入地址)");
    QVBoxLayout *layMaps = new QVBoxLayout();
    layMaps->addWidget(tabRegisterMaps);
    grpMaps->setLayout(layMaps);
    
    // Main Layout Assembly
    mainPageLayout->addWidget(leftWidget, 2);
    mainPageLayout->addWidget(grpMaps, 1);

    return page;
}

QWidget* MainWindow::createSerialPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // 1. Settings
    QGroupBox *grpSettings = new QGroupBox("串口设置");
    QGridLayout *laySet = new QGridLayout();
    laySet->addWidget(lblSerialPort, 0, 0); laySet->addWidget(cmbSerialPort, 0, 1); laySet->addWidget(btnRefreshPorts, 0, 2);
    laySet->addWidget(lblBaudRate, 0, 3); laySet->addWidget(cmbBaudRate, 0, 4);
    laySet->addWidget(lblDataBits, 1, 0); laySet->addWidget(cmbDataBits, 1, 1);
    laySet->addWidget(lblParity, 1, 2); laySet->addWidget(cmbParity, 1, 3);
    laySet->addWidget(lblStopBits, 1, 4); laySet->addWidget(cmbStopBits, 1, 5);
    
    QHBoxLayout *layActs = new QHBoxLayout();
    layActs->addWidget(btnSerialOpen);
    layActs->addWidget(btnSerialClose);
    layActs->addWidget(lblSerialStatus);
    layActs->addStretch();
    laySet->addLayout(layActs, 2, 0, 1, 6);
    
    grpSettings->setLayout(laySet);
    layout->addWidget(grpSettings);
    
    // 2. Data Area
    QGroupBox *grpData = new QGroupBox("数据收发");
    QVBoxLayout *layData = new QVBoxLayout();
    
    QHBoxLayout *layOpts = new QHBoxLayout();
    layOpts->addWidget(chkHexDisplay);
    layOpts->addWidget(btnSerialClearRecv);
    layOpts->addStretch();
    layData->addLayout(layOpts);
    
    layData->addWidget(new QLabel("接收区:"));
    layData->addWidget(txtSerialRecv);
    
    layData->addWidget(new QLabel("发送区:"));
    layData->addWidget(txtSerialSend);
    
    QHBoxLayout *laySend = new QHBoxLayout();
    laySend->addWidget(chkHexSend);
    laySend->addWidget(btnSerialSend);
    laySend->addStretch();
    layData->addLayout(laySend);
    
    grpData->setLayout(layData);
    layout->addWidget(grpData);
    
    layout->setStretch(0, 0);
    layout->setStretch(1, 1);
    
    return page;
}

QWidget* MainWindow::createGitPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    
    // 1. Repository Selection
    QGroupBox *grpRepo = new QGroupBox("Git 仓库 (记忆路径)");
    QHBoxLayout *layRepo = new QHBoxLayout();
    layRepo->addWidget(new QLabel("路径:"));
    
    // cmbGitDir is already initialized in createWidgets()
    loadGitHistory(); // Load saved paths
    
    layRepo->addWidget(cmbGitDir, 1);
    layRepo->addWidget(btnGitSelectDir);
    grpRepo->setLayout(layRepo);
    layout->addWidget(grpRepo);
    
    // 2. Branch & Actions
    QGroupBox *grpOps = new QGroupBox("Git 操作");
    QVBoxLayout *layOps = new QVBoxLayout();
    
    // Branch Selection
    QHBoxLayout *layBranch = new QHBoxLayout();
    layBranch->addWidget(new QLabel("当前/目标分支:"));
    layBranch->addWidget(cmbGitBranches, 1);
    layBranch->addWidget(btnGitRefreshBranches);
    layBranch->addWidget(btnGitCheckout);
    layOps->addLayout(layBranch);
    
    // Commit Msg
    QHBoxLayout *layCommit = new QHBoxLayout();
    layCommit->addWidget(new QLabel("提交信息:"));
    layCommit->addWidget(txtGitCommitMsg, 1);
    layOps->addLayout(layCommit);
    
    // Buttons Grid
    QGridLayout *layBtns = new QGridLayout();
    layBtns->addWidget(btnGitAdd, 0, 0); layBtns->addWidget(btnGitCommit, 0, 1); layBtns->addWidget(btnGitStatus, 0, 2);
    layBtns->addWidget(btnGitPush, 1, 0); layBtns->addWidget(btnGitPull, 1, 1); layBtns->addWidget(btnGitMerge, 1, 2);
    layOps->addLayout(layBtns);
    
    // History Section
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layOps->addWidget(line);
    
    QHBoxLayout *layHist = new QHBoxLayout();
    layHist->addWidget(new QLabel("版本历史:"));
    layHist->addWidget(cmbGitHistory, 1);
    layHist->addWidget(btnGitRefreshLog);
    layHist->addWidget(btnGitReset);
    layOps->addLayout(layHist);
    
    grpOps->setLayout(layOps);
    layout->addWidget(grpOps);
    
    // 3. Log Output
    QGroupBox *grpLog = new QGroupBox("Git 输出");
    QVBoxLayout *layLog = new QVBoxLayout();
    layLog->addWidget(txtGitLog);
    grpLog->setLayout(layLog);
    layout->addWidget(grpLog);
    
    layout->setStretch(0, 0);
    layout->setStretch(1, 0);
    layout->setStretch(2, 1);
    
    return page;
}

void MainWindow::createLayouts()
{
    centralWidget = new QWidget();
    setCentralWidget(centralWidget);
    
    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);
    
    stackedWidget = new QStackedWidget();
    modbusPageWidget = createModbusPage();
    serialPageWidget = createSerialPage();
    gitPageWidget = createGitPage();
    
    stackedWidget->addWidget(modbusPageWidget);
    stackedWidget->addWidget(serialPageWidget);
    stackedWidget->addWidget(gitPageWidget);
    
    mainLayout->addWidget(navWidget);
    mainLayout->addWidget(stackedWidget);
    
    // Set stretch, allow stack to expand
    mainLayout->setStretch(0, 0);
    mainLayout->setStretch(1, 1);
}

void MainWindow::createConnections()
{
    // Navigation
    connect(navWidget, &QListWidget::currentItemChanged, this, &MainWindow::onNavSelectionChanged);

    // Modbus Connections
    connect(btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(tcpSocket, &QTcpSocket::connected, this, &MainWindow::onSocketConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &MainWindow::onSocketDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &MainWindow::onSocketReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &MainWindow::onSocketError);
#else
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error), this, &MainWindow::onSocketError);
#endif

    connect(continuousTimer, &QTimer::timeout, this, &MainWindow::onContinuousReadTimer);
    connect(chkContinuousRead, &QCheckBox::toggled, this, &MainWindow::onContinuousReadToggled);

    // Register Maps
    connect(tblAGV, &QTableWidget::cellClicked, this, &MainWindow::onRegisterTableCellClicked);
    connect(tblRobot, &QTableWidget::cellClicked, this, &MainWindow::onRegisterTableCellClicked);
    connect(tblAGV, &QTableWidget::cellChanged, this, &MainWindow::onRegisterTableChanged);
    connect(tblRobot, &QTableWidget::cellChanged, this, &MainWindow::onRegisterTableChanged);
    connect(tabRegisterMaps, &QTabWidget::currentChanged, this, &MainWindow::onRegisterTabChanged);

    connect(btnReadCoils, &QPushButton::clicked, this, &MainWindow::onReadCoilsClicked);
    connect(btnReadInputs, &QPushButton::clicked, this, &MainWindow::onReadInputsClicked);
    connect(btnReadHoldingRegisters, &QPushButton::clicked, this, &MainWindow::onReadHoldingRegistersClicked);
    connect(btnReadInputRegisters, &QPushButton::clicked, this, &MainWindow::onReadInputRegistersClicked);

    connect(btnWriteSingleCoil, &QPushButton::clicked, this, &MainWindow::onWriteSingleCoilClicked);
    connect(btnWriteSingleRegister, &QPushButton::clicked, this, &MainWindow::onWriteSingleRegisterClicked);
    connect(btnWriteMultipleCoils, &QPushButton::clicked, this, &MainWindow::onWriteMultipleCoilsClicked);
    connect(btnWriteMultipleRegisters, &QPushButton::clicked, this, &MainWindow::onWriteMultipleRegistersClicked);

    connect(cmbDisplayFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDisplayFormatChanged);
    connect(btnClearLog, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);

    // Serial Connections
    connect(btnRefreshPorts, &QPushButton::clicked, this, &MainWindow::refreshSerialPorts);
    connect(btnSerialOpen, &QPushButton::clicked, this, &MainWindow::onSerialOpenClicked);
    connect(btnSerialClose, &QPushButton::clicked, this, &MainWindow::onSerialCloseClicked);
    connect(serialPort, &QSerialPort::readyRead, this, &MainWindow::onSerialRead);
    connect(btnSerialSend, &QPushButton::clicked, this, &MainWindow::onSerialSendClicked);
    connect(btnSerialClearRecv, &QPushButton::clicked, [this](){ txtSerialRecv->clear(); });

    // Git Connections
    connect(btnGitSelectDir, &QPushButton::clicked, this, &MainWindow::onGitSelectDirClicked);
    connect(btnGitRefreshBranches, &QPushButton::clicked, this, &MainWindow::onGitRefreshBranchesClicked);
    connect(btnGitCheckout, &QPushButton::clicked, this, &MainWindow::onGitCheckoutClicked);
    connect(btnGitAdd, &QPushButton::clicked, this, &MainWindow::onGitAddClicked);
    connect(btnGitCommit, &QPushButton::clicked, this, &MainWindow::onGitCommitClicked);
    connect(btnGitPush, &QPushButton::clicked, this, &MainWindow::onGitPushClicked);
    connect(btnGitPull, &QPushButton::clicked, this, &MainWindow::onGitPullClicked);
    connect(btnGitMerge, &QPushButton::clicked, this, &MainWindow::onGitMergeClicked);
    connect(btnGitStatus, &QPushButton::clicked, this, &MainWindow::onGitStatusClicked);
    connect(btnGitRefreshLog, &QPushButton::clicked, this, &MainWindow::onGitRefreshLogClicked);
    connect(btnGitReset, &QPushButton::clicked, this, &MainWindow::onGitResetClicked);
}

// --- Navigation Logic ---

void MainWindow::onNavSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current) return;
    int index = navWidget->row(current);
    stackedWidget->setCurrentIndex(index);
}

// --- Modbus Logic ---

void MainWindow::onConnectClicked()
{
    QString ip = cmbIP->currentText().trimmed();
    int port = txtPort->text().toInt();

    if (ip.isEmpty() || port <= 0) {
        QMessageBox::warning(this, "警告", "无效的IP或端口");
        return;
    }

    if (tcpSocket->state() == QAbstractSocket::ConnectedState)
        tcpSocket->disconnectFromHost();

    tcpSocket->connectToHost(ip, port);
    logMessage(QString("正在连接 %1:%2...").arg(ip).arg(port));
}

void MainWindow::onDisconnectClicked()
{
    if (tcpSocket->state() != QAbstractSocket::UnconnectedState) {
        tcpSocket->disconnectFromHost();
    }
}

void MainWindow::onSocketConnected()
{
    updateConnectionStatus(true);
    logMessage("已连接");
    saveConnectionHistory(cmbIP->currentText().trimmed());
}

void MainWindow::onSocketDisconnected()
{
    updateConnectionStatus(false);
    continuousTimer->stop();
    logMessage("已断开");
}

void MainWindow::onSocketError(QAbstractSocket::SocketError error)
{
    updateConnectionStatus(false);
    continuousTimer->stop();
    logMessage(QString("Socket错误: %1").arg(tcpSocket->errorString()), true);
}

void MainWindow::sendModbusRequest(quint8 functionCode, quint16 startAddress, quint16 quantity, const QVector<quint16> &values)
{
    if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
        logMessage("未连接!", true);
        return;
    }

    QByteArray request;
    QDataStream stream(&request, QIODevice::WriteOnly);
    
    transactionId++;
    stream << transactionId;
    stream << quint16(0); // Protocol ID
    
    int length = 6;
    int byteCount = 0;
    
    if (functionCode == 15 || functionCode == 16) {
        if (functionCode == 15) {
            byteCount = (quantity + 7) / 8;
        } else {
            byteCount = quantity * 2;
        }
        length = 7 + byteCount; 
    }
    
    stream << quint16(length);
    stream << quint8(txtSlaveID->text().toInt());
    stream << functionCode;
    stream << startAddress;
    
    if (functionCode <= 4) {
        stream << quantity;
    } else if (functionCode == 5 || functionCode == 6) {
        if (values.size() > 0) stream << values[0];
    } else if (functionCode == 15) {
        stream << quantity;
        stream << quint8(byteCount);
        // Pack bits
        quint8 currentByte = 0;
        for (int i=0; i<values.size(); i++) {
            if (values[i] > 0) currentByte |= (1 << (i % 8));
            if ((i + 1) % 8 == 0 || i == values.size() - 1) {
                stream << currentByte;
                currentByte = 0;
            }
        }
    } else if (functionCode == 16) {
        stream << quantity;
        stream << quint8(byteCount);
        for (quint16 v : values) stream << v;
    }

    tcpSocket->write(request);
}

void MainWindow::onSocketReadyRead()
{
    while (tcpSocket->bytesAvailable() >= 9) { // Header + Function + ByteCount
        QByteArray header = tcpSocket->peek(7);
        QDataStream stream(header);
        quint16 transId, protoId, len;
        quint8 unitId;
        stream >> transId >> protoId >> len >> unitId;
        
        if (tcpSocket->bytesAvailable() < 6 + len) return; // Wait full struct
        
        QByteArray packet = tcpSocket->read(6 + len);
        parseModbusResponse(packet);
    }
}

void MainWindow::parseModbusResponse(const QByteArray &response)
{
    QDataStream stream(response);
    quint16 tId, pId, len;
    quint8 uId, funcCode;
    stream >> tId >> pId >> len >> uId >> funcCode;
    
    if (funcCode & 0x80) {
        quint8 errorCode;
        stream >> errorCode;
        logMessage(QString("Modbus异常: Code %1").arg(errorCode), true);
        return;
    }
    
    QString resultStr;
    
    if (funcCode == 1 || funcCode == 2) {
        quint8 byteCount;
        stream >> byteCount;
        QStringList bits;
        // Simple bit parsing, not perfect for all edge cases but functional
        int totalQuantity = currentReadParams.quantity;
        for (int i=0; i<byteCount; ++i) {
            quint8 byteVal;
            stream >> byteVal;
            for (int b=0; b<8; ++b) {
                if (bits.size() < totalQuantity)
                    bits << ((byteVal >> b) & 1 ? "1" : "0");
            }
        }
        resultStr = bits.join(", ");
    } 
    else if (funcCode == 3 || funcCode == 4) {
        quint8 byteCount;
        stream >> byteCount;
        QStringList regs;
        for (int i=0; i<byteCount/2; ++i) {
            quint16 val;
            stream >> val;
            regs << formatValue(val);
        }
        resultStr = regs.join(", ");
    }
    else {
        resultStr = "写入成功";
    }
    
    txtResult->setText(QDateTime::currentDateTime().toString("[HH:mm:ss] ") + resultStr);
    
    if (!chkContinuousRead->isChecked())
        logMessage("收到响应: " + resultStr);
}

void MainWindow::onReadCoilsClicked() {
    currentReadParams = {1, (quint16)spinReadStartAddr->value(), (quint16)spinReadQuantity->value()};
    sendModbusRequest(1, currentReadParams.startAddress, currentReadParams.quantity);
}
void MainWindow::onReadInputsClicked() {
    currentReadParams = {2, (quint16)spinReadStartAddr->value(), (quint16)spinReadQuantity->value()};
    sendModbusRequest(2, currentReadParams.startAddress, currentReadParams.quantity);
}
void MainWindow::onReadHoldingRegistersClicked() {
    currentReadParams = {3, (quint16)spinReadStartAddr->value(), (quint16)spinReadQuantity->value()};
    sendModbusRequest(3, currentReadParams.startAddress, currentReadParams.quantity);
}
void MainWindow::onReadInputRegistersClicked() {
    currentReadParams = {4, (quint16)spinReadStartAddr->value(), (quint16)spinReadQuantity->value()};
    sendModbusRequest(4, currentReadParams.startAddress, currentReadParams.quantity);
}

void MainWindow::onWriteSingleCoilClicked() {
    QVector<quint16> v; v << (chkWriteCoil->isChecked() ? 0xFF00 : 0x0000);
    sendModbusRequest(5, spinWriteStartAddr->value(), 1, v);
}
void MainWindow::onWriteSingleRegisterClicked() {
    QVector<quint16> v; v << spinWriteValue->value();
    sendModbusRequest(6, spinWriteStartAddr->value(), 1, v);
}
void MainWindow::onWriteMultipleCoilsClicked() {
    // Basic CSV parsing needed here, simplifying for brevity
     QStringList items = txtWriteValues->text().split(',');
     QVector<quint16> vals;
     for(auto s : items) vals << (s.toInt() > 0 ? 1 : 0);
     sendModbusRequest(15, spinWriteStartAddr->value(), vals.size(), vals);
}
void MainWindow::onWriteMultipleRegistersClicked() {
     QStringList items = txtWriteValues->text().split(',');
     QVector<quint16> vals;
     for(auto s : items) vals << s.toUShort();
     sendModbusRequest(16, spinWriteStartAddr->value(), vals.size(), vals);
}

void MainWindow::onContinuousReadToggled(bool checked) {
    spinReadInterval->setEnabled(checked);
    if (checked) {
        if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
            continuousTimer->start(spinReadInterval->value());
        } else {
            // If not connected, uncheck and warn
            // Use blockSignals to avoid recursion if we want
            chkContinuousRead->blockSignals(true);
            chkContinuousRead->setChecked(false);
            chkContinuousRead->blockSignals(false);
            spinReadInterval->setEnabled(false);
            QMessageBox::warning(this, "警告", "请先连接Modbus服务器");
        }
    } else {
        continuousTimer->stop();
    }
}

void MainWindow::onContinuousReadTimer() {
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        sendModbusRequest(currentReadParams.functionCode, currentReadParams.startAddress, currentReadParams.quantity);
    }
}

void MainWindow::onDisplayFormatChanged(int index) {
    displayFormat = (DisplayFormat)index;
}

void MainWindow::logMessage(const QString &msg, bool isError) {
    QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString fullMsg = QString("[%1] %2").arg(time, msg);
    
    if (isError) {
        txtLog->append(QString("<font color='red'>%1</font>").arg(fullMsg));
    } else {
        txtLog->append(fullMsg);
    }
}

void MainWindow::updateConnectionStatus(bool connected) {
    if (connected) {
        lblStatusText->setText("已连接");
        lblStatusText->setStyleSheet("color: green; font-weight: bold;");
        btnConnect->setEnabled(false);
        btnDisconnect->setEnabled(true);
        // Start continuous if checked
        if (chkContinuousRead->isChecked()) continuousTimer->start(spinReadInterval->value());
    } else {
        lblStatusText->setText("未连接");
        lblStatusText->setStyleSheet("color: red; font-weight: bold;");
        btnConnect->setEnabled(true);
        btnDisconnect->setEnabled(false);
    }
}

QString MainWindow::formatValue(quint16 value, bool isBit) const {
    if (isBit) return value ? "1" : "0";
    if (displayFormat == FormatHex) return QString("0x%1").arg(value, 4, 16, QChar('0')).toUpper();
    if (displayFormat == FormatBinary) return QString("0b%1").arg(value, 16, 2, QChar('0'));
    return QString::number(value);
}

void MainWindow::onClearLogClicked() {
    txtLog->clear();
}

// --- Connection History Logic ---
void MainWindow::saveConnectionHistory(const QString &ip) {
    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("ConnectionHistory").toStringList();
    
    history.removeAll(ip);
    history.prepend(ip);
    while (history.size() > MAX_HISTORY) history.removeLast();
    
    settings.setValue("ConnectionHistory", history);
    
    cmbIP->blockSignals(true);
    cmbIP->clear();
    cmbIP->addItems(history);
    cmbIP->setCurrentText(ip);
    cmbIP->blockSignals(false);
}

void MainWindow::loadConnectionHistory() {
    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("ConnectionHistory").toStringList();
    if (history.isEmpty()) history << "192.168.1.13";
    
    cmbIP->blockSignals(true);
    cmbIP->addItems(history);
    cmbIP->setCurrentIndex(0);
    cmbIP->blockSignals(false);
}

// --- Serial Port Logic ---

void MainWindow::refreshSerialPorts() {
    cmbSerialPort->clear();
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        cmbSerialPort->addItem(info.portName());
    }
}

void MainWindow::onSerialOpenClicked() {
    if (cmbSerialPort->count() == 0) return;
    
    serialPort->setPortName(cmbSerialPort->currentText());
    serialPort->setBaudRate(cmbBaudRate->currentText().toInt());
    
    // Data bits
    QString db = cmbDataBits->currentText();
    if (db == "8") serialPort->setDataBits(QSerialPort::Data8);
    else if (db == "7") serialPort->setDataBits(QSerialPort::Data7);
    else if (db == "6") serialPort->setDataBits(QSerialPort::Data6);
    else if (db == "5") serialPort->setDataBits(QSerialPort::Data5);
    
    // Parity
    QString par = cmbParity->currentText();
    if (par == "None") serialPort->setParity(QSerialPort::NoParity);
    else if (par == "Even") serialPort->setParity(QSerialPort::EvenParity);
    else if (par == "Odd") serialPort->setParity(QSerialPort::OddParity);
    else if (par == "Space") serialPort->setParity(QSerialPort::SpaceParity);
    else if (par == "Mark") serialPort->setParity(QSerialPort::MarkParity);
    
    // Stop bits
    QString sb = cmbStopBits->currentText();
    if (sb == "1") serialPort->setStopBits(QSerialPort::OneStop);
    else if (sb == "1.5") serialPort->setStopBits(QSerialPort::OneAndHalfStop);
    else if (sb == "2") serialPort->setStopBits(QSerialPort::TwoStop);
    
    if (serialPort->open(QIODevice::ReadWrite)) {
        updateSerialStatus(true);
    } else {
        QMessageBox::critical(this, "错误", "无法打开串口:\n" + serialPort->errorString());
    }
}

void MainWindow::onSerialCloseClicked() {
    if (serialPort->isOpen()) serialPort->close();
    updateSerialStatus(false);
}

void MainWindow::updateSerialStatus(bool connected) {
    if (connected) {
        lblSerialStatus->setText("串口已打开");
        lblSerialStatus->setStyleSheet("color: green; font-weight: bold;");
        btnSerialOpen->setEnabled(false);
        btnSerialClose->setEnabled(true);
        cmbSerialPort->setEnabled(false);
        cmbBaudRate->setEnabled(false);
    } else {
        lblSerialStatus->setText("串口关闭");
        lblSerialStatus->setStyleSheet("color: red; font-weight: bold;");
        btnSerialOpen->setEnabled(true);
        btnSerialClose->setEnabled(false);
        cmbSerialPort->setEnabled(true);
        cmbBaudRate->setEnabled(true);
    }
}

void MainWindow::onSerialRead() {
    QByteArray data = serialPort->readAll();
    QString display;
    if (chkHexDisplay->isChecked()) {
        display = data.toHex(' ').toUpper();
    } else {
        display = QString::fromLocal8Bit(data);
    }
    txtSerialRecv->moveCursor(QTextCursor::End);
    txtSerialRecv->insertPlainText(display);
    txtSerialRecv->moveCursor(QTextCursor::End);
}

void MainWindow::onSerialSendClicked() {
    if (!serialPort->isOpen()) return;
    
    QString text = txtSerialSend->toPlainText();
    QByteArray data;
    
    if (chkHexSend->isChecked()) {
        text = text.remove(' ');
        data = QByteArray::fromHex(text.toLatin1());
    } else {
        data = text.toLocal8Bit();
    }
    
    serialPort->write(data);
}

// --- Git Logic ---

void MainWindow::onGitSelectDirClicked() {
    QString initialDir = cmbGitDir->currentText();
    QString dir = QFileDialog::getExistingDirectory(this, "选择Git仓库", initialDir.isEmpty() ? QDir::homePath() : initialDir);
    if (!dir.isEmpty()) {
        cmbGitDir->setCurrentText(dir);
        saveGitHistory(dir);
        runGitCommand(QStringList() << "status"); // Check status which will verify it's a git repo
        onGitRefreshBranchesClicked(); // Auto refresh
        onGitRefreshLogClicked();
    }
}

void MainWindow::runGitCommand(const QStringList &args) {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先选择Git仓库目录!</font>");
        return;
    }

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProgram("git");
    process.setArguments(args);
    
    txtGitLog->append(QString("<font color='cyan'>$ git %1</font>").arg(args.join(" ")));
    
    process.start();
    if (!process.waitForStarted()) {
         txtGitLog->append("<font color='red'>错误: 无法启动git命令，请检查是否安装了git</font>");
         return;
    }
    
    // Default wait 30s
    if (!process.waitForFinished(30000)) {
         txtGitLog->append("<font color='red'>部分超时或后台运行...</font>");
    }
    
    QByteArray stdoutData = process.readAllStandardOutput();
    QByteArray stderrData = process.readAllStandardError();
    
    if (!stdoutData.isEmpty()) {
        txtGitLog->append(QString::fromLocal8Bit(stdoutData));
    }
    if (!stderrData.isEmpty()) {
        txtGitLog->append(QString("<font color='orange'>%1</font>").arg(QString::fromLocal8Bit(stderrData)));
    }
    
    txtGitLog->moveCursor(QTextCursor::End);
}

void MainWindow::onGitRefreshBranchesClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;
    
    // Get local branches
    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start("git", QStringList() << "branch");
    process.waitForFinished();
    
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    
    cmbGitBranches->clear();
    QString currentBranch;
    
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith("* ")) {
            currentBranch = line.mid(2);
            cmbGitBranches->addItem(currentBranch);
        } else {
            cmbGitBranches->addItem(line);
        }
    }
    
    if (!currentBranch.isEmpty())
        cmbGitBranches->setCurrentText(currentBranch);
        
    txtGitLog->append("已刷新分支列表");
}

void MainWindow::onGitCheckoutClicked() {
    QString branch = cmbGitBranches->currentText().trimmed();
    if (branch.isEmpty()) {
       txtGitLog->append("<font color='red'>错误: 请先选择要切换的分支</font>");
       return;
    }
    runGitCommand(QStringList() << "checkout" << branch);
    // Refresh to show updated status (though current logic doesn't mark active branch in combobox explicitly other than selection)
    onGitRefreshBranchesClicked();
    onGitRefreshLogClicked();
}

void MainWindow::onGitAddClicked() {
    runGitCommand(QStringList() << "add" << ".");
}

void MainWindow::onGitCommitClicked() {
    QString msg = txtGitCommitMsg->text().trimmed();
    if (msg.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入提交信息");
        return;
    }
    runGitCommand(QStringList() << "commit" << "-m" << msg);
    txtGitCommitMsg->clear();
}

void MainWindow::onGitPushClicked() {
     // Push current branch
     QString branch = cmbGitBranches->currentText();
     if (branch.isEmpty()) branch = "master"; // fallback
     runGitCommand(QStringList() << "push" << "origin" << branch);
}

void MainWindow::onGitPullClicked() {
    QString branch = cmbGitBranches->currentText();
    if (branch.isEmpty()) branch = "master";
    runGitCommand(QStringList() << "pull" << "origin" << branch);
}

void MainWindow::onGitMergeClicked() {
    QString branch = cmbGitBranches->currentText();
    QString currentBranch; 
    
    // Detect current branch again to identify if we are merging "buffer" into "current"
    // Usually user selects the branch they want to merge INTO current branch
    // Or maybe selects the target branch? 
    // Simplified logic: Merge the selected combo box branch into current checked out branch??
    // BUT typical workflow: checkout target, then merge source. 
    // Let's assume user is on correct branch, and selects "branch to merge from" in combo?
    // Or maybe we ask user?
    
    // Let's implement: Merge selected branch in ComboBox INTO current active branch
    if (branch.isEmpty()) return;
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认合并", QString("确定要将分支 '%1' 合并到当前分支吗?").arg(branch),
                                  QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        runGitCommand(QStringList() << "merge" << branch);
    }
}

void MainWindow::onGitStatusClicked() {
    runGitCommand(QStringList() << "status");
}

void MainWindow::onGitRefreshLogClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;
    
    QProcess process;
    process.setWorkingDirectory(workDir);
    // Get short hash, relative time, Subject, Author name
    process.start("git", QStringList() << "log" << "--pretty=format:%h - %cd : %s (%an)" << "--date=short" << "-n" << "20");
    process.waitForFinished();
    
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    
    cmbGitHistory->clear();
    for (const QString &line : lines) {
        cmbGitHistory->addItem(line.trimmed());
    }
    
    txtGitLog->append("已刷新历史版本 (Git Log)");
}

void MainWindow::onGitResetClicked() {
    QString selected = cmbGitHistory->currentText();
    if (selected.isEmpty()) return;
    
    // Extract Hash (first part before " - ")
    QString hash = selected.split(" - ").first();
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "危险操作!", 
                                  QString("确定要将工作区和暂存区以及HEAD全部重置 (Hard Reset) 到版本 %1 吗?\n此操作不可撤销，会丢失当前未提交的修改!").arg(hash),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        runGitCommand(QStringList() << "reset" << "--hard" << hash);
    }
}

void MainWindow::saveGitHistory(const QString &dir) {
    if (dir.isEmpty()) return;

    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();

    history.removeAll(dir);
    history.prepend(dir);

    while (history.size() > MAX_HISTORY) {
        history.removeLast();
    }

    settings.setValue("GitHistory", history);
    
    // Update UI
    cmbGitDir->blockSignals(true);
    cmbGitDir->clear();
    cmbGitDir->addItems(history);
    cmbGitDir->setCurrentText(dir);
    cmbGitDir->blockSignals(false);
}

void MainWindow::loadGitHistory() {
    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();
    
    cmbGitDir->blockSignals(true);
    cmbGitDir->clear();
    cmbGitDir->addItems(history);
    if (!history.isEmpty()) {
        cmbGitDir->setCurrentIndex(0);
    }
    cmbGitDir->blockSignals(false);
}

void MainWindow::setupRegisterTable(QTableWidget *table) {
    if(!table) return;
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(QStringList() << "地址" << "注释");
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    
    // Add some default rows for testing
    table->setRowCount(50);
    for(int i=0; i<50; i++) {
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        table->setItem(i, 1, new QTableWidgetItem(""));
    }
}

void MainWindow::onRegisterTableCellClicked(int row, int column) {
    Q_UNUSED(column);
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if(!table) return;
    
    QTableWidgetItem *item = table->item(row, 0); // Address is col 0
    if(item) {
        bool ok;
        int addr = item->text().toInt(&ok);
        if(ok) {
            spinReadStartAddr->setValue(addr);
            spinWriteStartAddr->setValue(addr);
        }
    }
}

void MainWindow::onRegisterTabChanged(int index) {
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->disconnectFromHost();
        // Wait briefly or just rely on state change? 
        // Force immediate update of UI if needed but slots handle it.
    }

    if (index == 0) { // AGV
        cmbIP->setCurrentText("192.168.1.88");
        onConnectClicked();
    } else if (index == 1) { // Robot
        cmbIP->setCurrentText("192.168.1.13");
        onConnectClicked();
    }
}

void MainWindow::onRegisterTableChanged(int row, int column) {
    Q_UNUSED(row);
    Q_UNUSED(column);
    // Auto-save logic could go here if we want instant save
    // For now we rely on saveRegisterTables() called in destructor
    // But user asked for memory function, maybe they expect it to persist?
    // Let's implement saveRegisterTables and call it here if we want robustness
    // Considering performance, maybe not on every keystroke/cell edit?
    // Let's do nothing here and rely on destructor, or implementing a delayed save
}

void MainWindow::saveRegisterTables() {
    QSettings settings("LiChenYang", "LinuxHelper");
    
    auto saveTable = [&](QTableWidget* table, QString keyPrefix) {
        int rows = table->rowCount();
        settings.beginWriteArray(keyPrefix);
        for (int i = 0; i < rows; ++i) {
            settings.setArrayIndex(i);
            QTableWidgetItem *addrItem = table->item(i, 0);
            QTableWidgetItem *cmtItem = table->item(i, 1);
            settings.setValue("addr", addrItem ? addrItem->text() : "");
            settings.setValue("cmt", cmtItem ? cmtItem->text() : "");
        }
        settings.endArray();
    };

    saveTable(tblAGV, "Map_AGV");
    saveTable(tblRobot, "Map_Robot");
}

void MainWindow::loadRegisterTables() {
    QSettings settings("LiChenYang", "LinuxHelper");
    
    auto loadTable = [&](QTableWidget* table, QString keyPrefix) {
        int size = settings.beginReadArray(keyPrefix);
        if (size > 0) {
            // Adjust row count if needed, or keep fixed 50?
            // User might want dynamic, but let's stick to max(50, size)
            if (size > table->rowCount()) table->setRowCount(size);
            
            for (int i = 0; i < size; ++i) {
                settings.setArrayIndex(i);
                QString addr = settings.value("addr").toString();
                QString cmt = settings.value("cmt").toString();
                
                if (!table->item(i, 0)) table->setItem(i, 0, new QTableWidgetItem());
                if (!table->item(i, 1)) table->setItem(i, 1, new QTableWidgetItem());
                
                table->item(i, 0)->setText(addr);
                table->item(i, 1)->setText(cmt);
            }
        }
        settings.endArray();
    };

    loadTable(tblAGV, "Map_AGV");
    loadTable(tblRobot, "Map_Robot");
}
