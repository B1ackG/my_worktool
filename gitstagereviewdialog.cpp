#include "gitstagereviewdialog.h"

#include "deepseekclient.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
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

QString normalizePathKey(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1String("./")))
        path = path.mid(2);
    return path.trimmed();
}

QString stripMarkdownFences(QString text)
{
    text = text.trimmed();
    if (!text.startsWith(QStringLiteral("```")))
        return text;
    const int firstNl = text.indexOf(QLatin1Char('\n'));
    if (firstNl > 0)
        text = text.mid(firstNl + 1);
    if (text.endsWith(QStringLiteral("```")))
        text.chop(3);
    return text.trimmed();
}

QString extractJsonPayload(QString text)
{
    text = stripMarkdownFences(text);
    // Prefer object/array span if model added prose around JSON.
    const int obj = text.indexOf(QLatin1Char('{'));
    const int arr = text.indexOf(QLatin1Char('['));
    int start = -1;
    if (obj >= 0 && (arr < 0 || obj < arr))
        start = obj;
    else if (arr >= 0)
        start = arr;
    if (start < 0)
        return text;
    const int endObj = text.lastIndexOf(QLatin1Char('}'));
    const int endArr = text.lastIndexOf(QLatin1Char(']'));
    const int end = qMax(endObj, endArr);
    if (end > start)
        return text.mid(start, end - start + 1).trimmed();
    return text;
}

QString formatSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("?");
    if (bytes < 1024)
        return QStringLiteral("%1B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}
} // namespace

GitStageReviewDialog::GitStageReviewDialog(const QString &repoDir,
                                           const QVector<GitStageEntry> &entries,
                                           QWidget *parent)
    : QDialog(parent)
    , m_repoDir(repoDir)
    , m_entries(entries)
    , m_deepSeek(new DeepSeekClient(this))
{
    setWindowTitle(QStringLiteral("暂存审查"));
    resize(860, 520);

    auto *layout = new QVBoxLayout(this);

    m_lblSummary = new QLabel(this);
    m_lblSummary->setWordWrap(true);
    layout->addWidget(m_lblSummary);

    m_lblAiStatus = new QLabel(this);
    m_lblAiStatus->setWordWrap(true);
    m_lblAiStatus->setStyleSheet(QStringLiteral("color: #555;"));
    layout->addWidget(m_lblAiStatus);

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
        // 用户与 AI 均可勾选「危险」项；规则默认不勾，但不锁定。
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (e.risk == GitStageRisk::Blocked || e.risk == GitStageRisk::Suspicious)
            checkItem->setCheckState(Qt::Unchecked);
        else
            checkItem->setCheckState(Qt::Checked);
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
    m_btnDeepSeek = new QPushButton(QStringLiteral("DeepSeek 分类"), this);
    m_btnDeepSeek->setToolTip(
        QStringLiteral("让 DeepSeek 分类哪些应暂存提交；可覆盖规则「危险」项。"
                       "权限：使用者 > AI > 规则默认。"));
    quickRow->addWidget(btnNormal);
    quickRow->addWidget(btnSafe);
    quickRow->addWidget(btnNone);
    quickRow->addWidget(m_btnDeepSeek);
    quickRow->addStretch();
    layout->addLayout(quickRow);

    m_lblAutoIgnore = new QLabel(
        QStringLiteral("未勾选：确认后写入 .gitignore（有扩展名 → *.ext；无扩展名 → 相对路径+文件名）。"
                       "「危险」默认不勾，但用户与 DeepSeek 可勾选并以 git add -f 强制暂存。"
                       "确认后会对「已跟踪、未勾选且应忽略」的路径执行 git rm --cached（不删文件）。"),
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
    m_btnOk = new QPushButton(QStringLiteral("确认暂存所选"), this);
    m_btnOk->setDefault(true);
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(m_btnOk);
    layout->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnOk, &QPushButton::clicked, this, &GitStageReviewDialog::onAcceptClicked);
    connect(btnNormal, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectNormalOnly);
    connect(btnSafe, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectAllSafe);
    connect(btnNone, &QPushButton::clicked, this, &GitStageReviewDialog::onSelectNone);
    connect(m_btnDeepSeek, &QPushButton::clicked, this, &GitStageReviewDialog::onDeepSeekClassifyClicked);
    connect(m_deepSeek, &DeepSeekClient::chatFinished, this,
            &GitStageReviewDialog::onDeepSeekClassifyFinished);
    connect(m_deepSeek, &DeepSeekClient::chatFailed, this,
            &GitStageReviewDialog::onDeepSeekClassifyFailed);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        rebuildSummary();
    });

    rebuildSummary();

    // Auto-classify when API key is already configured (non-interactive: fail quietly).
    if (!DeepSeekClient::apiKey().isEmpty()) {
        m_lblAiStatus->setText(QStringLiteral("DeepSeek：打开后将自动分类哪些文件应提交…"));
        QTimer::singleShot(0, this, [this]() { startDeepSeekClassify(false); });
    } else {
        m_lblAiStatus->setText(
            QStringLiteral("DeepSeek：未配置 API Key。可点「DeepSeek 分类」，或先在主界面「DeepSeek 设置」填写。"));
    }
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
    QString summary =
        QStringLiteral("共 %1 项待处理：危险 %2（规则默认不勾，用户/AI 可勾选强制暂存），"
                       "可疑 %3（默认不勾），已勾选 %4。\n"
                       "将写入忽略规则：%5")
            .arg(m_entries.size())
            .arg(blocked)
            .arg(suspicious)
            .arg(checked)
            .arg(ignorePatterns.isEmpty() ? QStringLiteral("（无）")
                                          : ignorePatterns.join(QLatin1String(", ")));
    if (m_aiApplied)
        summary += QStringLiteral("\n勾选已按 DeepSeek 建议调整（可再手动改；使用者权限最高）。");
    m_lblSummary->setText(summary);
}

QStringList GitStageReviewDialog::pathsForIgnoreSuggestion() const
{
    QStringList paths;
    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        const QTableWidgetItem *item = m_table->item(row, ColCheck);
        const bool checked = item && item->checkState() == Qt::Checked;
        // 已勾选 = 用户/AI 要提交，不要写入 ignore。
        if (checked)
            continue;
        if (e.risk == GitStageRisk::Blocked) {
            if (GitStageGuard::isExtensionLessDangerous(e.path, nullptr))
                paths << e.path;
            continue;
        }
        paths << e.path;
    }
    return paths;
}

void GitStageReviewDialog::setDeepSeekBusy(bool busy)
{
    if (m_btnDeepSeek) {
        m_btnDeepSeek->setEnabled(!busy);
        m_btnDeepSeek->setText(busy ? QStringLiteral("DeepSeek 分类中…")
                                    : QStringLiteral("DeepSeek 分类"));
    }
}

void GitStageReviewDialog::onDeepSeekClassifyClicked()
{
    startDeepSeekClassify(true);
}

void GitStageReviewDialog::startDeepSeekClassify(bool interactive)
{
    if (!m_deepSeek)
        return;
    if (m_deepSeek->isBusy()) {
        if (interactive) {
            QMessageBox::information(this, QStringLiteral("DeepSeek"),
                                     QStringLiteral("已有请求进行中，请稍候。"));
        }
        return;
    }
    if (DeepSeekClient::apiKey().isEmpty()) {
        if (interactive) {
            QMessageBox::warning(
                this, QStringLiteral("DeepSeek"),
                QStringLiteral("未配置 DeepSeek API Key。\n请先在主界面 Git 区点击「DeepSeek 设置」。"));
        }
        m_lblAiStatus->setText(QStringLiteral("DeepSeek：未配置 API Key。"));
        return;
    }
    if (m_entries.isEmpty()) {
        if (interactive) {
            QMessageBox::information(this, QStringLiteral("DeepSeek"),
                                     QStringLiteral("没有待分类的路径。"));
        }
        return;
    }

    const QString systemPrompt = QStringLiteral(
        "你是 Git 暂存审查助手，负责判断哪些未提交路径「应该暂存并提交」。\n"
        "项目是 Qt/C++（qmake）桌面应用仓库。\n"
        "权限：使用者 > 你(AI) > 本地规则分类。规则分类（正常/可疑/危险）只是参考，你有权覆盖，"
        "包括对「危险/blocked」设 stage=true（将强制 git add -f）。\n"
        "判断原则：\n"
        "1) 源码/工程文件（.cpp/.h/.pro/.qml/.pri/.qrc/.ui 等）及配套文档（README、LICENSE）通常应提交。\n"
        "2) 构建产物、中间文件、本机配置、日志、密钥、.user、明显垃圾默认不提交。\n"
        "3) third_party 下为运行时依赖且体积合理的二进制（如 OpenSSL DLL）+ LICENSE/README 可提交；"
        "完整 SDK 解压树、超大缓存包通常不提交。\n"
        "4) 未跟踪的杂散数据（csv/日志/临时 txt）默认不提交；若明显是项目需要的资产可 stage=true。\n"
        "5) 对危险项：仅在确有提交必要时 stage=true，并在 reason 说明为何覆盖规则。\n"
        "只输出 JSON，不要 Markdown，不要解释。格式：\n"
        "{\"files\":[{\"path\":\"相对路径\",\"stage\":true,\"reason\":\"一句话中文原因\"}]}\n"
        "必须为输入中的每个 path 给出一条；path 必须与输入完全一致。");

    m_classifyInteractive = interactive;
    setDeepSeekBusy(true);
    m_lblAiStatus->setText(QStringLiteral("DeepSeek：正在分类哪些文件应提交…"));
    m_deepSeek->chat(systemPrompt, buildClassifyUserPrompt());
}

QString GitStageReviewDialog::buildClassifyUserPrompt() const
{
    QStringList lines;
    lines << QStringLiteral("仓库：%1").arg(m_repoDir);
    lines << QStringLiteral("待分类文件列表（path | git状态 | 规则分类 | 大小 | 规则原因）：");
    for (const GitStageEntry &e : m_entries) {
        lines << QStringLiteral("%1 | %2 | %3 | %4 | %5")
                     .arg(e.path,
                          e.xyStatus,
                          GitStageGuard::riskLabel(e.risk),
                          formatSize(e.sizeBytes),
                          e.reason.isEmpty() ? QStringLiteral("-") : e.reason);
    }
    lines << QStringLiteral("请返回 JSON，决定每个 path 的 stage true/false。");
    return lines.join(QLatin1Char('\n'));
}

bool GitStageReviewDialog::applyDeepSeekClassification(const QString &content, QString *errorOut)
{
    const QByteArray raw = extractJsonPayload(content).toUtf8();
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || doc.isNull()) {
        if (errorOut)
            *errorOut = QStringLiteral("无法解析 JSON：%1").arg(pe.errorString());
        return false;
    }

    QJsonArray files;
    if (doc.isArray()) {
        files = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("files")).isArray())
            files = obj.value(QStringLiteral("files")).toArray();
        else if (obj.value(QStringLiteral("items")).isArray())
            files = obj.value(QStringLiteral("items")).toArray();
        else {
            if (errorOut)
                *errorOut = QStringLiteral("JSON 中缺少 files 数组。");
            return false;
        }
    } else {
        if (errorOut)
            *errorOut = QStringLiteral("JSON 根类型无效。");
        return false;
    }

    QHash<QString, QPair<bool, QString>> decisions;
    for (const QJsonValue &v : files) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString path = normalizePathKey(o.value(QStringLiteral("path")).toString());
        if (path.isEmpty())
            continue;
        bool stage = false;
        if (o.contains(QStringLiteral("stage"))) {
            const QJsonValue sv = o.value(QStringLiteral("stage"));
            if (sv.isBool())
                stage = sv.toBool();
            else
                stage = QString::compare(sv.toString(), QStringLiteral("true"), Qt::CaseInsensitive) == 0
                    || sv.toString() == QStringLiteral("1")
                    || QString::compare(sv.toString(), QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        } else if (o.contains(QStringLiteral("commit"))) {
            stage = o.value(QStringLiteral("commit")).toBool();
        } else if (o.contains(QStringLiteral("should_stage"))) {
            stage = o.value(QStringLiteral("should_stage")).toBool();
        }
        const QString reason = o.value(QStringLiteral("reason")).toString().trimmed();
        decisions.insert(path, qMakePair(stage, reason));
    }

    if (decisions.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("模型未返回任何有效 path 决策。");
        return false;
    }

    int applied = 0;
    int stageCount = 0;
    int overrideBlocked = 0;
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        QTableWidgetItem *check = m_table->item(row, ColCheck);
        QTableWidgetItem *reasonItem = m_table->item(row, ColReason);
        if (!check)
            continue;

        const QString key = normalizePathKey(e.path);
        if (!decisions.contains(key))
            continue;

        const auto decision = decisions.value(key);
        check->setCheckState(decision.first ? Qt::Checked : Qt::Unchecked);
        if (decision.first) {
            ++stageCount;
            if (e.risk == GitStageRisk::Blocked)
                ++overrideBlocked;
        }
        if (reasonItem) {
            QString aiReason = decision.second.isEmpty()
                ? (decision.first ? QStringLiteral("建议提交") : QStringLiteral("建议不提交"))
                : decision.second;
            if (e.risk == GitStageRisk::Blocked && decision.first)
                aiReason = QStringLiteral("覆盖危险规则 · %1").arg(aiReason);
            reasonItem->setText(QStringLiteral("[AI] %1").arg(aiReason));
        }
        ++applied;
    }
    m_table->blockSignals(false);

    if (applied == 0) {
        if (errorOut)
            *errorOut = QStringLiteral("返回的 path 与列表对不上。");
        return false;
    }

    m_aiApplied = true;
    rebuildSummary();
    QString status =
        QStringLiteral("DeepSeek：已应用分类（匹配 %1/%2，建议勾选 %3）")
            .arg(applied)
            .arg(m_entries.size())
            .arg(stageCount);
    if (overrideBlocked > 0)
        status += QStringLiteral("，其中覆盖危险 %1 项").arg(overrideBlocked);
    status += QStringLiteral("。可手动改勾选（使用者权限最高）。");
    m_lblAiStatus->setText(status);
    return true;
}

void GitStageReviewDialog::onDeepSeekClassifyFinished(const QString &content)
{
    setDeepSeekBusy(false);
    const bool interactive = m_classifyInteractive;
    m_classifyInteractive = false;
    QString err;
    if (!applyDeepSeekClassification(content, &err)) {
        m_lblAiStatus->setText(QStringLiteral("DeepSeek：分类结果解析失败 — %1").arg(err));
        if (interactive) {
            QMessageBox::warning(this, QStringLiteral("DeepSeek 分类"),
                                 QStringLiteral("无法应用分类结果：\n%1\n\n已保留规则默认勾选。")
                                     .arg(err));
        }
    }
}

void GitStageReviewDialog::onDeepSeekClassifyFailed(const QString &error)
{
    setDeepSeekBusy(false);
    const bool interactive = m_classifyInteractive;
    m_classifyInteractive = false;
    m_lblAiStatus->setText(QStringLiteral("DeepSeek：分类失败 — %1").arg(error));
    if (interactive) {
        QMessageBox::warning(this, QStringLiteral("DeepSeek 分类"),
                             QStringLiteral("分类失败：\n%1").arg(error));
    }
}

void GitStageReviewDialog::onSelectNormalOnly()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item)
            continue;
        item->setCheckState(m_entries.at(row).risk == GitStageRisk::Normal ? Qt::Checked
                                                                           : Qt::Unchecked);
    }
    m_table->blockSignals(false);
    m_aiApplied = false;
    rebuildSummary();
}

void GitStageReviewDialog::onSelectAllSafe()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item)
            continue;
        // 「安全」快捷：勾选正常+可疑，危险仍不勾（可用 AI/手动覆盖）。
        item->setCheckState(m_entries.at(row).risk == GitStageRisk::Blocked ? Qt::Unchecked
                                                                            : Qt::Checked);
    }
    m_table->blockSignals(false);
    m_aiApplied = false;
    rebuildSummary();
}

void GitStageReviewDialog::onSelectNone()
{
    m_table->blockSignals(true);
    for (int row = 0; row < m_entries.size(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColCheck);
        if (!item)
            continue;
        item->setCheckState(Qt::Unchecked);
    }
    m_table->blockSignals(false);
    m_aiApplied = false;
    rebuildSummary();
}

void GitStageReviewDialog::onAcceptClicked()
{
    m_selectedPaths.clear();
    m_blockedTrackedPaths.clear();

    QStringList forcePaths;
    for (int row = 0; row < m_entries.size(); ++row) {
        const GitStageEntry &e = m_entries.at(row);
        const QTableWidgetItem *item = m_table->item(row, ColCheck);
        const bool checked = item && item->checkState() == Qt::Checked;

        // 仅对未勾选的已跟踪危险项做 rm --cached；已勾选表示要保留/强制暂存。
        if (e.risk == GitStageRisk::Blocked && !e.untracked && !checked)
            m_blockedTrackedPaths << e.path;

        if (!checked)
            continue;

        m_selectedPaths << e.path;
        if (e.risk == GitStageRisk::Blocked
            || GitStageGuard::isBlockedPath(m_repoDir, e.path, nullptr))
            forcePaths << e.path;
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

    if (!forcePaths.isEmpty()) {
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, QStringLiteral("强制暂存危险路径"),
            QStringLiteral("以下路径规则上为「危险」（匹配忽略规则等），但用户/AI 已勾选，"
                           "将使用 git add -f 强制暂存：\n\n%1\n\n是否继续？")
                .arg(forcePaths.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply != QMessageBox::Yes)
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
