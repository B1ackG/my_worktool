#ifndef GITWORKTREEDIALOG_H
#define GITWORKTREEDIALOG_H

#include "gitworktreeinfo.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QTableWidget;
class QPushButton;
class MainWindow;

class GitWorktreeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitWorktreeDialog(MainWindow *mainWindow, const QString &mainRepoDir,
                               QWidget *parent = nullptr);

    /** Log lines produced during dialog actions (for MainWindow txtGitLog). */
    QString accumulatedLog() const { return m_log; }

    bool shouldStayOpen() const { return m_keepOpen; }

private slots:
    void onRefresh();
    void onCreateWizard();
    void onCleanupSuggestions();
    void onPrune();
    void onOpenDir();
    void onEnter();
    void onApply();
    void onRemove();

private:
    void rebuildTable();
    void markOverdue(QVector<GitWorktreeEntry> &entries) const;
    int selectedRow() const;
    GitWorktreeEntry entryAt(int row) const;
    void appendLog(const QString &text);
    void removeFromGitHistory(const QString &path);

    MainWindow *m_mainWindow = nullptr;
    QString m_mainRepoDir;
    QVector<GitWorktreeEntry> m_entries;
    QTableWidget *m_table = nullptr;
    QLabel *m_lblHint = nullptr;
    QString m_log;
    bool m_keepOpen = true;
};

#endif // GITWORKTREEDIALOG_H
