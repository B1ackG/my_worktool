#include "mainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QDataStream>
#include <QDebug>
#include <QIntValidator>
#include <QRandomGenerator>
#include <bitset>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tcpSocket(nullptr)
    , continuousTimer(nullptr)
    , transactionId(0)
    , displayFormat(FormatDecimal)
    , simMainDevice(nullptr)
    , simAGVDevice(nullptr)
    , serialPort(nullptr)
{
    setWindowTitle("李晨阳的linux工作助手");
    resize(1200, 720);

    // Initialize Objects
    tcpSocket = new QTcpSocket(this);
    continuousTimer = new QTimer(this);
    serialPort = new QSerialPort(this);
    simMainDevice = new ModbusSlave(this);
    simAGVDevice = new ModbusSlave(this);

    // Create UI
    createWidgets();
    createLayouts();
    createConnections();
    
    // Load History
    loadConnectionHistory();
    loadGitHistory();
    loadRegisterTables();
    syncSimulatorTablesFromMaps();

    connect(simMainDevice, &ModbusSlave::clientConnected, this, [this](){
        int count = simMainDevice->clientCount();
        lblSimMainStatus->setText(QString("在线(%1)").arg(count));
        lblSimMainStatus->setStyleSheet("color: green; font-weight: bold;"); 
    });
    connect(simMainDevice, &ModbusSlave::clientDisconnected, this, [this](){
        int count = simMainDevice->clientCount();
        if (count > 0) {
            lblSimMainStatus->setText(QString("在线(%1)").arg(count));
        } else {
            lblSimMainStatus->setText("离线");
            lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
        }
    });

    connect(simAGVDevice, &ModbusSlave::clientConnected, this, [this](){
        int count = simAGVDevice->clientCount();
        lblSimAGVStatus->setText(QString("在线(%1)").arg(count));
        lblSimAGVStatus->setStyleSheet("color: green; font-weight: bold;");
    });
    connect(simAGVDevice, &ModbusSlave::clientDisconnected, this, [this](){
        int count = simAGVDevice->clientCount();
        if (count > 0) {
            lblSimAGVStatus->setText(QString("在线(%1)").arg(count));
        } else {
            lblSimAGVStatus->setText("离线");
            lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
        }
    });

    // Preload initial register values per spec
    // 主设备 (5020)
    simMainDevice->setRegister(201, 0x427B);
    simMainDevice->setRegister(202, 0xAD49);
    simMainDevice->setRegister(203, 0x44FA);
    simMainDevice->setRegister(204, 0x0000);
    simMainDevice->setRegister(804, 0);
    simMainDevice->setRegister(805, 0);
    simMainDevice->setRegister(403, 0);
    simMainDevice->setRegister(29, 0);
    simMainDevice->setRegister(400, 0);
    simMainDevice->setRegister(401, 0);
    simMainDevice->setRegister(404, 0);
    for (int a = 500; a <= 503; ++a) simMainDevice->setRegister(a, 0);

    // AGV (5021)
    simAGVDevice->setRegister(0, 64);
    simAGVDevice->setRegister(2, 0);
    simAGVDevice->setRegister(3, 0);
    simAGVDevice->setRegister(4, 0);
    simAGVDevice->setRegister(50, 0x0000);
    simAGVDevice->setRegister(104, 0);

    // Do not auto-start servers; user can configure bind IP/ports and start them
    txtSimLog->append(QString("模拟器已初始化（请配置端口并启动主设备/AGV）"));

    // Wire simulator UI buttons
    connect(btnSimStartMain, &QPushButton::clicked, this, &MainWindow::onStartSimulatorClicked);
    connect(btnSimStopMain, &QPushButton::clicked, this, &MainWindow::onStopSimulatorClicked);
    connect(btnSimStartAGV, &QPushButton::clicked, this, &MainWindow::onStartSimulatorClicked);
    connect(btnSimStopAGV, &QPushButton::clicked, this, &MainWindow::onStopSimulatorClicked);
    connect(btnSimSaveScene, &QPushButton::clicked, this, &MainWindow::onSimSaveSceneClicked);
    connect(btnSimLoadScene, &QPushButton::clicked, this, &MainWindow::onSimLoadSceneClicked);
    connect(btnSimRunScript, &QPushButton::clicked, this, &MainWindow::onSimRunScriptClicked);
    connect(btnSimStopScript, &QPushButton::clicked, this, &MainWindow::onSimStopScriptClicked);
    connect(simMainDevice, &ModbusSlave::registerOperation, this, &MainWindow::onRegisterOperation);
    connect(simAGVDevice, &ModbusSlave::registerOperation, this, &MainWindow::onRegisterOperation);

    // initial buttons: start enabled, stop disabled
    btnSimStartMain->setEnabled(true);
    btnSimStopMain->setEnabled(false);
    lblSimMainStatus->setText("离线"); lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
    btnSimStartAGV->setEnabled(true);
    btnSimStopAGV->setEnabled(false);
    lblSimAGVStatus->setText("离线"); lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
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
    navWidget->addItem("Modbus 从站模拟器");
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
    
    cmbGitRemote = new QComboBox();
    cmbGitRemote->addItem("origin");
    cmbGitRemote->addItem("github");
    cmbGitRemote->setEditable(true); // Allow custom remotes
    
    btnGitAdd = new QPushButton("git add .");
    btnGitCommit = new QPushButton("git commit");
    btnGitPush = new QPushButton("git push");
    btnGitPull = new QPushButton("git pull");
    btnGitMerge = new QPushButton("git merge");
    btnGitStatus = new QPushButton("git status");
    btnGitOpenIgnore = new QPushButton("管理 .gitignore");
    btnGitCheckIgnore = new QPushButton("检查 .gitignore");
    btnGitCheckIgnore->setToolTip("检查是否存在常用的 .gitignore 规则");
    
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
    layBtns->addWidget(new QLabel("远程仓库:"), 0, 0); layBtns->addWidget(cmbGitRemote, 0, 1, 1, 2);
    layBtns->addWidget(btnGitAdd, 1, 0); layBtns->addWidget(btnGitCommit, 1, 1); layBtns->addWidget(btnGitStatus, 1, 2);
    layBtns->addWidget(btnGitPush, 2, 0); layBtns->addWidget(btnGitPull, 2, 1); layBtns->addWidget(btnGitMerge, 2, 2);
    layBtns->addWidget(btnGitCheckIgnore, 3, 0, 1, 1); layBtns->addWidget(btnGitOpenIgnore, 3, 1, 1, 2);
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

QWidget* MainWindow::createSimulatorPage()
{
    QWidget *page = new QWidget();
    QHBoxLayout *mainPageLayout = new QHBoxLayout(page);

    // LEFT: Main control panels (Start/Stop)
    QWidget *leftWidget = new QWidget();
    leftWidget->setFixedWidth(420); // 增加左侧宽度从 320 到 420
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);

    QGroupBox *gMain = new QGroupBox("主设备控制 (Port 5020)");
    QVBoxLayout *lm = new QVBoxLayout();
    QHBoxLayout *lmBtns = new QHBoxLayout();
    btnSimStartMain = new QPushButton("启动");
    btnSimStopMain = new QPushButton("停止");
    btnSimStopMain->setEnabled(false);
    lblSimMainStatus = new QLabel("离线");
    lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
    lmBtns->addWidget(btnSimStartMain);
    lmBtns->addWidget(btnSimStopMain);
    lmBtns->addStretch();
    lmBtns->addWidget(lblSimMainStatus);
    lm->addLayout(lmBtns);
    
    QGridLayout *glMain = new QGridLayout();
    glMain->addWidget(new QLabel("绑定:"), 0, 0);
    txtSimBindIP = new QLineEdit("0.0.0.0"); glMain->addWidget(txtSimBindIP, 0, 1);
    glMain->addWidget(new QLabel("端口:"), 0, 2);
    txtSimMainPort = new QLineEdit("5020"); txtSimMainPort->setMaximumWidth(60); glMain->addWidget(txtSimMainPort, 0, 3);
    lm->addLayout(glMain);
    gMain->setLayout(lm);

    QGroupBox *gAGV = new QGroupBox("AGV 设备控制 (Port 5021)");
    QVBoxLayout *la = new QVBoxLayout();
    QHBoxLayout *laBtns = new QHBoxLayout();
    btnSimStartAGV = new QPushButton("启动");
    btnSimStopAGV = new QPushButton("停止");
    btnSimStopAGV->setEnabled(false);
    lblSimAGVStatus = new QLabel("离线");
    lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
    laBtns->addWidget(btnSimStartAGV);
    laBtns->addWidget(btnSimStopAGV);
    laBtns->addStretch();
    laBtns->addWidget(lblSimAGVStatus);
    la->addLayout(laBtns);
    
    QGridLayout *glAGV = new QGridLayout();
    glAGV->addWidget(new QLabel("端口:"), 0, 0);
    txtSimAGVPort = new QLineEdit("5021"); txtSimAGVPort->setMaximumWidth(80); glAGV->addWidget(txtSimAGVPort, 0, 1);
    la->addLayout(glAGV);
    gAGV->setLayout(la);

    leftLayout->addWidget(gMain);
    leftLayout->addWidget(gAGV);

    // --- Tabbed Tools Area ---
    tabSimTools = new QTabWidget();
    
    // Sub-Page 1: Waveforms
    QWidget *pageWave = new QWidget();
    QVBoxLayout *lw = new QVBoxLayout(pageWave);
    QGridLayout *glWave = new QGridLayout();
    glWave->addWidget(new QLabel("设备:"), 0, 0);
    cmbWaveDevice = new QComboBox(); cmbWaveDevice->addItems(QStringList() << "主设备" << "AGV");
    glWave->addWidget(cmbWaveDevice, 0, 1);
    glWave->addWidget(new QLabel("地址:"), 0, 2);
    spinWaveAddr = new QSpinBox(); spinWaveAddr->setRange(0, 9999);
    glWave->addWidget(spinWaveAddr, 0, 3);
    glWave->addWidget(new QLabel("类型:"), 1, 0);
    cmbWaveType = new QComboBox(); cmbWaveType->addItems(QStringList() << "正弦波" << "方波" << "三角波" << "锯齿波" << "随机");
    glWave->addWidget(cmbWaveType, 1, 1);
    glWave->addWidget(new QLabel("幅度:"), 1, 2);
    spinWaveAmp = new QDoubleSpinBox(); spinWaveAmp->setRange(0, 65535); spinWaveAmp->setValue(1000);
    glWave->addWidget(spinWaveAmp, 1, 3);
    glWave->addWidget(new QLabel("周期(s):"), 2, 0);
    spinWavePeriod = new QDoubleSpinBox(); spinWavePeriod->setRange(0.1, 3600); spinWavePeriod->setValue(2.0);
    glWave->addWidget(spinWavePeriod, 2, 1);
    glWave->addWidget(new QLabel("偏移:"), 2, 2);
    spinWaveOffset = new QDoubleSpinBox(); spinWaveOffset->setRange(-65535, 65535); spinWaveOffset->setValue(1000);
    glWave->addWidget(spinWaveOffset, 2, 3);
    glWave->addWidget(new QLabel("相位(°):"), 3, 0);
    spinWavePhase = new QDoubleSpinBox(); spinWavePhase->setRange(0.0, 360.0); spinWavePhase->setValue(0.0);
    glWave->addWidget(spinWavePhase, 3, 1);
    glWave->addWidget(new QLabel("占空比:"), 3, 2);
    spinWaveDuty = new QDoubleSpinBox(); spinWaveDuty->setRange(0.01, 0.99); spinWaveDuty->setSingleStep(0.05); spinWaveDuty->setValue(0.5);
    glWave->addWidget(spinWaveDuty, 3, 3);
    btnWaveAdd = new QPushButton("➕ 添加/更新通道");
    glWave->addWidget(btnWaveAdd, 4, 0, 1, 4);
    lw->addLayout(glWave);
    tblWaveChannels = new QTableWidget();
    tblWaveChannels->setColumnCount(5);
    tblWaveChannels->setHorizontalHeaderLabels(QStringList() << "设备" << "地址" << "类型" << "状态" << "操作");
    tblWaveChannels->horizontalHeader()->setStretchLastSection(true);
    lw->addWidget(tblWaveChannels);
    tabSimTools->addTab(pageWave, "周期波形");

    // Sub-Page 2: Scripting
    QWidget *pageScript = new QWidget();
    QVBoxLayout *ls = new QVBoxLayout(pageScript);
    txtSimScript = new QTextEdit();
    txtSimScript->setPlaceholderText("示例:\nafter 3000 set 804 1\nafter 5000 setbit 50 10 1");
    ls->addWidget(txtSimScript);
    QHBoxLayout *lsBtns = new QHBoxLayout();
    btnSimRunScript = new QPushButton("运行脚本");
    btnSimStopScript = new QPushButton("停止脚本"); btnSimStopScript->setEnabled(false);
    lsBtns->addWidget(btnSimRunScript); lsBtns->addWidget(btnSimStopScript);
    ls->addLayout(lsBtns);
    tabSimTools->addTab(pageScript, "自动化脚本");

    // Sub-Page 3: Fault Injection
    QWidget *pageFault = new QWidget();
    QVBoxLayout *lf = new QVBoxLayout(pageFault);
    QGridLayout *gf = new QGridLayout();
    gf->addWidget(new QLabel("固定延迟(ms):"), 0, 0);
    spinSimDelayMs = new QSpinBox(); spinSimDelayMs->setRange(0, 10000); gf->addWidget(spinSimDelayMs, 0, 1);
    gf->addWidget(new QLabel("丢包概率:"), 0, 2);
    spinSimDropProb = new QDoubleSpinBox(); spinSimDropProb->setRange(0.0, 1.0); spinSimDropProb->setSingleStep(0.01); gf->addWidget(spinSimDropProb, 0, 3);
    gf->addWidget(new QLabel("功能码注入:"), 1, 0);
    txtInjectFunc = new QLineEdit(); txtInjectFunc->setPlaceholderText("FC"); gf->addWidget(txtInjectFunc, 1, 1);
    gf->addWidget(new QLabel("错误码:"), 1, 2);
    txtInjectFuncCode = new QLineEdit(); gf->addWidget(txtInjectFuncCode, 1, 3);
    QPushButton *btnApplyFault = new QPushButton("应用网络设置");
    gf->addWidget(btnApplyFault, 2, 0, 1, 4);
    lf->addLayout(gf); lf->addStretch();
    tabSimTools->addTab(pageFault, "异常注入");
    connect(btnApplyFault, &QPushButton::clicked, this, &MainWindow::onApplyFaultSettingsClicked);

    // Sub-Page 4: Scene
    QWidget *pageScene = new QWidget();
    QVBoxLayout *lsc = new QVBoxLayout(pageScene);
    btnSimSaveScene = new QPushButton("💾 保存当前寄存器快照 (Scene)");
    btnSimLoadScene = new QPushButton("📂 加载寄存器快照 (Scene)");
    lsc->addWidget(btnSimSaveScene); lsc->addWidget(btnSimLoadScene); lsc->addStretch();
    tabSimTools->addTab(pageScene, "场景快照");

    leftLayout->addWidget(tabSimTools);
    leftLayout->addStretch();

    // RIGHT: Register Maps and Specialized Log
    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);

    tabSimRegisterMaps = new QTabWidget();
    tblSimAGV = new QTableWidget();
    tblSimMain = new QTableWidget();
    setupSimulatorRegisterTable(tblSimAGV);
    setupSimulatorRegisterTable(tblSimMain);
    tabSimRegisterMaps->addTab(tblSimAGV, "AGV 寄存器表");
    tabSimRegisterMaps->addTab(tblSimMain, "主设备 寄存器表");
    rightLayout->addWidget(tabSimRegisterMaps, 3);

    QGroupBox *gSimLog = new QGroupBox("模拟器运行日志");
    QVBoxLayout *ll = new QVBoxLayout();
    txtSimLog = new QTextEdit(); txtSimLog->setReadOnly(true);
    txtSimLog->setStyleSheet("background: #1e1e1e; color: #00ff00; font-family: Monospace;");
    ll->addWidget(txtSimLog);
    rightLayout->addWidget(gSimLog, 1);

    mainPageLayout->addWidget(leftWidget);
    mainPageLayout->addWidget(rightWidget);

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
    simulatorPageWidget = createSimulatorPage();
    
    stackedWidget->addWidget(modbusPageWidget);
    stackedWidget->addWidget(serialPageWidget);
    stackedWidget->addWidget(gitPageWidget);
    stackedWidget->addWidget(simulatorPageWidget);
    
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
    connect(btnGitOpenIgnore, &QPushButton::clicked, this, &MainWindow::onGitOpenIgnoreClicked);
    connect(btnGitCheckIgnore, &QPushButton::clicked, this, &MainWindow::onGitCheckIgnoreClicked);
    connect(btnGitRefreshLog, &QPushButton::clicked, this, &MainWindow::onGitRefreshLogClicked);
    connect(btnGitReset, &QPushButton::clicked, this, &MainWindow::onGitResetClicked);
    
    // Waveform Controls
    connect(btnWaveAdd, &QPushButton::clicked, this, &MainWindow::onSimAddCyclicTimerClicked);
    
    simTickTimer = new QTimer(this);
    connect(simTickTimer, &QTimer::timeout, this, &MainWindow::onSimTimerTick);
    simTickTimer->start(100); 
}

void MainWindow::onSimAddCyclicTimerClicked()
{
    if (!cmbWaveDevice || !spinWaveAddr || !cmbWaveType || !spinWaveAmp || !spinWaveOffset || !spinWavePeriod || !tblWaveChannels) {
        if (txtSimLog) {
            txtSimLog->append(QString("[%1] 波形控件未初始化，无法添加通道").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
        }
        return;
    }

    CyclicTimer t;
    t.device = cmbWaveDevice->currentText() == "主设备" ? "Main" : "AGV";
    t.addr = (quint16)spinWaveAddr->value();
    t.type = cmbWaveType->currentText();
    t.amplitude = spinWaveAmp->value();
    t.offset = spinWaveOffset->value();
    t.period = spinWavePeriod->value();
    t.phase = spinWavePhase ? spinWavePhase->value() : 0.0;
    t.dutyCycle = spinWaveDuty ? spinWaveDuty->value() : 0.5;
    t.currentTicks = 0;
    t.active = true;

    // Replace if exists, else append
    bool found = false;
    for (int i=0; i<simCyclicTimers.size(); ++i) {
        if (simCyclicTimers[i].device == t.device && simCyclicTimers[i].addr == t.addr) {
            simCyclicTimers[i] = t;
            found = true;
            break;
        }
    }
    if (!found) simCyclicTimers.append(t);

    // Refresh UI Table
    tblWaveChannels->setRowCount(simCyclicTimers.size());
    for (int i=0; i<simCyclicTimers.size(); ++i) {
        const CyclicTimer &ct = simCyclicTimers[i];
        tblWaveChannels->setItem(i, 0, new QTableWidgetItem(ct.device == "Main" ? "主设备" : "AGV"));
        tblWaveChannels->setItem(i, 1, new QTableWidgetItem(QString::number(ct.addr)));
        tblWaveChannels->setItem(i, 2, new QTableWidgetItem(ct.type));
        tblWaveChannels->setItem(i, 3, new QTableWidgetItem(ct.active ? "运行中" : "停止"));
        
        QPushButton *btnRemove = new QPushButton("删除");
        tblWaveChannels->setCellWidget(i, 4, btnRemove);
        connect(btnRemove, &QPushButton::clicked, this, [this](){
            QPushButton *btn = qobject_cast<QPushButton*>(sender());
            if (!btn) return;
            // 通过位置查找对应行
            int row = -1;
            for(int r=0; r < tblWaveChannels->rowCount(); ++r) {
                if(tblWaveChannels->cellWidget(r, 4) == btn) {
                    row = r;
                    break;
                }
            }
            if (row >= 0 && row < simCyclicTimers.size()) {
                QString msg = QString("停止并移除波形通道: %1 地址 %2").arg(simCyclicTimers[row].device).arg(simCyclicTimers[row].addr);
                txtSimLog->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(msg));
                simCyclicTimers.removeAt(row);
                // 刷新界面表格内容
                onSimAddCyclicTimerClicked(); 
            }
        });
    }

    QString msg = QString("已更新波形通道: %1 地址 %2 类型: %3 (幅度:%4 周期:%5s)").arg(t.device).arg(t.addr).arg(t.type).arg(t.amplitude).arg(t.period);
    txtSimLog->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(msg));
}

void MainWindow::onSimTimerTick()
{
    for (int i=0; i<simCyclicTimers.size(); ++i) {
        CyclicTimer &t = simCyclicTimers[i];
        if (!t.active) continue;

        t.currentTicks++;
        double currentTime = t.currentTicks * 0.1; // 100ms per tick
        double freq = 1.0 / t.period;
        double omega = 2 * M_PI * freq;
        double phaseRad = t.phase * M_PI / 180.0;
        
        double val = 0;
        if (t.type == "正弦波") {
            val = t.amplitude * sin(omega * currentTime + phaseRad) + t.offset;
        } else if (t.type == "方波") {
            double cyclePos = fmod(currentTime + (t.phase/360.0)*t.period, t.period) / t.period;
            val = (cyclePos < t.dutyCycle) ? (t.amplitude + t.offset) : (-t.amplitude + t.offset);
        } else if (t.type == "三角波") {
            double cyclePos = fmod(currentTime + (t.phase/360.0)*t.period, t.period) / t.period;
            if (cyclePos < 0.25) val = t.amplitude * (cyclePos * 4.0);
            else if (cyclePos < 0.75) val = t.amplitude * (2.0 - cyclePos * 4.0);
            else val = t.amplitude * (cyclePos * 4.0 - 4.0);
            val += t.offset;
        } else if (t.type == "锯齿波") {
            double cyclePos = fmod(currentTime + (t.phase/360.0)*t.period, t.period) / t.period;
            val = t.amplitude * (2.0 * cyclePos - 1.0) + t.offset;
        } else if (t.type == "随机") {
            val = (QRandomGenerator::global()->generateDouble() * 2.0 - 1.0) * t.amplitude + t.offset;
        }

        quint16 regVal = static_cast<quint16>(qBound(0.0, val, 65535.0));
        ModbusSlave *target = (t.device == "Main") ? simMainDevice : simAGVDevice;
        if (target) {
            target->setRegister(t.addr, regVal);
            
            // 每秒记录一次日志，避免刷新过快 (100ms * 10 = 1s)
            if (t.currentTicks % 10 == 0) {
                QString logMsg = QString("周期更新: %1 地址[%2] -> %3 (类型:%4)")
                                 .arg(t.device == "Main" ? "主设备" : "AGV")
                                 .arg(t.addr)
                                 .arg(regVal)
                                 .arg(t.type);
                txtSimLog->append(QString("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(logMsg));
            }
        }
    }
}

void MainWindow::onSimGenerateReportClicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, "导出测试报告", "", "HTML Files (*.html)");
    if (fileName.isEmpty()) return;
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    
    QTextStream out(&file);
    out << "<html><head><title>Modbus Simulator Report</title><style>";
    out << "table { border-collapse: collapse; width: 100%; } th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
    out << "th { background-color: #f2f2f2; } .write { color: red; } .read { color: blue; }";
    out << "</style></head><body>";
    out << "<h1>Modbus 模拟器测试报告</h1>";
    out << "<p>生成日期: " << QDateTime::currentDateTime().toString() << "</p>";
    out << "<h2>操作历史 (最后" << registerHistory.size() << "条)</h2>";
    out << "<table><tr><th>时间</th><th>类型</th><th>地址</th><th>数值</th></tr>";
    
    for (const QJsonObject &obj : registerHistory) {
        QString type = obj["type"].toString();
        out << "<tr><td>" << obj["time"].toString() << "</td>";
        out << "<td class='" << (type == "Write" ? "write" : "read") << "'>" << type << "</td>";
        out << "<td>" << obj["addr"].toInt() << "</td>";
        out << "<td>" << obj["val"].toInt() << "</td></tr>";
    }
    out << "</table></body></html>";
    file.close();
    logMessage("HTML测试报告已生成: " + fileName);
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
    // Determine if we should use local simulator mapping
    QSettings settings("LiChenYang", "LinuxHelper");
    bool localSim = settings.value("tcp.local_simulator", false).toBool() || (qgetenv("LOCAL_SIMULATOR") == "1");

    // Default to values from UI
    QString ip = cmbIP->currentText().trimmed();
    int port = txtPort->text().toInt();

    // If local simulator active, override ip/port according to selected register tab (AGV/Robot)
    int mapIndex = tabRegisterMaps->currentIndex(); // 0: AGV, 1: Robot
    if (localSim) {
        ip = "127.0.0.1";
        if (mapIndex == 1) {
            // Robot -> main device on 5020
            port = 5020;
        } else {
            // AGV -> 5021
            port = 5021;
        }
        // reflect to UI
        cmbIP->setCurrentText(ip);
        txtPort->setText(QString::number(port));
    }

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

// --- Simulator Slots ---
void MainWindow::onStartSimulatorClicked()
{
    QPushButton *b = qobject_cast<QPushButton*>(sender());
    if (!b) return;
    if (b == btnSimStartMain) {
        QString bind = txtSimBindIP->text().trimmed();
        quint16 port = static_cast<quint16>(txtSimMainPort->text().toUShort());
        bool ok = simMainDevice->start(port, 1, bind);
        txtSimLog->append(QString("主设备 启动 %1 (绑定 %2:%3)").arg(ok).arg(bind).arg(port));
        if (ok) {
            btnSimStartMain->setEnabled(false);
            btnSimStopMain->setEnabled(true);
            lblSimMainStatus->setText("离线");
            lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
        } else {
            btnSimStartMain->setEnabled(true);
            btnSimStopMain->setEnabled(false);
            lblSimMainStatus->setText("离线");
            lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
        }
    } else if (b == btnSimStartAGV) {
        QString bind = txtSimBindIP->text().trimmed();
        quint16 port = static_cast<quint16>(txtSimAGVPort->text().toUShort());
        bool ok = simAGVDevice->start(port, 1, bind);
        txtSimLog->append(QString("AGV 启动 %1 (绑定 %2:%3)").arg(ok).arg(bind).arg(port));
        if (ok) {
            btnSimStartAGV->setEnabled(false);
            btnSimStopAGV->setEnabled(true);
            lblSimAGVStatus->setText("离线");
            lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
        } else {
            btnSimStartAGV->setEnabled(true);
            btnSimStopAGV->setEnabled(false);
            lblSimAGVStatus->setText("离线");
            lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
        }
    }
}

void MainWindow::onStopSimulatorClicked()
{
    QPushButton *b = qobject_cast<QPushButton*>(sender());
    if (!b) return;
    if (b == btnSimStopMain) {
        simMainDevice->stop();
        txtSimLog->append("主设备 停止");
        btnSimStartMain->setEnabled(true);
        btnSimStopMain->setEnabled(false);
        lblSimMainStatus->setText("离线");
        lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
    } else if (b == btnSimStopAGV) {
        simAGVDevice->stop();
        txtSimLog->append("AGV 停止");
        btnSimStartAGV->setEnabled(true);
        btnSimStopAGV->setEnabled(false);
        lblSimAGVStatus->setText("离线");
        lblSimAGVStatus->setStyleSheet("color: red; font-weight: bold;");
    }
}

void MainWindow::onSimSetRegisterClicked()
{
    bool ok;
    int addr = simAddrEdit->text().toInt(&ok);
    if (!ok || addr < 0 || addr > 1000) {
        QMessageBox::warning(this, "错误", "地址需在 0~1000 之间");
        return;
    }
    quint16 val = quint16(simValueEdit->text().toUShort());
    ModbusSlave *target = (simDeviceSelect->currentIndex() == 0) ? simMainDevice : simAGVDevice;
    target->setRegister(addr, val);
    txtSimLog->append(QString("设置 %1 地址 %2 = %3").arg(simDeviceSelect->currentText()).arg(addr).arg(val));
}

void MainWindow::onSimSetBitClicked()
{
    int bit = simBitIndex->value();
    ModbusSlave *target = (simDeviceSelect->currentIndex() == 0) ? simMainDevice : simAGVDevice;
    bool v = simBitValue->isChecked();
    bool ok = target->setRegisterBit(50, bit, v);
    txtSimLog->append(QString("%1 寄存器50 位%2 = %3").arg(simDeviceSelect->currentText()).arg(bit).arg(v));
    if (!ok) QMessageBox::warning(this, "错误", "设置位失败");
}

void MainWindow::onSimWriteValuesClicked()
{
    QTableWidget *table = (tabSimRegisterMaps->currentIndex() == 0) ? tblSimAGV : tblSimMain;
    ModbusSlave *target = (tabSimRegisterMaps->currentIndex() == 0) ? simAGVDevice : simMainDevice;
    QString deviceName = (tabSimRegisterMaps->currentIndex() == 0) ? "AGV" : "主设备";

    int written = 0;
    int skipped = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *addrItem = table->item(row, 0);
        QTableWidgetItem *valItem = table->item(row, 2);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;
        if (!valItem || valItem->text().trimmed().isEmpty()) {
            skipped++;
            continue;
        }

        bool okAddr = false;
        bool okVal = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        uint val = valItem->text().trimmed().toUInt(&okVal, 0);

        if (!okAddr || addr > 1000 || !okVal || val > 65535) {
            skipped++;
            continue;
        }

        if (target->setRegister(static_cast<quint16>(addr), static_cast<quint16>(val))) {
            written++;
        } else {
            skipped++;
        }
    }

    txtSimLog->append(QString("%1 批量写入完成: 成功 %2, 跳过 %3")
                      .arg(deviceName)
                      .arg(written)
                      .arg(skipped));
}

void MainWindow::onSimRandomValuesClicked()
{
    QTableWidget *table = (tabSimRegisterMaps->currentIndex() == 0) ? tblSimAGV : tblSimMain;

    int randomized = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *addrItem = table->item(row, 0);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;

        bool okAddr = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        if (!okAddr || addr > 1000) continue;

        if (!table->item(row, 2)) table->setItem(row, 2, new QTableWidgetItem());
        quint16 randomValue = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
        table->item(row, 2)->setText(QString::number(randomValue));
        randomized++;
    }

    QString deviceName = (tabSimRegisterMaps->currentIndex() == 0) ? "AGV" : "主设备";
    txtSimLog->append(QString("%1 随机值填充完成: %2 行").arg(deviceName).arg(randomized));
}

void MainWindow::onSimRandomAndWriteClicked()
{
    QTableWidget *table = (tabSimRegisterMaps->currentIndex() == 0) ? tblSimAGV : tblSimMain;
    ModbusSlave *target = (tabSimRegisterMaps->currentIndex() == 0) ? simAGVDevice : simMainDevice;
    QString deviceName = (tabSimRegisterMaps->currentIndex() == 0) ? "AGV" : "主设备";

    int written = 0;
    int skipped = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *addrItem = table->item(row, 0);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;

        bool okAddr = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        if (!okAddr || addr > 1000) { skipped++; continue; }

        quint16 randomValue = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
        if (!table->item(row, 2)) table->setItem(row, 2, new QTableWidgetItem());
        table->item(row, 2)->setText(QString::number(randomValue));

        if (target->setRegister(static_cast<quint16>(addr), randomValue)) {
            written++;
        } else {
            skipped++;
        }
    }

    txtSimLog->append(QString("%1 随机并写入完成: 成功 %2, 跳过 %3").arg(deviceName).arg(written).arg(skipped));
}

void MainWindow::onSimSaveSceneClicked()
{
    QString fn = QFileDialog::getSaveFileName(this, "保存场景", QString(), "JSON Files (*.json)");
    if (fn.isEmpty()) return;
    QJsonObject root;

    QVector<quint16> mainData = simMainDevice->exportHolding();
    QJsonObject mainObj;
    for (int i = 0; i < mainData.size(); ++i) {
        if (mainData[i] != 0) mainObj.insert(QString::number(i), int(mainData[i]));
    }
    QVector<quint16> agvData = simAGVDevice->exportHolding();
    QJsonObject agvObj;
    for (int i = 0; i < agvData.size(); ++i) {
        if (agvData[i] != 0) agvObj.insert(QString::number(i), int(agvData[i]));
    }
    root.insert("main", mainObj);
    root.insert("agv", agvObj);

    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "无法保存场景文件");
        return;
    }
    QJsonDocument doc(root);
    f.write(doc.toJson());
    f.close();
    txtSimLog->append(QString("场景已保存: %1").arg(fn));
}

void MainWindow::onSimLoadSceneClicked()
{
    QString fn = QFileDialog::getOpenFileName(this, "加载场景", QString(), "JSON Files (*.json)");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "错误", "无法打开场景文件"); return; }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll()); f.close();
    if (!doc.isObject()) { QMessageBox::warning(this, "错误", "场景文件格式错误"); return; }
    QJsonObject root = doc.object();

    QVector<quint16> mainData = simMainDevice->exportHolding();
    int msize = mainData.size();
    if (root.contains("main") && root.value("main").isObject()) {
        QJsonObject mainObj = root.value("main").toObject();
        for (auto it = mainObj.begin(); it != mainObj.end(); ++it) {
            bool ok = false; int idx = it.key().toInt(&ok); if (!ok) continue;
            if (idx >= 0 && idx < msize) mainData[idx] = (quint16)it.value().toInt();
        }
    }
    simMainDevice->loadHolding(mainData);

    QVector<quint16> agvData = simAGVDevice->exportHolding();
    int asize = agvData.size();
    if (root.contains("agv") && root.value("agv").isObject()) {
        QJsonObject agvObj = root.value("agv").toObject();
        for (auto it = agvObj.begin(); it != agvObj.end(); ++it) {
            bool ok = false; int idx = it.key().toInt(&ok); if (!ok) continue;
            if (idx >= 0 && idx < asize) agvData[idx] = (quint16)it.value().toInt();
        }
    }
    simAGVDevice->loadHolding(agvData);

    syncSimulatorTablesFromMaps();
    txtSimLog->append(QString("场景已加载: %1").arg(fn));
}

void MainWindow::onSimRunScriptClicked()
{
    QString txt = txtSimScript->toPlainText();
    if (txt.trimmed().isEmpty()) return;
    QStringList lines = txt.split('\n', Qt::SkipEmptyParts);
    for (QString raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        QStringList tok = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tok.size() < 4) continue;
        if (tok[0].toLower() != "after") continue;
        int ms = tok[1].toInt();
        QString cmd = tok[2].toLower();

        // Determine device/address/value positions
        QString deviceToken;
        quint16 addr = 0; bool okAddr=false;
        QString valueToken;

        if (cmd == "set") {
            if (tok.size() >= 5) {
                // if tok[3] is device keyword
                if (tok[3].toLower() == "main" || tok[3].toLower() == "agv") {
                    deviceToken = tok[3].toLower();
                    if (tok.size() >= 6) { addr = (quint16)tok[4].toUInt(&okAddr); valueToken = tok[5]; }
                    else continue;
                } else {
                    addr = (quint16)tok[3].toUInt(&okAddr); if (!okAddr) continue; valueToken = tok[4];
                }
            } else continue;
            // schedule
            QTimer *t = new QTimer(this);
            t->setSingleShot(true);
            scriptTimers.append(t);
            connect(t, &QTimer::timeout, this, [this, t, deviceToken, addr, valueToken]() {
                bool isFloat = valueToken.contains('.');
                bool acted = false;
                if (!deviceToken.isEmpty()) {
                    ModbusSlave *dev = (deviceToken == "agv") ? simAGVDevice : simMainDevice;
                    if (isFloat) { float fv = valueToken.toFloat(); dev->setFloat(addr, fv); acted = true; }
                    else { quint16 v = (quint16)QString(valueToken).toUShort(); dev->setRegister(addr, v); acted = true; }
                } else {
                    // try main first, then agv
                    if (isFloat) {
                        float fv = valueToken.toFloat();
                        if (simMainDevice->setFloat(addr, fv)) acted = true; else if (simAGVDevice->setFloat(addr, fv)) acted = true;
                    } else {
                        quint16 v = (quint16)QString(valueToken).toUShort();
                        if (simMainDevice->setRegister(addr, v)) acted = true; else if (simAGVDevice->setRegister(addr, v)) acted = true;
                    }
                }
                txtSimLog->append(QString("脚本执行: addr=%1 val=%2 %3").arg(addr).arg(valueToken).arg(acted?"成功":"未作用到设备"));
                t->deleteLater();
            });
            t->start(ms);
        }
        else if (cmd == "setbit") {
            // after <ms> setbit [main|agv]? <addr> <bit> <0|1>
            int base = 3;
            if (tok[3].toLower() == "main" || tok[3].toLower() == "agv") { deviceToken = tok[3].toLower(); base = 4; }
            if (tok.size() < base+3) continue;
            addr = (quint16)tok[base].toUInt(&okAddr); if (!okAddr) continue;
            int bit = tok[base+1].toInt(); int val = tok[base+2].toInt();
            QTimer *t = new QTimer(this); t->setSingleShot(true); scriptTimers.append(t);
            connect(t, &QTimer::timeout, this, [this, t, deviceToken, addr, bit, val]() {
                bool acted = false;
                if (!deviceToken.isEmpty()) {
                    ModbusSlave *dev = (deviceToken == "agv") ? simAGVDevice : simMainDevice;
                    acted = dev->setRegisterBit(addr, bit, val != 0);
                } else {
                    if (simMainDevice->setRegisterBit(addr, bit, val != 0)) acted = true; else if (simAGVDevice->setRegisterBit(addr, bit, val != 0)) acted = true;
                }
                txtSimLog->append(QString("脚本执行(setbit): addr=%1 bit=%2 val=%3 %4").arg(addr).arg(bit).arg(val).arg(acted?"成功":"未作用到设备"));
                t->deleteLater();
            });
            t->start(ms);
        }
    }
    btnSimRunScript->setEnabled(false);
    btnSimStopScript->setEnabled(true);
    txtSimLog->append("脚本调度已启动");
}

void MainWindow::onSimStopScriptClicked()
{
    for (QTimer *t : scriptTimers) {
        if (t->isActive()) t->stop();
        t->deleteLater();
    }
    scriptTimers.clear();
    btnSimRunScript->setEnabled(true);
    btnSimStopScript->setEnabled(false);
    txtSimLog->append("脚本调度已停止");
}

void MainWindow::onRegisterOperation(quint16 addr, quint16 value, const QString &opType)
{
    QJsonObject entry;
    entry.insert("timestamp", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"));
    
    // Determine source d  evice based on sender
    ModbusSlave *senderDevice = qobject_cast<ModbusSlave*>(sender());
    QString device = "unknown";
    if (senderDevice == simMainDevice) device = "main";
    else if (senderDevice == simAGVDevice) device = "agv";
    
    entry.insert("device", device);
    entry.insert("address", (int)addr);
    entry.insert("value", (int)value);
    entry.insert("operation", opType);
    
    registerHistory.append(entry);
    
    // Optional: limit in-memory history size
    if (registerHistory.size() > 5000) registerHistory.removeFirst();
}

void MainWindow::onExportHistoryClicked()
{
    if (registerHistory.isEmpty()) {
        QMessageBox::information(this, "提示", "暂无历史记录可导出");
        return;
    }
    
    QString fn = QFileDialog::getSaveFileName(this, "导出记录", QString(), "CSV Files (*.csv)");
    if (fn.isEmpty()) return;
    
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    
    QTextStream out(&f);
    out << "Timestamp,Device,Address,Value,Operation\n";
    for (const QJsonObject &obj : registerHistory) {
        out << obj.value("timestamp").toString() << ","
            << obj.value("device").toString() << ","
            << obj.value("address").toInt() << ","
            << obj.value("value").toInt() << ","
            << obj.value("operation").toString() << "\n";
    }
    f.close();
    txtSimLog->append(QString("历史记录已导出: %1 (共 %2 条)").arg(fn).arg(registerHistory.size()));
}

void MainWindow::onApplyFaultSettingsClicked()
{
    int delay = spinSimDelayMs->value();
    double drop = spinSimDropProb->value();
    simMainDevice->setFixedDelayMs(delay);
    simMainDevice->setDropProbability(drop);
    simAGVDevice->setFixedDelayMs(delay);
    simAGVDevice->setDropProbability(drop);

    // clear previous injections
    simMainDevice->clearInjectedExceptions();
    simAGVDevice->clearInjectedExceptions();

    // function injection
    QString ffunc = txtInjectFunc->text().trimmed();
    QString fcode = txtInjectFuncCode->text().trimmed();
    if (!ffunc.isEmpty() && !fcode.isEmpty()) {
        bool ok1 = false, ok2 = false;
        int func = ffunc.toInt(&ok1);
        int code = fcode.toInt(&ok2);
        if (ok1 && ok2) {
            simMainDevice->injectExceptionForFunction((quint8)func, (quint8)code);
            simAGVDevice->injectExceptionForFunction((quint8)func, (quint8)code);
            txtSimLog->append(QString("注入功能异常: func=%1 code=%2").arg(func).arg(code));
        }
    }

    // address injection
    QString aaddr = txtInjectAddr->text().trimmed();
    QString acode = txtInjectAddrCode->text().trimmed();
    if (!aaddr.isEmpty() && !acode.isEmpty()) {
        bool ok1 = false, ok2 = false;
        int addr = aaddr.toInt(&ok1);
        int code = acode.toInt(&ok2);
        if (ok1 && ok2) {
            simMainDevice->injectExceptionForAddress((quint16)addr, (quint8)code);
            simAGVDevice->injectExceptionForAddress((quint16)addr, (quint8)code);
            txtSimLog->append(QString("注入地址异常: addr=%1 code=%2").arg(addr).arg(code));
        }
    }
    txtSimLog->append(QString("已应用网络/异常注入设置: delay=%1ms drop=%2").arg(delay).arg(drop));
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
    // Push current branch to selected remote
    QString branch = cmbGitBranches->currentText();
    QString remote = cmbGitRemote->currentText();
    if (branch.isEmpty()) branch = "master"; 
    if (remote.isEmpty()) remote = "origin";
    
    // Use -u to set upstream as requested
    runGitCommand(QStringList() << "push" << "-u" << remote << branch);
}

void MainWindow::onGitPullClicked() {
    QString branch = cmbGitBranches->currentText();
    QString remote = cmbGitRemote->currentText();
    if (branch.isEmpty()) branch = "master";
    if (remote.isEmpty()) remote = "origin";
    runGitCommand(QStringList() << "pull" << remote << branch);
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

void MainWindow::onGitOpenIgnoreClicked() {
    QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先选择 Git 仓库目录。</font>");
        return;
    }

    QString ignorePath = repoDir + "/.gitignore";
    QFile file(ignorePath);
    if (!file.exists()) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "文件未找到", 
            ".gitignore 文件不存在，是否创建一个空文件并打开？", QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            } else {
                txtGitLog->append("<font color='red'>错误: 无法创建 .gitignore 文件。</font>");
                return;
            }
        } else {
            return;
        }
    }

    // Try to open with default editor
    bool started = QDesktopServices::openUrl(QUrl::fromLocalFile(ignorePath));
    if (started) {
        txtGitLog->append(QString("信息: 已尝试使用系统默认编辑器打开 %1").arg(ignorePath));
    } else {
        txtGitLog->append("<font color='red'>错误: 无法打开 .gitignore 文件，请手动到目录下修改。</font>");
    }
}

void MainWindow::onGitCheckIgnoreClicked() {
    QString repoDir = cmbGitDir->currentText();
    if (repoDir.isEmpty()) {
        txtGitLog->append("错误: 请先选择 Git 仓库目录。");
        return;
    }

    QFile ignoreFile(repoDir + "/.gitignore");
    
    auto createIgnoreFile = [&](const QString& type) {
        if (ignoreFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&ignoreFile);
            if (type == "Qt") {
                out << "# Qt intermediate files\n"
                    << "*.o\n*.obj\nMakefile*\nmoc_*\nui_*\nqrc_*\n.qmake.stash\nbuild/\nbuild_*/\n\n"
                    << ".qtc_clangd/\n"
                    << "build_log.txt\n\n"
                    << "# Project specific\n"
                    << "ModbusTCPAssistant\n*.user\n*.user.*\n";
            } else if (type == "Keil") {
                out << "# Keil intermediate files\n"
                    << "*.lst\n*.obj\n*.o\n*.d\n*.crf\n*.lnp\n*.axf\n*.htm\n*.build_log.htm\n"
                    << "*.dep\n*.ie\n*.i\n"
                    << "# Keil IDE files\n"
                    << "Listings/\nObjects/\n"
                    << "*.uvgui.*\n*.uvguix.*\n*.bak\n";
            }
            out << "\n# OS files\n.DS_Store\nThumbs.db\n";
            ignoreFile.close();
            txtGitLog->append(QString("成功: 已创建 %1 类型的 .gitignore 文件。").arg(type));
        }
    };

    if (!ignoreFile.exists()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(".gitignore 检查");
        msgBox.setText(".gitignore 文件不存在！\n请选择要创建的模板类型：");
        QPushButton *qtBtn = msgBox.addButton("Qt/C++ 模板", QMessageBox::ActionRole);
        QPushButton *keilBtn = msgBox.addButton("Keil/C 模板", QMessageBox::ActionRole);
        msgBox.addButton("取消", QMessageBox::RejectRole);

        msgBox.exec();

        if (msgBox.clickedButton() == qtBtn) {
            createIgnoreFile("Qt");
        } else if (msgBox.clickedButton() == keilBtn) {
            createIgnoreFile("Keil");
        }
    } else {
        txtGitLog->append("信息: .gitignore 文件已存在。正在检查缺失规则...");
        if (ignoreFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QString content = ignoreFile.readAll();
            
            struct Rule { QString pattern; QString description; };
            QList<Rule> suggestions = {
                {".qtc_clangd/", ".qtc_clangd/ (QtCreator 缓存缓存)"},
                {"build_log.txt", "build_log.txt (编译日志)"},
                {"build/", "build/ (默认构建目录)"},
                {"build_*/", "build_*/ (通配构建目录)"},
                {"*.o", "*.o (中间对象文件)"},
                {"*.user", "*.user (用户本地配置)"}
            };

            QStringList missingPatterns;
            QStringList missingDesc;

            for (const auto& rule : suggestions) {
                if (!content.contains(rule.pattern)) {
                    missingPatterns << rule.pattern;
                    missingDesc << rule.description;
                }
            }

            if (!missingPatterns.isEmpty()) {
                QString msg = "您的 .gitignore 可能缺少以下建议规则:\n" + missingDesc.join("\n") + 
                              "\n\n是否要自动将这些通用忽略规则添加到文件末尾？";
                QMessageBox::StandardButton reply = QMessageBox::question(this, "优化建议", msg, QMessageBox::Yes|QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    QTextStream out(&ignoreFile);
                    if (!content.endsWith("\n")) out << "\n";
                    out << "\n# Automatically added by Assistant\n";
                    for (const QString& p : missingPatterns) {
                        out << p << "\n";
                    }
                    txtGitLog->append("成功: 已自动补全缺失的忽略规则。");
                    
                    // 彻底清除缓存以确保忽略规则对已追踪文件生效
                    txtGitLog->append("信息: 正在从 Git 索引中强制移除被忽略的文件以确保规则生效...");
                    QStringList cleanArgs;
                    cleanArgs << "rm" << "-r" << "--cached";
                    for (const QString& p : missingPatterns) {
                        // 去掉可能的回车符或空格，并针对目录模式（如 build/）做处理
                        QString cleanPattern = p.trimmed();
                        if (cleanPattern.endsWith("/")) {
                            cleanPattern.chop(1);
                        }
                        cleanArgs << cleanPattern;
                    }
                    runGitCommand(cleanArgs);
                }
            } else {
                QMessageBox::information(this, ".gitignore 检查", "您的 .gitignore 看起来已经包含基本的忽略规则。");
            }
            ignoreFile.close();
        }
    }
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

void MainWindow::setupSimulatorRegisterTable(QTableWidget *table) {
    if (!table) return;
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(QStringList() << "地址" << "描述" << "整数值" << "浮点值" << "操作");
    
    // 重新规划列表宽度
    table->setColumnWidth(0, 50);  // 地址列：仅需显示4-5位数字
    table->setColumnWidth(1, 120); // 描述列：允许较长描述
    table->setColumnWidth(2, 60);  // 整数值：5位数字
    table->setColumnWidth(3, 80);  // 浮点值：格式化后的浮点数
    table->setColumnWidth(4, 70);  // 操作列：固定的按钮宽度

    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); // 描述列自适应剩余空间
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setRowCount(50);

    for (int i = 0; i < 50; ++i) {
        if (!table->item(i, 0)) table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        if (!table->item(i, 1)) table->setItem(i, 1, new QTableWidgetItem(""));
        if (!table->item(i, 2)) table->setItem(i, 2, new QTableWidgetItem(""));
        if (!table->item(i, 3)) table->setItem(i, 3, new QTableWidgetItem(""));
        
        QPushButton *btnBit = new QPushButton("位编辑");
        btnBit->setFixedWidth(60); // 明确限制按钮宽度
        table->setCellWidget(i, 4, btnBit);
        connect(btnBit, &QPushButton::clicked, this, [this, table, i](){
            onSimShowBitEditor(i);
        });
    }
    connect(table, &QTableWidget::cellChanged, this, &MainWindow::onSimTableRowChanged);
}

void MainWindow::onSimTableRowChanged(int row, int column)
{
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;
    ModbusSlave *target = (table == tblSimAGV) ? simAGVDevice : simMainDevice;
    
    QTableWidgetItem *addrItem = table->item(row, 0);
    if (!addrItem || addrItem->text().isEmpty()) return;
    quint16 addr = (quint16)addrItem->text().toUInt();
    
    table->blockSignals(true);
    if (column == 2) { // Integer changed
        quint16 val = (quint16)table->item(row, 2)->text().toUInt();
        target->setRegister(addr, val);
        float f = target->getFloat(addr);
        if (table->item(row, 3)) table->item(row, 3)->setText(QString::number(f, 'f', 2));
    }
    else if (column == 3) { // Float changed
        float f = table->item(row, 3)->text().toFloat();
        target->setFloat(addr, f);
        if (table->item(row, 2)) table->item(row, 2)->setText(QString::number(target->getRegister(addr)));
        // also check next row
        for (int r=0; r < table->rowCount(); ++r) {
            if (table->item(r,0) && table->item(r,0)->text().toUInt() == (uint)(addr+1)) {
                if (table->item(r,2)) table->item(r,2)->setText(QString::number(target->getRegister(addr+1)));
                break;
            }
        }
    }
    table->blockSignals(false);
}

void MainWindow::onSimShowBitEditor(int row)
{
    QTableWidget *table = (tabSimRegisterMaps->currentIndex() == 0) ? tblSimAGV : tblSimMain;
    ModbusSlave *target = (tabSimRegisterMaps->currentIndex() == 0) ? simAGVDevice : simMainDevice;
    
    QTableWidgetItem *addrItem = table->item(row, 0);
    if (!addrItem || addrItem->text().isEmpty()) return;
    quint16 addr = (quint16)addrItem->text().toUInt();
    quint16 val = target->getRegister(addr);
    
    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle(QString("地址 %1 位编辑器").arg(addr));
    QVBoxLayout *v = new QVBoxLayout(dlg);
    QGridLayout *g = new QGridLayout();
    QList<QCheckBox*> checks;
    for (int i=0; i<16; ++i) {
        QCheckBox *cb = new QCheckBox(QString("Bit %1").arg(i));
        cb->setChecked((val >> i) & 1);
        g->addWidget(cb, i/4, i%4);
        checks.append(cb);
    }
    v->addLayout(g);
    QPushButton *btnOk = new QPushButton("确定");
    v->addWidget(btnOk);
    connect(btnOk, &QPushButton::clicked, dlg, &QDialog::accept);
    
    if (dlg->exec() == QDialog::Accepted) {
        quint16 newVal = 0;
        for (int i=0; i<16; ++i) if (checks[i]->isChecked()) newVal |= (1 << i);
        target->setRegister(addr, newVal);
        table->blockSignals(true);
        if (table->item(row, 2)) table->item(row, 2)->setText(QString::number(newVal));
        table->blockSignals(false);
    }
    dlg->deleteLater();
}

void MainWindow::syncSimulatorTablesFromMaps() {
    auto syncOne = [](QTableWidget *src, QTableWidget *dst) {
        if (!src || !dst) return;
        if (dst->rowCount() < src->rowCount()) dst->setRowCount(src->rowCount());

        for (int row = 0; row < src->rowCount(); ++row) {
            if (!dst->item(row, 0)) dst->setItem(row, 0, new QTableWidgetItem());
            if (!dst->item(row, 1)) dst->setItem(row, 1, new QTableWidgetItem());
            if (!dst->item(row, 2)) dst->setItem(row, 2, new QTableWidgetItem());

            QTableWidgetItem *srcAddr = src->item(row, 0);
            QTableWidgetItem *srcCmt = src->item(row, 1);
            dst->item(row, 0)->setText(srcAddr ? srcAddr->text() : "");
            dst->item(row, 1)->setText(srcCmt ? srcCmt->text() : "");

            Qt::ItemFlags flags0 = dst->item(row, 0)->flags();
            Qt::ItemFlags flags1 = dst->item(row, 1)->flags();
            dst->item(row, 0)->setFlags(flags0 & ~Qt::ItemIsEditable);
            dst->item(row, 1)->setFlags(flags1 & ~Qt::ItemIsEditable);
        }
    };

    syncOne(tblAGV, tblSimAGV);
    syncOne(tblRobot, tblSimMain);
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

    // Check whether local simulator mapping is enabled via settings or env var
    QSettings settings("LiChenYang", "LinuxHelper");
    bool localSim = settings.value("tcp.local_simulator", false).toBool() || (qgetenv("LOCAL_SIMULATOR") == "1");

    if (localSim) {
        // If using local simulator, map AGV -> 127.0.0.1:5021, Robot -> 127.0.0.1:5020
        if (index == 0) { // AGV
            cmbIP->setCurrentText("127.0.0.1");
            txtPort->setText("5021");
            onConnectClicked();
        } else if (index == 1) { // Robot
            cmbIP->setCurrentText("127.0.0.1");
            txtPort->setText("5020");
            onConnectClicked();
        }
    } else {
        // Legacy behavior: connect to configured physical device addresses
        if (index == 0) { // AGV
            cmbIP->setCurrentText("192.168.1.88");
            onConnectClicked();
        } else if (index == 1) { // Robot
            cmbIP->setCurrentText("192.168.1.13");
            onConnectClicked();
        }
    }
}

void MainWindow::onRegisterTableChanged(int row, int column) {
    Q_UNUSED(row);
    Q_UNUSED(column);
    syncSimulatorTablesFromMaps();
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
    syncSimulatorTablesFromMaps();
}
