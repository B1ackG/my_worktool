#ifndef GITWORKTREERUNNER_H
#define GITWORKTREERUNNER_H

#include "gitworktreeinfo.h"

#include <QString>
#include <QStringList>
#include <QVector>

class GitWorktreeRunner
{
public:
    static QString gitBinary();
    static bool finishProcess(class QProcess &process, int timeoutMs = 30000);

    /** Run git with cwd = repoDir. Returns exitCode == 0. */
    static bool runInRepo(const QString &repoDir, const QStringList &args,
                          QString *stdoutOut = nullptr, QString *stderrOut = nullptr,
                          int timeoutMs = 30000);

    static QVector<GitWorktreeEntry> listWorktrees(const QString &mainRepoDir,
                                                   const QString &mainBranch = QString());

    static QString pathForBranch(const QString &repoDir, const QString &branchName);

    static QString checkedOutBranch(const QString &repoDir);
    static bool isDirty(const QString &repoDir);
    static QDateTime lastActivity(const QString &repoDir);
    static int countCommitsAhead(const QString &mainRepoDir, const QString &mainBranch,
                                 const QString &otherBranch);
    static QStringList listCommitsAhead(const QString &mainRepoDir, const QString &mainBranch,
                                        const QString &otherBranch);
    static QString diffBranches(const QString &mainRepoDir, const QString &mainBranch,
                                const QString &otherBranch, int maxLines = 4000);
    static QString diffWorktree(const QString &worktreeDir, int maxLines = 4000);

    static QStringList localBranches(const QString &repoDir);
    static QStringList branchesInUse(const QString &repoDir);

    static GitWorktreeApplyPlan buildApplyPlan(const QString &mainDir,
                                               const QString &worktreeDir);

    static QString normalizeLocalBranchRef(const QString &branchRef);
    static QString slugifyForPath(const QString &text);
    static QString defaultWorktreePath(const QString &mainRepoDir, const QString &branchOrSlug);
};

#endif // GITWORKTREERUNNER_H
