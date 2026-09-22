#include "windeploypackager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

bool pathLooksSkipped(const QString &rel)
{
    const QString n = QDir::fromNativeSeparators(rel);
    return n.contains(QStringLiteral("/.git/")) || n.startsWith(QStringLiteral(".git/"))
        || n.contains(QStringLiteral("/.venv/")) || n.startsWith(QStringLiteral(".venv/"))
        || n.contains(QStringLiteral("/node_modules/"))
        || n.startsWith(QStringLiteral("node_modules/"));
}

bool relativeInRelease(const QString &rel)
{
    const QString n = QDir::fromNativeSeparators(rel);
    return n.startsWith(QStringLiteral("release/")) || n.contains(QStringLiteral("/release/"));
}

QString joinPath(const QString &dir, const QString &name)
{
    return QDir(dir).filePath(name);
}

} // namespace

WinDeployPackager::WinDeployPackager(QObject *parent)
    : QObject(parent)
{
}

QString WinDeployPackager::defaultQtDir()
{
    const QString prefix = QLibraryInfo::location(QLibraryInfo::PrefixPath);
    if (QFile::exists(joinPath(prefix, QStringLiteral("bin/windeployqt.exe")))
        || QFile::exists(joinPath(prefix, QStringLiteral("bin/windeployqt")))) {
        return QDir(prefix).absolutePath();
    }

    const QString fallback = QStringLiteral("D:/Qt/5.15.2/mingw81_64");
    if (QDir(fallback).exists()) {
        return fallback;
    }
    return prefix;
}

QString WinDeployPackager::findWindeployqt(const QString &qtDir)
{
    const QString bin = QDir(qtDir).filePath(QStringLiteral("bin"));
#ifdef Q_OS_WIN
    const QString exe = QDir(bin).filePath(QStringLiteral("windeployqt.exe"));
#else
    const QString exe = QDir(bin).filePath(QStringLiteral("windeployqt"));
#endif
    if (QFileInfo::exists(exe)) {
        return QFileInfo(exe).absoluteFilePath();
    }

    const QString fromApp = QDir(QLibraryInfo::location(QLibraryInfo::BinariesPath))
#ifdef Q_OS_WIN
                                .filePath(QStringLiteral("windeployqt.exe"));
#else
                                .filePath(QStringLiteral("windeployqt"));
#endif
    if (QFileInfo::exists(fromApp)) {
        return QFileInfo(fromApp).absoluteFilePath();
    }
    return QString();
}

QString WinDeployPackager::guessMingwBin(const QString &qtDir)
{
    const QFileInfo gccInQt(QDir(qtDir).filePath(QStringLiteral("bin/gcc.exe")));
    if (gccInQt.exists()) {
        return gccInQt.absolutePath();
    }

    QDir tools(QDir(qtDir).absolutePath() + QStringLiteral("/../../Tools"));
    if (!tools.exists()) {
        return QString();
    }
    const QStringList names = tools.entryList(QStringList() << QStringLiteral("mingw*"),
                                              QDir::Dirs | QDir::NoDotAndDotDot);
    QString preferred;
    for (const QString &name : names) {
        const QString bin = tools.filePath(name + QStringLiteral("/bin"));
        if (!QFile::exists(QDir(bin).filePath(QStringLiteral("gcc.exe")))) {
            continue;
        }
        if (name.contains(QStringLiteral("64"))) {
            return QDir(bin).absolutePath();
        }
        if (preferred.isEmpty()) {
            preferred = QDir(bin).absolutePath();
        }
    }
    return preferred;
}

bool WinDeployPackager::isSkippedDeployPath(const QString &relativePath)
{
    return pathLooksSkipped(relativePath);
}

QFileInfo WinDeployPackager::findLatestExe(const QString &repoDir, const QString &skipAbsolutePath)
{
    if (repoDir.trimmed().isEmpty() || !QDir(repoDir).exists()) {
        return QFileInfo();
    }

    const QString skip = QDir::cleanPath(skipAbsolutePath);
    QFileInfo bestRelease;
    QFileInfo bestAny;
    QDateTime bestReleaseTime;
    QDateTime bestAnyTime;

    QDirIterator it(repoDir, QStringList() << QStringLiteral("*.exe"), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fileInfo = it.fileInfo();
        const QString absPath = QDir::cleanPath(fileInfo.absoluteFilePath());
        if (!skip.isEmpty() && absPath.compare(skip, Qt::CaseInsensitive) == 0) {
            continue;
        }

        const QString rel = QDir(repoDir).relativeFilePath(absPath);
        if (pathLooksSkipped(rel)) {
            continue;
        }

        const QString name = fileInfo.fileName().toLower();
        if (name == QLatin1String("windeployqt.exe") || name.startsWith(QLatin1String("moc"))
            || name.startsWith(QLatin1String("uic")) || name.startsWith(QLatin1String("rcc"))) {
            continue;
        }

        if (!bestAny.exists() || fileInfo.lastModified() > bestAnyTime) {
            bestAny = fileInfo;
            bestAnyTime = fileInfo.lastModified();
        }
        if (relativeInRelease(rel)) {
            if (!bestRelease.exists() || fileInfo.lastModified() > bestReleaseTime) {
                bestRelease = fileInfo;
                bestReleaseTime = fileInfo.lastModified();
            }
        }
    }

    if (bestRelease.exists()) {
        return bestRelease;
    }
    return bestAny;
}

QString WinDeployPackager::defaultOutputDir(const QString &exePath)
{
    const QString base = QFileInfo(exePath).completeBaseName();
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktop.isEmpty()) {
        return QDir::home().filePath(base.isEmpty() ? QStringLiteral("win-deploy") : base);
    }
    return QDir(desktop).filePath(base.isEmpty() ? QStringLiteral("win-deploy") : base);
}

QString WinDeployPackager::repoSettingsKey(const QString &repoDir)
{
    return QString::fromLatin1(QDir(repoDir).absolutePath().toUtf8().toHex());
}

void WinDeployPackager::log(const QString &line)
{
    emit logMessage(line);
}

bool WinDeployPackager::copyOneFile(const QString &src, const QString &destDir, QString *errorOut)
{
    const QFileInfo srcInfo(src);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        return true;
    }
    if (!QDir().mkpath(destDir)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法创建目录: %1").arg(QDir::toNativeSeparators(destDir));
        }
        return false;
    }
    const QString dest = QDir(destDir).filePath(srcInfo.fileName());
    if (QDir::cleanPath(srcInfo.absoluteFilePath())
            .compare(QDir::cleanPath(dest), Qt::CaseInsensitive)
        == 0) {
        return true;
    }
    if (QFile::exists(dest) && !QFile::remove(dest)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法覆盖: %1（文件可能正在使用）")
                            .arg(QDir::toNativeSeparators(dest));
        }
        return false;
    }
    if (!QFile::copy(srcInfo.absoluteFilePath(), dest)) {
        if (errorOut) {
            *errorOut = QStringLiteral("复制失败: %1 -> %2")
                            .arg(QDir::toNativeSeparators(srcInfo.absoluteFilePath()),
                                 QDir::toNativeSeparators(dest));
        }
        return false;
    }
    log(QStringLiteral("拷贝 %1").arg(srcInfo.fileName()));
    return true;
}

bool WinDeployPackager::copySiblingRuntimeFiles(const QString &exePath, const QString &destDir,
                                                QString *errorOut)
{
    const QDir exeDir = QFileInfo(exePath).absoluteDir();
    const QFileInfoList files = exeDir.entryInfoList(QStringList() << QStringLiteral("*.dll")
                                                                   << QStringLiteral("*.ini"),
                                                     QDir::Files);
    for (const QFileInfo &fi : files) {
        if (!copyOneFile(fi.absoluteFilePath(), destDir, errorOut)) {
            return false;
        }
    }
    return true;
}

bool WinDeployPackager::copyProjectIniFiles(const QString &repoDir, const QString &exeDir,
                                            const QString &destDir, QString *errorOut)
{
    auto copyInisFrom = [&](const QString &dir) {
        if (dir.isEmpty() || !QDir(dir).exists()) {
            return true;
        }
        if (QDir::cleanPath(dir).compare(QDir::cleanPath(exeDir), Qt::CaseInsensitive) == 0) {
            return true;
        }
        const QFileInfoList files = QDir(dir).entryInfoList(QStringList() << QStringLiteral("*.ini"),
                                                            QDir::Files);
        for (const QFileInfo &fi : files) {
            if (!copyOneFile(fi.absoluteFilePath(), destDir, errorOut)) {
                return false;
            }
        }
        return true;
    };

    if (!copyInisFrom(repoDir)) {
        return false;
    }
    if (!copyInisFrom(QDir(repoDir).filePath(QStringLiteral("release")))) {
        return false;
    }
    return true;
}

bool WinDeployPackager::copyDirRecursively(const QString &src, const QString &dst, QString *errorOut)
{
    QDir source(src);
    if (!source.exists()) {
        return true;
    }
    if (!QDir().mkpath(dst)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法创建目录: %1").arg(QDir::toNativeSeparators(dst));
        }
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
    for (const QFileInfo &info : entries) {
        const QString destPath = QDir(dst).filePath(info.fileName());
        if (info.isDir()) {
            if (!copyDirRecursively(info.absoluteFilePath(), destPath, errorOut)) {
                return false;
            }
        } else {
            if (QFile::exists(destPath) && !QFile::remove(destPath)) {
                if (errorOut) {
                    *errorOut = QStringLiteral("无法覆盖: %1").arg(QDir::toNativeSeparators(destPath));
                }
                return false;
            }
            if (!QFile::copy(info.absoluteFilePath(), destPath)) {
                if (errorOut) {
                    *errorOut = QStringLiteral("复制失败: %1")
                                    .arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                }
                return false;
            }
        }
    }
    return true;
}

bool WinDeployPackager::copyScriptsDir(const QString &repoDir, const QString &destDir, QString *errorOut)
{
    const QString src = QDir(repoDir).filePath(QStringLiteral("scripts"));
    if (!QDir(src).exists()) {
        return true;
    }
    log(QStringLiteral("拷贝 scripts/"));
    return copyDirRecursively(src, QDir(destDir).filePath(QStringLiteral("scripts")), errorOut);
}

bool WinDeployPackager::runWindeployqt(const QString &qtDir, const QString &destExe, QString *errorOut)
{
#ifndef Q_OS_WIN
    if (errorOut) {
        *errorOut = QStringLiteral("windeployqt 仅能在 Windows 上运行。");
    }
    return false;
#else
    const QString tool = findWindeployqt(qtDir);
    if (tool.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("找不到 windeployqt。请填写正确的 Qt 目录（含 bin/windeployqt.exe）。");
        }
        return false;
    }

    QStringList args;
    args << QStringLiteral("--force") << QStringLiteral("--compiler-runtime")
         << QDir::toNativeSeparators(destExe);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString qtBin = QDir(qtDir).filePath(QStringLiteral("bin"));
    const QString mingwBin = guessMingwBin(qtDir);
    QString path = env.value(QStringLiteral("PATH"));
    QString prefix = QDir::toNativeSeparators(qtBin);
    if (!mingwBin.isEmpty()) {
        prefix = QDir::toNativeSeparators(mingwBin) + QLatin1Char(';') + prefix;
    }
    env.insert(QStringLiteral("PATH"), prefix + QLatin1Char(';') + path);

    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    log(QStringLiteral("运行 %1 %2").arg(QDir::toNativeSeparators(tool), args.join(QLatin1Char(' '))));
    proc.start(tool, args);
    if (!proc.waitForStarted(10000)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法启动 windeployqt: %1").arg(proc.errorString());
        }
        return false;
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        if (errorOut) {
            *errorOut = QStringLiteral("windeployqt 超时。");
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(proc.readAll());
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                           Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        log(line);
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("windeployqt 失败（退出码 %1）").arg(proc.exitCode());
        }
        return false;
    }
    return true;
#endif
}

bool WinDeployPackager::verifyPackage(const QString &outputDir, QString *errorOut)
{
    const QDir out(outputDir);
    const QString qwindows = out.filePath(QStringLiteral("platforms/qwindows.dll"));
    if (!QFile::exists(qwindows)) {
        if (errorOut) {
            *errorOut = QStringLiteral("缺少 platforms/qwindows.dll，工控机无法启动界面。");
        }
        return false;
    }

    const QStringList mingw = {QStringLiteral("libgcc_s_seh-1.dll"), QStringLiteral("libstdc++-6.dll"),
                               QStringLiteral("libwinpthread-1.dll")};
    QStringList missing;
    for (const QString &name : mingw) {
        if (!QFile::exists(out.filePath(name))) {
            missing << name;
        }
    }
    if (!missing.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("缺少 MinGW 运行库: %1（需要 --compiler-runtime）")
                            .arg(missing.join(QStringLiteral(", ")));
        }
        return false;
    }
    return true;
}

bool WinDeployPackager::pack(const WinDeployRequest &req, QString *errorOut)
{
    if (errorOut) {
        errorOut->clear();
    }

#ifndef Q_OS_WIN
    if (errorOut) {
        *errorOut = QStringLiteral("一键 Windows 打包只能在 Windows 上使用，Linux 请继续用 SCP。");
    }
    return false;
#else
    const QString exe = QDir::cleanPath(req.exePath.trimmed());
    const QString outDir = QDir::cleanPath(req.outputDir.trimmed());
    const QString repo = QDir::cleanPath(req.repoDir.trimmed());
    QString qtDir = req.qtDir.trimmed();
    if (qtDir.isEmpty()) {
        qtDir = defaultQtDir();
    }

    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        if (errorOut) {
            *errorOut = QStringLiteral("找不到可执行文件，请先在 Qt Creator 中编译 Release。");
        }
        return false;
    }
    if (!exe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        if (errorOut) {
            *errorOut = QStringLiteral("请选择 Windows 的 .exe 文件。");
        }
        return false;
    }
    if (outDir.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("请选择输出目录。");
        }
        return false;
    }

    log(QStringLiteral("工程: %1").arg(QDir::toNativeSeparators(repo)));
    log(QStringLiteral("exe: %1").arg(QDir::toNativeSeparators(exe)));
    log(QStringLiteral("输出: %1").arg(QDir::toNativeSeparators(outDir)));
    log(QStringLiteral("Qt: %1").arg(QDir::toNativeSeparators(qtDir)));

    if (!QDir().mkpath(outDir)) {
        if (errorOut) {
            *errorOut = QStringLiteral("无法创建输出目录: %1").arg(QDir::toNativeSeparators(outDir));
        }
        return false;
    }

    if (!copyOneFile(exe, outDir, errorOut)) {
        return false;
    }
    const QString destExe = QDir(outDir).filePath(QFileInfo(exe).fileName());
    const QString exeDir = QFileInfo(exe).absolutePath();
    if (!copySiblingRuntimeFiles(exe, outDir, errorOut)) {
        return false;
    }
    if (!copyProjectIniFiles(repo, exeDir, outDir, errorOut)) {
        return false;
    }
    if (!copyScriptsDir(repo, outDir, errorOut)) {
        return false;
    }
    if (!runWindeployqt(qtDir, destExe, errorOut)) {
        return false;
    }
    if (!verifyPackage(outDir, errorOut)) {
        return false;
    }

    log(QStringLiteral("完成。把整个输出文件夹拷到工控机即可，不必安装 Qt。"));
    return true;
#endif
}
