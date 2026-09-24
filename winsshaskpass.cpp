#include "winsshaskpass.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace WinSsh {
namespace {

const char kAskPassSource[] =
    "using System;\r\n"
    "class SshAskPass {\r\n"
    "    static void Main() {\r\n"
    "        string p = Environment.GetEnvironmentVariable(\"WORKASSISTANT_SSH_PASS\");\r\n"
    "        if (p != null) {\r\n"
    "            Console.OutputEncoding = new System.Text.UTF8Encoding(false);\r\n"
    "            Console.Write(p);\r\n"
    "        }\r\n"
    "    }\r\n"
    "}\r\n";

QString findCsc()
{
    const QString winDir = QString::fromLocal8Bit(qgetenv("WINDIR"));
    const QStringList candidates = {
        QDir(winDir).filePath(QStringLiteral("Microsoft.NET/Framework64/v4.0.30319/csc.exe")),
        QDir(winDir).filePath(QStringLiteral("Microsoft.NET/Framework/v4.0.30319/csc.exe")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("csc"));
}

QString shortPathIfNeeded(const QString &path)
{
    const QString native = QDir::toNativeSeparators(path);
    if (!native.contains(QLatin1Char(' '))) {
        return native;
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetShortPathNameW(reinterpret_cast<const wchar_t *>(native.utf16()), buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return native;
    }
    return QDir::toNativeSeparators(QString::fromWCharArray(buf));
}

QString askPassExecutable(QString *errorOut)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法创建 SSH 密码辅助程序目录");
        }
        return QString();
    }

    const QString nativeDir = shortPathIfNeeded(QDir(dir).absolutePath());
    const QString exePath = QDir(nativeDir).filePath(QStringLiteral("ssh-askpass-v1.exe"));
    if (QFileInfo::exists(exePath) && QFileInfo(exePath).size() > 0) {
        return shortPathIfNeeded(QFileInfo(exePath).absoluteFilePath());
    }

    const QString srcPath = QDir(dir).filePath(QStringLiteral("ssh-askpass-v1.cs"));
    QFile src(srcPath);
    if (!src.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法写入 SSH 密码辅助程序源码");
        }
        return QString();
    }
    src.write(kAskPassSource);
    src.close();

    const QString csc = findCsc();
    if (csc.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral(
                "未找到 csc.exe，无法为 SSH 密码登录生成辅助程序。请改用 SSH 密钥并留空密码，"
                "或安装 .NET Framework 4");
        }
        return QString();
    }

    QProcess compiler;
    compiler.start(csc, {QStringLiteral("/nologo"), QStringLiteral("/target:exe"),
                         QStringLiteral("/out:") + QDir::toNativeSeparators(exePath),
                         QDir::toNativeSeparators(srcPath)});
    if (!compiler.waitForStarted(5000) || !compiler.waitForFinished(20000)
        || compiler.exitStatus() != QProcess::NormalExit || compiler.exitCode() != 0
        || !QFileInfo::exists(exePath)) {
        const QString detail = QString::fromLocal8Bit(compiler.readAllStandardError()
                                                       + compiler.readAllStandardOutput())
                                   .trimmed();
        if (errorOut) {
            *errorOut = QStringLiteral("编译 SSH 密码辅助程序失败。%1")
                            .arg(detail.isEmpty() ? QStringLiteral("请改用 SSH 密钥并留空密码。")
                                                  : detail);
        }
        return QString();
    }
    return shortPathIfNeeded(QFileInfo(exePath).absoluteFilePath());
}

bool prepareProcess(QProcess *process, const QString &password, QString *errorOut)
{
    if (!process) {
        if (errorOut) {
            *errorOut = QStringLiteral("内部错误：未创建进程");
        }
        return false;
    }
    if (password.isEmpty()) {
        return true;
    }

    const QString askPass = askPassExecutable(errorOut);
    if (askPass.isEmpty()) {
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SSH_ASKPASS"), QDir::toNativeSeparators(askPass));
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    env.insert(QStringLiteral("DISPLAY"), QStringLiteral("1"));
    env.insert(QStringLiteral("WORKASSISTANT_SSH_PASS"), password);
    process->setProcessEnvironment(env);
    return true;
}

QStringList authOptions(const QString &password)
{
    QStringList args;
    // 工控机重刷后主机密钥会变。known_hosts 里的旧记录会让 OpenSSH 关掉密码登录，
    // 只设 StrictHostKeyChecking=no 挡不住。传输时不读、不写用户的 known_hosts。
    args << QStringLiteral("-o") << QStringLiteral("StrictHostKeyChecking=no")
         << QStringLiteral("-o") << QStringLiteral("UserKnownHostsFile=/dev/null")
         << QStringLiteral("-o") << QStringLiteral("GlobalKnownHostsFile=/dev/null");
    if (password.isEmpty()) {
        args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    } else {
        args << QStringLiteral("-o") << QStringLiteral("NumberOfPasswordPrompts=1");
    }
    return args;
}

QString programOrError(const QString &name, QString *errorOut)
{
    const QString path = QStandardPaths::findExecutable(name);
    if (path.isEmpty() && errorOut) {
        *errorOut = QStringLiteral("未找到 %1。请启用 Windows 可选功能中的 OpenSSH 客户端。").arg(name);
    }
    return path;
}

} // namespace

bool startSsh(QProcess *process, const QString &targetIp, const QString &remoteCommand,
              const QString &password, QString *errorOut)
{
    if (!prepareProcess(process, password, errorOut)) {
        return false;
    }
    const QString program = programOrError(QStringLiteral("ssh"), errorOut);
    if (program.isEmpty()) {
        return false;
    }
    QStringList args = authOptions(password);
    args << QStringLiteral("root@%1").arg(targetIp) << remoteCommand;
    process->start(program, args);
    return true;
}

bool startScp(QProcess *process, const QString &source, const QString &destination,
              const QString &password, QString *errorOut)
{
    if (!prepareProcess(process, password, errorOut)) {
        return false;
    }
    const QString program = programOrError(QStringLiteral("scp"), errorOut);
    if (program.isEmpty()) {
        return false;
    }
    QStringList args = authOptions(password);
    args << source << destination;
    process->start(program, args);
    return true;
}

} // namespace WinSsh
