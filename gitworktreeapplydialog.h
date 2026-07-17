#ifndef GITWORKTREEAPPLYDIALOG_H
#define GITWORKTREEAPPLYDIALOG_H

#include "gitworktreeinfo.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class GitWorktreeApplyDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitWorktreeApplyDialog(const QString &mainDir, const QString &worktreeDir,
                                    QWidget *parent = nullptr);

    bool applied() const { return m_applied; }
    bool removeAfterApply() const { return m_removeAfter; }
    bool deleteTempBranch() const { return m_deleteTempBranch; }
    QString logText() const { return m_log; }

private slots:
    void onApplyClicked();

private:
    void rebuildPlan();

    QString m_mainDir;
    QString m_worktreeDir;
    GitWorktreeApplyPlan m_plan;
    QLabel *m_lblSummary = nullptr;
    QPlainTextEdit *m_txtPreview = nullptr;
    QCheckBox *m_chkRemove = nullptr;
    QCheckBox *m_chkDeleteTemp = nullptr;
    QPushButton *m_btnApply = nullptr;
    bool m_applied = false;
    bool m_removeAfter = false;
    bool m_deleteTempBranch = false;
    QString m_log;
};

#endif // GITWORKTREEAPPLYDIALOG_H
