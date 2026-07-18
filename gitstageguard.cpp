#include "gitstageguard.h"

#include "platformprefs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QTextStream>

namespace {

const qint64 kSuspiciousSizeBytes = 2LL * 1024 * 1024;

QString normalizeRepoPath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1String("./")))
        path = path.mid(2);
    return path;
}

QString fileNameOf(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

QString extensionLower(const QString &path)
{
    const QString name = fileNameOf(path);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return QString();
    return name.mid(dot + 1).toLower();
}

bool pathUnderDir(const QString &path, const QString &dirPrefix)
{
    const QString p = normalizeRepoPath(path);
    QString d = dirPrefix;
    if (d.endsWith(QLatin1Char('/')))
        d.chop(1);
    return p == d || p.startsWith(d + QLatin1Char('/'));
}

bool matchesKnownLogName(const QString &path)
{
    const QString name = fileNameOf(normalizeRepoPath(path)).toLower();
    if (name == QLatin1String("build.log")
        || name == QLatin1String("build_log.txt")
        || name == QLatin1String("system_check.log")
        || name == QLatin1String("build_output.txt")) {
        return true;
    }
    if (name.startsWith(QLatin1String("build_error")) && name.endsWith(QLatin1String(".txt")))
        return true;
    return false;
}

QString unquotePorcelainPath(QString path)
{
    path = path.trimmed();
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
        path.replace(QLatin1String("\\n"), QLatin1String("\n"));
        path.replace(QLatin1String("\\t"), QLatin1String("\t"));
        path.replace(QLatin1String("\\\""), QLatin1String("\""));
        path.replace(QLatin1String("\\\\"), QLatin1String("\\"));
    }
    return normalizeRepoPath(path);
}

} // namespace

QHash<QString, QString> GitStageGuard::ignoredPathReasons(const QString &repoDir,
                                                          const QStringList &paths)
{
    QHash<QString, QString> result;
    if (repoDir.trimmed().isEmpty() || paths.isEmpty())
        return result;

    QByteArray stdinData;
    for (const QString &raw : paths) {
        const QString path = normalizeRepoPath(raw);
        if (path.isEmpty())
            continue;
        stdinData += path.toUtf8();
        stdinData += '\0';
    }
    if (stdinData.isEmpty())
        return result;

    // --no-index: apply .gitignore / exclude even for already-tracked paths.
    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(PlatformPrefs::gitBinary(),
                  {QStringLiteral("check-ignore"), QStringLiteral("--no-index"),
                   QStringLiteral("-v"), QStringLiteral("-z"), QStringLiteral("--stdin")});
    if (!process.waitForStarted(5000))
        return result;
    process.write(stdinData);
    process.closeWriteChannel();
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        return result;
    }

    // Verbose -z: each record is "source:line:pattern\0path\0" (git 2.x) or
    // "source\0line\0pattern\0path\0". Prefer parsing by finding path as last field
    // of each pair when using non-z-friendly split: use line mode fallback if needed.
    const QByteArray raw = process.readAllStandardOutput();
    if (raw.isEmpty())
        return result;

    // git check-ignore -v -z outputs: <source> <NUL> <linenum> <NUL> <pattern> <NUL> <pathname> <NUL>
    // (documented for -z with --verbose). Some versions use colon form without inner NULs.
    // Detect format: if first field looks like ".gitignore:12:*.log" containing two colons before path.
    int i = 0;
    QStringList fields;
    while (i < raw.size()) {
        int next = raw.indexOf('\0', i);
        if (next < 0)
            next = raw.size();
        fields << QString::fromUtf8(raw.mid(i, next - i));
        i = next + 1;
    }
    while (!fields.isEmpty() && fields.last().isEmpty())
        fields.removeLast();

    auto addReason = [&](const QString &path, const QString &source, const QString &pattern) {
        const QString p = normalizeRepoPath(path);
        if (p.isEmpty())
            return;
        QString reason = QStringLiteral("匹配忽略规则");
        if (!pattern.isEmpty())
            reason += QStringLiteral(": %1").arg(pattern);
        if (!source.isEmpty())
            reason += QStringLiteral(" (%1)").arg(source);
        result.insert(p, reason);
    };

    // Format A: groups of 4: source, linenum, pattern, path
    if (fields.size() >= 4 && fields.size() % 4 == 0
        && !fields.at(0).contains(QLatin1Char(':'))) {
        for (int f = 0; f + 3 < fields.size(); f += 4) {
            const QString source = fields.at(f);
            const QString line = fields.at(f + 1);
            const QString pattern = fields.at(f + 2);
            const QString path = fields.at(f + 3);
            addReason(path, source + QLatin1Char(':') + line, pattern);
        }
        return result;
    }

    // Format B: alternating "source:line:pattern" and path (or single colon-form + path)
    for (int f = 0; f < fields.size(); ++f) {
        const QString &item = fields.at(f);
        if (item.isEmpty())
            continue;
        // colon-form detail line
        if (item.contains(QLatin1Char(':'))) {
            QString path;
            if (f + 1 < fields.size() && !fields.at(f + 1).contains(QLatin1Char(':'))) {
                path = fields.at(f + 1);
                ++f;
            }
            // Parse source:linenum:pattern — pattern may contain colons rarely
            const int c1 = item.indexOf(QLatin1Char(':'));
            const int c2 = item.indexOf(QLatin1Char(':'), c1 + 1);
            QString source = item;
            QString pattern;
            if (c1 >= 0 && c2 > c1) {
                source = item.left(c2);
                pattern = item.mid(c2 + 1);
            }
            if (path.isEmpty()) {
                // Sometimes pathname is after a tab in the same field
                const int tab = item.lastIndexOf(QLatin1Char('\t'));
                if (tab >= 0) {
                    path = item.mid(tab + 1);
                    const QString head = item.left(tab);
                    const int hc1 = head.indexOf(QLatin1Char(':'));
                    const int hc2 = head.indexOf(QLatin1Char(':'), hc1 + 1);
                    if (hc1 >= 0 && hc2 > hc1) {
                        source = head.left(hc2);
                        pattern = head.mid(hc2 + 1);
                    }
                }
            }
            if (!path.isEmpty())
                addReason(path, source, pattern);
        } else {
            // path-only (non-verbose leftover)
            addReason(item, QString(), QString());
        }
    }

    return result;
}

bool GitStageGuard::isExtensionLessDangerous(const QString &path, QString *reason)
{
    const QString p = normalizeRepoPath(path);
    const QString name = fileNameOf(p);
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;

    // "file.txt" / ".gitignore" → has a suffix after a dot; not treated as extension-less.
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0)
        return false;

    static const QSet<QString> kKeep = {
        QStringLiteral("makefile"),      QStringLiteral("gnumakefile"),
        QStringLiteral("license"),       QStringLiteral("licence"),
        QStringLiteral("copying"),       QStringLiteral("authors"),
        QStringLiteral("news"),          QStringLiteral("install"),
        QStringLiteral("changelog"),     QStringLiteral("changes"),
        QStringLiteral("dockerfile"),    QStringLiteral("containerfile"),
        QStringLiteral("gemfile"),       QStringLiteral("rakefile"),
        QStringLiteral("procfile"),      QStringLiteral("vagrantfile"),
        QStringLiteral("configure"),     QStringLiteral("configure.ac"),
        QStringLiteral("sconstruct"),    QStringLiteral("sconscript"),
        QStringLiteral("tags"),
    };
    if (kKeep.contains(name.toLower()))
        return false;

    if (reason)
        *reason = QStringLiteral("无扩展名文件");
    return true;
}

bool GitStageGuard::isBlockedPath(const QString &repoDir, const QString &path, QString *reason)
{
    const QString p = normalizeRepoPath(path);
    if (p.isEmpty())
        return false;

    if (isExtensionLessDangerous(p, reason))
        return true;

    if (repoDir.trimmed().isEmpty())
        return false;

    const QHash<QString, QString> map = ignoredPathReasons(repoDir, {p});
    if (!map.contains(p)) {
        if (reason)
            reason->clear();
        return false;
    }
    if (reason)
        *reason = map.value(p);
    return true;
}

bool GitStageGuard::isSuspiciousUntracked(const QString &path, qint64 sizeBytes, QString *reason)
{
    const QString ext = extensionLower(path);
    static const QSet<QString> kSuspiciousExt = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("csv"),
        QStringLiteral("tsv"), QStringLiteral("xlsx"), QStringLiteral("xls"),
        QStringLiteral("ods")
    };
    if (kSuspiciousExt.contains(ext)) {
        if (reason)
            *reason = QStringLiteral("未跟踪的文本/表格文件 (*.%1)").arg(ext);
        return true;
    }
    if (sizeBytes >= kSuspiciousSizeBytes) {
        if (reason)
            *reason = QStringLiteral("未跟踪大文件 (>2MB)");
        return true;
    }
    return false;
}

GitStageRisk GitStageGuard::classify(const QString &repoDir, const QString &path, bool untracked,
                                     qint64 sizeBytes, QString *reason)
{
    QString blockedReason;
    if (isBlockedPath(repoDir, path, &blockedReason)) {
        if (reason)
            *reason = blockedReason;
        return GitStageRisk::Blocked;
    }
    if (untracked) {
        QString suspiciousReason;
        if (isSuspiciousUntracked(path, sizeBytes, &suspiciousReason)) {
            if (reason)
                *reason = suspiciousReason;
            return GitStageRisk::Suspicious;
        }
    }
    if (reason)
        reason->clear();
    return GitStageRisk::Normal;
}

QString GitStageGuard::riskLabel(GitStageRisk risk)
{
    switch (risk) {
    case GitStageRisk::Blocked:
        return QStringLiteral("危险");
    case GitStageRisk::Suspicious:
        return QStringLiteral("可疑");
    case GitStageRisk::Normal:
    default:
        return QStringLiteral("正常");
    }
}

QVector<GitStageEntry> GitStageGuard::collectPending(const QString &repoDir, QString *error)
{
    QVector<GitStageEntry> entries;
    if (repoDir.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("仓库目录为空");
        return entries;
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(PlatformPrefs::gitBinary(),
                  {QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("-z"),
                   QStringLiteral("-uall")});
    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("无法启动 git");
        return entries;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error)
            *error = QStringLiteral("git status --porcelain 超时");
        return entries;
    }
    if (process.exitCode() != 0) {
        if (error) {
            const QString err = PlatformPrefs::decodeProcessOutput(process.readAllStandardError());
            *error = err.isEmpty() ? QStringLiteral("git status --porcelain 失败") : err;
        }
        return entries;
    }
    const QByteArray raw = process.readAllStandardOutput();

    int i = 0;
    while (i < raw.size()) {
        int next = raw.indexOf('\0', i);
        if (next < 0)
            next = raw.size();
        const QByteArray chunk = raw.mid(i, next - i);
        i = next + 1;
        if (chunk.size() < 3)
            continue;

        const QString xy = QString::fromUtf8(chunk.left(2));
        QString pathPart = QString::fromUtf8(chunk.mid(3));

        if (!xy.isEmpty()
            && (xy.at(0) == QLatin1Char('R') || xy.at(0) == QLatin1Char('C'))
            && i < raw.size()) {
            int next2 = raw.indexOf('\0', i);
            if (next2 < 0)
                next2 = raw.size();
            pathPart = QString::fromUtf8(raw.mid(i, next2 - i));
            i = next2 + 1;
        }

        const QString path = unquotePorcelainPath(pathPart);
        if (path.isEmpty())
            continue;

        GitStageEntry entry;
        entry.path = path;
        entry.xyStatus = xy;
        entry.untracked = (xy == QLatin1String("??"));

        const QFileInfo fi(QDir(repoDir).filePath(path));
        entry.sizeBytes = fi.exists() ? fi.size() : -1;
        entries.push_back(entry);
    }

    QStringList allPaths;
    allPaths.reserve(entries.size());
    for (const GitStageEntry &e : entries)
        allPaths << e.path;

    const QHash<QString, QString> ignored = ignoredPathReasons(repoDir, allPaths);

    for (GitStageEntry &entry : entries) {
        QString noExtReason;
        if (isExtensionLessDangerous(entry.path, &noExtReason)) {
            entry.risk = GitStageRisk::Blocked;
            entry.reason = noExtReason;
            continue;
        }
        if (ignored.contains(entry.path)) {
            entry.risk = GitStageRisk::Blocked;
            entry.reason = ignored.value(entry.path);
            continue;
        }
        if (entry.untracked) {
            QString suspiciousReason;
            if (isSuspiciousUntracked(entry.path, entry.sizeBytes, &suspiciousReason)) {
                entry.risk = GitStageRisk::Suspicious;
                entry.reason = suspiciousReason;
                continue;
            }
        }
        entry.risk = GitStageRisk::Normal;
        entry.reason.clear();
    }

    return entries;
}

bool GitStageGuard::isSourceLikeExtension(const QString &extensionLower)
{
    static const QSet<QString> kSource = {
        QStringLiteral("c"),    QStringLiteral("cc"),   QStringLiteral("cpp"),
        QStringLiteral("cxx"),  QStringLiteral("h"),    QStringLiteral("hh"),
        QStringLiteral("hpp"),  QStringLiteral("hxx"),  QStringLiteral("pro"),
        QStringLiteral("pri"),  QStringLiteral("qml"),  QStringLiteral("qrc"),
        QStringLiteral("ui"),   QStringLiteral("cmake"), QStringLiteral("in"),
        QStringLiteral("py"),   QStringLiteral("js"),   QStringLiteral("ts"),
        QStringLiteral("tsx"),  QStringLiteral("jsx"),  QStringLiteral("java"),
        QStringLiteral("go"),   QStringLiteral("rs"),   QStringLiteral("cs"),
        QStringLiteral("rb"),   QStringLiteral("swift"), QStringLiteral("m"),
        QStringLiteral("mm"),   QStringLiteral("s"),    QStringLiteral("asm"),
        QStringLiteral("ld"),   QStringLiteral("lds"),
    };
    return kSource.contains(extensionLower);
}

QStringList GitStageGuard::suggestIgnorePatterns(const QStringList &paths, bool skipSourceLike)
{
    QSet<QString> patterns;

    for (const QString &raw : paths) {
        const QString path = normalizeRepoPath(raw);
        if (path.isEmpty() || path == QLatin1String(".gitignore"))
            continue;

        if (pathUnderDir(path, QStringLiteral("monitor_logs"))
            || path.contains(QLatin1String("/monitor_logs/"))) {
            patterns.insert(QStringLiteral("monitor_logs/"));
        }

        if (matchesKnownLogName(path)) {
            patterns.insert(fileNameOf(path));
        }

        const QString ext = extensionLower(path);
        const QString name = fileNameOf(path);
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const bool noExt = (dot < 0);

        if (!noExt) {
            if (skipSourceLike && isSourceLikeExtension(ext))
                continue;
            if (!ext.isEmpty())
                patterns.insert(QStringLiteral("*.") + ext);
            continue;
        }

        // Extension-less: ignore by relative path and basename.
        // Do NOT add "dir/*" + "!dir/*.*" — the negation re-includes *.csv under that dir
        // and overrides earlier "*.csv" rules (last match wins in gitignore).
        patterns.insert(path);
        patterns.insert(name);
    }

    QStringList list = patterns.values();
    list.sort();
    return list;
}

QStringList GitStageGuard::trackedIgnoredPaths(const QString &repoDir)
{
    QStringList result;
    if (repoDir.trimmed().isEmpty())
        return result;

    QProcess ls;
    ls.setWorkingDirectory(repoDir);
    ls.start(PlatformPrefs::gitBinary(), {QStringLiteral("ls-files"), QStringLiteral("-z")});
    if (!ls.waitForStarted(5000) || !ls.waitForFinished(30000) || ls.exitCode() != 0)
        return result;

    const QByteArray raw = ls.readAllStandardOutput();
    QStringList tracked;
    int i = 0;
    while (i < raw.size()) {
        int next = raw.indexOf('\0', i);
        if (next < 0)
            next = raw.size();
        const QString path = QString::fromUtf8(raw.mid(i, next - i)).trimmed();
        i = next + 1;
        if (!path.isEmpty())
            tracked << path;
    }
    if (tracked.isEmpty())
        return result;

    const QHash<QString, QString> ignored = ignoredPathReasons(repoDir, tracked);
    QStringList keys = ignored.keys();
    keys.sort();
    return keys;
}

bool GitStageGuard::appendIgnorePatterns(const QString &repoDir, const QStringList &patterns,
                                         QString *error)
{
    if (patterns.isEmpty())
        return false;

    const QString ignorePath = QDir(repoDir).filePath(QStringLiteral(".gitignore"));
    QFile file(ignorePath);
    QString existing;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral("无法读取 .gitignore");
            return false;
        }
        existing = QString::fromUtf8(file.readAll());
        file.close();
    }

    QSet<QString> have;
    for (const QString &raw : existing.split(QLatin1Char('\n'))) {
        QString line = raw.trimmed();
        line.remove(QLatin1Char('\r'));
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            have.insert(line);
    }

    QStringList toAdd;
    for (const QString &p : patterns) {
        const QString line = p.trimmed();
        if (line.isEmpty() || have.contains(line))
            continue;
        toAdd << line;
        have.insert(line);
    }
    if (toAdd.isEmpty())
        return false;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法写入 .gitignore");
        return false;
    }
    QTextStream out(&file);
    if (!existing.isEmpty() && !existing.endsWith(QLatin1Char('\n')))
        out << QLatin1Char('\n');
    out << QStringLiteral("\n# Added by Assistant staging review\n");
    for (const QString &line : toAdd)
        out << line << QLatin1Char('\n');
    file.close();
    return true;
}
