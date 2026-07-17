#ifndef GITWORKTREEINFO_H
#define GITWORKTREEINFO_H

#include <QDateTime>
#include <QString>

struct GitWorktreeEntry {
    QString path;
    QString branch;          // empty when detached
    bool isMain = false;
    bool isDirty = false;
    bool isDetached = false;
    QDateTime lastActivity;
    int commitCountAhead = 0; // commits on branch not in mainBranch (0 if N/A)
    bool overdue = false;     // age or count policy
};

struct GitWorktreeApplyPlan {
    QString mainDir;
    QString mainBranch;
    QString worktreeDir;
    QString worktreeBranch;
    bool isDetached = false;
    bool mainDirty = false;
    bool worktreeDirty = false;
    int aheadCommitCount = 0;
    QStringList aheadCommitLines;
    QString previewDiff;
    QString summary;
};

struct GitWorktreeSetupPlan {
    QString configPath;
    QStringList commands; // expanded command lines or script path as single entry
    bool isScript = false;
    QString scriptPath;
};

#endif // GITWORKTREEINFO_H
