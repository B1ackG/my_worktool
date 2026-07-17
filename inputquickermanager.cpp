#include "inputquickermanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <unistd.h>

InputQuickerManager::InputQuickerManager(QObject *parent)
    : QObject(parent)
    , daemonProcess(new QProcess(this))
    , monitorProcess(new QProcess(this))
    , enabledValue(true)
    , daemonAutostartValue(true)
    , wheel2AxisValue(QStringLiteral("REL_HWHEEL"))
    , grabDeviceValue(false)
    , lastAppliedGrabDevice(false)
    , ownDaemonProcess(false)
{
    daemonProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(daemonProcess, &QProcess::readyReadStandardOutput, this, &InputQuickerManager::onDaemonOutput);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(daemonProcess, &QProcess::errorOccurred, this, &InputQuickerManager::onDaemonError);
    connect(monitorProcess, &QProcess::errorOccurred, this, &InputQuickerManager::onMonitorError);
#else
    connect(daemonProcess, QOverload<QProcess::ProcessError>::of(&QProcess::error),
            this, &InputQuickerManager::onDaemonError);
    connect(monitorProcess, QOverload<QProcess::ProcessError>::of(&QProcess::error),
            this, &InputQuickerManager::onMonitorError);
#endif
    connect(daemonProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &InputQuickerManager::onDaemonFinished);

    monitorProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(monitorProcess, &QProcess::readyReadStandardOutput, this, &InputQuickerManager::onMonitorOutput);
    connect(monitorProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &InputQuickerManager::onMonitorFinished);
}

InputQuickerManager::~InputQuickerManager()
{
    // Keep detached daemon alive after UI closes (replacement for xbindkeys).
    stopInputMonitor();
}

void InputQuickerManager::loadSettings()
{
    migrateLegacyConfigIfNeeded();
    readJsonConfig();
    emit bindingsChanged();
    emitStatus();
}

void InputQuickerManager::saveSettings() const
{
    writeJsonConfig();
}

bool InputQuickerManager::applySettings()
{
    saveSettings();
    emit logMessage(QStringLiteral("[快捷助手] 已保存 %1 条规则").arg(bindingsValue.size()));

    if (daemonAutostartValue) {
        setDaemonAutostartInstalled(true);
    } else if (isDaemonAutostartInstalled()) {
        setDaemonAutostartInstalled(false);
    }

    if (!enabledValue || bindingsValue.isEmpty()) {
        stopDaemon();
        lastAppliedDevicePath.clear();
        return true;
    }

    bool hasEnabled = false;
    for (const QuickerBinding &binding : bindingsValue) {
        if (binding.enabled) {
            hasEnabled = true;
            break;
        }
    }
    if (!hasEnabled) {
        stopDaemon();
        lastAppliedDevicePath.clear();
        return true;
    }

    if (isDaemonRunning() && !daemonNeedsRestart()) {
        if (reloadDaemonViaSignal()) {
            lastAppliedDevicePath = devicePathValue;
            lastAppliedGrabDevice = grabDeviceValue;
            emitStatus();
            return true;
        }
    }

    if (isDaemonRunning()) {
        stopDaemon();
    }
    const bool started = startDaemon();
    if (started) {
        lastAppliedDevicePath = devicePathValue;
        lastAppliedGrabDevice = grabDeviceValue;
    }
    return started;
}

bool InputQuickerManager::daemonNeedsRestart() const
{
    return devicePathValue != lastAppliedDevicePath
           || grabDeviceValue != lastAppliedGrabDevice;
}

bool InputQuickerManager::reloadDaemonViaSignal()
{
    if (!isDaemonRunning()) {
        return false;
    }

    const qint64 pid = resolveDaemonPid();
    if (pid <= 0) {
        return false;
    }

    if (::kill(static_cast<pid_t>(pid), SIGHUP) == 0) {
        writePidFile(pid);
        emit logMessage(QStringLiteral("[快捷助手] 配置已热加载（daemon 未重启）"));
        return true;
    }

    emit logMessage(QStringLiteral("[快捷助手] 热加载失败，将重启 daemon"));
    return false;
}

bool InputQuickerManager::startDaemon()
{
    clearStalePidFile();
    const qint64 existing = resolveDaemonPid();
    if (existing > 0) {
        writePidFile(existing);
        emit logMessage(QStringLiteral("[快捷助手] daemon 已在运行 (pid %1)").arg(existing));
        emitStatus();
        return true;
    }

    const QString scriptPath = daemonScriptPath();
    if (!QFileInfo::exists(scriptPath)) {
        emit logMessage(QStringLiteral("[快捷助手] 找不到 daemon 脚本: %1").arg(scriptPath));
        emitStatus();
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("INPUT_QUICKER_CONFIG"), configFilePath());
    env.insert(QStringLiteral("INPUT_QUICKER_PID"), pidFilePath());
    env.insert(QStringLiteral("INPUT_QUICKER_LOG"), logFilePath());

    QProcess launcher;
    launcher.setProcessEnvironment(env);
    launcher.setProgram(QStringLiteral("python3"));
    launcher.setArguments(QStringList() << scriptPath);

    qint64 pid = 0;
    if (!launcher.startDetached(&pid) || pid <= 0) {
        emit logMessage(QStringLiteral("[快捷助手] 启动失败（detached）"));
        emitStatus();
        return false;
    }

    ownDaemonProcess = false;
    for (int i = 0; i < 40 && !isDaemonRunning(); ++i) {
        usleep(50000);
    }

    const qint64 running = resolveDaemonPid();
    if (running <= 0) {
        emit logMessage(QStringLiteral("[快捷助手] daemon 未能保持运行，请查看日志: %1").arg(logFilePath()));
        emitStatus();
        return false;
    }

    writePidFile(running);
    emit logMessage(QStringLiteral("[快捷助手] daemon 已启动 (pid %1)").arg(running));
    emitStatus();
    return true;
}

void InputQuickerManager::stopDaemon()
{
    const qint64 pid = resolveDaemonPid();
    bool stopped = false;
    if (pid > 0 && isPidAlive(pid)) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
        for (int i = 0; i < 30 && isPidAlive(pid); ++i) {
            usleep(100000);
        }
        if (isPidAlive(pid)) {
            ::kill(static_cast<pid_t>(pid), SIGKILL);
        }
        stopped = true;
    } else if (daemonProcess->state() != QProcess::NotRunning) {
        daemonProcess->terminate();
        if (!daemonProcess->waitForFinished(3000)) {
            daemonProcess->kill();
            daemonProcess->waitForFinished(1000);
        }
        stopped = true;
    }

    clearStalePidFile();
    // Also clear any leftover processes found by name.
    const qint64 leftover = findDaemonPidByProcess();
    if (leftover > 0 && isPidAlive(leftover)) {
        ::kill(static_cast<pid_t>(leftover), SIGTERM);
        stopped = true;
    }

    if (stopped) {
        emit logMessage(QStringLiteral("[快捷助手] daemon 已停止"));
    }
    ownDaemonProcess = false;
    emitStatus();
}

bool InputQuickerManager::startInputMonitor(const QString &devicePath)
{
    if (isMonitorRunning()) {
        stopInputMonitor();
    }

    const QString scriptPath = monitorScriptPath();
    if (!QFileInfo::exists(scriptPath)) {
        emit logMessage(QStringLiteral("[快捷助手] 找不到监听脚本: %1").arg(scriptPath));
        return false;
    }

    const QString utilsPath = deviceUtilsScriptPath();
    if (QFileInfo::exists(utilsPath)) {
        QProcess check;
        QStringList checkArgs;
        checkArgs << utilsPath;
        if (!devicePath.isEmpty()) {
            checkArgs << QStringLiteral("--choose") << devicePath;
        }
        check.start(QStringLiteral("python3"), checkArgs);
        if (!check.waitForFinished(8000)) {
            check.kill();
            check.waitForFinished(1000);
            emit logMessage(QStringLiteral("[快捷助手] 设备检测超时"));
            return false;
        }
        if (check.exitCode() != 0) {
            const QString err = formatCaptureError(
                QString::fromLocal8Bit(check.readAllStandardError()).trimmed());
            emit logMessage(QStringLiteral("[快捷助手] %1").arg(err));
            return false;
        }
    }

    QStringList args;
    args << scriptPath << QStringLiteral("--json-lines");
    if (!devicePath.isEmpty()) {
        args << QStringLiteral("--device") << devicePath;
    }

    monitorLineBuffer.clear();
    monitorProcess->setProgram(QStringLiteral("python3"));
    monitorProcess->setArguments(args);
    monitorProcess->start();

    if (!monitorProcess->waitForStarted(3000)) {
        emit logMessage(QStringLiteral("[快捷助手] 监听启动失败: %1").arg(monitorProcess->errorString()));
        return false;
    }

    return true;
}

void InputQuickerManager::stopInputMonitor()
{
    if (!isMonitorRunning()) {
        return;
    }

    monitorProcess->terminate();
    if (!monitorProcess->waitForFinished(2000)) {
        monitorProcess->kill();
        monitorProcess->waitForFinished(1000);
    }
    monitorLineBuffer.clear();
    emit monitorStopped();
    emit logMessage(QStringLiteral("[快捷助手] 输入监听已停止"));
}

bool InputQuickerManager::isMonitorRunning() const
{
    return monitorProcess->state() != QProcess::NotRunning;
}

QList<InputQuickerManager::DeviceInfo> InputQuickerManager::refreshDeviceListFromProc() const
{
    QList<DeviceInfo> devices;
    QFile file(QStringLiteral("/proc/bus/input/devices"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return devices;
    }

    QTextStream stream(&file);
    QString name;
    QString handlers;
    const QRegularExpression nameRegex(QStringLiteral("^N: Name=\"(.*)\""));
    const QRegularExpression eventRegex(QStringLiteral("\\bevent\\d+\\b"));

    auto flushDevice = [&]() {
        const QRegularExpressionMatch eventMatch = eventRegex.match(handlers);
        if (!eventMatch.hasMatch()) {
            name.clear();
            handlers.clear();
            return;
        }

        DeviceInfo info;
        info.path = QStringLiteral("/dev/input/%1").arg(eventMatch.captured(0));
        info.name = name.isEmpty() ? info.path : name;
        info.score = 1;
        info.accessible = QFileInfo::exists(info.path);
        info.recommended = false;
        devices.append(info);
        name.clear();
        handlers.clear();
    };

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty()) {
            flushDevice();
            continue;
        }

        const QRegularExpressionMatch nameMatch = nameRegex.match(line);
        if (nameMatch.hasMatch()) {
            name = nameMatch.captured(1);
        } else if (line.startsWith(QStringLiteral("H: Handlers="))) {
            handlers = line.mid(QStringLiteral("H: Handlers=").length());
        }
    }
    flushDevice();
    return devices;
}

QList<InputQuickerManager::DeviceInfo> InputQuickerManager::refreshDeviceList() const
{
    const QString scriptPath = deviceUtilsScriptPath();
    if (!QFileInfo::exists(scriptPath)) {
        return refreshDeviceListFromProc();
    }

    QProcess process;
    process.start(QStringLiteral("python3"),
                  QStringList() << scriptPath << QStringLiteral("--list-json"));
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        return refreshDeviceListFromProc();
    }

    if (process.exitCode() != 0) {
        return refreshDeviceListFromProc();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (!doc.isArray()) {
        return refreshDeviceListFromProc();
    }

    QList<DeviceInfo> devices;
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        DeviceInfo info;
        info.path = obj.value(QStringLiteral("path")).toString();
        info.name = obj.value(QStringLiteral("name")).toString();
        info.score = obj.value(QStringLiteral("score")).toInt();
        info.capsSummary = obj.value(QStringLiteral("caps_summary")).toString();
        info.accessible = obj.value(QStringLiteral("accessible")).toBool();
        info.recommended = obj.value(QStringLiteral("recommended")).toBool();
        info.error = obj.value(QStringLiteral("error")).toString();
        if (!info.path.isEmpty()) {
            devices.append(info);
        }
    }
    return devices;
}

InputQuickerEnvCheck InputQuickerManager::runEnvironmentCheck() const
{
    InputQuickerEnvCheck result;

    {
        QProcess process;
        process.start(QStringLiteral("python3"),
                      QStringList() << QStringLiteral("-c")
                                    << QStringLiteral("import evdev; print('ok')"));
        if (process.waitForFinished(5000) && process.exitCode() == 0) {
            result.evdevOk = true;
            result.messages.append(QStringLiteral("python3-evdev: 已安装"));
        } else {
            result.messages.append(QStringLiteral("python3-evdev: 未安装 (sudo apt install python3-evdev)"));
        }
    }

    {
        QProcess process;
        process.start(QStringLiteral("which"), QStringList() << QStringLiteral("xdotool"));
        if (process.waitForFinished(3000) && process.exitCode() == 0) {
            result.xdotoolOk = true;
            result.messages.append(QStringLiteral("xdotool: 已安装"));
        } else {
            result.messages.append(QStringLiteral("xdotool: 未安装 (sudo apt install xdotool)"));
        }
    }

    const QList<DeviceInfo> devices = refreshDeviceList();
    for (const DeviceInfo &device : devices) {
        if (device.accessible) {
            result.inputReadable = true;
            break;
        }
    }
    if (result.inputReadable) {
        result.messages.append(QStringLiteral("/dev/input: 可读"));
    } else if (devices.isEmpty()) {
        result.messages.append(QStringLiteral("/dev/input: 未找到指针设备"));
    } else {
        result.messages.append(QStringLiteral("/dev/input: 无读取权限"));
    }

    gid_t groups[64];
    const int groupCount = getgroups(64, groups);
    for (int i = 0; i < groupCount; ++i) {
        struct group *grp = getgrgid(groups[i]);
        if (grp && QString::fromLocal8Bit(grp->gr_name) == QStringLiteral("input")) {
            result.inInputGroup = true;
            break;
        }
    }

    const char *userName = qgetenv("USER").constData();
    if (userName && *userName) {
        struct group *inputGroup = getgrnam("input");
        if (inputGroup && inputGroup->gr_mem) {
            for (char **member = inputGroup->gr_mem; *member != nullptr; ++member) {
                if (qstrcmp(*member, userName) == 0) {
                    result.inputGroupConfigured = true;
                    break;
                }
            }
        }
    }

    if (result.inInputGroup) {
        result.messages.append(QStringLiteral("用户组: 当前会话已在 input 组"));
    } else if (result.inputGroupConfigured) {
        result.messages.append(
            QStringLiteral("用户组: 已加入 input 组，但当前会话未生效（请注销并重新登录，或重启电脑）"));
    } else {
        result.messages.append(QStringLiteral("用户组: 未加入 input 组 (sudo usermod -aG input $USER)"));
    }

    return result;
}

QString InputQuickerManager::formatCaptureError(const QString &stderrText) const
{
    const QString text = stderrText.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("录制失败");
    }
    if (text.contains(QStringLiteral("no mouse device found"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("no_device"))) {
        return QStringLiteral("未找到鼠标设备。请先在上方选择输入设备，或点击「刷新设备」。");
    }
    if (text.contains(QStringLiteral("permission"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("permission_denied"))) {
        return QStringLiteral("无 /dev/input 读取权限。请执行: sudo usermod -aG input $USER，然后重新登录。");
    }
    if (text.contains(QStringLiteral("capture timeout"), Qt::CaseInsensitive)) {
        return QStringLiteral("录制超时，请在时限内按下鼠标键或滚动滚轮。");
    }
    if (text.startsWith(QStringLiteral("ERROR:"))) {
        const QString payload = text.mid(6).trimmed();
        const int colon = payload.indexOf(QLatin1Char(':'));
        if (colon > 0 && colon < payload.size() - 1) {
            return payload.mid(colon + 1).trimmed();
        }
        return payload;
    }
    return text;
}

bool InputQuickerManager::isDaemonRunning() const
{
    return resolveDaemonPid() > 0;
}

qint64 InputQuickerManager::daemonPid() const
{
    const qint64 pid = resolveDaemonPid();
    if (pid > 0) {
        const qint64 fromFile = readDaemonPid();
        if (fromFile != pid) {
            writePidFile(pid);
        }
    }
    return pid;
}

QString InputQuickerManager::statusText() const
{
    const qint64 pid = resolveDaemonPid();
    if (pid > 0) {
        return QStringLiteral("守护脚本运行中 (pid %1)").arg(pid);
    }
    if (!enabledValue) {
        return QStringLiteral("已关闭（脚本未自启）");
    }
    return QStringLiteral("守护脚本未在运行");
}

QString InputQuickerManager::configFilePath() const
{
    return QDir::home().filePath(QStringLiteral(".config/LiChenYang/input_quicker.json"));
}

QString InputQuickerManager::pidFilePath() const
{
    return QDir::home().filePath(QStringLiteral(".config/LiChenYang/input_quicker.pid"));
}

QString InputQuickerManager::logFilePath() const
{
    return QDir::home().filePath(QStringLiteral(".config/LiChenYang/input_quicker.log"));
}

QString InputQuickerManager::scriptFallbackPath(const QString &scriptName) const
{
    return QFileInfo(QStringLiteral(__FILE__)).absoluteDir().filePath(QStringLiteral("scripts/") + scriptName);
}

QString InputQuickerManager::resolveScriptPath(const QString &scriptName) const
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("scripts/") + scriptName),
        QDir(QDir::currentPath()).filePath(QStringLiteral("scripts/") + scriptName),
        scriptFallbackPath(scriptName)
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.constLast();
}

QString InputQuickerManager::daemonScriptPath() const
{
    return resolveScriptPath(QStringLiteral("input_quicker_daemon.py"));
}

QString InputQuickerManager::captureScriptPath() const
{
    return resolveScriptPath(QStringLiteral("capture_input_trigger.py"));
}

QString InputQuickerManager::monitorScriptPath() const
{
    return resolveScriptPath(QStringLiteral("monitor_input_device.py"));
}

QString InputQuickerManager::deviceUtilsScriptPath() const
{
    return resolveScriptPath(QStringLiteral("input_device_utils.py"));
}

bool InputQuickerManager::enabled() const
{
    return enabledValue;
}

bool InputQuickerManager::daemonAutostart() const
{
    return daemonAutostartValue;
}

QString InputQuickerManager::devicePath() const
{
    return devicePathValue;
}

QString InputQuickerManager::wheel2Axis() const
{
    return wheel2AxisValue;
}

bool InputQuickerManager::grabDevice() const
{
    return grabDeviceValue;
}

QList<QuickerBinding> InputQuickerManager::bindings() const
{
    return bindingsValue;
}

void InputQuickerManager::setEnabled(bool enabled)
{
    enabledValue = enabled;
}

void InputQuickerManager::setDaemonAutostart(bool enabled)
{
    daemonAutostartValue = enabled;
}

void InputQuickerManager::setDevicePath(const QString &path)
{
    devicePathValue = path;
}

void InputQuickerManager::setWheel2Axis(const QString &axis)
{
    wheel2AxisValue = axis.isEmpty() ? QStringLiteral("REL_HWHEEL") : axis;
}

void InputQuickerManager::setGrabDevice(bool grab)
{
    grabDeviceValue = grab;
}

void InputQuickerManager::setBindings(const QList<QuickerBinding> &bindings)
{
    bindingsValue = bindings;
    emit bindingsChanged();
}

void InputQuickerManager::addBinding(const QuickerBinding &binding)
{
    bindingsValue.append(binding);
    emit bindingsChanged();
}

bool InputQuickerManager::updateBinding(const QuickerBinding &binding)
{
    for (int i = 0; i < bindingsValue.size(); ++i) {
        if (bindingsValue.at(i).id == binding.id) {
            bindingsValue[i] = binding;
            emit bindingsChanged();
            return true;
        }
    }
    return false;
}

bool InputQuickerManager::removeBinding(const QString &id)
{
    for (int i = 0; i < bindingsValue.size(); ++i) {
        if (bindingsValue.at(i).id == id) {
            bindingsValue.removeAt(i);
            emit bindingsChanged();
            return true;
        }
    }
    return false;
}

QuickerBinding InputQuickerManager::bindingById(const QString &id) const
{
    for (const QuickerBinding &binding : bindingsValue) {
        if (binding.id == id) {
            return binding;
        }
    }
    return {};
}

bool InputQuickerManager::isTriggerUsed(const QJsonObject &trigger, const QString &excludeId) const
{
    const QString type = trigger.value(QStringLiteral("type")).toString();
    const QString code = trigger.value(QStringLiteral("code")).toString();
    const QString axis = trigger.value(QStringLiteral("axis")).toString();
    const QString direction = trigger.value(QStringLiteral("direction")).toString();

    for (const QuickerBinding &binding : bindingsValue) {
        if (!excludeId.isEmpty() && binding.id == excludeId) {
            continue;
        }
        const QJsonObject existing = binding.trigger;
        if (existing.value(QStringLiteral("type")).toString() != type) {
            continue;
        }
        if (type == QStringLiteral("mouse_button")
            && existing.value(QStringLiteral("code")).toString() == code) {
            return true;
        }
        if (type == QStringLiteral("wheel")
            && existing.value(QStringLiteral("axis")).toString() == axis
            && existing.value(QStringLiteral("direction")).toString() == direction) {
            return true;
        }
    }
    return false;
}

QList<QuickerBinding> InputQuickerManager::defaultWorkspaceBindings() const
{
    QList<QuickerBinding> defaults;

    auto makeBinding = [](const QString &id, const QString &name, const QJsonObject &trigger,
                          const QString &preset) {
        QuickerBinding binding;
        binding.id = id;
        binding.name = name;
        binding.enabled = true;
        binding.trigger = trigger;
        binding.action = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("preset")},
            {QStringLiteral("preset"), preset}
        };
        return binding;
    };

    defaults.append(makeBinding(
        QStringLiteral("default-side-prev"),
        QStringLiteral("上一工作区"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("mouse_button")},
                    {QStringLiteral("code"), QStringLiteral("BTN_SIDE")}},
        QStringLiteral("workspace_prev")));

    defaults.append(makeBinding(
        QStringLiteral("default-side-next"),
        QStringLiteral("下一工作区"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("mouse_button")},
                    {QStringLiteral("code"), QStringLiteral("BTN_EXTRA")}},
        QStringLiteral("workspace_next")));

    defaults.append(makeBinding(
        QStringLiteral("default-hwheel-prev"),
        QStringLiteral("第二滚轮上一工作区"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("wheel")},
                    {QStringLiteral("axis"), wheel2AxisValue},
                    {QStringLiteral("direction"), QStringLiteral("negative")}},
        QStringLiteral("workspace_prev")));

    defaults.append(makeBinding(
        QStringLiteral("default-hwheel-next"),
        QStringLiteral("第二滚轮下一工作区"),
        QJsonObject{{QStringLiteral("type"), QStringLiteral("wheel")},
                    {QStringLiteral("axis"), wheel2AxisValue},
                    {QStringLiteral("direction"), QStringLiteral("positive")}},
        QStringLiteral("workspace_next")));

    return defaults;
}

void InputQuickerManager::importDefaultWorkspaceBindings()
{
    const QList<QuickerBinding> defaults = defaultWorkspaceBindings();
    for (const QuickerBinding &binding : defaults) {
        if (!isTriggerUsed(binding.trigger)) {
            bindingsValue.append(binding);
        }
    }
    emit bindingsChanged();
}

bool InputQuickerManager::captureTrigger(const QString &devicePath, int timeoutMs,
                                         QJsonObject &triggerOut, QString &errorOut)
{
    const QString scriptPath = captureScriptPath();
    if (!QFileInfo::exists(scriptPath)) {
        errorOut = QStringLiteral("找不到录制脚本: %1").arg(scriptPath);
        return false;
    }

    QProcess process;
    QStringList args;
    args << scriptPath;
    if (!devicePath.isEmpty()) {
        args << QStringLiteral("--device") << devicePath;
    }
    args << QStringLiteral("--timeout") << QString::number(qMax(1000, timeoutMs) / 1000.0, 'f', 1);
    process.start(QStringLiteral("python3"), args);
    if (!process.waitForFinished(timeoutMs + 2000)) {
        process.kill();
        process.waitForFinished(1000);
        errorOut = QStringLiteral("录制超时，请在时限内按下鼠标键或滚动滚轮。");
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitCode() != 0) {
        errorOut = formatCaptureError(stderrText);
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
    if (!doc.isObject()) {
        errorOut = QStringLiteral("录制结果无效: %1").arg(output);
        return false;
    }

    triggerOut = doc.object();
    return true;
}

void InputQuickerManager::onDaemonOutput()
{
    const QString output = QString::fromLocal8Bit(daemonProcess->readAllStandardOutput()).trimmed();
    if (output.isEmpty()) {
        return;
    }
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        emit logMessage(QStringLiteral("[快捷助手] %1").arg(line.trimmed()));
    }
}

void InputQuickerManager::onDaemonError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    emit logMessage(QStringLiteral("[快捷助手] 进程错误: %1").arg(daemonProcess->errorString()));
    emitStatus();
}

void InputQuickerManager::onDaemonFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit) {
        emit logMessage(QStringLiteral("[快捷助手] daemon 异常退出"));
    } else {
        emit logMessage(QStringLiteral("[快捷助手] daemon 退出，代码: %1").arg(exitCode));
    }
    emitStatus();
}

void InputQuickerManager::onMonitorOutput()
{
    monitorLineBuffer += QString::fromLocal8Bit(monitorProcess->readAllStandardOutput());
    int newlineIndex = monitorLineBuffer.indexOf(QLatin1Char('\n'));
    while (newlineIndex >= 0) {
        const QString line = monitorLineBuffer.left(newlineIndex).trimmed();
        monitorLineBuffer.remove(0, newlineIndex + 1);
        newlineIndex = monitorLineBuffer.indexOf(QLatin1Char('\n'));

        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith(QStringLiteral("ERROR:"))) {
            emit logMessage(QStringLiteral("[快捷助手] %1").arg(formatCaptureError(line)));
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) {
            emit logMessage(QStringLiteral("[监听] %1").arg(line));
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString eventType = obj.value(QStringLiteral("event")).toString();
        if (eventType == QStringLiteral("started")) {
            emit monitorStarted(obj.value(QStringLiteral("path")).toString(),
                                obj.value(QStringLiteral("name")).toString());
            continue;
        }
        if (eventType == QStringLiteral("stopped")) {
            continue;
        }

        emit monitorEventReceived(obj);
    }
}

void InputQuickerManager::onMonitorError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    emit logMessage(QStringLiteral("[快捷助手] 监听错误: %1").arg(monitorProcess->errorString()));
}

void InputQuickerManager::onMonitorFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    if (exitCode != 0) {
        emit logMessage(QStringLiteral("[快捷助手] 输入监听已停止（退出代码 %1）").arg(exitCode));
    } else {
        emit logMessage(QStringLiteral("[快捷助手] 输入监听已停止"));
    }
    emit monitorStopped();
}

void InputQuickerManager::emitStatus()
{
    emit statusChanged(statusText());
}

QuickerBinding InputQuickerManager::bindingFromJson(const QJsonObject &obj) const
{
    QuickerBinding binding;
    binding.id = obj.value(QStringLiteral("id")).toString();
    binding.name = obj.value(QStringLiteral("name")).toString();
    binding.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    binding.trigger = obj.value(QStringLiteral("trigger")).toObject();
    binding.action = obj.value(QStringLiteral("action")).toObject();
    if (binding.id.isEmpty()) {
        binding.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    return binding;
}

QJsonObject InputQuickerManager::bindingToJson(const QuickerBinding &binding) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), binding.id);
    obj.insert(QStringLiteral("name"), binding.name);
    obj.insert(QStringLiteral("enabled"), binding.enabled);
    obj.insert(QStringLiteral("trigger"), binding.trigger);
    obj.insert(QStringLiteral("action"), binding.action);
    return obj;
}

void InputQuickerManager::writeJsonConfig() const
{
    QFileInfo info(configFilePath());
    QDir().mkpath(info.absolutePath());

    QJsonArray bindingsArray;
    for (const QuickerBinding &binding : bindingsValue) {
        bindingsArray.append(bindingToJson(binding));
    }

    QJsonObject root;
    root.insert(QStringLiteral("devicePath"), devicePathValue);
    root.insert(QStringLiteral("wheel2Axis"), wheel2AxisValue);
    root.insert(QStringLiteral("enabled"), enabledValue);
    root.insert(QStringLiteral("daemonAutostart"), daemonAutostartValue);
    root.insert(QStringLiteral("grabDevice"), grabDeviceValue);
    root.insert(QStringLiteral("bindings"), bindingsArray);

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit const_cast<InputQuickerManager *>(this)->logMessage(
            QStringLiteral("[快捷助手] 写入配置失败: %1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void InputQuickerManager::readJsonConfig()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bindingsValue = defaultWorkspaceBindings();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        bindingsValue = defaultWorkspaceBindings();
        return;
    }

    const QJsonObject root = doc.object();
    devicePathValue = root.value(QStringLiteral("devicePath")).toString();
    wheel2AxisValue = root.value(QStringLiteral("wheel2Axis")).toString(QStringLiteral("REL_HWHEEL"));
    enabledValue = root.value(QStringLiteral("enabled")).toBool(true);
    daemonAutostartValue = root.value(QStringLiteral("daemonAutostart")).toBool(true);
    grabDeviceValue = root.value(QStringLiteral("grabDevice")).toBool(false);

    bindingsValue.clear();
    const QJsonArray bindingsArray = root.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &value : bindingsArray) {
        if (!value.isObject()) {
            continue;
        }
        bindingsValue.append(bindingFromJson(value.toObject()));
    }
    if (bindingsValue.isEmpty()) {
        bindingsValue = defaultWorkspaceBindings();
    }
}

void InputQuickerManager::migrateLegacyConfigIfNeeded()
{
    const QString newPath = configFilePath();
    if (QFileInfo::exists(newPath)) {
        return;
    }

    const QString legacyPath = QDir::home().filePath(QStringLiteral(".config/LiChenYang/workspace_mouse.json"));
    if (!QFileInfo::exists(legacyPath)) {
        return;
    }

    QFile legacyFile(legacyPath);
    if (!legacyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QJsonDocument legacyDoc = QJsonDocument::fromJson(legacyFile.readAll());
    if (!legacyDoc.isObject()) {
        return;
    }

    const QJsonObject legacy = legacyDoc.object();
    devicePathValue = legacy.value(QStringLiteral("devicePath")).toString();
    wheel2AxisValue = legacy.value(QStringLiteral("wheel2Axis")).toString(QStringLiteral("REL_HWHEEL"));
    enabledValue = true;
    bindingsValue.clear();

    const QList<QuickerBinding> defaults = defaultWorkspaceBindings();
    if (legacy.value(QStringLiteral("sideButtonsEnabled")).toBool(true)) {
        bindingsValue.append(defaults.at(0));
        bindingsValue.append(defaults.at(1));
    }
    if (legacy.value(QStringLiteral("wheel2Enabled")).toBool(false)) {
        bindingsValue.append(defaults.at(2));
        bindingsValue.append(defaults.at(3));
    }
    if (bindingsValue.isEmpty()) {
        bindingsValue = defaultWorkspaceBindings();
    }

    writeJsonConfig();
    emit logMessage(QStringLiteral("[快捷助手] 已从旧版 workspace_mouse.json 迁移配置"));
}

qint64 InputQuickerManager::readDaemonPid() const
{
    QFile file(pidFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    bool ok = false;
    const qint64 pid = QString::fromUtf8(file.readAll().trimmed()).toLongLong(&ok);
    return ok ? pid : -1;
}

qint64 InputQuickerManager::findDaemonPidByProcess() const
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (!ok || pid <= 0) {
            continue;
        }
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (!cmdline.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (cmdline.readAll().contains("input_quicker_daemon.py")) {
            return pid;
        }
    }
    return -1;
}

qint64 InputQuickerManager::resolveDaemonPid() const
{
    const qint64 fromFile = readDaemonPid();
    if (fromFile > 0 && isPidAlive(fromFile)) {
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(fromFile));
        if (cmdline.open(QIODevice::ReadOnly)
            && cmdline.readAll().contains("input_quicker_daemon.py")) {
            return fromFile;
        }
    }
    return findDaemonPidByProcess();
}

bool InputQuickerManager::isPidAlive(qint64 pid) const
{
    if (pid <= 0) {
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
}

void InputQuickerManager::clearStalePidFile() const
{
    const qint64 pid = readDaemonPid();
    if (pid > 0 && isPidAlive(pid)) {
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cmdline.open(QIODevice::ReadOnly) && cmdline.readAll().contains("input_quicker_daemon.py")) {
            return;
        }
    }
    QFile::remove(pidFilePath());
}

void InputQuickerManager::writePidFile(qint64 pid) const
{
    if (pid <= 0) {
        return;
    }
    QDir().mkpath(QFileInfo(pidFilePath()).absolutePath());
    QFile file(pidFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }
    file.write(QByteArray::number(pid) + '\n');
}

QString InputQuickerManager::daemonAutostartDesktopPath() const
{
    const QString autostartDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                 + QStringLiteral("/autostart");
    return autostartDir + QStringLiteral("/input-quicker-daemon.desktop");
}

bool InputQuickerManager::isDaemonAutostartInstalled() const
{
    const QString desktopPath = daemonAutostartDesktopPath();
    if (!QFile::exists(desktopPath)) {
        return false;
    }
    QFile file(desktopPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    return content.contains(QStringLiteral("input_quicker_daemon.py"))
           && !content.contains(QStringLiteral("Hidden=true"));
}

bool InputQuickerManager::setDaemonAutostartInstalled(bool enabled)
{
    const QString desktopPath = daemonAutostartDesktopPath();
    const QFileInfo desktopInfo(desktopPath);

    if (!enabled) {
        if (QFile::exists(desktopPath) && !QFile::remove(desktopPath)) {
            return false;
        }
        daemonAutostartValue = false;
        writeJsonConfig();
        emit logMessage(QStringLiteral("[快捷助手] 已关闭 daemon 开机自启动"));
        return true;
    }

    QDir autostartDir = desktopInfo.dir();
    if (!autostartDir.exists() && !autostartDir.mkpath(QStringLiteral("."))) {
        return false;
    }

    const QString scriptPath = daemonScriptPath();
    QFile file(desktopPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Version=1.0\n";
    out << "Name=快捷助手输入守护\n";
    out << "Comment=Mouse side buttons / bindings via input_quicker_daemon\n";
    out << "Exec=env INPUT_QUICKER_CONFIG=\"" << configFilePath()
        << "\" INPUT_QUICKER_PID=\"" << pidFilePath()
        << "\" INPUT_QUICKER_LOG=\"" << logFilePath()
        << "\" python3 \"" << scriptPath << "\"\n";
    out << "Terminal=false\n";
    out << "Categories=Utility;\n";
    out << "X-GNOME-Autostart-enabled=true\n";
    out << "X-GNOME-Autostart-Delay=2\n";

    daemonAutostartValue = true;
    writeJsonConfig();
    emit logMessage(QStringLiteral("[快捷助手] 已写入开机自启动: %1").arg(desktopPath));
    return true;
}

int InputQuickerManager::migrateXbindkeysSideButtons()
{
    // xbindkeys b:9 -> left workspace, b:8 -> right workspace
    // Map to common Linux evdev codes BTN_SIDE / BTN_EXTRA.
    const QList<QuickerBinding> sideDefaults = {
        defaultWorkspaceBindings().value(0),
        defaultWorkspaceBindings().value(1)
    };

    int added = 0;
    for (const QuickerBinding &binding : sideDefaults) {
        if (!isTriggerUsed(binding.trigger)) {
            bindingsValue.append(binding);
            ++added;
        }
    }

    // Also cover mice that report BTN_BACK / BTN_FORWARD instead.
    auto makeAlt = [](const QString &id, const QString &name, const QString &code, const QString &preset) {
        QuickerBinding binding;
        binding.id = id;
        binding.name = name;
        binding.enabled = true;
        binding.trigger = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mouse_button")},
            {QStringLiteral("code"), code}
        };
        binding.action = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("preset")},
            {QStringLiteral("preset"), preset}
        };
        return binding;
    };

    const QList<QuickerBinding> alts = {
        makeAlt(QStringLiteral("migrated-btn-back"), QStringLiteral("后退键上一工作区"),
                QStringLiteral("BTN_BACK"), QStringLiteral("workspace_prev")),
        makeAlt(QStringLiteral("migrated-btn-forward"), QStringLiteral("前进键下一工作区"),
                QStringLiteral("BTN_FORWARD"), QStringLiteral("workspace_next"))
    };
    for (const QuickerBinding &binding : alts) {
        if (!isTriggerUsed(binding.trigger)) {
            bindingsValue.append(binding);
            ++added;
        }
    }

    if (added > 0) {
        emit bindingsChanged();
        emit logMessage(QStringLiteral("[快捷助手] 已从 xbindkeys 侧键行为迁移 %1 条规则").arg(added));
    } else {
        emit logMessage(QStringLiteral("[快捷助手] 侧键规则已存在，无需重复迁移"));
    }
    return added;
}

bool InputQuickerManager::disableSystemXbindkeys()
{
    const QString noautoPath = QDir::home().filePath(QStringLiteral(".xbindkeys.noauto"));
    QFile noauto(noautoPath);
    if (!noauto.exists()) {
        if (!noauto.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit logMessage(QStringLiteral("[快捷助手] 无法创建 %1").arg(noautoPath));
            return false;
        }
        noauto.write("# Created by Input Quicker to disable xbindkeys autostart\n");
        noauto.close();
    }

    QProcess::execute(QStringLiteral("killall"), QStringList() << QStringLiteral("xbindkeys"));
    emit logMessage(QStringLiteral("[快捷助手] 已禁用系统 xbindkeys（%1）").arg(noautoPath));
    return true;
}

QStringList InputQuickerManager::pollDaemonLog(qint64 &offset) const
{
    QStringList lines;
    QFile file(logFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return lines;
    }
    if (offset > file.size()) {
        offset = 0;
    }
    if (!file.seek(offset)) {
        return lines;
    }
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine();
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    offset = file.pos();
    return lines;
}
