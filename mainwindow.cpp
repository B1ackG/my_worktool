#include "mainwindow.h"
#include <QMessageBox>
#include <QDateTime>
#include <QDate>
#include <QDataStream>
#include <QDebug>
#include <QIntValidator>
#include <QRandomGenerator>
#include <bitset>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>

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
    loadAutoScene(); // 自动加载上次保存的寄存器设置、格式和波形
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

    // 程序启动时确保波形初始状态为停止
    onSimStopAllWaveformsClicked();
}

MainWindow::~MainWindow()
{
    saveRegisterTables();
    saveAutoScene(); // 自动保存所有寄存器设置、格式和波形

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
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    cmbGitDir->setPlaceholderText("选择Git仓库路径...");
#else
    cmbGitDir->lineEdit()->setPlaceholderText("选择Git仓库路径...");
#endif
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
    btnGitCopyDaily = new QPushButton("复制到日报");
    btnGitCopyDaily->setStyleSheet("background-color: #d1f2eb; font-weight: bold;");

    txtScpTargetIp = new QLineEdit("192.168.1.245");
    txtScpTargetIp->setPlaceholderText("目标设备地址");
    txtScpPassword = new QLineEdit();
    txtScpPassword->setPlaceholderText("SSH 密码");
    txtScpPassword->setEchoMode(QLineEdit::Password);
    btnScpTransfer = new QPushButton("搜索并传输(全目录层级)");
    btnScpTransfer->setStyleSheet("background-color: #fce4ec; font-weight: bold;");
    
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
    layHist->addWidget(btnGitCopyDaily);
    layOps->addLayout(layHist);

    // SCP Transfer Section
    QHBoxLayout *layScp = new QHBoxLayout();
    layScp->addWidget(new QLabel("目标:"));
    layScp->addWidget(txtScpTargetIp, 1);
    layScp->addWidget(new QLabel("密码:"));
    layScp->addWidget(txtScpPassword, 1);
    layScp->addWidget(btnScpTransfer);
    layOps->addLayout(layScp);
    
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
    cmbWaveType = new QComboBox(); cmbWaveType->addItems(QStringList() << "正弦波" << "方波" << "三角波" << "锯齿波" << "随机" << "来回增减");
    glWave->addWidget(cmbWaveType, 1, 1);
    glWave->addWidget(new QLabel("幅度(或最大):"), 1, 2);
    spinWaveAmp = new QDoubleSpinBox(); spinWaveAmp->setRange(-65535, 65535); spinWaveAmp->setValue(1000);
    glWave->addWidget(spinWaveAmp, 1, 3);
    glWave->addWidget(new QLabel("周期(s):"), 2, 0);
    spinWavePeriod = new QDoubleSpinBox(); spinWavePeriod->setRange(0.1, 3600); spinWavePeriod->setValue(2.0);
    glWave->addWidget(spinWavePeriod, 2, 1);
    glWave->addWidget(new QLabel("偏移(或最小):"), 2, 2);
    spinWaveOffset = new QDoubleSpinBox(); spinWaveOffset->setRange(-65535, 65535); spinWaveOffset->setValue(0);
    glWave->addWidget(spinWaveOffset, 2, 3);
    glWave->addWidget(new QLabel("相位(°):"), 3, 0);
    spinWavePhase = new QDoubleSpinBox(); spinWavePhase->setRange(0.0, 360.0); spinWavePhase->setValue(0.0);
    glWave->addWidget(spinWavePhase, 3, 1);
    glWave->addWidget(new QLabel("占空比:"), 3, 2);
    spinWaveDuty = new QDoubleSpinBox(); spinWaveDuty->setRange(0.01, 0.99); spinWaveDuty->setSingleStep(0.05); spinWaveDuty->setValue(0.5);
    glWave->addWidget(spinWaveDuty, 3, 3);
    btnWaveAdd = new QPushButton("➕ 添加/更新通道");
    btnWaveStopAll = new QPushButton("⏹️ 全部暂停/恢复");
    QHBoxLayout *hWaveBtns = new QHBoxLayout();
    hWaveBtns->addWidget(btnWaveAdd);
    hWaveBtns->addWidget(btnWaveStopAll);
    glWave->addLayout(hWaveBtns, 4, 0, 1, 4);
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
    QPushButton *btnExportCsv = new QPushButton("📊 导出寄存器表 (Excel/CSV)");
    QPushButton *btnImportCsv = new QPushButton("📥 导入寄存器表 (Excel/CSV)");
    lsc->addWidget(btnSimSaveScene); lsc->addWidget(btnSimLoadScene); 
    lsc->addWidget(btnExportCsv); lsc->addWidget(btnImportCsv);
    lsc->addStretch();
    tabSimTools->addTab(pageScene, "场景与导入导出");

    connect(btnSimSaveScene, &QPushButton::clicked, this, &MainWindow::onSimSaveSceneClicked);
    connect(btnSimLoadScene, &QPushButton::clicked, this, &MainWindow::onSimLoadSceneClicked);
    connect(btnExportCsv, &QPushButton::clicked, this, &MainWindow::onSimExportCsvClicked);
    connect(btnImportCsv, &QPushButton::clicked, this, &MainWindow::onSimImportCsvClicked);

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
    txtSimLog = new QTextEdit();
    txtSimLog->setReadOnly(true);
    txtSimLog->setStyleSheet("background: #1e1e1e; color: #00ff00; font-family: Monospace;");
    ll->addWidget(txtSimLog);
    gSimLog->setLayout(ll); // 补上这两行，确保布局加入到 GroupBox
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
    connect(btnGitCopyDaily, &QPushButton::clicked, this, &MainWindow::onGitCopyForDailyReportClicked);
    connect(btnScpTransfer, &QPushButton::clicked, this, &MainWindow::onScpTransferClicked);
    
    // Waveform Controls
    connect(btnWaveAdd, &QPushButton::clicked, this, &MainWindow::onSimAddCyclicTimerClicked);
    connect(btnWaveStopAll, &QPushButton::clicked, this, &MainWindow::onSimStopAllWaveformsClicked);
    
    simTickTimer = new QTimer(this);
    connect(simTickTimer, &QTimer::timeout, this, &MainWindow::onSimTimerTick);
    simTickTimer->start(100); 
}

void MainWindow::onSimStopAllWaveformsClicked()
{
    if (simCyclicTimers.isEmpty()) return;

    // 如果当前有任何一个是激活的，则全部停止；如果全都是停止的，则全部激活。
    bool anyActive = false;
    for (const auto &t : simCyclicTimers) if (t.active) { anyActive = true; break; }

    for (int i=0; i<simCyclicTimers.size(); ++i) {
        simCyclicTimers[i].active = !anyActive;
    }
    
    // 刷新日志和界面
    QString status = anyActive ? "全部停止" : "全部启动";
    txtSimLog->append(QString("[%1] 周期波形已%2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(status));
    
    // 强制刷新表格显示
    if (tblWaveChannels) {
        tblWaveChannels->setRowCount(simCyclicTimers.size());
        for (int i=0; i<simCyclicTimers.size(); ++i) {
            const CyclicTimer &ct = simCyclicTimers[i];
            tblWaveChannels->setItem(i, 0, new QTableWidgetItem(ct.device == "Main" ? "主设备" : "AGV"));
            tblWaveChannels->setItem(i, 1, new QTableWidgetItem(QString::number(ct.addr)));
            tblWaveChannels->setItem(i, 2, new QTableWidgetItem(ct.type));
            tblWaveChannels->setItem(i, 3, new QTableWidgetItem(ct.active ? "运行中" : "停止"));
            
            QPushButton *btnRemove = new QPushButton("删除");
            tblWaveChannels->setCellWidget(i, 4, btnRemove);
            connect(btnRemove, &QPushButton::clicked, this, &MainWindow::onSimRemoveCyclicTimerClicked);
        }
    }
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
        connect(btnRemove, &QPushButton::clicked, this, &MainWindow::onSimRemoveCyclicTimerClicked);
    }
}

void MainWindow::onSimRemoveCyclicTimerClicked()
{
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
        } else if (t.type == "来回增减") {
            // 来回增减逻辑：在 offset 到 amplitude 之间循环
            // t.offset 对应最小值，t.amplitude 对应最大值
            double minVal = t.offset;
            double maxVal = t.amplitude;
            if (minVal > maxVal) qSwap(minVal, maxVal);
            double range = maxVal - minVal;
            if (range <= 0) {
                val = minVal;
            } else {
                // cyclePos 从 0 到 1
                double cyclePos = fmod(currentTime + (t.phase / 360.0) * t.period, t.period) / t.period;
                // 0 -> 0.5 增加, 0.5 -> 1.0 减少
                if (cyclePos < 0.5) {
                    val = minVal + (cyclePos * 2.0) * range;
                } else {
                    val = maxVal - ((cyclePos - 0.5) * 2.0) * range;
                }
            }
        }

        ModbusSlave *target = (t.device == "Main") ? simMainDevice : simAGVDevice;
        if (target) {
            // 根据 UI 当前该地址的显示格式决定如何设置寄存器
            QTableWidget *table = (t.device == "Main") ? tblSimMain : tblSimAGV;
            QString fmt = "Unsigned";
            int foundRow = -1;
            if (table) {
                for (int r = 0; r < table->rowCount(); ++r) {
                    QTableWidgetItem *addrItem = table->item(r, 0);
                    if (addrItem && (quint16)addrItem->text().toUInt() == t.addr) {
                        fmt = simTableFormats.value(table).value(r, "Unsigned");
                        foundRow = r;
                        break;
                    }
                }
            }

            if (fmt == "32-bit Float") {
                target->setFloat(t.addr, (float)val);
            } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
                uint32_t val32 = (fmt == "32-bit Signed") ? (uint32_t)(int32_t)val : (uint32_t)val;
                target->setRegister(t.addr, (quint16)(val32 >> 16));
                target->setRegister(t.addr + 1, (quint16)(val32 & 0xFFFF));
            } else {
                // 普通 16 位模式
                quint16 regVal = static_cast<quint16>(qBound(0.0, val, 65535.0));
                target->setRegister(t.addr, regVal);
            }

            // 更新 UI 表格显示
            if (table && foundRow >= 0) {
                refreshSimRowDisplay(table, foundRow);
                // 如果是 32 位格式，还需要刷新下一行
                if (fmt.startsWith("32-bit")) {
                    refreshSimRowDisplay(table, foundRow + 1);
                }
                
                // 检查是否有其他 32 位格式也引用了这些地址（反向同步）
                for (int k = 0; k < table->rowCount(); ++k) {
                    if (k == foundRow) continue;
                    QString fmtk = simTableFormats.value(table).value(k, "Unsigned");
                    if (!fmtk.startsWith("32-bit")) continue;
                    QTableWidgetItem *aItem = table->item(k, 0);
                    if (!aItem) continue;
                    quint16 a = (quint16)aItem->text().toUInt();
                    if (a == t.addr || (quint16)(a + 1) == t.addr) {
                        refreshSimRowDisplay(table, k);
                    }
                }
            }
            
            // 每秒记录一次日志，避免刷新过快 (100ms * 10 = 1s)
            if (t.currentTicks % 10 == 0) {
                QString logMsg = QString("周期更新: %1 地址[%2] -> %3 (类型:%4, 格式:%5)")
                                 .arg(t.device == "Main" ? "主设备" : "AGV")
                                 .arg(t.addr)
                                 .arg(val)
                                 .arg(t.type)
                                 .arg(fmt);
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
        simLastReadValues.remove("主设备");
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
        simLastReadValues.remove("AGV");
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
        simLastReadValues.remove("主设备");
        txtSimLog->append("主设备 停止");
        btnSimStartMain->setEnabled(true);
        btnSimStopMain->setEnabled(false);
        lblSimMainStatus->setText("离线");
        lblSimMainStatus->setStyleSheet("color: red; font-weight: bold;");
    } else if (b == btnSimStopAGV) {
        simAGVDevice->stop();
        simLastReadValues.remove("AGV");
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
    QString initialPath = lastScenePath.isEmpty() ? QDir::currentPath() : lastScenePath;
    QString fn = QFileDialog::getSaveFileName(this, "保存场景", initialPath, "JSON Files (*.json);;All Files (*)");
    if (fn.isEmpty()) return;
    
    // Save path for next time
    lastScenePath = QFileInfo(fn).absolutePath();
    
    QJsonObject root;

    auto exportDeviceHolding = [this](ModbusSlave *dev, QTableWidget *table) {
        QJsonObject obj;
        if (!dev || !table) return obj;
        
        QJsonObject values;
        QJsonObject formats;
        
        // Export only registers that are actually in the UI table to keep size reasonable
        // and ensure we capture what the user sees/configured.
        for (int i = 0; i < table->rowCount(); ++i) {
            QTableWidgetItem *addrItem = table->item(i, 0);
            if (!addrItem) continue;
            bool ok;
            quint16 addr = (quint16)addrItem->text().toUInt(&ok);
            if (!ok) continue;
            
            // Save value if non-zero
            quint16 val = dev->getRegister(addr);
            if (val != 0) {
                values.insert(QString::number(addr), int(val));
            }
            
            // Save format if not default (Unsigned)
            QString fmt = simTableFormats.value(table).value(i, "Unsigned");
            if (fmt != "Unsigned") {
                formats.insert(QString::number(i), fmt);
            }
        }
        obj.insert("values", values);
        obj.insert("formats", formats);
        return obj;
    };

    root.insert("main", exportDeviceHolding(simMainDevice, tblSimMain));
    root.insert("agv", exportDeviceHolding(simAGVDevice, tblSimAGV));

    // Save Waveform Settings (CyclicTimers)
    QJsonArray waveArr;
    for (const CyclicTimer &t : simCyclicTimers) {
        QJsonObject o;
        o.insert("device", t.device);
        o.insert("addr", int(t.addr));
        o.insert("type", t.type);
        o.insert("amplitude", t.amplitude);
        o.insert("offset", t.offset);
        o.insert("period", t.period);
        o.insert("phase", t.phase);
        o.insert("dutyCycle", t.dutyCycle);
        o.insert("active", t.active);
        waveArr.append(o);
    }
    root.insert("waveforms", waveArr);

    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "错误", "无法保存场景文件");
        return;
    }
    QJsonDocument doc(root);
    f.write(doc.toJson());
    f.close();

    int mCount = root.value("main").toObject().value("values").toObject().size();
    int aCount = root.value("agv").toObject().value("values").toObject().size();
    txtSimLog->append(QString("场景已保存: %1 (主:%2个, AGV:%3个)").arg(fn).arg(mCount).arg(aCount));
}

void MainWindow::onSimLoadSceneClicked()
{
    QString initialPath = lastScenePath.isEmpty() ? QDir::currentPath() : lastScenePath;
    QString fn = QFileDialog::getOpenFileName(this, "加载场景", initialPath, "JSON Files (*.json);;All Files (*)");
    if (fn.isEmpty()) return;

    // Save path for next time
    lastScenePath = QFileInfo(fn).absolutePath();

    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "错误", "无法打开场景文件"); return; }
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll()); f.close();
    if (doc.isNull() || !doc.isObject()) { 
        QMessageBox::warning(this, "错误", "场景文件损坏或格式错误"); 
        return; 
    }
    QJsonObject root = doc.object();

    auto loadDeviceHolding = [this](ModbusSlave *dev, QTableWidget *table, QJsonObject &obj) {
        if (!dev || !table) return 0;
        int count = 0;

        // Support both old and new format
        QJsonObject valuesObj;
        if (obj.contains("values") && obj.value("values").isObject()) {
            valuesObj = obj.value("values").toObject();
        } else {
            // Backward compatibility
            valuesObj = obj;
        }

        for (auto it = valuesObj.begin(); it != valuesObj.end(); ++it) {
            quint16 addr = it.key().toUInt();
            quint16 val = (quint16)it.value().toInt();
            dev->setRegister(addr, val);
            count++;
        }
        return count;
    };

    int cAGV = 0, cMain = 0;
    if (root.contains("AGV")) {
        QJsonObject agvObj = root.value("AGV").toObject();
        cAGV = loadDeviceHolding(simAGVDevice, tblSimAGV, agvObj);
    }
    if (root.contains("Main")) {
        QJsonObject mainObj = root.value("Main").toObject();
        cMain = loadDeviceHolding(simMainDevice, tblSimMain, mainObj);
    }

    logMessage(QString("场景加载成功: AGV(%1) 主设备(%2)").arg(cAGV).arg(cMain));
    
    // 强制刷新表格显示
    for(int i=0; i<tblSimAGV->rowCount(); ++i) refreshSimRowDisplay(tblSimAGV, i);
    for(int i=0; i<tblSimMain->rowCount(); ++i) refreshSimRowDisplay(tblSimMain, i);
}

void MainWindow::onSimExportCsvClicked()
{
    QString fn = QFileDialog::getSaveFileName(this, "导出寄存器表", QString(), "CSV Files (*.csv)");
    if (fn.isEmpty()) return;

    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存文件");
        return;
    }

    QTextStream out(&f);
    // BOM for Excel
    out.setGenerateByteOrderMark(true);
    out << "Device,Address,Value,Format,Description\n";

    auto exportTable = [&](QTableWidget *table, const QString &deviceName) {
        if (!table) return;
        for (int i = 0; i < table->rowCount(); ++i) {
            QString addr = table->item(i, 0) ? table->item(i, 0)->text() : "";
            QString val = table->item(i, 1) ? table->item(i, 1)->text() : "";
            QString fmt = simTableFormats.value(table).value(i, "Unsigned");
            QString desc = table->item(i, 2) ? table->item(i, 2)->text() : "";
            
            // CSV escaping
            if (desc.contains(",")) desc = "\"" + desc + "\"";
            
            out << deviceName << "," << addr << "," << val << "," << fmt << "," << desc << "\n";
        }
    };

    exportTable(tblSimAGV, "AGV");
    exportTable(tblSimMain, "Main");

    f.close();
    txtSimLog->append(QString("寄存器表已导出: %1").arg(fn));
}

void MainWindow::onSimImportCsvClicked()
{
    QString fn = QFileDialog::getOpenFileName(this, "导入寄存器表", QString(), "CSV Files (*.csv);;All Files (*)");
    if (fn.isEmpty()) return;

    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QTextStream in(&f);
    in.setCodec("UTF-8");
    QString header = in.readLine(); // skip header
    int count = 0;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        QStringList parts = line.split(",");
        if (parts.size() < 3) continue;

        QString deviceStr = parts[0].trimmed();
        quint16 addr = parts[1].trimmed().toUInt();
        QString valStr = parts[2].trimmed();
        QString fmt = (parts.size() >= 4) ? parts[3].trimmed() : "Unsigned";
        QString desc = (parts.size() >= 5) ? parts[4].trimmed() : "";
        
        // 处理 CSV 中带引号的情况
        if (desc.startsWith("\"") && desc.endsWith("\"")) desc = desc.mid(1, desc.length()-2);

        QTableWidget *table = (deviceStr.toLower() == "main" || deviceStr == "主设备") ? tblSimMain : tblSimAGV;
        ModbusSlave *slave = (deviceStr.toLower() == "main" || deviceStr == "主设备") ? simMainDevice : simAGVDevice;

        if (!table || !slave) continue;

        // 查找或添加行
        int row = -1;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 0) && table->item(r, 0)->text().toUInt() == addr) {
                row = r;
                break;
            }
        }

        if (row == -1) {
            row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QString::number(addr)));
            table->setItem(row, 1, new QTableWidgetItem(valStr));
            table->setItem(row, 2, new QTableWidgetItem(desc));
        } else {
            if (!table->item(row, 1)) table->setItem(row, 1, new QTableWidgetItem(valStr));
            else table->item(row, 1)->setText(valStr);
            
            if (!table->item(row, 2)) table->setItem(row, 2, new QTableWidgetItem(desc));
            else table->item(row, 2)->setText(desc);
        }

        // 更新格式和值
        simTableFormats[table][row] = fmt;
        
        // 特殊处理 32-bit 格式的行锁定逻辑
        if (fmt.startsWith("32-bit") && row + 1 < table->rowCount()) {
            int lockedRow = row + 1;
            simDisabledRowsOwner[table][lockedRow] = row;
            setSimRowEnabled(table, lockedRow, false);
        }
        
        // 尝试解析并设置寄存器值
        bool ok = false;
        if (fmt == "32-bit Float") {
            slave->setFloat(addr, valStr.toFloat(&ok));
        } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
            uint32_t v32 = valStr.toUInt(&ok);
            slave->setRegister(addr, (quint16)(v32 >> 16));
            slave->setRegister(addr + 1, (quint16)(v32 & 0xFFFF));
        } else {
            slave->setRegister(addr, (quint16)valStr.toUInt(&ok));
        }
        
        refreshSimRowDisplay(table, row);
        count++;
    }

    f.close();
    syncSimulatorTablesFromMaps(); // 额外同步一次确保全量更新
    txtSimLog->append(QString("寄存器表已导入: %1 (共 %2 条)").arg(fn).arg(count));
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
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    const QString normalizedOpType = opType.trimmed().toLower();
    const bool isRead = normalizedOpType == "read";
    const bool isWrite = (normalizedOpType == "write" || normalizedOpType == "write_bit");
    entry.insert("timestamp", timeStr);
    
    // Determine source device based on sender
    ModbusSlave *senderDevice = qobject_cast<ModbusSlave*>(sender());
    QString deviceName = "unknown";
    QTableWidget *table = nullptr;
    if (senderDevice == simMainDevice) {
        deviceName = "主设备";
        table = tblSimMain;
    } else if (senderDevice == simAGVDevice) {
        deviceName = "AGV";
        table = tblSimAGV;
    }
    
    entry.insert("device", deviceName);
    entry.insert("address", (int)addr);
    entry.insert("value", (int)value);
    entry.insert("operation", opType);
    
    registerHistory.append(entry);
    
    // 1. 同步到 UI 寄存器表整数列
    if (table) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QTableWidgetItem *addrItem = table->item(r, 0);
            if (addrItem && (quint16)addrItem->text().toUInt() == addr) {
                refreshSimRowDisplay(table, r);
                for (int k = 0; k < table->rowCount(); ++k) {
                    QString fmtk = simTableFormats.value(table).value(k, "Unsigned");
                    if (!fmtk.startsWith("32-bit")) continue;
                    QTableWidgetItem *aItem = table->item(k, 0);
                    if (!aItem) continue;
                    quint16 a = (quint16)aItem->text().toUInt();
                    if (a == addr || (quint16)(a + 1) == addr) {
                        refreshSimRowDisplay(table, k);
                    }
                }
                break;
            }
        }
    }

    // 2. 输出到模拟器运行日志
    if (txtSimLog) {
        bool shouldLog = true;
        if (isRead) {
            QMap<quint16, quint16> &deviceReadValues = simLastReadValues[deviceName];
            shouldLog = !deviceReadValues.contains(addr) || deviceReadValues.value(addr) != value;
            deviceReadValues.insert(addr, value);
        }

        if (shouldLog) {
            const QString actionText = isRead ? "读取" : (isWrite ? "写入" : normalizedOpType);
            const QString arrowText = isRead ? "->" : (isWrite ? "<-" : ":");
            QString logMsg = QString("指令: [%1] %2 地址[%3] %4 %5")
                                .arg(deviceName)
                                .arg(actionText)
                                .arg(addr)
                                .arg(arrowText)
                                .arg(value);
            txtSimLog->append(QString("[%1] %2").arg(timeStr).arg(logMsg));
        }
    }
    
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

#ifdef Q_OS_WIN
    process.setProgram("git.exe");
#else
    process.setProgram("git");
#endif
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
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "branch");
#else
    process.start("git", QStringList() << "branch");
#endif
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
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "log" << "--pretty=format:%h - %cd : %s (%an)" << "--date=short" << "-n" << "20");
#else
    process.start("git", QStringList() << "log" << "--pretty=format:%h - %cd : %s (%an)" << "--date=short" << "-n" << "20");
#endif
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

void MainWindow::onGitCopyForDailyReportClicked() {
    QString today = QDate::currentDate().toString("yyyy-MM-dd");
    QStringList todayCommits;

    // Iterate through all items in the history combo box
    for (int i = 0; i < cmbGitHistory->count(); ++i) {
        QString item = cmbGitHistory->itemText(i);
        // Format: %h - %cd : %s (%an) -> e.g., "a1b2c3d - 2026-03-05 : Fix bug (Author)"
        
        // Extract date (usually between the first " - " and the first " : ")
        int dashIdx = item.indexOf(" - ");
        int colonIdx = item.indexOf(" : ");
        
        if (dashIdx != -1 && colonIdx != -1) {
            QString datePart = item.mid(dashIdx + 3, colonIdx - (dashIdx + 3)).trimmed();
            if (datePart == today) {
                // Extract Subject
                QString subject = item.mid(colonIdx + 3);
                int authorIdx = subject.lastIndexOf(" (");
                if (authorIdx != -1) {
                    subject = subject.left(authorIdx);
                }
                todayCommits.prepend(subject.trimmed()); // prepend to get chronological order (git log is reverse)
            }
        }
    }

    if (todayCommits.isEmpty()) {
        txtGitLog->append("未找到日期为 " + today + " 的提交记录");
        return;
    }

    QString finalContent = todayCommits.join("\n");
    QGuiApplication::clipboard()->setText(finalContent);
    txtGitLog->append(QString("已拼接今日(%1)共 %2 条提交并复制:").arg(today).arg(todayCommits.size()));
    txtGitLog->append(finalContent);
}

void MainWindow::onScpTransferClicked() {
    QString dir = cmbGitDir->currentText();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        txtGitLog->append("错误: 请先选择有效的 Git 目录");
        return;
    }

    QString targetIp = txtScpTargetIp->text().trimmed();
    if (targetIp.isEmpty()) {
        txtGitLog->append("错误: 请输入目标设备地址");
        return;
    }

    QString password = txtScpPassword->text();

    // 递归查找目录下最深层、最新的可执行文件
    QString latestFile;
    QDateTime latestTime;

    QDirIterator it(dir, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fileInfo = it.fileInfo();
        QString fileName = fileInfo.fileName();

        // 过滤掉辅助工具自身和常见的非嵌入式程序
        if (fileName == "ModbusTCPAssistant") continue;
        if (fileName.startsWith(".") || fileName.endsWith(".so")) continue;
        if (fileName.contains(".") && !fileName.endsWith(".sh")) continue;

        if (latestFile.isEmpty() || fileInfo.lastModified() > latestTime) {
            latestFile = fileInfo.absoluteFilePath();
            latestTime = fileInfo.lastModified();
        }
    }

    if (latestFile.isEmpty()) {
        txtGitLog->append("未在目录及其子目录下找到符合条件的可执行文件");
        return;
    }

    QFileInfo fi(latestFile);
    txtGitLog->append(QString("发现最新可执行文件: %1 (修改时间: %2)").arg(fi.fileName()).arg(fi.lastModified().toString()));

    // 检查文件时间与当前时间的差距
    QDateTime now = QDateTime::currentDateTime();
    qint64 diffSeconds = fi.lastModified().secsTo(now);
    if (qAbs(diffSeconds) > 60) {
        QString timeInfo = QString("找到的可执行文件 [%1] 与当前时间相差较大：\n\n"
                                   "文件修改时间: %2\n"
                                   "当前系统时间: %3\n"
                                   "时间差距: %4 分钟 %5 秒\n\n"
                                   "是否确认继续传输？")
                               .arg(fi.fileName())
                               .arg(fi.lastModified().toString("yyyy-MM-dd HH:mm:ss"))
                               .arg(now.toString("yyyy-MM-dd HH:mm:ss"))
                               .arg(qAbs(diffSeconds) / 60)
                               .arg(qAbs(diffSeconds) % 60);
        
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "文件时间检查", timeInfo, 
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            txtGitLog->append("用户取消传输：文件时间不匹配");
            return;
        }
    }

    // 执行停止并传输命令
    // 使用 sshpass 处理密码，增加 -o StrictHostKeyChecking=no 避免指纹验证阻塞
    QString fileName = fi.fileName();
    QString remotePath = QString("/userfs/app/%1").arg(fileName);
    QString stopCmd = QString("pkill -9 %1").arg(fileName);
    QString runCmd = QString("chmod +x %1 && %1 &").arg(remotePath);

    QString fullRemoteCmd;
    if (!password.isEmpty()) {
        fullRemoteCmd = QString("sshpass -p %1 ssh -o StrictHostKeyChecking=no root@%2 \"%3; exit 0\"").arg(password).arg(targetIp).arg(stopCmd);
    } else {
        fullRemoteCmd = QString("ssh -o StrictHostKeyChecking=no root@%2 \"%3; exit 0\"").arg(targetIp).arg(stopCmd);
    }

    txtGitLog->append(QString("正在停止目标程序: %1 ...").arg(fileName));
    
    QProcess *stopProcess = new QProcess(this);
    connect(stopProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, stopProcess, fileName, password, targetIp, latestFile, remotePath, runCmd](int, QProcess::ExitStatus) {
        stopProcess->deleteLater();
        
        // 停止命令执行完（无论成功失败，可能程序本就没运行），开始传输
        QString scpProgram;
        QStringList scpArgs;

        if (!password.isEmpty()) {
            scpProgram = "sshpass";
            scpArgs << "-p" << password << "scp" << "-o" << "StrictHostKeyChecking=no" << latestFile << QString("root@%1:/userfs/app").arg(targetIp);
        } else {
            scpProgram = "scp";
            scpArgs << "-o" << "StrictHostKeyChecking=no" << latestFile << QString("root@%1:/userfs/app").arg(targetIp);
        }

        txtGitLog->append(QString("正在传输文件: %1 ...").arg(fileName));

        QProcess *scpProcess = new QProcess(this);
        connect(scpProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, scpProcess, fileName](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                txtGitLog->append(QString("传输成功: %1 已上传至 /userfs/app").arg(fileName));
            } else {
                QString error = scpProcess->readAllStandardError();
                txtGitLog->append(QString("传输失败 (退出码 %1): %2").arg(exitCode).arg(error));
            }
            scpProcess->deleteLater();
        });
        scpProcess->start(scpProgram, scpArgs);
    });

    stopProcess->start("sh", QStringList() << "-c" << fullRemoteCmd);
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
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels(QStringList() << "地址" << "描述" << "值");
    
    // 重新规划列表宽度
    table->setColumnWidth(0, 50);  // 地址列
    table->setColumnWidth(1, 150); // 描述列
    table->setColumnWidth(2, 100); // 值列

    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); // 描述列自适应剩余空间
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &MainWindow::onSimShowContextMenu);

    table->setRowCount(50);

    for (int i = 0; i < 50; ++i) {
        if (!table->item(i, 0)) table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        if (!table->item(i, 1)) table->setItem(i, 1, new QTableWidgetItem(""));
        if (!table->item(i, 2)) table->setItem(i, 2, new QTableWidgetItem("0"));
    }
    connect(table, &QTableWidget::cellChanged, this, &MainWindow::onSimTableRowChanged);
}

void MainWindow::refreshSimRowDisplay(QTableWidget *table, int row)
{
    if (!table) return;
    if (row < 0 || row >= table->rowCount()) return;

    QTableWidgetItem *addrItem = table->item(row, 0);
    if (!addrItem || addrItem->text().isEmpty()) return;

    ModbusSlave *target = (table == tblSimAGV) ? simAGVDevice : simMainDevice;
    if (!target) return;

    quint16 addr = (quint16)addrItem->text().toUInt();
    quint16 val = target->getRegister(addr);

    QString fmt = simTableFormats.value(table).value(row, "Unsigned");
    QString display;
    if (fmt == "Hex") display = "0x" + QString::number(val, 16).toUpper().rightJustified(4, '0');
    else if (fmt == "Binary") display = "0b" + QString::number(val, 2).rightJustified(16, '0');
    else if (fmt == "Signed") display = QString::number((int16_t)val);
    else if (fmt == "ASCII - Hex") {
        char c1 = (char)((val >> 8) & 0xFF);
        char c2 = (char)(val & 0xFF);
        display = QString("'%1%2'").arg(c1 > 31 ? QChar(c1) : '.').arg(c2 > 31 ? QChar(c2) : '.');
    } else if (fmt.startsWith("32-bit")) {
        uint32_t val32 = ((uint32_t)target->getRegister(addr) << 16) | target->getRegister(addr + 1);
        if (fmt == "32-bit Signed") display = QString::number((int32_t)val32);
        else if (fmt == "32-bit Unsigned") display = QString::number(val32);
        else if (fmt == "32-bit Float") {
            float f;
            memcpy(&f, &val32, 4);
            display = QString::number(f, 'f', 2);
        }
    }
    if (display.isEmpty()) display = QString::number(val);

    table->blockSignals(true);
    if (!table->item(row, 2)) table->setItem(row, 2, new QTableWidgetItem());
    table->item(row, 2)->setText(display);
    table->blockSignals(false);
}

void MainWindow::setSimRowEnabled(QTableWidget *table, int row, bool enabled)
{
    if (!table) return;
    if (row < 0 || row >= table->rowCount()) return;

    for (int col = 0; col < table->columnCount(); ++col) {
        if (!table->item(row, col)) table->setItem(row, col, new QTableWidgetItem());
        QTableWidgetItem *item = table->item(row, col);
        Qt::ItemFlags flags = item->flags();
        if (enabled) {
            flags |= (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (col == 2) flags |= Qt::ItemIsEditable;
            else flags &= ~Qt::ItemIsEditable;
            item->setForeground(QBrush(Qt::black));
        } else {
            flags &= ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            item->setForeground(QBrush(Qt::gray));
        }
        item->setFlags(flags);
    }
}

void MainWindow::onSimTableRowChanged(int row, int column)
{
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;
    ModbusSlave *target = (table == tblSimAGV) ? simAGVDevice : simMainDevice;
    
    QTableWidgetItem *addrItem = table->item(row, 0);
    if (!addrItem || addrItem->text().isEmpty()) return;
    quint16 addr = (quint16)addrItem->text().toUInt();
    
    if (column == 2) { 
        QString valStr = table->item(row, 2)->text();
        bool ok = false;
        quint16 val = 0;
        
        QString fmt = simTableFormats.value(table).value(row, "Unsigned");

        if (fmt == "32-bit Float") {
            float f = valStr.toFloat(&ok);
            if (ok) {
                target->setFloat(addr, f);
                // 强制刷新当前行和下一行（因为 setFloat 修改了两个寄存器）
                refreshSimRowDisplay(table, row);
                refreshSimRowDisplay(table, row + 1);
            }
        } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
            bool ok32 = false;
            uint32_t val32 = 0;
            if (fmt == "32-bit Signed") val32 = (uint32_t)valStr.toInt(&ok32);
            else val32 = valStr.toUInt(&ok32);

            if (ok32) {
                target->setRegister(addr, (quint16)(val32 >> 16));
                target->setRegister(addr + 1, (quint16)(val32 & 0xFFFF));
                refreshSimRowDisplay(table, row);
                refreshSimRowDisplay(table, row + 1);
                ok = true; // 标记为已处理
            }
        } else {
            // 原有的 16 位逻辑
            if (valStr.startsWith("0x", Qt::CaseInsensitive)) {
                val = (quint16)valStr.toUInt(&ok, 16);
            } else if (valStr.startsWith("0b", Qt::CaseInsensitive)) {
                val = (quint16)valStr.mid(2).toUInt(&ok, 2);
            } else {
                val = (quint16)valStr.toUInt(&ok, 10);
                if (!ok) val = (quint16)valStr.toInt(&ok, 10);
            }

            if (ok) {
                target->setRegister(addr, val);
                refreshSimRowDisplay(table, row);
            }
        }

        if (!ok && fmt != "32-bit Float" && !fmt.startsWith("32-bit")) {
            // 如果解析失败且不是32位格式，还原显示
            refreshSimRowDisplay(table, row);
        }
        
        // 旧有的 Float 逻辑适配（如果有第4列）
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
            if (!dst->item(row, 2)) dst->setItem(row, 2, new QTableWidgetItem("0"));

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

void MainWindow::onSimShowContextMenu(const QPoint &pos) {
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;
    QModelIndex index = table->indexAt(pos);
    if (!index.isValid()) return;
    int row = index.row();

    if (simDisabledRowsOwner.value(table).contains(row)) {
        int owner = simDisabledRowsOwner.value(table).value(row);
        if (txtSimLog) {
            txtSimLog->append(QString("行 %1 已被行 %2 的 32-bit 格式占用，请先修改行 %2 的格式").arg(row).arg(owner));
        }
        return;
    }

    QMenu menu(this);
    
    // Format Submenu
    QMenu *formatMenu = menu.addMenu("Format");
    QStringList formats = {"Signed", "Unsigned", "Hex", "ASCII - Hex", "Binary"};
    for (const QString &fmt : formats) {
        QAction *a = formatMenu->addAction(fmt);
        connect(a, &QAction::triggered, this, [this, table, row, fmt](){ 
            QString oldFmt = simTableFormats.value(table).value(row, "Unsigned");
            if (oldFmt.startsWith("32-bit")) {
                int lockedRow = row + 1;
                simDisabledRowsOwner[table].remove(lockedRow);
                setSimRowEnabled(table, lockedRow, true);
                refreshSimRowDisplay(table, lockedRow);
            }
            simTableFormats[table][row] = fmt;
            refreshSimRowDisplay(table, row);
        });
    }
    formatMenu->addSeparator();
    QAction *af32 = formatMenu->addAction("32-bit Float");
    connect(af32, &QAction::triggered, this, [this, table, row](){ 
        QString fmt = "32-bit Float";
        if (row + 1 >= table->rowCount()) {
            if (txtSimLog) txtSimLog->append(QString("行 %1 无法设置 %2：缺少下一行").arg(row).arg(fmt));
            return;
        }

        QString oldFmt = simTableFormats.value(table).value(row, "Unsigned");
        if (oldFmt.startsWith("32-bit")) {
            int prevLockedRow = row + 1;
            simDisabledRowsOwner[table].remove(prevLockedRow);
            setSimRowEnabled(table, prevLockedRow, true);
            refreshSimRowDisplay(table, prevLockedRow);
        }

        simTableFormats[table][row] = fmt;
        int lockedRow = row + 1;
        simDisabledRowsOwner[table][lockedRow] = row;
        setSimRowEnabled(table, lockedRow, false);
        refreshSimRowDisplay(table, row);
    });

    menu.addSeparator();
    QAction *actBit = menu.addAction("bit Edit");
    connect(actBit, &QAction::triggered, this, [this, row](){ onSimShowBitEditor(row); });

    QAction *actWave = menu.addAction("periodic waveformation");
    connect(actWave, &QAction::triggered, this, [this, row](){ onSimShowWaveformEditor(row); });

    menu.exec(table->viewport()->mapToGlobal(pos));
}

void MainWindow::onSimSetFormat(const QString &format) {
    // 转发请求或更新全局设置即可，这里简单记录日志，实际由刷新函数处理
    txtSimLog->append(QString("格式切换为: %1").arg(format));
    // 假设您已实现 cmbDisplayFormat 或类似逻辑，可在此同步
    if (cmbDisplayFormat) {
        int idx = cmbDisplayFormat->findText(format);
        if (idx >= 0) cmbDisplayFormat->setCurrentIndex(idx);
    }
}

void MainWindow::onSimShowWaveformEditor(int row) {
    QTableWidget *table = (tabSimRegisterMaps->currentIndex() == 0) ? tblSimAGV : tblSimMain;
    QTableWidgetItem *addrItem = table->item(row, 0);
    if (!addrItem) return;
    int addr = addrItem->text().toInt();

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle(QString("地址 %1 周期波形配置").arg(addr));
    dlg->setMinimumSize(400, 300); // 设置最小大小确保显示
    QVBoxLayout *v = new QVBoxLayout(dlg);
    
    QGroupBox *group = new QGroupBox("波形参数", dlg);
    QGridLayout *g = new QGridLayout(group);
    
    QString currentFmt = simTableFormats.value(table).value(row, "Unsigned");
    bool isFloat32 = (currentFmt == "32-bit Float");

    g->addWidget(new QLabel("类型:"), 0, 0);
    QComboBox *cbType = new QComboBox();
    cbType->addItems(QStringList() << "正弦波" << "方波" << "三角波" << "锯齿波" << "随机" << "来回增减");
    g->addWidget(cbType, 0, 1);
    
    QLabel *lblAmp = new QLabel("幅度(或最大):");
    g->addWidget(lblAmp, 1, 0);
    QDoubleSpinBox *spAmp = new QDoubleSpinBox(); 
    spAmp->setRange(-1e9, 1e9); 
    spAmp->setValue(isFloat32 ? 10.0 : 1000.0);
    g->addWidget(spAmp, 1, 1);
    
    g->addWidget(new QLabel("周期(s):"), 2, 0);
    QDoubleSpinBox *spPer = new QDoubleSpinBox(); spPer->setRange(0.1, 3600); spPer->setValue(2.0);
    g->addWidget(spPer, 2, 1);

    QLabel *lblOff = new QLabel("偏移(或最小):");
    g->addWidget(lblOff, 3, 0);
    QDoubleSpinBox *spOff = new QDoubleSpinBox(); 
    spOff->setRange(-1e9, 1e9); 
    spOff->setValue(0);
    g->addWidget(spOff, 3, 1);

    // 切换类型时自动更新标签提示
    connect(cbType, &QComboBox::currentTextChanged, [=](const QString &text){
        if (text == "来回增减") {
            lblAmp->setText("最大值:");
            lblOff->setText("最小值:");
        } else {
            lblAmp->setText("幅度:");
            lblOff->setText("偏移:");
        }
    });
    
    v->addWidget(group);
    
    QHBoxLayout *hButtons = new QHBoxLayout();
    QPushButton *btnOk = new QPushButton("开始生成");
    QPushButton *btnCancel = new QPushButton("取消");
    hButtons->addStretch();
    hButtons->addWidget(btnOk);
    hButtons->addWidget(btnCancel);
    v->addLayout(hButtons);
    
    connect(btnOk, &QPushButton::clicked, dlg, [=](){
        CyclicTimer t;
        t.device = (tabSimRegisterMaps->currentIndex() == 0) ? "AGV" : "Main";
        t.addr = (quint16)addr;
        t.type = cbType->currentText();
        t.amplitude = spAmp->value();
        t.offset = spOff->value();
        t.period = spPer->value();
        t.phase = 0.0;
        t.dutyCycle = 0.5;
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

        // 如果全局 UI 的波形表格已初始化，刷新它
        if (tblWaveChannels) {
            onSimAddCyclicTimerClicked();
        }

        txtSimLog->append(QString("地址 %1 开始生成 %2 (格式: %3, 幅度:%4, 周期:%5s)")
            .arg(addr).arg(cbType->currentText()).arg(currentFmt).arg(spAmp->value()).arg(spPer->value()));
        dlg->accept();
    });
    connect(btnCancel, &QPushButton::clicked, dlg, &QDialog::reject);
    
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::saveAutoScene()
{
    QJsonObject root;
    auto exportDeviceHolding = [this](ModbusSlave *dev, QTableWidget *table) {
        QJsonObject obj;
        if (!dev || !table) return obj;
        QJsonObject values, formats;
        for (int i = 0; i < table->rowCount(); ++i) {
            QTableWidgetItem *addrItem = table->item(i, 0);
            if (!addrItem) continue;
            bool ok;
            quint16 addr = (quint16)addrItem->text().toUInt(&ok);
            if (!ok) continue;
            quint16 val = dev->getRegister(addr);
            if (val != 0) values.insert(QString::number(addr), int(val));
            QString fmt = simTableFormats.value(table).value(i, "Unsigned");
            if (fmt != "Unsigned") formats.insert(QString::number(i), fmt);
        }
        obj.insert("values", values);
        obj.insert("formats", formats);
        return obj;
    };
    root.insert("main", exportDeviceHolding(simMainDevice, tblSimMain));
    root.insert("agv", exportDeviceHolding(simAGVDevice, tblSimAGV));
    
    QJsonArray waveArr;
    for (const CyclicTimer &t : simCyclicTimers) {
        QJsonObject o;
        o.insert("device", t.device); o.insert("addr", int(t.addr)); o.insert("type", t.type);
        o.insert("amplitude", t.amplitude); o.insert("offset", t.offset); o.insert("period", t.period);
        o.insert("phase", t.phase); o.insert("dutyCycle", t.dutyCycle); o.insert("active", t.active);
        waveArr.append(o);
    }
    root.insert("waveforms", waveArr);
    
    QFile f("autoscene.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.close();
    }
}

void MainWindow::loadAutoScene()
{
    QFile f("autoscene.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll()); f.close();
    if (doc.isNull() || !doc.isObject()) return;
    QJsonObject root = doc.object();

    auto loadDeviceHolding = [this](ModbusSlave *dev, QTableWidget *table, QJsonObject &obj) {
        if (!dev || !table) return;
        QJsonObject valuesObj = obj.contains("values") ? obj.value("values").toObject() : obj;
        for (auto it = valuesObj.begin(); it != valuesObj.end(); ++it) {
            dev->setRegister(it.key().toUInt(), (quint16)it.value().toInt());
        }
        if (obj.contains("formats")) {
            QJsonObject formatsObj = obj.value("formats").toObject();
            for (auto it = formatsObj.begin(); it != formatsObj.end(); ++it) {
                int row = it.key().toInt();
                QString fmt = it.value().toString();
                simTableFormats[table][row] = fmt;
                if (fmt.startsWith("32-bit") && row+1 < table->rowCount()) {
                    simDisabledRowsOwner[table][row+1] = row;
                    setSimRowEnabled(table, row+1, false);
                }
                refreshSimRowDisplay(table, row);
            }
        }
    };
    if (root.contains("main")) {
        QJsonObject mainObj = root.value("main").toObject();
        loadDeviceHolding(simMainDevice, tblSimMain, mainObj);
    }
    if (root.contains("agv")) {
        QJsonObject agvObj = root.value("agv").toObject();
        loadDeviceHolding(simAGVDevice, tblSimAGV, agvObj);
    }
    
    if (root.contains("waveforms")) {
        simCyclicTimers.clear();
        QJsonArray waveArr = root.value("waveforms").toArray();
        for (auto v : waveArr) {
            QJsonObject o = v.toObject();
            CyclicTimer t;
            t.device = o.value("device").toString(); t.addr = (quint16)o.value("addr").toInt();
            t.type = o.value("type").toString(); t.amplitude = o.value("amplitude").toDouble();
            t.offset = o.value("offset").toDouble(); t.period = o.value("period").toDouble();
            t.phase = o.value("phase").toDouble(); t.dutyCycle = o.value("dutyCycle").toDouble();
            t.currentTicks = 0; t.active = o.value("active").toBool();
            simCyclicTimers.append(t);
        }
        if (tblWaveChannels) {
            tblWaveChannels->setRowCount(simCyclicTimers.size());
            for (int i=0; i<simCyclicTimers.size(); ++i) {
                const auto &ct = simCyclicTimers[i];
                tblWaveChannels->setItem(i, 0, new QTableWidgetItem(ct.device=="Main"?"主设备":"AGV"));
                tblWaveChannels->setItem(i, 1, new QTableWidgetItem(QString::number(ct.addr)));
                tblWaveChannels->setItem(i, 2, new QTableWidgetItem(ct.type));
                tblWaveChannels->setItem(i, 3, new QTableWidgetItem(ct.active?"运行中":"停止"));
                QPushButton *btn = new QPushButton("删除");
                tblWaveChannels->setCellWidget(i, 4, btn);
                connect(btn, &QPushButton::clicked, this, &MainWindow::onSimRemoveCyclicTimerClicked);
            }
        }
    }
}
