#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
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
    void onGitRefreshLogClicked();
    void onGitResetClicked();

    // Common
    void onClearLogClicked();

private:
    void createWidgets();
    void createLayouts();
    void createConnections();
    
    // Helpers to init pages
    QWidget* createModbusPage();
    QWidget* createSerialPage();
    QWidget* createGitPage();

    // --- Main UI Structure ---
    QWidget *centralWidget;
    QHBoxLayout *mainLayout; // Horizontal: Nav + Stack
    QListWidget *navWidget;
    QStackedWidget *stackedWidget;
    QWidget *modbusPageWidget;
    QWidget *serialPageWidget;
    QWidget *gitPageWidget;

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
    QPushButton *btnGitPull;
    QPushButton *btnGitMerge;
    QPushButton *btnGitStatus;
    QComboBox *cmbGitHistory;
    QPushButton *btnGitRefreshLog;
    QPushButton *btnGitReset;
    QTextEdit *txtGitLog;

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

    // History
    static const int MAX_HISTORY = 10;
};

#endif // MAINWINDOW_H
