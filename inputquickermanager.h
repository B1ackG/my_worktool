#ifndef INPUTQUICKERMANAGER_H
#define INPUTQUICKERMANAGER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

struct QuickerBinding {
    QString id;
    QString name;
    bool enabled = true;
    QJsonObject trigger;
    QJsonObject action;
};

struct InputQuickerEnvCheck {
    bool evdevOk = false;
    bool xdotoolOk = false;
    bool inputReadable = false;
    bool inInputGroup = false;
    bool inputGroupConfigured = false;
    QStringList messages;
    bool allOk() const { return evdevOk && xdotoolOk && inputReadable; }
};

class InputQuickerManager : public QObject
{
    Q_OBJECT

public:
    struct DeviceInfo {
        QString path;
        QString name;
        int score = 0;
        QString capsSummary;
        bool accessible = false;
        bool recommended = false;
        QString error;
    };

    explicit InputQuickerManager(QObject *parent = nullptr);
    ~InputQuickerManager() override;

    void loadSettings();
    void saveSettings() const;
    bool applySettings();
    bool startDaemon();
    void stopDaemon();
    QList<DeviceInfo> refreshDeviceList() const;

    bool isDaemonRunning() const;
    qint64 daemonPid() const;
    bool isMonitorRunning() const;
    QString statusText() const;
    QString configFilePath() const;
    QString pidFilePath() const;
    QString logFilePath() const;
    QString daemonScriptPath() const;
    QString captureScriptPath() const;
    QString monitorScriptPath() const;
    QString deviceUtilsScriptPath() const;
    QString daemonAutostartDesktopPath() const;

    bool enabled() const;
    bool daemonAutostart() const;
    QString devicePath() const;
    QString wheel2Axis() const;
    bool grabDevice() const;
    QList<QuickerBinding> bindings() const;

    void setEnabled(bool enabled);
    void setDaemonAutostart(bool enabled);
    void setDevicePath(const QString &path);
    void setWheel2Axis(const QString &axis);
    void setGrabDevice(bool grab);
    void setBindings(const QList<QuickerBinding> &bindings);

    void addBinding(const QuickerBinding &binding);
    bool updateBinding(const QuickerBinding &binding);
    bool removeBinding(const QString &id);
    QuickerBinding bindingById(const QString &id) const;
    bool isTriggerUsed(const QJsonObject &trigger, const QString &excludeId = QString()) const;

    QList<QuickerBinding> defaultWorkspaceBindings() const;
    void importDefaultWorkspaceBindings();

    bool isDaemonAutostartInstalled() const;
    bool setDaemonAutostartInstalled(bool enabled);
    int migrateXbindkeysSideButtons();
    bool disableSystemXbindkeys();
    QStringList pollDaemonLog(qint64 &offset) const;

    bool captureTrigger(const QString &devicePath, int timeoutMs, QJsonObject &triggerOut, QString &errorOut);
    QString formatCaptureError(const QString &stderrText) const;

    bool startInputMonitor(const QString &devicePath);
    void stopInputMonitor();

    InputQuickerEnvCheck runEnvironmentCheck() const;

signals:
    void statusChanged(const QString &status);
    void logMessage(const QString &message);
    void bindingsChanged();
    void monitorEventReceived(const QJsonObject &event);
    void monitorStarted(const QString &devicePath, const QString &deviceName);
    void monitorStopped();

private slots:
    void onDaemonOutput();
    void onDaemonError(QProcess::ProcessError error);
    void onDaemonFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onMonitorOutput();
    void onMonitorError(QProcess::ProcessError error);
    void onMonitorFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString scriptFallbackPath(const QString &scriptName) const;
    QString resolveScriptPath(const QString &scriptName) const;
    void emitStatus();
    void writeJsonConfig() const;
    void readJsonConfig();
    void migrateLegacyConfigIfNeeded();
    QuickerBinding bindingFromJson(const QJsonObject &obj) const;
    QJsonObject bindingToJson(const QuickerBinding &binding) const;
    QList<DeviceInfo> refreshDeviceListFromProc() const;
    bool reloadDaemonViaSignal();
    bool daemonNeedsRestart() const;
    qint64 readDaemonPid() const;
    qint64 findDaemonPidByProcess() const;
    qint64 resolveDaemonPid() const;
    bool isPidAlive(qint64 pid) const;
    void clearStalePidFile() const;
    void writePidFile(qint64 pid) const;

    QProcess *daemonProcess;
    QProcess *monitorProcess;
    bool enabledValue;
    bool daemonAutostartValue;
    QString devicePathValue;
    QString wheel2AxisValue;
    bool grabDeviceValue;
    QString lastAppliedDevicePath;
    bool lastAppliedGrabDevice;
    QList<QuickerBinding> bindingsValue;
    QString monitorLineBuffer;
    bool ownDaemonProcess;
};

#endif // INPUTQUICKERMANAGER_H
