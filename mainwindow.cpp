#include "mainwindow.h"
#include "cursorskillsdialog.h"
#include "deepseekclient.h"
#include "gitstageguard.h"
#include "gitstagereviewdialog.h"
#include "gitworktreedialog.h"
#include "gitworktreerunner.h"
#include "lifeassistantwidget.h"
#include "inputquickerwidget.h"
#include "platformprefs.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QMessageBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QFrame>
#include <QInputDialog>
#include <QDateTime>
#include <QDate>
#include <QTime>
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
#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QProgressBar>
#include <QStatusBar>
#include <QStandardPaths>
#include <QMenuBar>
#include <QActionGroup>
#include <QKeySequence>
#include <QCoreApplication>
#include <QSet>
#include <QScrollArea>
#include <QSplitter>
#include <QSignalBlocker>

namespace {

constexpr int kGitGoalLinesPerStar = 5000;
constexpr int kGitGoalMaxAutoStars = 10;

bool finishGitProcess(QProcess &process, int timeoutMs = -1)
{
    const bool finished = timeoutMs < 0
                              ? process.waitForFinished(-1)
                              : process.waitForFinished(timeoutMs);
    if (!finished && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    return finished;
}

int gitLinesToStarCount(int lines)
{
    if (lines <= 0) {
        return 0;
    }
    return (lines + kGitGoalLinesPerStar - 1) / kGitGoalLinesPerStar;
}

double gitLinesToStarWeight(int lines)
{
    return static_cast<double>(lines) / static_cast<double>(kGitGoalLinesPerStar);
}

static const int kDefaultStringRegisterCount = 15;

int parseStringRegisterCount(const QString &regFmt, int defaultCount = kDefaultStringRegisterCount)
{
    const QRegularExpression re(QStringLiteral(R"(STRING\s*\[\s*(\d+)\s*\])"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(regFmt.trimmed());
    if (match.hasMatch()) {
        const int count = match.captured(1).toInt();
        return count > 0 ? count : defaultCount;
    }
    if (regFmt.trimmed().contains(QStringLiteral("STRING"), Qt::CaseInsensitive)
        || regFmt.trimmed().contains(QStringLiteral("WSTRING"), Qt::CaseInsensitive)) {
        return defaultCount;
    }
    return -1;
}

QString decodeUtf8FromRegisters(const QVector<quint16> &regs)
{
    QByteArray bytes;
    bytes.reserve(regs.size() * 2);
    for (quint16 w : regs) {
        bytes.append(static_cast<char>((w >> 8) & 0xFF));
        bytes.append(static_cast<char>(w & 0xFF));
    }
    while (!bytes.isEmpty() && bytes.back() == '\0') {
        bytes.chop(1);
    }
    return QString::fromUtf8(bytes);
}

QVector<quint16> encodeUtf8ToRegisters(const QString &str, int regCount)
{
    QVector<quint16> regs;
    if (regCount <= 0) {
        return regs;
    }

    QByteArray bytes = str.toUtf8();
    const int targetSize = regCount * 2;
    if (bytes.size() < targetSize) {
        bytes.append(QByteArray(targetSize - bytes.size(), '\0'));
    } else if (bytes.size() > targetSize) {
        bytes.truncate(targetSize);
    }
    regs.reserve(regCount);
    for (int i = 0; i < regCount; ++i) {
        const quint8 hi = (i * 2 < bytes.size()) ? static_cast<quint8>(bytes[i * 2]) : 0;
        const quint8 lo = (i * 2 + 1 < bytes.size()) ? static_cast<quint8>(bytes[i * 2 + 1]) : 0;
        regs << static_cast<quint16>((static_cast<quint16>(hi) << 8) | lo);
    }
    return regs;
}

int simFormatWordCount(const QString &fmt, int stringRegCount = kDefaultStringRegisterCount)
{
    if (fmt == QStringLiteral("String")) {
        return qMax(1, stringRegCount);
    }
    if (fmt.startsWith(QStringLiteral("64-bit"))) {
        return 4;
    }
    if (fmt.startsWith(QStringLiteral("32-bit"))) {
        return 2;
    }
    return 1;
}

QStringList parseBitDescriptionsFromComment(const QString &comment)
{
    QStringList bits;
    for (int i = 0; i < 16; ++i) {
        bits.append(QString());
    }

    const QString text = comment.simplified();
    if (text.isEmpty()) {
        return bits;
    }

    // 标准导入格式：各位说明以 "0 名称 ... 1 名称 ..." 形式合并在同一描述中
    const QRegularExpression re(
        QStringLiteral("(?:^|\\s)(\\d{1,2})\\s+(.*?)(?=\\s\\d{1,2}\\s|$)"));
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int bit = m.captured(1).toInt();
        const QString desc = m.captured(2).trimmed();
        if (bit >= 0 && bit <= 15 && !desc.isEmpty()) {
            bits[bit] = desc;
        }
    }

    return bits;
}

quint16 swapBytes16(quint16 w)
{
    return static_cast<quint16>(((w & 0xFF) << 8) | ((w >> 8) & 0xFF));
}

QString float32WordOrderLabel(MainWindow::Float32WordOrder order)
{
    switch (order) {
    case MainWindow::Float32WordOrder::CDAB: return QStringLiteral("CDAB");
    case MainWindow::Float32WordOrder::ABCD: return QStringLiteral("ABCD");
    case MainWindow::Float32WordOrder::BADC: return QStringLiteral("BADC");
    case MainWindow::Float32WordOrder::DCBA: return QStringLiteral("DCBA");
    }
    return QStringLiteral("CDAB");
}

QString float64WordOrderLabel(MainWindow::Float64WordOrder order)
{
    switch (order) {
    case MainWindow::Float64WordOrder::GHEF_CDAB: return QStringLiteral("GHEF CDAB");
    case MainWindow::Float64WordOrder::ABCD_EFGH: return QStringLiteral("ABCD EFGH");
    case MainWindow::Float64WordOrder::BADC_FEHG: return QStringLiteral("BADC FEHG");
    case MainWindow::Float64WordOrder::DCBA_HGFE: return QStringLiteral("DCBA HGFE");
    }
    return QStringLiteral("GHEF CDAB");
}

MainWindow::Float32WordOrder float32WordOrderFromString(const QString &text)
{
    const QString key = text.trimmed().toUpper();
    if (key == QStringLiteral("ABCD")) return MainWindow::Float32WordOrder::ABCD;
    if (key == QStringLiteral("BADC")) return MainWindow::Float32WordOrder::BADC;
    if (key == QStringLiteral("DCBA")) return MainWindow::Float32WordOrder::DCBA;
    return MainWindow::Float32WordOrder::CDAB;
}

MainWindow::Float64WordOrder float64WordOrderFromString(const QString &text)
{
    const QString key = text.trimmed().toUpper().remove(QLatin1Char(' '));
    if (key == QStringLiteral("ABCD_EFGH") || key == QStringLiteral("ABCDEFGH")) {
        return MainWindow::Float64WordOrder::ABCD_EFGH;
    }
    if (key == QStringLiteral("BADC_FEHG") || key == QStringLiteral("BADCFEHG")) {
        return MainWindow::Float64WordOrder::BADC_FEHG;
    }
    if (key == QStringLiteral("DCBA_HGFE") || key == QStringLiteral("DCBAHGFE")) {
        return MainWindow::Float64WordOrder::DCBA_HGFE;
    }
    return MainWindow::Float64WordOrder::GHEF_CDAB;
}

namespace RegisterMapCol {
constexpr int Direction = 0;
constexpr int Address = 1;
constexpr int Comment = 2;
constexpr int Format = 3;
constexpr int ColumnCount = 4;
}

namespace SimRegisterCol {
constexpr int Direction = 0;
constexpr int Address = 1;
constexpr int Description = 2;
constexpr int Value = 3;
constexpr int ColumnCount = 4;
}

enum class RegisterMapDirection { Unknown, Read, Write };

RegisterMapDirection parseRegisterMapDirection(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return RegisterMapDirection::Unknown;
    }
    const QString lower = t.toLower();
    if (t == QStringLiteral("读") || lower == QStringLiteral("read") || lower == QStringLiteral("r")) {
        return RegisterMapDirection::Read;
    }
    if (t == QStringLiteral("写") || lower == QStringLiteral("write") || lower == QStringLiteral("w")) {
        return RegisterMapDirection::Write;
    }
    return RegisterMapDirection::Unknown;
}

QString stripUtf8Bom(QString text)
{
    if (text.startsWith(QChar(0xFEFF))) {
        text = text.mid(1);
    }
    return text;
}

QStringList parseRegisterMapCsvLine(const QString &line)
{
    QStringList parts;
    bool inQuotes = false;
    QString field;
    for (int i = 0; i < line.length(); ++i) {
        const QChar c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            parts.append(stripUtf8Bom(field.trimmed()));
            field.clear();
        } else {
            field += c;
        }
    }
    parts.append(stripUtf8Bom(field.trimmed()));
    return parts;
}

QString escapeRegisterMapCsvField(QString value)
{
    if (value.contains(',') || value.contains('"') || value.contains('\n')) {
        return QStringLiteral("\"") + value.replace('"', QStringLiteral("\"\"")) + QStringLiteral("\"");
    }
    return value;
}

QString mapRegisterFormatToSimFormat(const QString &regFmt)
{
    const QString fmtText = regFmt.trimmed().toUpper();
    if (fmtText.isEmpty()) {
        return QStringLiteral("Unsigned");
    }
    if (fmtText.contains(QStringLiteral("LREAL"))) {
        return QStringLiteral("64-bit Float");
    }
    if (fmtText.contains(QStringLiteral("REAL"))) {
        return QStringLiteral("32-bit Float");
    }
    if (fmtText.contains(QStringLiteral("DINT")) || fmtText.contains(QStringLiteral("INT32"))) {
        return QStringLiteral("32-bit Signed");
    }
    if (fmtText.contains(QStringLiteral("UDINT")) || fmtText.contains(QStringLiteral("UINT32"))) {
        return QStringLiteral("32-bit Unsigned");
    }
    if (fmtText.contains(QStringLiteral("STRING")) || fmtText.contains(QStringLiteral("WSTRING"))) {
        return QStringLiteral("String");
    }
    if (fmtText.contains(QStringLiteral("BOOL"))) {
        return QStringLiteral("Binary");
    }
    if (fmtText.contains(QStringLiteral("UINT")) || fmtText.contains(QStringLiteral("WORD"))) {
        return QStringLiteral("Unsigned");
    }
    if (fmtText.contains(QStringLiteral("INT"))) {
        return QStringLiteral("Signed");
    }
    return QStringLiteral("Unsigned");
}

QString gitIgnoreDefaultTemplate(const QString &type)
{
    if (type == QStringLiteral("Qt")) {
        return QStringLiteral(
            "# Qt intermediate files\n"
            "*.o\n*.obj\nMakefile*\nmoc_*\nui_*\nqrc_*\n.qmake.stash\nbuild/\nbuild_*/\n\n"
            ".qtc_clangd/\n"
            "build_log.txt\n\n"
            "# Logs and local data dumps\n"
            "*.log\n*.LOG\nmonitor_logs/\n*.csv\n\n"
            "# Project specific\n"
            "ModbusTCPAssistant\n*.user\n*.user.*\n\n"
            "# OS files\n.DS_Store\nThumbs.db\n");
    }
    if (type == QStringLiteral("Keil")) {
        return QStringLiteral(
            "# Keil intermediate files\n"
            "*.lst\n*.obj\n*.o\n*.d\n*.crf\n*.lnp\n*.axf\n*.htm\n*.build_log.htm\n"
            "*.dep\n*.ie\n*.i\n"
            "# Keil IDE files\n"
            "Listings/\nObjects/\n"
            "*.uvgui.*\n*.uvguix.*\n*.bak\n\n"
            "# Logs and local data dumps\n"
            "*.log\n*.LOG\nmonitor_logs/\n*.csv\n\n"
            "# OS files\n.DS_Store\nThumbs.db\n");
    }
    return QString();
}

QString gitIgnoreTemplateFilePath(const QString &type)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                        + QStringLiteral("/gitignore_templates");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + type + QStringLiteral(".gitignore");
}

bool gitIgnoreSaveTemplate(const QString &type, const QString &content)
{
    QFile file(gitIgnoreTemplateFilePath(type));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    file.write(content.toUtf8());
    file.close();
    return true;
}

QString gitIgnoreTemplateContent(const QString &type)
{
    const QString path = gitIgnoreTemplateFilePath(type);
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString stored = QString::fromUtf8(file.readAll());
        file.close();
        if (!stored.trimmed().isEmpty())
            return stored;
    }

    // 从旧版 QSettings 迁移一次
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QString legacy = settings.value(QStringLiteral("GitIgnoreTemplates/") + type).toString();
    if (!legacy.trimmed().isEmpty()) {
        gitIgnoreSaveTemplate(type, legacy);
        settings.remove(QStringLiteral("GitIgnoreTemplates/") + type);
        return legacy;
    }
    return gitIgnoreDefaultTemplate(type);
}

QString gitIgnoreNormalizeLine(QString line)
{
    line = line.trimmed();
    line.remove(QLatin1Char('\r'));
    return line;
}

QStringList gitIgnoreEffectiveLines(const QString &content)
{
    QStringList lines;
    for (const QString &raw : content.split(QLatin1Char('\n'))) {
        const QString line = gitIgnoreNormalizeLine(raw);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        lines << line;
    }
    return lines;
}

struct GitIgnoreTemplateDiff {
    QStringList missingInFile; // 模板有、.gitignore 没有
    QStringList extraInFile;   // .gitignore 有、模板没有
};

GitIgnoreTemplateDiff gitIgnoreDiffAgainstTemplate(const QString &fileContent, const QString &templateType)
{
    QSet<QString> fileLines;
    for (const QString &line : gitIgnoreEffectiveLines(fileContent))
        fileLines.insert(line);

    QSet<QString> templateLines;
    for (const QString &line : gitIgnoreEffectiveLines(gitIgnoreTemplateContent(templateType)))
        templateLines.insert(line);

    GitIgnoreTemplateDiff diff;
    for (const QString &line : templateLines) {
        if (!fileLines.contains(line))
            diff.missingInFile << line;
    }
    for (const QString &line : fileLines) {
        if (!templateLines.contains(line))
            diff.extraInFile << line;
    }
    diff.missingInFile.sort();
    diff.extraInFile.sort();
    return diff;
}

QString gitIgnorePickTemplateType(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    QPushButton *qtBtn = msgBox.addButton(QStringLiteral("Qt/C++ 模板"), QMessageBox::AcceptRole);
    QPushButton *keilBtn = msgBox.addButton(QStringLiteral("Keil/C 模板"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = msgBox.addButton(QStringLiteral("暂不"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(skipBtn);
    msgBox.exec();

    const QAbstractButton *clicked = msgBox.clickedButton();
    if (clicked == qtBtn)
        return QStringLiteral("Qt");
    if (clicked == keilBtn)
        return QStringLiteral("Keil");
    return QString();
}

bool gitIgnoreAppendLinesToTemplate(QTextEdit *log, const QString &targetType, const QStringList &lines)
{
    QString tmpl = gitIgnoreTemplateContent(targetType);
    QSet<QString> existing;
    for (const QString &line : gitIgnoreEffectiveLines(tmpl))
        existing.insert(line);

    QStringList toAdd;
    for (QString line : lines) {
        line = gitIgnoreNormalizeLine(line);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (!existing.contains(line)) {
            toAdd << line;
            existing.insert(line);
        }
    }
    if (toAdd.isEmpty())
        return false;

    if (!tmpl.endsWith(QLatin1Char('\n')))
        tmpl += QLatin1Char('\n');
    tmpl += QStringLiteral("\n# Added from project .gitignore\n");
    for (const QString &line : toAdd)
        tmpl += line + QLatin1Char('\n');

    if (!gitIgnoreSaveTemplate(targetType, tmpl)) {
        log->append(QStringLiteral("<font color='red'>错误: 无法写入模板文件 %1</font>")
                        .arg(gitIgnoreTemplateFilePath(targetType)));
        return false;
    }
    log->append(QStringLiteral("成功: 已将 %1 条规则写入 %2 模板（%3），新建 .gitignore 时会自动包含。")
                    .arg(toAdd.size())
                    .arg(targetType)
                    .arg(gitIgnoreTemplateFilePath(targetType)));
    return true;
}

} // namespace

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
    , float32WordOrder(Float32WordOrder::CDAB)
    , float64WordOrder(Float64WordOrder::GHEF_CDAB)
{
    setWindowTitle(QStringLiteral("李晨阳的linux工作助手"));
    resize(1280, 780);
    setMinimumSize(960, 600);

    // Initialize Objects
    tcpSocket = new QTcpSocket(this);
    continuousTimer = new QTimer(this);
    serialPort = new QSerialPort(this);
    simMainDevice = new ModbusSlave(this);
    simAGVDevice = new ModbusSlave(this);
    deepSeekClient = new DeepSeekClient(this);
    connect(deepSeekClient, &DeepSeekClient::chatFinished, this, &MainWindow::onDeepSeekCommitMsgReady);
    connect(deepSeekClient, &DeepSeekClient::chatFailed, this, &MainWindow::onDeepSeekCommitMsgFailed);
    deepSeekHelpClient = new DeepSeekClient(this);
    connect(deepSeekHelpClient, &DeepSeekClient::chatFinished, this, &MainWindow::onDeepSeekGitHelpReady);
    connect(deepSeekHelpClient, &DeepSeekClient::chatFailed, this, &MainWindow::onDeepSeekGitHelpFailed);
    simWriteRefreshTimer = new QTimer(this);
    simWriteRefreshTimer->setSingleShot(true);
    simWriteRefreshTimer->setInterval(33);
    connect(simWriteRefreshTimer, &QTimer::timeout, this, &MainWindow::flushPendingSimWriteRefresh);
    monitorTimer = new QTimer(this);
    tcpServer = new QTcpServer(this);
    tcpAssistantSocket = nullptr; // Initialize in connect/onNewConnection
    tcpCyclicTimer = new QTimer(this);

    // Create UI
    createWidgets();
    createLayouts();
    createMenus();
    createConnections();

    // Load History
    loadConnectionHistory();
    loadGitHistory();
    loadGitDiffReminderSettings();
    loadGitNetworkSettings();
    QTimer::singleShot(0, this, &MainWindow::deferredGitRepoInit);
    loadModbusFloatOrderSettings();
    loadRegisterTables();
    loadAutoScene(); // 自动加载上次保存的寄存器设置、格式和波形
    syncSimulatorTablesFromMaps();

    dailyReportAutoSaveTimer = new QTimer(this);
    dailyReportAutoSaveTimer->setInterval(60 * 1000);
    connect(dailyReportAutoSaveTimer, &QTimer::timeout, this, &MainWindow::onDailyReportAutoSaveTick);
    dailyReportAutoSaveTimer->start();
    // 稍晚检查，给节假日助手 API 留出时间更新 isWorkdayToday
    QTimer::singleShot(8000, this, &MainWindow::onDailyReportAutoSaveTick);

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
    connect(simMainDevice, &ModbusSlave::registersChanged, this, &MainWindow::onRegistersChanged);
    connect(simAGVDevice, &ModbusSlave::registersChanged, this, &MainWindow::onRegistersChanged);

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
    navWidget->addItem(QStringLiteral("Modbus TCP"));
    navWidget->addItem(QStringLiteral("串口调试"));
    navWidget->addItem(QStringLiteral("Git 工作流"));
    navWidget->addItem(QStringLiteral("从站模拟器"));
    navWidget->addItem(QStringLiteral("TCP 通讯"));
    navWidget->addItem(QStringLiteral("性能监控"));
    navWidget->addItem(QStringLiteral("快捷助手"));
    navWidget->addItem(QStringLiteral("生活办公"));
    navWidget->setFixedWidth(168);
    navWidget->setFocusPolicy(Qt::NoFocus);
    navWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    navWidget->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: #1f2a37;"
        "  border: none;"
        "  outline: 0;"
        "  padding: 8px 0;"
        "  color: #c5d0db;"
        "}"
        "QListWidget::item {"
        "  height: 44px;"
        "  margin: 2px 8px;"
        "  padding-left: 12px;"
        "  border-radius: 6px;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item:hover { background: #2a3747; color: #ffffff; }"
        "QListWidget::item:selected {"
        "  background: #2f6fed;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "}"));

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
    cmbDisplayFormat->addItems(QStringList() << "十进制" << "十六进制" << "二进制" << "32位浮点数" << "64位浮点数" << "字符串");

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
    cmbWriteFormat->addItems(QStringList() << "十进制" << "十六进制" << "二进制" << "32位浮点数" << "64位浮点数" << "字符串");

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

    tblGitRepoMeta = new QTableWidget();
    tblGitRepoMeta->setColumnCount(3);
    tblGitRepoMeta->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("路径") << QStringLiteral("中文名") << QStringLiteral("主项目"));
    tblGitRepoMeta->horizontalHeader()->setStretchLastSection(false);
    tblGitRepoMeta->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tblGitRepoMeta->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tblGitRepoMeta->setColumnWidth(2, 64);
    tblGitRepoMeta->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblGitRepoMeta->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    tblGitRepoMeta->verticalHeader()->setVisible(false);
    tblGitRepoMeta->setMinimumHeight(48);
    tblGitRepoMeta->setMaximumHeight(120);
    tblGitRepoMeta->setToolTip(QStringLiteral("为各记忆路径设置日报中文名；勾选「主项目」后复制到日报时填入该仓库的工作目标与完成度"));

    tblGitGoals = new QTableWidget();
    tblGitGoals->setColumnCount(10);
    tblGitGoals->setHorizontalHeaderLabels(
        QStringList() << "" << "目标" << "进度" << "难度" << "父目标" << "开始日期" << "计划结束"
                      << "实际完成" << "分支" << "备注");
    tblGitGoals->setColumnWidth(0, 32);
    tblGitGoals->horizontalHeader()->setStretchLastSection(true);
    tblGitGoals->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblGitGoals->setSelectionMode(QAbstractItemView::SingleSelection);
    tblGitGoals->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblGitGoals->verticalHeader()->setVisible(false);
    tblGitGoals->setMinimumHeight(80);
    tblGitGoals->setMaximumHeight(220);
    tblGitGoals->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tblGitGoals->setToolTip(QStringLiteral("双击行可将目标分支下拉框切换到该目标绑定的分支"));

    btnGitGoalAdd = new QPushButton("添加目标");
    btnGitGoalEdit = new QPushButton("编辑目标");
    btnGitGoalDelete = new QPushButton("删除目标");
    btnGitGoalStart = new QPushButton("目标开始");
    btnGitGoalStart->setToolTip("记录开始日期并补齐上级未填写的开始日期；可翻译分支名并选择是否创建 Git 分支");
    btnGitGoalStart->setStyleSheet("background-color: #c8e6c9; font-weight: bold;");

    lblGitCurrentBranch = new QLabel(QStringLiteral("(未知)"));
    lblGitCurrentBranch->setStyleSheet(QStringLiteral("font-weight: bold;"));
    lblGitCurrentBranch->setMinimumWidth(120);

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
    btnGitAiCommitMsg = new QPushButton(QStringLiteral("AI 整理提交说明"));
    btnGitAiCommitMsg->setToolTip(
        QStringLiteral("用 DeepSeek 根据未提交改动生成提交说明，填入输入框；可选择是否立即暂存并提交"));
    btnGitAiCommitMsg->setStyleSheet(QStringLiteral("background-color: #e8f5e9; font-weight: bold;"));
    btnGitAskDeepSeek = new QPushButton(QStringLiteral("不懂，问 DeepSeek"));
    btnGitAskDeepSeek->setToolTip(
        QStringLiteral("把下方 Git 输出发给 DeepSeek，用通俗中文解释发生了什么，并告诉你接下来该怎么做"));
    btnGitAskDeepSeek->setStyleSheet(QStringLiteral("background-color: #fff3e0; font-weight: bold;"));

    cmbGitRemote = new QComboBox();
    cmbGitRemote->addItem("GitHub");
    cmbGitRemote->addItem("origin");
    cmbGitRemote->setEditable(true); // Allow custom remotes
    
    btnGitAdd = new QPushButton("git add . (暂存全部)");
    btnGitCommit = new QPushButton("git commit (提交)");
    btnGitPush = new QPushButton("git push (推送)");
    btnGitPull = new QPushButton("git pull (拉取)");
    btnGitMerge = new QPushButton("git merge (合并)");
    btnGitRebase = new QPushButton("git rebase (变基)");
    btnGitRebase->setToolTip("将当前分支的提交重新应用到所选分支之上（会改写当前分支历史）");
    btnGitRebase->setStyleSheet("background-color: #fff8e1; font-weight: bold;");
    btnGitStatus = new QPushButton("git status (状态)");
    btnGitDiff = new QPushButton("git diff (差异)");
    btnGitDiff->setToolTip("显示工作区与暂存区的差异");
    btnGitFetch = new QPushButton("git fetch --prune (同步远端)");
    chkGitAutoFetch = new QCheckBox(QStringLiteral("换仓库时自动 fetch"));
    chkGitAutoFetch->setChecked(false);
    chkGitAutoFetch->setToolTip(QStringLiteral("开启后，切换记忆路径时会自动执行 git fetch。代理异常时建议关闭。"));
    chkGitAutoPushAfterCommit = new QCheckBox(QStringLiteral("提交后自动推送"));
    chkGitAutoPushAfterCommit->setChecked(true);
    chkGitAutoPushAfterCommit->setToolTip(
        QStringLiteral("开启后，git commit 成功且有未推送提交时自动执行 git push（含按钮、AI 提交与控制台）。"));
    lblGitPendingStatus = new QLabel(QStringLiteral("Git: —"));
    lblGitPendingStatus->setToolTip(QStringLiteral("点击可跳转到首个有未提交/未推送的仓库"));
    lblGitPendingStatus->setCursor(Qt::PointingHandCursor);
    lblGitPendingStatus->setMinimumWidth(180);
    statusBar()->addPermanentWidget(lblGitPendingStatus);
    lblGitPendingStatus->installEventFilter(this);
    gitPendingStatusTimer = new QTimer(this);
    gitPendingStatusTimer->setInterval(60 * 1000);
    lblGitNetworkStatus = new QLabel();
    lblGitNetworkStatus->setVisible(false);
    barGitNetworkBusy = new QProgressBar();
    barGitNetworkBusy->setRange(0, 0);
    barGitNetworkBusy->setTextVisible(false);
    barGitNetworkBusy->setFixedHeight(14);
    barGitNetworkBusy->setVisible(false);
    btnGitCancelNetwork = new QPushButton(QStringLiteral("取消通讯"));
    btnGitCancelNetwork->setEnabled(false);
    btnGitCancelNetwork->setToolTip(QStringLiteral("中断当前进行中的 git 远程通讯（fetch/pull/push）"));
    btnGitStash = new QPushButton("git stash (临时存档)");
    btnGitStashPop = new QPushButton("git stash pop (恢复临存)");
    btnGitAutoDiffReminder = new QPushButton(QStringLiteral("开启可执行文件提醒"));
    btnGitAutoDiffReminder->setCheckable(true);
    btnGitAutoDiffReminder->setStyleSheet("background-color: #fff3cd; font-weight: bold;");
    btnGitAutoDiffReminder->setToolTip(
        QStringLiteral("定时检查记忆列表中所有仓库的最新可执行文件是否相对基线有更新，有则提醒"));
    btnGitExeReminderCheckNow = new QPushButton(QStringLiteral("立即检查"));
    btnGitExeReminderCheckNow->setToolTip(QStringLiteral("立刻扫描全部记忆仓库的可执行文件更新（不必等间隔）"));
    spinGitDiffIntervalMinutes = new QSpinBox();
    spinGitDiffIntervalMinutes->setRange(1, 24 * 60);
    spinGitDiffIntervalMinutes->setValue(5);
    spinGitDiffIntervalMinutes->setSuffix(" 分钟");
    spinGitDiffIntervalMinutes->setToolTip(QStringLiteral("可执行文件更新检查间隔"));

    btnGitRemoteAdd = new QPushButton("链接远程仓库");
    btnGitRemoteAdd->setToolTip("为本地目录添加远程仓库链接 (git remote add)");
    btnGitRemoteAdd->setStyleSheet("background-color: #e8f5e9; font-weight: bold;"); // 浅绿色

    cmbGitHistory = new QComboBox();
    btnGitRefreshLog = new QPushButton("刷新历史");
    btnGitReset = new QPushButton("硬回退 (Reset)");
    btnGitReset->setStyleSheet("color: red; font-weight: bold;");
    
    btnGitSoftReset = new QPushButton("软回退 (SoftReset)");
    btnGitSoftReset->setStyleSheet("color: #FF8C00; font-weight: bold;");
    btnGitSoftReset->setToolTip("执行 git reset --soft HEAD^ (撤回最后一次提交，保留代码修改)");

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
    txtGitLog->setStyleSheet("background: #1e1e1e; color: #d4d4d4; font-family: Monospace; font-size: 12px; border: none;");
    txtGitLog->setPlaceholderText(QStringLiteral("Git 命令输出…"));

    txtGitCmdInput = new QLineEdit();
    txtGitCmdInput->setPlaceholderText(
        QStringLiteral("在当前 Git 仓库目录下执行，例如 status 或 git log -5（↑↓ 翻历史）"));
    txtGitCmdInput->setStyleSheet(
        QStringLiteral("QLineEdit { background: #1e1e1e; color: #d4d4d4; font-family: Monospace; "
                       "font-size: 12px; border: none; padding: 6px 4px; selection-background-color: #264f78; }"));
    txtGitCmdInput->installEventFilter(this);
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
    QVBoxLayout *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    pageLayout->setSpacing(6);

    // Upper controls scroll on small screens; log stays usable below.
    QWidget *controlsHost = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(controlsHost);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    
    // 1. Repository Selection
    QGroupBox *grpRepo = new QGroupBox("Git 仓库 (记忆路径)");
    QVBoxLayout *layRepoVBox = new QVBoxLayout();
    QHBoxLayout *layRepo = new QHBoxLayout();
    layRepo->addWidget(new QLabel("路径:"));
    
    layRepo->addWidget(cmbGitDir, 1);
    layRepo->addWidget(btnGitSelectDir);
    layRepo->addWidget(btnGitRemoveHistory);
    layRepoVBox->addLayout(layRepo);
    layRepoVBox->addWidget(chkGitAutoFetch);
    layRepoVBox->addWidget(chkGitAutoPushAfterCommit);
    layRepoVBox->addWidget(new QLabel(QStringLiteral("日报路径配置:")));
    layRepoVBox->addWidget(tblGitRepoMeta);
    grpRepo->setLayout(layRepoVBox);
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
    QHBoxLayout *layCurrentBranch = new QHBoxLayout();
    layCurrentBranch->addWidget(new QLabel(QStringLiteral("当前分支:")));
    layCurrentBranch->addWidget(lblGitCurrentBranch, 1);
    layCurrentBranch->addStretch();
    layOps->addLayout(layCurrentBranch);

    QHBoxLayout *layBranch = new QHBoxLayout();
    layBranch->addWidget(new QLabel(QStringLiteral("目标分支:")));
    layBranch->addWidget(cmbGitBranches, 1);
    layOps->addLayout(layBranch);

    QHBoxLayout *layBranchBtns = new QHBoxLayout();
    layBranchBtns->addWidget(btnGitRefreshBranches);
    layBranchBtns->addWidget(btnGitCheckout);
    layBranchBtns->addWidget(btnGitSyncRemote);
    layBranchBtns->addWidget(btnGitCreateBranch);
    layBranchBtns->addWidget(btnGitDeleteBranch);
    layBranchBtns->addStretch();
    layOps->addLayout(layBranchBtns);
    
    // Commit Msg
    QHBoxLayout *layCommit = new QHBoxLayout();
    layCommit->addWidget(new QLabel("提交信息:"));
    layCommit->addWidget(txtGitCommitMsg, 1);
    layCommit->addWidget(btnGitAiCommitMsg);
    layOps->addLayout(layCommit);
    
    // Common actions — keep columns modest so narrow widths do not crush labels
    QGridLayout *layBtns = new QGridLayout();
    layBtns->setHorizontalSpacing(6);
    layBtns->setVerticalSpacing(6);
    layBtns->addWidget(btnGitAdd, 0, 0);
    layBtns->addWidget(btnGitCommit, 0, 1);
    layBtns->addWidget(btnGitStatus, 0, 2);
    layBtns->addWidget(btnGitDiff, 0, 3);

    layBtns->addWidget(btnGitFetch, 1, 0);
    layBtns->addWidget(btnGitPush, 1, 1);
    layBtns->addWidget(btnGitPull, 1, 2);
    layBtns->addWidget(btnGitMerge, 1, 3);

    layBtns->addWidget(new QLabel("远程仓库:"), 2, 0);
    layBtns->addWidget(cmbGitRemote, 2, 1, 1, 2);
    layBtns->addWidget(btnGitRebase, 2, 3);

    layBtns->addWidget(btnGitStash, 3, 0);
    layBtns->addWidget(btnGitStashPop, 3, 1);
    layBtns->addWidget(btnGitRemoteAdd, 3, 2);
    layOps->addLayout(layBtns);

    QHBoxLayout *layReminder = new QHBoxLayout();
    layReminder->addWidget(btnGitAutoDiffReminder);
    layReminder->addWidget(btnGitExeReminderCheckNow);
    layReminder->addWidget(new QLabel(QStringLiteral("检查间隔:")));
    layReminder->addWidget(spinGitDiffIntervalMinutes);
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
    layOps->addLayout(layHist);

    QHBoxLayout *layHistBtns = new QHBoxLayout();
    layHistBtns->addWidget(btnGitRefreshLog);
    layHistBtns->addWidget(btnGitSoftReset);
    layHistBtns->addWidget(btnGitReset);
    layHistBtns->addStretch();
    layOps->addLayout(layHistBtns);

    // SCP Transfer Section
    QHBoxLayout *layScp = new QHBoxLayout();
    layScp->addWidget(new QLabel("目标:"));
    layScp->addWidget(txtScpTargetIp, 1);
    layScp->addWidget(new QLabel("密码:"));
    layScp->addWidget(txtScpPassword, 1);
    layOps->addLayout(layScp);

    QHBoxLayout *layScpBtns = new QHBoxLayout();
    layScpBtns->addWidget(btnScpTransfer);
    layScpBtns->addWidget(btnRebootTarget);
    layScpBtns->addStretch();
    layOps->addLayout(layScpBtns);

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
    gitDiffReminderTimer->setInterval(5 * 60 * 1000);
    
    layMon->addWidget(lblCpuUsage);
    layMon->addWidget(chartCpu);
    layMon->addWidget(lblMemUsage);
    layMon->addWidget(chartMem);
    layMon->addStretch();
    layOps->addLayout(layMon);
    
    grpOps->setLayout(layOps);
    layout->addWidget(grpOps);
    layout->addStretch(0);

    QScrollArea *controlsScroll = new QScrollArea();
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    controlsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    controlsScroll->setWidget(controlsHost);
    controlsScroll->setMinimumHeight(100);
    controlsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 3. Log Output + interactive command line (same console surface)
    QGroupBox *grpLog = new QGroupBox("Git 输出");
    QVBoxLayout *layLog = new QVBoxLayout();
    QHBoxLayout *layNetBusy = new QHBoxLayout();
    layNetBusy->addWidget(lblGitNetworkStatus, 1);
    layNetBusy->addWidget(barGitNetworkBusy, 2);
    layNetBusy->addWidget(btnGitCancelNetwork);
    layLog->addLayout(layNetBusy);

    QFrame *consoleFrame = new QFrame();
    consoleFrame->setObjectName(QStringLiteral("gitConsoleFrame"));
    consoleFrame->setStyleSheet(
        QStringLiteral("#gitConsoleFrame { background: #1e1e1e; border: 1px solid #3c3c3c; }"
                       "#gitConsoleFrame QTextEdit { background: #1e1e1e; color: #d4d4d4; "
                       "font-family: Monospace; font-size: 12px; border: none; }"
                       "#gitConsoleFrame QLineEdit { background: #1e1e1e; color: #d4d4d4; "
                       "font-family: Monospace; font-size: 12px; border: none; }"
                       "#gitConsoleFrame QLabel { background: #1e1e1e; color: #4ec9b0; "
                       "font-family: Monospace; font-size: 12px; }"));
    QVBoxLayout *layConsole = new QVBoxLayout(consoleFrame);
    layConsole->setContentsMargins(4, 4, 4, 4);
    layConsole->setSpacing(0);
    txtGitLog->setMinimumHeight(80);
    layConsole->addWidget(txtGitLog, 1);

    QFrame *cmdSep = new QFrame();
    cmdSep->setFixedHeight(1);
    cmdSep->setStyleSheet(QStringLiteral("background: #3c3c3c; border: none;"));
    layConsole->addWidget(cmdSep);

    QHBoxLayout *layCmd = new QHBoxLayout();
    layCmd->setContentsMargins(2, 2, 2, 2);
    layCmd->setSpacing(4);
    QLabel *lblGitPrompt = new QLabel(QStringLiteral("$"));
    lblGitPrompt->setFixedWidth(14);
    layCmd->addWidget(lblGitPrompt);
    layCmd->addWidget(txtGitCmdInput, 1);
    layConsole->addLayout(layCmd);

    lblGitConsoleCwd = new QLabel();
    lblGitConsoleCwd->setWordWrap(true);
    lblGitConsoleCwd->setStyleSheet(
        QStringLiteral("color: #858585; font-family: Monospace; font-size: 11px; background: #1e1e1e; "
                       "padding: 2px 4px;"));
    layConsole->addWidget(lblGitConsoleCwd);
    updateGitConsoleCwdLabel();

    QHBoxLayout *layAskHelp = new QHBoxLayout();
    layAskHelp->setContentsMargins(0, 6, 0, 0);
    layAskHelp->addWidget(btnGitAskDeepSeek);
    layAskHelp->addStretch(1);
    layLog->addWidget(consoleFrame, 1);
    layLog->addLayout(layAskHelp);
    grpLog->setLayout(layLog);
    grpLog->setMinimumHeight(140);
    grpLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QSplitter *gitSplitter = new QSplitter(Qt::Vertical);
    gitSplitter->setChildrenCollapsible(false);
    gitSplitter->addWidget(controlsScroll);
    gitSplitter->addWidget(grpLog);
    gitSplitter->setStretchFactor(0, 3);
    gitSplitter->setStretchFactor(1, 2);
    gitSplitter->setSizes({420, 280});

    pageLayout->addWidget(gitSplitter, 1);
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
    spinWaveAddr = new QSpinBox(); spinWaveAddr->setRange(0, ModbusSlave::MaxHoldingRegisterAddress);
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

void MainWindow::createMenus()
{
    // --- 文件 ---
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    QAction *actOpenRepo = fileMenu->addAction(QStringLiteral("打开 Git 仓库…"));
    actOpenRepo->setShortcut(QKeySequence::Open);
    connect(actOpenRepo, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitSelectDirClicked();
    });

    QAction *actOpenDaily = fileMenu->addAction(QStringLiteral("打开日报"));
    actOpenDaily->setToolTip(QStringLiteral("用系统默认程序打开今日日报文件"));
    connect(actOpenDaily, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitOpenDailyReportClicked();
    });

    QAction *actCopyDaily = fileMenu->addAction(QStringLiteral("复制到日报"));
    connect(actCopyDaily, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitCopyForDailyReportClicked();
    });

    fileMenu->addSeparator();

    QMenu *regMapMenu = fileMenu->addMenu(QStringLiteral("寄存器映射"));
    connect(regMapMenu->addAction(QStringLiteral("导出…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(0);
        onExportRegisterMapClicked();
    });
    connect(regMapMenu->addAction(QStringLiteral("导入…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(0);
        onImportRegisterMapClicked();
    });
    connect(regMapMenu->addAction(QStringLiteral("导入标准文件…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(0);
        onImportStandardFileClicked();
    });

    QMenu *simSceneMenu = fileMenu->addMenu(QStringLiteral("模拟器场景"));
    connect(simSceneMenu->addAction(QStringLiteral("保存寄存器快照…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(3);
        onSimSaveSceneClicked();
    });
    connect(simSceneMenu->addAction(QStringLiteral("加载寄存器快照…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(3);
        onSimLoadSceneClicked();
    });
    simSceneMenu->addSeparator();
    connect(simSceneMenu->addAction(QStringLiteral("导出寄存器表 (CSV)…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(3);
        onSimExportCsvClicked();
    });
    connect(simSceneMenu->addAction(QStringLiteral("导入寄存器表 (CSV)…")), &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(3);
        onSimImportCsvClicked();
    });

    fileMenu->addSeparator();
    QAction *actQuit = fileMenu->addAction(QStringLiteral("退出"));
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    // --- 视图 ---
    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    auto *pageGroup = new QActionGroup(this);
    pageGroup->setExclusive(true);
    const QStringList pageNames = {
        QStringLiteral("Modbus TCP"),
        QStringLiteral("串口调试"),
        QStringLiteral("Git 工作流"),
        QStringLiteral("从站模拟器"),
        QStringLiteral("TCP 通讯"),
        QStringLiteral("性能监控"),
        QStringLiteral("快捷助手"),
        QStringLiteral("生活办公"),
    };
    for (int i = 0; i < pageNames.size(); ++i) {
        QAction *act = viewMenu->addAction(pageNames.at(i));
        act->setCheckable(true);
        act->setData(i);
        pageGroup->addAction(act);
        if (i == 0)
            act->setChecked(true);
        connect(act, &QAction::triggered, this, [this, i]() {
            navWidget->setCurrentRow(i);
        });
    }
    connect(navWidget, &QListWidget::currentRowChanged, this, [pageGroup](int row) {
        for (QAction *a : pageGroup->actions()) {
            if (a->data().toInt() == row) {
                a->setChecked(true);
                break;
            }
        }
    });

    // --- 工具 ---
    QMenu *toolsMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));

    QAction *actWorktree = toolsMenu->addAction(QStringLiteral("Worktree 管理…"));
    connect(actWorktree, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitWorktreeManageClicked();
    });

    QAction *actOpenIgnore = toolsMenu->addAction(QStringLiteral("管理 .gitignore…"));
    connect(actOpenIgnore, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitOpenIgnoreClicked();
    });

    QAction *actCheckIgnore = toolsMenu->addAction(QStringLiteral("检查 .gitignore…"));
    connect(actCheckIgnore, &QAction::triggered, this, [this]() {
        navWidget->setCurrentRow(2);
        onGitCheckIgnoreClicked();
    });

    QAction *actSshKey = toolsMenu->addAction(QStringLiteral("获取 SSH 公钥"));
    actSshKey->setToolTip(QStringLiteral("获取本机 SSH 公钥并复制到剪贴板，用于上传 GitHub"));
    connect(actSshKey, &QAction::triggered, this, &MainWindow::onGitGetSshKeyClicked);

    toolsMenu->addSeparator();

    QAction *actSkills = toolsMenu->addAction(QStringLiteral("打开 Cursor Skills…"));
    actSkills->setToolTip(
        QStringLiteral("查看并打开总 Skill（~/.cursor/skills）以及各记忆仓库的 .cursor/skills"));
    connect(actSkills, &QAction::triggered, this, &MainWindow::onGitOpenSkillsClicked);

    toolsMenu->addSeparator();

    QAction *actExeCheckNow = toolsMenu->addAction(QStringLiteral("立即检查可执行文件更新"));
    connect(actExeCheckNow, &QAction::triggered, this, &MainWindow::onGitAutoDiffReminderTick);

    // --- 设置 ---
    QMenu *settingsMenu = menuBar()->addMenu(QStringLiteral("设置(&S)"));

    actAutostart = settingsMenu->addAction(QStringLiteral("开机自启动"));
    actAutostart->setCheckable(true);
    connect(actAutostart, &QAction::toggled, this, &MainWindow::onAutostartToggled);

    actExeReminder = settingsMenu->addAction(QStringLiteral("可执行文件更新提醒"));
    actExeReminder->setCheckable(true);
    actExeReminder->setToolTip(
        QStringLiteral("定时检查记忆列表中所有仓库的最新可执行文件是否相对基线有更新，有则提醒"));
    connect(actExeReminder, &QAction::toggled, this, [this](bool checked) {
        if (btnGitAutoDiffReminder && btnGitAutoDiffReminder->isChecked() != checked)
            btnGitAutoDiffReminder->setChecked(checked);
    });

    settingsMenu->addSeparator();

    actDeepSeekSettings = settingsMenu->addAction(QStringLiteral("DeepSeek 设置…"));
    actDeepSeekSettings->setToolTip(QStringLiteral("配置 API Key / Base URL / 模型名（保存在本机，不进仓库）"));
    connect(actDeepSeekSettings, &QAction::triggered, this, &MainWindow::onGitDeepSeekSettingsClicked);

    QAction *actPlatform = settingsMenu->addAction(QStringLiteral("运行平台…"));
    connect(actPlatform, &QAction::triggered, this, &MainWindow::showPlatformModeDialog);

    QAction *actFloatOrder = settingsMenu->addAction(QStringLiteral("Modbus 浮点字序…"));
    connect(actFloatOrder, &QAction::triggered, this, &MainWindow::showModbusFloatOrderDialog);

    // --- 帮助 ---
    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    QAction *actAbout = helpMenu->addAction(QStringLiteral("关于…"));
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            QStringLiteral("关于"),
            QStringLiteral("<h3>%1</h3>"
                           "<p>集成 Modbus TCP / 串口调试 / Git 工作流 / 从站模拟器 / "
                           "TCP 通讯 / 性能监控 / 快捷助手 / 生活办公。</p>"
                           "<p>组织：%2</p>")
                .arg(QApplication::applicationDisplayName(),
                     QApplication::organizationName()));
    });

    syncAutostartActionState();
}

void MainWindow::showPlatformModeDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("运行平台"));
    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QLabel *hint = new QLabel(
        QStringLiteral("Git / Worktree 脚本与输出编码跟随所选平台；\n"
                       "焦点屏蔽、性能监控、快捷助手跟随实际操作系统。"),
        &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QFormLayout *form = new QFormLayout();
    QComboBox *cb = new QComboBox(&dlg);
    cb->addItem(QStringLiteral("Linux"), static_cast<int>(PlatformPrefs::PlatformMode::Linux));
    cb->addItem(QStringLiteral("Windows"), static_cast<int>(PlatformPrefs::PlatformMode::Windows));
    const int idx = cb->findData(static_cast<int>(PlatformPrefs::mode()));
    cb->setCurrentIndex(idx >= 0 ? idx : 0);
    form->addRow(QStringLiteral("目标平台:"), cb);
    layout->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const auto previous = PlatformPrefs::mode();
    const auto selected = static_cast<PlatformPrefs::PlatformMode>(cb->currentData().toInt());
    if (selected == previous) {
        return;
    }
    PlatformPrefs::setMode(selected);
    QMessageBox::information(this,
                             QStringLiteral("运行平台"),
                             QStringLiteral("已切换为 %1。\nGit / Worktree 相关行为已立即生效。")
                                 .arg(selected == PlatformPrefs::PlatformMode::Windows
                                          ? QStringLiteral("Windows")
                                          : QStringLiteral("Linux")));
}

QString MainWindow::autostartDesktopFilePath() const
{
    const QString autostartDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                 + QStringLiteral("/autostart");
    return autostartDir + QStringLiteral("/ModbusTCPAssistant.desktop");
}

QString MainWindow::autostartRegistryKey() const
{
    return QStringLiteral("LinuxHelper");
}

bool MainWindow::isAutostartEnabled() const
{
#ifdef Q_OS_WIN
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    const QString stored = runKey.value(autostartRegistryKey()).toString();
    if (stored.isEmpty()) {
        return false;
    }
    const QString execPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return stored.contains(execPath, Qt::CaseInsensitive);
#else
    const QString desktopPath = autostartDesktopFilePath();
    if (!QFile::exists(desktopPath))
        return false;

    QFile file(desktopPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QString execPath = QCoreApplication::applicationFilePath();
    const QString content = QString::fromUtf8(file.readAll());
    return content.contains(QStringLiteral("Exec=")) && content.contains(execPath);
#endif
}

bool MainWindow::setAutostartEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    if (enabled) {
        const QString execPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        runKey.setValue(autostartRegistryKey(), QStringLiteral("\"%1\"").arg(execPath));
    } else {
        runKey.remove(autostartRegistryKey());
    }
    runKey.sync();
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(QStringLiteral("autostart/enabled"), enabled);
    return runKey.status() == QSettings::NoError;
#else
    const QString desktopPath = autostartDesktopFilePath();
    const QFileInfo desktopInfo(desktopPath);

    if (enabled) {
        QDir autostartDir = desktopInfo.dir();
        if (!autostartDir.exists() && !autostartDir.mkpath(QStringLiteral(".")))
            return false;

        // Write UTF-8 bytes directly. QTextStream << const char* treats the
        // payload as Latin-1 and double-encodes Chinese (can inject NEL into Name).
        const QString execPath = QCoreApplication::applicationFilePath();
        const QString quotedExec = execPath.contains(QLatin1Char(' '))
                                       ? QStringLiteral("\"%1\"").arg(execPath)
                                       : execPath;
        const QByteArray content =
            QByteArrayLiteral("[Desktop Entry]\n")
            + QByteArrayLiteral("Type=Application\n")
            + QByteArrayLiteral("Version=1.0\n")
            + QStringLiteral("Name=李晨阳的linux工作助手\n").toUtf8()
            + QStringLiteral("Comment=Linux工作助手\n").toUtf8()
            + QByteArrayLiteral("Exec=") + quotedExec.toUtf8() + '\n'
            + QByteArrayLiteral("TryExec=") + execPath.toUtf8() + '\n'
            + QByteArrayLiteral("Path=") + QFileInfo(execPath).absolutePath().toUtf8() + '\n'
            + QByteArrayLiteral("Terminal=false\n")
            + QByteArrayLiteral("Categories=Utility;\n")
            + QByteArrayLiteral("StartupNotify=true\n")
            + QByteArrayLiteral("X-GNOME-Autostart-enabled=true\n");

        QFile file(desktopPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        if (file.write(content) != content.size())
            return false;
        file.close();

        QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
        settings.setValue(QStringLiteral("autostart/enabled"), true);
        return true;
    }

    if (QFile::exists(desktopPath) && !QFile::remove(desktopPath))
        return false;

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(QStringLiteral("autostart/enabled"), false);
    return true;
#endif
}

void MainWindow::syncAutostartActionState()
{
    if (!actAutostart)
        return;

    actAutostart->blockSignals(true);
    actAutostart->setChecked(isAutostartEnabled());
    actAutostart->blockSignals(false);
}

void MainWindow::onAutostartToggled(bool checked)
{
    if (setAutostartEnabled(checked))
        return;

    syncAutostartActionState();
    QMessageBox::warning(this,
                         QStringLiteral("设置失败"),
                         checked ? QStringLiteral("无法启用开机自启动，请检查程序是否有写入权限。")
                                 : QStringLiteral("无法关闭开机自启动，请检查 autostart 目录权限。"));
}

void MainWindow::createLayouts()
{
    centralWidget = new QWidget();
    setCentralWidget(centralWidget);
    
    mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    stackedWidget = new QStackedWidget();
    modbusPageWidget = createModbusPage();
    serialPageWidget = createSerialPage();
    gitPageWidget = createGitPage();
    simulatorPageWidget = createSimulatorPage();
    tcpAssistantPageWidget = createTcpAssistantPage();
    performancePageWidget = createPerformancePage();
    inputQuickerPageWidget = new InputQuickerWidget(this);
    lifeAssistantPageWidget = createLifeAssistantPage();
    
    stackedWidget->addWidget(modbusPageWidget);
    stackedWidget->addWidget(serialPageWidget);
    stackedWidget->addWidget(gitPageWidget);
    stackedWidget->addWidget(simulatorPageWidget);
    stackedWidget->addWidget(tcpAssistantPageWidget);
    stackedWidget->addWidget(performancePageWidget);
    stackedWidget->addWidget(inputQuickerPageWidget);
    stackedWidget->addWidget(lifeAssistantPageWidget);

    QWidget *contentHost = new QWidget();
    contentHost->setObjectName(QStringLiteral("contentHost"));
    contentHost->setStyleSheet(QStringLiteral("#contentHost { background: #f4f6f8; }"));
    QVBoxLayout *contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(14, 12, 14, 12);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(stackedWidget);
    
    mainLayout->addWidget(navWidget);
    mainLayout->addWidget(contentHost, 1);
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
        } else if (index == FormatString) {
            spinWriteQuantity->setValue(kDefaultStringRegisterCount);
            txtWriteValues->setPlaceholderText(QStringLiteral("输入 UTF-8 文本，如：你好"));
        } else {
            txtWriteValues->setPlaceholderText(QStringLiteral("e.g. 100,200,300"));
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
    connect(tblGitRepoMeta, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (gitRepoMetaRefreshing || !item || item->column() != 1)
            return;
        const QTableWidgetItem *pathItem = tblGitRepoMeta->item(item->row(), 0);
        if (!pathItem)
            return;
        saveGitRepoAlias(pathItem->data(Qt::UserRole).toString(), item->text());
    });
    connect(cmbGitDir, &QComboBox::currentTextChanged, this, &MainWindow::onGitDirChanged);
    connect(cmbGitBranches, &QComboBox::currentTextChanged, this, &MainWindow::onGitBranchSelectionChanged);
    connect(btnGitGoalAdd, &QPushButton::clicked, this, &MainWindow::onGitGoalAddClicked);
    connect(btnGitGoalEdit, &QPushButton::clicked, this, &MainWindow::onGitGoalEditClicked);
    connect(btnGitGoalDelete, &QPushButton::clicked, this, &MainWindow::onGitGoalDeleteClicked);
    connect(btnGitGoalStart, &QPushButton::clicked, this, &MainWindow::onGitGoalStartClicked);
    connect(tblGitGoals, &QTableWidget::cellDoubleClicked, this, &MainWindow::onGitGoalRowDoubleClicked);
    connect(btnGitRefreshBranches, &QPushButton::clicked, this, [this]() {
        onGitRefreshBranchesClicked(false);
    });
    connect(btnGitCheckout, &QPushButton::clicked, this, &MainWindow::onGitCheckoutClicked);
    connect(btnGitSyncRemote, &QPushButton::clicked, this, &MainWindow::onGitSyncRemoteClicked);
    connect(btnGitCreateBranch, &QPushButton::clicked, this, &MainWindow::onGitCreateBranchClicked);
    connect(btnGitDeleteBranch, &QPushButton::clicked, this, &MainWindow::onGitDeleteBranchClicked);
    connect(btnGitAdd, &QPushButton::clicked, this, &MainWindow::onGitAddClicked);
    connect(btnGitCommit, &QPushButton::clicked, this, &MainWindow::onGitCommitClicked);
    connect(btnGitAiCommitMsg, &QPushButton::clicked, this, &MainWindow::onGitAiCommitMsgClicked);
    connect(btnGitAskDeepSeek, &QPushButton::clicked, this, &MainWindow::onGitAskDeepSeekClicked);
    connect(btnGitPush, &QPushButton::clicked, this, &MainWindow::onGitPushClicked);
    connect(btnGitPull, &QPushButton::clicked, this, &MainWindow::onGitPullClicked);
    connect(btnGitMerge, &QPushButton::clicked, this, &MainWindow::onGitMergeClicked);
    connect(btnGitRebase, &QPushButton::clicked, this, &MainWindow::onGitRebaseClicked);
    connect(btnGitStatus, &QPushButton::clicked, this, &MainWindow::onGitStatusClicked);
    connect(btnGitRemoteAdd, &QPushButton::clicked, this, &MainWindow::onGitRemoteAddClicked);
    connect(btnGitRefreshLog, &QPushButton::clicked, this, &MainWindow::onGitRefreshLogClicked);
    connect(btnGitDiff, &QPushButton::clicked, this, &MainWindow::onGitDiffClicked);
    connect(btnGitFetch, &QPushButton::clicked, this, &MainWindow::onGitFetchClicked);
    connect(btnGitCancelNetwork, &QPushButton::clicked, this, &MainWindow::onGitCancelNetworkClicked);
    connect(chkGitAutoFetch, &QCheckBox::toggled, this, &MainWindow::onGitAutoFetchToggled);
    connect(chkGitAutoPushAfterCommit, &QCheckBox::toggled, this,
            &MainWindow::onGitAutoPushAfterCommitToggled);
    if (gitPendingStatusTimer) {
        connect(gitPendingStatusTimer, &QTimer::timeout, this, &MainWindow::onGitPendingStatusBarTick);
        gitPendingStatusTimer->start();
    }
    connect(txtGitCmdInput, &QLineEdit::returnPressed, this, &MainWindow::onGitConsoleCommandSubmitted);
    connect(btnGitStash, &QPushButton::clicked, this, &MainWindow::onGitStashClicked);
    connect(btnGitStashPop, &QPushButton::clicked, this, &MainWindow::onGitStashPopClicked);
    connect(btnGitAutoDiffReminder, &QPushButton::toggled, this, &MainWindow::onGitAutoDiffReminderToggled);
    connect(btnGitExeReminderCheckNow, &QPushButton::clicked, this, &MainWindow::onGitAutoDiffReminderTick);
    connect(gitDiffReminderTimer, &QTimer::timeout, this, &MainWindow::onGitAutoDiffReminderTick);
    connect(spinGitDiffIntervalMinutes, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int minutes){
        int ms = qMax(1, minutes) * 60 * 1000;
        gitDiffReminderTimer->setInterval(ms);
        if (btnGitAutoDiffReminder->isChecked()) {
            txtGitLog->append(QStringLiteral("[可执行文件提醒] 间隔已更新为 %1 分钟").arg(minutes));
        }
        saveGitDiffReminderSettings();
    });
    connect(btnGitReset, &QPushButton::clicked, this, &MainWindow::onGitResetClicked);
    connect(btnGitSoftReset, &QPushButton::clicked, this, &MainWindow::onGitSoftResetClicked);
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
            QTableWidget *table = (t.device == "Main") ? tblSimMain : tblSimAGV;
            QString fmt = QStringLiteral("Unsigned");
            int foundRow = -1;
            if (table) {
                if (t.cacheValid && t.cachedRow >= 0 && t.cachedRow < table->rowCount()) {
                    foundRow = t.cachedRow;
                    fmt = t.cachedFmt;
                } else {
                    foundRow = findSimRowByAddress(table, t.addr);
                    if (foundRow >= 0) {
                        fmt = simTableFormats.value(table).value(foundRow, QStringLiteral("Unsigned"));
                    }
                    t.cachedRow = foundRow;
                    t.cachedFmt = fmt;
                    t.cacheValid = true;
                }
            }

            if (fmt == "32-bit Float") {
                writeFloat32ToSlave(target, t.addr, static_cast<float>(val));
            } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
                uint32_t val32 = (fmt == "32-bit Signed") ? (uint32_t)(int32_t)val : (uint32_t)val;
                target->setRegisters(t.addr, {
                    static_cast<quint16>(val32 >> 16),
                    static_cast<quint16>(val32 & 0xFFFF)
                });
            } else if (fmt == "64-bit Float") {
                writeFloat64ToSlave(target, t.addr, static_cast<double>(val));
            } else if (fmt == "String") {
                // String 格式不支持数值波形写入
            } else {
                quint16 regVal = static_cast<quint16>(qBound(0.0, val, 65535.0));
                target->setRegister(t.addr, regVal);
            }

            // UI refresh ~5Hz (every 2 ticks); register values still update at 10Hz
            if (table && foundRow >= 0 && (t.currentTicks % 2) == 0) {
                refreshSimTableForAddr(table, t.addr);
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
                quint16 w0, w1;
                stream >> w0 >> w1;
                const float f = decodeFloat32Words(w0, w1);
                regs << QString::number(f, 'f', 4);
            }
        }
        else if (displayFormat == FormatDouble && byteCount >= 8) {
            for (int i = 0; i < byteCount / 8; ++i) {
                quint16 w0, w1, w2, w3;
                stream >> w0 >> w1 >> w2 >> w3;
                const double d = decodeFloat64Words(w0, w1, w2, w3);
                regs << QString::number(d, 'g', 8);
            }
        }
        else if (displayFormat == FormatString && byteCount >= 2) {
            QVector<quint16> stringRegs;
            for (int i = 0; i < byteCount / 2; ++i) {
                quint16 val;
                stream >> val;
                stringRegs << val;
            }
            resultStr = decodeUtf8FromRegisters(stringRegs);
        }
        else {
            for (int i=0; i<byteCount/2; ++i) {
                quint16 val;
                stream >> val;
                regs << formatValue(val);
            }
            resultStr = regs.join(", ");
        }
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
    } else if (format == FormatString) {
        const QString stringText = txtWriteValues->text().trimmed();
        const QVector<quint16> encoded = encodeUtf8ToRegisters(stringText, 1);
        val = encoded.isEmpty() ? 0 : encoded.first();
    } else if (format == FormatFloat || format == FormatDouble) {
        if (format == FormatFloat) {
            const QPair<quint16, quint16> words = encodeFloat32Words(txt.toFloat());
            val = words.first;
        } else {
            quint16 w0 = 0, w1 = 0, w2 = 0, w3 = 0;
            encodeFloat64Words(txt.toDouble(), w0, w1, w2, w3);
            val = w0;
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
             const QPair<quint16, quint16> words = encodeFloat32Words(s.toFloat());
             vals << words.first << words.second;
         }
     } else if (format == FormatDouble) {
         for (const QString &s : items) {
             quint16 w0 = 0, w1 = 0, w2 = 0, w3 = 0;
             encodeFloat64Words(s.toDouble(), w0, w1, w2, w3);
             vals << w0 << w1 << w2 << w3;
         }
     } else if (format == FormatString) {
         const int regCount = spinWriteQuantity->value();
         vals = encodeUtf8ToRegisters(txtWriteValues->text(), regCount);
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
    } else if (displayFormat == FormatString) {
        if (spinReadQuantity->value() < 2) {
            spinReadQuantity->setValue(kDefaultStringRegisterCount);
        }
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
    if (displayFormat == FormatString) return decodeUtf8FromRegisters(QVector<quint16>() << value);
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
    if (!ok || addr < 0 || addr > ModbusSlave::MaxHoldingRegisterAddress) {
        QMessageBox::warning(this, "错误", QString("地址需在 0~%1 之间").arg(ModbusSlave::MaxHoldingRegisterAddress));
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
        QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
        QTableWidgetItem *valItem = table->item(row, SimRegisterCol::Value);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;
        if (!valItem || valItem->text().trimmed().isEmpty()) {
            skipped++;
            continue;
        }

        bool okAddr = false;
        bool okVal = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        uint val = valItem->text().trimmed().toUInt(&okVal, 0);

        if (!okAddr || addr > ModbusSlave::MaxHoldingRegisterAddress || !okVal || val > 65535) {
            skipped++;
            continue;
        }

        if (target->setRegister(static_cast<quint16>(addr), static_cast<quint16>(val))) {
            written++;
            for (int r = 0; r < table->rowCount(); ++r) {
                QTableWidgetItem *a = table->item(r, SimRegisterCol::Address);
                if (a && a->text().trimmed().toUInt() == addr) {
                    refreshSimRowDisplay(table, r);
                }
            }
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
        QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;

        bool okAddr = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        if (!okAddr || addr > ModbusSlave::MaxHoldingRegisterAddress) continue;

        if (!table->item(row, SimRegisterCol::Value))
            table->setItem(row, SimRegisterCol::Value, new QTableWidgetItem());
        quint16 randomValue = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
        table->item(row, SimRegisterCol::Value)->setText(QString::number(randomValue));
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
        QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
        if (!addrItem || addrItem->text().trimmed().isEmpty()) continue;

        bool okAddr = false;
        uint addr = addrItem->text().trimmed().toUInt(&okAddr, 10);
        if (!okAddr || addr > ModbusSlave::MaxHoldingRegisterAddress) { skipped++; continue; }

        quint16 randomValue = static_cast<quint16>(QRandomGenerator::global()->bounded(65536));
        if (!table->item(row, SimRegisterCol::Value))
            table->setItem(row, SimRegisterCol::Value, new QTableWidgetItem());
        table->item(row, SimRegisterCol::Value)->setText(QString::number(randomValue));

        if (target->setRegister(static_cast<quint16>(addr), randomValue)) {
            written++;
            for (int r = 0; r < table->rowCount(); ++r) {
                QTableWidgetItem *a = table->item(r, SimRegisterCol::Address);
                if (a && a->text().trimmed().toUInt() == addr) {
                    refreshSimRowDisplay(table, r);
                }
            }
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
            QTableWidgetItem *addrItem = table->item(i, SimRegisterCol::Address);
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
    out << "Device,Direction,Address,Value,Format,Description\n";

    auto exportTable = [&](QTableWidget *table, const QString &deviceName) {
        if (!table) return;
        for (int i = 0; i < table->rowCount(); ++i) {
            QString direction = table->item(i, SimRegisterCol::Direction)
                                   ? table->item(i, SimRegisterCol::Direction)->text() : "";
            QString addr = table->item(i, SimRegisterCol::Address)
                               ? table->item(i, SimRegisterCol::Address)->text() : "";
            QString val = table->item(i, SimRegisterCol::Value)
                              ? table->item(i, SimRegisterCol::Value)->text() : "";
            QString fmt = simTableFormats.value(table).value(i, "Unsigned");
            QString desc = table->item(i, SimRegisterCol::Description)
                               ? table->item(i, SimRegisterCol::Description)->text() : "";

            if (direction.isEmpty() && addr.isEmpty() && val.isEmpty() && desc.isEmpty()) continue;

            out << deviceName << ","
                << escapeRegisterMapCsvField(direction) << ","
                << escapeRegisterMapCsvField(addr) << ","
                << escapeRegisterMapCsvField(val) << ","
                << escapeRegisterMapCsvField(fmt) << ","
                << escapeRegisterMapCsvField(desc) << "\n";
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
    const QString headerLine = in.readLine();
    const QStringList headerParts = parseRegisterMapCsvLine(headerLine);

    int deviceCol = 0;
    int dirCol = -1;
    int addrCol = 1;
    int valCol = 2;
    int fmtCol = 3;
    int descCol = 4;
    bool hasHeader = false;

    for (int i = 0; i < headerParts.size(); ++i) {
        const QString h = headerParts[i].trimmed().toLower();
        if (h == QStringLiteral("device") || h == QStringLiteral("设备")) {
            deviceCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("direction") || h == QStringLiteral("方向")) {
            dirCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("address") || h == QStringLiteral("地址")) {
            addrCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("value") || h == QStringLiteral("值")) {
            valCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("format") || h == QStringLiteral("格式")) {
            fmtCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("description") || h == QStringLiteral("描述") || h == QStringLiteral("comment")) {
            descCol = i;
            hasHeader = true;
        }
    }

    if (!hasHeader) {
        dirCol = -1;
        deviceCol = 0;
        addrCol = 1;
        valCol = 2;
        fmtCol = 3;
        descCol = 4;
    }

    int count = 0;

    auto importLine = [&](const QString &line) {
        if (line.trimmed().isEmpty()) return;
        const QStringList parts = parseRegisterMapCsvLine(line);
        if (parts.size() < 3) return;

        const QString deviceStr = parts.value(deviceCol).trimmed();
        const QString direction = dirCol >= 0 ? parts.value(dirCol).trimmed() : QString();
        const quint16 addr = parts.value(addrCol).trimmed().toUInt();
        const QString valStr = parts.value(valCol).trimmed();
        const QString fmt = parts.value(fmtCol).trimmed().isEmpty()
                                ? QStringLiteral("Unsigned")
                                : parts.value(fmtCol).trimmed();
        const QString desc = parts.value(descCol).trimmed();

        QTableWidget *table = (deviceStr.toLower() == "main" || deviceStr == QStringLiteral("主设备"))
                                  ? tblSimMain : tblSimAGV;
        ModbusSlave *slave = (deviceStr.toLower() == "main" || deviceStr == QStringLiteral("主设备"))
                                 ? simMainDevice : simAGVDevice;

        if (!table || !slave) return;

        int row = -1;
        for (int r = 0; r < table->rowCount(); ++r) {
            QTableWidgetItem *addrItem = table->item(r, SimRegisterCol::Address);
            QTableWidgetItem *dirItem = table->item(r, SimRegisterCol::Direction);
            if (!addrItem || addrItem->text().toUInt() != addr) continue;
            if (dirCol >= 0) {
                const QString existingDir = dirItem ? dirItem->text().trimmed() : QString();
                if (existingDir != direction) continue;
            }
            row = r;
            break;
        }

        if (row == -1) {
            row = table->rowCount();
            table->insertRow(row);
            for (int col = 0; col < SimRegisterCol::ColumnCount; ++col) {
                table->setItem(row, col, new QTableWidgetItem());
            }
            table->item(row, SimRegisterCol::Address)->setText(QString::number(addr));
        }

        if (!table->item(row, SimRegisterCol::Direction))
            table->setItem(row, SimRegisterCol::Direction, new QTableWidgetItem());
        if (!table->item(row, SimRegisterCol::Description))
            table->setItem(row, SimRegisterCol::Description, new QTableWidgetItem());
        if (!table->item(row, SimRegisterCol::Value))
            table->setItem(row, SimRegisterCol::Value, new QTableWidgetItem());

        table->item(row, SimRegisterCol::Direction)->setText(direction);
        table->item(row, SimRegisterCol::Description)->setText(desc);
        table->item(row, SimRegisterCol::Value)->setText(valStr);
        applyRegisterMapRowStyle(table, row);

        simTableFormats[table][row] = fmt;

        bool ok = false;
        if (fmt == "32-bit Float") {
            float fv = valStr.toFloat(&ok);
            if (ok) {
                writeFloat32ToSlave(slave, addr, fv);
            }
        } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
            uint32_t v32 = valStr.toUInt(&ok);
            slave->setRegister(addr, (quint16)(v32 >> 16));
            slave->setRegister(addr + 1, (quint16)(v32 & 0xFFFF));
        } else if (fmt == "64-bit Float") {
            double d = valStr.toDouble(&ok);
            if (ok) {
                writeFloat64ToSlave(slave, addr, d);
            }
        } else if (fmt == "String") {
            const int regCount = simTableStringLengths.value(table).value(row, kDefaultStringRegisterCount);
            const QVector<quint16> encoded = encodeUtf8ToRegisters(valStr, regCount);
            for (int i = 0; i < encoded.size(); ++i) {
                slave->setRegister(static_cast<quint16>(addr + i), encoded[i]);
            }
            ok = true;
        } else {
            slave->setRegister(addr, (quint16)valStr.toUInt(&ok));
        }

        refreshSimRowDisplay(table, row);
        count++;
    };

    if (!hasHeader) {
        importLine(headerLine);
    }
    while (!in.atEnd()) {
        importLine(in.readLine());
    }

    f.close();
    syncSimulatorTablesFromMaps();
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
                    if (isFloat) { writeFloat32ToSlave(dev, addr, valueToken.toFloat()); acted = true; }
                    else { quint16 v = (quint16)QString(valueToken).toUShort(); dev->setRegister(addr, v); acted = true; }
                } else {
                    // try main first, then agv
                    if (isFloat) {
                        float fv = valueToken.toFloat();
                        if (writeFloat32ToSlave(simMainDevice, addr, fv)) acted = true;
                        else if (writeFloat32ToSlave(simAGVDevice, addr, fv)) acted = true;
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

void MainWindow::refreshSimTableForAddr(QTableWidget *table, quint16 addr)
{
    if (!table) {
        return;
    }
    const QVector<int> rows = simAddrTouchRows.value(table).value(addr);
    if (rows.isEmpty()) {
        const int row = simAddrToRow.value(table).value(addr, -1);
        if (row >= 0) {
            refreshSimRowDisplay(table, row);
        }
        return;
    }
    for (int row : rows) {
        const QString fmt = simTableFormats.value(table).value(row, QStringLiteral("Unsigned"));
        const int stringRegCount = simTableStringLengths.value(table).value(row, kDefaultStringRegisterCount);
        if (simFormatWordCount(fmt, stringRegCount) > 1) {
            refreshSimMultiWordRows(table, row);
        } else {
            refreshSimRowDisplay(table, row);
        }
    }
}

void MainWindow::flushPendingSimWriteRefresh()
{
    const QHash<QTableWidget *, QSet<quint16>> pending = simPendingWriteAddrs;
    simPendingWriteAddrs.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
        QTableWidget *table = it.key();
        if (!table) {
            continue;
        }
        for (quint16 addr : it.value()) {
            refreshSimTableForAddr(table, addr);
        }
    }
}

void MainWindow::handleRegisterOps(ModbusSlave *senderDevice,
                                   const QVector<QPair<quint16, quint16>> &ops,
                                   const QString &opType)
{
    if (ops.isEmpty()) {
        return;
    }

    const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString normalizedOpType = opType.trimmed().toLower();
    const bool isRead = normalizedOpType == QLatin1String("read");
    const bool isWrite = (normalizedOpType == QLatin1String("write")
                          || normalizedOpType == QLatin1String("write_bit"));

    QString deviceName = QStringLiteral("unknown");
    QTableWidget *table = nullptr;
    if (senderDevice == simMainDevice) {
        deviceName = QStringLiteral("主设备");
        table = tblSimMain;
    } else if (senderDevice == simAGVDevice) {
        deviceName = QStringLiteral("AGV");
        table = tblSimAGV;
    }

    // History: one summary entry for batches, single entry for one op
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("timestamp"), timeStr);
        entry.insert(QStringLiteral("device"), deviceName);
        entry.insert(QStringLiteral("operation"), opType);
        if (ops.size() == 1) {
            entry.insert(QStringLiteral("address"), static_cast<int>(ops.first().first));
            entry.insert(QStringLiteral("value"), static_cast<int>(ops.first().second));
        } else {
            entry.insert(QStringLiteral("address"), static_cast<int>(ops.first().first));
            entry.insert(QStringLiteral("value"), static_cast<int>(ops.first().second));
            entry.insert(QStringLiteral("qty"), ops.size());
            entry.insert(QStringLiteral("endAddress"), static_cast<int>(ops.last().first));
            entry.insert(QStringLiteral("endValue"), static_cast<int>(ops.last().second));
        }
        registerHistory.append(entry);
        while (registerHistory.size() > 5000) {
            registerHistory.removeFirst();
        }
    }

    // Writes: coalesce UI refresh; reads: do not refresh table (values unchanged)
    if (table && isWrite) {
        for (const auto &op : ops) {
            simPendingWriteAddrs[table].insert(op.first);
        }
        simWriteRefreshTimer->start();
    }

    if (!txtSimLog) {
        return;
    }

    if (isRead) {
        QMap<quint16, quint16> &deviceReadValues = simLastReadValues[deviceName];
        int changed = 0;
        for (const auto &op : ops) {
            if (!deviceReadValues.contains(op.first) || deviceReadValues.value(op.first) != op.second) {
                ++changed;
            }
            deviceReadValues.insert(op.first, op.second);
        }
        if (changed == 0) {
            return;
        }
        if (ops.size() == 1) {
            txtSimLog->append(QStringLiteral("[%1] 指令: [%2] 读取 地址[%3] -> %4")
                                  .arg(timeStr, deviceName)
                                  .arg(ops.first().first)
                                  .arg(ops.first().second));
        } else {
            txtSimLog->append(QStringLiteral("[%1] 指令: [%2] 读取 地址[%3..%4] qty=%5 (变更%6)")
                                  .arg(timeStr, deviceName)
                                  .arg(ops.first().first)
                                  .arg(ops.last().first)
                                  .arg(ops.size())
                                  .arg(changed));
        }
        return;
    }

    if (isWrite) {
        if (ops.size() == 1) {
            txtSimLog->append(QStringLiteral("[%1] 指令: [%2] 写入 地址[%3] <- %4")
                                  .arg(timeStr, deviceName)
                                  .arg(ops.first().first)
                                  .arg(ops.first().second));
        } else {
            txtSimLog->append(QStringLiteral("[%1] 指令: [%2] 写入 地址[%3..%4] qty=%5")
                                  .arg(timeStr, deviceName)
                                  .arg(ops.first().first)
                                  .arg(ops.last().first)
                                  .arg(ops.size()));
        }
    }
}

void MainWindow::onRegisterOperation(quint16 addr, quint16 value, const QString &opType)
{
    ModbusSlave *senderDevice = qobject_cast<ModbusSlave *>(sender());
    handleRegisterOps(senderDevice, {{addr, value}}, opType);
}

void MainWindow::onRegistersChanged(const QVector<QPair<quint16, quint16>> &ops, const QString &opType)
{
    ModbusSlave *senderDevice = qobject_cast<ModbusSlave *>(sender());
    handleRegisterOps(senderDevice, ops, opType);
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
        saveGitHistory(dir);
        runGitCommand(QStringList() << "status"); // Check status which will verify it's a git repo
        onGitRefreshLogClicked();
    }
}

bool MainWindow::runGitCommand(const QStringList &args) {
    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    if (workDir.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: %1</font>")
                              .arg(pathError.isEmpty() ? QStringLiteral("请先选择Git仓库目录!") : pathError));
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(workDir);

    process.setProgram(PlatformPrefs::gitBinary());
    process.setArguments(args);
    
    txtGitLog->append(QString("<font color='cyan'>$ git %1</font>").arg(args.join(" ")));
    
    process.start();
    if (!process.waitForStarted()) {
         txtGitLog->append("<font color='red'>错误: 无法启动git命令，请检查是否安装了git</font>");
         return false;
    }
    
    if (!finishGitProcess(process, 30000)) {
         txtGitLog->append("<font color='red'>部分超时或后台运行...</font>");
         return false;
    }
    
    QByteArray stdoutData = process.readAllStandardOutput();
    QByteArray stderrData = process.readAllStandardError();
    
    if (!stdoutData.isEmpty()) {
        txtGitLog->append(PlatformPrefs::decodeProcessOutput(stdoutData));
    }
    if (!stderrData.isEmpty()) {
        txtGitLog->append(QString("<font color='orange'>%1</font>").arg(PlatformPrefs::decodeProcessOutput(stderrData)));
    }
    
    txtGitLog->moveCursor(QTextCursor::End);
    const bool ok = process.exitCode() == 0;
    if (ok && !args.isEmpty()) {
        const QString sub = args.first().toLower();
        if (sub == QLatin1String("commit")) {
            maybeAutoPushAfterCommit();
        }
        static const QSet<QString> kRefreshPendingSubs = {
            QStringLiteral("commit"), QStringLiteral("add"),     QStringLiteral("status"),
            QStringLiteral("stash"),  QStringLiteral("reset"),   QStringLiteral("checkout"),
            QStringLiteral("switch"), QStringLiteral("merge"),   QStringLiteral("rebase"),
            QStringLiteral("restore"), QStringLiteral("rm"),     QStringLiteral("mv"),
            QStringLiteral("clean"),  QStringLiteral("cherry-pick")};
        if (kRefreshPendingSubs.contains(sub)) {
            refreshGitPendingStatusBar();
        }
    }
    return ok;
}

QString MainWindow::currentGitWorkDir(QString *errorOut) const
{
    const QString raw = cmbGitDir ? cmbGitDir->currentText().trimmed() : QString();
    if (raw.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("请先选择Git仓库目录!");
        }
        return {};
    }

    const QFileInfo fi(raw);
    const QString absPath = QDir::cleanPath(fi.absoluteFilePath());
    if (!QDir(absPath).exists()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Git仓库目录不存在: %1").arg(absPath);
        }
        return {};
    }
    return absPath;
}

void MainWindow::updateGitConsoleCwdLabel()
{
    if (!lblGitConsoleCwd) {
        return;
    }
    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    if (workDir.isEmpty()) {
        lblGitConsoleCwd->setText(QStringLiteral("cwd: （未选择仓库，命令将无法执行）"));
        lblGitConsoleCwd->setStyleSheet(
            QStringLiteral("color: #ce9178; font-family: Monospace; font-size: 11px; background: #1e1e1e; "
                           "padding: 2px 4px;"));
        return;
    }
    lblGitConsoleCwd->setText(QStringLiteral("cwd: %1").arg(QDir::toNativeSeparators(workDir)));
    lblGitConsoleCwd->setStyleSheet(
        QStringLiteral("color: #858585; font-family: Monospace; font-size: 11px; background: #1e1e1e; "
                       "padding: 2px 4px;"));
}

QStringList MainWindow::parseGitConsoleCommand(const QString &rawLine, QString *errorOut) const
{
    QString line = rawLine.trimmed();
    if (line.isEmpty()) {
        return {};
    }

    if (line.startsWith(QLatin1String("git "), Qt::CaseInsensitive)) {
        line = line.mid(4).trimmed();
    } else if (line.compare(QLatin1String("git"), Qt::CaseInsensitive) == 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("请补全子命令，例如: status / git log -5");
        }
        return {};
    }

    if (line.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("空命令");
        }
        return {};
    }

    const QStringList args = QProcess::splitCommand(line);
    if (args.isEmpty() && errorOut) {
        *errorOut = QStringLiteral("无法解析命令");
    }
    return args;
}

void MainWindow::onGitConsoleCommandSubmitted()
{
    if (!txtGitCmdInput || !txtGitLog) {
        return;
    }

    const QString raw = txtGitCmdInput->text().trimmed();
    if (raw.isEmpty()) {
        return;
    }

    if (gitConsoleHistory.isEmpty() || gitConsoleHistory.constLast() != raw) {
        gitConsoleHistory.append(raw);
    }
    gitConsoleHistoryIndex = gitConsoleHistory.size();
    txtGitCmdInput->clear();

    if (raw.compare(QLatin1String("clear"), Qt::CaseInsensitive) == 0
        || raw.compare(QLatin1String("cls"), Qt::CaseInsensitive) == 0) {
        txtGitLog->clear();
        return;
    }

    // Console commands always run under the currently selected Git repo path.
    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    if (workDir.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: %1</font>")
                              .arg(pathError.isEmpty() ? QStringLiteral("请先选择Git仓库目录!") : pathError));
        txtGitLog->moveCursor(QTextCursor::End);
        return;
    }

    QString parseError;
    const QStringList args = parseGitConsoleCommand(raw, &parseError);
    if (args.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='orange'>%1</font>")
                              .arg(parseError.isEmpty() ? QStringLiteral("无效命令") : parseError));
        txtGitLog->moveCursor(QTextCursor::End);
        return;
    }

    txtGitLog->append(QStringLiteral("<font color='gray'>cwd: %1</font>")
                          .arg(QDir::toNativeSeparators(workDir).toHtmlEscaped()));

    const QString sub = args.first().toLower();
    const bool isNetwork = (sub == QLatin1String("push") || sub == QLatin1String("pull")
                            || sub == QLatin1String("fetch") || sub == QLatin1String("clone")
                            || sub == QLatin1String("ls-remote"));

    if (isNetwork) {
        const int timeoutMs = (sub == QLatin1String("clone")) ? 120000 : 60000;
        runGitNetworkCommand(args, timeoutMs, [this, sub](bool) {
            if (sub == QLatin1String("fetch") || sub == QLatin1String("pull")
                || sub == QLatin1String("push")) {
                refreshGitBranchesLocal();
            }
        });
        return;
    }

    runGitCommand(args);

    if (sub == QLatin1String("checkout") || sub == QLatin1String("switch")
        || sub == QLatin1String("branch") || sub == QLatin1String("commit")
        || sub == QLatin1String("merge") || sub == QLatin1String("rebase")
        || sub == QLatin1String("stash") || sub == QLatin1String("reset")) {
        refreshGitBranchesLocal();
        onGitRefreshLogClicked();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == lblGitPendingStatus && event
        && event->type() == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const QList<QPair<QString, QString>> items = collectGitPendingExitItems();
            if (!items.isEmpty()) {
                focusGitPendingRepo(items.first().first);
            } else if (navWidget) {
                constexpr int kGitPageIndex = 2;
                navWidget->setCurrentRow(kGitPageIndex);
            }
            return true;
        }
    }
    if (watched == txtGitCmdInput && event && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Up) {
            if (!gitConsoleHistory.isEmpty()) {
                if (gitConsoleHistoryIndex < 0 || gitConsoleHistoryIndex > gitConsoleHistory.size()) {
                    gitConsoleHistoryIndex = gitConsoleHistory.size();
                }
                if (gitConsoleHistoryIndex > 0) {
                    --gitConsoleHistoryIndex;
                }
                txtGitCmdInput->setText(gitConsoleHistory.value(gitConsoleHistoryIndex));
                txtGitCmdInput->setCursorPosition(txtGitCmdInput->text().size());
            }
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            if (!gitConsoleHistory.isEmpty()) {
                if (gitConsoleHistoryIndex < gitConsoleHistory.size() - 1) {
                    ++gitConsoleHistoryIndex;
                    txtGitCmdInput->setText(gitConsoleHistory.value(gitConsoleHistoryIndex));
                    txtGitCmdInput->setCursorPosition(txtGitCmdInput->text().size());
                } else {
                    gitConsoleHistoryIndex = gitConsoleHistory.size();
                    txtGitCmdInput->clear();
                }
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::isGitAutoFetchEnabled() const
{
    return chkGitAutoFetch && chkGitAutoFetch->isChecked();
}

bool MainWindow::isGitAutoPushAfterCommitEnabled() const
{
    return chkGitAutoPushAfterCommit && chkGitAutoPushAfterCommit->isChecked();
}

void MainWindow::loadGitNetworkSettings()
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("gitNetwork"));
    gitAutoFetchEnabled = settings.value(QStringLiteral("autoFetch"), false).toBool();
    gitAutoPushAfterCommitEnabled =
        settings.value(QStringLiteral("autoPushAfterCommit"), true).toBool();
    settings.endGroup();

    if (chkGitAutoFetch) {
        chkGitAutoFetch->blockSignals(true);
        chkGitAutoFetch->setChecked(gitAutoFetchEnabled);
        chkGitAutoFetch->blockSignals(false);
    }
    if (chkGitAutoPushAfterCommit) {
        chkGitAutoPushAfterCommit->blockSignals(true);
        chkGitAutoPushAfterCommit->setChecked(gitAutoPushAfterCommitEnabled);
        chkGitAutoPushAfterCommit->blockSignals(false);
    }
}

void MainWindow::saveGitNetworkSettings()
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("gitNetwork"));
    settings.setValue(QStringLiteral("autoFetch"), gitAutoFetchEnabled);
    settings.setValue(QStringLiteral("autoPushAfterCommit"), gitAutoPushAfterCommitEnabled);
    settings.endGroup();
}

void MainWindow::onGitAutoFetchToggled(bool checked)
{
    gitAutoFetchEnabled = checked;
    saveGitNetworkSettings();
    txtGitLog->append(checked
                          ? QStringLiteral("<font color='gray'>[Git] 已开启：换仓库时自动 fetch。</font>")
                          : QStringLiteral("<font color='gray'>[Git] 已关闭自动 fetch（推荐代理异常时使用）。</font>"));
}

void MainWindow::onGitAutoPushAfterCommitToggled(bool checked)
{
    gitAutoPushAfterCommitEnabled = checked;
    saveGitNetworkSettings();
    txtGitLog->append(checked
                          ? QStringLiteral("<font color='gray'>[Git] 已开启：提交后自动推送。</font>")
                          : QStringLiteral("<font color='gray'>[Git] 已关闭提交后自动推送。</font>"));
}

void MainWindow::onGitPendingStatusBarTick()
{
    refreshGitPendingStatusBar();
}

void MainWindow::refreshGitPendingStatusBar()
{
    if (!lblGitPendingStatus) {
        return;
    }

    const QString current = currentGitWorkDir();
    QString currentName;
    bool currentDirty = false;
    int currentUnpushed = 0;
    if (!current.isEmpty() && isGitRepository(current)) {
        currentName = gitRepoDisplayName(current);
        if (currentName.isEmpty()) {
            currentName = QFileInfo(current).fileName();
        }
        currentDirty = GitWorktreeRunner::isDirty(current);
        currentUnpushed = gitUnpushedCommitCount(current);
    }

    const QList<QPair<QString, QString>> pending = collectGitPendingExitItems();
    int otherPending = 0;
    for (const auto &item : pending) {
        if (QDir(item.first).absolutePath() != QDir(current).absolutePath()) {
            ++otherPending;
        }
    }

    QStringList tipLines;
    tipLines.reserve(pending.size() + 1);
    tipLines << QStringLiteral("点击跳转到首个待处理仓库");
    for (const auto &item : pending) {
        tipLines << item.second;
    }
    lblGitPendingStatus->setToolTip(tipLines.join(QLatin1Char('\n')));

    if (currentName.isEmpty()) {
        if (pending.isEmpty()) {
            lblGitPendingStatus->setText(QStringLiteral("Git: —"));
            lblGitPendingStatus->setStyleSheet(QStringLiteral("color: #6b7785;"));
        } else {
            lblGitPendingStatus->setText(
                QStringLiteral("Git: %1 仓待处理").arg(pending.size()));
            lblGitPendingStatus->setStyleSheet(
                QStringLiteral("color: #c0392b; font-weight: 600;"));
        }
        return;
    }

    QStringList parts;
    if (currentDirty) {
        parts << QStringLiteral("未提交");
    }
    if (currentUnpushed > 0) {
        parts << QStringLiteral("未推送 %1").arg(currentUnpushed);
    }

    QString text;
    if (parts.isEmpty()) {
        text = QStringLiteral("Git: %1 · 干净").arg(currentName);
        lblGitPendingStatus->setStyleSheet(QStringLiteral("color: #1e8449;"));
    } else {
        text = QStringLiteral("Git: %1 · %2").arg(currentName, parts.join(QStringLiteral(" · ")));
        lblGitPendingStatus->setStyleSheet(
            QStringLiteral("color: #c0392b; font-weight: 600;"));
    }
    if (otherPending > 0) {
        text += QStringLiteral(" ｜ 另有 %1 仓").arg(otherPending);
    }
    lblGitPendingStatus->setText(text);
}

void MainWindow::maybeAutoPushAfterCommit()
{
    if (!isGitAutoPushAfterCommitEnabled()) {
        return;
    }

    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    if (workDir.isEmpty()) {
        return;
    }

    QString upstream;
    const bool hasUpstream =
        GitWorktreeRunner::runInRepo(
            workDir,
            {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("@{u}")},
            &upstream, nullptr, 8000)
        && !upstream.trimmed().isEmpty();
    // 已有上游且没有领先提交时跳过；无上游时仍走 push -u 建立跟踪
    if (hasUpstream && gitUnpushedCommitCount(workDir) <= 0) {
        txtGitLog->append(
            QStringLiteral("<font color='gray'>[自动推送] 无未推送提交，跳过。</font>"));
        return;
    }

    if (gitNetworkBusy) {
        txtGitLog->append(
            QStringLiteral("<font color='orange'>[自动推送] 当前有远程通讯进行中，请稍后手动 push。</font>"));
        return;
    }

    const QString branch = gitCheckedOutBranch(workDir);
    if (branch.isEmpty()) {
        txtGitLog->append(
            QStringLiteral("<font color='orange'>[自动推送] 无法识别当前分支，已跳过。</font>"));
        return;
    }

    QString remote = cmbGitRemote ? cmbGitRemote->currentText().trimmed() : QString();
    if (remote.isEmpty()) {
        remote = QStringLiteral("origin");
    }

    txtGitLog->append(
        QStringLiteral("<font color='cyan'>[自动推送] commit 成功，正在 push %1/%2 …</font>")
            .arg(remote, branch));
    runGitNetworkCommand(
        QStringList() << QStringLiteral("push") << QStringLiteral("-u") << remote << branch, 60000,
        [this](bool ok) {
            if (ok) {
                txtGitLog->append(QStringLiteral("<font color='green'>[自动推送] 推送成功。</font>"));
            } else {
                txtGitLog->append(
                    QStringLiteral("<font color='red'>[自动推送] 推送失败，请检查网络或手动 push。</font>"));
            }
            refreshGitPendingStatusBar();
        });
}

void MainWindow::setGitNetworkBusy(bool busy, const QString &statusText)
{
    gitNetworkBusy = busy;

    if (lblGitNetworkStatus) {
        lblGitNetworkStatus->setText(statusText);
        lblGitNetworkStatus->setVisible(busy && !statusText.isEmpty());
    }
    if (barGitNetworkBusy) {
        barGitNetworkBusy->setVisible(busy);
    }
    if (btnGitCancelNetwork) {
        btnGitCancelNetwork->setEnabled(busy);
    }

    const bool enableNetBtns = !busy;
    if (btnGitFetch) btnGitFetch->setEnabled(enableNetBtns);
    if (btnGitPush) btnGitPush->setEnabled(enableNetBtns);
    if (btnGitPull) btnGitPull->setEnabled(enableNetBtns);
}

void MainWindow::onGitCancelNetworkClicked()
{
    if (!gitNetworkBusy) {
        return;
    }

    gitNetworkUserCancelled = true;
    if (gitNetworkTimeout) {
        gitNetworkTimeout->stop();
    }
    if (gitNetworkProcess && gitNetworkProcess->state() != QProcess::NotRunning) {
        gitNetworkProcess->kill();
    }
    txtGitLog->append(QStringLiteral("<font color='orange'>[Git] 已跳过 git 通讯。</font>"));
}

void MainWindow::finishGitNetworkCommand(bool ok, const QString &stdoutText, const QString &stderrText)
{
    if (!gitNetworkBusy) {
        return;
    }

    if (gitNetworkTimeout) {
        gitNetworkTimeout->stop();
    }

    if (!stdoutText.isEmpty()) {
        txtGitLog->append(stdoutText);
    }
    if (!stderrText.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='orange'>%1</font>").arg(stderrText));
    }
    txtGitLog->moveCursor(QTextCursor::End);

    const bool wasCancelled = gitNetworkUserCancelled;
    const bool wasTimedOut = gitNetworkTimedOut;
    const bool suppressRetry = gitNetworkSuppressRetry;
    auto done = gitNetworkDoneCallback;
    gitNetworkDoneCallback = {};
    gitNetworkUserCancelled = false;
    gitNetworkTimedOut = false;
    gitNetworkSuppressRetry = false;

    if (gitNetworkProcess) {
        gitNetworkProcess->disconnect(this);
        gitNetworkProcess->deleteLater();
        gitNetworkProcess = nullptr;
    }

    setGitNetworkBusy(false);

    if (done) {
        done(ok);
    }

    refreshGitPendingStatusBar();

    if (!ok && !wasCancelled && !suppressRetry) {
        const QString reason = wasTimedOut
                                   ? QStringLiteral("git 远程通讯超时（可能受系统代理/虚拟网卡影响）。")
                                   : QStringLiteral("git 远程通讯失败。");
        offerGitNetworkRetry(reason);
    }
}

void MainWindow::offerGitNetworkRetry(const QString &reason)
{
    if (gitNetworkLastArgs.isEmpty()) {
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Git 通讯"));
    box.setText(reason);
    box.setInformativeText(QStringLiteral("是否重试？也可跳过并继续使用本地仓库。"));
    QPushButton *retryBtn = box.addButton(QStringLiteral("重试"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = box.addButton(QStringLiteral("跳过"), QMessageBox::RejectRole);
    box.setDefaultButton(skipBtn);
    box.exec();

    if (box.clickedButton() == retryBtn) {
        const QStringList args = gitNetworkLastArgs;
        const int timeoutMs = gitNetworkLastTimeoutMs;
        const auto done = gitNetworkLastDoneCallback;
        runGitNetworkCommand(args, timeoutMs, done);
    } else {
        txtGitLog->append(QStringLiteral("<font color='gray'>[Git] 已跳过此次远程通讯。</font>"));
    }
}

void MainWindow::runGitNetworkCommand(const QStringList &args, int timeoutMs,
                                      const std::function<void(bool ok)> &done)
{
    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    if (workDir.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: %1</font>")
                              .arg(pathError.isEmpty() ? QStringLiteral("请先选择Git仓库目录!") : pathError));
        gitNetworkSuppressRetry = false;
        if (done) {
            done(false);
        }
        return;
    }

    if (gitNetworkBusy) {
        txtGitLog->append(QStringLiteral("<font color='orange'>[Git] 已有远程通讯进行中，请先等待或点「取消通讯」。</font>"));
        gitNetworkSuppressRetry = false;
        if (done) {
            done(false);
        }
        return;
    }

    gitNetworkLastArgs = args;
    gitNetworkLastTimeoutMs = timeoutMs;
    gitNetworkLastDoneCallback = done;
    gitNetworkDoneCallback = done;
    gitNetworkUserCancelled = false;
    gitNetworkTimedOut = false;

    if (!gitNetworkTimeout) {
        gitNetworkTimeout = new QTimer(this);
        gitNetworkTimeout->setSingleShot(true);
        connect(gitNetworkTimeout, &QTimer::timeout, this, [this]() {
            if (!gitNetworkBusy || !gitNetworkProcess) {
                return;
            }
            gitNetworkTimedOut = true;
            txtGitLog->append(QStringLiteral("<font color='red'>[Git] 远程通讯超时，正在中断…</font>"));
            if (gitNetworkProcess->state() != QProcess::NotRunning) {
                gitNetworkProcess->kill();
            }
        });
    }

    gitNetworkProcess = new QProcess(this);
    gitNetworkProcess->setWorkingDirectory(workDir);
    gitNetworkProcess->setProgram(PlatformPrefs::gitBinary());
    gitNetworkProcess->setArguments(args);

    txtGitLog->append(QStringLiteral("<font color='cyan'>$ git %1</font>").arg(args.join(QLatin1Char(' '))));
    setGitNetworkBusy(true, QStringLiteral("正在 git %1 …").arg(args.join(QLatin1Char(' '))));

    connect(gitNetworkProcess,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus) {
                const QString out = PlatformPrefs::decodeProcessOutput(gitNetworkProcess ? gitNetworkProcess->readAllStandardOutput()
                                                                       : QByteArray());
                const QString err = PlatformPrefs::decodeProcessOutput(gitNetworkProcess ? gitNetworkProcess->readAllStandardError()
                                                                       : QByteArray());
                const bool ok = !gitNetworkUserCancelled && !gitNetworkTimedOut && exitCode == 0;
                finishGitNetworkCommand(ok, out, err);
            });

    connect(gitNetworkProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            txtGitLog->append(QStringLiteral("<font color='red'>错误: 无法启动git命令，请检查是否安装了git</font>"));
            finishGitNetworkCommand(false, QString(), QString());
        }
    });

    gitNetworkTimeout->start(qMax(1000, timeoutMs));
    gitNetworkProcess->start();
}

bool MainWindow::gitHasUncommittedChanges(const QString &workDir) const {
    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(), QStringList() << QStringLiteral("status") << QStringLiteral("--porcelain"));

    if (!finishGitProcess(process, 15000) || process.exitCode() != 0) {
        const_cast<MainWindow *>(this)->txtGitLog->append(
            QStringLiteral("<font color='orange'>[传输前检查] git status --porcelain 执行失败，跳过 git add / commit。</font>"));
        return false;
    }

    const QString output = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput()).trimmed();
    return !output.isEmpty();
}

bool MainWindow::gitStageWithReview(const QString &workDir)
{
    if (workDir.trimmed().isEmpty() || !QDir(workDir).exists()) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: Git 仓库目录无效。</font>"));
        return false;
    }

    QString error;
    const QVector<GitStageEntry> entries = GitStageGuard::collectPending(workDir, &error);
    if (!error.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='red'>[暂存审查] %1</font>").arg(error));
        return false;
    }
    if (entries.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='gray'>[暂存审查] 没有可暂存的改动。</font>"));
        return true;
    }

    txtGitLog->append(QStringLiteral("<font color='gray'>[暂存审查] 发现 %1 个待处理路径。</font>")
                          .arg(entries.size()));

    GitStageReviewDialog dlg(workDir, entries, this);
    if (dlg.exec() != QDialog::Accepted) {
        txtGitLog->append(QStringLiteral("<font color='orange'>[暂存审查] 已取消。</font>"));
        return false;
    }

    bool ignoreFileUpdated = false;
    if (dlg.appendIgnoreRequested()) {
        const QStringList patterns = dlg.ignorePatternsToAppend();
        if (!patterns.isEmpty()) {
            QString ignoreErr;
            if (GitStageGuard::appendIgnorePatterns(workDir, patterns, &ignoreErr)) {
                ignoreFileUpdated = true;
                txtGitLog->append(QStringLiteral("<font color='green'>[暂存审查] 已追加忽略规则: %1</font>")
                                      .arg(patterns.join(QLatin1String(", "))));
            } else if (!ignoreErr.isEmpty()) {
                txtGitLog->append(QStringLiteral("<font color='red'>[暂存审查] 写入 .gitignore 失败: %1</font>")
                                      .arg(ignoreErr));
                return false;
            } else {
                txtGitLog->append(QStringLiteral("<font color='gray'>[暂存审查] 忽略规则已存在，无需追加。</font>"));
            }
        }
    }

    if (dlg.uncacheBlockedRequested()) {
        QSet<QString> toUncache;
        for (const QString &p : dlg.blockedTrackedPaths())
            toUncache.insert(p);
        // After ignore rules update, also drop any other tracked paths that are now ignored.
        for (const QString &p : GitStageGuard::trackedIgnoredPaths(workDir))
            toUncache.insert(p);

        if (!toUncache.isEmpty()) {
            QStringList paths = toUncache.values();
            paths.sort();
            QStringList args;
            args << QStringLiteral("rm") << QStringLiteral("-r") << QStringLiteral("--cached")
                 << QStringLiteral("--");
            args << paths;
            if (!runGitCommand(args)) {
                txtGitLog->append(QStringLiteral(
                    "<font color='orange'>[暂存审查] git rm --cached 未完全成功，请检查日志。</font>"));
            } else {
                txtGitLog->append(QStringLiteral("<font color='green'>[暂存审查] 已取消跟踪 %1 个路径。</font>")
                                      .arg(paths.size()));
            }
        } else {
            txtGitLog->append(QStringLiteral(
                "<font color='gray'>[暂存审查] 没有已跟踪且应忽略的路径需要取消跟踪"
                "（未跟踪文件只需被 .gitignore 忽略即可）。</font>"));
        }
    }

    QStringList selected = dlg.selectedPaths();
    if (ignoreFileUpdated && !selected.contains(QStringLiteral(".gitignore")))
        selected.prepend(QStringLiteral(".gitignore"));

    if (selected.isEmpty()) {
        txtGitLog->append(QStringLiteral(
            "<font color='gray'>[暂存审查] 未选择暂存路径（可能仅更新了索引）。</font>"));
        return true;
    }

    // 审查对话框已确认（含用户/AI 覆盖危险项）；用 -f 以便加入 .gitignore 匹配路径。
    QStringList forceNoted;
    for (const QString &path : selected) {
        if (path == QStringLiteral(".gitignore"))
            continue;
        QString reason;
        if (GitStageGuard::isBlockedPath(workDir, path, &reason)
            || GitStageGuard::isExtensionLessDangerous(path, nullptr))
            forceNoted << (reason.isEmpty() ? path : QStringLiteral("%1 (%2)").arg(path, reason));
    }
    if (!forceNoted.isEmpty()) {
        txtGitLog->append(QStringLiteral(
            "<font color='orange'>[暂存审查] 强制暂存危险/忽略路径：%1</font>")
                              .arg(forceNoted.join(QLatin1String(", "))));
    }

    QStringList args;
    args << QStringLiteral("add") << QStringLiteral("-f") << QStringLiteral("--");
    args << selected;
    if (!runGitCommand(args)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[暂存审查] git add -f 失败。</font>"));
        return false;
    }
    txtGitLog->append(QStringLiteral("<font color='green'>[暂存审查] 已暂存 %1 个路径。</font>")
                          .arg(selected.size()));
    return true;
}

bool MainWindow::gitStagedHasBlockedPaths(const QString &workDir, QStringList *blockedOut) const
{
    if (blockedOut)
        blockedOut->clear();
    if (workDir.trimmed().isEmpty())
        return false;

    // name-status -z: status\0path\0 (rename/copy: status\0old\0new\0).
    // Staged deletions (D) of ignored files are intentional untracking — allow them.
    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(),
                  {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-status"),
                   QStringLiteral("-z")});
    if (!finishGitProcess(process, 15000) || process.exitCode() != 0)
        return false;

    const QByteArray raw = process.readAllStandardOutput();
    QStringList pathsToCheck;
    int i = 0;
    auto nextField = [&]() -> QString {
        if (i >= raw.size())
            return QString();
        int next = raw.indexOf('\0', i);
        if (next < 0)
            next = raw.size();
        const QString field = QString::fromUtf8(raw.mid(i, next - i));
        i = next + 1;
        return field;
    };

    while (i < raw.size()) {
        const QString status = nextField();
        if (status.isEmpty())
            break;
        const QChar code = status.at(0);
        if (code == QLatin1Char('R') || code == QLatin1Char('C')) {
            nextField(); // old path
            const QString newPath = nextField().trimmed();
            if (!newPath.isEmpty())
                pathsToCheck << newPath;
            continue;
        }
        const QString path = nextField().trimmed();
        if (path.isEmpty())
            continue;
        if (code == QLatin1Char('D'))
            continue; // removing ignored paths from the index is OK
        pathsToCheck << path;
    }

    if (pathsToCheck.isEmpty())
        return false;

    QStringList blocked;
    for (const QString &path : pathsToCheck) {
        QString reason;
        if (GitStageGuard::isBlockedPath(workDir, path, &reason))
            blocked << path;
    }
    if (blocked.isEmpty())
        return false;
    if (blockedOut)
        *blockedOut = blocked;
    return true;
}

int MainWindow::gitUnpushedCommitCount(const QString &workDir) const
{
    if (workDir.trimmed().isEmpty() || !isGitRepository(workDir)) {
        return 0;
    }

    // 无上游跟踪分支时不算“未推送”
    QString upstream;
    if (!GitWorktreeRunner::runInRepo(
            workDir,
            {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("@{u}")},
            &upstream, nullptr, 8000)) {
        return 0;
    }
    if (upstream.trimmed().isEmpty()) {
        return 0;
    }

    QString out;
    if (!GitWorktreeRunner::runInRepo(
            workDir,
            {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("@{u}..HEAD")},
            &out, nullptr, 8000)) {
        return 0;
    }

    bool ok = false;
    const int count = out.trimmed().toInt(&ok);
    return (ok && count > 0) ? count : 0;
}

int MainWindow::gitBehindCommitCount(const QString &workDir) const
{
    if (workDir.trimmed().isEmpty() || !isGitRepository(workDir)) {
        return 0;
    }

    // 无上游跟踪分支时不算“远程领先”
    QString upstream;
    if (!GitWorktreeRunner::runInRepo(
            workDir,
            {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("@{u}")},
            &upstream, nullptr, 8000)) {
        return 0;
    }
    if (upstream.trimmed().isEmpty()) {
        return 0;
    }

    QString out;
    if (!GitWorktreeRunner::runInRepo(
            workDir,
            {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD..@{u}")},
            &out, nullptr, 8000)) {
        return 0;
    }

    bool ok = false;
    const int count = out.trimmed().toInt(&ok);
    return (ok && count > 0) ? count : 0;
}

QList<QPair<QString, QString>> MainWindow::collectGitPendingExitItems() const
{
    QList<QPair<QString, QString>> items;

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    QSet<QString> seen;

    for (const QString &rawPath : history) {
        const QString absPath = QDir(rawPath).absolutePath();
        if (absPath.isEmpty() || seen.contains(absPath)) {
            continue;
        }
        seen.insert(absPath);

        if (!QDir(absPath).exists() || !isGitRepository(absPath)) {
            continue;
        }

        const bool dirty = GitWorktreeRunner::isDirty(absPath);
        const int unpushed = gitUnpushedCommitCount(absPath);
        if (!dirty && unpushed <= 0) {
            continue;
        }

        QStringList parts;
        if (dirty) {
            parts << QStringLiteral("未提交改动");
        }
        if (unpushed > 0) {
            parts << QStringLiteral("未推送 %1 个提交").arg(unpushed);
        }

        QString name = gitRepoDisplayName(absPath);
        if (name.isEmpty()) {
            name = QFileInfo(absPath).fileName();
        }
        items.append({absPath, QStringLiteral("• %1：%2").arg(name, parts.join(QStringLiteral("；")))});
    }

    return items;
}

void MainWindow::focusGitPendingRepo(const QString &repoDir)
{
    const QString absPath = QDir(repoDir).absolutePath();
    if (absPath.isEmpty() || !QDir(absPath).exists()) {
        return;
    }

    // Git 工作流助手在导航列表中的固定下标
    constexpr int kGitPageIndex = 2;
    if (navWidget && navWidget->currentRow() != kGitPageIndex) {
        navWidget->setCurrentRow(kGitPageIndex);
    }

    enterGitRepoPath(absPath);
}

bool MainWindow::confirmExitDespiteGitPending()
{
    const QList<QPair<QString, QString>> items = collectGitPendingExitItems();
    if (items.isEmpty()) {
        return true;
    }

    QStringList warnings;
    warnings.reserve(items.size());
    QStringList unpushedDirs;
    for (const auto &item : items) {
        warnings << item.second;
        if (gitUnpushedCommitCount(item.first) > 0) {
            unpushedDirs << item.first;
        }
    }

    QString hint = QStringLiteral("（选择「去处理」将切换到第一个待处理仓库）");
    if (!unpushedDirs.isEmpty()) {
        hint = QStringLiteral("（「一键推送」将推送有未推送提交的仓库；「去处理」切换到第一个仓库）");
    }

    const QString message =
        QStringLiteral("以下仓库仍有未提交或未推送的内容：\n\n%1\n\n仍要退出程序吗？\n%2")
            .arg(warnings.join(QLatin1Char('\n')), hint);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("关闭前提醒"));
    box.setText(message);
    QPushButton *quitBtn = box.addButton(QStringLiteral("仍要退出"), QMessageBox::AcceptRole);
    QPushButton *pushAllBtn = nullptr;
    if (!unpushedDirs.isEmpty()) {
        pushAllBtn = box.addButton(QStringLiteral("一键推送"), QMessageBox::ActionRole);
    }
    QPushButton *handleBtn = box.addButton(QStringLiteral("去处理"), QMessageBox::RejectRole);
    if (pushAllBtn) {
        box.setDefaultButton(pushAllBtn);
    } else {
        box.setDefaultButton(handleBtn);
    }
    box.exec();

    if (box.clickedButton() == quitBtn) {
        return true;
    }

    if (pushAllBtn && box.clickedButton() == pushAllBtn) {
        pushAllUnpushedRepos(unpushedDirs);
        // 推送后再检查：仍有未提交/未推送则继续提示，全部清掉则允许退出
        return confirmExitDespiteGitPending();
    }

    focusGitPendingRepo(items.first().first);
    return false;
}

void MainWindow::pushAllUnpushedRepos(const QStringList &repoDirs)
{
    if (repoDirs.isEmpty()) {
        return;
    }

    constexpr int kGitPageIndex = 2;
    if (navWidget && navWidget->currentRow() != kGitPageIndex) {
        navWidget->setCurrentRow(kGitPageIndex);
    }

    int okCount = 0;
    QStringList failures;

    if (txtGitLog) {
        txtGitLog->append(QStringLiteral("<font color='cyan'>[一键推送] 开始推送 %1 个有未推送提交的仓库…</font>")
                              .arg(repoDirs.size()));
    }

    for (const QString &repoDir : repoDirs) {
        const QString absPath = QDir(repoDir).absolutePath();
        QString name = gitRepoDisplayName(absPath);
        if (name.isEmpty()) {
            name = QFileInfo(absPath).fileName();
        }

        if (absPath.isEmpty() || !QDir(absPath).exists() || !isGitRepository(absPath)) {
            failures << QStringLiteral("%1（目录无效）").arg(name);
            continue;
        }

        if (gitUnpushedCommitCount(absPath) <= 0) {
            if (txtGitLog) {
                txtGitLog->append(QStringLiteral("<font color='gray'>[一键推送] %1：无需推送，跳过</font>").arg(name));
            }
            continue;
        }

        if (txtGitLog) {
            txtGitLog->append(QStringLiteral("<font color='cyan'>$ [%1] git push</font>").arg(name));
        }

        QString out;
        QString err;
        const bool ok = GitWorktreeRunner::runInRepo(
            absPath, {QStringLiteral("push")}, &out, &err, 60000);

        if (txtGitLog) {
            if (!out.trimmed().isEmpty()) {
                txtGitLog->append(out.trimmed());
            }
            if (!err.trimmed().isEmpty()) {
                txtGitLog->append(QStringLiteral("<font color='orange'>%1</font>").arg(err.trimmed()));
            }
        }

        if (ok) {
            ++okCount;
            if (txtGitLog) {
                txtGitLog->append(QStringLiteral("<font color='green'>[一键推送] %1：推送成功</font>").arg(name));
            }
        } else {
            failures << name;
            if (txtGitLog) {
                txtGitLog->append(QStringLiteral("<font color='red'>[一键推送] %1：推送失败</font>").arg(name));
            }
        }
    }

    const QString current = cmbGitDir ? cmbGitDir->currentText().trimmed() : QString();
    if (!current.isEmpty() && QDir(current).exists()) {
        refreshGitBranchesLocal();
    }

    if (txtGitLog) {
        txtGitLog->moveCursor(QTextCursor::End);
    }

    if (failures.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("一键推送"),
            QStringLiteral("已全部推送完成（%1 个仓库）。").arg(okCount));
    } else {
        QMessageBox::warning(
            this, QStringLiteral("一键推送"),
            QStringLiteral("成功 %1 个，失败 %2 个：\n%3")
                .arg(okCount)
                .arg(failures.size())
                .arg(failures.join(QLatin1Char('\n'))));
    }
    refreshGitPendingStatusBar();
}

QList<QPair<QString, QString>> MainWindow::collectRemoteAheadItems() const
{
    QList<QPair<QString, QString>> items;

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    QSet<QString> seen;

    for (const QString &rawPath : history) {
        const QString absPath = QDir(rawPath).absolutePath();
        if (absPath.isEmpty() || seen.contains(absPath)) {
            continue;
        }
        seen.insert(absPath);

        if (!QDir(absPath).exists() || !isGitRepository(absPath)) {
            continue;
        }

        const int behind = gitBehindCommitCount(absPath);
        if (behind <= 0) {
            continue;
        }

        QString name = gitRepoDisplayName(absPath);
        if (name.isEmpty()) {
            name = QFileInfo(absPath).fileName();
        }
        items.append({absPath,
                      QStringLiteral("• %1：远程领先 %2 个提交").arg(name).arg(behind)});
    }

    return items;
}

void MainWindow::promptRemoteAheadOnOpen()
{
    const QList<QPair<QString, QString>> items = collectRemoteAheadItems();
    if (items.isEmpty()) {
        return;
    }

    QStringList warnings;
    warnings.reserve(items.size());
    for (const auto &item : items) {
        warnings << item.second;
    }

    if (txtGitLog) {
        txtGitLog->append(QStringLiteral("<font color='orange'>[启动检查] 发现远程领先本地的仓库：\n%1</font>")
                              .arg(warnings.join(QLatin1Char('\n'))));
    }

    const QString message =
        QStringLiteral("以下仓库的远程分支领先于本地：\n\n%1\n\n建议先拉取同步后再继续工作。\n"
                       "（「一键全部同步」将对上述仓库执行 git pull；「去处理」切换到第一个仓库）")
            .arg(warnings.join(QLatin1Char('\n')));

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("启动前提醒"));
    box.setText(message);
    QPushButton *laterBtn = box.addButton(QStringLiteral("稍后"), QMessageBox::AcceptRole);
    QPushButton *syncAllBtn = box.addButton(QStringLiteral("一键全部同步"), QMessageBox::YesRole);
    box.addButton(QStringLiteral("去处理"), QMessageBox::RejectRole);
    box.setDefaultButton(syncAllBtn);
    box.exec();

    if (box.clickedButton() == laterBtn) {
        return;
    }

    if (box.clickedButton() == syncAllBtn) {
        QStringList dirs;
        dirs.reserve(items.size());
        for (const auto &item : items) {
            dirs << item.first;
        }
        pullAllRemoteAheadRepos(dirs);
        return;
    }

    focusGitPendingRepo(items.first().first);
}

void MainWindow::pullAllRemoteAheadRepos(const QStringList &repoDirs)
{
    if (repoDirs.isEmpty()) {
        return;
    }

    constexpr int kGitPageIndex = 2;
    if (navWidget && navWidget->currentRow() != kGitPageIndex) {
        navWidget->setCurrentRow(kGitPageIndex);
    }

    int okCount = 0;
    QStringList failures;

    if (txtGitLog) {
        txtGitLog->append(QStringLiteral("<font color='cyan'>[一键同步] 开始拉取 %1 个远程领先仓库…</font>")
                              .arg(repoDirs.size()));
    }

    for (const QString &repoDir : repoDirs) {
        const QString absPath = QDir(repoDir).absolutePath();
        QString name = gitRepoDisplayName(absPath);
        if (name.isEmpty()) {
            name = QFileInfo(absPath).fileName();
        }

        if (absPath.isEmpty() || !QDir(absPath).exists() || !isGitRepository(absPath)) {
            failures << QStringLiteral("%1（目录无效）").arg(name);
            continue;
        }

        if (txtGitLog) {
            txtGitLog->append(QStringLiteral("<font color='cyan'>$ [%1] git pull</font>").arg(name));
        }

        QString out;
        QString err;
        const bool ok = GitWorktreeRunner::runInRepo(
            absPath, {QStringLiteral("pull")}, &out, &err, 60000);

        if (txtGitLog) {
            if (!out.trimmed().isEmpty()) {
                txtGitLog->append(out.trimmed());
            }
            if (!err.trimmed().isEmpty()) {
                txtGitLog->append(QStringLiteral("<font color='orange'>%1</font>").arg(err.trimmed()));
            }
        }

        if (ok) {
            ++okCount;
            if (txtGitLog) {
                txtGitLog->append(QStringLiteral("<font color='green'>[一键同步] %1：拉取成功</font>").arg(name));
            }
        } else {
            failures << name;
            if (txtGitLog) {
                txtGitLog->append(QStringLiteral("<font color='red'>[一键同步] %1：拉取失败</font>").arg(name));
            }
        }
    }

    const QString current = cmbGitDir ? cmbGitDir->currentText().trimmed() : QString();
    if (!current.isEmpty() && QDir(current).exists()) {
        refreshGitBranchesLocal();
    }

    if (txtGitLog) {
        txtGitLog->moveCursor(QTextCursor::End);
    }

    if (failures.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("一键全部同步"),
            QStringLiteral("已全部同步完成（%1 个仓库）。").arg(okCount));
    } else {
        QMessageBox::warning(
            this, QStringLiteral("一键全部同步"),
            QStringLiteral("成功 %1 个，失败 %2 个：\n%3")
                .arg(okCount)
                .arg(failures.size())
                .arg(failures.join(QLatin1Char('\n'))));
        focusGitPendingRepo(repoDirs.first());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmExitDespiteGitPending()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onGitRefreshBranchesClicked(bool fetchRemote) {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    if (fetchRemote) {
        runGitNetworkCommand(QStringList() << QStringLiteral("fetch") << QStringLiteral("--prune"), 60000,
                             [this](bool) {
                                 refreshGitBranchesLocal();
                             });
        return;
    }

    refreshGitBranchesLocal();
}

void MainWindow::refreshGitBranchesLocal() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(), QStringList() << "branch" << "-a");
    finishGitProcess(process, 30000);
    
    QString output = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
#endif
    
    const QString previousTarget = cmbGitBranches->currentText().trimmed();

    cmbGitBranches->blockSignals(true);
    cmbGitBranches->clear();
    QString checkedOutBranch;

    for (QString line : lines) {
        line = line.trimmed();
        bool isCurrent = line.startsWith("* ");
        if (isCurrent) {
            line = line.mid(2).trimmed();
            checkedOutBranch = line;
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

    if (!previousTarget.isEmpty() && cmbGitBranches->findText(previousTarget) >= 0) {
        cmbGitBranches->setCurrentText(previousTarget);
    }
    cmbGitBranches->blockSignals(false);

    if (lblGitCurrentBranch) {
        lblGitCurrentBranch->setText(checkedOutBranch.isEmpty()
                                         ? QStringLiteral("(未检出)")
                                         : checkedOutBranch);
    }

    txtGitLog->append("<font color='gray'>已刷新本地及远程分支（红色为远程分支）。</font>");

    const int completed = markGoalsCompletedForDeletedBranches(workDir);
    if (completed > 0) {
        refreshGitGoalsTable();
    }

    syncGitMainBranchSetting(workDir);
    onGitRefreshLogClicked();
    updateGitGoalBranchHighlights();
}

void MainWindow::onGitDiffClicked() {
    // 显示当前工作区与暂存区差异；如果需要可扩展为接受参数（如 --staged）
    runGitCommand(QStringList() << "diff");
}

void MainWindow::onGitFetchClicked() {
    runGitNetworkCommand(QStringList() << QStringLiteral("fetch") << QStringLiteral("--prune"), 60000,
                         [this](bool ok) {
                             if (ok) {
                                 refreshGitBranchesLocal();
                             }
                         });
}

void MainWindow::onGitStashClicked() {
    runGitCommand(QStringList() << "stash");
}

void MainWindow::onGitStashPopClicked() {
    runGitCommand(QStringList() << "stash" << "pop");
}

QFileInfo MainWindow::findLatestDeployExecutable(const QString &workDir, bool allowRunningApp) const
{
    if (workDir.trimmed().isEmpty() || !QDir(workDir).exists()) {
        return QFileInfo();
    }

    const QString selfPath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();

    QFileInfo bestBin;
    QFileInfo bestScript;
    QDateTime bestBinTime;
    QDateTime bestScriptTime;

    QDirIterator it(workDir, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fileInfo = it.fileInfo();
        const QString fileName = fileInfo.fileName();
        const QString absPath = fileInfo.absoluteFilePath();

        // SCP must not pick the running helper; reminder must see rebuilds of this binary.
        if (!allowRunningApp && !selfPath.isEmpty() && absPath == selfPath) {
            continue;
        }

        // Skip obvious non-deploy paths
        const QString rel = QDir(workDir).relativeFilePath(absPath);
        if (rel.contains(QStringLiteral("/.venv/")) || rel.startsWith(QStringLiteral(".venv/"))
            || rel.contains(QStringLiteral("/.git/")) || rel.contains(QStringLiteral("/node_modules/"))) {
            continue;
        }

        if (fileName.startsWith(QLatin1Char('.')) || fileName.endsWith(QStringLiteral(".so"))) {
            continue;
        }
        // Allow extension-less binaries and *.sh; skip other dotted names (objects, images, …)
        if (fileName.contains(QLatin1Char('.')) && !fileName.endsWith(QStringLiteral(".sh"))) {
            continue;
        }

        if (fileName.endsWith(QStringLiteral(".sh"))) {
            if (!bestScript.exists() || fileInfo.lastModified() > bestScriptTime) {
                bestScript = fileInfo;
                bestScriptTime = fileInfo.lastModified();
            }
        } else {
            if (!bestBin.exists() || fileInfo.lastModified() > bestBinTime) {
                bestBin = fileInfo;
                bestBinTime = fileInfo.lastModified();
            }
        }
    }

    if (bestBin.exists()) {
        return bestBin;
    }
    return bestScript;
}

static QString deployExeBaselineId(const QString &repoDir)
{
    return QString::fromLatin1(QDir(repoDir).absolutePath().toUtf8().toHex());
}

void MainWindow::rememberDeployExecutableBaseline(const QString &repoDir, const QFileInfo &fi)
{
    if (repoDir.trimmed().isEmpty() || !fi.exists()) {
        return;
    }
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("exeUpdateReminder"));
    settings.beginGroup(QStringLiteral("baselines"));
    settings.beginGroup(deployExeBaselineId(repoDir));
    settings.setValue(QStringLiteral("path"), fi.absoluteFilePath());
    settings.setValue(QStringLiteral("mtimeMs"), fi.lastModified().toMSecsSinceEpoch());
    settings.setValue(QStringLiteral("size"), fi.size());
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();
}

bool MainWindow::hasDeployExecutableBaseline(const QString &repoDir) const
{
    if (repoDir.trimmed().isEmpty()) {
        return false;
    }
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("exeUpdateReminder"));
    settings.beginGroup(QStringLiteral("baselines"));
    settings.beginGroup(deployExeBaselineId(repoDir));
    const bool ok = settings.contains(QStringLiteral("path"))
                    && settings.contains(QStringLiteral("mtimeMs"))
                    && settings.contains(QStringLiteral("size"));
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();
    return ok;
}

bool MainWindow::deployExecutableNewerThanBaseline(const QString &repoDir, const QFileInfo &fi) const
{
    if (!fi.exists() || repoDir.trimmed().isEmpty()) {
        return false;
    }
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("exeUpdateReminder"));
    settings.beginGroup(QStringLiteral("baselines"));
    settings.beginGroup(deployExeBaselineId(repoDir));
    const QString basePath = settings.value(QStringLiteral("path")).toString();
    const qint64 baseMtime = settings.value(QStringLiteral("mtimeMs"), qint64(-1)).toLongLong();
    const qint64 baseSize = settings.value(QStringLiteral("size"), qint64(-1)).toLongLong();
    settings.endGroup();
    settings.endGroup();
    settings.endGroup();

    if (basePath.isEmpty() || baseMtime < 0 || baseSize < 0) {
        return false;
    }

    const QString curPath = fi.absoluteFilePath();
    const qint64 curMtime = fi.lastModified().toMSecsSinceEpoch();
    const qint64 curSize = fi.size();
    return curPath != basePath || curMtime != baseMtime || curSize != baseSize;
}

void MainWindow::onGitAutoDiffReminderToggled(bool checked) {
    gitDiffReminderEnabled = checked;
    saveGitDiffReminderSettings();

    if (actExeReminder) {
        QSignalBlocker blocker(actExeReminder);
        actExeReminder->setChecked(checked);
    }

    if (checked) {
        btnGitAutoDiffReminder->setText(QStringLiteral("关闭可执行文件提醒"));
        gitDiffReminderTimer->setInterval(spinGitDiffIntervalMinutes->value() * 60 * 1000);
        gitDiffReminderTimer->start();
        txtGitLog->append(QStringLiteral("[可执行文件提醒] 已开启：每 %1 分钟检查记忆仓库中全部仓库的可执行文件是否更新。")
                              .arg(spinGitDiffIntervalMinutes->value()));
        onGitAutoDiffReminderTick();
    } else {
        gitDiffReminderTimer->stop();
        btnGitAutoDiffReminder->setText(QStringLiteral("开启可执行文件提醒"));
        txtGitLog->append(QStringLiteral("[可执行文件提醒] 已关闭。"));
    }
}

void MainWindow::onGitAutoDiffReminderTick()
{
    QStringList repos;
    QSet<QString> seen;
    auto appendRepo = [&](const QString &raw) {
        const QString path = QDir(raw.trimmed()).absolutePath();
        if (path.isEmpty() || !QDir(path).exists() || seen.contains(path)) {
            return;
        }
        seen.insert(path);
        repos << path;
    };

    if (cmbGitDir) {
        for (int i = 0; i < cmbGitDir->count(); ++i) {
            appendRepo(cmbGitDir->itemText(i));
        }
        appendRepo(cmbGitDir->currentText());
    }
    if (repos.isEmpty()) {
        QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
        const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
        for (const QString &h : history) {
            appendRepo(h);
        }
    }

    if (repos.isEmpty()) {
        txtGitLog->append(QStringLiteral("[可执行文件提醒] 记忆仓库为空，跳过本次检查。"));
        return;
    }

    txtGitLog->append(QStringLiteral("[可执行文件提醒] 开始检查 %1 个记忆仓库…").arg(repos.size()));

    const QString selfPath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    QStringList updatedLines;
    QStringList updatedRepos;
    int baselineOnly = 0;
    int unchanged = 0;
    int noExe = 0;
    int selfUpdated = 0;

    for (const QString &workDir : repos) {
        const QFileInfo fi = findLatestDeployExecutable(workDir, true);
        if (!fi.exists()) {
            ++noExe;
            txtGitLog->append(QStringLiteral("[可执行文件提醒] %1：未找到可部署可执行文件")
                                  .arg(workDir));
            continue;
        }

        if (!hasDeployExecutableBaseline(workDir)) {
            rememberDeployExecutableBaseline(workDir, fi);
            ++baselineOnly;
            txtGitLog->append(QStringLiteral("[可执行文件提醒] %1：已记录基线 %2 (%3)")
                                  .arg(workDir, fi.fileName(),
                                       fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            continue;
        }

        if (!deployExecutableNewerThanBaseline(workDir, fi)) {
            ++unchanged;
            continue;
        }

        rememberDeployExecutableBaseline(workDir, fi);
        updatedRepos << workDir;
        const bool isSelf = (!selfPath.isEmpty()
                             && fi.absoluteFilePath() == selfPath);
        if (isSelf) {
            ++selfUpdated;
            updatedLines << QStringLiteral(
                                "仓库: %1\n本工具可执行文件已更新（重编）\n文件: %2\n路径: %3\n修改时间: %4\n大小: %5 字节")
                                .arg(workDir, fi.fileName(), fi.absoluteFilePath(),
                                     fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                .arg(fi.size());
        } else {
            updatedLines << QStringLiteral(
                                "仓库: %1\n文件: %2\n路径: %3\n修改时间: %4\n大小: %5 字节\n建议: SCP 传输到目标设备")
                                .arg(workDir, fi.fileName(), fi.absoluteFilePath(),
                                     fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                                .arg(fi.size());
        }
        txtGitLog->append(QStringLiteral("[可执行文件提醒] 有更新: %1 -> %2")
                              .arg(workDir, fi.absoluteFilePath()));
    }

    txtGitLog->append(QStringLiteral("[可执行文件提醒] 检查完成：更新 %1，新建基线 %2，无变化 %3，无产物 %4")
                          .arg(updatedLines.size())
                          .arg(baselineOnly)
                          .arg(unchanged)
                          .arg(noExe));

    if (updatedLines.isEmpty()) {
        return;
    }

    QString header;
    if (selfUpdated > 0 && selfUpdated == updatedLines.size()) {
        header = QStringLiteral("检测到本工具可执行文件已更新（重编）。\n\n");
    } else if (selfUpdated > 0) {
        header = QStringLiteral("检测到 %1 个记忆仓库的可执行文件已更新（含本工具重编）。\n"
                                "其它仓库建议执行 SCP 传输到目标设备。\n\n")
                     .arg(updatedLines.size());
    } else {
        header = QStringLiteral("检测到 %1 个记忆仓库的可执行文件已更新，建议执行 SCP 传输到目标设备。\n\n")
                     .arg(updatedLines.size());
    }

    const QString tips = header + updatedLines.join(QStringLiteral("\n\n────────\n\n"));

    // Prefer current combo repo if it is among updated; else first updated repo.
    QString targetRepo = updatedRepos.first();
    const QString currentRepo = cmbGitDir ? QDir(cmbGitDir->currentText().trimmed()).absolutePath()
                                          : QString();
    if (!currentRepo.isEmpty() && updatedRepos.contains(currentRepo)) {
        targetRepo = currentRepo;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("可执行文件更新提醒"));
    box.setText(tips);
    auto *btnAi = box.addButton(QStringLiteral("AI 整理提交说明"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
    box.setDefaultButton(btnAi);
    box.exec();

    if (box.clickedButton() != btnAi) {
        return;
    }

    focusGitPendingRepo(targetRepo);
    txtGitLog->append(QStringLiteral("<font color='cyan'>[可执行文件提醒] 已切换到 %1，开始 AI 整理提交说明…</font>")
                          .arg(targetRepo));
    onGitAiCommitMsgClicked();
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
    if (branch.startsWith(QStringLiteral("+ "))) {
        branch = branch.mid(2).trimmed();
    }
    if (branch.isEmpty()) {
       txtGitLog->append("<font color='red'>错误: 请先选择要删除的分支</font>");
       return;
    }
    if (branch.startsWith(QStringLiteral("remotes/"))) {
        txtGitLog->append("<font color='red'>错误: 请选择本地分支删除，远程分支请用其它方式管理。</font>");
        return;
    }

    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        return;
    }

    const QString localBranch = normalizeLocalBranchRef(branch);
    const QString worktreePath = gitWorktreePathUsingBranch(repoDir, localBranch);

    QString confirmMsg = QStringLiteral("确定要删除本地分支 %1 吗？\n(未合并时 git 可能拒绝删除)").arg(localBranch);
    if (!worktreePath.isEmpty()) {
        confirmMsg = QStringLiteral(
                          "分支 %1 正被 Git Worktree 占用（与「添加 Worktree」功能相关，不是程序冲突）：\n%2\n\n"
                          "删除本地分支前必须先移除该 worktree。\n是否先移除 worktree 再删除分支？")
                          .arg(localBranch, worktreePath);
    }

    if (QMessageBox::question(this, QStringLiteral("确认删除"), confirmMsg,
                              QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    if (!worktreePath.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='gray'>[Worktree] 正在移除占用分支的工作树: %1</font>")
                              .arg(worktreePath));
        if (!runGitCommand(QStringList() << QStringLiteral("worktree") << QStringLiteral("remove")
                                         << worktreePath)) {
            txtGitLog->append(QStringLiteral("<font color='orange'>[提示] 若移除失败，可手动执行: git worktree remove --force %1</font>")
                                  .arg(worktreePath));
            return;
        }
    }

    if (runGitCommand(QStringList() << QStringLiteral("branch") << QStringLiteral("-d") << localBranch)) {
        const int completed = markGoalsCompletedForDeletedBranches(repoDir);
        if (completed > 0) {
            refreshGitGoalsTable();
        }
    }

    onGitRefreshBranchesClicked();
    onGitRefreshLogClicked();
}

void MainWindow::onGitAddClicked() {
    const QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: 请先选择Git仓库目录!</font>"));
        return;
    }
    gitStageWithReview(workDir);
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

void MainWindow::onGitWorktreeManageClicked()
{
    const QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Worktree"),
                             QStringLiteral("请先选择本地 Git 仓库目录。"));
        return;
    }
    if (!isGitRepository(workDir)) {
        QMessageBox::warning(this, QStringLiteral("Worktree"),
                             QStringLiteral("当前路径不是有效的 Git 仓库。"));
        return;
    }

    GitWorktreeDialog dlg(this, workDir, this);
    dlg.exec();
}

void MainWindow::enterGitRepoPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    saveGitHistory(path);
    txtGitLog->append(QStringLiteral("<font color='gray'>[Worktree] 已进入: %1</font>")
                          .arg(QDir(path).absolutePath()));
}

void MainWindow::rememberGitRepoPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    const QString absDir = QDir(path).absolutePath();
    const QString current = cmbGitDir->currentText().trimmed();

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    history.removeAll(path);
    history.removeAll(absDir);
    history.prepend(absDir);
    while (history.size() > MAX_HISTORY) {
        history.removeLast();
    }
    settings.setValue(QStringLiteral("GitHistory"), history);

    // 保持当前主目录选中，不切换到新 worktree
    applyGitHistoryToCombo(history, current.isEmpty() ? absDir : current, false);
    if (!current.isEmpty()) {
        cmbGitDir->blockSignals(true);
        cmbGitDir->setCurrentText(current);
        cmbGitDir->blockSignals(false);
    }
    refreshGitRepoMetaTable();
    txtGitLog->append(QStringLiteral("<font color='gray'>[History] 已记录 Worktree 路径: %1</font>")
                          .arg(absDir));
}

void MainWindow::forgetGitRepoPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    const QString absDir = QDir(path).absolutePath();
    const QString current = cmbGitDir->currentText().trimmed();
    const QString currentAbs = QDir(current).absolutePath();

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    history.removeAll(path);
    history.removeAll(absDir);
    removeGitRepoAlias(absDir);
    if (gitRepoMainProjectPath() == absDir) {
        setGitRepoMainProject(QString());
    }
    settings.setValue(QStringLiteral("GitHistory"), history);

    QString nextSelect = current;
    if (currentAbs == absDir || current == path) {
        nextSelect = history.isEmpty() ? QString() : history.first();
        applyGitHistoryToCombo(history, nextSelect, true);
    } else {
        applyGitHistoryToCombo(history, current, false);
        cmbGitDir->blockSignals(true);
        cmbGitDir->setCurrentText(current);
        cmbGitDir->blockSignals(false);
        refreshGitRepoMetaTable();
    }
    txtGitLog->append(QStringLiteral("<font color='gray'>[History] 已从记忆移除: %1</font>")
                          .arg(absDir));
}

void MainWindow::appendGitLogHtml(const QString &html)
{
    if (!txtGitLog || html.isEmpty()) {
        return;
    }
    txtGitLog->append(html);
    txtGitLog->moveCursor(QTextCursor::End);
}

void MainWindow::refreshAfterWorktreeApply()
{
    refreshGitBranchesLocal();
    runGitCommand(QStringList() << QStringLiteral("status"));
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

    const QString workDir = cmbGitDir->currentText().trimmed();
    QStringList blocked;
    if (gitStagedHasBlockedPaths(workDir, &blocked)) {
        const auto reply = QMessageBox::question(
            this, QStringLiteral("确认提交危险路径"),
            QStringLiteral("暂存区包含匹配 .gitignore / 规则危险的路径：\n\n%1\n\n"
                           "若这是暂存审查中用户/AI 的有意选择，可继续提交。是否仍要 commit？")
                .arg(blocked.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            txtGitLog->append(QStringLiteral(
                "<font color='orange'>[提交防护] 暂存区含应忽略路径，已取消 commit。</font>"));
            return;
        }
        txtGitLog->append(QStringLiteral(
            "<font color='orange'>[提交防护] 用户确认提交含危险路径的暂存区。</font>"));
    }

    runGitCommand(QStringList() << "commit" << "-m" << msg);
    txtGitCommitMsg->clear();
}

bool MainWindow::captureGitOutput(const QString &workDir, const QStringList &args, QString *stdoutOut,
                                  QString *stderrOut, int timeoutMs) const
{
    if (stdoutOut)
        stdoutOut->clear();
    if (stderrOut)
        stderrOut->clear();
    if (workDir.trimmed().isEmpty())
        return false;

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProgram(PlatformPrefs::gitBinary());
    process.setArguments(args);
    process.start();
    if (!process.waitForStarted(5000))
        return false;
    if (!finishGitProcess(process, timeoutMs))
        return false;

    if (stdoutOut)
        *stdoutOut = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
    if (stderrOut)
        *stderrOut = PlatformPrefs::decodeProcessOutput(process.readAllStandardError());
    return process.exitCode() == 0;
}

QString MainWindow::collectUncommittedContextForAi(const QString &workDir, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();
    if (workDir.trimmed().isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("未选择 Git 仓库目录");
        return {};
    }

    QString statusOut;
    QString statusErr;
    if (!captureGitOutput(workDir,
                          QStringList{QStringLiteral("status"), QStringLiteral("--porcelain")},
                          &statusOut, &statusErr, 15000)) {
        if (errorOut)
            *errorOut = QStringLiteral("git status 失败: %1").arg(statusErr.trimmed());
        return {};
    }
    if (statusOut.trimmed().isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("没有未提交的改动");
        return {};
    }

    QString logOut;
    captureGitOutput(workDir,
                     QStringList{QStringLiteral("log"), QStringLiteral("-5"), QStringLiteral("--oneline")},
                     &logOut, nullptr, 10000);

    QString unstaged;
    QString staged;
    captureGitOutput(workDir, QStringList{QStringLiteral("diff")}, &unstaged, nullptr, 30000);
    captureGitOutput(workDir,
                     QStringList{QStringLiteral("diff"), QStringLiteral("--cached")},
                     &staged, nullptr, 30000);

    QString statOut;
    captureGitOutput(workDir,
                     QStringList{QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("--stat")},
                     &statOut, nullptr, 15000);

    constexpr int kMaxDiffChars = 80000;
    auto truncate = [](QString text, int maxChars) -> QString {
        text.replace(QLatin1Char('\0'), QLatin1Char(' '));
        if (text.size() <= maxChars)
            return text;
        return text.left(maxChars) + QStringLiteral("\n…(diff 已截断)…");
    };

    QString combinedDiff = unstaged;
    if (!staged.trimmed().isEmpty()) {
        if (!combinedDiff.isEmpty())
            combinedDiff += QLatin1Char('\n');
        combinedDiff += QStringLiteral("===== STAGED (cached) =====\n") + staged;
    }

    QString context;
    context += QStringLiteral("【最近提交】\n");
    context += logOut.trimmed().isEmpty() ? QStringLiteral("(无)\n") : logOut.trimmed() + QLatin1Char('\n');
    context += QStringLiteral("\n【status --porcelain】\n");
    context += statusOut.trimmed() + QLatin1Char('\n');
    context += QStringLiteral("\n【diff --stat】\n");
    context += (statOut.trimmed().isEmpty() ? QStringLiteral("(无)") : statOut.trimmed()) + QLatin1Char('\n');
    context += QStringLiteral("\n【diff】\n");
    if (combinedDiff.trimmed().isEmpty())
        context += QStringLiteral("(仅有未跟踪文件或无可文本 diff；请结合 status 概括)\n");
    else
        context += truncate(combinedDiff, kMaxDiffChars);

    return context;
}

void MainWindow::setGitAiCommitBusy(bool busy)
{
    if (btnGitAiCommitMsg)
        btnGitAiCommitMsg->setEnabled(!busy);
    if (actDeepSeekSettings)
        actDeepSeekSettings->setEnabled(!busy && !(deepSeekHelpClient && deepSeekHelpClient->isBusy()));
    if (btnGitAiCommitMsg) {
        btnGitAiCommitMsg->setText(busy ? QStringLiteral("AI 生成中…")
                                        : QStringLiteral("AI 整理提交说明"));
    }
}

void MainWindow::setGitAskDeepSeekBusy(bool busy)
{
    if (btnGitAskDeepSeek) {
        btnGitAskDeepSeek->setEnabled(!busy);
        btnGitAskDeepSeek->setText(busy ? QStringLiteral("DeepSeek 分析中…")
                                       : QStringLiteral("不懂，问 DeepSeek"));
    }
    if (actDeepSeekSettings)
        actDeepSeekSettings->setEnabled(!busy && !(deepSeekClient && deepSeekClient->isBusy()));
}

QString MainWindow::collectGitHelpContextForAi(const QString &workDir) const
{
    QString context;
    context += QStringLiteral("【仓库目录】\n");
    context += workDir.trimmed().isEmpty() ? QStringLiteral("(未选择)\n")
                                           : workDir.trimmed() + QLatin1Char('\n');

    if (!workDir.trimmed().isEmpty()) {
        const QString branch = gitCheckedOutBranch(workDir);
        context += QStringLiteral("\n【当前分支】\n");
        context += branch.isEmpty() ? QStringLiteral("(未知)\n") : branch + QLatin1Char('\n');

        QString statusOut;
        if (captureGitOutput(workDir,
                             QStringList{QStringLiteral("status"), QStringLiteral("-sb")},
                             &statusOut, nullptr, 15000)) {
            context += QStringLiteral("\n【git status -sb】\n");
            context += (statusOut.trimmed().isEmpty() ? QStringLiteral("(干净)")
                                                      : statusOut.trimmed())
                       + QLatin1Char('\n');
        }
    }

    QString logPlain;
    if (txtGitLog)
        logPlain = txtGitLog->toPlainText().trimmed();

    constexpr int kMaxLogChars = 14000;
    if (logPlain.size() > kMaxLogChars)
        logPlain = QStringLiteral("…(更早输出已省略)…\n") + logPlain.right(kMaxLogChars);

    context += QStringLiteral("\n【Git 输出面板（用户看不懂的内容）】\n");
    if (logPlain.isEmpty())
        context += QStringLiteral("(输出为空；请结合仓库状态给出常规排查建议)\n");
    else
        context += logPlain + QLatin1Char('\n');

    return context;
}

void MainWindow::showDeepSeekGitHelpDialog(const QString &advice)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("DeepSeek：接下来怎么做"));
    dlg.resize(640, 480);

    auto *txt = new QTextEdit(&dlg);
    txt->setReadOnly(true);
    txt->setPlainText(advice);
    txt->setStyleSheet(QStringLiteral("font-size: 13px;"));

    auto *buttons = new QDialogButtonBox(&dlg);
    auto *btnCopy = buttons->addButton(QStringLiteral("复制建议"), QDialogButtonBox::ActionRole);
    auto *btnClose = buttons->addButton(QDialogButtonBox::Close);
    connect(btnCopy, &QPushButton::clicked, &dlg, [advice]() {
        if (QClipboard *clip = QApplication::clipboard())
            clip->setText(advice);
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(new QLabel(
        QStringLiteral("下面是 DeepSeek 根据 Git 输出给出的解释与下一步建议（仅供参考，危险操作请再确认）："),
        &dlg));
    root->addWidget(txt, 1);
    root->addWidget(buttons);
    dlg.exec();
}

void MainWindow::onGitAskDeepSeekClicked()
{
    if (DeepSeekClient::apiKey().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("未配置 DeepSeek API Key，请先打开「DeepSeek 设置」。"));
        onGitDeepSeekSettingsClicked();
        if (DeepSeekClient::apiKey().isEmpty())
            return;
    }
    if (deepSeekHelpClient && deepSeekHelpClient->isBusy()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("已有求助请求进行中，请稍候。"));
        return;
    }

    QString pathError;
    const QString workDir = currentGitWorkDir(&pathError);
    // Allow asking even if path is invalid — log output alone may still be useful
    const QString context = collectGitHelpContextForAi(workDir);

    const QString logPlain = txtGitLog ? txtGitLog->toPlainText().trimmed() : QString();
    if (logPlain.isEmpty() && workDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("不懂，问 DeepSeek"),
                                 QStringLiteral("Git 输出为空，且未选择仓库。请先执行一次 Git 操作，或选择仓库后再问。"));
        return;
    }

    const QString systemPrompt = QStringLiteral(
        "你是耐心的 Git 助手，服务对象是不太熟悉 Git 命令行输出的用户。"
        "根据用户提供的仓库状态和 Git 输出面板内容，用通俗中文回答。"
        "输出结构必须包含：\n"
        "1) 【现在是什么情况】用 1～3 句说明成功/失败/冲突/卡住等原因；\n"
        "2) 【接下来怎么做】给出分步建议，优先安全、可逆的操作；\n"
        "3) 【可复制命令】如需命令，每行一条，尽量写成可在本应用 Git 控制台直接粘贴的形式"
        "（可带或不带开头的 git）；\n"
        "危险操作（如 reset --hard、push --force、clean -fd）必须明确标出风险，并给出更安全的替代方案。"
        "若信息不足，说明还缺什么，并建议先运行哪些查看命令（如 status、diff、log）。"
        "不要编造仓库里并不存在的分支或提交。");

    setGitAskDeepSeekBusy(true);
    if (txtGitLog)
        txtGitLog->append(QStringLiteral("<font color='gray'>[DeepSeek] 正在阅读 Git 输出并给出下一步建议…</font>"));
    deepSeekHelpClient->chat(systemPrompt, context);
}

void MainWindow::onDeepSeekGitHelpReady(const QString &content)
{
    setGitAskDeepSeekBusy(false);
    const QString advice = content.trimmed();
    if (txtGitLog) {
        txtGitLog->append(QStringLiteral("<font color='green'>[DeepSeek 建议]</font>"));
        // Keep log readable: show full text as plain (escaped) with line breaks
        const QString html = advice.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
        txtGitLog->append(html);
        txtGitLog->moveCursor(QTextCursor::End);
    }
    showDeepSeekGitHelpDialog(advice);
}

void MainWindow::onDeepSeekGitHelpFailed(const QString &error)
{
    setGitAskDeepSeekBusy(false);
    if (txtGitLog) {
        txtGitLog->append(QStringLiteral("<font color='red'>[DeepSeek] 求助失败：%1</font>")
                              .arg(error.toHtmlEscaped()));
    }
    QMessageBox::warning(this, QStringLiteral("DeepSeek"),
                         QStringLiteral("分析 Git 输出失败：\n%1").arg(error));
}

void MainWindow::onGitDeepSeekSettingsClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("DeepSeek 设置"));
    dlg.resize(520, 180);

    auto *form = new QFormLayout();
    auto *editKey = new QLineEdit(DeepSeekClient::apiKey(), &dlg);
    editKey->setEchoMode(QLineEdit::Password);
    editKey->setPlaceholderText(QStringLiteral("sk-…"));
    auto *chkShow = new QCheckBox(QStringLiteral("显示 Key"), &dlg);
    connect(chkShow, &QCheckBox::toggled, editKey, [editKey](bool on) {
        editKey->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    auto *editBase = new QLineEdit(DeepSeekClient::baseUrl(), &dlg);
    auto *editModel = new QLineEdit(DeepSeekClient::model(), &dlg);

    form->addRow(QStringLiteral("API Key"), editKey);
    form->addRow(QString(), chkShow);
    form->addRow(QStringLiteral("Base URL"), editBase);
    form->addRow(QStringLiteral("模型"), editModel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *root = new QVBoxLayout(&dlg);
    root->addLayout(form);
    root->addWidget(new QLabel(
        QStringLiteral("配置保存在本机 QSettings（LiChenYang/LinuxHelper），不会写入仓库。"), &dlg));
    root->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    DeepSeekClient::setApiKey(editKey->text());
    DeepSeekClient::setBaseUrl(editBase->text());
    DeepSeekClient::setModel(editModel->text());
    txtGitLog->append(QStringLiteral("<font color='green'>[DeepSeek] 设置已保存。</font>"));
}

void MainWindow::onGitAiCommitMsgClicked()
{
    const QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择 Git 仓库目录"));
        return;
    }
    if (DeepSeekClient::apiKey().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("未配置 DeepSeek API Key，请先打开「DeepSeek 设置」。"));
        onGitDeepSeekSettingsClicked();
        if (DeepSeekClient::apiKey().isEmpty())
            return;
    }
    if (deepSeekClient && deepSeekClient->isBusy()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("已有请求进行中，请稍候。"));
        return;
    }

    QString err;
    const QString context = collectUncommittedContextForAi(workDir, &err);
    if (context.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("AI 提交说明"),
                                 err.isEmpty() ? QStringLiteral("无法收集改动上下文") : err);
        if (!err.isEmpty())
            txtGitLog->append(QStringLiteral("<font color='orange'>[DeepSeek] %1</font>").arg(err));
        return;
    }

    const QString systemPrompt = QStringLiteral(
        "你是 Git 提交信息助手。根据用户给出的 status/diff/最近提交风格，写一条中文 commit message。"
        "要求：1) 只输出提交信息本身，不要解释、不要 Markdown、不要引号包裹；"
        "2) 1～2 句，说明做了什么或为什么，不要流水账列文件名；"
        "3) 风格贴近最近提交；4) 改动杂乱时概括主线，次要改动可一句带过。");

    gitAiCommitPendingConfirm = true;
    setGitAiCommitBusy(true);
    txtGitLog->append(QStringLiteral("<font color='gray'>[DeepSeek] 正在根据未提交改动生成提交说明…</font>"));
    deepSeekClient->chat(systemPrompt, context);
}

void MainWindow::onDeepSeekCommitMsgReady(const QString &content)
{
    setGitAiCommitBusy(false);

    QString msg = content.trimmed();
    // Keep first paragraph / first two non-empty lines as the commit subject(+body)
    const QStringList lines = msg.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                        Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
        if (lines.size() == 1) {
            msg = lines.first().trimmed();
        } else {
            msg = lines.first().trimmed();
            const QString second = lines.at(1).trimmed();
            if (!second.isEmpty() && msg.size() + second.size() < 200)
                msg += QLatin1Char('\n') + second;
        }
    }
    // Strip surrounding quotes the model sometimes adds
    if ((msg.startsWith(QLatin1Char('"')) && msg.endsWith(QLatin1Char('"')))
        || (msg.startsWith(QStringLiteral("「")) && msg.endsWith(QStringLiteral("」")))) {
        msg = msg.mid(1, msg.size() - 2).trimmed();
    }

    if (txtGitCommitMsg)
        txtGitCommitMsg->setText(msg.contains(QLatin1Char('\n')) ? msg.split(QLatin1Char('\n')).first()
                                                                  : msg);
    // Prefer single-line in QLineEdit; if model returned two lines, keep first in the box
    // and show full text in log / confirm dialog.
    txtGitLog->append(QStringLiteral("<font color='green'>[DeepSeek] 生成提交说明：</font>%1")
                          .arg(msg.toHtmlEscaped()));

    if (!gitAiCommitPendingConfirm)
        return;
    gitAiCommitPendingConfirm = false;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("AI 提交说明"));
    box.setText(QStringLiteral("已生成提交说明，是否继续暂存并提交？"));
    box.setInformativeText(msg);
    auto *btnCommit = box.addButton(QStringLiteral("暂存并提交"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("仅填入，稍后手动提交"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() != btnCommit)
        return;

    const QString workDir = cmbGitDir->currentText().trimmed();
    if (!gitStageWithReview(workDir))
        return;

    QStringList blocked;
    if (gitStagedHasBlockedPaths(workDir, &blocked)) {
        const auto reply = QMessageBox::question(
            this, QStringLiteral("确认提交危险路径"),
            QStringLiteral("暂存区包含匹配 .gitignore / 规则危险的路径：\n\n%1\n\n是否仍要 commit？")
                .arg(blocked.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
    }

    // Use the (possibly edited) line edit text if user changed it; else full generated msg
    QString commitMsg = txtGitCommitMsg ? txtGitCommitMsg->text().trimmed() : QString();
    if (commitMsg.isEmpty())
        commitMsg = msg;
    if (commitMsg.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("提交信息为空"));
        return;
    }

    runGitCommand(QStringList() << QStringLiteral("commit") << QStringLiteral("-m") << commitMsg);
    if (txtGitCommitMsg)
        txtGitCommitMsg->clear();
}

void MainWindow::onDeepSeekCommitMsgFailed(const QString &error)
{
    setGitAiCommitBusy(false);
    gitAiCommitPendingConfirm = false;
    txtGitLog->append(QStringLiteral("<font color='red'>[DeepSeek] 失败：%1</font>")
                          .arg(error.toHtmlEscaped()));
    QMessageBox::warning(this, QStringLiteral("DeepSeek"),
                         QStringLiteral("生成提交说明失败：\n%1").arg(error));
}

void MainWindow::onGitPushClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    QString branch = gitCheckedOutBranch(workDir);
    QString remote = cmbGitRemote->currentText();
    if (branch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 无法识别当前检出的分支，无法推送。</font>");
        return;
    }
    if (remote.isEmpty()) remote = "origin";

    // Use -u to set upstream as requested
    runGitNetworkCommand(QStringList() << QStringLiteral("push") << QStringLiteral("-u") << remote << branch, 30000);
}

void MainWindow::onGitPullClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    QString branch = gitCheckedOutBranch(workDir);
    QString remote = cmbGitRemote->currentText();
    if (branch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 无法识别当前检出的分支，无法拉取。</font>");
        return;
    }
    if (remote.isEmpty()) remote = "origin";
    runGitNetworkCommand(QStringList() << QStringLiteral("pull") << remote << branch, 30000,
                         [this](bool ok) {
                             if (ok) {
                                 refreshGitBranchesLocal();
                             }
                         });
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
        txtGitLog->append("<font color='red'>错误: 请先在目标分支中选择要合并进来的分支!</font>");
        return;
    }

    QString currentBranch = gitCheckedOutBranch(workDir);
    
    if (currentBranch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 无法识别当前检出的分支，无法合并。</font>");
        return;
    }

    if (branchToMerge == currentBranch) {
        QMessageBox::warning(this, "合并提示", "当前已经在分支 [" + currentBranch + "] 上，无需合并自身。请在目标分支中选择其他分支。");
        return;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认合并", 
                                  QString("确定要将分支 [%1] 的改动合并到当前分支 [%2] 吗?").arg(branchToMerge).arg(currentBranch),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        runGitCommand(QStringList() << "merge" << branchToMerge);
    }
}

void MainWindow::onGitRebaseClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    QString upstreamBranch = cmbGitBranches->currentText().trimmed();
    if (upstreamBranch.startsWith("+ ")) {
        upstreamBranch = upstreamBranch.mid(2).trimmed();
    }

    if (upstreamBranch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 请先在目标分支中选择要变基到的基准分支!</font>");
        return;
    }

    QString currentBranch = gitCheckedOutBranch(workDir);

    if (currentBranch.isEmpty()) {
        txtGitLog->append("<font color='red'>错误: 无法识别当前检出的分支，无法变基。</font>");
        return;
    }

    if (upstreamBranch == currentBranch) {
        QMessageBox::warning(this, "变基提示",
                             "当前已经在分支 [" + currentBranch + "] 上，无法变基到自身。请在目标分支中选择其他分支作为基准。");
        return;
    }

    const QString confirmMsg = QString(
        "确定要将当前分支 [%1] 变基到 [%2] 之上吗？\n\n"
        "变基会把 [%1] 上独有的提交逐个重放到 [%2] 的最新提交之后，并改写 [%1] 的历史。\n"
        "若 [%1] 已推送到远程，变基后通常需要 force push。")
        .arg(currentBranch, upstreamBranch);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "确认变基", confirmMsg, QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        runGitCommand(QStringList() << "rebase" << upstreamBranch);
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
    QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty()) {
        txtGitLog->append("错误: 请先选择 Git 仓库目录。");
        return;
    }

    QFile ignoreFile(repoDir + QStringLiteral("/.gitignore"));

    auto createIgnoreFile = [&](const QString &type) {
        if (!ignoreFile.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        QTextStream out(&ignoreFile);
        out << gitIgnoreTemplateContent(type);
        ignoreFile.close();
        txtGitLog->append(QStringLiteral("成功: 已创建 %1 类型的 .gitignore 文件。").arg(type));
    };

    if (!ignoreFile.exists()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(QStringLiteral(".gitignore 检查"));
        msgBox.setText(QStringLiteral(".gitignore 文件不存在！\n请选择要创建的模板类型："));
        QPushButton *qtBtn = msgBox.addButton(QStringLiteral("Qt/C++ 模板"), QMessageBox::ActionRole);
        QPushButton *keilBtn = msgBox.addButton(QStringLiteral("Keil/C 模板"), QMessageBox::ActionRole);
        msgBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        msgBox.exec();

        if (msgBox.clickedButton() == qtBtn)
            createIgnoreFile(QStringLiteral("Qt"));
        else if (msgBox.clickedButton() == keilBtn)
            createIgnoreFile(QStringLiteral("Keil"));
        return;
    }

    const QString templateType = gitIgnorePickTemplateType(
        this, QStringLiteral(".gitignore 检查"),
        QStringLiteral("请选择用于对照检查的模板类型。\n"
                       "将按该模板文件中的全部有效规则（逐行）与当前 .gitignore 比对。"));
    if (templateType.isEmpty())
        return;

    if (!ignoreFile.open(QIODevice::ReadWrite | QIODevice::Text))
        return;

    QString content = ignoreFile.readAll();
    const int templateRuleCount = gitIgnoreEffectiveLines(gitIgnoreTemplateContent(templateType)).size();
    txtGitLog->append(QStringLiteral("信息: 正在与 %1 模板对照检查（模板共 %2 条有效规则）…")
                          .arg(templateType)
                          .arg(templateRuleCount));

    GitIgnoreTemplateDiff diff = gitIgnoreDiffAgainstTemplate(content, templateType);

    if (diff.missingInFile.isEmpty() && diff.extraInFile.isEmpty()) {
        QMessageBox::information(this, QStringLiteral(".gitignore 检查"),
                                 QStringLiteral("当前 .gitignore 与 %1 模板完全一致。").arg(templateType));
        ignoreFile.close();
        return;
    }

    if (!diff.missingInFile.isEmpty()) {
        const QString msg = QStringLiteral("相对 %1 模板，.gitignore 缺少以下 %2 条规则:\n\n%3\n\n"
                                           "是否从模板补全到 .gitignore 文件末尾？")
                                .arg(templateType)
                                .arg(diff.missingInFile.size())
                                .arg(diff.missingInFile.join(QLatin1Char('\n')));
        const QMessageBox::StandardButton reply =
            QMessageBox::question(this, QStringLiteral("补全 .gitignore"), msg, QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QTextStream out(&ignoreFile);
            if (!content.endsWith(QLatin1Char('\n')))
                out << QLatin1Char('\n');
            out << QStringLiteral("\n# Added from %1 template by Assistant\n").arg(templateType);
            for (const QString &p : diff.missingInFile)
                out << p << QLatin1Char('\n');
            out.flush();
            ignoreFile.flush();
            txtGitLog->append(QStringLiteral("成功: 已从 %1 模板补全 %2 条规则到 .gitignore。")
                                  .arg(templateType)
                                  .arg(diff.missingInFile.size()));

            txtGitLog->append(QStringLiteral("信息: 正在从 Git 索引中移除已忽略路径的缓存…"));
            QStringList cleanArgs;
            cleanArgs << QStringLiteral("rm") << QStringLiteral("-r") << QStringLiteral("--cached");
            for (QString cleanPattern : diff.missingInFile) {
                if (cleanPattern.endsWith(QLatin1Char('/')))
                    cleanPattern.chop(1);
                cleanArgs << cleanPattern;
            }
            runGitCommand(cleanArgs);

            ignoreFile.seek(0);
            content = ignoreFile.readAll();
            diff = gitIgnoreDiffAgainstTemplate(content, templateType);
        }
    }

    if (!diff.extraInFile.isEmpty()) {
        const QString msg = QStringLiteral("相对 %1 模板，.gitignore 多出以下 %2 条规则:\n\n%3\n\n"
                                           "是否写入 %1 模板？")
                                .arg(templateType)
                                .arg(diff.extraInFile.size())
                                .arg(diff.extraInFile.join(QLatin1Char('\n')));
        const QMessageBox::StandardButton reply =
            QMessageBox::question(this, QStringLiteral("更新模板"), msg, QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (!gitIgnoreAppendLinesToTemplate(txtGitLog, templateType, diff.extraInFile)) {
                txtGitLog->append(QStringLiteral("<font color='gray'>信息: 模板未变更。</font>"));
            }
        }
    } else if (diff.missingInFile.isEmpty()) {
        txtGitLog->append(QStringLiteral("信息: 与 %1 模板一致，无多余规则。").arg(templateType));
    }

    ignoreFile.close();
}

void MainWindow::onGitRefreshLogClicked() {
    QString workDir = cmbGitDir->currentText().trimmed();
    if (workDir.isEmpty()) return;

    QStringList args;
    args << QStringLiteral("log")
         << QStringLiteral("--pretty=format:%h - %cd : %s (%an)")
         << QStringLiteral("--date=short")
         << QStringLiteral("-n")
         << QStringLiteral("20");

    QString branch = cmbGitBranches ? cmbGitBranches->currentText().trimmed() : QString();
    if (branch.startsWith(QStringLiteral("* "))) {
        branch = branch.mid(2).trimmed();
    }
    if (!branch.isEmpty()) {
        QString logRef = branch;
        if (logRef.startsWith(QStringLiteral("remotes/origin/"))) {
            logRef = QStringLiteral("origin/") + logRef.mid(QStringLiteral("remotes/origin/").size());
        } else if (logRef.startsWith(QStringLiteral("remotes/"))) {
            logRef = logRef.mid(QStringLiteral("remotes/").size());
        }
        args << logRef;
    }

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(), args);
    finishGitProcess(process, 30000);
    
    QString output = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
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
    refreshGitGoalsTable();
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
    QString errorMsg;
    const QString finalContent = buildDailyReportContent(&errorMsg, true);
    if (finalContent.isEmpty()) {
        txtGitLog->append(errorMsg.isEmpty()
                              ? QStringLiteral("错误: 无法生成日报内容")
                              : errorMsg);
        return;
    }

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QGuiApplication::clipboard()->setText(finalContent);
    txtGitLog->append(QStringLiteral("已拼接今日(%1)日报并复制:").arg(today));
    txtGitLog->append(finalContent);
    saveDailyReportToDocs(finalContent);
}

void MainWindow::onGitOpenDailyReportClicked() {
    const QString filePath = dailyReportFilePathForToday();
    if (!QFileInfo::exists(filePath)) {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: 今日日报文件不存在: %1</font>").arg(filePath));
        txtGitLog->append(QStringLiteral("提示: 可先点击「复制到日报」生成文件。"));
        return;
    }

    const bool started = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    if (started) {
        txtGitLog->append(QStringLiteral("信息: 已尝试使用系统默认程序打开 %1").arg(filePath));
    } else {
        txtGitLog->append(QStringLiteral("<font color='red'>错误: 无法打开日报文件，请手动打开: %1</font>").arg(filePath));
    }
}

void MainWindow::onGitOpenSkillsClicked() {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();

    QVector<CursorSkillRepoRef> repos;
    repos.reserve(history.size());
    QSet<QString> seen;
    for (const QString &rawPath : history) {
        const QString absPath = gitGoalsRepoKey(rawPath);
        if (absPath.isEmpty() || seen.contains(absPath))
            continue;
        seen.insert(absPath);
        CursorSkillRepoRef ref;
        ref.repoPath = absPath;
        ref.displayName = gitRepoDisplayName(absPath);
        repos.append(ref);
    }

    CursorSkillsDialog dlg(repos, this);
    dlg.exec();
    txtGitLog->append(QStringLiteral("信息: 已打开 Cursor Skills 列表（总 Skill: %1）")
                          .arg(CursorSkillsDialog::globalSkillsRoot()));
}

QString MainWindow::buildDailyReportContent(QString *errorOut, bool showUiWarnings) {
    if (errorOut)
        errorOut->clear();

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    if (history.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("错误: 记忆路径为空，请先添加 Git 仓库");
        return QString();
    }

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QStringList numberedEntries;

    for (const QString &rawPath : history) {
        const QString absPath = gitGoalsRepoKey(rawPath);
        if (absPath.isEmpty())
            continue;

        if (!QDir(absPath).exists()) {
            txtGitLog->append(
                QStringLiteral("<font color='orange'>[日报] 跳过无效路径: %1</font>").arg(absPath));
            continue;
        }
        if (!isGitRepository(absPath)) {
            txtGitLog->append(
                QStringLiteral("<font color='orange'>[日报] 跳过非 Git 仓库: %1</font>").arg(absPath));
            continue;
        }

        const QStringList subjects = fetchTodayCommitSubjects(absPath);
        if (subjects.isEmpty())
            continue;

        const QString displayName = gitRepoDisplayName(absPath);
        for (const QString &subject : subjects) {
            numberedEntries.append(QStringLiteral("【%1】%2").arg(displayName, subject));
        }
    }

    if (numberedEntries.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("未找到日期为 %1 的提交记录").arg(today);
        return QString();
    }

    QString commitsPart;
    for (int i = 0; i < numberedEntries.size(); ++i) {
        commitsPart += QStringLiteral("%1. %2\n").arg(i + 1).arg(numberedEntries.at(i));
    }
    commitsPart = commitsPart.trimmed();

    QString finalContent = commitsPart;
    const QString mainPath = gitRepoMainProjectPath();
    if (!mainPath.isEmpty() && QDir(mainPath).exists() && isGitRepository(mainPath)) {
        QList<GitWorkGoal> goals = loadGitGoals(mainPath);
        syncGoalDifficultyFromDiffLines(mainPath, goals);

        QList<const GitWorkGoal *> rootGoals;
        for (const GitWorkGoal &g : goals) {
            if (g.parentId.isEmpty()) {
                rootGoals.append(&g);
            }
        }

        if (rootGoals.size() > 1) {
            QStringList titles;
            for (const GitWorkGoal *g : rootGoals) {
                titles << g->title;
            }
            const QString warnMsg =
                QStringLiteral("主项目仓库存在 %1 个无父目标（根目标），无法自动填入完成度：\n%2")
                    .arg(rootGoals.size())
                    .arg(titles.join(QStringLiteral("\n")));
            txtGitLog->append(QStringLiteral("<font color='orange'>[日报] %1</font>").arg(warnMsg));
            if (showUiWarnings) {
                QMessageBox::warning(this, QStringLiteral("复制到日报"), warnMsg);
            }
        } else if (rootGoals.size() == 1) {
            const GitWorkGoal *root = rootGoals.first();
            const GitRootProgressInfo progress = calcRootGoalProgress(mainPath, *root, goals);
            finalContent = root->title + QLatin1Char('\n') + commitsPart
                           + QStringLiteral("\n\n完成度：%1%").arg(qRound(progress.totalPercent));
        }
    }

    return finalContent;
}

QString MainWindow::dailyReportDocsDir() const {
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("docs/日报"));
}

QString MainWindow::dailyReportFilePathForToday() const {
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(dailyReportDocsDir()).filePath(today + QStringLiteral(".txt"));
}

bool MainWindow::saveDailyReportToDocs(const QString &content) {
    if (content.trimmed().isEmpty())
        return false;

    const QString dirPath = dailyReportDocsDir();
    if (!QDir().mkpath(dirPath)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[日报] 无法创建目录: %1</font>").arg(dirPath));
        return false;
    }

    const QString filePath = dailyReportFilePathForToday();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[日报] 无法写入文件: %1</font>").arg(filePath));
        return false;
    }

    QByteArray bytes = content.toUtf8();
    if (!content.endsWith(QLatin1Char('\n')))
        bytes.append('\n');
    if (file.write(bytes) != bytes.size()) {
        txtGitLog->append(QStringLiteral("<font color='red'>[日报] 写入不完整: %1</font>").arg(filePath));
        file.close();
        return false;
    }
    file.close();

    txtGitLog->append(QStringLiteral("<font color='green'>[日报] 已保存到 %1</font>").arg(filePath));
    return true;
}

void MainWindow::onDailyReportAutoSaveTick() {
    tryAutoSaveDailyReport();
}

void MainWindow::tryAutoSaveDailyReport() {
    if (!lifeAssistant || !lifeAssistant->isWorkdayToday())
        return;

    const QTime now = QTime::currentTime();
    if (now < QTime(17, 30))
        return;

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("GitDailyReport"));
    const QString lastAutoSaveDate = settings.value(QStringLiteral("lastAutoSaveDate")).toString();
    if (lastAutoSaveDate == today) {
        settings.endGroup();
        return;
    }

    QString errorMsg;
    const QString content = buildDailyReportContent(&errorMsg, false);
    if (content.isEmpty()) {
        txtGitLog->append(QStringLiteral("<font color='orange'>[日报] 工作日 17:30 自动保存跳过: %1</font>")
                              .arg(errorMsg.isEmpty() ? QStringLiteral("无内容") : errorMsg));
    } else {
        saveDailyReportToDocs(content);
        txtGitLog->append(QStringLiteral("<font color='green'>[日报] 工作日 17:30 自动保存完成</font>"));
    }

    settings.setValue(QStringLiteral("lastAutoSaveDate"), today);
    settings.endGroup();
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

    // 有未提交改动时才执行 add + commit，确保代码状态可追溯。
    if (gitHasUncommittedChanges(dir)) {
        if (!gitStageWithReview(dir)) {
            txtGitLog->append(QStringLiteral("已取消传输：暂存审查未通过或已取消，未执行 git commit。"));
            return;
        }

        if (spinGitDiffIntervalMinutes && spinGitDiffIntervalMinutes->value() < 10) {
            if (QMessageBox::question(this,
                                      "传输前检查",
                                      "当前间隔设置小于10分钟，是否先执行一次软回退 (git reset --soft HEAD^)？",
                                      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                runGitCommand(QStringList() << "reset" << "--soft" << "HEAD^");
                onGitRefreshLogClicked();
            }
        }

        QStringList blocked;
        if (gitStagedHasBlockedPaths(dir, &blocked)) {
            const auto reply = QMessageBox::question(
                this, QStringLiteral("确认提交危险路径"),
                QStringLiteral("暂存区包含匹配 .gitignore / 规则危险的路径：\n\n%1\n\n"
                               "是否仍要提交并继续传输？")
                    .arg(blocked.join(QLatin1Char('\n'))),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                txtGitLog->append(QStringLiteral("已取消传输：用户未确认含危险路径的提交。"));
                return;
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
    } else {
        txtGitLog->append("工作区无未提交改动，跳过 git add / commit，继续搜索可执行文件...");
    }

    // 递归查找目录下最新的可执行文件（与可执行文件提醒共用规则）
    const QFileInfo fi = findLatestDeployExecutable(dir);
    if (!fi.exists()) {
        txtGitLog->append("未在目录及其子目录下找到符合条件的可执行文件");
        return;
    }

    const QString latestFile = fi.absoluteFilePath();
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
    connect(stopProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this, stopProcess, fileName, password, targetIp, latestFile, dir, remotePath, runCmd](int, QProcess::ExitStatus) {
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
        connect(scpProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
                [this, scpProcess, fileName, dir, latestFile](int exitCode, QProcess::ExitStatus exitStatus) {
            if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                txtGitLog->append(QString("传输成功: %1 已上传至 /userfs/app").arg(fileName));
                currentMonitoringProcess = fileName; // 记录当前传输的文件名以便监测
                const QFileInfo transferred(latestFile);
                if (transferred.exists()) {
                    rememberDeployExecutableBaseline(dir, transferred);
                }
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
                const QFileInfo fi = findLatestDeployExecutable(dir);
                if (fi.exists()) {
                    currentMonitoringProcess = fi.fileName();
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

void MainWindow::applyGitHistoryToCombo(const QStringList &history, const QString &selectPath,
                                        bool activateRepo) {
    cmbGitDir->blockSignals(true);
    cmbGitDir->clear();
    cmbGitDir->addItems(history);
    if (!selectPath.isEmpty()) {
        cmbGitDir->setCurrentText(selectPath);
    } else if (!history.isEmpty()) {
        cmbGitDir->setCurrentIndex(0);
    }
    cmbGitDir->blockSignals(false);
    updateGitConsoleCwdLabel();
    if (!activateRepo) {
        refreshGitRepoMetaTable();
        return;
    }
    const QString path = selectPath.isEmpty() && !history.isEmpty() ? history.first() : selectPath;
    activateGitRepo(path.isEmpty() ? cmbGitDir->currentText().trimmed() : path, isGitAutoFetchEnabled());
    refreshGitGoalsTable();
    refreshGitRepoMetaTable();
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
    applyGitHistoryToCombo(history, QString(), false);
}

void MainWindow::loadGitDiffReminderSettings() {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("exeUpdateReminder"));

    // Prefer new group; fall back to legacy gitDiffReminder.enabled / interval
    if (!settings.contains(QStringLiteral("enabled"))) {
        settings.endGroup();
        settings.beginGroup(QStringLiteral("gitDiffReminder"));
        gitDiffReminderEnabled = settings.value(QStringLiteral("enabled"), true).toBool();
        const int legacyInterval = settings.value(QStringLiteral("intervalMinutes"), 5).toInt();
        settings.endGroup();
        settings.beginGroup(QStringLiteral("exeUpdateReminder"));
        if (spinGitDiffIntervalMinutes) {
            spinGitDiffIntervalMinutes->setValue(legacyInterval);
        }
    } else {
        gitDiffReminderEnabled = settings.value(QStringLiteral("enabled"), true).toBool();
        if (spinGitDiffIntervalMinutes) {
            spinGitDiffIntervalMinutes->setValue(
                settings.value(QStringLiteral("intervalMinutes"), 5).toInt());
        }
    }

    settings.endGroup();

    if (gitDiffReminderTimer && spinGitDiffIntervalMinutes) {
        gitDiffReminderTimer->setInterval(spinGitDiffIntervalMinutes->value() * 60 * 1000);
    }
}

void MainWindow::saveGitDiffReminderSettings() {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("exeUpdateReminder"));
    settings.setValue(QStringLiteral("enabled"), gitDiffReminderEnabled);
    if (spinGitDiffIntervalMinutes) {
        settings.setValue(QStringLiteral("intervalMinutes"), spinGitDiffIntervalMinutes->value());
    }
    settings.endGroup();
}

void MainWindow::applyGitDiffReminderEnabled(bool enabled) {
    gitDiffReminderEnabled = enabled;

    if (btnGitAutoDiffReminder) {
        btnGitAutoDiffReminder->blockSignals(true);
        btnGitAutoDiffReminder->setChecked(enabled);
        btnGitAutoDiffReminder->setText(enabled ? QStringLiteral("关闭可执行文件提醒")
                                                : QStringLiteral("开启可执行文件提醒"));
        btnGitAutoDiffReminder->blockSignals(false);
    }

    if (actExeReminder) {
        QSignalBlocker blocker(actExeReminder);
        actExeReminder->setChecked(enabled);
    }

    if (!gitDiffReminderTimer) {
        return;
    }

    if (enabled) {
        const int minutes = spinGitDiffIntervalMinutes ? spinGitDiffIntervalMinutes->value() : 5;
        gitDiffReminderTimer->setInterval(qMax(1, minutes) * 60 * 1000);
        gitDiffReminderTimer->start();
    } else {
        gitDiffReminderTimer->stop();
    }
}

void MainWindow::deferredGitRepoInit() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (!repoDir.isEmpty() && QDir(repoDir).exists()) {
        activateGitRepo(repoDir, false);
        refreshGitGoalsTable();
        // 启动后静默 fetch，再检查远程是否领先本地；失败不弹重试框以免干扰启动
        gitNetworkSuppressRetry = true;
        runGitNetworkCommand(
            QStringList() << QStringLiteral("fetch") << QStringLiteral("--prune"), 60000,
            [this](bool) {
                refreshGitBranchesLocal();
                promptRemoteAheadOnOpen();
            });
    } else {
        promptRemoteAheadOnOpen();
    }

    if (gitDiffReminderEnabled) {
        applyGitDiffReminderEnabled(true);
        saveGitDiffReminderSettings();
        txtGitLog->append(QStringLiteral("[可执行文件提醒] 已开启：每 %1 分钟检查记忆仓库中全部仓库的可执行文件是否更新。")
                              .arg(spinGitDiffIntervalMinutes ? spinGitDiffIntervalMinutes->value() : 5));
        onGitAutoDiffReminderTick();
    }

    refreshGitPendingStatusBar();
}

void MainWindow::removeGitHistoryPath(const QString &dir) {
    if (dir.isEmpty()) return;

    QSettings settings("LiChenYang", "LinuxHelper");
    QStringList history = settings.value("GitHistory").toStringList();
    const QString absDir = QDir(dir).absolutePath();

    history.removeAll(dir);
    history.removeAll(absDir);
    removeGitRepoAlias(absDir);
    if (gitRepoMainProjectPath() == absDir) {
        setGitRepoMainProject(QString());
    }
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
    activateGitRepo(cmbGitDir->currentText().trimmed(), isGitAutoFetchEnabled());
    refreshGitGoalsTable();
    updateGitConsoleCwdLabel();
    if (gitDiffReminderEnabled && gitDiffReminderTimer && !gitDiffReminderTimer->isActive()) {
        gitDiffReminderTimer->start();
    }
}

void MainWindow::activateGitRepo(const QString &repoDir, bool fetchRemote) {
    if (repoDir.isEmpty() || !QDir(repoDir).exists()) {
        refreshGitPendingStatusBar();
        return;
    }

    onGitRefreshBranchesClicked(fetchRemote);

    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    syncChildGoalEndDatesFromParents(goals);
    syncParentStartDatesFromLeaves(goals);
    saveGitGoals(repoDir, goals);
    refreshGitPendingStatusBar();
}

void MainWindow::onGitBranchSelectionChanged() {
    const QString repoDir = cmbGitDir->currentText().trimmed();
    if (repoDir.isEmpty() || !QDir(repoDir).exists()) {
        return;
    }
    onGitRefreshLogClicked();
    updateGitGoalBranchHighlights();
}

QString MainWindow::gitRepoAlias(const QString &repoDir) const {
    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty())
        return QString();

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("GitRepoMeta"));
    settings.beginGroup(QStringLiteral("aliases"));
    const QString alias = settings.value(key).toString().trimmed();
    settings.endGroup();
    settings.endGroup();
    return alias;
}

void MainWindow::saveGitRepoAlias(const QString &repoDir, const QString &alias) {
    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty())
        return;

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("GitRepoMeta"));
    settings.beginGroup(QStringLiteral("aliases"));
    if (alias.trimmed().isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key, alias.trimmed());
    }
    settings.endGroup();
    settings.endGroup();
}

void MainWindow::removeGitRepoAlias(const QString &repoDir) {
    saveGitRepoAlias(repoDir, QString());
}

QString MainWindow::gitRepoDisplayName(const QString &repoDir) const {
    const QString alias = gitRepoAlias(repoDir);
    if (!alias.isEmpty())
        return alias;

    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty())
        return QString();
    return QFileInfo(key).fileName();
}

QString MainWindow::gitRepoMainProjectPath() const {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("GitRepoMeta"));
    const QString path = settings.value(QStringLiteral("mainProject")).toString().trimmed();
    settings.endGroup();
    return gitGoalsRepoKey(path);
}

void MainWindow::setGitRepoMainProject(const QString &repoDir) {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.beginGroup(QStringLiteral("GitRepoMeta"));
    const QString key = gitGoalsRepoKey(repoDir);
    if (key.isEmpty()) {
        settings.remove(QStringLiteral("mainProject"));
    } else {
        settings.setValue(QStringLiteral("mainProject"), key);
    }
    settings.endGroup();
}

bool MainWindow::isGitRepository(const QString &workDir) const {
    if (workDir.trimmed().isEmpty())
        return false;
    return QFile::exists(QDir(workDir.trimmed()).absoluteFilePath(QStringLiteral(".git")));
}

QStringList MainWindow::fetchTodayCommitSubjects(const QString &workDir) const {
    QStringList todayCommits;
    if (workDir.trimmed().isEmpty())
        return todayCommits;

    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    QStringList args;
    args << QStringLiteral("log")
         << QStringLiteral("--all")
         << QStringLiteral("--pretty=format:%h - %cd : %s (%an)")
         << QStringLiteral("--date=short");

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(), args);
    if (!finishGitProcess(process, 15000)) {
        return todayCommits;
    }

    const QString output = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = output.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif

    for (const QString &item : lines) {
        const int dashIdx = item.indexOf(QStringLiteral(" - "));
        const int colonIdx = item.indexOf(QStringLiteral(" : "));
        if (dashIdx == -1 || colonIdx == -1)
            continue;

        const QString datePart = item.mid(dashIdx + 3, colonIdx - (dashIdx + 3)).trimmed();
        if (datePart == today) {
            QString subject = item.mid(colonIdx + 3);
            const int authorIdx = subject.lastIndexOf(QStringLiteral(" ("));
            if (authorIdx != -1)
                subject = subject.left(authorIdx);
            todayCommits.prepend(subject.trimmed());
        } else if (datePart < today) {
            break;
        }
    }

    return todayCommits;
}

void MainWindow::refreshGitRepoMetaTable() {
    if (!tblGitRepoMeta)
        return;

    gitRepoMetaRefreshing = true;
    tblGitRepoMeta->blockSignals(true);
    tblGitRepoMeta->setRowCount(0);

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QStringList history = settings.value(QStringLiteral("GitHistory")).toStringList();
    const QString mainPath = gitRepoMainProjectPath();

    for (const QString &rawPath : history) {
        const QString absPath = gitGoalsRepoKey(rawPath);
        if (absPath.isEmpty())
            continue;

        const int row = tblGitRepoMeta->rowCount();
        tblGitRepoMeta->insertRow(row);

        QTableWidgetItem *pathItem = new QTableWidgetItem(absPath);
        pathItem->setFlags(pathItem->flags() & ~Qt::ItemIsEditable);
        pathItem->setToolTip(absPath);
        pathItem->setData(Qt::UserRole, absPath);
        tblGitRepoMeta->setItem(row, 0, pathItem);

        QTableWidgetItem *aliasItem = new QTableWidgetItem(gitRepoAlias(absPath));
        aliasItem->setData(Qt::UserRole, absPath);
        tblGitRepoMeta->setItem(row, 1, aliasItem);

        QCheckBox *mainCb = new QCheckBox();
        mainCb->setChecked(absPath == mainPath);
        mainCb->setToolTip(QStringLiteral("勾选后，复制到日报时使用此仓库的工作目标标题和完成度"));

        QWidget *centerWidget = new QWidget();
        QHBoxLayout *lay = new QHBoxLayout(centerWidget);
        lay->addWidget(mainCb);
        lay->setAlignment(Qt::AlignCenter);
        lay->setContentsMargins(0, 0, 0, 0);
        tblGitRepoMeta->setCellWidget(row, 2, centerWidget);

        connect(mainCb, &QCheckBox::toggled, this, [this, absPath](bool checked) {
            if (gitRepoMetaRefreshing)
                return;

            if (checked) {
                setGitRepoMainProject(absPath);
                for (int r = 0; r < tblGitRepoMeta->rowCount(); ++r) {
                    QWidget *widget = tblGitRepoMeta->cellWidget(r, 2);
                    if (!widget)
                        continue;
                    QCheckBox *cb = widget->findChild<QCheckBox *>();
                    if (!cb)
                        continue;
                    const QTableWidgetItem *pathItem = tblGitRepoMeta->item(r, 0);
                    const QString rowPath = pathItem ? pathItem->data(Qt::UserRole).toString() : QString();
                    if (rowPath != absPath && cb->isChecked()) {
                        gitRepoMetaRefreshing = true;
                        cb->setChecked(false);
                        gitRepoMetaRefreshing = false;
                    }
                }
            } else if (gitRepoMainProjectPath() == absPath) {
                setGitRepoMainProject(QString());
            }
        });
    }

    tblGitRepoMeta->blockSignals(false);
    gitRepoMetaRefreshing = false;
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
        g.difficulty = settings.value("difficulty", 0).toInt();
        if (!g.parentId.isEmpty()) {
            if (g.difficulty < 1) {
                g.difficulty = 3; // 默认 3 星（约 15000 行变更）
            } else if (g.difficulty > 5) {
                // 旧版半格存储 → 整星（四舍五入）
                g.difficulty = qMax(1, static_cast<int>(std::lround(g.difficulty * 0.5)));
            }
        }
        if (!g.id.isEmpty() && !g.title.isEmpty()) {
            goals.append(g);
        }
    }
    settings.endArray();
    settings.endGroup();
    settings.endGroup();
    return goals;
}

QString MainWindow::formatDifficultyStars(int starCount) const {
    return QString::number(qMax(1, starCount)) + QStringLiteral(" 星");
}

QPixmap MainWindow::gitDifficultyStarPixmap(int starCount, int starSize) const {
    starCount = qMax(1, starCount);
    const int gap = 2;
    const int width = starCount * starSize + qMax(0, starCount - 1) * gap;
    const int height = starSize;

    QPixmap pixmap(width, height);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor fillColor(255, 193, 7);

    for (int i = 0; i < starCount; ++i) {
        const int x = i * (starSize + gap);
        const QRect rect(x, 0, starSize, starSize);
        const QPointF center = rect.center();
        const qreal outerR = qMin(rect.width(), rect.height()) * 0.46;
        const qreal innerR = outerR * 0.42;

        QPainterPath path;
        for (int j = 0; j < 10; ++j) {
            const qreal angle = -M_PI_2 + j * M_PI / 5.0;
            const qreal r = (j % 2 == 0) ? outerR : innerR;
            const QPointF pt(center.x() + r * std::cos(angle), center.y() + r * std::sin(angle));
            if (j == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        path.closeSubpath();
        painter.fillPath(path, fillColor);
    }

    return pixmap;
}

QDate MainWindow::gitGoalEffectiveStartDate(const GitWorkGoal &goal,
                                            const QList<GitWorkGoal> &goals) const {
    if (!goal.startDate.isEmpty()) {
        const QDate d = QDate::fromString(goal.startDate, QStringLiteral("yyyy-MM-dd"));
        if (d.isValid()) {
            return d;
        }
    }
    if (!goal.parentId.isEmpty()) {
        const GitWorkGoal *parent = gitGoalById(goals, goal.parentId);
        if (parent) {
            return gitGoalEffectiveStartDate(*parent, goals);
        }
    }
    return QDate();
}

int MainWindow::gitBranchDiffLineCountVsMain(const QString &repoDir, const QString &branchRef,
                                             const QString &mainBranch) const {
    if (repoDir.isEmpty() || branchRef.isEmpty() || mainBranch.isEmpty()) {
        return 0;
    }

    const QString branch = normalizeLocalBranchRef(branchRef);
    const QString main = normalizeLocalBranchRef(mainBranch);
    if (branch.isEmpty() || main.isEmpty() || branch == main) {
        return 0;
    }
    if (!gitBranchExists(repoDir, branch) || !gitBranchExists(repoDir, main)) {
        return 0;
    }

    QStringList args;
    args << QStringLiteral("diff")
         << QStringLiteral("--numstat")
         << QStringLiteral("%1...%2").arg(main, branch);

    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(PlatformPrefs::gitBinary(), args);
    if (!finishGitProcess(process, 10000) || process.exitCode() != 0) {
        return 0;
    }

    const QString output = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = output.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif

    int total = 0;
    for (const QString &line : lines) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const QStringList parts = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
#else
        const QStringList parts = line.split(QLatin1Char('\t'), QString::KeepEmptyParts);
#endif
        if (parts.size() < 2) {
            continue;
        }
        if (parts[0] == QStringLiteral("-")) {
            continue;
        }
        bool okIns = false;
        bool okDel = false;
        const int ins = parts[0].toInt(&okIns);
        const int del = parts[1].toInt(&okDel);
        if (okIns) {
            total += ins;
        }
        if (okDel) {
            total += del;
        }
    }
    return total;
}

int MainWindow::gitGoalActualDiffLines(const QString &repoDir, const GitWorkGoal &goal) const {
    if (goal.branchName.isEmpty()) {
        return 0;
    }
    const QString mainBranch = resolveGitMainBranch(repoDir);
    if (mainBranch.isEmpty()) {
        return 0;
    }
    return gitBranchDiffLineCountVsMain(repoDir, goal.branchName, mainBranch);
}

bool MainWindow::syncGoalDifficultyFromDiffLines(const QString &repoDir, QList<GitWorkGoal> &goals) {
    if (repoDir.isEmpty()) {
        return false;
    }

    bool changed = false;
    for (GitWorkGoal &g : goals) {
        if (g.parentId.isEmpty() || gitGoalHasChildren(g.id, goals)) {
            continue;
        }
        const int presetStars = qMax(1, g.difficulty);
        const int actualLines = gitGoalActualDiffLines(repoDir, g);
        const int actualStars = gitLinesToStarCount(actualLines);
        if (actualStars > presetStars) {
            const int raised = qMin(actualStars, kGitGoalMaxAutoStars);
            if (raised > presetStars) {
                g.difficulty = raised;
                changed = true;
            }
        }
    }
    return changed;
}

void MainWindow::collectGitGoalDescendantIds(const QString &parentId, const QList<GitWorkGoal> &goals,
                                              QStringList &outIds) const {
    for (const GitWorkGoal &g : goals) {
        if (g.parentId != parentId) {
            continue;
        }
        outIds.append(g.id);
        collectGitGoalDescendantIds(g.id, goals, outIds);
    }
}

void MainWindow::collectGitGoalLeafDescendantIds(const QString &parentId, const QList<GitWorkGoal> &goals,
                                                 QStringList &outIds) const {
    for (const GitWorkGoal &g : goals) {
        if (g.parentId != parentId) {
            continue;
        }
        if (gitGoalHasChildren(g.id, goals)) {
            collectGitGoalLeafDescendantIds(g.id, goals, outIds);
        } else {
            outIds.append(g.id);
        }
    }
}

bool MainWindow::gitGoalHasChildren(const QString &goalId, const QList<GitWorkGoal> &goals) const {
    for (const GitWorkGoal &g : goals) {
        if (g.parentId == goalId) {
            return true;
        }
    }
    return false;
}

int MainWindow::gitGoalDisplayDifficulty(const GitWorkGoal &goal,
                                         const QList<GitWorkGoal> &goals) const {
    if (!gitGoalHasChildren(goal.id, goals)) {
        return qMax(1, goal.difficulty);
    }

    int sum = 0;
    for (const GitWorkGoal &g : goals) {
        if (g.parentId != goal.id) {
            continue;
        }
        sum += gitGoalDisplayDifficulty(g, goals);
    }
    return qMax(1, sum);
}

bool MainWindow::syncParentStartDatesFromLeaves(QList<GitWorkGoal> &goals) {
    bool changed = false;
    for (GitWorkGoal &g : goals) {
        if (!gitGoalHasChildren(g.id, goals)) {
            continue;
        }

        QStringList leafIds;
        collectGitGoalLeafDescendantIds(g.id, goals, leafIds);

        QDate earliest;
        bool anyStarted = false;
        for (const QString &leafId : leafIds) {
            const GitWorkGoal *leaf = gitGoalById(goals, leafId);
            if (!leaf || leaf->startDate.isEmpty()) {
                continue;
            }
            const QDate d = QDate::fromString(leaf->startDate, QStringLiteral("yyyy-MM-dd"));
            if (!d.isValid()) {
                continue;
            }
            if (leaf->started) {
                anyStarted = true;
            }
            if (!earliest.isValid() || d < earliest) {
                earliest = d;
            }
        }

        if (!earliest.isValid()) {
            continue;
        }

        const QString dateStr = earliest.toString(QStringLiteral("yyyy-MM-dd"));
        if (g.startDate != dateStr) {
            g.startDate = dateStr;
            changed = true;
        }
        if (anyStarted && !g.started) {
            g.started = true;
            changed = true;
        }
    }
    return changed;
}

bool MainWindow::isGitSubGoalCompleted(const GitWorkGoal &goal) const {
    return !goal.actualDate.trimmed().isEmpty();
}

GitRootProgressInfo MainWindow::calcRootGoalProgress(const QString &repoDir, const GitWorkGoal &root,
                                                     const QList<GitWorkGoal> &goals) const {
    GitRootProgressInfo info;

    const QDate planEnd = QDate::fromString(root.endDate, QStringLiteral("yyyy-MM-dd"));
    const QDate planStart = QDate::fromString(root.startDate, QStringLiteral("yyyy-MM-dd"));
    const QDate today = QDate::currentDate();
    double timePercent = 0.0;

    if (planEnd.isValid()) {
        if (!root.started) {
            if (planStart.isValid() && today > planStart) {
                int totalDays = static_cast<int>(planStart.daysTo(planEnd));
                if (totalDays < 1) {
                    totalDays = 1;
                }
                const int overdueDays = static_cast<int>(planStart.daysTo(today));
                timePercent = qMin(30.0, 30.0 * overdueDays / totalDays);
            }
        } else {
            QDate effectiveStart = planStart.isValid() ? planStart : today;
            if (planEnd <= effectiveStart) {
                timePercent = today >= planEnd ? 100.0 : 0.0;
            } else if (today <= effectiveStart) {
                timePercent = 0.0;
            } else if (today >= planEnd) {
                timePercent = 100.0;
            } else {
                const int totalDays = static_cast<int>(effectiveStart.daysTo(planEnd));
                const int elapsed = static_cast<int>(effectiveStart.daysTo(today));
                timePercent = 100.0 * static_cast<double>(elapsed)
                              / static_cast<double>(qMax(1, totalDays));
            }
        }
    }
    info.timePercent = qBound(0.0, timePercent, 100.0);

    QStringList descendantIds;
    collectGitGoalLeafDescendantIds(root.id, goals, descendantIds);
    info.descendantCount = descendantIds.size();
    info.hasSubGoals = info.descendantCount > 0;

    double taskPercent = 0.0;
    if (info.hasSubGoals) {
        for (const QString &id : descendantIds) {
            const GitWorkGoal *sub = gitGoalById(goals, id);
            if (!sub) {
                continue;
            }

            const double presetStars = static_cast<double>(qMax(1, sub->difficulty));
            const int actualLines = repoDir.isEmpty() ? 0 : gitGoalActualDiffLines(repoDir, *sub);
            const double actualStars = gitLinesToStarWeight(actualLines);
            const double effectiveExpected = qMax(presetStars, actualStars);
            const bool completed = isGitSubGoalCompleted(*sub);

            const double denom = completed ? qMax(actualStars, 1.0) : effectiveExpected;
            info.totalWeight += denom;
            info.completedWeight += actualStars;
            if (completed) {
                info.completedCount++;
            }
        }
        if (info.totalWeight > 0.0) {
            taskPercent = 100.0 * info.completedWeight / info.totalWeight;
        }
    }
    info.taskPercent = qBound(0.0, taskPercent, 100.0);

    if (info.hasSubGoals) {
        info.totalPercent = qBound(0.0, info.timePercent * 0.35 + info.taskPercent * 0.65, 100.0);
    } else {
        info.totalPercent = info.timePercent;
    }
    return info;
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

int MainWindow::markGoalsCompletedForDeletedBranches(const QString &repoDir) {
    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    bool changed = false;
    int updated = 0;
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    for (GitWorkGoal &g : goals) {
        if (g.branchName.isEmpty() || !g.actualDate.isEmpty()) {
            continue;
        }
        const QString goalBranch = normalizeLocalBranchRef(g.branchName);
        if (goalBranch.isEmpty()) {
            continue;
        }
        if (gitBranchExists(repoDir, goalBranch)) {
            continue;
        }

        g.actualDate = today;
        changed = true;
        updated++;
        txtGitLog->append(QStringLiteral("<font color='green'>[工作目标] 分支 %1 已删除，已填写实际完成日期: %2（%3）</font>")
                              .arg(goalBranch, today, g.title));
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
        settings.setValue("difficulty", g.parentId.isEmpty() ? 0 : qMax(1, g.difficulty));
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

void MainWindow::appendGitGoalTableRow(const QString &repoDir, const GitWorkGoal &g,
                                        const QList<GitWorkGoal> &allGoals, int depth,
                                        bool hasChildren, bool childrenCollapsed) {
    const int row = tblGitGoals->rowCount();
    tblGitGoals->insertRow(row);

    static const int kTreeIndentPerLevel = 22;
    static const int kToggleButtonWidth = 26;

    QWidget *treeLeadCell = new QWidget();
    QHBoxLayout *treeLeadLayout = new QHBoxLayout(treeLeadCell);
    treeLeadLayout->setContentsMargins(0, 0, 0, 0);
    treeLeadLayout->setSpacing(0);

    if (depth > 0) {
        QWidget *levelIndent = new QWidget();
        levelIndent->setFixedWidth(depth * kTreeIndentPerLevel);
        treeLeadLayout->addWidget(levelIndent);
    }

    if (hasChildren) {
        QPushButton *btnToggle = new QPushButton(childrenCollapsed ? QStringLiteral("▶")
                                                                   : QStringLiteral("▼"));
        btnToggle->setFixedSize(kToggleButtonWidth, 22);
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
        treeLeadLayout->addWidget(btnToggle);
    } else if (depth > 0) {
        QWidget *toggleAlign = new QWidget();
        toggleAlign->setFixedWidth(kToggleButtonWidth);
        treeLeadLayout->addWidget(toggleAlign);
    }

    treeLeadLayout->addStretch();
    tblGitGoals->setCellWidget(row, 0, treeLeadCell);

    QTableWidgetItem *titleItem = new QTableWidgetItem(g.title);
    titleItem->setData(Qt::UserRole, g.id);
    tblGitGoals->setItem(row, 1, titleItem);

    const bool isRoot = g.parentId.isEmpty();
    if (isRoot) {
        const GitRootProgressInfo progress = calcRootGoalProgress(repoDir, g, allGoals);
        QProgressBar *bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(qRound(progress.totalPercent));
        bar->setFormat(QStringLiteral("%p%"));
        bar->setFixedHeight(18);
        QString tip = QStringLiteral("综合进度 %1%\n时间进度 %2%")
                          .arg(QString::number(progress.totalPercent, 'f', 1))
                          .arg(QString::number(progress.timePercent, 'f', 1));
        if (progress.hasSubGoals) {
            tip += QStringLiteral("\n子任务进度 %1%（%2/%3 已完成，行数加权 %4/%5）")
                       .arg(QString::number(progress.taskPercent, 'f', 1))
                       .arg(progress.completedCount)
                       .arg(progress.descendantCount)
                       .arg(QString::number(progress.completedWeight, 'f', 1))
                       .arg(QString::number(progress.totalWeight, 'f', 1));
            tip += QStringLiteral("\n未完成最下级子任务按相对主分支的 +/− 行数计分（%1 行 = 1 星）；已完成按实际星级计权")
                       .arg(kGitGoalLinesPerStar);
        } else {
            tip += QStringLiteral("\n无子任务，进度仅按时间计算");
        }
        bar->setToolTip(tip);
        tblGitGoals->setCellWidget(row, 2, bar);
        tblGitGoals->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
    } else {
        tblGitGoals->setItem(row, 2, new QTableWidgetItem(QStringLiteral("—")));
        const bool hasChildren = gitGoalHasChildren(g.id, allGoals);
        const int starCount = hasChildren ? gitGoalDisplayDifficulty(g, allGoals)
                                          : qMax(1, g.difficulty);
        const int actualDiffLines = (!hasChildren && !repoDir.isEmpty())
                                        ? gitGoalActualDiffLines(repoDir, g)
                                        : 0;
        const QString mainBranch = repoDir.isEmpty() ? QString() : resolveGitMainBranch(repoDir);
        QTableWidgetItem *diffItem = new QTableWidgetItem();
        diffItem->setData(Qt::DecorationRole, gitDifficultyStarPixmap(starCount, 14));
        diffItem->setText(formatDifficultyStars(starCount));
        if (hasChildren) {
            diffItem->setToolTip(QStringLiteral("由下级子目标星级相加，合计 %1 星").arg(starCount));
        } else {
            diffItem->setToolTip(
                QStringLiteral("预计 %1 星（约 %2 行）；分支 %3 相对主分支 %4 共 %5 行（约 %6 星，+与−之和）\n"
                               "%7 行 = 1 星；超出预设时自动抬升星级（最高 %8 星）")
                    .arg(starCount)
                    .arg(starCount * kGitGoalLinesPerStar)
                    .arg(g.branchName.isEmpty() ? QStringLiteral("（未绑定）") : g.branchName)
                    .arg(mainBranch.isEmpty() ? QStringLiteral("（未配置）") : mainBranch)
                    .arg(actualDiffLines)
                    .arg(gitLinesToStarCount(actualDiffLines))
                    .arg(kGitGoalLinesPerStar)
                    .arg(kGitGoalMaxAutoStars));
        }
        tblGitGoals->setItem(row, 3, diffItem);
    }

    QString parentDisplay = QStringLiteral("(无)");
    if (!g.parentId.isEmpty()) {
        const QString parentTitle = gitGoalTitleById(allGoals, g.parentId);
        parentDisplay = parentTitle.isEmpty() ? QStringLiteral("(已删除)") : parentTitle;
    }
    tblGitGoals->setItem(row, 4, new QTableWidgetItem(parentDisplay));

    QString startDisplay;
    if (g.startDate.isEmpty()) {
        startDisplay = isRoot ? QStringLiteral("未开始") : QStringLiteral("—");
    } else if (!g.started) {
        startDisplay = g.startDate + QStringLiteral(" (计划)");
    } else {
        startDisplay = g.startDate;
    }
    tblGitGoals->setItem(row, 5, new QTableWidgetItem(startDisplay));
    tblGitGoals->setItem(row, 6, new QTableWidgetItem(g.endDate.isEmpty() ? QStringLiteral("—") : g.endDate));
    const QString actual = g.actualDate.isEmpty() ? QStringLiteral("—") : g.actualDate;
    tblGitGoals->setItem(row, 7, new QTableWidgetItem(actual));
    const QString branchDisplay = g.branchName.isEmpty() ? QStringLiteral("—") : g.branchName;
    tblGitGoals->setItem(row, 8, new QTableWidgetItem(branchDisplay));

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
    tblGitGoals->setItem(row, 9, remarkItem);

    applyGitGoalRowBranchHighlight(row, gitGoalBranchHighlight(g));
}

MainWindow::GitGoalBranchHighlight MainWindow::gitGoalBranchHighlight(const GitWorkGoal &goal) const {
    if (goal.branchName.isEmpty()) {
        return GitGoalBranchHighlight::None;
    }
    const QString goalBranch = normalizeLocalBranchRef(goal.branchName);
    if (goalBranch.isEmpty()) {
        return GitGoalBranchHighlight::None;
    }

    QString currentBranch;
    if (lblGitCurrentBranch) {
        currentBranch = normalizeLocalBranchRef(lblGitCurrentBranch->text());
        if (currentBranch == QStringLiteral("(未检出)") || currentBranch == QStringLiteral("(未知)")) {
            currentBranch.clear();
        }
    }

    QString targetBranch;
    if (cmbGitBranches) {
        targetBranch = normalizeLocalBranchRef(cmbGitBranches->currentText());
    }

    const bool matchCurrent = !currentBranch.isEmpty()
                              && currentBranch.compare(goalBranch, Qt::CaseInsensitive) == 0;
    const bool matchTarget = !targetBranch.isEmpty()
                             && targetBranch.compare(goalBranch, Qt::CaseInsensitive) == 0;

    if (matchCurrent && matchTarget) {
        return GitGoalBranchHighlight::Both;
    }
    if (matchCurrent) {
        return GitGoalBranchHighlight::CurrentBranch;
    }
    if (matchTarget) {
        return GitGoalBranchHighlight::TargetBranch;
    }
    return GitGoalBranchHighlight::None;
}

void MainWindow::applyGitGoalRowBranchHighlight(int row, GitGoalBranchHighlight highlight) {
    if (!tblGitGoals || row < 0 || row >= tblGitGoals->rowCount()) {
        return;
    }

    QColor bg;
    QString tipSuffix;
    switch (highlight) {
    case GitGoalBranchHighlight::CurrentBranch:
        bg = QColor(QStringLiteral("#c8e6c9"));
        tipSuffix = QStringLiteral("（与 Git 操作中的当前分支一致）");
        break;
    case GitGoalBranchHighlight::TargetBranch:
        bg = QColor(QStringLiteral("#e3f2fd"));
        tipSuffix = QStringLiteral("（与 Git 操作中的目标分支一致）");
        break;
    case GitGoalBranchHighlight::Both:
        bg = QColor(QStringLiteral("#fff9c4"));
        tipSuffix = QStringLiteral("（与 Git 操作中的当前、目标分支均一致）");
        break;
    default:
        return;
    }

    const QBrush brush(bg);
    for (int col = 0; col < tblGitGoals->columnCount(); ++col) {
        if (QTableWidgetItem *item = tblGitGoals->item(row, col)) {
            item->setBackground(brush);
        }
    }

    if (QTableWidgetItem *branchItem = tblGitGoals->item(row, 8)) {
        const QString branchText = branchItem->text();
        if (branchText != QStringLiteral("—")) {
            branchItem->setToolTip(branchText + tipSuffix);
        }
    }
}

void MainWindow::updateGitGoalBranchHighlights() {
    if (!tblGitGoals) {
        return;
    }

    for (int row = 0; row < tblGitGoals->rowCount(); ++row) {
        for (int col = 0; col < tblGitGoals->columnCount(); ++col) {
            if (QTableWidgetItem *item = tblGitGoals->item(row, col)) {
                item->setBackground(QBrush());
                if (col == 8 && item->text() != QStringLiteral("—")) {
                    item->setToolTip(QString());
                }
            }
        }

        QTableWidgetItem *branchItem = tblGitGoals->item(row, 8);
        if (!branchItem) {
            continue;
        }

        GitWorkGoal goal;
        goal.branchName = branchItem->text();
        if (goal.branchName == QStringLiteral("—")) {
            goal.branchName.clear();
        }
        applyGitGoalRowBranchHighlight(row, gitGoalBranchHighlight(goal));
    }
}

void MainWindow::refreshGitGoalsTable() {
    if (!tblGitGoals) return;

    const QString repoDir = cmbGitDir ? cmbGitDir->currentText().trimmed() : QString();
    QList<GitWorkGoal> goals = loadGitGoals(repoDir);
    if (!repoDir.isEmpty() && QDir(repoDir).exists()) {
        bool changed = syncParentStartDatesFromLeaves(goals);
        if (syncGoalDifficultyFromDiffLines(repoDir, goals)) {
            changed = true;
        }
        if (changed) {
            saveGitGoals(repoDir, goals);
        }
    }
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
        appendGitGoalTableRow(repoDir, *g, goals, depth, hasChildren, childrenCollapsed);

        for (const QString &childId : children) {
            visitGoal(childId, depth + 1);
        }
    };

    int maxDepth = 0;
    std::function<void(const QString &, int)> measureDepth;
    measureDepth = [&](const QString &goalId, int depth) {
        maxDepth = qMax(maxDepth, depth);
        for (const QString &childId : childrenByParent.value(goalId)) {
            measureDepth(childId, depth + 1);
        }
    };
    for (const QString &rootId : rootIds) {
        measureDepth(rootId, 0);
    }

    for (const QString &rootId : rootIds) {
        visitGoal(rootId, 0);
    }

    tblGitGoals->resizeColumnsToContents();
    const int treeColWidth = 26 + 8 + maxDepth * 22;
    tblGitGoals->setColumnWidth(0, qMax(40, treeColWidth));
    tblGitGoals->setColumnWidth(2, 110);
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

QString MainWindow::translateGoalTitleToEnglish(const QString &title)
{
    const QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    // Offline only: avoid sync HTTP / QEventLoop that can freeze the UI for up to 8s.
    txtGitLog->append(QStringLiteral("[工作目标] 使用拼音/离线命名分支。"));
    return chineseTitleToPinyinSlug(trimmed);
}

QString MainWindow::suggestBranchNameFromTitle(const QString &title) {
    return translateGoalTitleToEnglish(title);
}

QString MainWindow::gitCheckedOutBranch(const QString &repoDir) const {
    const QString workDir = repoDir.trimmed();
    if (workDir.isEmpty() || !QDir(workDir).exists()) {
        return QString();
    }

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.start(PlatformPrefs::gitBinary(),
                  QStringList() << QStringLiteral("rev-parse") << QStringLiteral("--abbrev-ref") << QStringLiteral("HEAD"));
    if (!process.waitForFinished(10000)) {
        return QString();
    }
    const QString branch = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput()).trimmed();
    if (branch.isEmpty() || branch == QStringLiteral("HEAD")) {
        return QString();
    }
    return branch;
}

QStringList MainWindow::gitBranchHintLinesFromCombo() const {
    QStringList hints;
    if (!cmbGitBranches) {
        return hints;
    }
    for (int i = 0; i < cmbGitBranches->count(); ++i) {
        hints.append(cmbGitBranches->itemText(i));
    }
    return hints;
}

bool MainWindow::branchNameInBranchHints(const QStringList &hints, const QString &name) const {
    const QString target = name.trimmed();
    if (target.isEmpty()) {
        return false;
    }
    const QString remoteRef = QStringLiteral("remotes/origin/") + target;
    for (QString line : hints) {
        line = line.trimmed();
        if (line.startsWith(QStringLiteral("* "))) {
            line = line.mid(2).trimmed();
        }
        if (line.compare(target, Qt::CaseInsensitive) == 0
            || line.compare(remoteRef, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool MainWindow::selectGitBranchInCombo(const QString &branchName) {
    if (!cmbGitBranches) {
        return false;
    }
    const QString target = normalizeLocalBranchRef(branchName);
    if (target.isEmpty()) {
        return false;
    }

    int localMatch = -1;
    int remoteMatch = -1;
    for (int i = 0; i < cmbGitBranches->count(); ++i) {
        const QString line = cmbGitBranches->itemText(i).trimmed();
        if (normalizeLocalBranchRef(line).compare(target, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (line.startsWith(QStringLiteral("remotes/"))) {
            if (remoteMatch < 0) {
                remoteMatch = i;
            }
        } else {
            localMatch = i;
            break;
        }
    }

    const int index = localMatch >= 0 ? localMatch : remoteMatch;
    if (index < 0) {
        return false;
    }
    cmbGitBranches->setCurrentIndex(index);
    return true;
}

QString MainWindow::detectDefaultMainBranch(const QString &repoDir, const QStringList &branchHints) const {
    auto branchPresent = [&](const QString &name) -> bool {
        if (!repoDir.isEmpty() && gitBranchExists(repoDir, name)) {
            return true;
        }
        return branchNameInBranchHints(branchHints, name);
    };

    if (branchPresent(QStringLiteral("main"))) {
        return QStringLiteral("main");
    }
    if (branchPresent(QStringLiteral("master"))) {
        return QStringLiteral("master");
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(PlatformPrefs::gitBinary(),
                  QStringList() << QStringLiteral("symbolic-ref")
                                << QStringLiteral("refs/remotes/origin/HEAD"));
    if (finishGitProcess(process, 5000) && process.exitCode() == 0) {
        const QString sym = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput()).trimmed();
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

void MainWindow::saveGitMainBranchSetting(const QString &repoDir, const QString &branchName) {
    const QString branch = branchName.trimmed();
    if (branch.isEmpty()) {
        return;
    }

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(QStringLiteral("GitDefaultMainBranch"), branch);

    const QString key = gitGoalsRepoKey(repoDir);
    if (!key.isEmpty()) {
        settings.beginGroup(QStringLiteral("GitMainBranch"));
        settings.setValue(key, branch);
        settings.endGroup();
    }
}

void MainWindow::syncGitMainBranchSetting(const QString &repoDir) {
    if (repoDir.isEmpty() || !QDir(repoDir).exists()) {
        return;
    }

    const QStringList branchHints = gitBranchHintLinesFromCombo();

    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    QString branch;
    const QString key = gitGoalsRepoKey(repoDir);
    if (!key.isEmpty()) {
        settings.beginGroup(QStringLiteral("GitMainBranch"));
        branch = settings.value(key).toString().trimmed();
        settings.endGroup();
    }
    if (branch.isEmpty()) {
        branch = settings.value(QStringLiteral("GitDefaultMainBranch")).toString().trimmed();
    }

    auto branchPresent = [&](const QString &name) -> bool {
        if (name.isEmpty()) {
            return false;
        }
        if (gitBranchExists(repoDir, name)) {
            return true;
        }
        return branchNameInBranchHints(branchHints, name);
    };

    if (!branch.isEmpty() && !branchPresent(branch)) {
        const QString remembered = branch;
        branch.clear();
        if (txtGitLog) {
            txtGitLog->append(QStringLiteral("<font color='orange'>[主分支] 记忆的 %1 在当前仓库不存在，已按分支列表自动识别</font>")
                                  .arg(remembered));
        }
    }

    if (branch.isEmpty()) {
        branch = detectDefaultMainBranch(repoDir, branchHints);
    }
    if (!branch.isEmpty()) {
        saveGitMainBranchSetting(repoDir, branch);
    }
}

QString MainWindow::resolveGitMainBranch(const QString &repoDir) const {
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QString key = gitGoalsRepoKey(repoDir);
    if (!key.isEmpty()) {
        settings.beginGroup(QStringLiteral("GitMainBranch"));
        const QString perRepo = settings.value(key).toString().trimmed();
        settings.endGroup();
        if (!perRepo.isEmpty()) {
            return perRepo;
        }
    }
    const QString globalDefault = settings.value(QStringLiteral("GitDefaultMainBranch")).toString().trimmed();
    if (!globalDefault.isEmpty()) {
        return globalDefault;
    }
    return detectDefaultMainBranch(repoDir, gitBranchHintLinesFromCombo());
}

QString MainWindow::gitWorktreePathUsingBranch(const QString &repoDir, const QString &branchName) const
{
    return GitWorktreeRunner::pathForBranch(repoDir, branchName);
}

bool MainWindow::gitBranchExists(const QString &repoDir, const QString &branchName) const {
    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(PlatformPrefs::gitBinary(), QStringList() << QStringLiteral("show-ref")
                                                            << QStringLiteral("--verify")
                                                            << QStringLiteral("--quiet")
                                                            << QStringLiteral("refs/heads/") + branchName);
    if (!finishGitProcess(process, 5000)) {
        return false;
    }
    return process.exitCode() == 0;
}

bool MainWindow::checkoutGitBranch(const QString &repoDir, const QString &branchName) {
    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.setProgram(PlatformPrefs::gitBinary());
    process.setArguments(QStringList() << QStringLiteral("checkout") << branchName);
    process.start();
    if (!finishGitProcess(process, 30000)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[Git] 切换分支超时</font>"));
        return false;
    }
    const QString stderrData = PlatformPrefs::decodeProcessOutput(process.readAllStandardError());
    if (process.exitCode() != 0) {
        txtGitLog->append(QStringLiteral("<font color='red'>[Git] 切换分支失败: %1</font>").arg(stderrData.trimmed()));
        return false;
    }
    return true;
}

bool MainWindow::createGitBranch(const QString &repoDir, const QString &branchName) {
    const QString mainBranch = resolveGitMainBranch(repoDir);
    if (mainBranch.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("工作目标"),
                             QStringLiteral("未配置主分支。请在仓库区域设置主分支并保存。"));
        return false;
    }
    if (!gitBranchExists(repoDir, mainBranch)) {
        QMessageBox::warning(this, QStringLiteral("工作目标"),
                             QStringLiteral("主分支 %1 在本地不存在，请先拉取或修正主分支配置。").arg(mainBranch));
        return false;
    }

    txtGitLog->append(QStringLiteral("<font color='cyan'>[工作目标] 从主分支 %1 创建: %2</font>")
                          .arg(mainBranch, branchName));

    if (!checkoutGitBranch(repoDir, mainBranch)) {
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.setProgram(PlatformPrefs::gitBinary());
    process.setArguments(QStringList() << QStringLiteral("checkout") << QStringLiteral("-b") << branchName);
    process.start();
    if (!finishGitProcess(process, 30000)) {
        txtGitLog->append(QStringLiteral("<font color='red'>[工作目标] 创建分支超时</font>"));
        return false;
    }
    const QString stdoutData = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput());
    const QString stderrData = PlatformPrefs::decodeProcessOutput(process.readAllStandardError());
    if (process.exitCode() != 0) {
        txtGitLog->append(QStringLiteral("<font color='red'>[工作目标] 创建分支失败: %1</font>").arg(stderrData.trimmed()));
        return false;
    }
    txtGitLog->append(QStringLiteral("<font color='green'>[工作目标] 已从 %1 创建并切换到分支: %2</font>")
                          .arg(mainBranch, branchName));
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

    QLabel *lblPlanHint = new QLabel(
        QStringLiteral("无父目标时需填写计划开始与计划结束，列表显示综合进度条；子目标 1 星 = %1 行 +/− 变更，"
                       "未完成时按分支相对主分支的行数折算星级计进度，超出预设时自动抬升星级（最高 %2 星），完成后以实际星级计权。")
            .arg(kGitGoalLinesPerStar)
            .arg(kGitGoalMaxAutoStars));
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

    QWidget *rowDifficulty = new QWidget();
    QHBoxLayout *layDifficulty = new QHBoxLayout(rowDifficulty);
    layDifficulty->setContentsMargins(0, 0, 0, 0);
    QSpinBox *spinDifficulty = new QSpinBox();
    spinDifficulty->setRange(1, 9999);
    spinDifficulty->setSingleStep(1);
    const int initialStars = goal.parentId.isEmpty() ? 3 : qMax(1, goal.difficulty);
    spinDifficulty->setValue(initialStars);
    spinDifficulty->setSuffix(QStringLiteral(" 星"));
    QLabel *lblDifficultyStars = new QLabel();
    lblDifficultyStars->setPixmap(gitDifficultyStarPixmap(initialStars, 16));
    lblDifficultyStars->setToolTip(formatDifficultyStars(initialStars));
    layDifficulty->addWidget(spinDifficulty);
    layDifficulty->addWidget(lblDifficultyStars);
    layDifficulty->addStretch();
    connect(spinDifficulty, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
            [lblDifficultyStars, this](int v) {
                lblDifficultyStars->setPixmap(gitDifficultyStarPixmap(v, 16));
                lblDifficultyStars->setToolTip(formatDifficultyStars(v));
            });

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
        rowDifficulty->setVisible(!isRoot);
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
    form->addRow(QStringLiteral("预计星级 (%1行/星):").arg(kGitGoalLinesPerStar), rowDifficulty);
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
        goal.difficulty = 0;
    } else {
        const GitWorkGoal *parent = gitGoalById(allGoals, parentId);
        if (!parent || parent->endDate.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("工作目标"),
                                 QStringLiteral("父目标尚未设置计划结束日期，请先为父目标填写计划结束。"));
            return false;
        }
        goal.endDate = parent->endDate;
        goal.difficulty = spinDifficulty->value();
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
    syncParentStartDatesFromLeaves(goals);
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
    syncParentStartDatesFromLeaves(goals);

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

void MainWindow::onGitGoalRowDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    if (row < 0 || !tblGitGoals) {
        return;
    }

    QTableWidgetItem *branchItem = tblGitGoals->item(row, 8);
    if (!branchItem) {
        return;
    }

    const QString branch = branchItem->text().trimmed();
    if (branch.isEmpty() || branch == QStringLiteral("—")) {
        return;
    }

    if (selectGitBranchInCombo(branch)) {
        txtGitLog->append(QStringLiteral("<font color='gray'>[工作目标] 已切换目标分支: %1</font>").arg(branch));
        return;
    }

    txtGitLog->append(QStringLiteral("<font color='orange'>[工作目标] 分支 %1 不在当前分支列表中，请先刷新分支。</font>")
                          .arg(branch));
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
        syncParentStartDatesFromLeaves(goals);
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
    table->setColumnCount(RegisterMapCol::ColumnCount);
    table->setHorizontalHeaderLabels(QStringList()
                                     << QStringLiteral("方向")
                                     << QStringLiteral("地址")
                                     << QStringLiteral("注释")
                                     << QStringLiteral("寄存器格式"));
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &MainWindow::onRegisterMapContextMenu);
    
    // Add some default rows for testing
    table->setRowCount(51);
    for(int i=0; i<50; i++) {
        table->setItem(i, RegisterMapCol::Direction, new QTableWidgetItem(""));
        table->setItem(i, RegisterMapCol::Address, new QTableWidgetItem(QString::number(i)));
        table->setItem(i, RegisterMapCol::Comment, new QTableWidgetItem(""));
        table->setItem(i, RegisterMapCol::Format, new QTableWidgetItem(QString::number(i)));
        applyRegisterMapRowStyle(table, i);
    }

    // Keep one blank row at bottom for direct data entry.
    table->setItem(50, RegisterMapCol::Direction, new QTableWidgetItem(""));
    table->setItem(50, RegisterMapCol::Address, new QTableWidgetItem(""));
    table->setItem(50, RegisterMapCol::Comment, new QTableWidgetItem(""));
    table->setItem(50, RegisterMapCol::Format, new QTableWidgetItem(""));
    applyRegisterMapRowStyle(table, 50);
}

void MainWindow::applyRegisterMapRowStyle(QTableWidget *table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) {
        return;
    }

    const QTableWidgetItem *dirItem = table->item(row, RegisterMapCol::Direction);
    const RegisterMapDirection dir = parseRegisterMapDirection(dirItem ? dirItem->text() : QString());

    QColor bg;
    if (dir == RegisterMapDirection::Read) {
        bg = QColor(QStringLiteral("#E8F4FD"));
    } else if (dir == RegisterMapDirection::Write) {
        bg = QColor(QStringLiteral("#FFF3E0"));
    }

    for (int col = 0; col < table->columnCount(); ++col) {
        if (!table->item(row, col)) {
            table->setItem(row, col, new QTableWidgetItem());
        }
        QTableWidgetItem *item = table->item(row, col);
        if (dir == RegisterMapDirection::Unknown) {
            item->setBackground(QBrush());
        } else {
            item->setBackground(QBrush(bg));
        }
    }
}

void MainWindow::applyRegisterMapTableStyles(QTableWidget *table)
{
    if (!table) {
        return;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        applyRegisterMapRowStyle(table, row);
    }
}

void MainWindow::setupSimulatorRegisterTable(QTableWidget *table) {
    if (!table) return;
    table->setColumnCount(SimRegisterCol::ColumnCount);
    table->setHorizontalHeaderLabels(QStringList()
                                     << QStringLiteral("方向")
                                     << QStringLiteral("地址")
                                     << QStringLiteral("描述")
                                     << QStringLiteral("值"));

    table->setColumnWidth(SimRegisterCol::Direction, 40);
    table->setColumnWidth(SimRegisterCol::Address, 50);
    table->setColumnWidth(SimRegisterCol::Description, 150);
    table->setColumnWidth(SimRegisterCol::Value, 100);

    table->horizontalHeader()->setSectionResizeMode(SimRegisterCol::Description, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, &MainWindow::onSimShowContextMenu);

    table->setRowCount(50);

    for (int i = 0; i < 50; ++i) {
        if (!table->item(i, SimRegisterCol::Direction))
            table->setItem(i, SimRegisterCol::Direction, new QTableWidgetItem(""));
        if (!table->item(i, SimRegisterCol::Address))
            table->setItem(i, SimRegisterCol::Address, new QTableWidgetItem(QString::number(i)));
        if (!table->item(i, SimRegisterCol::Description))
            table->setItem(i, SimRegisterCol::Description, new QTableWidgetItem(""));
        if (!table->item(i, SimRegisterCol::Value))
            table->setItem(i, SimRegisterCol::Value, new QTableWidgetItem("0"));
        applyRegisterMapRowStyle(table, i);
    }
    connect(table, &QTableWidget::cellChanged, this, &MainWindow::onSimTableRowChanged);
    connect(table, &QTableWidget::cellDoubleClicked, this, &MainWindow::onSimCellDoubleClicked);
}

void MainWindow::refreshSimRowDisplay(QTableWidget *table, int row)
{
    if (!table) return;
    if (row < 0 || row >= table->rowCount()) return;

    QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
    if (!addrItem || addrItem->text().isEmpty()) return;

    QTableWidgetItem *valueItem = table->item(row, SimRegisterCol::Value);
    if (valueItem && !(valueItem->flags() & Qt::ItemIsEditable)) {
        return;
    }

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
    } else if (fmt == "String") {
        const int regCount = simTableStringLengths.value(table).value(row, kDefaultStringRegisterCount);
        QVector<quint16> stringRegs;
        stringRegs.reserve(regCount);
        for (int i = 0; i < regCount; ++i) {
            stringRegs << target->getRegister(static_cast<quint16>(addr + i));
        }
        display = decodeUtf8FromRegisters(stringRegs);
    } else if (fmt.startsWith("32-bit")) {
        if (fmt == "32-bit Float") {
            display = QString::number(readFloat32FromSlave(target, addr), 'f', 2);
        } else {
            const uint32_t val32 = ((uint32_t)target->getRegister(addr) << 16) | target->getRegister(addr + 1);
            if (fmt == "32-bit Signed") display = QString::number((int32_t)val32);
            else if (fmt == "32-bit Unsigned") display = QString::number(val32);
        }
    } else if (fmt == "64-bit Float") {
        display = QString::number(readFloat64FromSlave(target, addr), 'f', 6);
    }
    if (display.isEmpty()) display = QString::number(val);

    table->blockSignals(true);
    if (!table->item(row, SimRegisterCol::Value)) table->setItem(row, SimRegisterCol::Value, new QTableWidgetItem());
    table->item(row, SimRegisterCol::Value)->setText(display);
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
            if (col == SimRegisterCol::Value) {
                const QString fmt = simTableFormats.value(table).value(row, QStringLiteral("Unsigned"));
                if (fmt == QStringLiteral("Binary"))
                    flags &= ~Qt::ItemIsEditable;
                else
                    flags |= Qt::ItemIsEditable;
            } else {
                flags &= ~Qt::ItemIsEditable;
            }
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
    
    QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
    if (!addrItem || addrItem->text().isEmpty()) return;
    quint16 addr = (quint16)addrItem->text().toUInt();
    
    if (column != SimRegisterCol::Value) {
        return;
    }

    QTableWidgetItem *valueItem = table->item(row, SimRegisterCol::Value);
    if (!valueItem) return;
    QString valStr = valueItem->text();
    bool ok = false;
    quint16 val = 0;
    
    QString fmt = simTableFormats.value(table).value(row, "Unsigned");

    auto refreshSameAddressRows = [this, table, addr]() {
        for (int r = 0; r < table->rowCount(); ++r) {
            QTableWidgetItem *a = table->item(r, SimRegisterCol::Address);
            if (a && (quint16)a->text().toUInt() == addr) {
                refreshSimRowDisplay(table, r);
            }
        }
    };

    if (fmt == "32-bit Float") {
        float f = valStr.toFloat(&ok);
        if (ok) {
            writeFloat32ToSlave(target, addr, f);
            refreshSimMultiWordRows(table, row);
            refreshSameAddressRows();
        }
    } else if (fmt == "32-bit Signed" || fmt == "32-bit Unsigned") {
        bool ok32 = false;
        uint32_t val32 = 0;
        if (fmt == "32-bit Signed") val32 = (uint32_t)valStr.toInt(&ok32);
        else val32 = valStr.toUInt(&ok32);

        if (ok32) {
            target->setRegister(addr, (quint16)(val32 >> 16));
            target->setRegister(addr + 1, (quint16)(val32 & 0xFFFF));
            refreshSimMultiWordRows(table, row);
            refreshSameAddressRows();
            ok = true;
        }
    } else if (fmt == "64-bit Float") {
        double d = valStr.toDouble(&ok);
        if (ok) {
            writeFloat64ToSlave(target, addr, d);
            refreshSimMultiWordRows(table, row);
            refreshSameAddressRows();
        }
    } else if (fmt == "String") {
        const int regCount = simTableStringLengths.value(table).value(row, kDefaultStringRegisterCount);
        const QVector<quint16> encoded = encodeUtf8ToRegisters(valStr, regCount);
        for (int i = 0; i < encoded.size(); ++i) {
            target->setRegister(static_cast<quint16>(addr + i), encoded[i]);
        }
        refreshSimMultiWordRows(table, row);
        refreshSameAddressRows();
        ok = true;
    } else {
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
            refreshSameAddressRows();
        }
    }

    if (!ok && fmt != "32-bit Float" && fmt != "64-bit Float" && fmt != "String" && !fmt.startsWith("32-bit")) {
        refreshSimRowDisplay(table, row);
    }
}

void MainWindow::onSimCellDoubleClicked(int row, int column)
{
    if (column != SimRegisterCol::Value) return;
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;
    if (simTableFormats.value(table).value(row, QStringLiteral("Unsigned")) != QStringLiteral("Binary")) return;
    onSimShowBitEditor(table, row);
}

void MainWindow::onSimShowBitEditor(QTableWidget *table, int row)
{
    if (!table) return;
    ModbusSlave *target = (table == tblSimAGV) ? simAGVDevice : simMainDevice;
    if (!target) return;

    QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
    if (!addrItem || addrItem->text().isEmpty()) return;
    quint16 addr = (quint16)addrItem->text().toUInt();
    quint16 val = target->getRegister(addr);

    const QTableWidgetItem *descItem = table->item(row, SimRegisterCol::Description);
    const QStringList bitDescs = parseBitDescriptionsFromComment(descItem ? descItem->text() : QString());
    bool hasBitDesc = false;
    for (const QString &d : bitDescs) {
        if (!d.isEmpty()) {
            hasBitDesc = true;
            break;
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QString("地址 %1 位编辑器").arg(addr));
    if (hasBitDesc) {
        dlg.setMinimumWidth(520);
    }
    QVBoxLayout *v = new QVBoxLayout(&dlg);

    if (descItem && !descItem->text().trimmed().isEmpty() && !hasBitDesc) {
        QLabel *descLabel = new QLabel(descItem->text().trimmed());
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet(QStringLiteral("color: gray; margin-bottom: 4px;"));
        v->addWidget(descLabel);
    }

    QGridLayout *g = new QGridLayout();
    QList<QCheckBox*> checks;
    for (int i = 0; i < 16; ++i) {
        QString label = QString("位%1").arg(i);
        if (!bitDescs[i].isEmpty()) {
            label += QString("  %1").arg(bitDescs[i]);
        }
        QCheckBox *cb = new QCheckBox(label);
        cb->setChecked((val >> i) & 1);
        if (hasBitDesc) {
            g->addWidget(cb, i, 0);
        } else {
            g->addWidget(cb, i / 4, i % 4);
        }
        checks.append(cb);
    }
    v->addLayout(g);
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    QPushButton *btnOk = new QPushButton("确定");
    QPushButton *btnCancel = new QPushButton("取消");
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    v->addLayout(btnLayout);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        quint16 newVal = 0;
        for (int i = 0; i < 16; ++i) {
            if (checks[i]->isChecked()) newVal |= (1 << i);
        }
        target->setRegister(addr, newVal);
        refreshSimRowDisplay(table, row);
    }
}

QPair<quint16, quint16> MainWindow::encodeFloat32Words(float value) const
{
    quint32 u = 0;
    memcpy(&u, &value, sizeof(float));
    const quint16 ab = static_cast<quint16>(u & 0xFFFF);
    const quint16 cd = static_cast<quint16>((u >> 16) & 0xFFFF);
    quint16 w0 = 0;
    quint16 w1 = 0;
    switch (float32WordOrder) {
    case Float32WordOrder::CDAB:
        w0 = cd; w1 = ab;
        break;
    case Float32WordOrder::ABCD:
        w0 = ab; w1 = cd;
        break;
    case Float32WordOrder::BADC:
        w0 = swapBytes16(ab); w1 = swapBytes16(cd);
        break;
    case Float32WordOrder::DCBA:
        w0 = swapBytes16(cd); w1 = swapBytes16(ab);
        break;
    }
    return QPair<quint16, quint16>(w0, w1);
}

float MainWindow::decodeFloat32Words(quint16 w0, quint16 w1) const
{
    quint16 ab = 0;
    quint16 cd = 0;
    switch (float32WordOrder) {
    case Float32WordOrder::CDAB:
        cd = w0; ab = w1;
        break;
    case Float32WordOrder::ABCD:
        ab = w0; cd = w1;
        break;
    case Float32WordOrder::BADC:
        ab = swapBytes16(w0); cd = swapBytes16(w1);
        break;
    case Float32WordOrder::DCBA:
        cd = swapBytes16(w0); ab = swapBytes16(w1);
        break;
    }
    const quint32 u = static_cast<quint32>(cd) << 16 | ab;
    float f = 0.0f;
    memcpy(&f, &u, sizeof(float));
    return f;
}

void MainWindow::encodeFloat64Words(double value, quint16 &w0, quint16 &w1, quint16 &w2, quint16 &w3) const
{
    quint64 raw = 0;
    memcpy(&raw, &value, sizeof(double));
    const quint16 ab = static_cast<quint16>(raw & 0xFFFF);
    const quint16 cd = static_cast<quint16>((raw >> 16) & 0xFFFF);
    const quint16 ef = static_cast<quint16>((raw >> 32) & 0xFFFF);
    const quint16 gh = static_cast<quint16>((raw >> 48) & 0xFFFF);
    quint16 words[4] = {0, 0, 0, 0};
    switch (float64WordOrder) {
    case Float64WordOrder::GHEF_CDAB:
        words[0] = gh; words[1] = ef; words[2] = cd; words[3] = ab;
        break;
    case Float64WordOrder::ABCD_EFGH:
        words[0] = ab; words[1] = cd; words[2] = ef; words[3] = gh;
        break;
    case Float64WordOrder::BADC_FEHG:
        words[0] = swapBytes16(ab); words[1] = swapBytes16(cd);
        words[2] = swapBytes16(ef); words[3] = swapBytes16(gh);
        break;
    case Float64WordOrder::DCBA_HGFE:
        words[0] = swapBytes16(cd); words[1] = swapBytes16(ab);
        words[2] = swapBytes16(ef); words[3] = swapBytes16(gh);
        break;
    }
    w0 = words[0]; w1 = words[1]; w2 = words[2]; w3 = words[3];
}

double MainWindow::decodeFloat64Words(quint16 w0, quint16 w1, quint16 w2, quint16 w3) const
{
    quint16 ab = 0;
    quint16 cd = 0;
    quint16 ef = 0;
    quint16 gh = 0;
    switch (float64WordOrder) {
    case Float64WordOrder::GHEF_CDAB:
        gh = w0; ef = w1; cd = w2; ab = w3;
        break;
    case Float64WordOrder::ABCD_EFGH:
        ab = w0; cd = w1; ef = w2; gh = w3;
        break;
    case Float64WordOrder::BADC_FEHG:
        ab = swapBytes16(w0); cd = swapBytes16(w1);
        ef = swapBytes16(w2); gh = swapBytes16(w3);
        break;
    case Float64WordOrder::DCBA_HGFE:
        cd = swapBytes16(w0); ab = swapBytes16(w1);
        ef = swapBytes16(w2); gh = swapBytes16(w3);
        break;
    }
    const quint64 raw = static_cast<quint64>(ab)
                        | (static_cast<quint64>(cd) << 16)
                        | (static_cast<quint64>(ef) << 32)
                        | (static_cast<quint64>(gh) << 48);
    double d = 0.0;
    memcpy(&d, &raw, sizeof(double));
    return d;
}

bool MainWindow::writeFloat32ToSlave(ModbusSlave *slave, quint16 addr, float value)
{
    if (!slave) {
        return false;
    }
    const QPair<quint16, quint16> words = encodeFloat32Words(value);
    return slave->setRegisters(addr, {words.first, words.second});
}

float MainWindow::readFloat32FromSlave(ModbusSlave *slave, quint16 addr) const
{
    if (!slave) {
        return 0.0f;
    }
    return decodeFloat32Words(slave->getRegister(addr), slave->getRegister(addr + 1));
}

bool MainWindow::writeFloat64ToSlave(ModbusSlave *slave, quint16 addr, double value)
{
    if (!slave) {
        return false;
    }
    quint16 w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    encodeFloat64Words(value, w0, w1, w2, w3);
    return slave->setRegisters(addr, {w0, w1, w2, w3});
}

double MainWindow::readFloat64FromSlave(ModbusSlave *slave, quint16 addr) const
{
    if (!slave) {
        return 0.0;
    }
    return decodeFloat64Words(slave->getRegister(addr),
                              slave->getRegister(addr + 1),
                              slave->getRegister(addr + 2),
                              slave->getRegister(addr + 3));
}

void MainWindow::loadModbusFloatOrderSettings()
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    float32WordOrder = float32WordOrderFromString(
        settings.value(QStringLiteral("modbus/float32WordOrder"), QStringLiteral("CDAB")).toString());
    float64WordOrder = float64WordOrderFromString(
        settings.value(QStringLiteral("modbus/float64WordOrder"), QStringLiteral("GHEF_CDAB")).toString());
}

void MainWindow::saveModbusFloatOrderSettings()
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(QStringLiteral("modbus/float32WordOrder"), float32WordOrderLabel(float32WordOrder));
    settings.setValue(QStringLiteral("modbus/float64WordOrder"), float64WordOrderLabel(float64WordOrder));
}

void MainWindow::applyFloatWordOrderToSimulatorTables()
{
    if (tblSimAGV) {
        rebuildSimRowStates(tblSimAGV);
    }
    if (tblSimMain) {
        rebuildSimRowStates(tblSimMain);
    }
}

void MainWindow::showModbusFloatOrderDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Modbus 浮点字序"));
    QFormLayout *form = new QFormLayout(&dlg);

    QComboBox *cb32 = new QComboBox(&dlg);
    cb32->addItems(QStringList()
                     << QStringLiteral("CDAB")
                     << QStringLiteral("ABCD")
                     << QStringLiteral("BADC")
                     << QStringLiteral("DCBA"));
    cb32->setCurrentText(float32WordOrderLabel(float32WordOrder));

    QComboBox *cb64 = new QComboBox(&dlg);
    cb64->addItems(QStringList()
                    << QStringLiteral("GHEF CDAB")
                    << QStringLiteral("ABCD EFGH")
                    << QStringLiteral("BADC FEHG")
                    << QStringLiteral("DCBA HGFE"));
    cb64->setCurrentText(float64WordOrderLabel(float64WordOrder));

    form->addRow(QStringLiteral("REAL (32-bit) 字序:"), cb32);
    form->addRow(QStringLiteral("LREAL (64-bit) 字序:"), cb64);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    float32WordOrder = float32WordOrderFromString(cb32->currentText());
    float64WordOrder = float64WordOrderFromString(cb64->currentText());
    saveModbusFloatOrderSettings();
    applyFloatWordOrderToSimulatorTables();
    if (txtSimLog) {
        txtSimLog->append(QStringLiteral("[%1] 浮点字序已更新: REAL=%2, LREAL=%3")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                              .arg(float32WordOrderLabel(float32WordOrder))
                              .arg(float64WordOrderLabel(float64WordOrder)));
    }
}

void MainWindow::refreshSimMultiWordRows(QTableWidget *table, int startRow)
{
    if (!table || startRow < 0 || startRow >= table->rowCount()) {
        return;
    }
    refreshSimRowDisplay(table, startRow);
    QTableWidgetItem *addrItem = table->item(startRow, SimRegisterCol::Address);
    if (!addrItem || addrItem->text().isEmpty()) {
        return;
    }
    const QString fmt = simTableFormats.value(table).value(startRow, QStringLiteral("Unsigned"));
    const int stringRegCount = simTableStringLengths.value(table).value(startRow, kDefaultStringRegisterCount);
    const int wordCount = simFormatWordCount(fmt, stringRegCount);
    if (wordCount <= 1) {
        return;
    }
    const quint16 addr = static_cast<quint16>(addrItem->text().toUInt());
    for (int w = 1; w < wordCount; ++w) {
        const int subRow = findSimRowByAddress(table, addr + w);
        if (subRow >= 0) {
            refreshSimRowDisplay(table, subRow);
        }
    }
}

int MainWindow::findSimRowByAddress(QTableWidget *table, quint16 addr) const
{
    if (!table) {
        return -1;
    }
    const auto itTable = simAddrToRow.constFind(table);
    if (itTable != simAddrToRow.constEnd()) {
        return itTable->value(addr, -1);
    }
    for (int r = 0; r < table->rowCount(); ++r) {
        QTableWidgetItem *item = table->item(r, SimRegisterCol::Address);
        if (item && (quint16)item->text().toUInt() == addr) {
            return r;
        }
    }
    return -1;
}

void MainWindow::rebuildSimAddrIndex(QTableWidget *table)
{
    if (!table) {
        return;
    }
    QHash<quint16, int> &addrToRow = simAddrToRow[table];
    QHash<quint16, QVector<int>> &touchRows = simAddrTouchRows[table];
    addrToRow.clear();
    touchRows.clear();

    for (int r = 0; r < table->rowCount(); ++r) {
        QTableWidgetItem *addrItem = table->item(r, SimRegisterCol::Address);
        if (!addrItem || addrItem->text().isEmpty()) {
            continue;
        }
        bool ok = false;
        const quint16 baseAddr = static_cast<quint16>(addrItem->text().toUInt(&ok));
        if (!ok) {
            continue;
        }
        if (!addrToRow.contains(baseAddr)) {
            addrToRow.insert(baseAddr, r);
        }
        const QString fmt = simTableFormats.value(table).value(r, QStringLiteral("Unsigned"));
        const int stringRegCount = simTableStringLengths.value(table).value(r, kDefaultStringRegisterCount);
        const int wordCount = qMax(1, simFormatWordCount(fmt, stringRegCount));
        for (int w = 0; w < wordCount; ++w) {
            touchRows[static_cast<quint16>(baseAddr + w)].append(r);
        }
    }

    // Invalidate waveform row/format cache when table layout changes
    for (CyclicTimer &t : simCyclicTimers) {
        t.cacheValid = false;
    }
}

void MainWindow::rebuildSimRowStates(QTableWidget *table)
{
    if (!table) {
        return;
    }

    rebuildSimAddrIndex(table);

    for (int r = 0; r < table->rowCount(); ++r) {
        setSimRowEnabled(table, r, true);
    }

    for (int r = 0; r < table->rowCount(); ++r) {
        const QString fmt = simTableFormats.value(table).value(r, QStringLiteral("Unsigned"));
        const int stringRegCount = simTableStringLengths.value(table).value(r, kDefaultStringRegisterCount);
        const int wordCount = simFormatWordCount(fmt, stringRegCount);
        if (wordCount <= 1) {
            continue;
        }

        QTableWidgetItem *addrItem = table->item(r, SimRegisterCol::Address);
        if (!addrItem || addrItem->text().isEmpty()) {
            continue;
        }
        const quint16 addr = (quint16)addrItem->text().toUInt();

        for (int w = 1; w < wordCount; ++w) {
            const int subRow = findSimRowByAddress(table, addr + w);
            if (subRow < 0) {
                continue;
            }
            setSimRowEnabled(table, subRow, false);
            simTableFormats[table].remove(subRow);
            table->blockSignals(true);
            if (!table->item(subRow, SimRegisterCol::Value)) {
                table->setItem(subRow, SimRegisterCol::Value, new QTableWidgetItem());
            }
            table->item(subRow, SimRegisterCol::Value)->setText(QString());
            table->blockSignals(false);
        }
    }

    rebuildSimAddrIndex(table);

    for (int r = 0; r < table->rowCount(); ++r) {
        refreshSimRowDisplay(table, r);
        applyRegisterMapRowStyle(table, r);
    }
}

void MainWindow::syncSimulatorTablesFromMaps() {
    auto syncOne = [this](QTableWidget *src, QTableWidget *dst) {
        if (!src || !dst) return;
        if (dst->rowCount() < src->rowCount()) dst->setRowCount(src->rowCount());

        for (int row = 0; row < src->rowCount(); ++row) {
            if (!dst->item(row, SimRegisterCol::Direction))
                dst->setItem(row, SimRegisterCol::Direction, new QTableWidgetItem());
            if (!dst->item(row, SimRegisterCol::Address))
                dst->setItem(row, SimRegisterCol::Address, new QTableWidgetItem());
            if (!dst->item(row, SimRegisterCol::Description))
                dst->setItem(row, SimRegisterCol::Description, new QTableWidgetItem());
            if (!dst->item(row, SimRegisterCol::Value))
                dst->setItem(row, SimRegisterCol::Value, new QTableWidgetItem("0"));

            QTableWidgetItem *srcDir = src->item(row, RegisterMapCol::Direction);
            QTableWidgetItem *srcAddr = src->item(row, RegisterMapCol::Address);
            QTableWidgetItem *srcCmt = src->item(row, RegisterMapCol::Comment);
            QTableWidgetItem *srcFmt = src->item(row, RegisterMapCol::Format);
            dst->item(row, SimRegisterCol::Direction)->setText(srcDir ? srcDir->text() : "");
            dst->item(row, SimRegisterCol::Address)->setText(srcAddr ? srcAddr->text() : "");
            dst->item(row, SimRegisterCol::Description)->setText(srcCmt ? srcCmt->text() : "");

            const QString regFmt = srcFmt ? srcFmt->text() : QString();
            if (!regFmt.trimmed().isEmpty()) {
                simTableFormats[dst][row] = mapRegisterFormatToSimFormat(regFmt);
                if (simTableFormats[dst][row] == QStringLiteral("String")) {
                    simTableStringLengths[dst][row] = parseStringRegisterCount(regFmt, kDefaultStringRegisterCount);
                }
            }

            for (int col : {SimRegisterCol::Direction, SimRegisterCol::Address, SimRegisterCol::Description}) {
                Qt::ItemFlags flags = dst->item(row, col)->flags();
                dst->item(row, col)->setFlags(flags & ~Qt::ItemIsEditable);
            }

            applyRegisterMapRowStyle(dst, row);
        }

        rebuildSimRowStates(dst);
    };

    syncOne(tblAGV, tblSimAGV);
    syncOne(tblRobot, tblSimMain);
}

void MainWindow::onRegisterTableCellClicked(int row, int column) {
    Q_UNUSED(column);
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if(!table) return;
    
    QTableWidgetItem *item = table->item(row, RegisterMapCol::Address);
    QTableWidgetItem *formatItem = table->item(row, RegisterMapCol::Format);
    QTableWidgetItem *dirItem = table->item(row, RegisterMapCol::Direction);
    const RegisterMapDirection dir = parseRegisterMapDirection(dirItem ? dirItem->text() : QString());
    const bool fillRead = (dir != RegisterMapDirection::Write);
    const bool fillWrite = (dir != RegisterMapDirection::Read);
    
    bool ok;
    if(item) {
        int addr = item->text().toInt(&ok);
        if(ok) {
            if (fillRead) {
                spinReadStartAddr->setValue(addr);
            }
            if (fillWrite) {
                spinWriteStartAddr->setValue(addr);
            }
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
        } else if (fmtText.contains("STRING") || fmtText.contains("WSTRING")) {
            formatIndex = 5; // String
            readQty = parseStringRegisterCount(formatItem->text(), kDefaultStringRegisterCount);
        } else if (fmtText.contains("BOOL")) {
            formatIndex = 2; // Binary
            readQty = 1;
        } else if (fmtText.contains("UINT") || fmtText.contains("INT")) {
            formatIndex = 0; // Decimal
            readQty = 1;
        }

        if (fillRead) {
            cmbDisplayFormat->setCurrentIndex(formatIndex);
            spinReadQuantity->setValue(readQty);
        }
        if (fillWrite) {
            cmbWriteFormat->setCurrentIndex(formatIndex);
            spinWriteQuantity->setValue(readQty);
        }
    }

    if (fillRead && chkAutoReadOnMapClick && chkAutoReadOnMapClick->isChecked()) {
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

    QString direction = table->item(row, RegisterMapCol::Direction) ? table->item(row, RegisterMapCol::Direction)->text().trimmed() : "";
    QString addr = table->item(row, RegisterMapCol::Address) ? table->item(row, RegisterMapCol::Address)->text().trimmed() : "";
    QString cmt = table->item(row, RegisterMapCol::Comment) ? table->item(row, RegisterMapCol::Comment)->text().trimmed() : "";
    QString regFmt = table->item(row, RegisterMapCol::Format) ? table->item(row, RegisterMapCol::Format)->text().trimmed() : "";
    return direction.isEmpty() && addr.isEmpty() && cmt.isEmpty() && regFmt.isEmpty();
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
        for (int col = 0; col < RegisterMapCol::ColumnCount; ++col) {
            table->setItem(newRow, col, new QTableWidgetItem(""));
        }
        applyRegisterMapRowStyle(table, newRow);
    } else {
        for (int col = 0; col < RegisterMapCol::ColumnCount; ++col) {
            if (!table->item(rows - 1, col)) {
                table->setItem(rows - 1, col, new QTableWidgetItem(""));
            }
        }
        applyRegisterMapRowStyle(table, rows - 1);
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
    applyRegisterMapTableStyles(table);
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
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (table) {
        if (column == RegisterMapCol::Direction) {
            applyRegisterMapRowStyle(table, row);
        }
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
            QTableWidgetItem *dirItem = table->item(i, RegisterMapCol::Direction);
            QTableWidgetItem *addrItem = table->item(i, RegisterMapCol::Address);
            QTableWidgetItem *cmtItem = table->item(i, RegisterMapCol::Comment);
            QTableWidgetItem *regFmtItem = table->item(i, RegisterMapCol::Format);
            settings.setValue("direction", dirItem ? dirItem->text() : "");
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
            if (size > table->rowCount()) table->setRowCount(size);

            table->blockSignals(true);
            
            for (int i = 0; i < size; ++i) {
                settings.setArrayIndex(i);
                QString direction = settings.value("direction").toString();
                QString addr = settings.value("addr").toString();
                QString cmt = settings.value("cmt").toString();
                QString regfmt = settings.value("regfmt").toString();
                
                for (int col = 0; col < RegisterMapCol::ColumnCount; ++col) {
                    if (!table->item(i, col)) {
                        table->setItem(i, col, new QTableWidgetItem());
                    }
                }
                
                table->item(i, RegisterMapCol::Direction)->setText(direction);
                table->item(i, RegisterMapCol::Address)->setText(addr);
                table->item(i, RegisterMapCol::Comment)->setText(cmt);
                table->item(i, RegisterMapCol::Format)->setText(regfmt);
            }

            table->blockSignals(false);
            applyRegisterMapTableStyles(table);
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
    out << "Tab,Direction,Address,Comment,RegisterFormat\n";

    auto exportTable = [&](QTableWidget *table, const QString &tabName) {
        if (!table) return;
        for (int i = 0; i < table->rowCount(); ++i) {
            QString direction = table->item(i, RegisterMapCol::Direction) ? table->item(i, RegisterMapCol::Direction)->text() : "";
            QString addr = table->item(i, RegisterMapCol::Address) ? table->item(i, RegisterMapCol::Address)->text() : "";
            QString cmt = table->item(i, RegisterMapCol::Comment) ? table->item(i, RegisterMapCol::Comment)->text() : "";
            QString regFmt = table->item(i, RegisterMapCol::Format) ? table->item(i, RegisterMapCol::Format)->text() : "";
            
            if (direction.isEmpty() && addr.isEmpty() && cmt.isEmpty() && regFmt.isEmpty()) continue;
            
            out << tabName << ","
                << escapeRegisterMapCsvField(direction) << ","
                << escapeRegisterMapCsvField(addr) << ","
                << escapeRegisterMapCsvField(cmt) << ","
                << escapeRegisterMapCsvField(regFmt) << "\n";
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
    in.setCodec("UTF-8");
    const QString headerLine = in.readLine();
    const QStringList headerParts = parseRegisterMapCsvLine(headerLine);

    int tabCol = 0;
    int dirCol = -1;
    int addrCol = 1;
    int cmtCol = 2;
    int fmtCol = 3;
    bool hasHeader = false;

    for (int i = 0; i < headerParts.size(); ++i) {
        const QString h = headerParts[i].trimmed().toLower();
        if (h == QStringLiteral("tab") || h == QStringLiteral("sheet") || h == QStringLiteral("worksheet")) {
            tabCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("direction") || h == QStringLiteral("方向")) {
            dirCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("address") || h == QStringLiteral("地址")) {
            addrCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("comment") || h == QStringLiteral("注释")) {
            cmtCol = i;
            hasHeader = true;
        } else if (h == QStringLiteral("registerformat") || h == QStringLiteral("寄存器格式")) {
            fmtCol = i;
            hasHeader = true;
        }
    }

    const bool hasDirection = dirCol >= 0;
    if (!hasHeader) {
        dirCol = -1;
        tabCol = 0;
        addrCol = 1;
        cmtCol = 2;
        fmtCol = 3;
    }
    
    auto clearTable = [&](QTableWidget* table) {
        table->blockSignals(true);
        table->setRowCount(0);
        table->blockSignals(false);
    };

    clearTable(tblAGV);
    clearTable(tblRobot);

    QMap<QString, int> tabRowCounters;
    tabRowCounters["agv"] = 0;
    tabRowCounters["robot"] = 0;

    auto importLine = [&](const QString &line) {
        if (line.trimmed().isEmpty()) return;

        const QStringList parts = parseRegisterMapCsvLine(line);
        if (parts.size() < 3) return;

        const QString tabStr = parts.value(tabCol).trimmed().toLower();
        const QString direction = hasDirection ? parts.value(dirCol).trimmed() : QString();
        const QString addr = parts.value(addrCol).trimmed();
        const QString cmt = parts.value(cmtCol).trimmed();
        QString regFmt = parts.value(fmtCol).trimmed();
        if (regFmt.isEmpty()) {
            regFmt = addr;
        }
        if (addr.isEmpty() && cmt.isEmpty() && regFmt.isEmpty() && direction.isEmpty()) {
            return;
        }

        QTableWidget *table = (tabStr == "robot" || tabStr == QStringLiteral("机器人")) ? tblRobot : tblAGV;
        const QString key = (tabStr == "robot" || tabStr == QStringLiteral("机器人")) ? "robot" : "agv";
        const int row = tabRowCounters[key]++;

        if (row >= table->rowCount()) {
            table->setRowCount(row + 1);
        }

        table->blockSignals(true);
        for (int col = 0; col < RegisterMapCol::ColumnCount; ++col) {
            if (!table->item(row, col)) {
                table->setItem(row, col, new QTableWidgetItem());
            }
        }
        table->item(row, RegisterMapCol::Direction)->setText(direction);
        table->item(row, RegisterMapCol::Address)->setText(addr);
        table->item(row, RegisterMapCol::Comment)->setText(cmt);
        table->item(row, RegisterMapCol::Format)->setText(regFmt);
        table->blockSignals(false);
        applyRegisterMapRowStyle(table, row);
    };

    if (!hasHeader) {
        importLine(headerLine);
    }

    while (!in.atEnd()) {
        importLine(in.readLine());
    }

    f.close();
    ensureRegisterMapEditableTailRow(tblAGV);
    ensureRegisterMapEditableTailRow(tblRobot);
    applyRegisterMapTableStyles(tblAGV);
    applyRegisterMapTableStyles(tblRobot);
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
        "Signed", "Unsigned", "Hex", "ASCII - Hex", "Binary", "String",
        "32-bit Signed", "32-bit Unsigned", "32-bit Float", "64-bit Float"
    };
    for (const QString &fmt : formats) {
        QAction *a = formatMenu->addAction(fmt);
        connect(a, &QAction::triggered, this, [this, table, row, fmt](){
            simTableFormats[table][row] = fmt;
            if (fmt == QStringLiteral("String")) {
                bool ok = false;
                const int count = QInputDialog::getInt(this,
                                                       QStringLiteral("String 寄存器数"),
                                                       QStringLiteral("占用寄存器数量:"),
                                                       kDefaultStringRegisterCount,
                                                       1,
                                                       60,
                                                       1,
                                                       &ok);
                simTableStringLengths[table][row] = ok ? count : kDefaultStringRegisterCount;
            } else {
                simTableStringLengths[table].remove(row);
            }
            rebuildSimRowStates(table);
        });
    }

    menu.addSeparator();
    QAction *actBit = menu.addAction("bit Edit");
    connect(actBit, &QAction::triggered, this, [this, table, row](){ onSimShowBitEditor(table, row); });

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
    QTableWidgetItem *addrItem = table->item(row, SimRegisterCol::Address);
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
        QJsonObject values, formats, stringLengths;
        for (int i = 0; i < table->rowCount(); ++i) {
            QTableWidgetItem *addrItem = table->item(i, SimRegisterCol::Address);
            if (!addrItem) continue;
            bool ok;
            quint16 addr = (quint16)addrItem->text().toUInt(&ok);
            if (!ok) continue;
            quint16 val = dev->getRegister(addr);
            if (val != 0) values.insert(QString::number(addr), int(val));
            QString fmt = simTableFormats.value(table).value(i, "Unsigned");
            if (fmt != "Unsigned") formats.insert(QString::number(i), fmt);
            if (fmt == "String") {
                const int regCount = simTableStringLengths.value(table).value(i, kDefaultStringRegisterCount);
                if (regCount != kDefaultStringRegisterCount) {
                    stringLengths.insert(QString::number(i), regCount);
                }
            }
        }
        obj.insert("values", values);
        obj.insert("formats", formats);
        if (!stringLengths.isEmpty()) {
            obj.insert("stringLengths", stringLengths);
        }
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
                if (fmt == "String") {
                    simTableStringLengths[table][row] = kDefaultStringRegisterCount;
                }
                refreshSimRowDisplay(table, row);
            }
        }
        if (obj.contains("stringLengths")) {
            QJsonObject lengthsObj = obj.value("stringLengths").toObject();
            for (auto it = lengthsObj.begin(); it != lengthsObj.end(); ++it) {
                const int row = it.key().toInt();
                const int regCount = it.value().toInt(kDefaultStringRegisterCount);
                simTableStringLengths[table][row] = qMax(1, regCount);
                refreshSimRowDisplay(table, row);
            }
        }
        rebuildSimRowStates(table);
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
        QString direction;
        QString address;
        QString dataType;
        QString comment;
        QString registerFormat;
    };

    auto inferDirectionFromName = [](const QString &name) -> QString {
        if (name.contains(QStringLiteral("Read"), Qt::CaseInsensitive)) {
            return QStringLiteral("读");
        }
        if (name.contains(QStringLiteral("Write"), Qt::CaseInsensitive)) {
            return QStringLiteral("写");
        }
        return QString();
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

            const QString rowDirection = inferDirectionFromName(name);

            if (!rowIndexByAddr.contains(finalAddr)) {
                MergedRow mr;
                mr.direction = rowDirection;
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
                    if (!mergedRows[idx].direction.isEmpty()
                        && !rowDirection.isEmpty()
                        && mergedRows[idx].direction != rowDirection) {
                        mergedRows[idx].direction.clear();
                    } else if (mergedRows[idx].direction.isEmpty()) {
                        mergedRows[idx].direction = rowDirection;
                    }
                    mergedRows[idx].comment = QString("%1 %2").arg(mergedRows[idx].comment).arg(note).simplified();
                }
            }
        }

        targetTable->blockSignals(true);
        targetTable->setRowCount(mergedRows.size());
        for (int i = 0; i < mergedRows.size(); ++i) {
            for (int col = 0; col < RegisterMapCol::ColumnCount; ++col) {
                if (!targetTable->item(i, col)) {
                    targetTable->setItem(i, col, new QTableWidgetItem());
                }
            }
            targetTable->item(i, RegisterMapCol::Direction)->setText(mergedRows[i].direction);
            targetTable->item(i, RegisterMapCol::Address)->setText(mergedRows[i].address);
            targetTable->item(i, RegisterMapCol::Comment)->setText(mergedRows[i].comment);
            targetTable->item(i, RegisterMapCol::Format)->setText(mergedRows[i].registerFormat);
        }
        targetTable->blockSignals(false);

        applyRegisterMapTableStyles(targetTable);
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

QWidget *MainWindow::createLifeAssistantPage()
{
    lifeAssistant = new LifeAssistantWidget(this);
    return lifeAssistant;
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
#ifdef Q_OS_WIN
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        auto toU64 = [](const FILETIME &ft) -> quint64 {
            return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        const quint64 idle = toU64(idleTime);
        const quint64 kernel = toU64(kernelTime);
        const quint64 user = toU64(userTime);
        // kernel includes idle on Windows
        const quint64 total = kernel + user;

        if (hasLastPerfSample) {
            const quint64 totalDiff = total - (lastTotalUser + lastTotalSys);
            const quint64 idleDiff = idle - lastTotalIdle;
            if (totalDiff > 0) {
                double usage = 100.0 * (1.0 - static_cast<double>(idleDiff) / static_cast<double>(totalDiff));
                if (usage < 0.0) {
                    usage = 0.0;
                }
                if (usage > 100.0) {
                    usage = 100.0;
                }
                lblLocalCpu->setText(QString("CPU 使用率: %1%").arg(usage, 0, 'f', 1));
                chartLocalCpu->addValue(usage);
                if (usage > 90.0) {
                    txtPerfLog->append(QString("<font color='red'>[%1] 警告: CPU 负载过高 (%2%)，可能导致系统卡顿。</font>")
                                           .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                           .arg(usage, 0, 'f', 1));
                }
            }
        }
        lastTotalUser = user;
        lastTotalUserLow = 0;
        lastTotalSys = kernel;
        lastTotalIdle = idle;
        hasLastPerfSample = true;
    }

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus) && memStatus.ullTotalPhys > 0) {
        const double usage = static_cast<double>(memStatus.dwMemoryLoad);
        lblLocalMem->setText(QString("内存 使用率: %1%").arg(usage, 0, 'f', 1));
        chartLocalMem->addValue(usage);
        if (usage > 95.0) {
            txtPerfLog->append(QString("<font color='red'>[%1] 严重警告: 内存几乎耗尽 (%2%)，极易导致系统卡死。</font>")
                                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                                   .arg(usage, 0, 'f', 1));
        }
    }
#else
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
#endif
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

