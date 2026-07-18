#include "gitstagereviewdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
enum Col {
    ColCheck = 0,
    ColPath,
    ColStatus,
    ColRisk,
    ColReason,
    ColCount
};
} // namespace

GitStageReviewDialog::GitStageReviewDialog(const QString &repoDir,
                                           const QVector<GitStageEntry> &entries,
                                           QWidget *parent)
    : QDialog(parent)
    , m_repoDir(repoDir)
    , m_entries(entries)
{
    setWindowTitle(QStringLiteral("暂存审查"));
    resize(780, 480);

    auto *layout = new QVBoxLayout(this);

    m_lblSummary = new QLabel(this);
    m_lblSummary->setWordWrap(true);
    layout->addWidget(m_lblSummary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("暂存"), QStringLiteral("路径"), QStringLiteral("状态"),
         QStringLiteral("分类"), QStringLiteral("原因")});
    m_table->horizontalHeader()->setSectionResizeMode(ColPath, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColReason, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setRowCount(m_entries.size());

    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        auto *checkItem = new QTableWidgetItem;
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (e.risk == GitStageRisk::Blocked) {
            checkItem->setFlags(Qt::ItemIsSelectable);
            checkItem->setCheckState(Qt::Unchecked);
        } else if (e.risk == GitStageRisk::Suspicious) {
            checkItem->setCheckState(Qt::Unchecked);
        } else {
            checkItem->setCheckState(Qt::Checked);
        }
        m_table->setItem(row, ColCheck, checkItem);
        m_table->setItem(row, ColPath, new QTableWidgetItem(e.path));
        m_table->setItem(row, ColStatus, new QTableWidgetItem(e.xyStatus));
        m_table->setItem(row, ColRisk, new QTableWidgetItem(GitStageGuard::riskLabel(e.risk)));
        m_table->setItem(row, ColReason, new QTableWidgetItem(e.reason));
    }

    layout->addWidget(m_table, 1);

    auto *quickRow = new QHBoxLayout;
    auto *btnNormal = new QPushButton(QStringLiteral("仅勾选正常"), this);
    auto *btnSafe = new QPushButton(QStringLiteral("勾选正常+可疑"), this);
    auto *btnNone = new QPushButton(QStringLiteral("全部不勾选"), this);
    quickRow->addWidget(btnNormal);
    quickRow->addWidget(btnSafe);
    quickRow->addWidget(btnNone);
    quickRow->addStretch();
    layout->addLayout(quickRow);

    m_lblAutoIgnore = new QLabel(
        QStringLiteral("未勾选：确认后写入 .gitignore（有扩展名 → *.ext；无扩展名 → 相对路径+文件名）。"
                       "无扩展名文件（除 Makefile/LICENSE 等）一律视为危险。"
                       "确认后会自动对「已跟踪且应忽略」的路径执行 git rm --cached（不删文件）。"),
        this);
    m_lblAutoIgnore->setWordWrap(true);
    layout->addWidget(m_lblAutoIgnore);

    m_chkUncacheBlocked = new QCheckBox(
        QStringLiteral("从索引取消跟踪：已跟踪且匹配忽略规则的路径 (git rm --cached，不删文件)"), this);
    m_chkUncacheBlocked->setChecked(true);
    m_chkUncacheBlocked->setToolTip(
        QStringLiteral("默认开启。对当前已在 Git 索引中、且匹配 .gitignore / 无扩展名规则的文件取消跟踪。"));
    layout->addWidget(m_chkUncacheBlocked);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *btnCancel = new QPushButton(QStringLiteral("取消"), this);
    auto *btnOk = new QPushButton(QStringLiteral("确认暂存所选"), this);
    btnOk->setDefault(true);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnOk);
    layout->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &GitStageReviewDialog::onAcceptClicked);
    connect(btnNormal, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectNormalOnly);
    connect(btnSafe, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectAllSafe);
    connect(btnNone, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectNone);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        rebuildSummary();
    });

    rebuildSummary();
}

void GitStageReviewDialog::rebuildSummary()
{
    int checked = 0;
    int blocked = 0;
    int suspicious = 0;
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).risk == GitStageRisk::Blocked)
            ++blocked;
        else if (m_entries.at(row).risk == GitStageRisk::Suspicious)
            ++suspicious;
        const QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (item && item->checkState() == Qt::Checked)
            ++checked;
    }
    const QStringList ignorePatterns =
        GitStageGuard::suggestIgnorePatterns(pathsForIgnoreSuggestion(), true);
    m_lblSummary->setText(
        QStringLiteral("共 %1 项待处理：危险 %2（.gitignore / 无扩展名，不可勾选），可疑 %3（默认不勾），已勾选 %4。\n"
                       "将写入忽略规则：%5")
            .arg(m_entries.size())
            .arg(blocked)
            .arg(suspicious)
            .arg(checked)
            .arg(ignorePatterns.isEmpty() ? QStringLiteral("（无）")
                                          : ignorePatterns.join(QLatin1String(", "))));
}

QStringList GitStageReviewDialog::pathsForIgnoreSuggestion() const
{
    QStringList paths;
    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        // Sync extension-less blocked paths into .gitignore (directory rules).
        if (e.risk == GitStageRisk::Blocked) {
            if (GitStageGuard::isExtensionLessDangerous(e.path, nullptr))
                paths << e.path;
            continue;
        }
        const QTableWidgetItem *item = m_table->item(row, ColCheck);
        const bool checked = item && item->checkState() == Qt::Checked;
        if (!checked)
            paths << e.path;
    }
    return paths;
}

void GitStageReviewDialog::onSelectNormalOnly()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item || m_entries.at(row).risk == GitStageRisk::Blocked)
            continue;
        item->setCheckState(m_entries.at(row).risk == GitStageRisk::Normal ? Qt::Checked
                                                                           : Qt::Unchecked);
    }
    m_table->blockSignals(false);
    rebuildSummary();
}

void GitStageReviewDialog::onSelectAllSafe()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item || m_entries.at(row).risk == GitStageRisk::Blocked)
            continue;
        item->setCheckState(Qt::Checked);
    }
    m_table->blockSignals(false);
    rebuildSummary();
}

void GitStageReviewDialog::onSelectNone()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item || m_entries.at(row).risk == GitStageRisk::Blocked)
            continue;
        item->setCheckState(Qt::Unchecked);
    }
    m_table->blockSignals(false);
    rebuildSummary();
}

void GitStageReviewDialog::onAcceptClicked()
{
    m_selectedPaths.clear();
    m_blockedTrackedPaths.clear();

    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        const QTableWidgetItem *item = m_table->item(row, ColCheck);
        const bool checked = item && item->checkState() == Qt::Checked;

        if (e.risk == GitStageRisk::Blocked && !e.untracked)
            m_blockedTrackedPaths << e.path;

        if (!checked)
            continue;

        QString reason;
        if (e.risk == GitStageRisk::Blocked
            || GitStageGuard::isBlockedPath(m_repoDir, e.path, &reason)) {
            QMessageBox::warning(
                this, QStringLiteral("拒绝暂存"),
                QStringLiteral("所选路径匹配 .gitignore，已拒绝本次暂存：\n%1\n（%2）\n\n"
                               "请取消勾选，或从索引移除（git rm --cached）。")
                    .arg(e.path, reason.isEmpty() ? e.reason : reason));
            return;
        }
        m_selectedPaths << e.path;
    }

    // Always learn ignore rules from unchecked file types.
    m_ignorePatterns = GitStageGuard::suggestIgnorePatterns(pathsForIgnoreSuggestion(), true);
    m_appendIgnore = !m_ignorePatterns.isEmpty();
    m_uncacheBlocked = m_chkUncacheBlocked && m_chkUncacheBlocked->isChecked();

    if (m_selectedPaths.isEmpty() && !m_appendIgnore && !m_uncacheBlocked) {
        QMessageBox::information(this, QStringLiteral("暂存审查"),
                                 QStringLiteral("未勾选任何路径，也没有可写入的忽略规则 / 索引移除操作。"));
        return;
    }

    if (m_appendIgnore) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("写入 .gitignore"),
            QStringLiteral("以下未勾选文件类型将追加到 .gitignore，之后同类文件将视为危险：\n\n%1\n\n是否继续？")
                .arg(m_ignorePatterns.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes) {
            m_appendIgnore = false;
            m_ignorePatterns.clear();
            if (m_selectedPaths.isEmpty() && !m_uncacheBlocked)
                return;
        }
    }

    accept();
}
