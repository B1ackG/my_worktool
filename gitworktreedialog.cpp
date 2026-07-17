#include "gitworktreedialog.h"

#include "gitworktreeapplydialog.h"
#include "gitworktreerunner.h"
#include "gitworktreewizard.h"
#include "mainwindow.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColPath = 0;
constexpr int kColBranch = 1;
constexpr int kColMain = 2;
constexpr int kColDirty = 3;
constexpr int kColActivity = 4;
constexpr int kColAhead = 5;
} // namespace

GitWorktreeDialog::GitWorktreeDialog(MainWindow *mainWindow, const QString &mainRepoDir,
                                     QWidget *parent)
    : QDialog(parent)
    , m_mainWindow(mainWindow)
    , m_mainRepoDir(QDir(mainRepoDir).absolutePath())
{
    setWindowTitle(QStringLiteral("Git Worktree 管理"));
    setMinimumSize(900, 480);
    resize(980, 520);

    m_lblHint = new QLabel(this);
    m_lblHint->setWordWrap(true);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("路径"), QStringLiteral("分支"), QStringLiteral("主仓"),
         QStringLiteral("脏"), QStringLiteral("最近活动"), QStringLiteral("超前提交")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    QPushButton *btnRefresh = new QPushButton(QStringLiteral("刷新"), this);
    QPushButton *btnCreate = new QPushButton(QStringLiteral("创建向导…"), this);
    btnCreate->setStyleSheet(QStringLiteral("font-weight: bold;"));
    QPushButton *btnCleanup = new QPushButton(QStringLiteral("清理建议…"), this);
    QPushButton *btnPrune = new QPushButton(QStringLiteral("Prune"), this);
    QPushButton *btnOpen = new QPushButton(QStringLiteral("打开目录"), this);
    QPushButton *btnEnter = new QPushButton(QStringLiteral("进入"), this);
    btnEnter->setStyleSheet(QStringLiteral("background-color: #e3f2fd; font-weight: bold;"));
    QPushButton *btnApply = new QPushButton(QStringLiteral("迁回主目录"), this);
    btnApply->setStyleSheet(QStringLiteral("background-color: #e8f5e9; font-weight: bold;"));
    QPushButton *btnRemove = new QPushButton(QStringLiteral("移除"), this);
    btnRemove->setStyleSheet(QStringLiteral("background-color: #ffebee;"));
    QPushButton *btnClose = new QPushButton(QStringLiteral("关闭"), this);

    QHBoxLayout *tools = new QHBoxLayout();
    tools->addWidget(btnRefresh);
    tools->addWidget(btnCreate);
    tools->addWidget(btnCleanup);
    tools->addWidget(btnPrune);
    tools->addStretch();
    tools->addWidget(btnOpen);
    tools->addWidget(btnEnter);
    tools->addWidget(btnApply);
    tools->addWidget(btnRemove);
    tools->addWidget(btnClose);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_lblHint);
    lay->addWidget(m_table, 1);
    lay->addLayout(tools);

    connect(btnRefresh, &QPushButton::clicked, this, &GitWorktreeDialog::onRefresh);
    connect(btnCreate, &QPushButton::clicked, this, &GitWorktreeDialog::onCreateWizard);
    connect(btnCleanup, &QPushButton::clicked, this, &GitWorktreeDialog::onCleanupSuggestions);
    connect(btnPrune, &QPushButton::clicked, this, &GitWorktreeDialog::onPrune);
    connect(btnOpen, &QPushButton::clicked, this, &GitWorktreeDialog::onOpenDir);
    connect(btnEnter, &QPushButton::clicked, this, &GitWorktreeDialog::onEnter);
    connect(btnApply, &QPushButton::clicked, this, &GitWorktreeDialog::onApply);
    connect(btnRemove, &QPushButton::clicked, this, &GitWorktreeDialog::onRemove);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onEnter(); });

    onRefresh();
}

void GitWorktreeDialog::appendLog(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    if (!m_log.isEmpty() && !m_log.endsWith(QLatin1Char('\n'))) {
        m_log += QLatin1Char('\n');
    }
    m_log += text;
    if (m_mainWindow) {
        m_mainWindow->appendGitLogHtml(
            QStringLiteral("<font color='gray'>%1</font>")
                .arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"))));
    }
}

void GitWorktreeDialog::markOverdue(QVector<GitWorktreeEntry> &entries) const
{
    QSettings settings(QStringLiteral("LiChenYang"), QStringLiteral("LinuxHelper"));
    const int maxCount = settings.value(QStringLiteral("GitWorktreeMaxCount"), 10).toInt();
    const int maxAgeDays = settings.value(QStringLiteral("GitWorktreeMaxAgeDays"), 30).toInt();

    int auxCount = 0;
    for (const GitWorktreeEntry &e : entries) {
        if (!e.isMain) {
            ++auxCount;
        }
    }
    const bool overCount = auxCount > maxCount;
    const QDateTime now = QDateTime::currentDateTime();

    for (GitWorktreeEntry &e : entries) {
        e.overdue = false;
        if (e.isMain) {
            continue;
        }
        if (overCount) {
            e.overdue = true;
        }
        if (e.lastActivity.isValid() && maxAgeDays > 0
            && e.lastActivity.daysTo(now) > maxAgeDays) {
            e.overdue = true;
        }
    }
}

void GitWorktreeDialog::rebuildTable()
{
    m_table->setRowCount(0);
    const QString mainBranch = GitWorktreeRunner::checkedOutBranch(m_mainRepoDir);
    m_entries = GitWorktreeRunner::listWorktrees(m_mainRepoDir, mainBranch);
    markOverdue(m_entries);

    int overdueCount = 0;
    for (const GitWorktreeEntry &e : m_entries) {
        if (e.overdue) {
            ++overdueCount;
        }
    }
    m_lblHint->setText(
        QStringLiteral("主仓库: %1 | 共 %2 个 worktree（含主仓）| 建议清理: %3")
            .arg(m_mainRepoDir)
            .arg(m_entries.size())
            .arg(overdueCount));

    for (int i = 0; i < m_entries.size(); ++i) {
        const GitWorktreeEntry &e = m_entries.at(i);
        m_table->insertRow(i);
        auto *pathItem = new QTableWidgetItem(e.path);
        auto *branchItem = new QTableWidgetItem(
            e.isDetached ? QStringLiteral("(detached)")
                         : (e.branch.isEmpty() ? QStringLiteral("-") : e.branch));
        auto *mainItem = new QTableWidgetItem(e.isMain ? QStringLiteral("是") : QStringLiteral("否"));
        auto *dirtyItem = new QTableWidgetItem(e.isDirty ? QStringLiteral("是") : QStringLiteral("否"));
        auto *actItem = new QTableWidgetItem(
            e.lastActivity.isValid() ? e.lastActivity.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                     : QStringLiteral("-"));
        auto *aheadItem = new QTableWidgetItem(
            e.isMain ? QStringLiteral("-") : QString::number(e.commitCountAhead));

        if (e.overdue) {
            const QBrush brush(QColor(255, 235, 238));
            pathItem->setBackground(brush);
            branchItem->setBackground(brush);
            mainItem->setBackground(brush);
            dirtyItem->setBackground(brush);
            actItem->setBackground(brush);
            aheadItem->setBackground(brush);
        }
        if (e.isMain) {
            const QFont bold = [&]() {
                QFont f = pathItem->font();
                f.setBold(true);
                return f;
            }();
            pathItem->setFont(bold);
            branchItem->setFont(bold);
        }

        m_table->setItem(i, kColPath, pathItem);
        m_table->setItem(i, kColBranch, branchItem);
        m_table->setItem(i, kColMain, mainItem);
        m_table->setItem(i, kColDirty, dirtyItem);
        m_table->setItem(i, kColActivity, actItem);
        m_table->setItem(i, kColAhead, aheadItem);
    }
}

void GitWorktreeDialog::onRefresh()
{
    rebuildTable();
}

int GitWorktreeDialog::selectedRow() const
{
    const auto ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) {
        return m_table->currentRow();
    }
    return ranges.first().topRow();
}

GitWorktreeEntry GitWorktreeDialog::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return GitWorktreeEntry();
    }
    return m_entries.at(row);
}

void GitWorktreeDialog::onCreateWizard()
{
    GitWorktreeWizard wizard(m_mainRepoDir, this);
    if (wizard.exec() != QDialog::Accepted && !wizard.createdOk()) {
        if (!wizard.logText().isEmpty()) {
            appendLog(wizard.logText());
        }
        return;
    }
    appendLog(wizard.logText());
    if (wizard.createdOk() && !wizard.createdPath().isEmpty() && m_mainWindow) {
        // Always remember path; enter only if chosen
        if (wizard.finishAction() == GitWorktreeWizard::FinishAction::EnterWorktree) {
            m_mainWindow->enterGitRepoPath(wizard.createdPath());
            m_keepOpen = false;
            accept();
            return;
        }
        m_mainWindow->rememberGitRepoPath(wizard.createdPath());
    }
    rebuildTable();
}

void GitWorktreeDialog::onCleanupSuggestions()
{
    QStringList candidates;
    for (const GitWorktreeEntry &e : m_entries) {
        if (!e.isMain && e.overdue) {
            candidates << e.path;
        }
    }
    if (candidates.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("清理建议"),
                                 QStringLiteral("当前没有超龄或超量的辅助 worktree。"));
        return;
    }
    const QString msg =
        QStringLiteral("以下 worktree 建议清理（超龄或超过数量上限）：\n\n%1\n\n确认逐个移除？")
            .arg(candidates.join(QLatin1Char('\n')));
    if (QMessageBox::question(this, QStringLiteral("清理建议"), msg, QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    for (const QString &path : candidates) {
        QString so, se;
        appendLog(QStringLiteral("$ git worktree remove %1").arg(path));
        if (!GitWorktreeRunner::runInRepo(
                m_mainRepoDir, {QStringLiteral("worktree"), QStringLiteral("remove"), path}, &so,
                &se)) {
            appendLog(so + se);
            QMessageBox::warning(this, QStringLiteral("移除失败"),
                                 QStringLiteral("无法移除:\n%1\n\n%2").arg(path, se.isEmpty() ? so : se));
            continue;
        }
        appendLog(so + se);
        removeFromGitHistory(path);
    }
    QString so, se;
    GitWorktreeRunner::runInRepo(m_mainRepoDir,
                                 {QStringLiteral("worktree"), QStringLiteral("prune")}, &so, &se);
    appendLog(QStringLiteral("$ git worktree prune\n") + so + se);
    rebuildTable();
}

void GitWorktreeDialog::onPrune()
{
    QString so, se;
    appendLog(QStringLiteral("$ git worktree prune"));
    GitWorktreeRunner::runInRepo(m_mainRepoDir,
                                 {QStringLiteral("worktree"), QStringLiteral("prune")}, &so, &se);
    appendLog(so + se);
    rebuildTable();
    QMessageBox::information(this, QStringLiteral("Prune"), QStringLiteral("已执行 git worktree prune。"));
}

void GitWorktreeDialog::onOpenDir()
{
    const int row = selectedRow();
    const GitWorktreeEntry e = entryAt(row);
    if (e.path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("打开目录"), QStringLiteral("请先选择一行。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(e.path));
}

void GitWorktreeDialog::onEnter()
{
    const int row = selectedRow();
    const GitWorktreeEntry e = entryAt(row);
    if (e.path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("进入"), QStringLiteral("请先选择一行。"));
        return;
    }
    if (m_mainWindow) {
        m_mainWindow->enterGitRepoPath(e.path);
    }
    m_keepOpen = false;
    accept();
}

void GitWorktreeDialog::onApply()
{
    const int row = selectedRow();
    const GitWorktreeEntry e = entryAt(row);
    if (e.path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("迁回"), QStringLiteral("请先选择一行。"));
        return;
    }
    if (e.isMain) {
        QMessageBox::information(this, QStringLiteral("迁回"),
                                 QStringLiteral("主仓行无需迁回。请选择辅助 worktree。"));
        return;
    }

    // Prefer the actual main worktree path from the list
    QString mainDir = m_mainRepoDir;
    for (const GitWorktreeEntry &x : m_entries) {
        if (x.isMain) {
            mainDir = x.path;
            break;
        }
    }

    GitWorktreeApplyDialog dlg(mainDir, e.path, this);
    if (dlg.exec() != QDialog::Accepted || !dlg.applied()) {
        if (!dlg.logText().isEmpty()) {
            appendLog(dlg.logText());
        }
        return;
    }
    appendLog(dlg.logText());

    if (dlg.removeAfterApply()) {
        QString so, se;
        appendLog(QStringLiteral("$ git worktree remove %1").arg(e.path));
        if (GitWorktreeRunner::runInRepo(
                mainDir, {QStringLiteral("worktree"), QStringLiteral("remove"), e.path}, &so, &se)) {
            appendLog(so + se);
            removeFromGitHistory(e.path);
        } else {
            appendLog(so + se);
            QMessageBox::warning(this, QStringLiteral("移除 Worktree"),
                                 QStringLiteral("迁回成功，但移除 worktree 失败：\n%1")
                                     .arg(se.isEmpty() ? so : se));
        }
    }

    if (dlg.deleteTempBranch() && !e.branch.isEmpty()
        && e.branch.startsWith(QStringLiteral("tmp/wt-"))) {
        QString so, se;
        appendLog(QStringLiteral("$ git branch -d %1").arg(e.branch));
        if (!GitWorktreeRunner::runInRepo(mainDir,
                                          {QStringLiteral("branch"), QStringLiteral("-d"), e.branch},
                                          &so, &se)) {
            appendLog(so + se);
        } else {
            appendLog(so + se);
        }
    }

    if (m_mainWindow) {
        m_mainWindow->refreshAfterWorktreeApply();
    }
    rebuildTable();
}

void GitWorktreeDialog::removeFromGitHistory(const QString &path)
{
    if (m_mainWindow) {
        m_mainWindow->forgetGitRepoPath(path);
    }
}

void GitWorktreeDialog::onRemove()
{
    const int row = selectedRow();
    const GitWorktreeEntry e = entryAt(row);
    if (e.path.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("移除"), QStringLiteral("请先选择一行。"));
        return;
    }
    if (e.isMain) {
        QMessageBox::warning(this, QStringLiteral("移除"), QStringLiteral("不能移除主工作树。"));
        return;
    }
    if (QMessageBox::question(
            this, QStringLiteral("确认移除"),
            QStringLiteral("确定要移除并删除目录吗？\n%1\n这将删除该目录下所有未提交的内容!")
                .arg(e.path),
            QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    QString so, se;
    appendLog(QStringLiteral("$ git worktree remove %1").arg(e.path));
    if (!GitWorktreeRunner::runInRepo(
            m_mainRepoDir, {QStringLiteral("worktree"), QStringLiteral("remove"), e.path}, &so, &se)) {
        appendLog(so + se);
        QMessageBox::critical(this, QStringLiteral("移除失败"),
                              QStringLiteral("%1").arg(se.isEmpty() ? so : se));
        return;
    }
    appendLog(so + se);
    removeFromGitHistory(e.path);
    rebuildTable();
}
