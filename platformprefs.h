#ifndef PLATFORMPREFS_H
#define PLATFORMPREFS_H

#include <QByteArray>
#include <QString>

namespace PlatformPrefs {

enum class PlatformMode {
    Linux = 0,
    Windows = 1
};

PlatformMode mode();
void setMode(PlatformMode mode);
bool preferWindows();

QString gitBinary();
QString decodeProcessOutput(const QByteArray &raw);

QString modeToString(PlatformMode mode);
PlatformMode modeFromString(const QString &value);
PlatformMode defaultMode();

} // namespace PlatformPrefs

#endif // PLATFORMPREFS_H
