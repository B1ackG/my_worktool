#include "platformprefs.h"

#include <QSettings>

namespace PlatformPrefs {
namespace {

const QString kOrg = QStringLiteral("LiChenYang");
const QString kApp = QStringLiteral("LinuxHelper");
const QString kKey = QStringLiteral("settings/platformMode");

} // namespace

PlatformMode defaultMode()
{
#ifdef Q_OS_WIN
    return PlatformMode::Windows;
#else
    return PlatformMode::Linux;
#endif
}

QString modeToString(PlatformMode m)
{
    return m == PlatformMode::Windows ? QStringLiteral("windows") : QStringLiteral("linux");
}

PlatformMode modeFromString(const QString &value)
{
    const QString v = value.trimmed().toLower();
    if (v == QStringLiteral("windows") || v == QStringLiteral("win")) {
        return PlatformMode::Windows;
    }
    if (v == QStringLiteral("linux") || v == QStringLiteral("unix")) {
        return PlatformMode::Linux;
    }
    return defaultMode();
}

PlatformMode mode()
{
    QSettings settings(kOrg, kApp);
    if (!settings.contains(kKey)) {
        return defaultMode();
    }
    return modeFromString(settings.value(kKey).toString());
}

void setMode(PlatformMode m)
{
    QSettings settings(kOrg, kApp);
    settings.setValue(kKey, modeToString(m));
}

bool preferWindows()
{
    return mode() == PlatformMode::Windows;
}

QString gitBinary()
{
    return preferWindows() ? QStringLiteral("git.exe") : QStringLiteral("git");
}

QString decodeProcessOutput(const QByteArray &raw)
{
    if (preferWindows()) {
        return QString::fromUtf8(raw);
    }
    return QString::fromLocal8Bit(raw);
}

} // namespace PlatformPrefs
