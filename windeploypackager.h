#ifndef WINDEPLOYPACKAGER_H
#define WINDEPLOYPACKAGER_H

#include <QFileInfo>
#include <QObject>
#include <QString>

struct WinDeployRequest {
    QString repoDir;
    QString exePath;
    QString outputDir;
    QString qtDir;
};

class WinDeployPackager : public QObject
{
    Q_OBJECT
public:
    explicit WinDeployPackager(QObject *parent = nullptr);

    static QString defaultQtDir();
    static QString findWindeployqt(const QString &qtDir);
    static QString guessMingwBin(const QString &qtDir);
    static QFileInfo findLatestExe(const QString &repoDir,
                                   const QString &skipAbsolutePath = QString());
    static QString defaultOutputDir(const QString &exePath);
    static QString repoSettingsKey(const QString &repoDir);
    static bool isSkippedDeployPath(const QString &relativePath);

    bool pack(const WinDeployRequest &req, QString *errorOut = nullptr);

signals:
    void logMessage(const QString &line);

private:
    void log(const QString &line);
    bool copyOneFile(const QString &src, const QString &destDir, QString *errorOut);
    bool copySiblingRuntimeFiles(const QString &exePath, const QString &destDir, QString *errorOut);
    bool copyProjectIniFiles(const QString &repoDir, const QString &exeDir, const QString &destDir,
                             QString *errorOut);
    bool copyScriptsDir(const QString &repoDir, const QString &destDir, QString *errorOut);
    bool copyDirRecursively(const QString &src, const QString &dst, QString *errorOut);
    bool runWindeployqt(const QString &qtDir, const QString &destExe, QString *errorOut);
    bool verifyPackage(const QString &outputDir, QString *errorOut);
};

#endif // WINDEPLOYPACKAGER_H
