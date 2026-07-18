#ifndef GITSTAGEGUARD_H
#define GITSTAGEGUARD_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

enum class GitStageRisk {
    Normal = 0,
    Suspicious = 1,
    Blocked = 2
};

struct GitStageEntry {
    QString path;
    QString xyStatus;
    GitStageRisk risk = GitStageRisk::Normal;
    QString reason;
    bool untracked = false;
    qint64 sizeBytes = -1;
};

class GitStageGuard
{
public:
    static QVector<GitStageEntry> collectPending(const QString &repoDir, QString *error = nullptr);

    /**
     * True if path matches repo ignore rules (.gitignore / exclude), even when already tracked.
     * Uses: git check-ignore --no-index
     */
    static bool isBlockedPath(const QString &repoDir, const QString &path, QString *reason = nullptr);

    /** Batch variant; keys of the returned map are ignored paths, values are reason text. */
    static QHash<QString, QString> ignoredPathReasons(const QString &repoDir,
                                                      const QStringList &paths);

    static bool isSuspiciousUntracked(const QString &path, qint64 sizeBytes, QString *reason = nullptr);

    /** True if path has no extension and is not on the keep-list (Makefile/LICENSE/…). */
    static bool isExtensionLessDangerous(const QString &path, QString *reason = nullptr);

    static GitStageRisk classify(const QString &repoDir, const QString &path, bool untracked,
                                 qint64 sizeBytes, QString *reason = nullptr);

    /**
     * Suggest .gitignore patterns (e.g. *.csv) for the given paths.
     * When skipSourceLike is true, common source/project extensions are omitted.
     * Extension-less paths use exact relative path + basename (no dir/* + !dir/*.*).
     */
    static QStringList suggestIgnorePatterns(const QStringList &paths, bool skipSourceLike = true);

    /** True for extensions that should not be auto-ignored (e.g. cpp, h, pro). */
    static bool isSourceLikeExtension(const QString &extensionLower);

    /** Append missing patterns to repoDir/.gitignore. Returns true if file changed. */
    static bool appendIgnorePatterns(const QString &repoDir, const QStringList &patterns,
                                     QString *error = nullptr);

    /** Tracked files that match ignore rules (--no-index); candidates for git rm --cached. */
    static QStringList trackedIgnoredPaths(const QString &repoDir);

    static QString riskLabel(GitStageRisk risk);
};

#endif // GITSTAGEGUARD_H
