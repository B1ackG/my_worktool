#include "mainwindow.h"
#include <QMessageBox>
#include <QInputDialog>
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
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QTextStream>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QUuid>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QEventLoop>
#include <QRadioButton>
#include <QButtonGroup>
#include <functional>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , tcpSocket(nullptr)
    , continuousTimer(nullptr)
    , transactionId(0)
    , displayFormat(FormatDecimal)
    , simMainDevice(nullptr)
    , simAGVDevice(nullptr)
    , serialPort(nullptr)
    , monitorTimer(nullptr)
    , currentMonitoringPid(-1)
    , prevProcJiffies(0)
    , prevTotalJiffies(0)
    , hasPrevCpuSample(false)
    , monitorFile(nullptr)
    , monitorStream(nullptr)
    , cpuThresholdValue(90.0)
{
    setWindowTitle("李晨阳的linux工作助手");
    resize(1200, 720);

    // Initialize Objects
    tcpSocket = new QTcpSocket(this);
    continuousTimer = new QTimer(this);
    serialPort = new QSerialPort(this);
    simMainDevice = new ModbusSlave(this);
    simAGVDevice = new ModbusSlave(this);
    monitorTimer = new QTimer(this);
    tcpServer = new QTcpServer(this);
    tcpAssistantSocket = nullptr; // Initialize in connect/onNewConnection
    tcpCyclicTimer = new QTimer(this);

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
    navWidget->addItem("TCP 通讯助手");
    navWidget->addItem("性能监控器");
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

    btnExportRegisterMap = new QPushButton("导出");
    btnImportRegisterMap = new QPushButton("导入");
    btnImportStandardFile = new QPushButton("导入标准文件");
    txtSearchMap = new QLineEdit();
    txtSearchMap->setPlaceholderText("搜索内容...");
    txtSearchMap->setClearButtonEnabled(true);
    btnSearchMap = new QPushButton("搜索");

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
    cmbDisplayFormat->addItems(QStringList() << "十进制" << "十六进制" << "二进制" << "32位浮点数" << "64位浮点数");

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

    lblWriteFormat = new QLabel("格式:");
    cmbWriteFormat = new QComboBox();
    cmbWriteFormat->addItems(QStringList() << "十进制" << "十六进制" << "二进制" << "32位浮点数" << "64位浮点数");

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

    // --- Page 5: TCP Assistant Widgets ---
    cmbTcpMode = new QComboBox();
    cmbTcpMode->addItems(QStringList() << "TCP Server" << "TCP Client");
    
    lblTcpLocalIP = new QLabel("本地IP: 0.0.0.0");
    txtTcpLocalPort = new QLineEdit("6000");
    txtTcpLocalPort->setValidator(new QIntValidator(1, 65535, this));
    
    txtTcpRemoteIP = new QLineEdit("127.0.0.1");
    txtTcpRemotePort = new QLineEdit("6000");
    txtTcpRemotePort->setValidator(new QIntValidator(1, 65535, this));
    
    btnTcpConnect = new QPushButton("监听"); // For Server
    btnTcpDisconnect = new QPushButton("停止");
    btnTcpDisconnect->setEnabled(false);
    lblTcpStatus = new QLabel("未运行");
    lblTcpStatus->setStyleSheet("color: red; font-weight: bold;");
    
    txtTcpRecv = new QTextEdit();
    txtTcpRecv->setReadOnly(true);
    txtTcpSend = new QTextEdit();
    txtTcpSend->setMaximumHeight(100);
    
    chkTcpHexRecv = new QCheckBox("Hex显示");
    chkTcpHexSend = new QCheckBox("Hex发送");
    chkTcpCyclicSend = new QCheckBox("循环发送");
    spinTcpInterval = new QSpinBox();
    spinTcpInterval->setRange(10, 60000);
    spinTcpInterval->setValue(1000);
    
    btnTcpSend = new QPushButton("发送");
    btnTcpClearRecv = new QPushButton("清空接收");

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
    btnGitRemoveHistory = new QPushButton("删除记忆");
    btnGitRemoveHistory->setToolTip("从记忆列表中移除当前选中的仓库路径（不删除磁盘目录）");

    tblGitGoals = new QTableWidget();
    tblGitGoals->setColumnCount(8);
    tblGitGoals->setHorizontalHeaderLabels(
        QStringList() << "" << "目标" << "父目标" << "开始日期" << "计划结束" << "实际完成" << "分支" << "备注");
    tblGitGoals->setColumnWidth(0, 32);
    tblGitGoals->horizontalHeader()->setStretchLastSection(true);
    tblGitGoals->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblGitGoals->setSelectionMode(QAbstractItemView::SingleSelection);
    tblGitGoals->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblGitGoals->verticalHeader()->setVisible(false);
    tblGitGoals->setMinimumHeight(120);

    btnGitGoalAdd = new QPushButton("添加目标");
    btnGitGoalEdit = new QPushButton("编辑目标");
    btnGitGoalDelete = new QPushButton("删除目标");
    btnGitGoalStart = new QPushButton("目标开始");
    btnGitGoalStart->setToolTip("记录开始日期并补齐上级未填写的开始日期；可翻译分支名并选择是否创建 Git 分支");
    btnGitGoalStart->setStyleSheet("background-color: #c8e6c9; font-weight: bold;");

    cmbGitBranches = new QComboBox();
    btnGitRefreshBranches = new QPushButton("刷新分支");
    btnGitCheckout = new QPushButton("切换分支");
    btnGitSyncRemote = new QPushButton("同步远程");
    btnGitSyncRemote->setToolTip("将选中的远程分支同步并签出到本地");
    btnGitSyncRemote->setStyleSheet("background-color: #e3f2fd; font-weight: bold;");
    btnGitCreateBranch = new QPushButton("创建分支");
    btnGitDeleteBranch = new QPushButton("删除分支");
    
    txtGitCommitMsg = new QLineEdit();
    txtGitCommitMsg->setPlaceholderText("Git Commit Message...");
    
    cmbGitRemote = new QComboBox();
    cmbGitRemote->addItem("origin");
    cmbGitRemote->addItem("github");
    cmbGitRemote->setEditable(true); // Allow custom remotes
    
    btnGitAdd = new QPushButton("git add . (暂存全部)");
    btnGitCommit = new QPushButton("git commit (提交)");
    btnGitPush = new QPushButton("git push (推送)");
    btnGitPull = new QPushButton("git pull (拉取)");
    btnGitMerge = new QPushButton("git merge (合并)");
    btnGitStatus = new QPushButton("git status (状态)");
    btnGitDiff = new QPushButton("git diff (差异)");
    btnGitDiff->setToolTip("显示工作区与暂存区的差异");
    btnGitFetch = new QPushButton("git fetch --prune (同步远端)");
    btnGitStash = new QPushButton("git stash (临时存档)");
    btnGitStashPop = new QPushButton("git stash pop (恢复临存)");
    btnGitSetDiffRule = new QPushButton("设置Diff提醒标准");
    btnGitAutoDiffReminder = new QPushButton("开启每小时Diff提醒");
    btnGitAutoDiffReminder->setCheckable(true);
    btnGitAutoDiffReminder->setStyleSheet("background-color: #fff3cd; font-weight: bold;");
    spinGitDiffIntervalMinutes = new QSpinBox();
    spinGitDiffIntervalMinutes->setRange(1, 24 * 60);
    spinGitDiffIntervalMinutes->setValue(60);
    spinGitDiffIntervalMinutes->setSuffix(" 分钟");
    spinGitDiffFileThreshold = new QSpinBox();
    spinGitDiffFileThreshold->setRange(1, 5000);
    spinGitDiffFileThreshold->setValue(5);
    spinGitDiffLineThreshold = new QSpinBox();
    spinGitDiffLineThreshold->setRange(1, 200000);
    spinGitDiffLineThreshold->setValue(100);
    btnGitOpenIgnore = new QPushButton("管理 .gitignore");
    btnGitGetSshKey = new QPushButton("获取SSH公钥"); 
    btnGitGetSshKey->setToolTip("获取本机 SSH 公钥并复制到剪贴板，用于上传 GitHub");
    btnGitGetSshKey->setStyleSheet("background-color: #fce4ec; font-weight: bold;"); // 浅粉色

    btnGitRemoteAdd = new QPushButton("链接远程仓库");
    btnGitRemoteAdd->setToolTip("为本地目录添加远程仓库链接 (git remote add)");
    btnGitRemoteAdd->setStyleSheet("background-color: #e8f5e9; font-weight: bold;"); // 浅绿色

    btnGitCheckIgnore = new QPushButton("检查 .gitignore");
    btnGitCheckIgnore->setToolTip("检查是否存在常用的 .gitignore 规则");

    btnGitWorktreeList = new QPushButton("Worktree 列表");
    btnGitWorktreeList->setToolTip("列出当前仓库的所有 git worktree (git worktree list)");
    btnGitWorktreeList->setStyleSheet("background-color: #e0f7fa; font-weight: bold;");

    btnGitWorktreeAdd = new QPushButton("添加 Worktree");
    btnGitWorktreeAdd->setToolTip("基于当前分支添加一个新的工作树目录 (git worktree add)");
    btnGitWorktreeAdd->setStyleSheet("background-color: #e0f7fa; font-weight: bold;");

    btnGitWorktreeRemove = new QPushButton("移除 Worktree");
    btnGitWorktreeRemove->setToolTip("安全移除一个工作树目录并清理引用 (git worktree remove)");
    btnGitWorktreeRemove->setStyleSheet("background-color: #ffebee; font-weight: bold;");

    btnGitWorktreePrune = new QPushButton("清理 Worktree");
    btnGitWorktreePrune->setToolTip("清理已失效的工作树引用 (git worktree prune)");
    btnGitWorktreePrune->setStyleSheet("background-color: #e0f7fa; font-weight: bold;");
    
    cmbGitHistory = new QComboBox();
    btnGitRefreshLog = new QPushButton("刷新历史");
    btnGitReset = new QPushButton("硬回退 (Reset)");
    btnGitReset->setStyleSheet("color: red; font-weight: bold;");
    
    btnGitSoftReset = new QPushButton("软回退 (SoftReset)");
    btnGitSoftReset->setStyleSheet("color: #FF8C00; font-weight: bold;");
    btnGitSoftReset->setToolTip("执行 git reset --soft HEAD^ (撤回最后一次提交，保留代码修改)");

    btnGitCopyDaily = new QPushButton("复制到日报");
    btnGitCopyDaily->setStyleSheet("background-color: #d1f2eb; font-weight: bold;");

    txtScpTargetIp = new QLineEdit("192.168.1.245");
    txtScpTargetIp->setPlaceholderText("目标设备地址");
    txtScpPassword = new QLineEdit();
    txtScpPassword->setPlaceholderText("SSH 密码");
    txtScpPassword->setEchoMode(QLineEdit::Password);
    btnScpTransfer = new QPushButton("搜索并传输(全目录层级)");
    btnScpTransfer->setStyleSheet("background-color: #fce4ec; font-weight: bold;");
    
    btnRebootTarget = new QPushButton("重启目标");
    btnRebootTarget->setStyleSheet("background-color: #ffccbc; font-weight: bold; color: #d84315;");

    // --- Page 6: Performance Monitor Widgets ---
    btnTogglePerfMonitor = new QPushButton("开始监控本机性能");
    btnTogglePerfMonitor->setCheckable(true);
    btnTogglePerfMonitor->setStyleSheet("font-weight: bold; min-height: 40px;");
    
    chartLocalCpu = new MonitorChart();
    chartLocalCpu->setMinimumHeight(200);
    chartLocalMem = new MonitorChart();
    chartLocalMem->setMinimumHeight(200);
    
    lblLocalCpu = new QLabel("CPU 使用率: 0%");
    lblLocalCpu->setStyleSheet("font-size: 16px; font-weight: bold; color: #008080;");
    lblLocalMem = new QLabel("内存 使用率: 0%");
    lblLocalMem->setStyleSheet("font-size: 16px; font-weight: bold; color: #008080;");
    
    txtPerfLog = new QTextEdit();
    txtPerfLog->setReadOnly(true);
    txtPerfLog->setPlaceholderText("性能监控日志 (检测到异常卡顿时会在此记录)...");

    perfTimer = new QTimer(this);
    perfTimer->setInterval(1000);
    hasLastPerfSample = false;
    lastTotalUser = lastTotalUserLow = lastTotalSys = lastTotalIdle = 0;

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
    
    QHBoxLayout *wFormat = new QHBoxLayout();
    wFormat->addWidget(lblWriteFormat); wFormat->addWidget(cmbWriteFormat);

    QGridLayout *w4 = new QGridLayout();
    w4->addWidget(btnWriteSingleCoil, 0, 0); w4->addWidget(btnWriteSingleRegister, 0, 1);
    w4->addWidget(btnWriteMultipleCoils, 1, 0); w4->addWidget(btnWriteMultipleRegisters, 1, 1);
    
    layWrite->addLayout(w1); layWrite->addLayout(w2); layWrite->addLayout(w3); layWrite->addLayout(wFormat); layWrite->addLayout(w4);
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
    
    QHBoxLayout *layMapBtns = new QHBoxLayout();
    layMapBtns->addWidget(txtSearchMap);
    layMapBtns->addWidget(btnSearchMap);
    layMapBtns->addWidget(btnExportRegisterMap);
    layMapBtns->addWidget(btnImportRegisterMap);
    layMapBtns->addWidget(btnImportStandardFile);
    chkAutoReadOnMapClick = new QCheckBox("点击自动读取");
    chkAutoWriteOnMapClick = new QCheckBox("点击自动写入");
    layMapBtns->addWidget(chkAutoReadOnMapClick);
    layMapBtns->addWidget(chkAutoWriteOnMapClick);
    layMapBtns->addStretch();
    
    layMaps->addLayout(layMapBtns);
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
    layRepo->addWidget(btnGitRemoveHistory);
    grpRepo->setLayout(layRepo);
    layout->addWidget(grpRepo);

    QGroupBox *grpGoals = new QGroupBox("工作目标（按当前仓库目录分开保存）");
    QVBoxLayout *layGoals = new QVBoxLayout();
    layGoals->addWidget(tblGitGoals);
    QHBoxLayout *layGoalBtns = new QHBoxLayout();
    layGoalBtns->addWidget(btnGitGoalAdd);
    layGoalBtns->addWidget(btnGitGoalEdit);
    layGoalBtns->addWidget(btnGitGoalDelete);
    layGoalBtns->addWidget(btnGitGoalStart);
    layGoalBtns->addStretch();
    layGoals->addLayout(layGoalBtns);
    grpGoals->setLayout(layGoals);
    layout->addWidget(grpGoals);
    
    // 2. Branch & Actions
    QGroupBox *grpOps = new QGroupBox("Git 操作");
    QVBoxLayout *layOps = new QVBoxLayout();
    
    // Branch Selection
    QHBoxLayout *layBranch = new QHBoxLayout();
    layBranch->addWidget(new QLabel("当前/目标分支:"));
    layBranch->addWidget(cmbGitBranches, 1);
    layBranch->addWidget(btnGitRefreshBranches);
    layBranch->addWidget(btnGitCheckout);
    layBranch->addWidget(btnGitSyncRemote);
    layBranch->addWidget(btnGitCreateBranch);
    layBranch->addWidget(btnGitDeleteBranch);
    layOps->addLayout(layBranch);
    
    // Commit Msg
    QHBoxLayout *layCommit = new QHBoxLayout();
    layCommit->addWidget(new QLabel("提交信息:"));
    layCommit->addWidget(txtGitCommitMsg, 1);
    layOps->addLayout(layCommit);
    
    // Common actions grouped by scenario
    QGridLayout *layBtns = new QGridLayout();
    layBtns->addWidget(btnGitAdd, 0, 0);
    layBtns->addWidget(btnGitCommit, 0, 1);
    layBtns->addWidget(btnGitStatus, 0, 2);
    layBtns->addWidget(btnGitDiff, 0, 3);
    layBtns->addWidget(btnGitFetch, 0, 4);

    layBtns->addWidget(new QLabel("远程仓库:"), 1, 0);
    layBtns->addWidget(cmbGitRemote, 1, 1, 1, 2);
    layBtns->addWidget(btnGitPush, 1, 3);
    layBtns->addWidget(btnGitPull, 1, 4);
    layBtns->addWidget(btnGitMerge, 1, 5);

    layBtns->addWidget(btnGitStash, 2, 0);
    layBtns->addWidget(btnGitStashPop, 2, 1);
    layBtns->addWidget(btnGitRemoteAdd, 2, 2);
    layBtns->addWidget(btnGitWorktreeList, 2, 3);
    layBtns->addWidget(btnGitWorktreeAdd, 2, 4);
    layBtns->addWidget(btnGitWorktreeRemove, 2, 5);

    layBtns->addWidget(btnGitWorktreePrune, 3, 0);
    layBtns->addWidget(btnGitGetSshKey, 3, 1);
    layBtns->addWidget(btnGitCheckIgnore, 3, 2);
    layBtns->addWidget(btnGitOpenIgnore, 3, 3);
    layOps->addLayout(layBtns);

    QHBoxLayout *layReminder = new QHBoxLayout();
    layReminder->addWidget(btnGitAutoDiffReminder);
    layReminder->addWidget(btnGitSetDiffRule);
    layReminder->addWidget(new QLabel("间隔:"));
    layReminder->addWidget(spinGitDiffIntervalMinutes);
    layReminder->addWidget(new QLabel("文件阈值:"));
    layReminder->addWidget(spinGitDiffFileThreshold);
    layReminder->addWidget(new QLabel("行数阈值:"));
    layReminder->addWidget(spinGitDiffLineThreshold);
    layReminder->addStretch();
    layOps->addLayout(layReminder);
    
    // History Section
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layOps->addWidget(line);
    
    QHBoxLayout *layHist = new QHBoxLayout();
    layHist->addWidget(new QLabel("版本历史:"));
    layHist->addWidget(cmbGitHistory, 1);
    layHist->addWidget(btnGitRefreshLog);
    layHist->addWidget(btnGitSoftReset); // <--- Add Here
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
    layScp->addWidget(btnRebootTarget);
    layOps->addLayout(layScp);

    // Monitoring Section
    QHBoxLayout *layMon = new QHBoxLayout();
    btnMonitorUsage = new QPushButton("开启检测占用");
    btnMonitorUsage->setCheckable(true);
    
    layMon->addWidget(btnMonitorUsage);
    
    layMon->addWidget(new QLabel("阈值:"));
    spinCpuThreshold = new QSpinBox();
    spinCpuThreshold->setRange(1, 100);
    spinCpuThreshold->setValue(90);
    spinCpuThreshold->setSuffix("%");
    layMon->addWidget(spinCpuThreshold);
    
    btnApplyThreshold = new QPushButton("确认");
    layMon->addWidget(btnApplyThreshold);
    
    lblCpuUsage = new QLabel("CPU: 0%");
    chartCpu = new MonitorChart();
    lblMemUsage = new QLabel("MEM: 0%");
    chartMem = new MonitorChart();
    gitDiffReminderTimer = new QTimer(this);
    gitDiffReminderTimer->setInterval(60 * 60 * 1000);
    
    layMon->addWidget(lblCpuUsage);
    layMon->addWidget(chartCpu);
    layMon->addWidget(lblMemUsage);
    layMon->addWidget(chartMem);
    layMon->addStretch();
    layOps->addLayout(layMon);
    
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
    layout->setStretch(2, 0);
    layout->setStretch(3, 1);
    
    refreshGitGoalsTable();
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
    tcpAssistantPageWidget = createTcpAssistantPage();
    performancePageWidget = createPerformancePage();
    
    stackedWidget->addWidget(modbusPageWidget);
    stackedWidget->addWidget(serialPageWidget);
    stackedWidget->addWidget(gitPageWidget);
    stackedWidget->addWidget(simulatorPageWidget);
    stackedWidget->addWidget(tcpAssistantPageWidget);
    stackedWidget->addWidget(performancePageWidget);
    
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

    // --- TCP Assistant Connections ---
    connect(btnTcpConnect, &QPushButton::clicked, this, &MainWindow::onTcpConnectClicked);
    connect(btnTcpDisconnect, &QPushButton::clicked, this, &MainWindow::onTcpDisconnectClicked);
    connect(btnTcpSend, &QPushButton::clicked, this, &MainWindow::onTcpSendClicked);
    connect(btnTcpClearRecv, &QPushButton::clicked, this, &MainWindow::onTcpClearRecvClicked);
    connect(cmbTcpMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTcpModeChanged);
    connect(tcpServer, &QTcpServer::newConnection, this, &MainWindow::onTcpServerNewConnection);
    connect(tcpCyclicTimer, &QTimer::timeout, this, &MainWindow::onTcpCyclicTimerTick);
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
    connect(btnSearchMap, &QPushButton::clicked, this, &MainWindow::onSearchMapClicked);
    connect(txtSearchMap, &QLineEdit::returnPressed, this, &MainWindow::onSearchMapTextFinished);
    connect(btnExportRegisterMap, &QPushButton::clicked, this, &MainWindow::onExportRegisterMapClicked);
    connect(btnImportRegisterMap, &QPushButton::clicked, this, &MainWindow::onImportRegisterMapClicked);
    connect(btnImportStandardFile, &QPushButton::clicked, this, &MainWindow::onImportStandardFileClicked);

    connect(btnReadCoils, &QPushButton::clicked, this, &MainWindow::onReadCoilsClicked);
    connect(btnReadInputs, &QPushButton::clicked, this, &MainWindow::onReadInputsClicked);
    connect(btnReadHoldingRegisters, &QPushButton::clicked, this, &MainWindow::onReadHoldingRegistersClicked);
    connect(btnReadInputRegisters, &QPushButton::clicked, this, &MainWindow::onReadInputRegistersClicked);

    connect(btnWriteSingleCoil, &QPushButton::clicked, this, &MainWindow::onWriteSingleCoilClicked);
    connect(btnWriteSingleRegister, &QPushButton::clicked, this, &MainWindow::onWriteSingleRegisterClicked);
    connect(btnWriteMultipleCoils, &QPushButton::clicked, this, &MainWindow::onWriteMultipleCoilsClicked);
    connect(btnWriteMultipleRegisters, &QPushButton::clicked, this, &MainWindow::onWriteMultipleRegistersClicked);

    connect(cmbDisplayFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDisplayFormatChanged);
    connect(cmbWriteFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
        if (index == FormatFloat) {
            spinWriteQuantity->setValue(2);
        } else if (index == FormatDouble) {
            spinWriteQuantity->setValue(4);
        }
    });
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
    connect(btnGitRemoveHistory, &QPushButton::clicked, this, &MainWindow::onGitRemoveHistoryClicked);
    connect(cmbGitDir, &QComboBox::currentTextChanged, this, &MainWindow::onGitDirChanged);
    connect(btnGitGoalAdd, &QPushButton::clicked, this, &MainWindow::onGitGoalAddClicked);
    connect(btnGitGoalEdit, &QPushButton::clicked, this, &MainWindow::onGitGoalEditClicked);
    connect(btnGitGoalDelete, &QPushButton::clicked, this, &MainWindow::onGitGoalDeleteClicked);
    connect(btnGitGoalStart, &QPushButton::clicked, this, &MainWindow::onGitGoalStartClicked);
    connect(btnGitRefreshBranches, &QPushButton::clicked, this, &MainWindow::onGitRefreshBranchesClicked);
    connect(btnGitCheckout, &QPushButton::clicked, this, &MainWindow::onGitCheckoutClicked);
    connect(btnGitSyncRemote, &QPushButton::clicked, this, &MainWindow::onGitSyncRemoteClicked);
    connect(btnGitCreateBranch, &QPushButton::clicked, this, &MainWindow::onGitCreateBranchClicked);
    connect(btnGitDeleteBranch, &QPushButton::clicked, this, &MainWindow::onGitDeleteBranchClicked);
    connect(btnGitAdd, &QPushButton::clicked, this, &MainWindow::onGitAddClicked);
    connect(btnGitCommit, &QPushButton::clicked, this, &MainWindow::onGitCommitClicked);
    connect(btnGitPush, &QPushButton::clicked, this, &MainWindow::onGitPushClicked);
    connect(btnGitPull, &QPushButton::clicked, this, &MainWindow::onGitPullClicked);
    connect(btnGitMerge, &QPushButton::clicked, this, &MainWindow::onGitMergeClicked);
    connect(btnGitStatus, &QPushButton::clicked, this, &MainWindow::onGitStatusClicked);
    connect(btnGitOpenIgnore, &QPushButton::clicked, this, &MainWindow::onGitOpenIgnoreClicked);
    connect(btnGitGetSshKey, &QPushButton::clicked, this, &MainWindow::onGitGetSshKeyClicked);
    connect(btnGitRemoteAdd, &QPushButton::clicked, this, &MainWindow::onGitRemoteAddClicked);
    connect(btnGitWorktreeList, &QPushButton::clicked, this, &MainWindow::onGitWorktreeListClicked);
    connect(btnGitWorktreeAdd, &QPushButton::clicked, this, &MainWindow::onGitWorktreeAddClicked);
    connect(btnGitWorktreeRemove, &QPushButton::clicked, this, &MainWindow::onGitWorktreeRemoveClicked);
    connect(btnGitWorktreePrune, &QPushButton::clicked, this, &MainWindow::onGitWorktreePruneClicked);
    connect(btnGitCheckIgnore, &QPushButton::clicked, this, &MainWindow::onGitCheckIgnoreClicked);
    connect(btnGitRefreshLog, &QPushButton::clicked, this, &MainWindow::onGitRefreshLogClicked);
    connect(btnGitDiff, &QPushButton::clicked, this, &MainWindow::onGitDiffClicked);
    connect(btnGitFetch, &QPushButton::clicked, this, &MainWindow::onGitFetchClicked);
    connect(btnGitStash, &QPushButton::clicked, this, &MainWindow::onGitStashClicked);
    connect(btnGitStashPop, &QPushButton::clicked, this, &MainWindow::onGitStashPopClicked);
    connect(btnGitSetDiffRule, &QPushButton::clicked, this, &MainWindow::onGitSetDiffRuleClicked);
    connect(btnGitAutoDiffReminder, &QPushButton::toggled, this, &MainWindow::onGitAutoDiffReminderToggled);
    connect(gitDiffReminderTimer, &QTimer::timeout, this, &MainWindow::onGitAutoDiffReminderTick);
    connect(spinGitDiffIntervalMinutes, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int minutes){
        int ms = qMax(1, minutes) * 60 * 1000;
        gitDiffReminderTimer->setInterval(ms);
        if (btnGitAutoDiffReminder->isChecked()) {
            txtGitLog->append(QString("[Diff提醒] 间隔已更新为 %1 分钟").arg(minutes));
        }
    });
    connect(btnGitReset, &QPushButton::clicked, this, &MainWindow::onGitResetClicked);
    connect(btnGitSoftReset, &QPushButton::clicked, this, &MainWindow::onGitSoftResetClicked);
    connect(btnGitCopyDaily, &QPushButton::clicked, this, &MainWindow::onGitCopyForDailyReportClicked);
    connect(btnScpTransfer, &QPushButton::clicked, this, &MainWindow::onScpTransferClicked);
    connect(btnRebootTarget, &QPushButton::clicked, this, &MainWindow::onRebootTargetClicked);
    connect(btnApplyThreshold, &QPushButton::clicked, this, [this](){
        cpuThresholdValue = spinCpuThreshold->value();
        txtGitLog->append(QString("[Monitor] CPU 阈值已更新为: %1%").arg(cpuThresholdValue));
    });
    connect(btnMonitorUsage, &QPushButton::toggled, this, &MainWindow::onMonitorUsageToggled);
    connect(monitorTimer, &QTimer::timeout, this, &MainWindow::onMonitorTimer);
    monitorTimer->setInterval(2000); // 2 seconds update interval

    // Performance Monitor Connections
    connect(btnTogglePerfMonitor, &QPushButton::toggled, this, &MainWindow::onPerformanceMonitorToggled);
    connect(perfTimer, &QTimer::timeout, this, &MainWindow::onPerformanceTimer);
    
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
            } else if (fmt == "64-bit Float") {
                double d = val;
                quint64 raw = 0;
                memcpy(&raw, &d, sizeof(double));
                target->setRegister(t.addr,     (quint16)((raw >> 48) & 0xFFFF));
                target->setRegister(t.addr + 1, (quint16)((raw >> 32) & 0xFFFF));
                target->setRegister(t.addr + 2, (quint16)((raw >> 16) & 0xFFFF));
                target->setRegister(t.addr + 3, (quint16)(raw & 0xFFFF));
            } else {
                // 普通 16 位模式
                quint16 regVal = static_cast<quint16>(qBound(0.0, val, 65535.0));
                target->setRegister(t.addr, regVal);
            }

            // 更新 UI 表格显示
            if (table && foundRow >= 0) {
                refreshSimRowDisplay(table, foundRow);
                // 多字格式按首地址顺序转换，需要刷新其占用的连续行显示。
                int wordCount = 1;
                if (fmt.startsWith("64-bit")) wordCount = 4;
                else if (fmt.startsWith("32-bit")) wordCount = 2;
                for (int w = 1; w < wordCount; ++w) {
                    refreshSimRowDisplay(table, foundRow + w);
                }
                
                // 检查是否有其他多字格式也引用了这些地址（反向同步）
                for (int k = 0; k < table->rowCount(); ++k) {
                    if (k == foundRow) continue;
                    QString fmtk = simTableFormats.value(table).value(k, "Unsigned");
                    if (!fmtk.startsWith("32-bit") && !fmtk.startsWith("64-bit")) continue;
                    QTableWidgetItem *aItem = table->item(k, 0);
                    if (!aItem) continue;
                    quint16 a = (quint16)aItem->text().toUInt();
                    int wordsK = fmtk.startsWith("64-bit") ? 4 : 2;
                    for (int w = 0; w < wordsK; ++w) {
                        if ((quint16)(a + w) == t.addr) {
                            refreshSimRowDisplay(table, k);
                            break;
                        }
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
                
                // 限制日志行数，防止长时间运行卡死
                if (txtSimLog->document()->blockCount() > 1000) {
                    QTextCursor cursor = txtSimLog->textCursor();
                    cursor.movePosition(QTextCursor::Start);
                    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 100);
                    cursor.removeSelectedText();
                }
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
    Q_UNUSED(previous);
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
        
        if (displayFormat == FormatFloat && byteCount >= 4) {
            for (int i = 0; i < byteCount / 4; ++i) {
                quint16 cd, ab;
                stream >> cd >> ab; // CDAB: [CD, AB]
                quint32 raw = (quint32(ab) << 16) | cd;
                float f;
                memcpy(&f, &raw, 4);
                regs << QString::number(f, 'f', 4);
            }
        } 
        else if (displayFormat == FormatDouble && byteCount >= 8) {
            for (int i = 0; i < byteCount / 8; ++i) {
                quint16 gh, ef, cd, ab;
                stream >> gh >> ef >> cd >> ab;
                // GHEF CDAB: [GH, EF, CD, AB] -> 与主机 memcpy 的 uint64 小端字序一致
                quint64 raw = quint64(ab) | (quint64(cd) << 16) | (quint64(ef) << 32) | (quint64(gh) << 48);
                double d;
                memcpy(&d, &raw, 8);
                regs << QString::number(d, 'g', 8);
            }
        }
        else {
            for (int i=0; i<byteCount/2; ++i) {
                quint16 val;
                stream >> val;
                regs << formatValue(val);
            }
        }
        resultStr = regs.join(", ");
    }
    else {
        resultStr = "写入成功";
    }
    
    txtResult->setText(QDateTime::currentDateTime().toString("[HH:mm:ss] ") + resultStr);

    // 构建详细日志信息：包含 SlaveID、寄存器类型（线圈/输入/保持/输入寄存器/写操作）、起始地址、数量及数据值
    QString typeDesc;
    if (funcCode == 1) typeDesc = "Coils (01)";
    else if (funcCode == 2) typeDesc = "Discrete Inputs (02)";
    else if (funcCode == 3) typeDesc = "Holding Registers (03)";
    else if (funcCode == 4) typeDesc = "Input Registers (04)";
    else if (funcCode == 5) typeDesc = "Write Single Coil (05)";
    else if (funcCode == 6) typeDesc = "Write Single Register (06)";
    else if (funcCode == 15) typeDesc = "Write Multiple Coils (15)";
    else if (funcCode == 16) typeDesc = "Write Multiple Registers (16)";
    else typeDesc = QString("Func %1").arg(funcCode);

    QString detail;
    // 对于读操作，使用 currentReadParams 保存的起始地址和数量
    if (funcCode == 1 || funcCode == 2 || funcCode == 3 || funcCode == 4) {
        detail = QString("Slave=%1, %2, Start=%3, Qty=%4, Values=[%5]")
                    .arg(uId)
                    .arg(typeDesc)
                    .arg(currentReadParams.startAddress)
                    .arg(currentReadParams.quantity)
                    .arg(resultStr);
    } else if (funcCode == 5 || funcCode == 6) {
        // 写单项：响应回显地址和值
        quint16 addrEcho = 0, valEcho = 0;
        stream >> addrEcho >> valEcho;
        detail = QString("Slave=%1, %2, Addr=%3, Value=%4")
                    .arg(uId).arg(typeDesc).arg(addrEcho).arg(valEcho);
    } else if (funcCode == 15 || funcCode == 16) {
        // 写多项：响应回显起始地址和数量
        quint16 startEcho = 0, qtyEcho = 0;
        stream >> startEcho >> qtyEcho;
        detail = QString("Slave=%1, %2, Start=%3, Qty=%4")
                    .arg(uId).arg(typeDesc).arg(startEcho).arg(qtyEcho);
    } else {
        detail = QString("Slave=%1, %2, Details=%3").arg(uId).arg(typeDesc).arg(resultStr);
    }

    // 始终记录详细日志（包括连续读取场景，以便审计）
    logMessage(QString("收到响应: %1").arg(detail));
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
    quint16 val = 0;
    QString txt = spinWriteValue->cleanText();
    int format = cmbWriteFormat->currentIndex();

    if (format == FormatHex) {
        val = txt.toUShort(nullptr, 16);
    } else if (format == FormatBinary) {
        val = txt.toUShort(nullptr, 2);
    } else if (format == FormatFloat || format == FormatDouble) {
        // 单寄存器写浮点仅取字序中的首字：32 位 CDAB 取 CD，64 位 GHEF CDAB 取 GH
        if (format == FormatFloat) {
            float f = txt.toFloat();
            quint32 raw;
            memcpy(&raw, &f, sizeof(float));
            val = (quint16)((raw >> 16) & 0xFFFF);
        } else {
            double d = txt.toDouble();
            quint64 raw;
            memcpy(&raw, &d, sizeof(double));
            val = (quint16)((raw >> 48) & 0xFFFF);
        }
    } else {
        val = (quint16)spinWriteValue->value();
    }

    QVector<quint16> v; v << val;
    sendModbusRequest(6, spinWriteStartAddr->value(), 1, v);
}
void MainWindow::onWriteMultipleCoilsClicked() {
    // Basic CSV parsing needed here, simplifying for brevity
     QStringList items = txtWriteValues->text().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
     QVector<quint16> vals;
     for(auto s : items) vals << (s.toInt() > 0 ? 1 : 0);
     sendModbusRequest(15, spinWriteStartAddr->value(), vals.size(), vals);
}
void MainWindow::onWriteMultipleRegistersClicked() {
     QStringList items = txtWriteValues->text().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
     QVector<quint16> vals;
     int format = cmbWriteFormat->currentIndex();

     if (format == FormatFloat) {
         for (const QString &s : items) {
             float f = s.toFloat();
             quint32 raw;
             memcpy(&raw, &f, 4);
             vals << (quint16)((raw >> 16) & 0xFFFF); // CDAB: CD
             vals << (quint16)(raw & 0xFFFF);         // AB
         }
     } else if (format == FormatDouble) {
         for (const QString &s : items) {
             double d = s.toDouble();
             quint64 raw;
             memcpy(&raw, &d, 8);
             vals << (quint16)((raw >> 48) & 0xFFFF); // GHEF CDAB: GH
             vals << (quint16)((raw >> 32) & 0xFFFF); // EF
             vals << (quint16)((raw >> 16) & 0xFFFF); // CD
             vals << (quint16)(raw & 0xFFFF);         // AB
         }
     } else {
         for(auto s : items) {
             if (format == FormatHex) {
                 vals << s.toUShort(nullptr, 16);
             } else if (format == FormatBinary) {
                 vals << s.toUShort(nullptr, 2);
             } else {
                 vals << s.toUShort();
             }
         }
     }
     
     if (vals.isEmpty()) return;
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
    
    // 根据格式自动锁定读取数量
    if (displayFormat == FormatFloat) {
        spinReadQuantity->setValue(2);
        // spinReadQuantity->setEnabled(false); // 可选：禁用调整，以免用户误改
    } else if (displayFormat == FormatDouble) {
        spinReadQuantity->setValue(4);
        // spinReadQuantity->setEnabled(false);
    } else {
        // spinReadQuantity->setEnabled(true);
    }
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
        
        // 尝试解析并设置寄存器值
        bool ok = false;
        if (fmt == "32-bit Float") {
            slave->setFloat(addr, valStr.toFloat(&ok));
        } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
            uint32_t v32 = valStr.toUInt(&ok);
            slave->setRegister(addr, (quint16)(v32 >> 16));
            slave->setRegister(addr + 1, (quint16)(v32 & 0xFFFF));
        } else if (fmt == "64-bit Float") {
            double d = valStr.toDouble(&ok);
            if (ok) {
                quint64 raw = 0;
                memcpy(&raw, &d, sizeof(double));
                slave->setRegister(addr,     (quint16)((raw >> 48) & 0xFFFF));
                slave->setRegister(addr + 1, (quint16)((raw >> 32) & 0xFFFF));
                slave->setRegister(addr + 2, (quint16)((raw >> 16) & 0xFFFF));
                slave->setRegister(addr + 3, (quint16)(raw & 0xFFFF));
            }
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
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = txt.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = txt.split('\n', QString::SkipEmptyParts);
#endif
    for (QString raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        QStringList tok = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#else
        QStringList tok = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
#endif
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
    if (registerHistory.size() > 5000) registerHistory.removeFirst();
    
    // 1. 同步到 UI 寄存器表整数列
    if (table) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QTableWidgetItem *addrItem = table->item(r, 0);
            if (addrItem && (quint16)addrItem->text().toUInt() == addr) {
                refreshSimRowDisplay(table, r);
                for (int k = 0; k < table->rowCount(); ++k) {
                    QString fmtk = simTableFormats.value(table).value(k, "Unsigned");
                    if (!fmtk.startsWith("32-bit") && !fmtk.startsWith("64-bit")) continue;
                    QTableWidgetItem *aItem = table->item(k, 0);
                    if (!aItem) continue;
                    quint16 a = (quint16)aItem->text().toUInt();
                    int words = fmtk.startsWith("64-bit") ? 4 : 2;
                    for (int w = 0; w < words; ++w) {
                        if ((quint16)(a + w) == addr) {
                            refreshSimRowDisplay(table, k);
                            break;
                        }
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

bool MainWindow::runGitCommand(const QStringList &args) {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先选择Git仓库目录!</font>");
        return false;
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
         return false;
    }
    
    // Default wait 30s
    if (!process.waitForFinished(30000)) {
         txtGitLog->append("<font color='red'>部分超时或后台运行...</font>");
         return false;
    }
    
    QByteArray stdoutData = process.readAllStandardOutput();
    QByteArray stderrData = process.readAllStandardError();
    
    if (!stdoutData.isEmpty()) {
#ifdef Q_OS_WIN
        txtGitLog->append(QString::fromUtf8(stdoutData));
#else
        txtGitLog->append(QString::fromLocal8Bit(stdoutData));
#endif
    }
    if (!stderrData.isEmpty()) {
#ifdef Q_OS_WIN
        txtGitLog->append(QString("<font color='orange'>%1</font>").arg(QString::fromUtf8(stderrData)));
#else
        txtGitLog->append(QString("<font color='orange'>%1</font>").arg(QString::fromLocal8Bit(stderrData)));
#endif
    }
    
    txtGitLog->moveCursor(QTextCursor::End);
    return process.exitCode() == 0;
}

void MainWindow::onGitRefreshBranchesClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;
    
    // 1. 先进行 fetch 获取最新远程分支信息
    QProcess fetchProcess;
    fetchProcess.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    fetchProcess.start("git.exe", QStringList() << "fetch" << "--prune");
#else
    fetchProcess.start("git", QStringList() << "fetch" << "--prune");
#endif
    fetchProcess.waitForFinished();

    // 2. 获取所有本地和远程分支 (git branch -a)
    QProcess process;
    process.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "branch" << "-a");
#else
    process.start("git", QStringList() << "branch" << "-a");
#endif
    process.waitForFinished();
    
#ifdef Q_OS_WIN
    QString output = QString::fromUtf8(process.readAllStandardOutput());
#else
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
#endif
    
    cmbGitBranches->clear();
    QString currentBranch;
    
    for (QString line : lines) {
        line = line.trimmed();
        bool isCurrent = line.startsWith("* ");
        if (isCurrent) {
            line = line.mid(2).trimmed();
            currentBranch = line;
        }

        bool isRemote = line.startsWith("remotes/");
        cmbGitBranches->addItem(line);
        int lastIndex = cmbGitBranches->count() - 1;

        if (isRemote) {
            // 远程分支设为红色
            cmbGitBranches->setItemData(lastIndex, QBrush(Qt::red), Qt::ForegroundRole);
        } else {
            // 本地分支设为黑色
            cmbGitBranches->setItemData(lastIndex, QBrush(Qt::black), Qt::ForegroundRole);
        }
    }
    
    if (!currentBranch.isEmpty())
        cmbGitBranches->setCurrentText(currentBranch);

    txtGitLog->append("<font color='gray'>已刷新本地及远程分支（红色为远程分支）。</font>");

    const int completed = markGoalsCompletedForMergedBranches(workDir);
    if (completed > 0) {
        refreshGitGoalsTable();
    }
}

void MainWindow::onGitDiffClicked() {
    // 显示当前工作区与暂存区差异；如果需要可扩展为接受参数（如 --staged）
    runGitCommand(QStringList() << "diff");
}

void MainWindow::onGitFetchClicked() {
    runGitCommand(QStringList() << "fetch" << "--prune");
}

void MainWindow::onGitStashClicked() {
    runGitCommand(QStringList() << "stash");
}

void MainWindow::onGitStashPopClicked() {
    runGitCommand(QStringList() << "stash" << "pop");
}

void MainWindow::onGitSetDiffRuleClicked() {
    int fileThreshold = spinGitDiffFileThreshold->value();
    int lineThreshold = spinGitDiffLineThreshold->value();

    QStringList options;
    options << "只要有改动就提醒"
            << QString("改动文件数 >= %1 时提醒").arg(fileThreshold)
            << QString("新增+删除总行数 >= %1 时提醒").arg(lineThreshold)
            << "涉及配置文件改动时提醒"
            << "涉及源码文件改动时提醒";

    int defaultIndex = (gitDiffReminderRule >= 0 && gitDiffReminderRule < options.size()) ? gitDiffReminderRule : 0;
    bool ok = false;
    QString selected = QInputDialog::getItem(this,
                                             "Diff 提醒标准",
                                             "请选择每小时检查 git diff 的判断标准:",
                                             options,
                                             defaultIndex,
                                             false,
                                             &ok);
    if (!ok) return;

    gitDiffReminderRule = options.indexOf(selected);
    txtGitLog->append(QString("[Diff提醒] 已设置标准: %1").arg(selected));
}

void MainWindow::onGitAutoDiffReminderToggled(bool checked) {
    if (checked) {
        if (gitDiffReminderRule < 0) {
            onGitSetDiffRuleClicked();
        }

        if (gitDiffReminderRule < 0) {
            btnGitAutoDiffReminder->blockSignals(true);
            btnGitAutoDiffReminder->setChecked(false);
            btnGitAutoDiffReminder->blockSignals(false);
            return;
        }

        btnGitAutoDiffReminder->setText("关闭每小时Diff提醒");
    gitDiffReminderTimer->setInterval(spinGitDiffIntervalMinutes->value() * 60 * 1000);
        gitDiffReminderTimer->start();
    txtGitLog->append(QString("[Diff提醒] 已开启：每 %1 分钟自动分析 git diff 并按标准提醒存档。")
                  .arg(spinGitDiffIntervalMinutes->value()));
        onGitAutoDiffReminderTick();
    } else {
        gitDiffReminderTimer->stop();
        btnGitAutoDiffReminder->setText("开启每小时Diff提醒");
        txtGitLog->append("[Diff提醒] 已关闭。");
    }
}

void MainWindow::onGitAutoDiffReminderTick() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty() || !QDir(workDir).exists()) {
        txtGitLog->append("[Diff提醒] 失败: Git 仓库目录无效，已停止提醒。");
        gitDiffReminderTimer->stop();
        if (btnGitAutoDiffReminder->isChecked()) {
            btnGitAutoDiffReminder->blockSignals(true);
            btnGitAutoDiffReminder->setChecked(false);
            btnGitAutoDiffReminder->setText("开启每小时Diff提醒");
            btnGitAutoDiffReminder->blockSignals(false);
        }
        return;
    }

    QProcess process;
    process.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "diff" << "--numstat");
#else
    process.start("git", QStringList() << "diff" << "--numstat");
#endif

    if (!process.waitForFinished(15000)) {
        txtGitLog->append("[Diff提醒] git diff --numstat 执行超时。");
        return;
    }

#ifdef Q_OS_WIN
    QString output = QString::fromUtf8(process.readAllStandardOutput());
#else
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
#endif

    int changedFiles = 0;
    int addedLines = 0;
    int removedLines = 0;
    int sourceTouched = 0;
    int configTouched = 0;
    int binaryTouched = 0;

    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith('\r')) line.chop(1);
        QStringList parts = line.split('\t');
        if (parts.size() < 3) continue;

        changedFiles++;

        bool okAdd = false;
        bool okDel = false;
        int add = parts[0].toInt(&okAdd);
        int del = parts[1].toInt(&okDel);
        if (okAdd) addedLines += add; else binaryTouched++;
        if (okDel) removedLines += del; else binaryTouched++;

        QString path = parts.mid(2).join("\t").toLower();
        if (path.endsWith(".cpp") || path.endsWith(".c") || path.endsWith(".h") ||
            path.endsWith(".hpp") || path.endsWith(".cc") || path.endsWith(".py") ||
            path.endsWith(".js") || path.endsWith(".ts") || path.endsWith(".java")) {
            sourceTouched++;
        }
        if (path.endsWith(".json") || path.endsWith(".yaml") || path.endsWith(".yml") ||
            path.endsWith(".ini") || path.endsWith(".toml") || path.endsWith(".conf") ||
            path.endsWith(".pro") || path.contains("cmakelists.txt")) {
            configTouched++;
        }
    }

    if (changedFiles <= 0) {
        txtGitLog->append("[Diff提醒] 当前无未提交改动。");
        return;
    }

    int totalLines = addedLines + removedLines;
    int fileThreshold = spinGitDiffFileThreshold->value();
    int lineThreshold = spinGitDiffLineThreshold->value();
    txtGitLog->append(QString("[Diff分析] 文件:%1, +%2/-%3, 总变更:%4, 源码文件:%5, 配置文件:%6, 二进制变更项:%7")
                          .arg(changedFiles)
                          .arg(addedLines)
                          .arg(removedLines)
                          .arg(totalLines)
                          .arg(sourceTouched)
                          .arg(configTouched)
                          .arg(binaryTouched));

    bool shouldRemind = false;
    QString ruleDesc;
    switch (gitDiffReminderRule) {
    case 0:
        shouldRemind = changedFiles > 0;
        ruleDesc = "只要有改动就提醒";
        break;
    case 1:
        shouldRemind = changedFiles >= fileThreshold;
        ruleDesc = QString("改动文件数 >= %1").arg(fileThreshold);
        break;
    case 2:
        shouldRemind = totalLines >= lineThreshold;
        ruleDesc = QString("新增+删除总行数 >= %1").arg(lineThreshold);
        break;
    case 3:
        shouldRemind = configTouched > 0;
        ruleDesc = "涉及配置文件改动";
        break;
    case 4:
        shouldRemind = sourceTouched > 0;
        ruleDesc = "涉及源码文件改动";
        break;
    default:
        shouldRemind = false;
        ruleDesc = "未设置";
        break;
    }

    if (!shouldRemind) return;

    QString tips = QString("检测到改动达到提醒标准：%1\n\n"
                           "建议执行存档操作（如 git commit / git tag / 导出补丁）以避免修改丢失。\n"
                           "统计: 文件 %2 个, +%3/-%4, 总变更 %5 行。")
                       .arg(ruleDesc)
                       .arg(changedFiles)
                       .arg(addedLines)
                       .arg(removedLines)
                       .arg(totalLines);
    QMessageBox::information(this, "Git 存档提醒", tips);
    txtGitLog->append(QString("[Diff提醒] 已触发存档提醒，标准: %1").arg(ruleDesc));
}

void MainWindow::onGitSyncRemoteClicked() {
    QString branch = cmbGitBranches->currentText().trimmed();
    if (branch.isEmpty()) return;

    if (!branch.startsWith("remotes/")) {
        QMessageBox::information(this, "提示", "该分支已在本地或不是远程分支标识，请直接使用'切换分支'。");
        return;
    }

    // 从 remotes/origin/branch-name 提取 branch-name
    // 通常格式是 remotes/[remote-name]/[branch-name]
    QStringList parts = branch.split('/');
    if (parts.size() < 3) {
        QMessageBox::critical(this, "错误", "无法解析远程分支路径: " + branch);
        return;
    }

    // 重新拼接真正的分支名 (处理分支名中包含 / 的情况)
    QString branchName = parts.mid(2).join('/');
    
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认同步", 
                                  QString("确定要将远程分支 '%1' 同步到本地并签出吗?").arg(branchName),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // 执行 git checkout -b branch-name --track remotes/origin/branch-name
        // 或者简单的 git checkout branch-name (如果 fetch 过，git 会自动建立追踪)
        runGitCommand(QStringList() << "checkout" << "-b" << branchName << "--track" << branch);
        onGitRefreshBranchesClicked(); // 刷新列表以变为黑色
    }
}

void MainWindow::onGitCheckoutClicked() {
    QString branch = cmbGitBranches->currentText().trimmed();
    if (branch.startsWith("+ ")) {
        branch = branch.mid(2).trimmed();
    }
    
    if (branch.isEmpty()) {
       txtGitLog->append("<font color='red'>错误: 请先选择要切换的分支</font>");
       return;
    }
    runGitCommand(QStringList() << "checkout" << branch);
    // Refresh to show updated status (though current logic doesn't mark active branch in combobox explicitly other than selection)
    onGitRefreshBranchesClicked();
    onGitRefreshLogClicked();
}

void MainWindow::onGitCreateBranchClicked() {
    bool ok;
    QString branchName = QInputDialog::getText(this, "创建新分支", 
                                              "输入新分支名称:", QLineEdit::Normal, 
                                              QString(), &ok);
    if (!ok || branchName.trimmed().isEmpty()) {
        return;
    }

    branchName = branchName.trimmed();
    // 执行 git checkout -b <branchName>
    runGitCommand(QStringList() << "checkout" << "-b" << branchName);
    
    // 刷新 UI
    onGitRefreshBranchesClicked();
    onGitRefreshLogClicked();
    
    // 将新分支设置为下拉框当前项
    int index = cmbGitBranches->findText(branchName);
    if (index >= 0) {
        cmbGitBranches->setCurrentIndex(index);
    }
}

void MainWindow::onGitDeleteBranchClicked() {
    QString branch = cmbGitBranches->currentText().trimmed();
    if (branch.isEmpty()) {
       txtGitLog->append("<font color='red'>错误: 请先选择要删除的分支</font>");
       return;
    }

    if (QMessageBox::question(this, "确认删除", 
                              QString("确定要删除本地分支 %1 吗？\n(注意：如果分支未合并，删除可能会失败)").arg(branch)) 
        != QMessageBox::Yes) {
        return;
    }

    // 执行 git branch -d <branch>
    runGitCommand(QStringList() << "branch" << "-d" << branch);
    
    // 刷新 UI
    onGitRefreshBranchesClicked();
    onGitRefreshLogClicked();
}

void MainWindow::onGitAddClicked() {
    runGitCommand(QStringList() << "add" << ".");
}

void MainWindow::onGitGetSshKeyClicked() {
    QString homeDir = QDir::homePath();
    QString sshPath = homeDir + "/.ssh";
    QString keyFile = "";

    // 常见的公钥文件名
    QStringList potentialKeys = { "id_ed25519.pub", "id_rsa.pub", "id_ecdsa.pub", "id_dsa.pub" };
    
    for (const QString &key : potentialKeys) {
        if (QFile::exists(sshPath + "/" + key)) {
            keyFile = sshPath + "/" + key;
            break;
        }
    }

    if (keyFile.isEmpty()) {
        QMessageBox::information(this, "SSH 公钥", "未找到常见的公钥文件 (id_ed25519.pub, id_rsa.pub等)。\n请确保已生成过 SSH Key (使用 ssh-keygen)。");
        return;
    }

    QFile file(keyFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法读取公钥文件: " + keyFile);
        return;
    }

    QString publicKey = QTextStream(&file).readAll().trimmed();
    file.close();

    if (publicKey.isEmpty()) {
        QMessageBox::warning(this, "警告", "公钥文件内容为空。");
        return;
    }

    // 复制到剪贴板
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(publicKey);

    // 在日志中显示
    txtGitLog->append("<font color='green'>[SSH] 公钥已获取并复制到剪贴板!</font>");
    txtGitLog->append(QString("<font color='gray'>文件路径: %1</font>").arg(keyFile));
    
    // 弹窗提示
    QMessageBox::information(this, "SSH 公钥", 
        "SSH 公钥内容已复制到剪贴板！\n\n文件路径: " + keyFile + "\n\n您可以直接去 GitHub 设置中粘贴了。");
}

void MainWindow::onGitWorktreeListClicked() {
    runGitCommand(QStringList() << "worktree" << "list");
}

void MainWindow::onGitWorktreeAddClicked() {
    QString currentBranch = cmbGitBranches->currentText().trimmed();
    if (currentBranch.startsWith("+ ")) {
        currentBranch = currentBranch.mid(2).trimmed();
    }

    if (currentBranch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先选择或输入一个分支来创建该分支的 Worktree!</font>");
        return;
    }

    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    // 1. 检查该分支是否已被其他 Worktree 占用
    QProcess checkProc;
    checkProc.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    checkProc.start("git.exe", QStringList() << "worktree" << "list" << "--porcelain");
#else
    checkProc.start("git", QStringList() << "worktree" << "list" << "--porcelain");
#endif
    checkProc.waitForFinished();
    QString listOut = QString::fromLocal8Bit(checkProc.readAllStandardOutput());
    
    bool branchUsed = listOut.contains("branch refs/heads/" + currentBranch + "\n") || 
                      listOut.contains("branch refs/heads/" + currentBranch + "\r\n") ||
                      listOut.endsWith("branch refs/heads/" + currentBranch);

    QString branchToUse = currentBranch;
    bool createNew = false;

    if (branchUsed) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "分支已被占用", 
                                      QString("分支 [%1] 已在其他工作树中打开。\n\n是否基于此分支创建一个新分支（例如 %1_fix）来创建 Worktree?").arg(currentBranch),
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::No) return;
        
        bool ok;
        branchToUse = QInputDialog::getText(this, "创建新分支", 
                                           "请输入新分支名称:", QLineEdit::Normal,
                                           currentBranch + "_work", &ok);
        if (!ok || branchToUse.trimmed().isEmpty()) return;
        branchToUse = branchToUse.trimmed();
        createNew = true;
    }

    // 2. 选择路径
    QDir repoDir(workDir);
    QString parentPath = repoDir.absolutePath();
    if (parentPath.endsWith("/")) parentPath.chop(1);
    
    QString branchSeed = branchToUse.contains("/") ? branchToUse.split("/").last() : branchToUse;
    QString suggestedPath = parentPath + "_worktree_" + branchSeed;

    bool ok;
    QString path = QInputDialog::getText(this, "添加 Git Worktree", 
                                         "请输入新工作树的本地路径:", QLineEdit::Normal,
                                         suggestedPath, &ok);
    if (!ok || path.isEmpty()) return;

    // 3. 执行命令
    QStringList args;
    args << "worktree" << "add";
    if (createNew) {
        args << "-b" << branchToUse << path << currentBranch; // 基于 currentBranch 创建新分支 branchToUse
    } else {
        args << path << branchToUse;
    }
    
    runGitCommand(args);

    // --- 新增：将新路径添加到记忆列表 ---
    saveGitHistory(path);
    txtGitLog->append(QString("<font color='gray'>[History] 已将新 Worktree 路径添加到记忆记录。</font>"));
}

void MainWindow::onGitWorktreePruneClicked() {
    runGitCommand(QStringList() << "worktree" << "prune");
    txtGitLog->append("<font color='gray'>[Worktree] 已执行清理。</font>");
}

void MainWindow::onGitWorktreeRemoveClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    // 获取当前 Worktree 列表以便用户选择
    QProcess process;
    process.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "worktree" << "list" << "--porcelain");
#else
    process.start("git", QStringList() << "worktree" << "list" << "--porcelain");
#endif
    process.waitForFinished();
    
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    QStringList lines = output.split("\n");
    QStringList worktrees;
    QString currentPath;
    
    // 解析 porcelain 输出获取路径
    for (const QString &line : lines) {
        if (line.startsWith("worktree ")) {
            QString path = line.mid(9).trimmed();
            // 排除主工作目录 (通常列表第一个)
            if (path != workDir && QDir(path).absolutePath() != QDir(workDir).absolutePath()) {
                worktrees << path;
            }
        }
    }

    if (worktrees.isEmpty()) {
        QMessageBox::information(this, "移除 Worktree", "当前没有可移除的辅助工作树。");
        return;
    }

    bool ok;
    QString target = QInputDialog::getItem(this, "移除 Git Worktree", 
                                           "请选择要永久移除的工作树路径:", worktrees, 0, false, &ok);
    
    if (ok && !target.isEmpty()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认移除", 
                                      QString("确定要移除并删除目录 %1 吗?\n这将删除该目录下所有未提交的内容!").arg(target),
                                      QMessageBox::Yes|QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            // --force 以防有未提交改动，慎重起见可以去掉 --force
            runGitCommand(QStringList() << "worktree" << "remove" << target);

            // --- 新增：从记忆路径中删除 ---
            QSettings settings("LiChenYang", "LinuxHelper");
            QStringList history = settings.value("GitHistory").toStringList();
            QString targetPath = QDir(target).absolutePath();
            
            bool removed = false;
            if (history.contains(targetPath)) {
                history.removeAll(targetPath);
                removed = true;
            }
            if (history.contains(target)) {
                history.removeAll(target);
                removed = true;
            }
            
            if (removed) {
                settings.setValue("GitHistory", history);
                applyGitHistoryToCombo(history);
                txtGitLog->append(QString("<font color='gray'>[History] 已从记忆记录中同步移除此 Worktree 路径。</font>"));
            }
        }
    }
}

void MainWindow::onGitRemoteAddClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择本地 Git 仓库目录（或打算初始化的目录）。");
        return;
    }

    // 检查是否已经是 Git 仓库，如果不是，询问是否初始化
    if (!QFile::exists(workDir + "/.git")) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "初始化", 
            "当前目录尚未初始化为 Git 仓库，是否执行 'git init'？",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            runGitCommand(QStringList() << "init");
        } else {
            return;
        }
    }

    bool ok;
    QString remoteUrl = QInputDialog::getText(this, "添加远程仓库",
                                         "请输入远程仓库 URL (例如 git@github.com:user/repo.git):",
                                         QLineEdit::Normal, "", &ok);
    if (!ok || remoteUrl.trimmed().isEmpty()) return;

    QString remoteName = cmbGitRemote->currentText().trimmed();
    if (remoteName.isEmpty()) remoteName = "origin";

    // 执行 git remote add [name] [url]
    runGitCommand(QStringList() << "remote" << "add" << remoteName << remoteUrl.trimmed());
    
    txtGitLog->append(QString("<font color='green'>[Remote] 已尝试链接远程仓库 '%1' 到 '%2'</font>")
                      .arg(remoteName).arg(remoteUrl));
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
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;
    
    // 获取当前选中的待合并分支
    QString branchToMerge = cmbGitBranches->currentText().trimmed();
    if (branchToMerge.startsWith("+ ")) {
        branchToMerge = branchToMerge.mid(2).trimmed();
    }
    
    if (branchToMerge.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先在下拉框选择要合并进来的目标分支!</font>");
        return;
    }

    // Detect current branch (where we are)
    QProcess process;
    process.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    process.start("git.exe", QStringList() << "rev-parse" << "--abbrev-ref" << "HEAD");
#else
    process.start("git", QStringList() << "rev-parse" << "--abbrev-ref" << "HEAD");
#endif
    process.waitForFinished();
    QString currentBranch = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    
    if (branchToMerge == currentBranch) {
        QMessageBox::warning(this, "合并提示", "当前已经在分支 [" + currentBranch + "] 上，无需合并自身。请在下拉框选择其他分支。");
        return;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认合并", 
                                  QString("确定要将分支 [%1] 的改动合并到当前分支 [%2] 吗?").arg(branchToMerge).arg(currentBranch),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        const QString mergedBranch = normalizeLocalBranchRef(branchToMerge);
        if (runGitCommand(QStringList() << "merge" << branchToMerge)) {
            const int completed = markGoalsCompletedForMergedBranches(workDir);
            if (completed > 0) {
                refreshGitGoalsTable();
            } else if (!mergedBranch.isEmpty()) {
                txtGitLog->append(QStringLiteral("<font color='gray'>[工作目标] 合并完成；分支 %1 若已并入主分支，刷新分支后会自动填写实际完成日期。</font>")
                                      .arg(mergedBranch));
            }
        }
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
                {"*.user", "*.user (用户本地配置)"},
                {"*.pro.user", "*.pro.user (Qt .pro 文件的本地用户配置，如 ModbusTCPAssistant.pro.user)"}
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
    
#ifdef Q_OS_WIN
    QString output = QString::fromUtf8(process.readAllStandardOutput());
#else
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
#endif
    
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

void MainWindow::onGitSoftResetClicked() {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认操作", 
                                  "确定要执行 git reset --soft HEAD^ 吗?\n这将撤销最后一次提交，但保留您的代码原始修改在暂存区。",
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // 在重置前获取最新的提交信息
        QString lastCommit = cmbGitHistory->itemText(0);
        QString commitMsg = "";

        // 解析格式: "%h - %cd : %s (%an)" -> 提取冒号后的提交信息
        int colonIdx = lastCommit.indexOf(" : ");
        if (colonIdx != -1) {
            commitMsg = lastCommit.mid(colonIdx + 3).trimmed();
            // 去掉末尾的作者部分 (Author)
            int authorStart = commitMsg.lastIndexOf(" (");
            if (authorStart != -1) {
                commitMsg = commitMsg.left(authorStart).trimmed();
            }
        }

        runGitCommand(QStringList() << "reset" << "--soft" << "HEAD^");
        
        // 复制到剪贴板
        if (!commitMsg.isEmpty()) {
            QApplication::clipboard()->setText(commitMsg);
            txtGitLog->append(QString("<font color='green'>[Reset] 已撤回提交并复制信息: %1</font>").arg(commitMsg));
        }

        onGitRefreshLogClicked(); // 自动刷新历史记录
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

    QString finalContent;
    for (int i = 0; i < todayCommits.size(); ++i) {
        finalContent += QString("%1. %2\n").arg(i + 1).arg(todayCommits[i]);
    }
    finalContent = finalContent.trimmed();

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

    // 每次“搜索并传输”前自动执行 add + commit，确保代码状态可追溯。
    runGitCommand(QStringList() << "add" << ".");

    if (spinGitDiffIntervalMinutes && spinGitDiffIntervalMinutes->value() < 10) {
        if (QMessageBox::question(this,
                                  "传输前检查",
                                  "当前间隔设置小于10分钟，是否先执行一次软回退 (git reset --soft HEAD^)？",
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            runGitCommand(QStringList() << "reset" << "--soft" << "HEAD^");
            onGitRefreshLogClicked();
        }
    }

    bool ok = false;
    QString defaultMsg = txtGitCommitMsg->text().trimmed();
    QString commitMsg = QInputDialog::getText(
        this,
        "Git 提交信息",
        "请输入本次传输前的提交信息:",
        QLineEdit::Normal,
        defaultMsg,
        &ok
    ).trimmed();

    if (!ok || commitMsg.isEmpty()) {
        txtGitLog->append("已取消传输：未提供提交信息，未执行 git commit。");
        return;
    }

    if (QMessageBox::question(this,
                              "确认提交",
                              QString("确认执行 git commit -m \"%1\" 并继续传输吗？").arg(commitMsg),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        txtGitLog->append("已取消传输：用户取消 git commit。");
        return;
    }

    txtGitCommitMsg->setText(commitMsg);
    runGitCommand(QStringList() << "commit" << "-m" << commitMsg);

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
                currentMonitoringProcess = fileName; // 记录当前传输的文件名以便监测
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

void MainWindow::onRebootTargetClicked() {
    QString targetIp = txtScpTargetIp->text().trimmed();
    if (targetIp.isEmpty()) {
        txtGitLog->append("错误: 请输入目标设备地址");
        return;
    }

    QString password = txtScpPassword->text();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "重启确认", 
                                  QString("确定要重启目标设备 [%1] 吗？").arg(targetIp), 
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No) {
        return;
    }

    txtGitLog->append(QString("正在向 [%1] 发送重启命令...").arg(targetIp));

    QProcess *process = new QProcess(this);
    connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, process, targetIp](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            txtGitLog->append(QString("成功向 [%1] 发送重启命令。").arg(targetIp));
        } else {
            QString err = process->readAllStandardError();
            txtGitLog->append(QString("发送重启命令失败: %1").arg(err));
        }
        process->deleteLater();
    });

    if (!password.isEmpty()) {
        QStringList args;
        args << "-p" << password
             << "ssh"
             << "-o" << "StrictHostKeyChecking=no"
             << "-o" << "PreferredAuthentications=password"
             << "-o" << "PubkeyAuthentication=no"
             << "-o" << "NumberOfPasswordPrompts=1"
             << QString("root@%1").arg(targetIp)
             << "reboot";
        process->start("sshpass", args);
    } else {
        QStringList args;
        args << "-o" << "StrictHostKeyChecking=no"
             << "-o" << "BatchMode=yes"
             << QString("root@%1").arg(targetIp)
             << "reboot";
        process->start("ssh", args);
    }
}

void MainWindow::onMonitorUsageToggled() {
    if (btnMonitorUsage->isChecked()) {
        ulimitSet = false; // 每次开启监测重新标记需要设置 ulimit
        lastKnownPid = -1; // 重置最近 PID
        QString targetIp = txtScpTargetIp->text().trimmed();
        if (targetIp.isEmpty()) {
            txtGitLog->append("错误: 请先在脚本传输中输入目标设备 IP");
            btnMonitorUsage->setChecked(false);
            return;
        }

        // 如果没有当前传输记录，尝试从路径历史中推测
        if (currentMonitoringProcess.isEmpty()) {
            QString dir = cmbGitDir->currentText();
            if (!dir.isEmpty() && QDir(dir).exists()) {
                QDirIterator it(dir, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
                QDateTime latestTime;
                while (it.hasNext()) {
                    it.next();
                    QFileInfo fileInfo = it.fileInfo();
                    if (fileInfo.fileName() == "ModbusTCPAssistant") continue;
                    if (fileInfo.fileName().startsWith(".") || fileInfo.fileName().endsWith(".so")) continue;
                    if (fileInfo.fileName().contains(".") && !fileInfo.fileName().endsWith(".sh")) continue;
                    if (currentMonitoringProcess.isEmpty() || fileInfo.lastModified() > latestTime) {
                        currentMonitoringProcess = fileInfo.fileName();
                        latestTime = fileInfo.lastModified();
                    }
                }
            }
        }

        if (currentMonitoringProcess.isEmpty()) {
            txtGitLog->append("错误: 未找到可监测的运行程序");
            btnMonitorUsage->setChecked(false);
            return;
        }

        // 打印正在尝试监测的进程名，方便调试
        txtGitLog->append(QString("[Monitor] 尝试监测进程名: %1").arg(currentMonitoringProcess));

        txtGitLog->append(QString("开始监测进程: %1 (%2)").arg(currentMonitoringProcess).arg(targetIp));

        // 初始化文件记录
        QString logDir = QCoreApplication::applicationDirPath() + "/monitor_logs";
        QDir().mkpath(logDir);
        QString fileName = QString("%1/monitor_%2_%3.csv")
                               .arg(logDir)
                               .arg(currentMonitoringProcess)
                               .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        monitorFile = new QFile(fileName, this);
        if (monitorFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
            monitorStream = new QTextStream(monitorFile);
            *monitorStream << "Timestamp,CPU_Usage(%),Mem_Usage(%)\n";
            txtGitLog->append(QString("[Monitor] 记录已开启, 保存至: %1").arg(fileName));
        } else {
            txtGitLog->append("[Monitor] 无法创建日志文件: " + fileName);
            delete monitorFile;
            monitorFile = nullptr;
        }

        btnMonitorUsage->setText("停止检测占用");
        currentMonitoringPid = -1;
        prevProcJiffies = 0;
        prevTotalJiffies = 0;
        hasPrevCpuSample = false;
        monitorTimer->start();
    } else {
        txtGitLog->append("停止资源占用监测");
        
        if (monitorFile) {
            monitorFile->close();
            delete monitorStream;
            delete monitorFile;
            monitorFile = nullptr;
            monitorStream = nullptr;
            txtGitLog->append("[Monitor] 资源记录已保存并关闭");
        }

        btnMonitorUsage->setText("开启检测占用");
        monitorTimer->stop();
        currentMonitoringPid = -1;
        prevProcJiffies = 0;
        prevTotalJiffies = 0;
        hasPrevCpuSample = false;
        lblCpuUsage->setText("CPU: 0%");
        lblMemUsage->setText("MEM: 0%");
        chartCpu->clear();
        chartMem->clear();
    }
}

void MainWindow::onMonitorTimer() {
    QString targetIp = txtScpTargetIp->text().trimmed();
    QString password = txtScpPassword->text();
    
    if (targetIp.isEmpty() || currentMonitoringProcess.isEmpty()) return;

    // 首先获取 PID (如果未知)
    if (currentMonitoringPid <= 0) {
        QString pidCmd = QString("pidof %1").arg(currentMonitoringProcess);
        QString sshTarget = QString("root@%1").arg(targetIp);
        QStringList sshArgs;
        if (!password.isEmpty()) {
            sshArgs << "-p" << password << "ssh" << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << pidCmd;
        } else {
            sshArgs << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << pidCmd;
        }

        QProcess *pidProc = new QProcess(this);
        connect(pidProc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, pidProc, targetIp, password](int exitCode, QProcess::ExitStatus) {
            QString out = pidProc->readAllStandardOutput().trimmed();
            QString err = pidProc->readAllStandardError().trimmed();
            if (exitCode == 0 && !out.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                QStringList pids = out.split(' ', Qt::SkipEmptyParts);
#else
                QStringList pids = out.split(' ', QString::SkipEmptyParts);
#endif
                if (!pids.isEmpty()) {
                    int newPid = pids[0].toInt();
                    if (newPid > 0) {
                        currentMonitoringPid = newPid;
                        lastKnownPid = newPid; // 同步记录
                        txtGitLog->append(QString("[Monitor] 找到进程 PID: %1").arg(newPid));
                        prevProcJiffies = 0;
                        prevTotalJiffies = 0;
                        hasPrevCpuSample = false;

                        // 执行 ulimit 配置
                        if (!ulimitSet) {
                            QString ulimitCmd = "ulimit -c unlimited && echo CorePattern: && echo 'core.%e.%p' > /proc/sys/kernel/core_pattern";
                            QString sshTarget = QString("root@%1").arg(targetIp);
                            QStringList sshArgs;
                            if (!password.isEmpty()) {
                                sshArgs << "-p" << password << "ssh" << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << ulimitCmd;
                            } else {
                                sshArgs << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << ulimitCmd;
                            }
                            QProcess *uProc = new QProcess(this);
                            connect(uProc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, uProc](int code, QProcess::ExitStatus) {
                                if (code == 0) {
                                    ulimitSet = true;
                                    txtGitLog->append("[Monitor] 已在远程设置 ulimit -c unlimited");
                                }
                                uProc->deleteLater();
                            });
                            if (!password.isEmpty()) uProc->start("sshpass", sshArgs); 
                            else uProc->start("ssh", sshArgs);
                        }
                    }
                }
            } else {
                currentMonitoringPid = -1;
                lblCpuUsage->setText("CPU: 未找到");
                lblMemUsage->setText("MEM: 未找到");
                if (!err.isEmpty()) txtGitLog->append("[Monitor] pidof 错误: " + err);
            }
            pidProc->deleteLater();
        });
        if (!password.isEmpty()) {
            pidProc->start("sshpass", sshArgs);
        } else {
            pidProc->start("ssh", sshArgs);
        }
        return; 
    }

    // 使用更基础、更通用的 sh/awk 指令，避免 dash 兼容性问题和变量转义干扰
    QString checkCmd = QString(
        "cat /proc/%1/stat | awk '{print $14+$15}'; "
        "grep '^cpu ' /proc/stat | awk '{s=0; for(i=2;i<=NF;i++) s+=$i; print s}'; "
        "grep -c '^cpu[0-9]' /proc/stat; "
        "grep -E '^(MemTotal|VmRSS):' /proc/meminfo /proc/%1/status | awk '{print $2}' | xargs echo"
    ).arg(currentMonitoringPid);

    QString sshTarget = QString("root@%1").arg(targetIp);
    QStringList sshArgs;
    if (!password.isEmpty()) {
        sshArgs << "-p" << password << "ssh" << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << checkCmd;
    } else {
        sshArgs << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << checkCmd;
    }

    QProcess *proc = new QProcess(this);
    connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, proc](int exitCode, QProcess::ExitStatus) {
        QString stdoutData = proc->readAllStandardOutput().trimmed();
        QString stderrData = proc->readAllStandardError().trimmed();
        
        if (exitCode == 0) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            QStringList lines = stdoutData.split('\n', Qt::SkipEmptyParts);
#else
            QStringList lines = stdoutData.split('\n', QString::SkipEmptyParts);
#endif

            if (lines.size() >= 4) {
                quint64 procJiffies = lines[0].trimmed().toULongLong();
                quint64 totalJiffies = lines[1].trimmed().toULongLong();
                int cpuCores = qMax(1, lines[2].trimmed().toInt());

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                QStringList memParts = lines[3].split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#else
                QStringList memParts = lines[3].split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
#endif

                double memPercent = 0.0;
                if (memParts.size() >= 2) {
                    double memTotalKb = memParts[0].toDouble();
                    double vmRssKb = memParts[1].toDouble();
                    if (memTotalKb > 0.0) {
                        memPercent = (vmRssKb * 100.0) / memTotalKb;
                    }
                }

                double cpuPercent = 0.0;
                if (hasPrevCpuSample && totalJiffies > prevTotalJiffies && procJiffies >= prevProcJiffies) {
                    quint64 deltaProc = procJiffies - prevProcJiffies;
                    quint64 deltaTotal = totalJiffies - prevTotalJiffies;
                    if (deltaTotal > 0) {
                        cpuPercent = (static_cast<double>(deltaProc) * 100.0 * cpuCores) / static_cast<double>(deltaTotal);
                    }
                }

                prevProcJiffies = procJiffies;
                prevTotalJiffies = totalJiffies;
                hasPrevCpuSample = true;

                lblCpuUsage->setText(QString("CPU: %1%").arg(QString::number(qMax(0.0, cpuPercent), 'f', 1)));
                lblMemUsage->setText(QString("MEM: %1%").arg(QString::number(qMax(0.0, memPercent), 'f', 1)));
                chartCpu->addValue(qBound(0.0, cpuPercent, 100.0));
                chartMem->addValue(qBound(0.0, memPercent, 100.0));

                if (monitorStream) {
                    *monitorStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << ","
                                   << QString::number(cpuPercent, 'f', 2) << ","
                                   << QString::number(memPercent, 'f', 2) << "\n";
                    monitorStream->flush(); // 实时写入文件
                }

                // 检测 CPU 使用率是否超过设定阈值
                if (cpuPercent > cpuThresholdValue) {
                    txtGitLog->append(QString("[Monitor] 警告: CPU 使用率 (%1%) 超过阈值 (%2%)！正在停止监测并收集诊断信息...").arg(QString::number(cpuPercent, 'f', 1)).arg(cpuThresholdValue));
                    
                    int pidToDebug = currentMonitoringPid;

                    // 1. 停止监测
                    btnMonitorUsage->setChecked(false); // 这会触发 onMonitorUsageToggled() 并停止 timer

                    // 2. 执行诊断命令
                    runDiagnosticCommands(pidToDebug);
                }
            } else {
                // 如果 lines.size() < 4 且没有其他明显错误，说明 /proc/%1/status 已经读取不到了
                // 这种情况即便 ssh 命令执行成功，但结果缺失，意味着进程已经在退出过程中或已彻底消失
                lblCpuUsage->setText("CPU: 进程丢失");
                lblMemUsage->setText("MEM: 进程丢失");
                txtGitLog->append(QString("[Monitor] 数据输出不完整 (收到 %1 行)，判定进程 %2 已崩溃/退出!").arg(lines.size()).arg(currentMonitoringProcess));
                
                currentMonitoringPid = -1; // 标记失效
                runCrashDiagnostics();    // 立即执行 Core Dump 深度分析
                
                chartCpu->addValue(0);
                chartMem->addValue(0);
            }
        } else {
            // 进到这里说明 PID 失效，即程序崩溃或退出
            int crashedPid = lastKnownPid;
            currentMonitoringPid = -1; 
            hasPrevCpuSample = false;
            lblCpuUsage->setText("CPU: 进程丢失");
            lblMemUsage->setText("MEM: 进程丢失");
            txtGitLog->append(QString("[Monitor] 监测到进程 %1 已退出/崩溃!").arg(currentMonitoringProcess));
            
            // 触发 Core Dump 诊断
            runCrashDiagnostics();

            chartCpu->addValue(0);
            chartMem->addValue(0);
        }
        proc->deleteLater();
    });
    if (!password.isEmpty()) {
        proc->start("sshpass", sshArgs);
    } else {
        proc->start("ssh", sshArgs);
    }
}

void MainWindow::runDiagnosticCommands(int pid) {
    QString targetIp = txtScpTargetIp->text().trimmed();
    QString password = txtScpPassword->text();
    if (targetIp.isEmpty() || pid <= 0) return;

    QString sshTarget = QString("root@%1").arg(targetIp);
    
    // 组合所有诊断命令
    // 1. top (1次全线程快照，查看各线程 CPU)
    // 2. gdb stack trace (附加到进程，打印所有线程的调用栈，然后分离)
    // 3. strace 深度追踪 (追踪网络、读写，带耗时统计，输出到 /tmp/190.strace)
    
    QString diagCmd = QString(
        "echo '--- TOP SNAPSHOT (Threads) ---'; top -H -p %1 -b -n 1; "
        "echo '\n--- GDB LIVE ANALYSIS (Backtrace) ---'; "
        "gdb -batch -ex \"set confirm off\" -ex \"set auto-load safe-path /\" "
        "-ex \"thread apply all bt\" -ex \"detach\" -ex \"quit\" -p %1; "
        "echo '\n--- STRACE NETWORK/IO TRACE (3s) ---'; "
        "timeout 3 strace -tt -f -p %1 -o /tmp/190.strace -T -e trace=network,read,write -s 128 2>&1; "
        "echo 'Done. Trace saved to /tmp/190.strace on target. First 20 lines of trace:'; "
        "head -n 20 /tmp/190.strace"
    ).arg(pid);

    QStringList sshArgs;
    if (!password.isEmpty()) {
        sshArgs << "-p" << password << "ssh" << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << diagCmd;
    } else {
        sshArgs << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << diagCmd;
    }

    QProcess *diagProc = new QProcess(this);
    connect(diagProc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, diagProc](int exitCode, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(diagProc->readAllStandardOutput());
        QString err = QString::fromUtf8(diagProc->readAllStandardError());
        
        txtGitLog->append("\n========== [Diagnostic Output] ==========");
        if (!out.isEmpty()) txtGitLog->append(out);
        if (!err.isEmpty()) txtGitLog->append("ERROR:\n" + err);
        txtGitLog->append("=========================================\n");
        
        diagProc->deleteLater();
    });

    txtGitLog->append(QString("[Monitor] 正在远程执行诊断命令 (PID: %1)...").arg(pid));
    if (!password.isEmpty()) {
        diagProc->start("sshpass", sshArgs);
    } else {
        diagProc->start("ssh", sshArgs);
    }
}

void MainWindow::runCrashDiagnostics() {
    QString targetIp = txtScpTargetIp->text().trimmed();
    QString password = txtScpPassword->text();
    QString processName = currentMonitoringProcess;
    if (targetIp.isEmpty() || processName.isEmpty()) return;

    QString sshTarget = QString("root@%1").arg(targetIp);
    
    // 自动寻找匹配进程名的 core 文件：崩溃后给系统一点落盘时间，重试搜索。
    // 去掉对 file 命令的依赖，通过文件名和 gdb 尝试加载
    QString diagCmd = QString(
        "CORE_FILE=''; "
        "for i in 1 2 3 4 5; do "
        "  CORE_FILE=$(find / /tmp /userfs -maxdepth 2 -name \"core.%1.*\" -mmin -20 2>/dev/null | xargs ls -t 2>/dev/null | head -n 1); "
        "  [ -n \"$CORE_FILE\" ] && break; "
        "  sleep 1; "
        "done; "
        "if [ -n \"$CORE_FILE\" ] && [ -f \"$CORE_FILE\" ]; then "
        "  echo \"--- FOUND CORE DUMP: $CORE_FILE ---\"; "
        "  gdb -batch -ex \"set confirm off\" -ex \"add-auto-load-safe-path /\" "
        "  -ex \"thread apply all bt full\" -ex \"quit\" /userfs/app/%1 \"$CORE_FILE\"; "
        "else "
        "  echo \"--- NO VALID CORE DUMP FOUND (Checked /, /tmp and /userfs with retry) ---\"; "
        "  echo \"--- Candidate files (last 20 min) ---\"; "
        "  find / /tmp /userfs -maxdepth 2 -name \"core.%1.*\" -mmin -20 2>/dev/null | head -n 20; "
        "fi "
    ).arg(processName);

    QStringList sshArgs;
    if (!password.isEmpty()) {
        sshArgs << "-p" << password << "ssh" << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << diagCmd;
    } else {
        sshArgs << "-n" << "-o" << "StrictHostKeyChecking=no" << sshTarget << diagCmd;
    }

    QProcess *diagProc = new QProcess(this);
    connect(diagProc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, diagProc](int exitCode, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(diagProc->readAllStandardOutput());
        QString err = QString::fromUtf8(diagProc->readAllStandardError());
        
        txtGitLog->append("\n========== [CRASH ANALYSYS] ==========");
        if (!out.isEmpty()) txtGitLog->append(out);
        if (!err.isEmpty()) txtGitLog->append("ERROR:\n" + err);
        txtGitLog->append("========================================\n");
        btnMonitorUsage->setChecked(false); // 停止监测
        diagProc->deleteLater();
    });

    txtGitLog->append("[Monitor] 进程已退出，正在尝试检索 Core Dump 并分析...");
    if (!password.isEmpty()) {
        diagProc->start("sshpass", sshArgs);
    } else {
        diagProc->start("ssh", sshArgs);
    }
}

void MainWindow::applyGitHistoryToCombo(const QStringList &history, const QString &selectPath) {
    cmbGitDir->blockSignals(true);
    cmbGitDir->clear();
    cmbGitDir->addItems(history);
    if (!selectPath.isEmpty()) {
        cmbGitDir->setCurrentText(selectPath);
    } else if (!history.isEmpty()) {
        cmbGitDir->setCurrentIndex(0);
    }
    cmbGitDir->blockSignals(false);
    refreshGitGoalsTable();
}

void MainWindow::saveGitHistory(const QString &dir) {
    if (dir.isEmpty()) return;

    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();
    const QString absDir = QDir(dir).absolutePath();

    history.removeAll(dir);
    history.removeAll(absDir);
    history.prepend(absDir);

    while (history.size() > MAX_HISTORY) {
        history.removeLast();
    }

    settings.setValue("GitHistory", history);
    applyGitHistoryToCombo(history, absDir);
}

void MainWindow::loadGitHistory() {
    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();
    applyGitHistoryToCombo(history);
}

void MainWindow::removeGitHistoryPath(const QString &dir) {
    if (dir.isEmpty()) return;

    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();
    const QString absDir = QDir(dir).absolutePath();

    history.removeAll(dir);
    history.removeAll(absDir);
    settings.setValue("GitHistory", history);

    QString nextSelect;
    if (!history.isEmpty()) {
        nextSelect = history.first();
    }
    applyGitHistoryToCombo(history, nextSelect);
}

void MainWindow::onGitRemoveHistoryClicked() {
    QString path = cmbGitDir->currentText().trimmed();
    if (path.isEmpty()) {
        QMessageBox::information(this, "删除记忆", "请先选择要移除的记忆路径。");
        return;
    }

    const QString absPath = QDir(path).absolutePath();
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "删除记忆",
        QString("确定从记忆列表中移除路径吗？\n%1\n（不会删除磁盘上的目录）").arg(absPath),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    removeGitHistoryPath(path);
    txtGitLog->append(QString("<font color='gray'>[History] 已移除记忆路径: %1</font>").arg(absPath));
}

void MainWindow::onGitDirChanged() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (!repoDir.isEmpty() && QDir(repoDir).exists()) {
        markGoalsCompletedForMergedBranches(repoDir);
        QList<GitWorkGoal> goals = loadGitGoals(repoDir);
        syncChildGoalEndDatesFromParents(goals);
        saveGitGoals(repoDir, goals);
    }
    refreshGitGoalsTable();
}

QString MainWindow::gitGoalsRepoKey(const QString &repoDir) const {
    if (repoDir.trimmed().isEmpty()) return QString();
    return QDir(repoDir.trimmed()).absolutePath();
}

QList<GitWorkGoal> MainWindow::loadGitGoals(const QString &repoDir) const {
    QList<GitWorkGoal> goals;
    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty()) return goals;

    QSettings settings("LiChenYang", "LinuxHelper");
    settings.beginGroup("GitGoals");
    settings.beginGroup(key);
    const int size = settings.beginReadArray("items");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        GitWorkGoal g;
        g.id = settings.value("id").toString();
        g.title = settings.value("title").toString();
        g.startDate = settings.value("startDate").toString();
        g.endDate = settings.value("endDate").toString();
        g.actualDate = settings.value("actualDate").toString();
        g.parentId = settings.value("parentId").toString();
        g.branchName = settings.value("branchName").toString();
        g.started = settings.value("started").toBool();
        g.remark = settings.value("remark").toString();
        if (!g.id.isEmpty() && !g.title.isEmpty()) {
            goals.append(g);
        }
    }
    settings.endArray();
    settings.endGroup();
    settings.endGroup();
    return goals;
}

QString MainWindow::normalizeLocalBranchRef(const QString &branchRef) const {
    QString b = branchRef.trimmed();
    if (b.startsWith(QStringLiteral("* "))) {
        b = b.mid(2).trimmed();
    }
    if (b.startsWith(QStringLiteral("+ "))) {
        b = b.mid(2).trimmed();
    }
    if (b.startsWith(QStringLiteral("remotes/"))) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const QStringList parts = b.split(QLatin1Char('/'), Qt::SkipEmptyParts);
#else
        const QStringList parts = b.split(QLatin1Char('/'), QString::SkipEmptyParts);
#endif
        if (parts.size() >= 3) {
            return parts.mid(2).join(QLatin1Char('/'));
        }
    }
    return b;
}

QString MainWindow::resolveMainBranchName(const QString &repoDir) const {
    if (gitBranchExists(repoDir, QStringLiteral("main"))) {
        return QStringLiteral("main");
    }
    if (gitBranchExists(repoDir, QStringLiteral("master"))) {
        return QStringLiteral("master");
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
#ifdef Q_OS_WIN
    process.start(QStringLiteral("git.exe"),
                  QStringList() << QStringLiteral("symbolic-ref") << QStringLiteral("refs/remotes/origin/HEAD"));
#else
    process.start(QStringLiteral("git"),
                  QStringList() << QStringLiteral("symbolic-ref") << QStringLiteral("refs/remotes/origin/HEAD"));
#endif
    if (process.waitForFinished(5000) && process.exitCode() == 0) {
        const QString sym = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        const int slash = sym.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            const QString candidate = sym.mid(slash + 1);
            if (gitBranchExists(repoDir, candidate)) {
                return candidate;
            }
        }
    }
    return QString();
}

QStringList MainWindow::gitMergedLocalBranches(const QString &repoDir, const QString &intoBranch) const {
    QStringList merged;
    if (intoBranch.isEmpty()) {
        return merged;
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
#ifdef Q_OS_WIN
    process.start(QStringLiteral("git.exe"),
                  QStringList() << QStringLiteral("branch") << QStringLiteral("--merged") << intoBranch);
#else
    process.start(QStringLiteral("git"),
                  QStringList() << QStringLiteral("branch") << QStringLiteral("--merged") << intoBranch);
#endif
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        return merged;
    }

#ifdef Q_OS_WIN
    const QString output = QString::fromUtf8(process.readAllStandardOutput());
#else
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = output.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif

    for (QString line : lines) {
        const QString name = normalizeLocalBranchRef(line);
        if (name.isEmpty() || name == intoBranch) {
            continue;
        }
        if (!merged.contains(name, Qt::CaseInsensitive)) {
            merged.append(name);
        }
    }
    return merged;
}

int MainWindow::markGoalsCompletedForMergedBranches(const QString &repoDir) {
    const QString mainBranch = resolveMainBranchName(repoDir);
    if (mainBranch.isEmpty()) {
        return 0;
    }

    const QStringList mergedBranches = gitMergedLocalBranches(repoDir, mainBranch);
    if (mergedBranches.isEmpty()) {
        return 0;
    }

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    bool changed = false;
    int updated = 0;
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    for (GitWorkGoal &g : goals) {
        if (g.branchName.isEmpty() || !g.actualDate.isEmpty()) {
            continue;
        }
        const QString goalBranch = normalizeLocalBranchRef(g.branchName);
        bool isMerged = false;
        for (const QString &mb : mergedBranches) {
            if (mb.compare(goalBranch, Qt::CaseInsensitive) == 0) {
                isMerged = true;
                break;
            }
        }
        if (!isMerged) {
            continue;
        }
        g.actualDate = today;
        changed = true;
        updated++;
        txtGitLog->append(QStringLiteral("<font color='green'>[工作目标] 分支 %1 已合并入 %2，已填写实际完成日期: %3（%4）</font>")
                              .arg(goalBranch, mainBranch, today, g.title));
    }

    if (changed) {
        saveGitGoals(repoDir, goals);
    }
    return updated;
}

void MainWindow::saveGitGoals(const QString &repoDir, const QList<GitWorkGoal> &goals) {
    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty()) return;

    QSettings settings("LiChenYang", "LinuxHelper");
    settings.beginGroup("GitGoals");
    settings.beginGroup(key);
    settings.remove("");
    settings.beginWriteArray("items");
    int idx = 0;
    for (const GitWorkGoal &g : goals) {
        if (g.id.isEmpty() || g.title.isEmpty()) continue;
        settings.setArrayIndex(idx++);
        settings.setValue("id", g.id);
        settings.setValue("title", g.title);
        settings.setValue("startDate", g.startDate);
        settings.setValue("endDate", g.endDate);
        settings.setValue("actualDate", g.actualDate);
        settings.setValue("parentId", g.parentId);
        settings.setValue("branchName", g.branchName);
        settings.setValue("started", g.started);
        settings.setValue("remark", g.remark);
    }
    settings.endArray();
    settings.endGroup();
    settings.endGroup();
}

QString MainWindow::gitGoalTitleById(const QList<GitWorkGoal> &goals, const QString &id) const {
    if (id.isEmpty()) return QString();
    for (const GitWorkGoal &g : goals) {
        if (g.id == id) return g.title;
    }
    return QString();
}

bool MainWindow::isGitGoalHiddenByCollapse(const GitWorkGoal &goal, const QList<GitWorkGoal> &allGoals,
                                           const QSet<QString> &collapsedIds) const {
    QString parentId = goal.parentId;
    while (!parentId.isEmpty()) {
        if (collapsedIds.contains(parentId)) {
            return true;
        }
        const GitWorkGoal *parent = gitGoalById(allGoals, parentId);
        if (!parent) {
            break;
        }
        parentId = parent->parentId;
    }
    return false;
}

QSet<QString> &MainWindow::gitGoalCollapsedIdsForRepo(const QString &repoDir) {
    const QString key = gitGoalsRepoKey(repoDir);
    return gitGoalCollapsedByRepo[key];
}

void MainWindow::appendGitGoalTableRow(const GitWorkGoal &g, const QList<GitWorkGoal> &allGoals, int depth,
                                        bool hasChildren, bool childrenCollapsed) {
    const int row = tblGitGoals->rowCount();
    tblGitGoals->insertRow(row);

    if (hasChildren) {
        QPushButton *btnToggle = new QPushButton(childrenCollapsed ? QStringLiteral("▶")
                                                                   : QStringLiteral("▼"));
        btnToggle->setFixedSize(26, 22);
        btnToggle->setToolTip(childrenCollapsed ? QStringLiteral("展开子目标")
                                              : QStringLiteral("折叠子目标"));
        btnToggle->setFlat(true);
        const QString goalId = g.id;
        connect(btnToggle, &QPushButton::clicked, this, [this, goalId]() {
            const QString repoDir = cmbGitDir->currentText().trimmed();
            QSet<QString> &collapsed = gitGoalCollapsedIdsForRepo(repoDir);
            if (collapsed.contains(goalId)) {
                collapsed.remove(goalId);
            } else {
                collapsed.insert(goalId);
            }
            refreshGitGoalsTable();
        });
        tblGitGoals->setCellWidget(row, 0, btnToggle);
    }

    const QString indent = depth > 0 ? QString(depth * 4, QLatin1Char(' ')) : QString();
    QTableWidgetItem *titleItem = new QTableWidgetItem(indent + g.title);
    titleItem->setData(Qt::UserRole, g.id);
    tblGitGoals->setItem(row, 1, titleItem);

    QString parentDisplay = QStringLiteral("(无)");
    if (!g.parentId.isEmpty()) {
        const QString parentTitle = gitGoalTitleById(allGoals, g.parentId);
        parentDisplay = parentTitle.isEmpty() ? QStringLiteral("(已删除)") : parentTitle;
    }
    tblGitGoals->setItem(row, 2, new QTableWidgetItem(parentDisplay));

    QString startDisplay;
    if (g.startDate.isEmpty()) {
        startDisplay = g.parentId.isEmpty() ? QStringLiteral("未开始") : QStringLiteral("—");
    } else if (!g.started) {
        startDisplay = g.startDate + QStringLiteral(" (计划)");
    } else {
        startDisplay = g.startDate;
    }
    tblGitGoals->setItem(row, 3, new QTableWidgetItem(startDisplay));
    tblGitGoals->setItem(row, 4, new QTableWidgetItem(g.endDate.isEmpty() ? QStringLiteral("—") : g.endDate));
    const QString actual = g.actualDate.isEmpty() ? QStringLiteral("—") : g.actualDate;
    tblGitGoals->setItem(row, 5, new QTableWidgetItem(actual));
    const QString branchDisplay = g.branchName.isEmpty() ? QStringLiteral("—") : g.branchName;
    tblGitGoals->setItem(row, 6, new QTableWidgetItem(branchDisplay));

    QString remarkDisplay = g.remark.trimmed();
    if (remarkDisplay.isEmpty()) {
        remarkDisplay = QStringLiteral("—");
    } else {
        remarkDisplay.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (remarkDisplay.length() > 40) {
            remarkDisplay = remarkDisplay.left(40) + QStringLiteral("…");
        }
    }
    QTableWidgetItem *remarkItem = new QTableWidgetItem(remarkDisplay);
    if (!g.remark.trimmed().isEmpty()) {
        remarkItem->setToolTip(g.remark.trimmed());
    }
    tblGitGoals->setItem(row, 7, remarkItem);
}

void MainWindow::refreshGitGoalsTable() {
    if (!tblGitGoals) return;

    const QString repoDir = cmbGitDir ? cmbGitDir->currentText().trimmed() : QString();
    const QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    const QSet<QString> collapsedIds = repoDir.isEmpty() ? QSet<QString>()
                                                         : gitGoalCollapsedIdsForRepo(repoDir);

    QHash<QString, QStringList> childrenByParent;
    QStringList rootIds;
    for (const GitWorkGoal &g : goals) {
        childrenByParent[g.parentId].append(g.id);
        if (g.parentId.isEmpty()) {
            rootIds.append(g.id);
        }
    }

    tblGitGoals->setRowCount(0);

    std::function<void(const QString &, int)> visitGoal;
    visitGoal = [&](const QString &goalId, int depth) {
        const GitWorkGoal *g = gitGoalById(goals, goalId);
        if (!g || isGitGoalHiddenByCollapse(*g, goals, collapsedIds)) {
            return;
        }

        const QStringList children = childrenByParent.value(goalId);
        const bool hasChildren = !children.isEmpty();
        const bool childrenCollapsed = collapsedIds.contains(goalId);
        appendGitGoalTableRow(*g, goals, depth, hasChildren, childrenCollapsed);

        for (const QString &childId : children) {
            visitGoal(childId, depth + 1);
        }
    };

    for (const QString &rootId : rootIds) {
        visitGoal(rootId, 0);
    }

    tblGitGoals->resizeColumnsToContents();
    tblGitGoals->setColumnWidth(0, 32);
}

GitWorkGoal *MainWindow::gitGoalById(QList<GitWorkGoal> &goals, const QString &id) {
    for (GitWorkGoal &g : goals) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

const GitWorkGoal *MainWindow::gitGoalById(const QList<GitWorkGoal> &goals, const QString &id) const {
    for (const GitWorkGoal &g : goals) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

QString MainWindow::slugifyBranchName(const QString &text) const {
    QString s = text.trimmed().toLower();
    QString out;
    bool lastDash = false;
    for (const QChar &c : s) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z'))
            || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            out += c;
            lastDash = false;
        } else if (!lastDash && !out.isEmpty()) {
            out += QLatin1Char('-');
            lastDash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    if (out.length() > 50) {
        out = out.left(50);
        while (out.endsWith(QLatin1Char('-'))) {
            out.chop(1);
        }
    }
    return out.isEmpty() ? QStringLiteral("goal") : out;
}

bool MainWindow::splitGoalBranchCategory(const QString &fullBranch, QString &category,
                                          QString &namePart) const {
    category.clear();
    namePart = fullBranch.trimmed();
    if (namePart.startsWith(QStringLiteral("fix/"), Qt::CaseInsensitive)) {
        category = QStringLiteral("fix");
        namePart = namePart.mid(4);
        return true;
    }
    if (namePart.startsWith(QStringLiteral("feature/"), Qt::CaseInsensitive)) {
        category = QStringLiteral("feature");
        namePart = namePart.mid(8);
        return true;
    }
    return false;
}

QString MainWindow::buildGoalBranchName(const QString &category, const QString &namePart) const {
    const QString slug = slugifyBranchName(namePart);
    if (category == QLatin1String("fix") || category == QLatin1String("feature")) {
        return category + QLatin1Char('/') + slug;
    }
    return slug;
}

QString MainWindow::chineseTitleToPinyinSlug(const QString &title) {
    QProcess proc;
    proc.setProgram("python3");
    proc.setArguments(QStringList()
                      << QStringLiteral("-c")
                      << QStringLiteral(
                             "import sys\n"
                             "t=sys.argv[1]\n"
                             "try:\n"
                             " from pypinyin import lazy_pinyin\n"
                             " py='-'.join(lazy_pinyin(t))\n"
                             " print(py)\n"
                             "except Exception:\n"
                             " print('')\n")
                      << title);
    proc.start();
    if (proc.waitForFinished(4000) && proc.exitCode() == 0) {
        QString py = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        py = slugifyBranchName(py);
        if (!py.isEmpty() && py != QLatin1String("goal")) {
            return py;
        }
    }

    QString asciiPart = slugifyBranchName(title);
    if (!asciiPart.isEmpty() && asciiPart != QLatin1String("goal")) {
        return asciiPart;
    }
    const uint hash = qHash(title);
    return QStringLiteral("goal-%1").arg(hash % 100000, 5, 10, QChar(QLatin1Char('0')));
}

QString MainWindow::translateGoalTitleToEnglish(const QString &title) {
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QNetworkAccessManager mgr;
    QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), trimmed);
    query.addQueryItem(QStringLiteral("langpair"), QStringLiteral("zh-CN|en"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LinuxHelper/1.0"));

    QNetworkReply *reply = mgr.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(8000);
    loop.exec();

    QString translated;
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject responseData = doc.object().value(QStringLiteral("responseData")).toObject();
        translated = responseData.value(QStringLiteral("translatedText")).toString().trimmed();
    }
    reply->deleteLater();

    if (!translated.isEmpty()) {
        const QString slug = slugifyBranchName(translated);
        if (!slug.isEmpty()) {
            txtGitLog->append(QStringLiteral("[工作目标] 联网翻译分支名: %1 → %2").arg(trimmed, slug));
            return slug;
        }
    }

    txtGitLog->append(QStringLiteral("[工作目标] 联网翻译不可用，使用拼音/离线命名。"));
    return chineseTitleToPinyinSlug(trimmed);
}

QString MainWindow::suggestBranchNameFromTitle(const QString &title) {
    return translateGoalTitleToEnglish(title);
}

bool MainWindow::gitBranchExists(const QString &repoDir, const QString &branchName) const {
    QProcess process;
    process.setWorkingDirectory(repoDir);
#ifdef Q_OS_WIN
    process.start(QStringLiteral("git.exe"), QStringList() << QStringLiteral("show-ref")
                                                           << QStringLiteral("--verify")
                                                           << QStringLiteral("--quiet")
                                                           << QStringLiteral("refs/heads/") + branchName);
#else
    process.start(QStringLiteral("git"), QStringList() << QStringLiteral("show-ref")
                                                       << QStringLiteral("--verify")
                                                       << QStringLiteral("--quiet")
                                                       << QStringLiteral("refs/heads/") + branchName);
#endif
    process.waitForFinished(5000);
    return process.exitCode() == 0;
}

bool MainWindow::createGitBranch(const QString &repoDir, const QString &branchName) {
    QProcess process;
    process.setWorkingDirectory(repoDir);
#ifdef Q_OS_WIN
    process.setProgram(QStringLiteral("git.exe"));
#else
    process.setProgram(QStringLiteral("git"));
#endif
    process.setArguments(QStringList() << QStringLiteral("checkout") << QStringLiteral("-b") << branchName);
    process.start();
    if (!process.waitForFinished(30000)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[工作目标] 创建分支超时</font>"));
        return false;
    }
    const QString stdoutData = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrData = QString::fromLocal8Bit(process.readAllStandardError());
    if (process.exitCode() != 0) {
        txtGitLog->append(QStringLiteral("<font color='red'>[工作目标] 创建分支失败: %1</font>").arg(stderrData.trimmed()));
        return false;
    }
    txtGitLog->append(QStringLiteral("<font color='green'>[工作目标] 已创建并切换到分支: %1</font>").arg(branchName));
    if (!stdoutData.trimmed().isEmpty()) {
        txtGitLog->append(stdoutData.trimmed());
    }
    onGitRefreshBranchesClicked();
    const int index = cmbGitBranches->findText(branchName);
    if (index >= 0) {
        cmbGitBranches->setCurrentIndex(index);
    }
    return true;
}

void MainWindow::fillAncestorStartDates(QList<GitWorkGoal> &goals, const QString &goalId,
                                        const QString &dateStr) {
    const GitWorkGoal *g = gitGoalById(goals, goalId);
    if (!g || g->parentId.isEmpty()) {
        return;
    }
    GitWorkGoal *parent = gitGoalById(goals, g->parentId);
    if (!parent) {
        return;
    }
    if (parent->startDate.isEmpty()) {
        parent->startDate = dateStr;
        parent->started = true;
    }
    fillAncestorStartDates(goals, parent->id, dateStr);
}

bool MainWindow::promptGitGoalStartDialog(const QString &goalTitle, QString &branchName,
                                          bool &createBranch) {
    createBranch = false;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("目标开始"));
    dlg.setMinimumWidth(480);

    QLabel *lblInfo = new QLabel(
        QStringLiteral("目标「%1」即将开始。\n请确认分支类型与名称（名称可由中文自动翻译），创建分支时将加上 fix/ 或 feature/ 前缀：")
            .arg(goalTitle));
    lblInfo->setWordWrap(true);

    QRadioButton *rdFeature = new QRadioButton(QStringLiteral("feature/（新功能）"));
    QRadioButton *rdFix = new QRadioButton(QStringLiteral("fix/（问题修复）"));
    rdFeature->setChecked(true);
    QButtonGroup *grpCategory = new QButtonGroup(&dlg);
    grpCategory->addButton(rdFeature, 0);
    grpCategory->addButton(rdFix, 1);

    QWidget *rowCategory = new QWidget();
    QHBoxLayout *layCategory = new QHBoxLayout(rowCategory);
    layCategory->setContentsMargins(0, 0, 0, 0);
    layCategory->addWidget(new QLabel(QStringLiteral("分支类型:")));
    layCategory->addWidget(rdFeature);
    layCategory->addWidget(rdFix);
    layCategory->addStretch();

    QLineEdit *txtBranch = new QLineEdit();
    txtBranch->setPlaceholderText(QStringLiteral("翻译后的英文名，如 login-timeout"));

    QLabel *lblPreview = new QLabel();
    lblPreview->setWordWrap(true);
    lblPreview->setStyleSheet(QStringLiteral("color: #1565c0; font-weight: bold;"));

    auto selectedCategory = [&]() -> QString {
        return rdFix->isChecked() ? QStringLiteral("fix") : QStringLiteral("feature");
    };

    auto updatePreview = [&]() {
        QString namePart = txtBranch->text().trimmed();
        QString cat;
        QString stripped;
        if (splitGoalBranchCategory(namePart, cat, stripped)) {
            namePart = stripped;
        }
        const QString full = buildGoalBranchName(selectedCategory(), namePart);
        lblPreview->setText(QStringLiteral("创建分支时将使用: %1").arg(full));
    };

    QPushButton *btnConfirmBranch = new QPushButton(QStringLiteral("确认并开始（创建分支）"));
    btnConfirmBranch->setStyleSheet(QStringLiteral("font-weight: bold;"));
    QPushButton *btnStartOnly = new QPushButton(QStringLiteral("仅记录开始（不创建分支）"));
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"));

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addWidget(lblInfo);
    layout->addWidget(rowCategory);
    layout->addWidget(new QLabel(QStringLiteral("分支名称:")));
    layout->addWidget(txtBranch);
    layout->addWidget(lblPreview);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(btnConfirmBranch);
    btnLayout->addWidget(btnStartOnly);
    btnLayout->addWidget(btnCancel);
    layout->addLayout(btnLayout);

    QString existingCategory;
    QString existingNamePart;
    if (splitGoalBranchCategory(branchName, existingCategory, existingNamePart)) {
        if (existingCategory == QLatin1String("fix")) {
            rdFix->setChecked(true);
        } else {
            rdFeature->setChecked(true);
        }
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString suggested = suggestBranchNameFromTitle(goalTitle);
    QApplication::restoreOverrideCursor();

    if (!existingNamePart.isEmpty()) {
        txtBranch->setText(existingNamePart);
    } else {
        txtBranch->setText(suggested);
    }
    updatePreview();

    connect(txtBranch, &QLineEdit::textChanged, &dlg, updatePreview);
    connect(rdFeature, &QRadioButton::toggled, &dlg, updatePreview);
    connect(rdFix, &QRadioButton::toggled, &dlg, updatePreview);

    auto acceptWithBranch = [&](bool doCreate) {
        QString namePart = txtBranch->text().trimmed();
        QString ignoredCat;
        splitGoalBranchCategory(namePart, ignoredCat, namePart);
        const QString slug = slugifyBranchName(namePart);
        if (slug.isEmpty() || slug == QLatin1String("goal")) {
            QMessageBox::warning(&dlg, QStringLiteral("目标开始"), QStringLiteral("请输入有效的英文分支名。"));
            return;
        }
        if (doCreate) {
            branchName = buildGoalBranchName(selectedCategory(), slug);
        } else {
            branchName = slug;
        }
        createBranch = doCreate;
        dlg.accept();
    };

    connect(btnConfirmBranch, &QPushButton::clicked, &dlg, [&]() { acceptWithBranch(true); });
    connect(btnStartOnly, &QPushButton::clicked, &dlg, [&]() { acceptWithBranch(false); });
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    return dlg.exec() == QDialog::Accepted;
}

bool MainWindow::editGitWorkGoalDialog(GitWorkGoal &goal, const QList<GitWorkGoal> &allGoals,
                                      const QString &excludeGoalId) {
    QDialog dlg(this);
    dlg.setWindowTitle(excludeGoalId.isEmpty() ? QStringLiteral("添加工作目标")
                                               : QStringLiteral("编辑工作目标"));
    dlg.setMinimumWidth(480);

    QLineEdit *txtTitle = new QLineEdit(goal.title);
    QComboBox *cmbParent = new QComboBox();
    cmbParent->addItem(QStringLiteral("(无)"), QString());
    for (const GitWorkGoal &g : allGoals) {
        if (g.id == excludeGoalId) continue;
        cmbParent->addItem(g.title, g.id);
    }
    if (goal.parentId.isEmpty()) {
        cmbParent->setCurrentIndex(0);
    } else {
        const int pIdx = cmbParent->findData(goal.parentId);
        if (pIdx >= 0) cmbParent->setCurrentIndex(pIdx);
    }

    QLabel *lblPlanHint = new QLabel(QStringLiteral("无父目标时需填写计划开始与计划结束；子目标的计划结束继承父目标，开始日期在「目标开始」时记录。"));
    lblPlanHint->setWordWrap(true);
    lblPlanHint->setStyleSheet(QStringLiteral("color: #555; font-size: 11px;"));

    QDateEdit *dateStart = new QDateEdit(QDate::currentDate());
    dateStart->setCalendarPopup(true);
    dateStart->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    QDateEdit *dateEnd = new QDateEdit(QDate::currentDate().addDays(7));
    dateEnd->setCalendarPopup(true);
    dateEnd->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));

    QWidget *rowStart = new QWidget();
    QHBoxLayout *layStart = new QHBoxLayout(rowStart);
    layStart->setContentsMargins(0, 0, 0, 0);
    layStart->addWidget(new QLabel(QStringLiteral("计划开始:")));
    layStart->addWidget(dateStart, 1);

    QWidget *rowEnd = new QWidget();
    QHBoxLayout *layEnd = new QHBoxLayout(rowEnd);
    layEnd->setContentsMargins(0, 0, 0, 0);
    layEnd->addWidget(new QLabel(QStringLiteral("计划结束:")));
    layEnd->addWidget(dateEnd, 1);

    QLabel *lblChildPlanEnd = new QLabel();
    lblChildPlanEnd->setWordWrap(true);
    lblChildPlanEnd->setStyleSheet(QStringLiteral("color: #1565c0;"));

    QTextEdit *txtRemark = new QTextEdit(goal.remark);
    txtRemark->setPlaceholderText(QStringLiteral("可选：记录需求链接、问题现象、验收标准等"));
    txtRemark->setMaximumHeight(100);

    QCheckBox *chkActual = new QCheckBox(QStringLiteral("已填写实际完成日期"));
    QDateEdit *dateActual = new QDateEdit(QDate::currentDate());
    dateActual->setCalendarPopup(true);
    dateActual->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    dateActual->setEnabled(false);

    if (!goal.startDate.isEmpty()) {
        const QDate d = QDate::fromString(goal.startDate, QStringLiteral("yyyy-MM-dd"));
        if (d.isValid()) dateStart->setDate(d);
    }
    if (!goal.endDate.isEmpty()) {
        const QDate d = QDate::fromString(goal.endDate, QStringLiteral("yyyy-MM-dd"));
        if (d.isValid()) dateEnd->setDate(d);
    }
    if (!goal.actualDate.isEmpty()) {
        const QDate d = QDate::fromString(goal.actualDate, QStringLiteral("yyyy-MM-dd"));
        if (d.isValid()) {
            chkActual->setChecked(true);
            dateActual->setEnabled(true);
            dateActual->setDate(d);
        }
    }

    auto updatePlanFieldsVisibility = [&]() {
        const bool isRoot = cmbParent->currentData().toString().isEmpty();
        rowStart->setVisible(isRoot);
        rowEnd->setVisible(isRoot);
        lblChildPlanEnd->setVisible(!isRoot);
        if (!isRoot) {
            const QString parentId = cmbParent->currentData().toString();
            const GitWorkGoal *parent = gitGoalById(allGoals, parentId);
            if (parent && !parent->endDate.isEmpty()) {
                lblChildPlanEnd->setText(
                    QStringLiteral("计划结束（继承父目标「%1」）: %2")
                        .arg(parent->title, parent->endDate));
            } else {
                lblChildPlanEnd->setText(QStringLiteral("计划结束: 父目标尚未设置计划结束，请先编辑父目标。"));
            }
        }
    };
    connect(cmbParent, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, updatePlanFieldsVisibility);
    connect(chkActual, &QCheckBox::toggled, dateActual, &QWidget::setEnabled);
    updatePlanFieldsVisibility();

    QFormLayout *form = new QFormLayout();
    form->addRow(QStringLiteral("目标名称:"), txtTitle);
    form->addRow(QStringLiteral("父目标:"), cmbParent);
    form->addRow(lblPlanHint);
    form->addRow(rowStart);
    form->addRow(rowEnd);
    form->addRow(lblChildPlanEnd);
    form->addRow(QStringLiteral("备注:"), txtRemark);
    form->addRow(chkActual);
    form->addRow(QStringLiteral("实际完成:"), dateActual);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    const QString title = txtTitle->text().trimmed();
    if (title.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("工作目标"), QStringLiteral("请填写目标名称。"));
        return false;
    }

    const QString parentId = cmbParent->currentData().toString();
    if (!excludeGoalId.isEmpty() && parentId == excludeGoalId) {
        QMessageBox::warning(this, QStringLiteral("工作目标"), QStringLiteral("不能将目标自身设为父目标。"));
        return false;
    }

    const bool isRoot = parentId.isEmpty();
    if (isRoot) {
        if (dateEnd->date() < dateStart->date()) {
            QMessageBox::warning(this, QStringLiteral("工作目标"), QStringLiteral("计划结束日期不能早于计划开始日期。"));
            return false;
        }
        goal.startDate = dateStart->date().toString(QStringLiteral("yyyy-MM-dd"));
        goal.endDate = dateEnd->date().toString(QStringLiteral("yyyy-MM-dd"));
    } else {
        const GitWorkGoal *parent = gitGoalById(allGoals, parentId);
        if (!parent || parent->endDate.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("工作目标"),
                                 QStringLiteral("父目标尚未设置计划结束日期，请先为父目标填写计划结束。"));
            return false;
        }
        goal.endDate = parent->endDate;
        if (excludeGoalId.isEmpty()) {
            goal.startDate.clear();
            goal.started = false;
        }
    }

    goal.title = title;
    goal.parentId = parentId;
    goal.remark = txtRemark->toPlainText().trimmed();
    goal.actualDate = chkActual->isChecked() ? dateActual->date().toString(QStringLiteral("yyyy-MM-dd")) : QString();
    return true;
}

void MainWindow::onGitGoalAddClicked() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("工作目标"), QStringLiteral("请先选择 Git 仓库目录。"));
        return;
    }

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    GitWorkGoal goal;
    goal.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    goal.started = false;

    if (!editGitWorkGoalDialog(goal, goals)) return;

    goals.append(goal);
    syncChildGoalEndDatesFromParents(goals);
    saveGitGoals(repoDir, goals);
    refreshGitGoalsTable();
    txtGitLog->append(QString("[工作目标] 已添加: %1").arg(goal.title));
}

void MainWindow::syncChildGoalEndDatesFromParents(QList<GitWorkGoal> &goals) {
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 32) {
        changed = false;
        for (GitWorkGoal &g : goals) {
            if (g.parentId.isEmpty()) {
                continue;
            }
            const GitWorkGoal *parent = gitGoalById(goals, g.parentId);
            if (!parent || parent->endDate.isEmpty()) {
                continue;
            }
            if (g.endDate != parent->endDate) {
                g.endDate = parent->endDate;
                changed = true;
            }
        }
    }
}

void MainWindow::onGitGoalStartClicked() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("工作目标"), QStringLiteral("请先选择 Git 仓库目录。"));
        return;
    }
    if (!QDir(repoDir).exists()) {
        QMessageBox::warning(this, QStringLiteral("工作目标"), QStringLiteral("当前仓库目录不存在。"));
        return;
    }

    const int row = tblGitGoals->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("工作目标"), QStringLiteral("请先选择要开始的目标。"));
        return;
    }

    QTableWidgetItem *idItem = tblGitGoals->item(row, 1);
    if (!idItem) return;
    const QString goalId = idItem->data(Qt::UserRole).toString();

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    GitWorkGoal *goal = gitGoalById(goals, goalId);
    if (!goal) return;

    const bool isRoot = goal->parentId.isEmpty();

    if (isRoot && goal->endDate.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("目标开始"),
                             QStringLiteral("请先在编辑中为该根目标填写计划结束日期。"));
        return;
    }

    if (goal->started) {
        if (QMessageBox::question(
                this, QStringLiteral("目标开始"),
                QStringLiteral("该目标已开始（开始日期: %1）。是否重新记录开始日期并再次确认分支？")
                    .arg(goal->startDate),
                QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
    }

    QString branchName = goal->branchName;
    bool createBranch = false;
    if (!promptGitGoalStartDialog(goal->title, branchName, createBranch)) {
        return;
    }

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    goal->startDate = today;
    goal->started = true;
    fillAncestorStartDates(goals, goal->id, today);

    if (createBranch) {
        if (gitBranchExists(repoDir, branchName)) {
            QMessageBox::warning(this, QStringLiteral("目标开始"),
                                 QStringLiteral("分支 %1 已存在，请修改分支名或选择不创建分支。").arg(branchName));
            return;
        }
        if (!createGitBranch(repoDir, branchName)) {
            return;
        }
        goal->branchName = branchName;
    }

    saveGitGoals(repoDir, goals);
    refreshGitGoalsTable();
    txtGitLog->append(QStringLiteral("[工作目标] 已开始: %1，开始日期 %2%3")
                          .arg(goal->title,
                               today,
                               createBranch ? QStringLiteral("，分支 ") + branchName : QStringLiteral("（未创建分支）")));
}

void MainWindow::onGitGoalEditClicked() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        QMessageBox::information(this, "工作目标", "请先选择 Git 仓库目录。");
        return;
    }

    const int row = tblGitGoals->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "工作目标", "请先选择要编辑的目标。");
        return;
    }

    QTableWidgetItem *idItem = tblGitGoals->item(row, 1);
    if (!idItem) return;
    const QString goalId = idItem->data(Qt::UserRole).toString();

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    for (int i = 0; i < goals.size(); ++i) {
        if (goals[i].id != goalId) continue;
        GitWorkGoal edited = goals[i];
        if (!editGitWorkGoalDialog(edited, goals, goalId)) return;
        goals[i] = edited;
        syncChildGoalEndDatesFromParents(goals);
        saveGitGoals(repoDir, goals);
        refreshGitGoalsTable();
        txtGitLog->append(QString("[工作目标] 已更新: %1").arg(edited.title));
        return;
    }
}

void MainWindow::onGitGoalDeleteClicked() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        QMessageBox::information(this, "工作目标", "请先选择 Git 仓库目录。");
        return;
    }

    const int row = tblGitGoals->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "工作目标", "请先选择要删除的目标。");
        return;
    }

    QTableWidgetItem *idItem = tblGitGoals->item(row, 1);
    if (!idItem) return;
    const QString goalId = idItem->data(Qt::UserRole).toString();

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    const QString title = gitGoalTitleById(goals, goalId);
    if (title.isEmpty()) {
        return;
    }

    if (QMessageBox::question(this, "删除工作目标",
                              QString("确定删除目标「%1」吗？\n其子目标将变为无父目标。").arg(title),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QList<GitWorkGoal> updated;
    for (GitWorkGoal g : goals) {
        if (g.id == goalId) continue;
        if (g.parentId == goalId) g.parentId.clear();
        updated.append(g);
    }
    saveGitGoals(repoDir, updated);
    refreshGitGoalsTable();
    txtGitLog->append(QString("[工作目标] 已删除: %1").arg(title));
}

void MainWindow::setupRegisterTable(QTableWidget *table) {
    if(!table) return;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels(QStringList() << "地址" << "注释" << "寄存器格式");
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &MainWindow::onRegisterMapContextMenu);
    
    // Add some default rows for testing
    table->setRowCount(51);
    for(int i=0; i<50; i++) {
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        table->setItem(i, 1, new QTableWidgetItem(""));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(i)));
    }

    // Keep one blank row at bottom for direct data entry.
    table->setItem(50, 0, new QTableWidgetItem(""));
    table->setItem(50, 1, new QTableWidgetItem(""));
    table->setItem(50, 2, new QTableWidgetItem(""));
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
        uint32_t val32;
        if (fmt == "32-bit Float") {
            // CDAB: [CD, AB] -> CD is high 16 bits of quint32 (which is low part of float? No, CDAB means big-endian order for the 32-bit float but words swapped?)
            // Normally CDAB for float means: Word0=CD, Word1=AB. 
            // If float is IEEE754: [Byte3 Byte2 Byte1 Byte0]
            // CDAB usually means [Byte3 Byte2] [Byte1 Byte1]
            val32 = ((uint32_t)target->getRegister(addr) << 16) | target->getRegister(addr + 1);
        } else {
            val32 = ((uint32_t)target->getRegister(addr) << 16) | target->getRegister(addr + 1);
        }
        if (fmt == "32-bit Signed") display = QString::number((int32_t)val32);
        else if (fmt == "32-bit Unsigned") display = QString::number(val32);
        else if (fmt == "32-bit Float") {
            float f;
            memcpy(&f, &val32, 4);
            display = QString::number(f, 'f', 2);
        }
    } else if (fmt == "64-bit Float") {
        quint64 raw = 0;
        // GHEF CDAB：addr 起为 GH, EF, CD, AB
        raw |= (quint64)target->getRegister(addr + 3);
        raw |= ((quint64)target->getRegister(addr + 2) << 16);
        raw |= ((quint64)target->getRegister(addr + 1) << 32);
        raw |= ((quint64)target->getRegister(addr) << 48);
        double d = 0.0;
        memcpy(&d, &raw, sizeof(double));
        display = QString::number(d, 'f', 6);
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
                // 只需要填写首寄存器，后续寄存器按地址顺序自动参与转换。
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
        } else if (fmt == "64-bit Float") {
            double d = valStr.toDouble(&ok);
            if (ok) {
                quint64 raw = 0;
                memcpy(&raw, &d, sizeof(double));
                target->setRegister(addr,     (quint16)((raw >> 48) & 0xFFFF));
                target->setRegister(addr + 1, (quint16)((raw >> 32) & 0xFFFF));
                target->setRegister(addr + 2, (quint16)((raw >> 16) & 0xFFFF));
                target->setRegister(addr + 3, (quint16)(raw & 0xFFFF));
                refreshSimRowDisplay(table, row);
                refreshSimRowDisplay(table, row + 1);
                refreshSimRowDisplay(table, row + 2);
                refreshSimRowDisplay(table, row + 3);
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

        if (!ok && fmt != "32-bit Float" && fmt != "64-bit Float" && !fmt.startsWith("32-bit")) {
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
    
    QTableWidgetItem *item = table->item(row, 0); // Address
    QTableWidgetItem *formatItem = table->item(row, 2); // Format is col 2
    
    bool ok;
    if(item) {
        int addr = item->text().toInt(&ok);
        if(ok) {
            spinReadStartAddr->setValue(addr);
            spinWriteStartAddr->setValue(addr);
        }
    }

    if(formatItem) {
        QString fmtText = formatItem->text().toUpper();
        int readQty = 1;
        int formatIndex = 0; // Default Decimal

        if (fmtText.contains("LREAL")) {
            formatIndex = 4; // 64-bit Float
            readQty = 4;
        } else if (fmtText.contains("REAL")) {
            formatIndex = 3; // 32-bit Float
            readQty = 2;
        } else if (fmtText.contains("BOOL")) {
            formatIndex = 2; // Binary
            readQty = 1;
        } else if (fmtText.contains("UINT") || fmtText.contains("INT")) {
            formatIndex = 0; // Decimal
            readQty = 1;
        }

        cmbDisplayFormat->setCurrentIndex(formatIndex);
        cmbWriteFormat->setCurrentIndex(formatIndex);
        spinReadQuantity->setValue(readQty);
        spinWriteQuantity->setValue(readQty);
    }

    if (chkAutoReadOnMapClick && chkAutoReadOnMapClick->isChecked()) {
        onReadHoldingRegistersClicked();
    }
    // Note: Writing is complex as it needs a value, so we only trigger if explicit.
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

bool MainWindow::isRegisterMapRowEmpty(const QTableWidget *table, int row) const {
    if (!table || row < 0 || row >= table->rowCount()) return true;

    QString addr = table->item(row, 0) ? table->item(row, 0)->text().trimmed() : "";
    QString cmt = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : "";
    QString regFmt = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : "";
    return addr.isEmpty() && cmt.isEmpty() && regFmt.isEmpty();
}

void MainWindow::ensureRegisterMapEditableTailRow(QTableWidget *table) {
    if (!table) return;
    if (table->rowCount() <= 0) table->setRowCount(1);

    table->blockSignals(true);

    int rows = table->rowCount();
    while (rows > 1 && isRegisterMapRowEmpty(table, rows - 1) && isRegisterMapRowEmpty(table, rows - 2)) {
        table->removeRow(rows - 1);
        rows = table->rowCount();
    }

    rows = table->rowCount();
    if (rows <= 0 || !isRegisterMapRowEmpty(table, rows - 1)) {
        int newRow = rows;
        table->insertRow(newRow);
        table->setItem(newRow, 0, new QTableWidgetItem(""));
        table->setItem(newRow, 1, new QTableWidgetItem(""));
        table->setItem(newRow, 2, new QTableWidgetItem(""));
    } else {
        if (!table->item(rows - 1, 0)) table->setItem(rows - 1, 0, new QTableWidgetItem(""));
        if (!table->item(rows - 1, 1)) table->setItem(rows - 1, 1, new QTableWidgetItem(""));
        if (!table->item(rows - 1, 2)) table->setItem(rows - 1, 2, new QTableWidgetItem(""));
    }

    table->blockSignals(false);
}

void MainWindow::copyRegisterMapSelection(QTableWidget *table) {
    if (!table) return;

    QList<QTableWidgetSelectionRange> ranges = table->selectedRanges();
    if (ranges.isEmpty()) return;

    QTableWidgetSelectionRange r = ranges.first();
    QStringList lines;
    for (int row = r.topRow(); row <= r.bottomRow(); ++row) {
        QStringList cols;
        for (int col = r.leftColumn(); col <= r.rightColumn(); ++col) {
            QTableWidgetItem *item = table->item(row, col);
            cols << (item ? item->text() : "");
        }
        lines << cols.join("\t");
    }

    QGuiApplication::clipboard()->setText(lines.join("\n"));
}

void MainWindow::pasteRegisterMapFromClipboard(QTableWidget *table, int startRow, int startColumn) {
    if (!table) return;
    if (startColumn < 0) startColumn = 0;
    if (startColumn >= table->columnCount()) return;

    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList rows = text.split('\n', Qt::SkipEmptyParts);
#else
    QStringList rows = text.split('\n', QString::SkipEmptyParts);
#endif
    if (rows.isEmpty()) return;

    table->blockSignals(true);
    for (int r = 0; r < rows.size(); ++r) {
        QString rowText = rows[r];
        if (rowText.endsWith('\r')) rowText.chop(1);
        QStringList cols = rowText.split('\t');

        int targetRow = startRow + r;
        if (targetRow >= table->rowCount()) {
            table->setRowCount(targetRow + 1);
        }

        for (int c = 0; c < cols.size(); ++c) {
            int targetCol = startColumn + c;
            if (targetCol >= table->columnCount()) break;
            if (!table->item(targetRow, targetCol)) {
                table->setItem(targetRow, targetCol, new QTableWidgetItem());
            }
            table->item(targetRow, targetCol)->setText(cols[c]);
        }
    }
    table->blockSignals(false);

    ensureRegisterMapEditableTailRow(table);
    syncSimulatorTablesFromMaps();
}

void MainWindow::onRegisterMapContextMenu(const QPoint &pos) {
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;

    QModelIndex index = table->indexAt(pos);
    int targetRow = index.isValid() ? index.row() : table->currentRow();
    int targetCol = index.isValid() ? index.column() : table->currentColumn();
    if (targetRow < 0) targetRow = table->rowCount() > 0 ? table->rowCount() - 1 : 0;
    if (targetCol < 0) targetCol = 0;

    QMenu menu(this);
    QAction *copyAction = menu.addAction("复制");
    QAction *pasteAction = menu.addAction("粘贴");

    copyAction->setEnabled(!table->selectedRanges().isEmpty());

    QAction *selected = menu.exec(table->viewport()->mapToGlobal(pos));
    if (selected == copyAction) {
        copyRegisterMapSelection(table);
    } else if (selected == pasteAction) {
        pasteRegisterMapFromClipboard(table, targetRow, targetCol);
    }
}

void MainWindow::onRegisterTableChanged(int row, int column) {
    Q_UNUSED(row);
    Q_UNUSED(column);
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (table) {
        ensureRegisterMapEditableTailRow(table);
    }
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
            QTableWidgetItem *regFmtItem = table->item(i, 2);
            settings.setValue("addr", addrItem ? addrItem->text() : "");
            settings.setValue("cmt", cmtItem ? cmtItem->text() : "");
            settings.setValue("regfmt", regFmtItem ? regFmtItem->text() : "");
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

            table->blockSignals(true);
            
            for (int i = 0; i < size; ++i) {
                settings.setArrayIndex(i);
                QString addr = settings.value("addr").toString();
                QString cmt = settings.value("cmt").toString();
                QString regfmt = settings.value("regfmt").toString();
                
                if (!table->item(i, 0)) table->setItem(i, 0, new QTableWidgetItem());
                if (!table->item(i, 1)) table->setItem(i, 1, new QTableWidgetItem());
                if (!table->item(i, 2)) table->setItem(i, 2, new QTableWidgetItem());
                
                table->item(i, 0)->setText(addr);
                table->item(i, 1)->setText(cmt);
                table->item(i, 2)->setText(regfmt);
            }

            table->blockSignals(false);
        }
        settings.endArray();
    };

    loadTable(tblAGV, "Map_AGV");
    loadTable(tblRobot, "Map_Robot");
    ensureRegisterMapEditableTailRow(tblAGV);
    ensureRegisterMapEditableTailRow(tblRobot);
    syncSimulatorTablesFromMaps();
}

void MainWindow::onExportRegisterMapClicked() {
    QString fn = QFileDialog::getSaveFileName(this, "导出地址映射表", QString(), "CSV Files (*.csv)");
    if (fn.isEmpty()) return;

    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存文件");
        return;
    }

    QTextStream out(&f);
    out.setGenerateByteOrderMark(true); // 保证 Excel 正常打开中文
    out << "Tab,Address,Comment,RegisterFormat\n";

    auto exportTable = [&](QTableWidget *table, const QString &tabName) {
        if (!table) return;
        for (int i = 0; i < table->rowCount(); ++i) {
            QString addr = table->item(i, 0) ? table->item(i, 0)->text() : "";
            QString cmt = table->item(i, 1) ? table->item(i, 1)->text() : "";
            QString regFmt = table->item(i, 2) ? table->item(i, 2)->text() : "";
            
            if (addr.isEmpty() && cmt.isEmpty() && regFmt.isEmpty()) continue;
            
            // CSV 规范：如果内容包含逗号或双引号，需要用双引号包围，双引号需转义为两个双引号
            if (cmt.contains(",") || cmt.contains("\"")) {
                cmt = "\"" + cmt.replace("\"", "\"\"") + "\"";
            }
            if (regFmt.contains(",") || regFmt.contains("\"")) {
                regFmt = "\"" + regFmt.replace("\"", "\"\"") + "\"";
            }
            
            out << tabName << "," << addr << "," << cmt << "," << regFmt << "\n";
        }
    };

    exportTable(tblAGV, "AGV");
    exportTable(tblRobot, "Robot");

    f.close();
    QMessageBox::information(this, "成功", "地址映射表已以 CSV 格式导出。");
}

void MainWindow::onImportRegisterMapClicked() {
    QString fn = QFileDialog::getOpenFileName(this, "导入地址映射表", QString(), "CSV Files (*.csv);;All Files (*)");
    if (fn.isEmpty()) return;

    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件");
        return;
    }

    QTextStream in(&f);
    QString header = in.readLine();
    
    auto clearTable = [&](QTableWidget* table) {
        table->blockSignals(true);
        for(int i=0; i<table->rowCount(); i++) {
            if (!table->item(i, 0)) table->setItem(i, 0, new QTableWidgetItem());
            if (!table->item(i, 1)) table->setItem(i, 1, new QTableWidgetItem());
            if (!table->item(i, 2)) table->setItem(i, 2, new QTableWidgetItem());
            table->item(i, 0)->setText("");
            table->item(i, 1)->setText("");
            table->item(i, 2)->setText("");
        }
        table->blockSignals(false);
    };

    clearTable(tblAGV);
    clearTable(tblRobot);

    QMap<QString, int> tabRowCounters;
    tabRowCounters["agv"] = 0;
    tabRowCounters["robot"] = 0;

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        
        // 健壮的 CSV 解析逻辑
        QStringList parts;
        bool inQuotes = false;
        QString field;
        for (int i = 0; i < line.length(); ++i) {
            QChar c = line[i];
            if (c == '"') {
                if (inQuotes && i + 1 < line.length() && line[i+1] == '"') {
                    field += '"'; // 处理转义的双引号
                    i++;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == ',' && !inQuotes) {
                parts.append(field);
                field.clear();
            } else {
                field += c;
            }
        }
        parts.append(field);

        if (parts.size() < 3) continue;

        QString tabStr = parts[0].trimmed().toLower();
        QString addr = parts[1].trimmed();
        QString cmt = parts[2].trimmed();
        QString regFmt = parts.size() > 3 ? parts[3].trimmed() : addr;

        QTableWidget *table = (tabStr == "robot" || tabStr == "机器人") ? tblRobot : tblAGV;
        QString key = (tabStr == "robot" || tabStr == "机器人") ? "robot" : "agv";
        int row = tabRowCounters[key]++;

        if (row >= table->rowCount()) table->setRowCount(row + 1);
        
        table->blockSignals(true);
        if (!table->item(row, 0)) table->setItem(row, 0, new QTableWidgetItem());
        if (!table->item(row, 1)) table->setItem(row, 1, new QTableWidgetItem());
        if (!table->item(row, 2)) table->setItem(row, 2, new QTableWidgetItem());
        table->item(row, 0)->setText(addr);
        table->item(row, 1)->setText(cmt);
        table->item(row, 2)->setText(regFmt);
        table->blockSignals(false);
    }

    f.close();
    ensureRegisterMapEditableTailRow(tblAGV);
    ensureRegisterMapEditableTailRow(tblRobot);
    saveRegisterTables();
    syncSimulatorTablesFromMaps();
    QMessageBox::information(this, "成功", "地址映射表 CSV 导入成功。");
}

void MainWindow::onSearchMapTextFinished()
{
    onSearchMapClicked();
}

void MainWindow::onSearchMapClicked()
{
    QString searchText = txtSearchMap->text().trimmed();
    if (searchText.isEmpty()) return;

    QTableWidget *currentTable = qobject_cast<QTableWidget*>(tabRegisterMaps->currentWidget());
    if (!currentTable) return;

    // 获取当前选中的位置作为起点
    int startRow = 0;
    int startCol = 0;
    QTableWidgetItem *currentItem = currentTable->currentItem();
    if (currentItem) {
        startRow = currentItem->row();
        startCol = currentItem->column() + 1; // 从下一个单元格开始搜
        if (startCol >= currentTable->columnCount()) {
            startCol = 0;
            startRow++;
        }
    }

    int rowCount = currentTable->rowCount();
    int colCount = currentTable->columnCount();

    // 循环搜索
    for (int i = 0; i < rowCount; i++) {
        int r = (startRow + i) % rowCount;
        int cStart = (i == 0) ? startCol : 0;
        for (int c = cStart; c < colCount; c++) {
            QTableWidgetItem *item = currentTable->item(r, c);
            if (item && item->text().contains(searchText, Qt::CaseInsensitive)) {
                currentTable->setCurrentCell(r, c);
                currentTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                return;
            }
        }
    }

    // 如果没搜到，提示一下
    QMessageBox::information(this, "搜索", QString("未找到 \"%1\"").arg(searchText));
}

void MainWindow::onSimShowContextMenu(const QPoint &pos) {
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;
    QModelIndex index = table->indexAt(pos);
    if (!index.isValid()) return;
    int row = index.row();

    QMenu menu(this);
    
    // Format Submenu
    QMenu *formatMenu = menu.addMenu("Format");
    QStringList formats = {
        "Signed", "Unsigned", "Hex", "ASCII - Hex", "Binary",
        "32-bit Signed", "32-bit Unsigned", "32-bit Float", "64-bit Float"
    };
    for (const QString &fmt : formats) {
        QAction *a = formatMenu->addAction(fmt);
        connect(a, &QAction::triggered, this, [this, table, row, fmt](){ 
            simTableFormats[table][row] = fmt;
            refreshSimRowDisplay(table, row);
        });
    }

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
    }
}

// --- TCP Assistant Implementation ---

QWidget* MainWindow::createTcpAssistantPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // 1. Connection Settings
    QGroupBox *grpSettings = new QGroupBox("TCP 设置");
    QGridLayout *laySet = new QGridLayout();
    
    laySet->addWidget(new QLabel("运行方式:"), 0, 0); laySet->addWidget(cmbTcpMode, 0, 1);
    
    // Server Panel
    QWidget *pnlServer = new QWidget();
    QHBoxLayout *layPnlServer = new QHBoxLayout(pnlServer);
    layPnlServer->setContentsMargins(0, 0, 0, 0);
    layPnlServer->addWidget(lblTcpLocalIP);
    layPnlServer->addWidget(new QLabel("端口:"));
    layPnlServer->addWidget(txtTcpLocalPort);
    laySet->addWidget(pnlServer, 1, 0, 1, 6);
    
    // Client Panel
    QWidget *pnlClient = new QWidget();
    QHBoxLayout *layPnlClient = new QHBoxLayout(pnlClient);
    layPnlClient->setContentsMargins(0, 0, 0, 0);
    layPnlClient->addWidget(new QLabel("远端IP:"));
    layPnlClient->addWidget(txtTcpRemoteIP);
    layPnlClient->addWidget(new QLabel("端口:"));
    layPnlClient->addWidget(txtTcpRemotePort);
    laySet->addWidget(pnlClient, 2, 0, 1, 6);
    
    // Initial visibility
    pnlClient->hide();
    connect(cmbTcpMode, QOverload<int>::of(&QComboBox::currentIndexChanged), [pnlServer, pnlClient, this](int index){
        if (index == 0) { // Server
            pnlServer->show();
            pnlClient->hide();
            btnTcpConnect->setText("监听");
        } else { // Client
            pnlServer->hide();
            pnlClient->show();
            btnTcpConnect->setText("连接");
        }
    });

    QHBoxLayout *layActs = new QHBoxLayout();
    layActs->addWidget(btnTcpConnect);
    layActs->addWidget(btnTcpDisconnect);
    layActs->addWidget(lblTcpStatus);
    layActs->addStretch();
    laySet->addLayout(layActs, 3, 0, 1, 6);
    
    grpSettings->setLayout(laySet);
    layout->addWidget(grpSettings);
    
    // 2. Data Area
    QGroupBox *grpData = new QGroupBox("数据收发");
    QVBoxLayout *layData = new QVBoxLayout();
    
    QHBoxLayout *layOpts = new QHBoxLayout();
    layOpts->addWidget(chkTcpHexRecv);
    layOpts->addWidget(btnTcpClearRecv);
    layOpts->addStretch();
    layData->addLayout(layOpts);
    
    layData->addWidget(new QLabel("接收区:"));
    layData->addWidget(txtTcpRecv);
    
    layData->addWidget(new QLabel("发送区:"));
    layData->addWidget(txtTcpSend);
    
    QHBoxLayout *laySend = new QHBoxLayout();
    laySend->addWidget(chkTcpHexSend);
    laySend->addWidget(chkTcpCyclicSend);
    laySend->addWidget(new QLabel("间隔(ms):"));
    laySend->addWidget(spinTcpInterval);
    laySend->addStretch();
    laySend->addWidget(btnTcpSend);
    layData->addLayout(laySend);
    
    grpData->setLayout(layData);
    layout->addWidget(grpData);
    
    layout->setStretch(0, 0);
    layout->setStretch(1, 1);
    
    return page;
}

void MainWindow::onImportStandardFileClicked()
{
    QString fn = QFileDialog::getOpenFileName(this,
                                              "导入标准格式文件",
                                              QString(),
                                              "CSV Files (*.csv);;All Files (*)");
    if (fn.isEmpty()) return;

    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导入失败", "无法打开 CSV 文件。");
        return;
    }

    QTextStream in(&f);
    in.setCodec("UTF-8");

    auto parseCsvLine = [](const QString &line) {
        QStringList parts;
        bool inQuotes = false;
        QString field;
        for (int i = 0; i < line.length(); ++i) {
            QChar c = line[i];
            if (c == '"') {
                if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == ',' && !inQuotes) {
                parts.append(field.trimmed());
                field.clear();
            } else {
                field += c;
            }
        }
        parts.append(field.trimmed());
        return parts;
    };

    int nameCol = -1;
    int addrCol = -1;
    int typeCol = -1;
    int cmtCol = -1;
    int deviceCol = -1;
    int tabCol = -1;
    bool foundHeader = false;

    QVector<QJsonObject> rows;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        QStringList cols = parseCsvLine(line);

        if (!foundHeader) {
            QStringList lowerCols;
            for (const QString &c : cols) lowerCols << c.trimmed().toLower();
            nameCol = lowerCols.indexOf("name");
            addrCol = lowerCols.indexOf("address");
            typeCol = lowerCols.indexOf("datatype");
            cmtCol = lowerCols.indexOf("comment");
            deviceCol = lowerCols.indexOf("device");
            tabCol = lowerCols.indexOf("tab");
            if (tabCol < 0) tabCol = lowerCols.indexOf("sheet");
            if (tabCol < 0) tabCol = lowerCols.indexOf("worksheet");
            if (nameCol >= 0 && addrCol >= 0 && typeCol >= 0 && cmtCol >= 0) {
                foundHeader = true;
            }
            continue;
        }

        QJsonObject item;
        item.insert("Name", nameCol < cols.size() ? cols[nameCol] : "");
        item.insert("Address", addrCol < cols.size() ? cols[addrCol] : "");
        item.insert("DataType", typeCol < cols.size() ? cols[typeCol] : "");
        item.insert("Comment", cmtCol < cols.size() ? cols[cmtCol] : "");
        item.insert("Device", deviceCol >= 0 && deviceCol < cols.size() ? cols[deviceCol] : "");
        item.insert("Tab", tabCol >= 0 && tabCol < cols.size() ? cols[tabCol] : "");
        if (!item.value("Address").toString().trimmed().isEmpty()) {
            rows.push_back(item);
        }
    }
    f.close();

    if (!foundHeader) {
        QMessageBox::warning(this, "导入失败", "CSV 中未找到 Name/Address/DataType/Comment 表头。");
        return;
    }
    if (rows.isEmpty()) {
        QMessageBox::information(this, "导入结果", "未读取到可导入的数据行。");
        return;
    }

    struct MergedRow {
        QString address;
        QString dataType;
        QString comment;
        QString registerFormat;
    };

    auto buildAddress = [](const QString &addrRaw, QString &finalAddr, QString &bitCommentPart) -> bool {
        QString raw = addrRaw.trimmed();
        QRegularExpression reMw("%MW\\s*([0-9]+)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression reMb("%MB\\s*([0-9]+)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression reMx("%MX\\s*([0-9]+)\\.([0-9]+)", QRegularExpression::CaseInsensitiveOption);

        auto mMx = reMx.match(raw);
        if (mMx.hasMatch()) {
            int before = mMx.captured(1).toInt();
            int after = mMx.captured(2).toInt();
            int addr = before / 2;
            if (before % 2 != 0) after += 8;
            finalAddr = QString::number(addr);
            bitCommentPart = QString::number(after);
            return true;
        }

        auto mMw = reMw.match(raw);
        if (mMw.hasMatch()) {
            finalAddr = mMw.captured(1);
            bitCommentPart.clear();
            return true;
        }

        auto mMb = reMb.match(raw);
        if (mMb.hasMatch()) {
            int mb = mMb.captured(1).toInt();
            finalAddr = QString::number(mb / 2);
            bitCommentPart.clear();
            return true;
        }

        // 兜底：没有匹配到 %M 前缀时，直接保留原始地址文本。
        finalAddr = raw;
        bitCommentPart.clear();
        return !finalAddr.isEmpty();
    };

    QMap<QString, QVector<QJsonObject>> groupedRows;
    QStringList groupOrder;
    for (const QJsonObject &o : rows) {
        QString key = o.value("Device").toString().trimmed();
        if (key.isEmpty()) key = "未分类";
        if (!groupedRows.contains(key)) groupOrder << key;
        groupedRows[key].push_back(o);
    }

    auto applyGroupToTable = [&](const QVector<QJsonObject> &groupData, QTableWidget *targetTable) -> int {
        if (!targetTable) return 0;

        QMap<QString, int> rowIndexByAddr;
        QVector<MergedRow> mergedRows;

        for (const QJsonObject &o : groupData) {
            QString name = o.value("Name").toString().trimmed();
            QString addressRaw = o.value("Address").toString().trimmed();
            QString dataType = o.value("DataType").toString().trimmed();
            QString comment = o.value("Comment").toString().trimmed();

            if (addressRaw.isEmpty()) continue;

            QString finalAddr;
            QString bitPart;
            if (!buildAddress(addressRaw, finalAddr, bitPart)) continue;

            QString note = QString("%1 %2").arg(name).arg(comment).simplified();
            if (!bitPart.isEmpty()) {
                note = QString("%1 %2").arg(bitPart).arg(note).simplified();
            }

            if (!rowIndexByAddr.contains(finalAddr)) {
                MergedRow mr;
                mr.address = finalAddr;
                mr.dataType = dataType;
                mr.comment = note;
                mr.registerFormat = dataType;
                rowIndexByAddr.insert(finalAddr, mergedRows.size());
                mergedRows.push_back(mr);
            } else {
                int idx = rowIndexByAddr.value(finalAddr);
                if (idx >= 0 && idx < mergedRows.size()) {
                    if (mergedRows[idx].dataType.isEmpty()) {
                        mergedRows[idx].dataType = dataType;
                        mergedRows[idx].registerFormat = dataType;
                    }
                    mergedRows[idx].comment = QString("%1 %2").arg(mergedRows[idx].comment).arg(note).simplified();
                }
            }
        }

        targetTable->blockSignals(true);
        targetTable->setRowCount(mergedRows.size());
        for (int i = 0; i < mergedRows.size(); ++i) {
            if (!targetTable->item(i, 0)) targetTable->setItem(i, 0, new QTableWidgetItem());
            if (!targetTable->item(i, 1)) targetTable->setItem(i, 1, new QTableWidgetItem());
            if (!targetTable->item(i, 2)) targetTable->setItem(i, 2, new QTableWidgetItem());
            targetTable->item(i, 0)->setText(mergedRows[i].address);
            targetTable->item(i, 1)->setText(mergedRows[i].comment);
            targetTable->item(i, 2)->setText(mergedRows[i].registerFormat);
        }
        targetTable->blockSignals(false);

        ensureRegisterMapEditableTailRow(targetTable);
        return mergedRows.size();
    };

    if (!tabRegisterMaps) {
        QMessageBox::warning(this, "导入失败", "未找到地址映射页签控件。");
        return;
    }

    while (tabRegisterMaps->count() > 2) {
        QWidget *w = tabRegisterMaps->widget(tabRegisterMaps->count() - 1);
        tabRegisterMaps->removeTab(tabRegisterMaps->count() - 1);
        if (w) w->deleteLater();
    }

    int totalImported = 0;
    for (int i = 0; i < groupOrder.size(); ++i) {
        QString groupName = groupOrder[i];
        QTableWidget *targetTable = nullptr;

        if (i == 0) {
            targetTable = tblAGV;
            tabRegisterMaps->setTabText(0, groupName);
        } else if (i == 1) {
            targetTable = tblRobot;
            tabRegisterMaps->setTabText(1, groupName);
        } else {
            targetTable = new QTableWidget();
            setupRegisterTable(targetTable);
            connect(targetTable, &QTableWidget::cellClicked, this, &MainWindow::onRegisterTableCellClicked);
            connect(targetTable, &QTableWidget::cellChanged, this, &MainWindow::onRegisterTableChanged);
            tabRegisterMaps->addTab(targetTable, groupName);
        }

        totalImported += applyGroupToTable(groupedRows.value(groupName), targetTable);
    }

    syncSimulatorTablesFromMaps();

    QMessageBox::information(this,
                             "导入完成",
                             QString("标准格式导入成功：共导入 %1 个类型分组，%2 条记录（按地址合并后）。")
                                 .arg(groupOrder.size())
                                 .arg(totalImported));
}

void MainWindow::onTcpModeChanged(int index)
{
    onTcpDisconnectClicked(); // Ensure closed before switching mode
}

void MainWindow::onTcpConnectClicked()
{
    if (cmbTcpMode->currentIndex() == 0) { // Server Mode
        quint16 port = txtTcpLocalPort->text().toUShort();
        if (tcpServer->listen(QHostAddress::Any, port)) {
            lblTcpStatus->setText("正在监听...");
            lblTcpStatus->setStyleSheet("color: green; font-weight: bold;");
            btnTcpConnect->setEnabled(false);
            btnTcpDisconnect->setEnabled(true);
            cmbTcpMode->setEnabled(false);
        } else {
            QMessageBox::critical(this, "错误", "监听失败: " + tcpServer->errorString());
        }
    } else { // Client Mode
        QString ip = txtTcpRemoteIP->text();
        quint16 port = txtTcpRemotePort->text().toUShort();
        if (!tcpAssistantSocket) {
            tcpAssistantSocket = new QTcpSocket(this);
            connect(tcpAssistantSocket, &QTcpSocket::connected, this, &MainWindow::onTcpClientConnected);
            connect(tcpAssistantSocket, &QTcpSocket::disconnected, this, &MainWindow::onTcpClientDisconnected);
            connect(tcpAssistantSocket, &QTcpSocket::readyRead, this, &MainWindow::onTcpReadyRead);
        }
        tcpAssistantSocket->connectToHost(ip, port);
        lblTcpStatus->setText("正在连接...");
        lblTcpStatus->setStyleSheet("color: orange; font-weight: bold;");
        btnTcpConnect->setEnabled(false);
        btnTcpDisconnect->setEnabled(true);
        cmbTcpMode->setEnabled(false);
    }
}

void MainWindow::onTcpDisconnectClicked()
{
    if (tcpServer->isListening()) {
        tcpServer->close();
    }
    if (tcpAssistantSocket) {
        tcpAssistantSocket->disconnectFromHost();
    }
    
    tcpCyclicTimer->stop();
    chkTcpCyclicSend->setChecked(false);
    btnTcpSend->setText("发送");
    
    lblTcpStatus->setText("未运行");
    lblTcpStatus->setStyleSheet("color: red; font-weight: bold;");
    btnTcpConnect->setEnabled(true);
    btnTcpDisconnect->setEnabled(false);
    cmbTcpMode->setEnabled(true);
}

void MainWindow::onTcpServerNewConnection()
{
    if (tcpAssistantSocket) {
        tcpAssistantSocket->disconnectFromHost();
        tcpAssistantSocket->deleteLater();
    }
    tcpAssistantSocket = tcpServer->nextPendingConnection();
    connect(tcpAssistantSocket, &QTcpSocket::disconnected, this, &MainWindow::onTcpClientDisconnected);
    connect(tcpAssistantSocket, &QTcpSocket::readyRead, this, &MainWindow::onTcpReadyRead);
    
    lblTcpStatus->setText(QString("已连接: %1").arg(tcpAssistantSocket->peerAddress().toString()));
    lblTcpStatus->setStyleSheet("color: green; font-weight: bold;");
}

void MainWindow::onTcpClientConnected()
{
    lblTcpStatus->setText("已连接到服务器");
    lblTcpStatus->setStyleSheet("color: green; font-weight: bold;");
}

void MainWindow::onTcpClientDisconnected()
{
    if (cmbTcpMode->currentIndex() == 0) { // Server Mode
        lblTcpStatus->setText("正在监听 (等待连接)");
        lblTcpStatus->setStyleSheet("color: blue; font-weight: bold;");
    } else {
        lblTcpStatus->setText("连接断开");
        lblTcpStatus->setStyleSheet("color: red; font-weight: bold;");
        btnTcpConnect->setEnabled(true);
        btnTcpDisconnect->setEnabled(false);
        cmbTcpMode->setEnabled(true);
    }
    
    tcpCyclicTimer->stop();
    chkTcpCyclicSend->setChecked(false);
    btnTcpSend->setText("发送");
}

void MainWindow::onTcpReadyRead()
{
    if (!tcpAssistantSocket) return;
    QByteArray data = tcpAssistantSocket->readAll();
    if (data.isEmpty()) return;

    QString display;
    if (chkTcpHexRecv->isChecked()) {
        display = data.toHex(' ').toUpper();
    } else {
        display = QString::fromLocal8Bit(data);
    }
    
    txtTcpRecv->append(QString("[%1] Recv: %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")).arg(display));
}

QWidget* MainWindow::createPerformancePage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);

    QLabel *header = new QLabel("电脑性能实时监控器");
    header->setStyleSheet("font-size: 18px; font-weight: bold; color: #333; margin-bottom: 10px;");
    layout->addWidget(header);

    layout->addWidget(btnTogglePerfMonitor);

    QGroupBox *chartGroup = new QGroupBox("监控图表");
    QGridLayout *grid = new QGridLayout(chartGroup);
    
    grid->addWidget(lblLocalCpu, 0, 0);
    grid->addWidget(chartLocalCpu, 1, 0);
    
    grid->addWidget(lblLocalMem, 0, 1);
    grid->addWidget(chartLocalMem, 1, 1);
    
    layout->addWidget(chartGroup);

    layout->addWidget(new QLabel("异常日志 (当系统负载过高导致卡顿时，将记录可能的原因):"));
    layout->addWidget(txtPerfLog);

    return page;
}

void MainWindow::onPerformanceMonitorToggled(bool checked)
{
    if (checked) {
        btnTogglePerfMonitor->setText("停止监控");
        btnTogglePerfMonitor->setStyleSheet("font-weight: bold; min-height: 40px; background-color: #ffcccc;");
        txtPerfLog->append(QString("[%1] 性能监控已开始...").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
        hasLastPerfSample = false;
        perfTimer->start();
    } else {
        btnTogglePerfMonitor->setText("开始监控本机性能");
        btnTogglePerfMonitor->setStyleSheet("font-weight: bold; min-height: 40px;");
        txtPerfLog->append(QString("[%1] 性能监控已停止").arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
        perfTimer->stop();
        lblLocalCpu->setText("CPU 使用率: 0%");
        lblLocalMem->setText("内存 使用率: 0%");
        chartLocalCpu->clear();
        chartLocalMem->clear();
    }
}

void MainWindow::onPerformanceTimer()
{
    // --- CPU Usage Calculation (Linux /proc/stat) ---
    QFile statFile("/proc/stat");
    if (statFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statFile);
        QString line = stream.readLine();
        if (line.startsWith("cpu ")) {
            QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                quint64 user = parts[1].toULongLong();
                quint64 nice = parts[2].toULongLong();
                quint64 system = parts[3].toULongLong();
                quint64 idle = parts[4].toULongLong();

                if (hasLastPerfSample) {
                    quint64 totalDiff = (user + nice + system + idle) - (lastTotalUser + lastTotalUserLow + lastTotalSys + lastTotalIdle);
                    quint64 idleDiff = idle - lastTotalIdle;
                    if (totalDiff > 0) {
                        double usage = 100.0 * (1.0 - (double)idleDiff / totalDiff);
                        lblLocalCpu->setText(QString("CPU 使用率: %1%").arg(usage, 0, 'f', 1));
                        chartLocalCpu->addValue(usage);

                        // Detection of "Lag" (High CPU)
                        if (usage > 90.0) {
                            txtPerfLog->append(QString("<font color='red'>[%1] 警告: CPU 负载过高 (%2%)，可能导致系统卡顿。</font>")
                                .arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(usage, 0, 'f', 1));
                        }
                    }
                }
                lastTotalUser = user;
                lastTotalUserLow = nice;
                lastTotalSys = system;
                lastTotalIdle = idle;
                hasLastPerfSample = true;
            }
        }
        statFile.close();
    }

    // --- Memory Usage Calculation (Linux /proc/meminfo) ---
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&memFile);
        quint64 memTotal = 0, memAvailable = 0;
        int found = 0;
        while (!stream.atEnd() && found < 2) {
            QString line = stream.readLine();
            if (line.startsWith("MemTotal:")) {
                memTotal = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).at(1).toULongLong();
                found++;
            } else if (line.startsWith("MemAvailable:")) {
                memAvailable = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).at(1).toULongLong();
                found++;
            }
        }
        if (memTotal > 0) {
            double usage = 100.0 * (1.0 - (double)memAvailable / memTotal);
            lblLocalMem->setText(QString("内存 使用率: %1%").arg(usage, 0, 'f', 1));
            chartLocalMem->addValue(usage);

            if (usage > 95.0) {
                txtPerfLog->append(QString("<font color='red'>[%1] 严重警告: 内存几乎耗尽 (%2%)，极易导致系统卡死 (Swap Thrashing)。</font>")
                    .arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(usage, 0, 'f', 1));
            }
        }
        memFile.close();
    }

    // Try to find top CPU consumers if lagging
    // This is optional but helpful
    static int checkCounter = 0;
    if (++checkCounter % 5 == 0) { // check every 5 seconds or if lagging?
        // If we want to automatically find the culprit:
        // Run: ps -eo pid,pcpu,comm --sort=-pcpu | head -n 3
    }
}

void MainWindow::onTcpSendClicked()
{
    if (chkTcpCyclicSend->isChecked()) {
        if (tcpCyclicTimer->isActive()) {
            tcpCyclicTimer->stop();
            btnTcpSend->setText("发送");
        } else {
            if (!tcpAssistantSocket || tcpAssistantSocket->state() != QAbstractSocket::ConnectedState) {
                QMessageBox::warning(this, "警告", "未连接到远程主机");
                return;
            }
            tcpCyclicTimer->start(spinTcpInterval->value());
            btnTcpSend->setText("停止发送");
            onTcpCyclicTimerTick(); // Send immediately
        }
    } else {
        if (!tcpAssistantSocket || tcpAssistantSocket->state() != QAbstractSocket::ConnectedState) {
            QMessageBox::warning(this, "警告", "未连接到远程主机");
            return;
        }
        onTcpCyclicTimerTick();
    }
}

void MainWindow::onTcpCyclicTimerTick()
{
    if (!tcpAssistantSocket || tcpAssistantSocket->state() != QAbstractSocket::ConnectedState) {
        tcpCyclicTimer->stop();
        btnTcpSend->setText("发送");
        chkTcpCyclicSend->setChecked(false);
        return;
    }

    QString text = txtTcpSend->toPlainText();
    QByteArray data;
    if (chkTcpHexSend->isChecked()) {
        data = QByteArray::fromHex(text.toUtf8());
    } else {
        data = text.toUtf8();
    }

    if (!data.isEmpty()) {
        tcpAssistantSocket->write(data);
    }
}

void MainWindow::onTcpClearRecvClicked()
{
    txtTcpRecv->clear();
}

