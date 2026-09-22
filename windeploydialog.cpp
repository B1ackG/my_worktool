#include "windeploydialog.h"

#include "windeploypackager.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const QString kOrg = QStringLiteral("LiChenYang");
const QString kApp = QStringLiteral("LinuxHelper");
} // namespace

WinDeployDialog::WinDeployDialog(const QVector<WinDeployRepoRef> &repos, const QString &currentRepo,
                                 QWidget *parent)
    : QDialog(parent)
    , m_repos(repos)
    , m_packager(new WinDeployPackager(this))
{
    setWindowTitle(QStringLiteral("打包 Windows 目录"));
    setMinimumSize(640, 480);
    resize(760, 560);

    auto *hint = new QLabel(
        QStringLiteral("先在 Qt Creator 编好 Release，再打包。"
                       "输出文件夹可整夹拷到工控机，不必在那边安装 Qt。"),
        this);
    hint->setWordWrap(true);

    m_cmbRepo = new QComboBox(this);
    m_cmbRepo->setMinimumWidth(360);
    int select = 0;
    const QString currentAbs = currentRepo.trimmed().isEmpty()
        ? QString()
        : QDir(currentRepo).absolutePath();
    for (int i = 0; i < m_repos.size(); ++i) {
        const WinDeployRepoRef &ref = m_repos.at(i);
        const QString abs = QDir(ref.repoPath).absolutePath();
        const QString label = ref.displayName.isEmpty()
            ? QDir::toNativeSeparators(abs)
            : QStringLiteral("%1  (%2)").arg(ref.displayName, QDir::toNativeSeparators(abs));
        m_cmbRepo->addItem(label, abs);
        if (!currentAbs.isEmpty() && QDir(abs).absolutePath() == currentAbs) {
            select = i;
        }
    }
    if (m_cmbRepo->count() == 0) {
        m_cmbRepo->addItem(QStringLiteral("（无记忆仓库）"), QString());
    }
    m_cmbRepo->setCurrentIndex(select);

    m_editExe = new QLineEdit(this);
    auto *btnExe = new QPushButton(QStringLiteral("浏览…"), this);
    auto *exeRow = new QHBoxLayout();
    exeRow->addWidget(m_editExe, 1);
    exeRow->addWidget(btnExe);

    m_editOutput = new QLineEdit(this);
    auto *btnOut = new QPushButton(QStringLiteral("浏览…"), this);
    auto *outRow = new QHBoxLayout();
    outRow->addWidget(m_editOutput, 1);
    outRow->addWidget(btnOut);

    m_editQt = new QLineEdit(this);
    auto *btnQt = new QPushButton(QStringLiteral("浏览…"), this);
    auto *qtRow = new QHBoxLayout();
    qtRow->addWidget(m_editQt, 1);
    qtRow->addWidget(btnQt);

    QSettings s(kOrg, kApp);
    s.beginGroup(QStringLiteral("winDeploy"));
    const QString savedQt = s.value(QStringLiteral("qtDir")).toString().trimmed();
    s.endGroup();
    m_editQt->setText(savedQt.isEmpty() ? WinDeployPackager::defaultQtDir() : savedQt);
    m_editQt->setPlaceholderText(QStringLiteral("含 bin/windeployqt.exe 的 Qt 目录"));
    m_editQt->setToolTip(QStringLiteral("默认使用本助手正在运行的 Qt；找不到再回落到 D:/Qt/5.15.2/mingw81_64"));

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("项目"), m_cmbRepo);
    form->addRow(QStringLiteral("可执行文件"), exeRow);
    form->addRow(QStringLiteral("输出目录"), outRow);
    form->addRow(QStringLiteral("Qt 目录"), qtRow);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("打包日志"));

    m_btnPack = new QPushButton(QStringLiteral("开始打包"), this);
    m_btnPack->setStyleSheet(QStringLiteral("font-weight: bold; background-color: #c8e6c9;"));
    m_btnOpen = new QPushButton(QStringLiteral("打开输出目录"), this);
    m_btnOpen->setEnabled(false);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), this);

#ifndef Q_OS_WIN
    m_btnPack->setEnabled(false);
    m_btnPack->setToolTip(QStringLiteral("仅 Windows 可用，Linux 请用 Git 页的 SCP 传输。"));
    hint->setText(hint->text() + QStringLiteral("\n当前不是 Windows，打包已禁用。"));
#endif

    auto *btns = new QHBoxLayout();
    btns->addWidget(m_btnPack);
    btns->addWidget(m_btnOpen);
    btns->addStretch();
    btns->addWidget(btnClose);

    auto *root = new QVBoxLayout(this);
    root->addWidget(hint);
    root->addLayout(form);
    root->addWidget(m_log, 1);
    root->addLayout(btns);

    connect(m_packager, &WinDeployPackager::logMessage, this, &WinDeployDialog::appendLog);
    connect(m_cmbRepo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &WinDeployDialog::onRepoChanged);
    connect(btnExe, &QPushButton::clicked, this, &WinDeployDialog::onBrowseExe);
    connect(btnOut, &QPushButton::clicked, this, &WinDeployDialog::onBrowseOutput);
    connect(btnQt, &QPushButton::clicked, this, &WinDeployDialog::onBrowseQt);
    connect(m_btnPack, &QPushButton::clicked, this, &WinDeployDialog::onPackClicked);
    connect(m_btnOpen, &QPushButton::clicked, this, &WinDeployDialog::onOpenOutputClicked);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);
    connect(m_editExe, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_editOutput->text().trimmed().isEmpty() && !text.trimmed().isEmpty()) {
            m_editOutput->setText(WinDeployPackager::defaultOutputDir(text));
        }
    });

    onRepoChanged();
}

QString WinDeployDialog::currentRepoPath() const
{
    return m_cmbRepo ? m_cmbRepo->currentData().toString() : QString();
}

void WinDeployDialog::fillExeForRepo(const QString &repoDir)
{
    const QFileInfo exe = WinDeployPackager::findLatestExe(repoDir);
    m_editExe->setText(exe.exists() ? QDir::toNativeSeparators(exe.absoluteFilePath()) : QString());
}

void WinDeployDialog::loadSavedOutput(const QString &repoDir)
{
    QSettings s(kOrg, kApp);
    s.beginGroup(QStringLiteral("winDeploy"));
    s.beginGroup(QStringLiteral("outputByRepo"));
    const QString saved = s.value(WinDeployPackager::repoSettingsKey(repoDir)).toString().trimmed();
    s.endGroup();
    s.endGroup();

    if (!saved.isEmpty()) {
        m_editOutput->setText(QDir::toNativeSeparators(saved));
        return;
    }
    const QString exe = m_editExe->text().trimmed();
    if (!exe.isEmpty()) {
        m_editOutput->setText(QDir::toNativeSeparators(WinDeployPackager::defaultOutputDir(exe)));
    } else {
        m_editOutput->clear();
    }
}

void WinDeployDialog::onRepoChanged()
{
    const QString repo = currentRepoPath();
    fillExeForRepo(repo);
    loadSavedOutput(repo);
    m_btnOpen->setEnabled(QDir(m_editOutput->text().trimmed()).exists());
}

void WinDeployDialog::onBrowseExe()
{
    const QString start = m_editExe->text().trimmed().isEmpty()
        ? currentRepoPath()
        : QFileInfo(m_editExe->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择可执行文件"), start,
#ifdef Q_OS_WIN
                                                      QStringLiteral("可执行文件 (*.exe)")
#else
                                                      QStringLiteral("所有文件 (*)")
#endif
    );
    if (!path.isEmpty()) {
        m_editExe->setText(QDir::toNativeSeparators(path));
        if (m_editOutput->text().trimmed().isEmpty()) {
            m_editOutput->setText(
                QDir::toNativeSeparators(WinDeployPackager::defaultOutputDir(path)));
        }
    }
}

void WinDeployDialog::onBrowseOutput()
{
    const QString start = m_editOutput->text().trimmed().isEmpty()
        ? QDir::homePath()
        : m_editOutput->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"), start);
    if (!dir.isEmpty()) {
        m_editOutput->setText(QDir::toNativeSeparators(dir));
        m_btnOpen->setEnabled(true);
    }
}

void WinDeployDialog::onBrowseQt()
{
    const QString start = m_editQt->text().trimmed().isEmpty() ? QStringLiteral("D:/Qt")
                                                               : m_editQt->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Qt 目录"), start);
    if (!dir.isEmpty()) {
        m_editQt->setText(QDir::toNativeSeparators(dir));
    }
}

void WinDeployDialog::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void WinDeployDialog::setBusy(bool busy)
{
    m_btnPack->setEnabled(!busy);
#ifndef Q_OS_WIN
    m_btnPack->setEnabled(false);
#endif
    m_cmbRepo->setEnabled(!busy);
    m_editExe->setEnabled(!busy);
    m_editOutput->setEnabled(!busy);
    m_editQt->setEnabled(!busy);
}

void WinDeployDialog::saveSettings() const
{
    QSettings s(kOrg, kApp);
    s.beginGroup(QStringLiteral("winDeploy"));
    s.setValue(QStringLiteral("qtDir"), m_editQt->text().trimmed());
    const QString repo = currentRepoPath();
    if (!repo.isEmpty()) {
        s.beginGroup(QStringLiteral("outputByRepo"));
        s.setValue(WinDeployPackager::repoSettingsKey(repo),
                   QDir::fromNativeSeparators(m_editOutput->text().trimmed()));
        s.endGroup();
    }
    s.endGroup();
}

void WinDeployDialog::onPackClicked()
{
    m_log->clear();
    WinDeployRequest req;
    req.repoDir = currentRepoPath();
    req.exePath = m_editExe->text().trimmed();
    req.outputDir = m_editOutput->text().trimmed();
    req.qtDir = m_editQt->text().trimmed();

    if (req.exePath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("未找到 exe。请先编译 Release，或点浏览选择。"));
        return;
    }
    if (req.outputDir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请选择输出目录。"));
        return;
    }

    setBusy(true);
    QString err;
    const bool ok = m_packager->pack(req, &err);
    setBusy(false);
    m_btnOpen->setEnabled(QDir(req.outputDir).exists());

    if (!ok) {
        appendLog(QStringLiteral("错误: %1").arg(err));
        QMessageBox::warning(this, QStringLiteral("打包失败"), err);
        return;
    }

    saveSettings();
    QMessageBox::information(
        this, QStringLiteral("打包完成"),
        QStringLiteral("已输出到:\n%1\n\n把这个文件夹整夹拷到工控机，双击其中的 exe。")
            .arg(QDir::toNativeSeparators(req.outputDir)));
}

void WinDeployDialog::onOpenOutputClicked()
{
    const QString dir = m_editOutput->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("输出目录还不存在。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
