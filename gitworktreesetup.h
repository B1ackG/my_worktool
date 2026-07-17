#ifndef GITWORKTREESETUP_H
#define GITWORKTREESETUP_H

#include "gitworktreeinfo.h"

#include <QString>
#include <QStringList>

class GitWorktreeSetup
{
public:
    /** Discover config under repo root: .cursor/worktrees.json then worktrees.json. */
    static QString discoverConfigPath(const QString &repoRoot);

    /** Load OS-specific setup plan. Empty commands if none. */
    static GitWorktreeSetupPlan loadPlan(const QString &repoRoot);

    /**
     * Run setup in worktreeDir with ROOT_WORKTREE_PATH=repoRoot.
     * Append stdout/stderr to logOut. Returns false on any failed step.
     */
    static bool runSetup(const GitWorktreeSetupPlan &plan, const QString &repoRoot,
                         const QString &worktreeDir, QString *logOut, int timeoutMs = 300000);
};

#endif // GITWORKTREESETUP_H
