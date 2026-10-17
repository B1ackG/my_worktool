#include "gitworktreerevdialog.h"

#include "gitworktreeinfo.h"
#include "gitworktreerunner.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int kColHash = 0;
constexpr int kColDate = 1;
constexpr int kColSubject = 2;
} // namespace

GitWorktreeQtOpenResult openWorktreeInQtCreator(QWidget *parent, const QString &projectDir)
{
    GitWorktreeQtOpenResult result;
    const QStringList pros = GitWorktreeRunner::topLevelProFiles(projectDir);
    if (pros.isEmpty()) {
        result.error = QStringLiteral("目录下没有 .pro 工程文件：\n%1").arg(projectDir);
        return result;
    }

    QString pro = pros.first();
    if (pros.size() > 1) {
        QStringList names;
        for (const QString &path : pros) {
            names << QFileInfo(path).fileName();
        }
        bool ok = false;
        const QString name = QInputDialog::getItem(
            parent, QStringLiteral("选择工程"),
            QStringLiteral("该目录有多个 .pro，请选择要打开的工程："), names, 0, false, &ok);
        if (!ok || name.isEmpty()) {
            result.cancelled = true;
            return result;
        }
        for (const QString &path : pros) {
            if (QFileInfo(path).fileName() == name) {
                pro = path;
                break;
            }
        }
    }

    QString program = GitWorktreeRunner::locateQtCreator();
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
#ifdef Q_OS_WIN
        const QString filter = QStringLiteral("Qt Creator (qtcreator.exe);;所有文件 (*.*)");
#else
        const QString filter = QStringLiteral("所有文件 (*)");
#endif
        program = QFileDialog::getOpenFileName(parent, QStringLiteral("选择 Qt Creator 程序"),
                                               QDir::homePath(), filter);
        if (program.isEmpty()) {
            result.cancelled = true;
            return result;
        }
    }

    const QFileInfo proInfo(pro);
    if (!QProcess::startDetached(program, {proInfo.absoluteFilePath()}, proInfo.absolutePath())) {
        result.error = QStringLiteral("无法启动 Qt Creator：\n%1").arg(program);
        return result;
    }
    GitWorktreeRunner::rememberQtCreator(program);
    result.opened = true;
    result.proFile = proInfo.absoluteFilePath();
    return result;
}

GitWorktreeRevDialog::GitWorktreeRevDialog(const QString &mainRepoDir, QWidget *parent)
    : QDialog(parent)
    , m_mainRepoDir(QDir(mainRepoDir).absolutePath())
{
    setWindowTitle(QStringLiteral("检出历史版本"));
    setMinimumSize(720, 520);
    resize(860, 560);

    m_lblHint = new QLabel(
        QStringLiteral("选择一次提交，检出到旁边的目录并用 Qt Creator 打开。当前仓库目录保持不动。"
                       "编译完成后，在 Worktree 管理里移除该目录即可回到现在的状态。"),
        this);
    m_lblHint->setWordWrap(true);

    m_txtFilter = new QLineEdit(this);
    m_txtFilter->setPlaceholderText(QStringLiteral("过滤哈希或说明"));
    m_txtFilter->setClearButtonEnabled(true);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("提交"), QStringLiteral("日期"), QStringLiteral("说明")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(kColSubject, QHeaderView::Stretch);
    m_table->setColumnWidth(kColHash, 90);
    m_table->setColumnWidth(kColDate, 110);

    m_txtRev = new QLineEdit(this);
    m_txtRev->setPlaceholderText(QStringLiteral("选择上方记录，或粘贴提交哈希"));
    m_txtPath = new QLineEdit(this);

    auto *lblRev = new QLabel(QStringLiteral("提交"), this);
    auto *lblPath = new QLabel(QStringLiteral("检出目录"), this);
    auto *lblPathNote = new QLabel(
        QStringLiteral("目录在仓库的 .worktrees/ 下，主目录不会切换。"), this);
    lblPathNote->setStyleSheet(QStringLiteral("color: #616161;"));

    m_btnCreateOpen = new QPushButton(QStringLiteral("创建并在 Qt Creator 中打开"), this);
    m_btnCreateOpen->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_btnCreateOpen->setDefault(true);
    m_btnCreate = new QPushButton(QStringLiteral("仅创建"), this);
    auto *btnCancel = new QPushButton(QStringLiteral("取消"), this);

    auto *revRow = new QHBoxLayout();
    revRow->addWidget(lblRev);
    revRow->addWidget(m_txtRev, 1);
    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(lblPath);
    pathRow->addWidget(m_txtPath, 1);
    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(m_btnCreateOpen);
    buttons->addWidget(m_btnCreate);
    buttons->addWidget(btnCancel);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_lblHint);
    lay->addWidget(m_txtFilter);
    lay->addWidget(m_table, 1);
    lay->addLayout(revRow);
    lay->addLayout(pathRow);
    lay->addWidget(lblPathNote);
    lay->addLayout(buttons);

    connect(m_txtFilter, &QLineEdit::textChanged, this, &GitWorktreeRevDialog::onFilterChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &GitWorktreeRevDialog::onSelectionChanged);
    connect(m_txtPath, &QLineEdit::textEdited, this, [this]() { m_pathEdited = true; });
    connect(m_btnCreateOpen, &QPushButton::clicked, this, &GitWorktreeRevDialog::onCreateAndOpen);
    connect(m_btnCreate, &QPushButton::clicked, this, &GitWorktreeRevDialog::onCreateOnly);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    loadCommits();
}

void GitWorktreeRevDialog::loadCommits()
{
    QString head;
    GitWorktreeRunner::runInRepo(m_mainRepoDir, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                                 &head);
    m_headHash = head.trimmed();

    const QVector<GitCommitRef> commits = GitWorktreeRunner::listRecentCommits(m_mainRepoDir, 120);
    m_table->setRowCount(0);
    if (commits.isEmpty()) {
        m_lblHint->setText(QStringLiteral("没有读到提交记录。请确认当前路径是有历史的 Git 仓库。"));
        setBusy(true);
        return;
    }

    m_table->setRowCount(commits.size());
    for (int i = 0; i < commits.size(); ++i) {
        const GitCommitRef &ref = commits.at(i);
        QString subject = ref.subject;
        if (!m_headHash.isEmpty() && ref.hash == m_headHash) {
            subject = QStringLiteral("（当前）") + subject;
        }
        auto *hashItem = new QTableWidgetItem(ref.shortHash);
        hashItem->setData(Qt::UserRole, ref.hash);
        hashItem->setData(Qt::UserRole + 1, ref.shortHash);
        hashItem->setToolTip(ref.hash);
        m_table->setItem(i, kColHash, hashItem);
        m_table->setItem(i, kColDate, new QTableWidgetItem(ref.date));
        m_table->setItem(i, kColSubject, new QTableWidgetItem(subject));
    }

    const int prefer = commits.size() > 1 ? 1 : 0;
    m_table->selectRow(prefer);
}

void GitWorktreeRevDialog::onFilterChanged(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool match = needle.isEmpty();
        if (!match) {
            for (int col = 0; col < m_table->columnCount(); ++col) {
                QTableWidgetItem *item = m_table->item(row, col);
                if (item && item->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
            QTableWidgetItem *hashItem = m_table->item(row, kColHash);
            if (!match && hashItem) {
                const QString full = hashItem->data(Qt::UserRole).toString();
                match = full.contains(needle, Qt::CaseInsensitive);
            }
        }
        m_table->setRowHidden(row, !match);
    }
}

void GitWorktreeRevDialog::onSelectionChanged()
{
    applySelectionToFields();
}

void GitWorktreeRevDialog::applySelectionToFields()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    QTableWidgetItem *hashItem = m_table->item(row, kColHash);
    if (!hashItem) {
        return;
    }
    const QString full = hashItem->data(Qt::UserRole).toString();
    const QString shortHash = hashItem->data(Qt::UserRole + 1).toString();
    m_txtRev->setText(full);
    if (!m_pathEdited) {
        m_txtPath->setText(GitWorktreeRunner::defaultWorktreePath(
            m_mainRepoDir, QStringLiteral("rev-%1").arg(shortHash)));
    }
}

void GitWorktreeRevDialog::setBusy(bool busy)
{
    m_btnCreateOpen->setEnabled(!busy);
    m_btnCreate->setEnabled(!busy);
}

void GitWorktreeRevDialog::onCreateOnly()
{
    createAtSelection(false);
}

void GitWorktreeRevDialog::onCreateAndOpen()
{
    createAtSelection(true);
}

bool GitWorktreeRevDialog::createAtSelection(bool openQt)
{
    QString full;
    QString shortHash;
    QString resolveError;
    if (!GitWorktreeRunner::resolveCommit(m_mainRepoDir, m_txtRev->text(), &full, &shortHash,
                                          &resolveError)) {
        QMessageBox::warning(this, QStringLiteral("提交"), resolveError);
        return false;
    }

    QString path = m_txtPath->text().trimmed();
    if (!m_pathEdited || path.isEmpty()) {
        path = GitWorktreeRunner::defaultWorktreePath(m_mainRepoDir,
                                                      QStringLiteral("rev-%1").arg(shortHash));
        m_txtPath->setText(path);
    }
    if (QFileInfo(path).exists()) {
        QMessageBox::warning(this, QStringLiteral("路径"),
                             QStringLiteral("目录已存在：\n%1\n请换一个路径，或先在 Worktree 管理里移除旧的工作树。")
                                 .arg(path));
        return false;
    }

    const QString parentDir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parentDir)) {
        QMessageBox::warning(this, QStringLiteral("路径"),
                             QStringLiteral("无法创建父目录：\n%1").arg(parentDir));
        return false;
    }

    const QStringList args = {QStringLiteral("worktree"), QStringLiteral("add"),
                              QStringLiteral("--detach"), path, full};
    m_log += QStringLiteral("$ git %1\n").arg(args.join(QLatin1Char(' ')));
    setBusy(true);

    QString so;
    QString se;
    const bool ok = GitWorktreeRunner::runInRepo(m_mainRepoDir, args, &so, &se);
    m_log += so;
    if (!se.trimmed().isEmpty()) {
        m_log += se;
        if (!m_log.endsWith(QLatin1Char('\n'))) {
            m_log += QLatin1Char('\n');
        }
    }
    if (!ok) {
        setBusy(false);
        m_log += QStringLiteral("worktree add 失败\n");
        const QString err = se.trimmed().isEmpty() ? so.trimmed() : se.trimmed();
        QMessageBox::critical(this, QStringLiteral("检出失败"),
                              err.isEmpty() ? QStringLiteral("git worktree add 失败。") : err);
        return false;
    }

    m_createdOk = true;
    m_createdPath = QDir(path).absolutePath();

    bool opened = false;
    if (openQt) {
        const GitWorktreeQtOpenResult qt = openWorktreeInQtCreator(this, m_createdPath);
        if (qt.opened) {
            opened = true;
            m_log += QStringLiteral("$ qtcreator %1\n").arg(qt.proFile);
        } else if (!qt.cancelled) {
            QMessageBox::warning(this, QStringLiteral("Qt Creator"),
                                 QStringLiteral("工作树已创建，但没能打开 Qt Creator。\n%1").arg(qt.error));
        }
    }

    QString body = QStringLiteral("已检出 %1，主目录没有切换。\n\n目录：\n%2\n\n").arg(shortHash, m_createdPath);
    if (opened) {
        body += QStringLiteral(
            "Qt 会按这个目录单独构建，不会覆盖当前工程已经生成的程序。"
            "请确认 Qt Creator 打开的是这一份，执行「重新构建」，把生成的程序拷到别处。\n\n");
    } else {
        body += QStringLiteral(
            "回到 Worktree 管理，选中这一行，点「用 Qt 打开」。"
            "在 Qt Creator 里执行「重新构建」，把生成的程序拷到别处。\n\n");
    }
    body += QStringLiteral(
        "编完后仍在 Worktree 管理中选中该目录，点「移除」。不要点「迁回主目录」。"
        "移除前可以先在 Qt Creator 里关闭该工程。");
    QMessageBox::information(this, QStringLiteral("已检出"), body);
    accept();
    return true;
}
