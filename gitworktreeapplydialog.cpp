#include "gitworktreeapplydialog.h"

#include "gitworktreerunner.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryFile>
#include <QVBoxLayout>

GitWorktreeApplyDialog::GitWorktreeApplyDialog(const QString &mainDir, const QString &worktreeDir,
                                               QWidget *parent)
    : QDialog(parent)
    , m_mainDir(QDir(mainDir).absolutePath())
    , m_worktreeDir(QDir(worktreeDir).absolutePath())
{
    setWindowTitle(QStringLiteral("迁回 Worktree 到主目录"));
    setMinimumSize(720, 520);
    resize(800, 560);

    m_lblSummary = new QLabel(this);
    m_lblSummary->setWordWrap(true);

    m_txtPreview = new QPlainTextEdit(this);
    m_txtPreview->setReadOnly(true);
    m_txtPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_txtPreview->font();
    mono.setFamily(QStringLiteral("Monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    m_txtPreview->setFont(mono);

    m_chkRemove = new QCheckBox(QStringLiteral("迁回成功后移除该 Worktree"), this);
    m_chkDeleteTemp = new QCheckBox(QStringLiteral("迁回成功后删除临时分支 (tmp/wt-*)"), this);
    m_chkDeleteTemp->setEnabled(false);

    m_btnApply = new QPushButton(QStringLiteral("确认迁回"), this);
    m_btnApply->setStyleSheet(QStringLiteral("font-weight: bold;"));
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"), this);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(m_btnApply);
    btnRow->addWidget(btnCancel);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_lblSummary);
    lay->addWidget(new QLabel(QStringLiteral("Diff 预览:"), this));
    lay->addWidget(m_txtPreview, 1);
    lay->addWidget(m_chkRemove);
    lay->addWidget(m_chkDeleteTemp);
    lay->addLayout(btnRow);

    connect(m_btnApply, &QPushButton::clicked, this, &GitWorktreeApplyDialog::onApplyClicked);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    rebuildPlan();
}

void GitWorktreeApplyDialog::rebuildPlan()
{
    m_plan = GitWorktreeRunner::buildApplyPlan(m_mainDir, m_worktreeDir);
    m_lblSummary->setText(
        QStringLiteral("主目录: %1\n主分支: %2\nWorktree: %3\nWorktree 分支: %4\n\n%5")
            .arg(m_plan.mainDir,
                 m_plan.mainBranch.isEmpty() ? QStringLiteral("(detached)") : m_plan.mainBranch,
                 m_plan.worktreeDir,
                 m_plan.isDetached ? QStringLiteral("(detached)") : m_plan.worktreeBranch,
                 m_plan.summary));
    m_txtPreview->setPlainText(m_plan.previewDiff);

    const bool isTemp = !m_plan.isDetached
                        && m_plan.worktreeBranch.startsWith(QStringLiteral("tmp/wt-"));
    m_chkDeleteTemp->setEnabled(isTemp && m_plan.aheadCommitCount > 0);
    if (!m_chkDeleteTemp->isEnabled()) {
        m_chkDeleteTemp->setChecked(false);
    }

    const bool canApply = !m_plan.mainDirty
                          && (m_plan.aheadCommitCount > 0 || m_plan.worktreeDirty)
                          && !m_plan.mainBranch.isEmpty();
    m_btnApply->setEnabled(canApply);
    if (m_plan.mainDirty) {
        m_lblSummary->setStyleSheet(QStringLiteral("color: #c62828;"));
    } else if (!canApply) {
        m_lblSummary->setStyleSheet(QStringLiteral("color: #666;"));
    } else {
        m_lblSummary->setStyleSheet(QString());
    }
}

void GitWorktreeApplyDialog::onApplyClicked()
{
    rebuildPlan();
    if (m_plan.mainDirty) {
        QMessageBox::warning(this, QStringLiteral("无法迁回"),
                             QStringLiteral("主目录有未提交改动，请先提交或 stash 后再迁回。"));
        return;
    }
    if (m_plan.mainBranch.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无法迁回"),
                             QStringLiteral("主目录处于 detached HEAD，请先检出一个分支。"));
        return;
    }
    if (m_plan.aheadCommitCount <= 0 && !m_plan.worktreeDirty) {
        QMessageBox::information(this, QStringLiteral("迁回"), QStringLiteral("没有可迁回的内容。"));
        return;
    }

    const QString confirm =
        QStringLiteral("确定将 Worktree 改动迁回主目录？\n\n%1").arg(m_plan.summary);
    if (QMessageBox::question(this, QStringLiteral("确认迁回"), confirm, QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    m_log.clear();

    if (m_plan.aheadCommitCount > 0) {
        if (m_plan.isDetached || m_plan.worktreeBranch.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("无法 merge"),
                                 QStringLiteral("Worktree 处于 detached 状态，无法 merge 提交；"
                                                "请仅对未提交改动使用 patch，或先在 worktree 检出分支。"));
            return;
        }
        m_log += QStringLiteral("$ git merge %1\n").arg(m_plan.worktreeBranch);
        QString so, se;
        if (!GitWorktreeRunner::runInRepo(m_plan.mainDir,
                                          {QStringLiteral("merge"), m_plan.worktreeBranch}, &so, &se)) {
            m_log += so + se;
            QMessageBox::critical(this, QStringLiteral("Merge 失败"),
                                  QStringLiteral("merge 失败（可能有冲突）。请到主目录手动解决。\n\n%1")
                                      .arg(se.isEmpty() ? so : se));
            return;
        }
        m_log += so + se;
    }

    if (m_plan.worktreeDirty) {
        QTemporaryFile patchFile;
        patchFile.setAutoRemove(true);
        if (!patchFile.open()) {
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("无法创建临时 patch 文件。"));
            return;
        }
        QString diffOut;
        GitWorktreeRunner::runInRepo(m_plan.worktreeDir, {QStringLiteral("diff")}, &diffOut);
        QString staged;
        GitWorktreeRunner::runInRepo(m_plan.worktreeDir,
                                     {QStringLiteral("diff"), QStringLiteral("--cached")}, &staged);
        QByteArray patchData = diffOut.toUtf8();
        if (!staged.isEmpty()) {
            patchData += "\n";
            patchData += staged.toUtf8();
        }
        if (patchData.trimmed().isEmpty()) {
            // dirty might be untracked only
            QMessageBox::warning(
                this, QStringLiteral("无法 apply 全部改动"),
                QStringLiteral("Worktree 仍有未跟踪文件等无法通过 git apply 迁回的内容。"
                               "已提交部分已 merge（若有）。请手动复制剩余文件。"));
        } else {
            patchFile.write(patchData);
            patchFile.flush();
            const QString patchPath = patchFile.fileName();
            m_log += QStringLiteral("$ git apply --check\n");
            QString so, se;
            if (!GitWorktreeRunner::runInRepo(
                    m_plan.mainDir,
                    {QStringLiteral("apply"), QStringLiteral("--check"), patchPath}, &so, &se)) {
                m_log += so + se;
                QMessageBox::critical(
                    this, QStringLiteral("Apply 检查失败"),
                    QStringLiteral("patch 无法干净应用到主目录。提交部分已 merge（若有）。\n"
                                   "请手动三路合入或复制文件。\n\n%1")
                        .arg(se.isEmpty() ? so : se));
                return;
            }
            m_log += QStringLiteral("$ git apply\n");
            so.clear();
            se.clear();
            if (!GitWorktreeRunner::runInRepo(m_plan.mainDir,
                                              {QStringLiteral("apply"), patchPath}, &so, &se)) {
                m_log += so + se;
                QMessageBox::critical(this, QStringLiteral("Apply 失败"),
                                      QStringLiteral("git apply 失败。\n\n%1").arg(se.isEmpty() ? so : se));
                return;
            }
            m_log += so + se;
        }
    }

    if (m_chkDeleteTemp->isEnabled() && m_chkDeleteTemp->isChecked()
        && m_plan.worktreeBranch.startsWith(QStringLiteral("tmp/wt-"))) {
        // only after successful apply; branch delete happens after worktree remove preferably
        m_deleteTempBranch = true;
    }

    m_removeAfter = m_chkRemove->isChecked();
    m_applied = true;
    QMessageBox::information(this, QStringLiteral("迁回完成"),
                             QStringLiteral("改动已迁回主目录。"));
    accept();
}
