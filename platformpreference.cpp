#include "platformpreference.h"

#include <QSettings>

PlatformPreference &PlatformPreference::instance()
{
    static PlatformPreference pref;
    return pref;
}

PlatformPreference::PlatformPreference(QObject *parent)
    : QObject(parent)
    , m_mode(defaultMode())
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const int stored = settings.value(settingsKey(), static_cast<int>(defaultMode())).toInt();
    if (stored == static_cast<int>(Linux) || stored == static_cast<int>(Windows)) {
        m_mode = static_cast<Mode>(stored);
    }
}

PlatformPreference::Mode PlatformPreference::mode() const
{
    return m_mode;
}

void PlatformPreference::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(settingsKey(), static_cast<int>(m_mode));
    emit modeChanged(m_mode);
}

bool PlatformPreference::linuxFeaturesAvailable() const
{
#ifdef Q_OS_LINUX
    return isLinux();
#else
    return false;
#endif
}

bool PlatformPreference::windowsFeaturesAvailable() const
{
#ifdef Q_OS_WIN
    return isWindows();
#else
    return false;
#endif
}

PlatformPreference::Mode PlatformPreference::defaultMode()
{
#ifdef Q_OS_WIN
    return Windows;
#else
    return Linux;
#endif
}

QString PlatformPreference::modeDisplayName(Mode mode)
{
    return mode == Windows ? QStringLiteral("Windows") : QStringLiteral("Linux");
}

QString PlatformPreference::settingsKey()
{
    return QStringLiteral("platform/mode");
}
