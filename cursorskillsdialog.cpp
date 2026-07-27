#include "cursorskillsdialog.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kColSource = 0;
constexpr int kColName = 1;
constexpr int kColPath = 2;
constexpr int kColStatus = 3;
constexpr int kRoleSkillDir = Qt::UserRole;
constexpr int kRoleSkillFile = Qt::UserRole + 1;
} // namespace

QString CursorSkillsDialog::globalSkillsRoot()
{
    return QDir::homePath() + QStringLiteral("/.cursor/skills");
}

CursorSkillsDialog::CursorSkillsDialog(const QVector<CursorSkillRepoRef> &repos, QWidget *parent)
    : QDialog(parent)
    , m_repos(repos)
{
    setWindowTitle(QStringLiteral("Cursor Skills"));
    setMinimumSize(820, 420);
    resize(920, 480);

    m_lblHint = new QLabel(this);
    m_lblHint->setWordWrap(true);
    m_lblHint->setText(
        QStringLiteral("总 Skill：%1\n"
                       "各仓库：<仓库>/.cursor/skills/\n"
                       "双击或点「打开 Skill」打开 SKILL.md；「打开目录」打开 skill 文件夹。")
            .arg(globalSkillsRoot()));

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("来源"), QStringLiteral("Skill"), QStringLiteral("路径"),
         QStringLiteral("状态")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(kColSource, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColName, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kColPath, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    QPushButton *btnRefresh = new QPushButton(QStringLiteral("刷新"), this);
    QPushButton *btnOpenDir = new QPushButton(QStringLiteral("打开目录"), this);
    QPushButton *btnOpenFile = new QPushButton(QStringLiteral("打开 Skill"), this);
    btnOpenFile->setStyleSheet(QStringLiteral("font-weight: bold; background-color: #e3f2fd;"));
    QPushButton *btnOpenGlobal = new QPushButton(QStringLiteral("打开总 Skills 目录"), this);
    btnOpenGlobal->setToolTip(globalSkillsRoot());
    QPushButton *btnClose = new QPushButton(QStringLiteral("关闭"), this);

    QHBoxLayout *tools = new QHBoxLayout();
    tools->addWidget(btnRefresh);
    tools->addWidget(btnOpenGlobal);
    tools->addStretch();
    tools->addWidget(btnOpenDir);
    tools->addWidget(btnOpenFile);
    tools->addWidget(btnClose);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(m_lblHint);
    lay->addWidget(m_table, 1);
    lay->addLayout(tools);

    connect(btnRefresh, &QPushButton::clicked, this, &CursorSkillsDialog::onRefresh);
    connect(btnOpenDir, &QPushButton::clicked, this, &CursorSkillsDialog::onOpenDir);
    connect(btnOpenFile, &QPushButton::clicked, this, &CursorSkillsDialog::onOpenSkillFile);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { onOpenSelected(); });
    connect(btnOpenGlobal, &QPushButton::clicked, this, [this]() {
        const QString root = globalSkillsRoot();
        if (!QDir(root).exists()) {
            QMessageBox::warning(this, QStringLiteral("打开总 Skills"),
                                 QStringLiteral("目录不存在:\n%1").arg(root));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(root));
    });

    onRefresh();
}

void CursorSkillsDialog::appendSkillsFromRoot(const QString &sourceLabel,
                                             const QString &skillsRoot, QVector<SkillRow> *out)
{
    if (!out)
        return;

    QDir root(skillsRoot);
    if (!root.exists()) {
        SkillRow missing;
        missing.sourceLabel = sourceLabel;
        missing.skillName = QStringLiteral("(无)");
        missing.skillDir = skillsRoot;
        missing.exists = false;
        out->append(missing);
        return;
    }

    const QFileInfoList entries =
        root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    if (entries.isEmpty()) {
        SkillRow empty;
        empty.sourceLabel = sourceLabel;
        empty.skillName = QStringLiteral("(空目录)");
        empty.skillDir = skillsRoot;
        empty.exists = true;
        out->append(empty);
        return;
    }

    for (const QFileInfo &fi : entries) {
        SkillRow row;
        row.sourceLabel = sourceLabel;
        row.skillName = fi.fileName();
        row.skillDir = fi.absoluteFilePath();
        const QString skillMd = QDir(row.skillDir).filePath(QStringLiteral("SKILL.md"));
        if (QFileInfo::exists(skillMd))
            row.skillFile = skillMd;
        row.exists = true;
        out->append(row);
    }
}

QVector<CursorSkillsDialog::SkillRow> CursorSkillsDialog::collectSkills() const
{
    QVector<SkillRow> rows;
    appendSkillsFromRoot(QStringLiteral("总 Skill"), globalSkillsRoot(), &rows);

    for (const CursorSkillRepoRef &repo : m_repos) {
        const QString absRepo = QDir(repo.repoPath).absolutePath();
        if (absRepo.isEmpty())
            continue;
        const QString label =
            repo.displayName.isEmpty() ? QFileInfo(absRepo).fileName() : repo.displayName;
        const QString skillsRoot =
            QDir(absRepo).filePath(QStringLiteral(".cursor/skills"));
        appendSkillsFromRoot(label, skillsRoot, &rows);
    }
    return rows;
}

void CursorSkillsDialog::rebuildTable()
{
    const QVector<SkillRow> rows = collectSkills();
    m_table->setRowCount(0);
    m_table->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const SkillRow &r = rows.at(i);
        auto *srcItem = new QTableWidgetItem(r.sourceLabel);
        auto *nameItem = new QTableWidgetItem(r.skillName);
        auto *pathItem = new QTableWidgetItem(r.skillDir);
        QString status;
        if (!r.exists)
            status = QStringLiteral("目录不存在");
        else if (r.skillName.startsWith(QLatin1Char('(')))
            status = QStringLiteral("无 skill");
        else if (r.skillFile.isEmpty())
            status = QStringLiteral("缺 SKILL.md");
        else
            status = QStringLiteral("就绪");
        auto *statusItem = new QTableWidgetItem(status);

        srcItem->setData(kRoleSkillDir, r.skillDir);
        srcItem->setData(kRoleSkillFile, r.skillFile);
        nameItem->setData(kRoleSkillDir, r.skillDir);
        nameItem->setData(kRoleSkillFile, r.skillFile);
        pathItem->setData(kRoleSkillDir, r.skillDir);
        pathItem->setData(kRoleSkillFile, r.skillFile);
        statusItem->setData(kRoleSkillDir, r.skillDir);
        statusItem->setData(kRoleSkillFile, r.skillFile);

        m_table->setItem(i, kColSource, srcItem);
        m_table->setItem(i, kColName, nameItem);
        m_table->setItem(i, kColPath, pathItem);
        m_table->setItem(i, kColStatus, statusItem);
    }
}

int CursorSkillsDialog::selectedRow() const
{
    const auto ranges = m_table->selectedRanges();
    if (ranges.isEmpty())
        return -1;
    return ranges.first().topRow();
}

CursorSkillsDialog::SkillRow CursorSkillsDialog::rowAt(int row) const
{
    SkillRow out;
    if (row < 0 || row >= m_table->rowCount())
        return out;
    QTableWidgetItem *item = m_table->item(row, kColSource);
    if (!item)
        return out;
    out.sourceLabel = item->text();
    if (auto *nameItem = m_table->item(row, kColName))
        out.skillName = nameItem->text();
    out.skillDir = item->data(kRoleSkillDir).toString();
    out.skillFile = item->data(kRoleSkillFile).toString();
    out.exists = QDir(out.skillDir).exists() || QFileInfo::exists(out.skillDir);
    return out;
}

void CursorSkillsDialog::onRefresh()
{
    rebuildTable();
}

void CursorSkillsDialog::onOpenDir()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("打开目录"),
                                 QStringLiteral("请先选择一行。"));
        return;
    }
    const SkillRow r = rowAt(row);
    QString target = r.skillDir;
    if (!QDir(target).exists()) {
        // For missing skill dirs under a repo, open parent .cursor if present, else repo root.
        const QFileInfo fi(target);
        const QString parent = fi.absolutePath();
        if (QDir(parent).exists())
            target = parent;
        else {
            QMessageBox::warning(this, QStringLiteral("打开目录"),
                                 QStringLiteral("路径不存在:\n%1").arg(r.skillDir));
            return;
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void CursorSkillsDialog::onOpenSkillFile()
{
    onOpenSelected();
}

void CursorSkillsDialog::onOpenSelected()
{
    const int row = selectedRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("打开 Skill"),
                                 QStringLiteral("请先选择一行。"));
        return;
    }
    const SkillRow r = rowAt(row);
    if (!r.skillFile.isEmpty() && QFileInfo::exists(r.skillFile)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(r.skillFile));
        return;
    }
    if (QDir(r.skillDir).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(r.skillDir));
        return;
    }
    QMessageBox::warning(this, QStringLiteral("打开 Skill"),
                         QStringLiteral("未找到 SKILL.md，且目录不存在:\n%1").arg(r.skillDir));
}
