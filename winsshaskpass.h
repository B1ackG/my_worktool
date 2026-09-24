#ifndef WINSSHASKPASS_H
#define WINSSHASKPASS_H

#include <QProcess>
#include <QString>

// Windows-only OpenSSH launcher. Password login uses SSH_ASKPASS instead of sshpass.
namespace WinSsh {

bool startSsh(QProcess *process, const QString &targetIp, const QString &remoteCommand,
              const QString &password, QString *errorOut);

bool startScp(QProcess *process, const QString &source, const QString &destination,
              const QString &password, QString *errorOut);

} // namespace WinSsh

#endif // WINSSHASKPASS_H
