#include "gitworktreesetup.h"
#include "platformprefs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

namespace {

QString expandRootPath(QString cmd, const QString &rootPath)
{
    cmd.replace(QStringLiteral("$ROOT_WORKTREE_PATH"), rootPath);
    cmd.replace(QStringLiteral("%ROOT_WORKTREE_PATH%"), rootPath);
    return cmd;
}

} // namespace

QString GitWorktreeSetup::discoverConfigPath(const QString &repoRoot)
{
    const QDir root(repoRoot);
    const QString cursorPath = root.filePath(QStringLiteral(".cursor/worktrees.json"));
    if (QFileInfo::exists(cursorPath)) {
        return cursorPath;
    }
    const QString fallback = root.filePath(QStringLiteral("worktrees.json"));
    if (QFileInfo::exists(fallback)) {
        return fallback;
    }
    return QString();
}

GitWorktreeSetupPlan GitWorktreeSetup::loadPlan(const QString &repoRoot)
{
    GitWorktreeSetupPlan plan;
    plan.configPath = discoverConfigPath(repoRoot);
    if (plan.configPath.isEmpty()) {
        return plan;
    }

    QFile f(plan.configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return plan;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) {
        return plan;
    }
    const QJsonObject obj = doc.object();

    QJsonValue selected;
    if (PlatformPrefs::preferWindows()) {
        if (obj.contains(QStringLiteral("setup-worktree-windows"))) {
            selected = obj.value(QStringLiteral("setup-worktree-windows"));
        } else {
            selected = obj.value(QStringLiteral("setup-worktree"));
        }
    } else if (obj.contains(QStringLiteral("setup-worktree-unix"))) {
        selected = obj.value(QStringLiteral("setup-worktree-unix"));
    } else {
        selected = obj.value(QStringLiteral("setup-worktree"));
    }

    const QDir configDir = QFileInfo(plan.configPath).absoluteDir();

    if (selected.isString()) {
        const QString scriptRel = selected.toString().trimmed();
        if (!scriptRel.isEmpty()) {
            plan.isScript = true;
            plan.scriptPath = QDir::isAbsolutePath(scriptRel)
                                  ? scriptRel
                                  : configDir.filePath(scriptRel);
            plan.commands << plan.scriptPath;
        }
    } else if (selected.isArray()) {
        const QJsonArray arr = selected.toArray();
        for (const QJsonValue &v : arr) {
            if (v.isString()) {
                const QString cmd = v.toString().trimmed();
                if (!cmd.isEmpty()) {
                    plan.commands << cmd;
                }
            }
        }
    }
    return plan;
}

bool GitWorktreeSetup::runSetup(const GitWorktreeSetupPlan &plan, const QString &repoRoot,
                                const QString &worktreeDir, QString *logOut, int timeoutMs)
{
    auto appendLog = [&](const QString &line) {
        if (logOut) {
            if (!logOut->isEmpty() && !logOut->endsWith(QLatin1Char('\n'))) {
                *logOut += QLatin1Char('\n');
            }
            *logOut += line;
        }
    };

    if (plan.commands.isEmpty()) {
        appendLog(QStringLiteral("[setup] no commands configured"));
        return true;
    }

    const QString rootAbs = QDir(repoRoot).absolutePath();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ROOT_WORKTREE_PATH"), rootAbs);

    bool allOk = true;

    if (plan.isScript) {
        QProcess process;
        process.setWorkingDirectory(worktreeDir);
        process.setProcessEnvironment(env);
        if (PlatformPrefs::preferWindows()) {
            process.start(QStringLiteral("powershell"),
                          {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                           QStringLiteral("Bypass"), QStringLiteral("-File"), plan.scriptPath});
        } else {
            process.start(QStringLiteral("/bin/bash"), {plan.scriptPath});
        }
        appendLog(QStringLiteral("[setup] run script: %1").arg(plan.scriptPath));
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(3000);
            appendLog(QStringLiteral("[setup] script timed out"));
            return false;
        }
        appendLog(PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput()));
        appendLog(PlatformPrefs::decodeProcessOutput(process.readAllStandardError()));
        if (process.exitCode() != 0) {
            appendLog(QStringLiteral("[setup] script failed, exit %1").arg(process.exitCode()));
            return false;
        }
        appendLog(QStringLiteral("[setup] script ok"));
        return true;
    }

    for (const QString &rawCmd : plan.commands) {
        const QString cmd = expandRootPath(rawCmd, rootAbs);
        appendLog(QStringLiteral("[setup] $ %1").arg(cmd));

        QProcess process;
        process.setWorkingDirectory(worktreeDir);
        process.setProcessEnvironment(env);
        if (PlatformPrefs::preferWindows()) {
            process.start(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), cmd});
        } else {
            process.start(QStringLiteral("/bin/bash"), {QStringLiteral("-lc"), cmd});
        }
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(3000);
            appendLog(QStringLiteral("[setup] timed out"));
            allOk = false;
            break;
        }
        const QString so = PlatformPrefs::decodeProcessOutput(process.readAllStandardOutput()).trimmed();
        const QString se = PlatformPrefs::decodeProcessOutput(process.readAllStandardError()).trimmed();
        if (!so.isEmpty()) {
            appendLog(so);
        }
        if (!se.isEmpty()) {
            appendLog(se);
        }
        if (process.exitCode() != 0) {
            appendLog(QStringLiteral("[setup] failed, exit %1").arg(process.exitCode()));
            allOk = false;
            break;
        }
    }

    if (allOk) {
        appendLog(QStringLiteral("[setup] complete"));
    }
    return allOk;
}
