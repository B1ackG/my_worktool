#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <QScrollBar>
#include <QStackedWidget>
#include <QListWidget>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSettings>
#include <QProcess>
#include <QFileDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QJsonObject>
#include <QSet>
#include <QHash>
#include <QDirIterator>
#include <QPainter>
#include <QPolygonF>
#include "modbusslave.h"

// A simple sparkline widget to show recent usage
class MonitorChart : public QWidget {
    Q_OBJECT
public:
    explicit MonitorChart(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(100, 30);
    }
    void addValue(double val) {
        values.append(val);
        if (values.size() > 20) values.removeFirst();
        update();
    }
    void clear() { values.clear(); update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::black);
        if (values.isEmpty()) return;
        
        double maxVal = 100.0;
        QPolygonF poly;
        for (int i = 0; i < values.size(); ++i) {
            double x = i * (width() / 20.0);
            double y = height() - (values[i] / maxVal * height());
            poly << QPointF(x, y);
        }
        p.setPen(QPen(Qt::cyan, 2));
        p.drawPolyline(poly);
    }
private:
    QList<double> values;
};

struct GitWorkGoal {
    QString id;
    QString title;
    QString startDate;
    QString endDate;
    QString actualDate;
    QString parentId;
    QString branchName;
    bool started = false;
    QString remark;
    int difficulty = 0; // 子任务星级（1 星 = 1 次预计提交），根目标为 0
};

struct GitRootProgressInfo {
    double totalPercent = 0.0;
    double timePercent = 0.0;
    double taskPercent = 0.0;
    int descendantCount = 0;
    int completedCount = 0;
    double totalWeight = 0.0;
    double completedWeight = 0.0;
    bool hasSubGoals = false;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void onImportStandardFileClicked();

private slots:
    // Pages Navigation
    void onNavSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous);

    // Modbus Slots
    void onConnectClicked();
    void onDisconnectClicked();
    void onReadCoilsClicked();
    void onReadInputsClicked();
    void onReadHoldingRegistersClicked();
    void onReadInputRegistersClicked();
    void onWriteSingleCoilClicked();
    void onWriteSingleRegisterClicked();
    void onWriteMultipleCoilsClicked();
    void onWriteMultipleRegistersClicked();
    void onDisplayFormatChanged(int index);
    void onContinuousReadToggled(bool checked);
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onContinuousReadTimer();
    void saveConnectionHistory(const QString &ip);
    void loadConnectionHistory();

    // Add these helper methods
    void saveAutoScene();
    void loadAutoScene();

    // Serial Port Slots
    void onSerialOpenClicked();
    void onSerialCloseClicked();
    void onSerialSendClicked();
    void onSerialRead();
    void refreshSerialPorts();

    // Git Slots
    void onGitSelectDirClicked();
    void onGitRefreshBranchesClicked();
    void onGitCheckoutClicked();
    void onGitSyncRemoteClicked();   // 新增：同步远程分支到本地
    void onGitCreateBranchClicked(); // 新增：创建新分支
    void onGitDeleteBranchClicked(); // 新增：删除分支
    void onGitAddClicked();
    void onGitCommitClicked();
    void onGitPushClicked();
    void onGitPullClicked();
    void onGitMergeClicked();
    void onGitStatusClicked();
    void onGitDiffClicked();
    void onGitFetchClicked();
    void onGitStashClicked();
    void onGitStashPopClicked();
    void onGitSetDiffRuleClicked();
    void onGitAutoDiffReminderToggled(bool checked);
    void onGitAutoDiffReminderTick();
    void onGitOpenIgnoreClicked();
    void onGitGetSshKeyClicked(); // 新增：获取SSH公钥
    void onGitRemoteAddClicked(); // 新增：链接远程仓库
    void onGitCheckIgnoreClicked();
    void onGitWorktreeListClicked();
    void onGitWorktreeAddClicked();
    void onGitWorktreeRemoveClicked();
    void onGitWorktreePruneClicked();
    void onGitRefreshLogClicked();
    void onGitResetClicked();
    void onGitSoftResetClicked();   // <--- 新增
    void onGitCopyForDailyReportClicked();
    void onGitRemoveHistoryClicked();
    void onGitDirChanged();
    void onGitBranchSelectionChanged();
    void onGitGoalAddClicked();
    void onGitGoalEditClicked();
    void onGitGoalDeleteClicked();
    void onGitGoalStartClicked();
    void onScpTransferClicked();
    void onRebootTargetClicked();
    void onMonitorUsageToggled();
    void onMonitorTimer();
    void runDiagnosticCommands(int pid);

    // Performance Monitor Slots
    void onPerformanceMonitorToggled(bool checked);
    void onPerformanceTimer();

    // Common
    void onClearLogClicked();

    // Settings
    void onAutostartToggled(bool checked);

    // Simulator Slots
    void onStartSimulatorClicked();
    void onStopSimulatorClicked();
    void onSimSetRegisterClicked();
    void onSimSetBitClicked();
    void onSimWriteValuesClicked();
    void onSimRandomValuesClicked();
    void onApplyFaultSettingsClicked();
    void onExportHistoryClicked();
    void onRegisterOperation(quint16 addr, quint16 value, const QString &opType);
    void onSimShowContextMenu(const QPoint &pos);
    void onSimSetFormat(const QString &format);
    void onSimShowWaveformEditor(int row);

private:
    void createWidgets();
    void createLayouts();
    void createConnections();
    void createMenus();
    void syncAutostartActionState();
    bool isAutostartEnabled() const;
    bool setAutostartEnabled(bool enabled);
    QString autostartDesktopFilePath() const;
    
    // Helpers to init pages
    QWidget* createModbusPage();
    QWidget* createSerialPage();
    QWidget* createGitPage();
    QWidget* createSimulatorPage();
    QWidget* createTcpAssistantPage();
    QWidget* createPerformancePage();

    // --- Main UI Structure ---
    QAction *actAutostart = nullptr;
    QWidget *centralWidget;
    QHBoxLayout *mainLayout; // Horizontal: Nav + Stack
    QListWidget *navWidget;
    QStackedWidget *stackedWidget;
    QWidget *modbusPageWidget;
    QWidget *serialPageWidget;
    QWidget *gitPageWidget;
    QWidget *simulatorPageWidget;
    QWidget *tcpAssistantPageWidget;
    QWidget *performancePageWidget;

    // --- Modbus Widgets ---
    // Register Map Tables
    QTabWidget *tabRegisterMaps;
    QTableWidget *tblAGV;
    QTableWidget *tblRobot;
    QLineEdit *txtSearchMap;  // 新增：地址映射表搜索框
    QPushButton *btnSearchMap; // 新增：搜索按钮
    QPushButton *btnExportRegisterMap;
    QPushButton *btnImportRegisterMap;
    QPushButton *btnImportStandardFile;
    QCheckBox *chkAutoReadOnMapClick;
    QCheckBox *chkAutoWriteOnMapClick;
    void setupRegisterTable(QTableWidget *table);
    void onRegisterTableCellClicked(int row, int column);
    void onRegisterTabChanged(int index);   // Tab switch handler
    void onRegisterTableChanged(int row, int column); // Auto-save handler
    void onRegisterMapContextMenu(const QPoint &pos);
    void saveRegisterTables();
    void loadRegisterTables();
    void onExportRegisterMapClicked();
    void onImportRegisterMapClicked();
    void onSearchMapTextFinished(); // 新增：搜索回车处理
    void onSearchMapClicked();      // 新增：搜索按钮点击处理

    // Connection
    QLabel *lblIP;
    QComboBox *cmbIP; // Changed from QLineEdit to QComboBox
    QLabel *lblPort;
    QLineEdit *txtPort;
    QLabel *lblSlaveID;
    QLineEdit *txtSlaveID;
    QLabel *lblTimeout;
    QSpinBox *spinTimeout;
    QLabel *lblRetries;
    QSpinBox *spinRetries;
    QLabel *lblStatus;
    QLabel *lblStatusText;
    QPushButton *btnConnect;
    QPushButton *btnDisconnect;

    // Read
    QLabel *lblReadStartAddr;
    QSpinBox *spinReadStartAddr;
    QLabel *lblReadQuantity;
    QSpinBox *spinReadQuantity;
    QCheckBox *chkContinuousRead;
    QLabel *lblReadInterval;
    QSpinBox *spinReadInterval;
    QLabel *lblDisplayFormat;
    QComboBox *cmbDisplayFormat;
    QPushButton *btnReadCoils;
    QPushButton *btnReadInputs;
    QPushButton *btnReadHoldingRegisters;
    QPushButton *btnReadInputRegisters;

    // Write
    QLabel *lblWriteStartAddr;
    QSpinBox *spinWriteStartAddr;
    QLabel *lblWriteQuantity;
    QSpinBox *spinWriteQuantity;
    QLabel *lblWriteValue;
    QSpinBox *spinWriteValue;
    QCheckBox *chkWriteCoil;
    QLabel *lblWriteValues;
    QLineEdit *txtWriteValues;
    QLabel *lblWriteFormat;
    QComboBox *cmbWriteFormat;
    QPushButton *btnWriteSingleCoil;
    QPushButton *btnWriteSingleRegister;
    QPushButton *btnWriteMultipleCoils;
    QPushButton *btnWriteMultipleRegisters;

    // Modbus Output
    QTextEdit *txtResult;
    QTextEdit *txtLog;
    QPushButton *btnClearLog;

    // --- Serial Widgets ---
    QLabel *lblSerialPort;
    QComboBox *cmbSerialPort;
    QPushButton *btnRefreshPorts;
    QLabel *lblBaudRate;
    QComboBox *cmbBaudRate;
    QLabel *lblDataBits;
    QComboBox *cmbDataBits;
    QLabel *lblParity;
    QComboBox *cmbParity;
    QLabel *lblStopBits;
    QComboBox *cmbStopBits;
    
    QPushButton *btnSerialOpen;
    QPushButton *btnSerialClose;
    QLabel *lblSerialStatus;

    QCheckBox *chkHexSend;
    QCheckBox *chkHexDisplay;
    
    QTextEdit *txtSerialRecv;
    QTextEdit *txtSerialSend;
    QPushButton *btnSerialSend;
    QPushButton *btnSerialClearRecv;

    // --- Git Widgets ---
    QComboBox *cmbGitDir;  // Changed from QLineEdit
    QPushButton *btnGitSelectDir;
    QPushButton *btnGitRemoveHistory;
    QTableWidget *tblGitGoals;
    QPushButton *btnGitGoalAdd;
    QPushButton *btnGitGoalEdit;
    QPushButton *btnGitGoalDelete;
    QPushButton *btnGitGoalStart;
    QComboBox *cmbGitBranches;
    QPushButton *btnGitRefreshBranches;
    QPushButton *btnGitCheckout;
    QPushButton *btnGitSyncRemote;   // 同步远程分支
    QPushButton *btnGitCreateBranch; // 新增：创建分支按钮
    QPushButton *btnGitDeleteBranch; // 新增：删除分支按钮
    QLineEdit *txtGitCommitMsg;
    QPushButton *btnGitAdd;
    QPushButton *btnGitCommit;
    QPushButton *btnGitPush;
    QComboBox *cmbGitRemote; // Added for remote selection
    QPushButton *btnGitPull;
    QPushButton *btnGitMerge;
    QPushButton *btnGitStatus;
    QPushButton *btnGitDiff;
    QPushButton *btnGitFetch;
    QPushButton *btnGitStash;
    QPushButton *btnGitStashPop;
    QPushButton *btnGitSetDiffRule;
    QPushButton *btnGitAutoDiffReminder;
    QSpinBox *spinGitDiffIntervalMinutes;
    QSpinBox *spinGitDiffFileThreshold;
    QSpinBox *spinGitDiffLineThreshold;
    QPushButton *btnGitOpenIgnore;
    QPushButton *btnGitGetSshKey; // 新增
    QPushButton *btnGitRemoteAdd; // 新增
    QPushButton *btnGitWorktreeList;
    QPushButton *btnGitWorktreeAdd;
    QPushButton *btnGitWorktreeRemove;
    QPushButton *btnGitWorktreePrune;
    QPushButton *btnGitCheckIgnore;
    QComboBox *cmbGitHistory;
    QPushButton *btnGitRefreshLog;
    QPushButton *btnGitReset;
    QPushButton *btnGitSoftReset; // <--- 新增
    QPushButton *btnGitCopyDaily;
    QTextEdit *txtGitLog;
    QLineEdit *txtScpTargetIp;
    QLineEdit *txtScpPassword;
    QPushButton *btnScpTransfer;
    QPushButton *btnRebootTarget;
    QPushButton *btnMonitorUsage;
    QSpinBox *spinCpuThreshold;
    QPushButton *btnApplyThreshold;
    double cpuThresholdValue;
    int lastKnownPid = -1; 
    bool ulimitSet = false;
    void runCrashDiagnostics(); 
    MonitorChart *chartCpu;
    MonitorChart *chartMem;
    QTimer *monitorTimer;
    QLabel *lblCpuUsage;
    QLabel *lblMemUsage;
    QString currentMonitoringProcess;
    int currentMonitoringPid;
    quint64 prevProcJiffies;
    quint64 prevTotalJiffies;
    bool hasPrevCpuSample;
    QFile *monitorFile;
    QTextStream *monitorStream;
    QTimer *gitDiffReminderTimer;
    int gitDiffReminderRule = -1;
    QHash<QString, QSet<QString>> gitGoalCollapsedByRepo;

    // --- Logic Objects ---
    
    // Modbus
    QTcpSocket *tcpSocket;
    QTimer *continuousTimer;
    quint16 transactionId;
    
    struct ReadParams {
        quint8 functionCode;
        quint16 startAddress;
        quint16 quantity;
    } currentReadParams;

    enum DisplayFormat {
        FormatDecimal = 0,
        FormatHex = 1,
        FormatBinary = 2,
        FormatFloat = 3,
        FormatDouble = 4
    };
    DisplayFormat displayFormat;

    // --- Simulator Members ---
    ModbusSlave *simMainDevice; // port 5020
    ModbusSlave *simAGVDevice;  // port 5021

    // Simulator UI
    QPushButton *btnSimStartMain;
    QPushButton *btnSimStopMain;
    QPushButton *btnSimStartAGV;
    QPushButton *btnSimStopAGV;
    QLabel *lblSimMainStatus;
    QLabel *lblSimAGVStatus;
    // Bind address/ports editable
    QLineEdit *txtSimBindIP;    // e.g. 0.0.0.0 or 127.0.0.1
    QLineEdit *txtSimMainPort;  // main device port
    QLineEdit *txtSimAGVPort;   // agv device port
    QLineEdit *simAddrEdit;
    QLineEdit *simValueEdit;
    QPushButton *btnSimSetReg;
    QComboBox *simDeviceSelect;
    QSpinBox *simBitIndex;
    QCheckBox *simBitValue;
    QPushButton *btnSimSetBit;
    QTabWidget *tabSimRegisterMaps;
    QTableWidget *tblSimMain;
    QTableWidget *tblSimAGV;
    QPushButton *btnSimWriteValues;
    QPushButton *btnSimRandomValues;
    QPushButton *btnSimRandomAndWrite;
    QPushButton *btnSimAddCyclicTimer; // 添加此行
    QPushButton *btnSimSaveScene;
    QPushButton *btnSimLoadScene;
    QTextEdit *txtSimScript;
    QPushButton *btnSimRunScript;
    QPushButton *btnSimStopScript;
    QTextEdit *txtSimLog;
    QPushButton *btnExportHistory;
    QTabWidget *tabSimTools; // 新增：模拟器子功能Tab

    // register history: simple in-memory list of JSON objects
    QVector<QJsonObject> registerHistory;
    QMap<QString, QMap<quint16, quint16>> simLastReadValues;
    // Fault injection controls
    QSpinBox *spinSimDelayMs;
    QDoubleSpinBox *spinSimDropProb;
    QLineEdit *txtInjectFunc;     // function code to inject
    QLineEdit *txtInjectFuncCode; // exception code for function
    QLineEdit *txtInjectAddr;     // address to inject
    QLineEdit *txtInjectAddrCode; // exception code for address

    // Serial
    QSerialPort *serialPort;

    // Helpers
    void sendModbusRequest(quint8 functionCode, quint16 startAddress, quint16 quantity,
                           const QVector<quint16> &values = QVector<quint16>());
    void parseModbusResponse(const QByteArray &response);
    QString formatValue(quint16 value, bool isBit = false) const;
    void logMessage(const QString &message, bool isError = false); // Logs to Modbus log
    void logSerialMessage(const QString &message); // Logs to Serial log or status
    void updateConnectionStatus(bool connected);
    void updateSerialStatus(bool connected);
    bool runGitCommand(const QStringList &args); // Git helper, returns true on exit 0
    void saveGitHistory(const QString &dir);
    void loadGitHistory();
    void removeGitHistoryPath(const QString &dir);
    void applyGitHistoryToCombo(const QStringList &history, const QString &selectPath = QString());
    QString gitGoalsRepoKey(const QString &repoDir) const;
    QList<GitWorkGoal> loadGitGoals(const QString &repoDir) const;
    void saveGitGoals(const QString &repoDir, const QList<GitWorkGoal> &goals);
    void refreshGitGoalsTable();
    bool editGitWorkGoalDialog(GitWorkGoal &goal, const QList<GitWorkGoal> &allGoals, const QString &excludeGoalId = QString());
    QString gitGoalTitleById(const QList<GitWorkGoal> &goals, const QString &id) const;
    GitWorkGoal *gitGoalById(QList<GitWorkGoal> &goals, const QString &id);
    const GitWorkGoal *gitGoalById(const QList<GitWorkGoal> &goals, const QString &id) const;
    QString translateGoalTitleToEnglish(const QString &title);
    QString chineseTitleToPinyinSlug(const QString &title);
    QString slugifyBranchName(const QString &text) const;
    bool splitGoalBranchCategory(const QString &fullBranch, QString &category, QString &namePart) const;
    QString buildGoalBranchName(const QString &category, const QString &namePart) const;
    QString suggestBranchNameFromTitle(const QString &title);
    bool gitBranchExists(const QString &repoDir, const QString &branchName) const;
    bool checkoutGitBranch(const QString &repoDir, const QString &branchName);
    bool createGitBranch(const QString &repoDir, const QString &branchName);
    QString detectDefaultMainBranch(const QString &repoDir,
                                    const QStringList &branchHints = QStringList()) const;
    QString resolveGitMainBranch(const QString &repoDir) const;
    void syncGitMainBranchSetting(const QString &repoDir);
    void activateGitRepo(const QString &repoDir);
    QStringList gitBranchHintLinesFromCombo() const;
    bool branchNameInBranchHints(const QStringList &hints, const QString &name) const;
    void saveGitMainBranchSetting(const QString &repoDir, const QString &branchName);
    QString gitWorktreePathUsingBranch(const QString &repoDir, const QString &branchName) const;
    void fillAncestorStartDates(QList<GitWorkGoal> &goals, const QString &goalId, const QString &dateStr);
    bool promptGitGoalStartDialog(const QString &goalTitle, QString &branchName, bool &createBranch);
    QString normalizeLocalBranchRef(const QString &branchRef) const;
    int markGoalsCompletedForDeletedBranches(const QString &repoDir);
    void syncChildGoalEndDatesFromParents(QList<GitWorkGoal> &goals);
    bool syncParentStartDatesFromLeaves(QList<GitWorkGoal> &goals);
    void appendGitGoalTableRow(const QString &repoDir, const GitWorkGoal &g,
                               const QList<GitWorkGoal> &allGoals, int depth,
                               bool hasChildren, bool childrenCollapsed);
    bool isGitGoalHiddenByCollapse(const GitWorkGoal &goal, const QList<GitWorkGoal> &allGoals,
                                   const QSet<QString> &collapsedIds) const;
    QSet<QString> &gitGoalCollapsedIdsForRepo(const QString &repoDir);
    QString formatDifficultyStars(int starCount) const;
    QPixmap gitDifficultyStarPixmap(int starCount, int starSize = 14) const;
    QDate gitGoalEffectiveStartDate(const GitWorkGoal &goal, const QList<GitWorkGoal> &goals) const;
    int gitBranchCommitCountSince(const QString &repoDir, const QString &branchRef,
                                  const QDate &sinceDate) const;
    int gitGoalActualCommits(const QString &repoDir, const GitWorkGoal &goal,
                             const QList<GitWorkGoal> &goals) const;
    bool syncGoalDifficultyFromCommits(const QString &repoDir, QList<GitWorkGoal> &goals);
    void collectGitGoalDescendantIds(const QString &parentId, const QList<GitWorkGoal> &goals,
                                     QStringList &outIds) const;
    void collectGitGoalLeafDescendantIds(const QString &parentId, const QList<GitWorkGoal> &goals,
                                         QStringList &outIds) const;
    bool gitGoalHasChildren(const QString &goalId, const QList<GitWorkGoal> &goals) const;
    int gitGoalDisplayDifficulty(const GitWorkGoal &goal, const QList<GitWorkGoal> &goals) const;
    bool isGitSubGoalCompleted(const GitWorkGoal &goal) const;
    GitRootProgressInfo calcRootGoalProgress(const QString &repoDir, const GitWorkGoal &root,
                                             const QList<GitWorkGoal> &goals) const;
    bool isRegisterMapRowEmpty(const QTableWidget *table, int row) const;
    void ensureRegisterMapEditableTailRow(QTableWidget *table);
    void copyRegisterMapSelection(QTableWidget *table);
    void pasteRegisterMapFromClipboard(QTableWidget *table, int startRow, int startColumn);
    void setupSimulatorRegisterTable(QTableWidget *table);
    void syncSimulatorTablesFromMaps();
    void onSimRandomAndWriteClicked();
    void onSimSaveSceneClicked();
    void onSimLoadSceneClicked();
    void onSimExportCsvClicked();
    void onSimImportCsvClicked();
    void onSimRunScriptClicked();
    void onSimStopScriptClicked();
    void onSimRandomAndWriteClicked_v2(); // unused but keeping to maintain structure if needed
    void onSimTableRowChanged(int row, int column);
    void onSimPopulateFloats();
    void onSimShowBitEditor(int row);
    void onSimAddCyclicTimerClicked();
    void onSimRemoveCyclicTimerClicked(); // 新增
    void onSimWaveTypeChanged(const QString &type); // 新增
    void onSimStopAllWaveformsClicked(); // 新增
    void onSimTimerTick();
    void onSimGenerateReportClicked();
    void refreshSimRowDisplay(QTableWidget *table, int row);
    void setSimRowEnabled(QTableWidget *table, int row, bool enabled);

    // --- TCP Assistant Slots ---
    void onTcpConnectClicked();
    void onTcpDisconnectClicked();
    void onTcpSendClicked();
    void onTcpModeChanged(int index);
    void onTcpServerNewConnection();
    void onTcpClientConnected();
    void onTcpClientDisconnected();
    void onTcpReadyRead();
    void onTcpCyclicTimerTick();
    void onTcpClearRecvClicked();

    struct CyclicTimer {
        QString device; // "Main" or "AGV"
        quint16 addr;
        QString type; // "Sine", "Square", "Triangle", "Random", "Sawtooth"
        double amplitude;
        double offset;
        double period; // in seconds
        double phase; // in degrees
        double dutyCycle; // for square wave (0-1)
        int currentTicks;
        bool active;
    };
    QList<CyclicTimer> simCyclicTimers;
    QTimer *simTickTimer;

    // UI Widgets for Waveform Configuration
    QComboBox *cmbWaveDevice;
    QSpinBox *spinWaveAddr;
    QComboBox *cmbWaveType;
    QDoubleSpinBox *spinWaveAmp;
    QDoubleSpinBox *spinWaveOffset;
    QDoubleSpinBox *spinWavePeriod;
    QDoubleSpinBox *spinWavePhase;
    QDoubleSpinBox *spinWaveDuty;
    QTableWidget *tblWaveChannels;
    QPushButton *btnWaveAdd;
    QPushButton *btnWaveStopAll;

    QList<QTimer*> scriptTimers;
    QMap<quint16, QTimer*> cyclicTimers;

    // 模拟器表格显示的格式缓存：Key=tablePtr, Value=Map<row, formatString>
    QMap<QTableWidget*, QMap<int, QString>> simTableFormats;
    // 被 32-bit 显示占用并禁用的行：Key=tablePtr, Value=Map<lockedRow, ownerRow>
    QMap<QTableWidget*, QMap<int, int>> simDisabledRowsOwner;

    // TCP Assistant Widgets
    QComboBox *cmbTcpMode;
    QLabel *lblTcpLocalIP;
    QLineEdit *txtTcpLocalPort;
    QLineEdit *txtTcpRemoteIP;
    QLineEdit *txtTcpRemotePort;
    QPushButton *btnTcpConnect;
    QPushButton *btnTcpDisconnect;
    QLabel *lblTcpStatus;
    QTextEdit *txtTcpRecv;
    QTextEdit *txtTcpSend;
    QCheckBox *chkTcpHexRecv;
    QCheckBox *chkTcpHexSend;
    QCheckBox *chkTcpCyclicSend;
    QSpinBox *spinTcpInterval;
    QPushButton *btnTcpSend;
    QPushButton *btnTcpClearRecv;
    
    QTcpServer *tcpServer;
    QTcpSocket *tcpAssistantSocket; // For Client or the connected client for Server
    QTimer *tcpCyclicTimer;

    // History
    static const int MAX_HISTORY = 10;
    QString lastScenePath; // 添加此行

    // --- Performance Monitor Widgets ---
    QPushButton *btnTogglePerfMonitor;
    MonitorChart *chartLocalCpu;
    MonitorChart *chartLocalMem;
    QLabel *lblLocalCpu;
    QLabel *lblLocalMem;
    QTextEdit *txtPerfLog;
    QTimer *perfTimer;
    quint64 lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;
    bool hasLastPerfSample;
};

#endif // MAINWINDOW_H
