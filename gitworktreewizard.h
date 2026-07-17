#ifndef GITWORKTREEWIZARD_H
#define GITWORKTREEWIZARD_H

#include <QWizard>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;

class GitWorktreeWizard : public QWizard
{
    Q_OBJECT
public:
    enum PageId { PageMode = 0, PagePath = 1, PageSetup = 2, PageResult = 3 };

    enum class FinishAction { StayMain, EnterWorktree, OpenManager };

    explicit GitWorktreeWizard(const QString &mainRepoDir, QWidget *parent = nullptr);

    QString createdPath() const { return m_createdPath; }
    QString createdBranch() const { return m_createdBranch; }
    FinishAction finishAction() const { return m_finishAction; }
    QString logText() const { return m_log; }
    bool createdOk() const { return m_createdOk; }

protected:
    void initializePage(int id) override;
    bool validateCurrentPage() override;

private:
    void buildPages();
    bool createWorktree();
    QString computeBranchName() const;
    QString computePath() const;

    QString m_mainRepoDir;
    QString m_createdPath;
    QString m_createdBranch;
    QString m_log;
    bool m_createdOk = false;
    bool m_ranCreate = false;
    FinishAction m_finishAction = FinishAction::StayMain;

    // PageMode
    QRadioButton *m_rdTemp = nullptr;
    QRadioButton *m_rdFeature = nullptr;
    QRadioButton *m_rdExisting = nullptr;
    QCheckBox *m_chkDetach = nullptr;
    QLineEdit *m_txtFeatureBranch = nullptr;
    QComboBox *m_cmbExistingBranch = nullptr;

    // PagePath
    QLineEdit *m_txtPath = nullptr;
    QLabel *m_lblPathHint = nullptr;

    // PageSetup
    QLabel *m_lblSetupInfo = nullptr;
    QPlainTextEdit *m_txtSetupCmds = nullptr;
    QCheckBox *m_chkRunSetup = nullptr;

    // PageResult
    QLabel *m_lblResult = nullptr;
    QPlainTextEdit *m_txtResultLog = nullptr;
    QRadioButton *m_rdStay = nullptr;
    QRadioButton *m_rdEnter = nullptr;
    QRadioButton *m_rdOpenMgr = nullptr;
};

#endif // GITWORKTREEWIZARD_H
