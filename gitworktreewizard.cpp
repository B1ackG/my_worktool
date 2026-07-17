#include "gitworktreewizard.h"

#include "gitworktreerunner.h"
#include "gitworktreesetup.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

class ModePage : public QWizardPage
{
public:
    explicit ModePage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("选择创建模式"));
        setSubTitle(QStringLiteral("临时隔离适合短任务；功能分支/已有分支适合长期并行开发。"));
    }
};

class PathPage : public QWizardPage
{
public:
    explicit PathPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("工作树路径"));
        setSubTitle(QStringLiteral("默认为仓库下的 .worktrees/<分支名>/"));
    }
};

class SetupPage : public QWizardPage
{
public:
    explicit SetupPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("创建后 Setup"));
        setSubTitle(QStringLiteral("可选执行 .cursor/worktrees.json 中的安装/构建命令。"));
    }
};

class ResultPage : public QWizardPage
{
public:
    explicit ResultPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("完成"));
        setSubTitle(QStringLiteral("选择下一步操作。"));
        setFinalPage(true);
    }
};

} // namespace

GitWorktreeWizard::GitWorktreeWizard(const QString &mainRepoDir, QWidget *parent)
    : QWizard(parent)
    , m_mainRepoDir(QDir(mainRepoDir).absolutePath())
{
    setWindowTitle(QStringLiteral("创建 Git Worktree"));
    setMinimumSize(640, 480);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setWizardStyle(QWizard::ModernStyle);
    buildPages();
}

void GitWorktreeWizard::buildPages()
{
    // --- Mode ---
    ModePage *modePage = new ModePage(this);
    m_rdTemp = new QRadioButton(QStringLiteral("临时隔离（新建 tmp/wt-<时间戳> 分支）"), modePage);
    m_rdFeature = new QRadioButton(QStringLiteral("功能分支（指定新分支名）"), modePage);
    m_rdExisting = new QRadioButton(QStringLiteral("已有分支（未被其他 worktree 占用）"), modePage);
    m_rdTemp->setChecked(true);

    m_chkDetach = new QCheckBox(QStringLiteral("高级：使用 detached HEAD（不创建分支）"), modePage);
    m_chkDetach->setToolTip(QStringLiteral("仅临时隔离模式下可用"));

    m_txtFeatureBranch = new QLineEdit(modePage);
    m_txtFeatureBranch->setPlaceholderText(QStringLiteral("例如 feature/login-ui"));
    m_txtFeatureBranch->setEnabled(false);

    m_cmbExistingBranch = new QComboBox(modePage);
    m_cmbExistingBranch->setEnabled(false);

    QButtonGroup *grp = new QButtonGroup(modePage);
    grp->addButton(m_rdTemp, 0);
    grp->addButton(m_rdFeature, 1);
    grp->addButton(m_rdExisting, 2);

    auto syncModeUi = [this]() {
        const bool feature = m_rdFeature->isChecked();
        const bool existing = m_rdExisting->isChecked();
        m_txtFeatureBranch->setEnabled(feature);
        m_cmbExistingBranch->setEnabled(existing);
        m_chkDetach->setEnabled(m_rdTemp->isChecked());
        if (!m_rdTemp->isChecked()) {
            m_chkDetach->setChecked(false);
        }
    };
    connect(m_rdTemp, &QRadioButton::toggled, this, syncModeUi);
    connect(m_rdFeature, &QRadioButton::toggled, this, syncModeUi);
    connect(m_rdExisting, &QRadioButton::toggled, this, syncModeUi);

    QVBoxLayout *modeLay = new QVBoxLayout(modePage);
    modeLay->addWidget(m_rdTemp);
    modeLay->addWidget(m_chkDetach);
    modeLay->addWidget(m_rdFeature);
    modeLay->addWidget(m_txtFeatureBranch);
    modeLay->addWidget(m_rdExisting);
    modeLay->addWidget(m_cmbExistingBranch);
    modeLay->addStretch();
    setPage(PageMode, modePage);

    // --- Path ---
    PathPage *pathPage = new PathPage(this);
    m_lblPathHint = new QLabel(pathPage);
    m_lblPathHint->setWordWrap(true);
    m_txtPath = new QLineEdit(pathPage);
    QVBoxLayout *pathLay = new QVBoxLayout(pathPage);
    pathLay->addWidget(m_lblPathHint);
    pathLay->addWidget(new QLabel(QStringLiteral("本地路径:"), pathPage));
    pathLay->addWidget(m_txtPath);
    pathLay->addStretch();
    setPage(PagePath, pathPage);

    // --- Setup ---
    SetupPage *setupPage = new SetupPage(this);
    m_lblSetupInfo = new QLabel(setupPage);
    m_lblSetupInfo->setWordWrap(true);
    m_txtSetupCmds = new QPlainTextEdit(setupPage);
    m_txtSetupCmds->setReadOnly(true);
    m_chkRunSetup = new QCheckBox(QStringLiteral("创建后执行 setup"), setupPage);
    QVBoxLayout *setupLay = new QVBoxLayout(setupPage);
    setupLay->addWidget(m_lblSetupInfo);
    setupLay->addWidget(m_txtSetupCmds, 1);
    setupLay->addWidget(m_chkRunSetup);
    setPage(PageSetup, setupPage);

    // --- Result ---
    ResultPage *resultPage = new ResultPage(this);
    m_lblResult = new QLabel(resultPage);
    m_lblResult->setWordWrap(true);
    m_txtResultLog = new QPlainTextEdit(resultPage);
    m_txtResultLog->setReadOnly(true);
    m_rdStay = new QRadioButton(QStringLiteral("留在主目录"), resultPage);
    m_rdEnter = new QRadioButton(QStringLiteral("进入新 Worktree"), resultPage);
    m_rdOpenMgr = new QRadioButton(QStringLiteral("打开 Worktree 管理器"), resultPage);
    m_rdStay->setChecked(true);
    QVBoxLayout *resLay = new QVBoxLayout(resultPage);
    resLay->addWidget(m_lblResult);
    resLay->addWidget(m_txtResultLog, 1);
    resLay->addWidget(m_rdStay);
    resLay->addWidget(m_rdEnter);
    resLay->addWidget(m_rdOpenMgr);
    setPage(PageResult, resultPage);
}

void GitWorktreeWizard::initializePage(int id)
{
    if (id == PageMode) {
        m_cmbExistingBranch->clear();
        const QStringList used = GitWorktreeRunner::branchesInUse(m_mainRepoDir);
        const QStringList all = GitWorktreeRunner::localBranches(m_mainRepoDir);
        for (const QString &b : all) {
            if (!used.contains(b)) {
                m_cmbExistingBranch->addItem(b);
            }
        }
    } else if (id == PagePath) {
        const QString branch = computeBranchName();
        const QString slug = m_chkDetach->isChecked()
                                 ? QStringLiteral("detached-%1")
                                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                                 : (branch.isEmpty() ? QStringLiteral("worktree") : branch);
        m_txtPath->setText(GitWorktreeRunner::defaultWorktreePath(m_mainRepoDir, slug));
        m_lblPathHint->setText(
            QStringLiteral("主仓库: %1\n分支/模式: %2")
                .arg(m_mainRepoDir,
                     m_chkDetach->isChecked()
                         ? QStringLiteral("detached HEAD")
                         : (branch.isEmpty() ? QStringLiteral("(未指定)") : branch)));
    } else if (id == PageSetup) {
        const GitWorktreeSetupPlan plan = GitWorktreeSetup::loadPlan(m_mainRepoDir);
        if (plan.configPath.isEmpty() || plan.commands.isEmpty()) {
            m_lblSetupInfo->setText(QStringLiteral("未找到 setup 配置（.cursor/worktrees.json）。"));
            m_txtSetupCmds->setPlainText(QStringLiteral("(无)"));
            m_chkRunSetup->setChecked(false);
            m_chkRunSetup->setEnabled(false);
        } else {
            m_lblSetupInfo->setText(QStringLiteral("配置文件: %1").arg(plan.configPath));
            m_txtSetupCmds->setPlainText(plan.commands.join(QLatin1Char('\n')));
            m_chkRunSetup->setEnabled(true);
            m_chkRunSetup->setChecked(true);
        }
    } else if (id == PageResult) {
        if (!m_ranCreate) {
            m_ranCreate = true;
            createWorktree();
        }
        if (m_createdOk) {
            m_lblResult->setText(
                QStringLiteral("创建成功。\n路径: %1\n分支: %2")
                    .arg(m_createdPath,
                         m_createdBranch.isEmpty() ? QStringLiteral("(detached)") : m_createdBranch));
            m_lblResult->setStyleSheet(QStringLiteral("color: #2e7d32; font-weight: bold;"));
        } else {
            m_lblResult->setText(QStringLiteral("创建失败，请查看日志。"));
            m_lblResult->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
            m_rdEnter->setEnabled(false);
        }
        m_txtResultLog->setPlainText(m_log);
    }
}

bool GitWorktreeWizard::validateCurrentPage()
{
    const int id = currentId();
    if (id == PageMode) {
        if (m_rdFeature->isChecked()) {
            if (m_txtFeatureBranch->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("分支名"),
                                     QStringLiteral("请输入新功能分支名称。"));
                return false;
            }
        } else if (m_rdExisting->isChecked()) {
            if (m_cmbExistingBranch->currentText().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("分支"),
                                     QStringLiteral("没有可用的未占用分支，请换一种模式。"));
                return false;
            }
        }
        return true;
    }
    if (id == PagePath) {
        const QString path = m_txtPath->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("路径"), QStringLiteral("请输入工作树路径。"));
            return false;
        }
        if (QDir(path).exists()) {
            QMessageBox::warning(this, QStringLiteral("路径"),
                                 QStringLiteral("目标路径已存在，请换一个空目录路径。"));
            return false;
        }
        return true;
    }
    if (id == PageResult) {
        if (m_rdEnter->isChecked()) {
            m_finishAction = FinishAction::EnterWorktree;
        } else if (m_rdOpenMgr->isChecked()) {
            m_finishAction = FinishAction::OpenManager;
        } else {
            m_finishAction = FinishAction::StayMain;
        }
        return m_createdOk;
    }
    return true;
}

QString GitWorktreeWizard::computeBranchName() const
{
    if (m_chkDetach->isChecked()) {
        return QString();
    }
    if (m_rdTemp->isChecked()) {
        return QStringLiteral("tmp/wt-%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    }
    if (m_rdFeature->isChecked()) {
        return m_txtFeatureBranch->text().trimmed();
    }
    if (m_rdExisting->isChecked()) {
        return m_cmbExistingBranch->currentText().trimmed();
    }
    return QString();
}

QString GitWorktreeWizard::computePath() const
{
    return m_txtPath->text().trimmed();
}

bool GitWorktreeWizard::createWorktree()
{
    m_log.clear();
    m_createdOk = false;
    m_createdPath.clear();
    m_createdBranch.clear();

    const QString path = computePath();
    const QString parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent)) {
        m_log += QStringLiteral("无法创建父目录: %1\n").arg(parent);
        return false;
    }

    QStringList args;
    args << QStringLiteral("worktree") << QStringLiteral("add");

    QString so, se;
    bool ok = false;

    if (m_chkDetach->isChecked()) {
        args << QStringLiteral("--detach") << path << QStringLiteral("HEAD");
        m_log += QStringLiteral("$ git %1\n").arg(args.join(QLatin1Char(' ')));
        ok = GitWorktreeRunner::runInRepo(m_mainRepoDir, args, &so, &se);
        m_createdBranch.clear();
    } else if (m_rdExisting->isChecked()) {
        const QString branch = computeBranchName();
        args << path << branch;
        m_log += QStringLiteral("$ git %1\n").arg(args.join(QLatin1Char(' ')));
        ok = GitWorktreeRunner::runInRepo(m_mainRepoDir, args, &so, &se);
        m_createdBranch = branch;
    } else {
        // temp or feature: create new branch from HEAD
        const QString branch = computeBranchName();
        // Recompute temp name at create time for accuracy
        QString branchFinal = branch;
        if (m_rdTemp->isChecked()) {
            branchFinal = QStringLiteral("tmp/wt-%1")
                              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        }
        args << QStringLiteral("-b") << branchFinal << path << QStringLiteral("HEAD");
        m_log += QStringLiteral("$ git %1\n").arg(args.join(QLatin1Char(' ')));
        ok = GitWorktreeRunner::runInRepo(m_mainRepoDir, args, &so, &se);
        m_createdBranch = branchFinal;
    }

    m_log += so;
    if (!se.trimmed().isEmpty()) {
        m_log += se;
        if (!m_log.endsWith(QLatin1Char('\n'))) {
            m_log += QLatin1Char('\n');
        }
    }

    if (!ok) {
        m_log += QStringLiteral("worktree add 失败\n");
        return false;
    }

    m_createdPath = QDir(path).absolutePath();
    m_createdOk = true;

    if (m_chkRunSetup->isChecked()) {
        const GitWorktreeSetupPlan plan = GitWorktreeSetup::loadPlan(m_mainRepoDir);
        QString setupLog;
        const bool setupOk =
            GitWorktreeSetup::runSetup(plan, m_mainRepoDir, m_createdPath, &setupLog);
        m_log += setupLog;
        if (!m_log.endsWith(QLatin1Char('\n'))) {
            m_log += QLatin1Char('\n');
        }
        if (!setupOk) {
            m_log += QStringLiteral("警告: setup 失败（worktree 已创建）\n");
        }
    }

    return true;
}
