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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    void onGitAddClicked();
    void onGitCommitClicked();
    void onGitPushClicked();
    void onGitPullClicked();
    void onGitMergeClicked();
    void onGitStatusClicked();
    void onGitOpenIgnoreClicked();
    void onGitCheckIgnoreClicked();
    void onGitRefreshLogClicked();
    void onGitResetClicked();
    void onGitSoftResetClicked();   // <--- 新增
    void onGitCopyForDailyReportClicked();
    void onScpTransferClicked();
    void onRebootTargetClicked();
    void onMonitorUsageToggled();
    void onMonitorTimer();
    void runDiagnosticCommands(int pid);

    // Common
    void onClearLogClicked();

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
    
    // Helpers to init pages
    QWidget* createModbusPage();
    QWidget* createSerialPage();
    QWidget* createGitPage();
    QWidget* createSimulatorPage();
    QWidget* createTcpAssistantPage();

    // --- Main UI Structure ---
    QWidget *centralWidget;
    QHBoxLayout *mainLayout; // Horizontal: Nav + Stack
    QListWidget *navWidget;
    QStackedWidget *stackedWidget;
    QWidget *modbusPageWidget;
    QWidget *serialPageWidget;
    QWidget *gitPageWidget;
    QWidget *simulatorPageWidget;
    QWidget *tcpAssistantPageWidget;

    // --- Modbus Widgets ---
    // Register Map Tables
    QTabWidget *tabRegisterMaps;
    QTableWidget *tblAGV;
    QTableWidget *tblRobot;
    void setupRegisterTable(QTableWidget *table);
    void onRegisterTableCellClicked(int row, int column);
    void onRegisterTabChanged(int index);   // Tab switch handler
    void onRegisterTableChanged(int row, int column); // Auto-save handler
    void saveRegisterTables();
    void loadRegisterTables();

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
    QComboBox *cmbGitBranches;
    QPushButton *btnGitRefreshBranches;
    QPushButton *btnGitCheckout;
    QLineEdit *txtGitCommitMsg;
    QPushButton *btnGitAdd;
    QPushButton *btnGitCommit;
    QPushButton *btnGitPush;
    QComboBox *cmbGitRemote; // Added for remote selection
    QPushButton *btnGitPull;
    QPushButton *btnGitMerge;
    QPushButton *btnGitStatus;
    QPushButton *btnGitOpenIgnore;
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
        FormatBinary = 2
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
    void runGitCommand(const QStringList &args); // Git helper
    void saveGitHistory(const QString &dir);
    void loadGitHistory();
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
};

#endif // MAINWINDOW_H
