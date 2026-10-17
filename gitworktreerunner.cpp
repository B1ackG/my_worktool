#include "gitworktreerunner.h"
#include "platformprefs.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString decodeGitOutput(const QByteArray &raw)
{
    return PlatformPrefs::decodeProcessOutput(raw);
}

QString limitLines(const QString &text, int maxLines)
{
    if (maxLines <= 0) {
        return text;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
#else
    const QStringList lines = text.split(QLatin1Char('\n'), QString::KeepEmptyParts);
#endif
    if (lines.size() <= maxLines) {
        return text;
    }
    QStringList head = lines.mid(0, maxLines);
    head << QStringLiteral("... (%1 more lines truncated)").arg(lines.size() - maxLines);
    return head.join(QLatin1Char('\n'));
}

} // namespace

QString GitWorktreeRunner::gitBinary()
{
    return PlatformPrefs::gitBinary();
}

bool GitWorktreeRunner::finishProcess(QProcess &process, int timeoutMs)
{
    const bool finished = timeoutMs < 0 ? process.waitForFinished(-1)
                                        : process.waitForFinished(timeoutMs);
    if (!finished && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
    }
    return finished;
}

bool GitWorktreeRunner::runInRepo(const QString &repoDir, const QStringList &args,
                                  QString *stdoutOut, QString *stderrOut, int timeoutMs)
{
    if (repoDir.trimmed().isEmpty()) {
        if (stderrOut) {
            *stderrOut = QStringLiteral("empty repo dir");
        }
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(repoDir);
    process.start(gitBinary(), args);
    if (!finishProcess(process, timeoutMs)) {
        if (stderrOut) {
            *stderrOut = QStringLiteral("git timed out: %1").arg(args.join(QLatin1Char(' ')));
        }
        return false;
    }

    if (stdoutOut) {
        *stdoutOut = decodeGitOutput(process.readAllStandardOutput());
    } else {
        process.readAllStandardOutput();
    }
    if (stderrOut) {
        *stderrOut = decodeGitOutput(process.readAllStandardError());
    } else {
        process.readAllStandardError();
    }
    return process.exitCode() == 0;
}

QString GitWorktreeRunner::normalizeLocalBranchRef(const QString &branchRef)
{
    QString b = branchRef.trimmed();
    if (b.startsWith(QStringLiteral("+ "))) {
        b = b.mid(2).trimmed();
    }
    if (b.startsWith(QStringLiteral("refs/heads/"))) {
        b = b.mid(QStringLiteral("refs/heads/").size());
    }
    if (b.startsWith(QStringLiteral("heads/"))) {
        b = b.mid(6);
    }
    return b;
}

QString GitWorktreeRunner::slugifyForPath(const QString &text)
{
    QString s = text.trimmed();
    s.replace(QLatin1Char('/'), QLatin1Char('-'));
    s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    while (s.contains(QStringLiteral("--"))) {
        s.replace(QStringLiteral("--"), QStringLiteral("-"));
    }
    if (s.startsWith(QLatin1Char('-'))) {
        s = s.mid(1);
    }
    if (s.endsWith(QLatin1Char('-'))) {
        s.chop(1);
    }
    if (s.isEmpty()) {
        s = QStringLiteral("worktree");
    }
    return s;
}

QString GitWorktreeRunner::defaultWorktreePath(const QString &mainRepoDir, const QString &branchOrSlug)
{
    const QString root = QDir(mainRepoDir).absolutePath();
    const QString slug = slugifyForPath(branchOrSlug);
    return QDir(root).filePath(QStringLiteral(".worktrees/%1").arg(slug));
}

QString GitWorktreeRunner::checkedOutBranch(const QString &repoDir)
{
    QString out;
    if (!runInRepo(repoDir, {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")},
                   &out)) {
        return QString();
    }
    const QString branch = out.trimmed();
    if (branch == QStringLiteral("HEAD")) {
        return QString(); // detached
    }
    return branch;
}

bool GitWorktreeRunner::isDirty(const QString &repoDir)
{
    QString out;
    if (!runInRepo(repoDir, {QStringLiteral("status"), QStringLiteral("--porcelain")}, &out)) {
        return false;
    }
    return !out.trimmed().isEmpty();
}

QDateTime GitWorktreeRunner::lastActivity(const QString &repoDir)
{
    QString out;
    if (runInRepo(repoDir, {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%ci")},
                  &out)
        && !out.trimmed().isEmpty()) {
        // e.g. 2026-07-15 12:00:00 +0800
        QDateTime dt = QDateTime::fromString(out.trimmed(), Qt::ISODate);
        if (!dt.isValid()) {
            dt = QDateTime::fromString(out.trimmed().left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        if (dt.isValid()) {
            return dt;
        }
    }
    const QFileInfo fi(repoDir);
    return fi.lastModified();
}

int GitWorktreeRunner::countCommitsAhead(const QString &mainRepoDir, const QString &mainBranch,
                                         const QString &otherBranch)
{
    if (mainBranch.isEmpty() || otherBranch.isEmpty()) {
        return 0;
    }
    QString out;
    const QString range = QStringLiteral("%1..%2").arg(mainBranch, otherBranch);
    if (!runInRepo(mainRepoDir, {QStringLiteral("rev-list"), QStringLiteral("--count"), range}, &out)) {
        return 0;
    }
    bool ok = false;
    const int n = out.trimmed().toInt(&ok);
    return ok ? n : 0;
}

QStringList GitWorktreeRunner::listCommitsAhead(const QString &mainRepoDir, const QString &mainBranch,
                                                const QString &otherBranch)
{
    QStringList result;
    if (mainBranch.isEmpty() || otherBranch.isEmpty()) {
        return result;
    }
    QString out;
    const QString range = QStringLiteral("%1..%2").arg(mainBranch, otherBranch);
    if (!runInRepo(mainRepoDir,
                   {QStringLiteral("log"), QStringLiteral("--oneline"), range}, &out)) {
        return result;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif
    for (const QString &line : lines) {
        result << line.trimmed();
    }
    return result;
}

QString GitWorktreeRunner::diffBranches(const QString &mainRepoDir, const QString &mainBranch,
                                        const QString &otherBranch, int maxLines)
{
    if (mainBranch.isEmpty() || otherBranch.isEmpty()) {
        return QString();
    }
    QString out;
    const QString range = QStringLiteral("%1...%2").arg(mainBranch, otherBranch);
    runInRepo(mainRepoDir, {QStringLiteral("diff"), range}, &out);
    return limitLines(out, maxLines);
}

QString GitWorktreeRunner::diffWorktree(const QString &worktreeDir, int maxLines)
{
    QString out;
    runInRepo(worktreeDir, {QStringLiteral("diff")}, &out);
    QString staged;
    runInRepo(worktreeDir, {QStringLiteral("diff"), QStringLiteral("--cached")}, &staged);
    QString combined = out;
    if (!staged.trimmed().isEmpty()) {
        if (!combined.isEmpty() && !combined.endsWith(QLatin1Char('\n'))) {
            combined += QLatin1Char('\n');
        }
        combined += QStringLiteral("--- staged ---\n") + staged;
    }
    return limitLines(combined, maxLines);
}

QStringList GitWorktreeRunner::localBranches(const QString &repoDir)
{
    QStringList result;
    QString out;
    if (!runInRepo(repoDir,
                   {QStringLiteral("branch"), QStringLiteral("--format=%(refname:short)")}, &out)) {
        return result;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif
    for (const QString &line : lines) {
        const QString b = line.trimmed();
        if (!b.isEmpty()) {
            result << b;
        }
    }
    return result;
}

QStringList GitWorktreeRunner::branchesInUse(const QString &repoDir)
{
    QStringList used;
    QString out;
    if (!runInRepo(repoDir,
                   {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")},
                   &out)) {
        return used;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif
    const QString prefix = QStringLiteral("refs/heads/");
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("branch "))) {
            QString ref = line.mid(7).trimmed();
            if (ref.startsWith(prefix)) {
                used << ref.mid(prefix.size());
            }
        }
    }
    return used;
}

QVector<GitCommitRef> GitWorktreeRunner::listRecentCommits(const QString &repoDir, int limit)
{
    QVector<GitCommitRef> commits;
    if (limit < 1) {
        limit = 1;
    }
    QString out;
    if (!runInRepo(repoDir,
                   {QStringLiteral("log"), QStringLiteral("-n"), QString::number(limit),
                    QStringLiteral("--date=short"),
                    QStringLiteral("--pretty=format:%H%x09%h%x09%ad%x09%s")},
                   &out)) {
        return commits;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif
    for (const QString &line : lines) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        const QStringList parts = line.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
#else
        const QStringList parts = line.split(QLatin1Char('\t'), QString::KeepEmptyParts);
#endif
        if (parts.size() < 4) {
            continue;
        }
        GitCommitRef ref;
        ref.hash = parts.at(0).trimmed();
        ref.shortHash = parts.at(1).trimmed();
        ref.date = parts.at(2).trimmed();
        ref.subject = parts.mid(3).join(QStringLiteral("\t")).trimmed();
        if (ref.hash.isEmpty()) {
            continue;
        }
        commits.append(ref);
    }
    return commits;
}

bool GitWorktreeRunner::resolveCommit(const QString &repoDir, const QString &rev, QString *fullOut,
                                      QString *shortOut, QString *errorOut)
{
    const QString spec = rev.trimmed();
    if (spec.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("请选择或输入一次提交。");
        }
        return false;
    }
    QString so;
    QString se;
    if (!runInRepo(repoDir,
                   {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                    spec + QStringLiteral("^{commit}")},
                   &so, &se)) {
        if (errorOut) {
            const QString err = se.trimmed().isEmpty() ? so.trimmed() : se.trimmed();
            *errorOut = err.isEmpty() ? QStringLiteral("无法识别该提交。") : err;
        }
        return false;
    }
    const QString full = so.trimmed();
    QString shortSo;
    if (!runInRepo(repoDir, {QStringLiteral("rev-parse"), QStringLiteral("--short"), full}, &shortSo)) {
        shortSo = full.left(7);
    }
    if (fullOut) {
        *fullOut = full;
    }
    if (shortOut) {
        const QString shortHash = shortSo.trimmed();
        *shortOut = shortHash.isEmpty() ? full.left(7) : shortHash;
    }
    return true;
}

QStringList GitWorktreeRunner::topLevelProFiles(const QString &dir)
{
    QStringList result;
    const QDir d(dir);
    const QStringList names = d.entryList({QStringLiteral("*.pro")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        result << d.absoluteFilePath(name);
    }
    return result;
}

QString GitWorktreeRunner::locateQtCreator()
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const QString saved = settings.value(QStringLiteral("QtCreatorBinary")).toString().trimmed();
    if (!saved.isEmpty() && QFileInfo(saved).isExecutable()) {
        return saved;
    }

    const QString found = QStandardPaths::findExecutable(QStringLiteral("qtcreator"));
    if (!found.isEmpty()) {
        return found;
    }

#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("qtcreator.exe");
#else
    const QString exeName = QStringLiteral("qtcreator");
#endif
    const QString rel = QStringLiteral("Tools/QtCreator/bin/") + exeName;
    QStringList candidates;
    const QString homeQt = QDir::home().filePath(QStringLiteral("Qt"));
    candidates << QDir(homeQt).filePath(rel);
    candidates << QDir(QStringLiteral("/opt")).filePath(QStringLiteral("qtcreator/bin/") + exeName);
    candidates << QDir(QStringLiteral("/usr/bin")).filePath(exeName);
#ifdef Q_OS_WIN
    candidates << QDir(QStringLiteral("C:/Qt")).filePath(rel);
#endif

    const QDir base(homeQt);
    if (base.exists()) {
        const QStringList subs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sub : subs) {
            candidates << base.filePath(sub + QLatin1Char('/') + rel);
        }
    }
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QString();
}

void GitWorktreeRunner::rememberQtCreator(const QString &program)
{
    const QString path = program.trimmed();
    if (path.isEmpty()) {
        return;
    }
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    settings.setValue(QStringLiteral("QtCreatorBinary"), path);
}

QString GitWorktreeRunner::pathForBranch(const QString &repoDir, const QString &branchName)
{
    const QString target = normalizeLocalBranchRef(branchName);
    if (target.isEmpty()) {
        return QString();
    }

    QString out;
    if (!runInRepo(repoDir,
                   {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")},
                   &out)) {
        return QString();
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::SkipEmptyParts);
#endif

    QString currentPath;
    QString currentBranch;
    const QString prefix = QStringLiteral("refs/heads/");

    auto checkCurrent = [&]() -> QString {
        if (!currentPath.isEmpty() && !currentBranch.isEmpty()
            && currentBranch.compare(target, Qt::CaseInsensitive) == 0) {
            return currentPath;
        }
        return QString();
    };

    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("worktree "))) {
            const QString hit = checkCurrent();
            if (!hit.isEmpty()) {
                return hit;
            }
            currentPath = line.mid(9).trimmed();
            currentBranch.clear();
        } else if (line.startsWith(QStringLiteral("branch "))) {
            const QString ref = line.mid(7).trimmed();
            if (ref.startsWith(prefix)) {
                currentBranch = ref.mid(prefix.size());
            }
            const QString hit = checkCurrent();
            if (!hit.isEmpty()) {
                return hit;
            }
        }
    }
    return checkCurrent();
}

QVector<GitWorktreeEntry> GitWorktreeRunner::listWorktrees(const QString &mainRepoDir,
                                                           const QString &mainBranch)
{
    QVector<GitWorktreeEntry> entries;
    QString out;
    if (!runInRepo(mainRepoDir,
                   {QStringLiteral("worktree"), QStringLiteral("list"), QStringLiteral("--porcelain")},
                   &out)) {
        return entries;
    }

    const QString mainAbs = QDir(mainRepoDir).absolutePath();
    const QString resolvedMainBranch =
        mainBranch.isEmpty() ? checkedOutBranch(mainRepoDir) : mainBranch;

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList lines = out.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
#else
    const QStringList lines = out.split(QLatin1Char('\n'), QString::KeepEmptyParts);
#endif

    GitWorktreeEntry current;
    bool have = false;
    const QString prefix = QStringLiteral("refs/heads/");

    auto flush = [&]() {
        if (!have || current.path.isEmpty()) {
            return;
        }
        current.isMain = (QDir(current.path).absolutePath() == mainAbs);
        current.isDirty = isDirty(current.path);
        current.lastActivity = lastActivity(current.path);
        if (!current.isDetached && !current.branch.isEmpty() && !resolvedMainBranch.isEmpty()
            && current.branch != resolvedMainBranch) {
            current.commitCountAhead =
                countCommitsAhead(mainRepoDir, resolvedMainBranch, current.branch);
        }
        entries.append(current);
        current = GitWorktreeEntry();
        have = false;
    };

    for (const QString &raw : lines) {
        const QString line = raw.trimmed().isEmpty() && raw.isEmpty() ? raw : raw;
        if (line.startsWith(QStringLiteral("worktree "))) {
            flush();
            current.path = line.mid(9).trimmed();
            current.isDetached = false;
            current.branch.clear();
            current.head.clear();
            have = true;
        } else if (line.startsWith(QStringLiteral("HEAD "))) {
            current.head = line.mid(5).trimmed();
        } else if (line.startsWith(QStringLiteral("branch "))) {
            const QString ref = line.mid(7).trimmed();
            if (ref.startsWith(prefix)) {
                current.branch = ref.mid(prefix.size());
                current.isDetached = false;
            }
        } else if (line == QStringLiteral("detached")) {
            current.isDetached = true;
            current.branch.clear();
        } else if (line.isEmpty()) {
            flush();
        }
    }
    flush();
    return entries;
}

GitWorktreeApplyPlan GitWorktreeRunner::buildApplyPlan(const QString &mainDir,
                                                       const QString &worktreeDir)
{
    GitWorktreeApplyPlan plan;
    plan.mainDir = QDir(mainDir).absolutePath();
    plan.worktreeDir = QDir(worktreeDir).absolutePath();
    plan.mainBranch = checkedOutBranch(plan.mainDir);
    plan.worktreeBranch = checkedOutBranch(plan.worktreeDir);
    plan.isDetached = plan.worktreeBranch.isEmpty();
    plan.mainDirty = isDirty(plan.mainDir);
    plan.worktreeDirty = isDirty(plan.worktreeDir);

    if (!plan.isDetached && !plan.mainBranch.isEmpty() && !plan.worktreeBranch.isEmpty()) {
        plan.aheadCommitLines = listCommitsAhead(plan.mainDir, plan.mainBranch, plan.worktreeBranch);
        plan.aheadCommitCount = plan.aheadCommitLines.size();
    }

    QStringList previewParts;
    if (plan.aheadCommitCount > 0) {
        previewParts << QStringLiteral("=== commits (%1..%2) ===")
                            .arg(plan.mainBranch, plan.worktreeBranch);
        previewParts << plan.aheadCommitLines.join(QLatin1Char('\n'));
        previewParts << QString();
        previewParts << QStringLiteral("=== branch diff ===");
        previewParts << diffBranches(plan.mainDir, plan.mainBranch, plan.worktreeBranch);
    }
    if (plan.worktreeDirty) {
        previewParts << QString();
        previewParts << QStringLiteral("=== uncommitted (worktree) ===");
        previewParts << diffWorktree(plan.worktreeDir);
    }
    if (previewParts.isEmpty()) {
        previewParts << QStringLiteral("(no differences to apply)");
    }
    plan.previewDiff = previewParts.join(QLatin1Char('\n'));

    QStringList summaryBits;
    if (plan.mainDirty) {
        summaryBits << QStringLiteral("主目录有未提交改动（将阻止迁回）");
    }
    if (plan.aheadCommitCount > 0) {
        summaryBits << QStringLiteral("将 merge %1 个独有提交").arg(plan.aheadCommitCount);
    }
    if (plan.worktreeDirty) {
        summaryBits << QStringLiteral("将 apply 未提交改动");
    }
    if (summaryBits.isEmpty()) {
        summaryBits << QStringLiteral("无可迁回内容");
    }
    plan.summary = summaryBits.join(QStringLiteral("；"));
    return plan;
}
