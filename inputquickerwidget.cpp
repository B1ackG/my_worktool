#include "inputquickerwidget.h"
#include "quickerbindingdialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

InputQuickerWidget::InputQuickerWidget(QWidget *parent)
    : QWidget(parent)
    , manager(new InputQuickerManager(this))
    , statusTimer(new QTimer(this))
{
    QLabel *header = new QLabel(QStringLiteral("快捷助手"), this);
    header->setStyleSheet(QStringLiteral(
        "font-size: 20px; font-weight: 700; color: #1f2a37; letter-spacing: 0.2px;"));

#ifdef Q_OS_WIN
    QLabel *hint = new QLabel(
        QStringLiteral("快捷助手依赖 Linux evdev/xdotool，当前 Windows 构建不可用。"
                       "可在 Linux 下使用本功能。"),
        this);
#else
    QLabel *hint = new QLabel(
        QStringLiteral("本页管理映射规则与守护进程；实际按键由开机自启的守护脚本执行，主窗口关闭后仍生效。"),
        this);
#endif
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #5b6b7c; margin: 2px 0 10px 0;"));

    chkEnabled = new QCheckBox(QStringLiteral("启用映射守护（开机自启）"), this);
    chkEnabled->setChecked(true);
    chkEnabled->setToolTip(
        QStringLiteral("开启：启动守护脚本并写入开机自启。\n"
                       "关闭：停止脚本并取消开机自启。\n"
                       "本程序只负责管理配置与脚本，映射不依赖主窗口保持打开。"));

    cmbDevice = new QComboBox(this);
    cmbDevice->setMinimumWidth(360);
    cmbDevice->setToolTip(QStringLiteral("选择要映射的输入设备；「自动选择」按能力评分挑选。"));

    btnRefreshDevices = new QPushButton(QStringLiteral("刷新设备"), this);
    btnRefreshDevices->setToolTip(QStringLiteral("重新扫描输入设备列表。"));

    lblStatus = new QLabel(QStringLiteral("状态: 未加载"), this);
    lblStatus->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    lblStatus->setMinimumWidth(140);
    lblStatus->setStyleSheet(QStringLiteral(
        "font-weight: 600; color: #334155; background: #e8eef5; border-radius: 4px; padding: 4px 10px;"));

    QFrame *controlBar = new QFrame(this);
    controlBar->setObjectName(QStringLiteral("quickerControlBar"));
    controlBar->setStyleSheet(QStringLiteral(
        "#quickerControlBar {"
        "  background: #ffffff;"
        "  border: 1px solid #d5dde5;"
        "  border-radius: 8px;"
        "}"
        "#quickerControlBar QLabel { color: #475569; }"));
    QHBoxLayout *topRow = new QHBoxLayout(controlBar);
    topRow->setContentsMargins(12, 10, 12, 10);
    topRow->setSpacing(10);
    topRow->addWidget(chkEnabled);
    topRow->addSpacing(8);
    topRow->addWidget(new QLabel(QStringLiteral("输入设备:"), controlBar));
    topRow->addWidget(cmbDevice, 1);
    topRow->addWidget(btnRefreshDevices);
    topRow->addWidget(lblStatus);

    tblBindings = new QTableWidget(0, 5, this);
    tblBindings->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("触发器"), QStringLiteral("动作"),
         QStringLiteral("启用"), QStringLiteral("操作")});
    tblBindings->horizontalHeader()->setStretchLastSection(true);
    tblBindings->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tblBindings->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tblBindings->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tblBindings->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblBindings->setSelectionMode(QAbstractItemView::SingleSelection);
    tblBindings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblBindings->setAlternatingRowColors(true);
    tblBindings->setShowGrid(false);
    tblBindings->verticalHeader()->setVisible(false);
    tblBindings->setStyleSheet(QStringLiteral(
        "QTableWidget { alternate-background-color: #f7fafc; }"
        "QTableWidget::item { padding: 4px; }"));

    btnAddBinding = new QPushButton(QStringLiteral("＋ 新增规则"), this);
    btnAddBinding->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #2f6fed; color: white; border: none; font-weight: 600;"
        "  padding: 7px 16px; border-radius: 6px;"
        "}"
        "QPushButton:hover { background: #245bd1; }"
        "QPushButton:pressed { background: #1e4dad; }"));

    QHBoxLayout *bindingButtonLayout = new QHBoxLayout();
    bindingButtonLayout->setContentsMargins(0, 4, 0, 0);
    bindingButtonLayout->addWidget(btnAddBinding);
    bindingButtonLayout->addStretch();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);
    layout->addWidget(header);
    layout->addWidget(hint);
    layout->addWidget(controlBar);
    layout->addLayout(bindingButtonLayout);
    layout->addWidget(tblBindings, 1);

    connect(btnRefreshDevices, &QPushButton::clicked, this, &InputQuickerWidget::populateDevices);
    connect(btnAddBinding, &QPushButton::clicked, this, &InputQuickerWidget::onAddBindingClicked);
    connect(chkEnabled, &QCheckBox::toggled, this, &InputQuickerWidget::onEnabledToggled);
    connect(cmbDevice, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InputQuickerWidget::onDeviceChanged);
    connect(manager, &InputQuickerManager::bindingsChanged, this, &InputQuickerWidget::refreshBindingsTable);
    connect(manager, &InputQuickerManager::statusChanged, this, &InputQuickerWidget::updateStatus);
    connect(statusTimer, &QTimer::timeout, this, &InputQuickerWidget::pollStatus);

    manager->loadSettings();

#ifdef Q_OS_WIN
    chkEnabled->setEnabled(false);
    cmbDevice->setEnabled(false);
    btnRefreshDevices->setEnabled(false);
    btnAddBinding->setEnabled(false);
    tblBindings->setEnabled(false);
    {
        const QSignalBlocker blocker(chkEnabled);
        chkEnabled->setChecked(false);
    }
    updateStatus(QStringLiteral("Windows 构建不可用"));
    const InputQuickerEnvCheck env = manager->runEnvironmentCheck();
    if (!env.messages.isEmpty()) {
        hint->setText(env.messages.join(QLatin1Char('\n')));
        hint->setStyleSheet(QStringLiteral("color: #b71c1c; margin-bottom: 4px;"));
    }
#else
    populateDevices();

    {
        const QSignalBlocker blocker(chkEnabled);
        chkEnabled->setChecked(manager->enabled());
    }

    ensureDefaultsIfEmpty();
    refreshBindingsTable();
    updateStatus(manager->statusText());
    statusTimer->start(1500);
    // Do not stop/restart a boot-started daemon just because the UI opened.
    ensureDaemonManaged();
#endif
}

InputQuickerWidget::~InputQuickerWidget() = default;

QString InputQuickerWidget::selectedDevicePath() const
{
    return cmbDevice->currentData().toString();
}

QString InputQuickerWidget::selectedDeviceName() const
{
    return cmbDevice->currentData(Qt::UserRole + 1).toString();
}

void InputQuickerWidget::populateDevices()
{
    const QString selectedPath = selectedDevicePath().isEmpty() ? manager->devicePath() : selectedDevicePath();
    const QString selectedName = manager->deviceName();
    const QSignalBlocker blocker(cmbDevice);
    cmbDevice->clear();
    cmbDevice->addItem(QStringLiteral("自动选择（按能力评分）"), QString());

    const QList<InputQuickerManager::DeviceInfo> devices = manager->refreshDeviceList();
    int matchByNameIndex = -1;
    for (const InputQuickerManager::DeviceInfo &device : devices) {
        QString label;
        if (device.recommended) {
            label = QStringLiteral("[推荐] %1  (%2)").arg(device.name).arg(device.path);
        } else {
            label = QStringLiteral("%1  (%2)").arg(device.name).arg(device.path);
        }
        if (!device.capsSummary.isEmpty()) {
            label += QStringLiteral(" [%1]").arg(device.capsSummary);
        }
        if (!device.accessible) {
            label += QStringLiteral(" (无权限)");
        }
        cmbDevice->addItem(label, device.path);
        cmbDevice->setItemData(cmbDevice->count() - 1, device.name, Qt::UserRole + 1);
        if (matchByNameIndex < 0 && !selectedName.isEmpty()
            && device.name.compare(selectedName, Qt::CaseInsensitive) == 0) {
            matchByNameIndex = cmbDevice->count() - 1;
        }
    }

    const int index = cmbDevice->findData(selectedPath);
    if (index >= 0) {
        cmbDevice->setCurrentIndex(index);
    } else if (matchByNameIndex >= 0) {
        // eventN renumbered after reboot — keep the same mouse by name.
        cmbDevice->setCurrentIndex(matchByNameIndex);
    } else if (!selectedPath.isEmpty()) {
        cmbDevice->addItem(QStringLiteral("手动设备: %1").arg(selectedPath), selectedPath);
        cmbDevice->setItemData(cmbDevice->count() - 1, selectedName, Qt::UserRole + 1);
        cmbDevice->setCurrentIndex(cmbDevice->count() - 1);
    } else if (!selectedName.isEmpty()) {
        cmbDevice->addItem(QStringLiteral("按名称: %1（当前未连接）").arg(selectedName), QString());
        cmbDevice->setItemData(cmbDevice->count() - 1, selectedName, Qt::UserRole + 1);
        cmbDevice->setCurrentIndex(cmbDevice->count() - 1);
    }
}

void InputQuickerWidget::refreshBindingsTable()
{
    const QSignalBlocker blocker(tblBindings);
    tblBindings->setRowCount(0);

    const QList<QuickerBinding> bindings = manager->bindings();
    for (int row = 0; row < bindings.size(); ++row) {
        const QuickerBinding &binding = bindings.at(row);
        tblBindings->insertRow(row);
        tblBindings->setItem(row, 0, new QTableWidgetItem(binding.name));
        tblBindings->setItem(row, 1, new QTableWidgetItem(triggerDisplayText(binding.trigger)));
        tblBindings->setItem(row, 2, new QTableWidgetItem(actionDisplayText(binding.action)));

        QWidget *enabledWidget = new QWidget(tblBindings);
        QCheckBox *enabledCheck = new QCheckBox(enabledWidget);
        enabledCheck->setChecked(binding.enabled);
        enabledCheck->setProperty("bindingId", binding.id);
        QHBoxLayout *enabledLayout = new QHBoxLayout(enabledWidget);
        enabledLayout->addWidget(enabledCheck);
        enabledLayout->setAlignment(Qt::AlignCenter);
        enabledLayout->setContentsMargins(0, 0, 0, 0);
        tblBindings->setCellWidget(row, 3, enabledWidget);
        connect(enabledCheck, &QCheckBox::toggled, this, &InputQuickerWidget::onBindingEnabledToggled);

        QWidget *actionWidget = new QWidget(tblBindings);
        QPushButton *editButton = new QPushButton(QStringLiteral("编辑"), actionWidget);
        QPushButton *deleteButton = new QPushButton(QStringLiteral("删除"), actionWidget);
        editButton->setProperty("bindingId", binding.id);
        deleteButton->setProperty("bindingId", binding.id);
        QHBoxLayout *actionLayout = new QHBoxLayout(actionWidget);
        actionLayout->addWidget(editButton);
        actionLayout->addWidget(deleteButton);
        actionLayout->setContentsMargins(0, 0, 0, 0);
        tblBindings->setCellWidget(row, 4, actionWidget);
        connect(editButton, &QPushButton::clicked, this, &InputQuickerWidget::onEditBindingClicked);
        connect(deleteButton, &QPushButton::clicked, this, &InputQuickerWidget::onDeleteBindingClicked);
    }
}

void InputQuickerWidget::onEnabledToggled(bool checked)
{
    Q_UNUSED(checked)
    applyChanges();
}

void InputQuickerWidget::onDeviceChanged()
{
    applyChanges();
}

void InputQuickerWidget::onAddBindingClicked()
{
    QuickerBindingDialog dialog(manager, this);
    QuickerBinding binding;
    binding.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    binding.enabled = true;
    binding.trigger = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("mouse_button")},
        {QStringLiteral("code"), QStringLiteral("BTN_SIDE")}
    };
    binding.action = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("preset")},
        {QStringLiteral("preset"), QStringLiteral("workspace_prev")}
    };
    dialog.setBinding(binding);
    dialog.setDevicePath(selectedDevicePath());

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QuickerBinding result = dialog.binding();
    if (result.name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无效规则"), QStringLiteral("请填写规则名称。"));
        return;
    }
    if (manager->isTriggerUsed(result.trigger)) {
        QMessageBox::warning(this, QStringLiteral("触发器冲突"), QStringLiteral("该触发器已被其他规则使用。"));
        return;
    }
    manager->addBinding(result);
    applyChanges();
}

void InputQuickerWidget::onEditBindingClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button) {
        return;
    }
    const QString bindingId = button->property("bindingId").toString();
    QuickerBinding binding = manager->bindingById(bindingId);
    if (binding.id.isEmpty()) {
        return;
    }

    QuickerBindingDialog dialog(manager, this);
    dialog.setBinding(binding);
    dialog.setDevicePath(selectedDevicePath());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QuickerBinding result = dialog.binding();
    if (result.name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无效规则"), QStringLiteral("请填写规则名称。"));
        return;
    }
    if (manager->isTriggerUsed(result.trigger, result.id)) {
        QMessageBox::warning(this, QStringLiteral("触发器冲突"), QStringLiteral("该触发器已被其他规则使用。"));
        return;
    }
    manager->updateBinding(result);
    applyChanges();
}

void InputQuickerWidget::onDeleteBindingClicked()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (!button) {
        return;
    }
    const QString bindingId = button->property("bindingId").toString();
    if (bindingId.isEmpty()) {
        return;
    }

    const auto reply = QMessageBox::question(this, QStringLiteral("删除规则"), QStringLiteral("确定删除这条规则吗？"));
    if (reply == QMessageBox::Yes) {
        manager->removeBinding(bindingId);
        applyChanges();
    }
}

void InputQuickerWidget::onBindingEnabledToggled()
{
    QCheckBox *check = qobject_cast<QCheckBox *>(sender());
    if (!check) {
        return;
    }
    const QString bindingId = check->property("bindingId").toString();
    QuickerBinding binding = manager->bindingById(bindingId);
    if (binding.id.isEmpty()) {
        return;
    }
    binding.enabled = check->isChecked();
    manager->updateBinding(binding);
    applyChanges();
}

void InputQuickerWidget::updateStatus(const QString &status)
{
    lblStatus->setText(QStringLiteral("状态: %1").arg(status));
    if (status.startsWith(QStringLiteral("守护脚本运行中"))) {
        lblStatus->setStyleSheet(QStringLiteral(
            "font-weight: 600; color: #166534; background: #dcfce7; border-radius: 4px; padding: 4px 10px;"));
    } else if (status.startsWith(QStringLiteral("已关闭"))) {
        lblStatus->setStyleSheet(QStringLiteral(
            "font-weight: 600; color: #475569; background: #e8eef5; border-radius: 4px; padding: 4px 10px;"));
    } else {
        lblStatus->setStyleSheet(QStringLiteral(
            "font-weight: 600; color: #9a3412; background: #ffedd5; border-radius: 4px; padding: 4px 10px;"));
    }
}

void InputQuickerWidget::pollStatus()
{
    updateStatus(manager->statusText());
}

QString InputQuickerWidget::triggerDisplayText(const QJsonObject &trigger) const
{
    const QString type = trigger.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("mouse_button")) {
        return QStringLiteral("鼠标键 %1").arg(trigger.value(QStringLiteral("code")).toString());
    }
    if (type == QStringLiteral("wheel")) {
        const QString direction = trigger.value(QStringLiteral("direction")).toString();
        const QString dirText = direction == QStringLiteral("positive") ? QStringLiteral("正向")
                                                                        : QStringLiteral("反向");
        return QStringLiteral("滚轮 %1 %2").arg(trigger.value(QStringLiteral("axis")).toString(), dirText);
    }
    return QStringLiteral("未知触发器");
}

QString InputQuickerWidget::actionDisplayText(const QJsonObject &action) const
{
    const QString type = action.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("keyboard")) {
        return QStringLiteral("键盘: %1").arg(action.value(QStringLiteral("combo")).toString());
    }
    if (type == QStringLiteral("command")) {
        return QStringLiteral("命令: %1").arg(action.value(QStringLiteral("command")).toString());
    }
    if (type == QStringLiteral("preset")) {
        return QStringLiteral("预设: %1").arg(action.value(QStringLiteral("preset")).toString());
    }
    return QStringLiteral("未知动作");
}

void InputQuickerWidget::syncManagerFromUi()
{
    const bool enabled = chkEnabled->isChecked();
    manager->setEnabled(enabled);
    // Enable = install boot autostart; disable = remove it.
    manager->setDaemonAutostart(enabled);
    manager->setDevicePath(selectedDevicePath());
    manager->setDeviceName(selectedDeviceName());
}

void InputQuickerWidget::ensureDaemonManaged()
{
    syncManagerFromUi();
    manager->saveSettings();
    if (!manager->enabled()) {
        manager->setDaemonAutostartInstalled(false);
        updateStatus(manager->statusText());
        return;
    }

    manager->setDaemonAutostartInstalled(true);
    if (!manager->isDaemonRunning()) {
        if (!manager->startDaemon()) {
            updateStatus(QStringLiteral("启动失败 — 请查看日志"));
            return;
        }
    }
    updateStatus(manager->statusText());
}

bool InputQuickerWidget::applyChanges(bool showFailureDialog)
{
    syncManagerFromUi();
    if (!manager->applySettings()) {
        if (showFailureDialog) {
            reportApplyFailure();
        } else {
            const InputQuickerEnvCheck check = manager->runEnvironmentCheck();
            QString shortStatus = QStringLiteral("启动失败");
            if (!check.evdevOk || !check.xdotoolOk) {
                shortStatus = QStringLiteral("启动失败 — 缺少依赖");
            } else if (!check.inputReadable) {
                shortStatus = QStringLiteral("启动失败 — 无设备权限");
            }
            updateStatus(shortStatus);
        }
        return false;
    }
    updateStatus(manager->statusText());
    return true;
}

void InputQuickerWidget::ensureDefaultsIfEmpty()
{
    if (!manager->bindings().isEmpty()) {
        return;
    }
    manager->importDefaultWorkspaceBindings();
}

void InputQuickerWidget::reportApplyFailure()
{
    const InputQuickerEnvCheck check = manager->runEnvironmentCheck();
    const QString detail = check.messages.join(QStringLiteral("\n"));
    QString shortStatus = QStringLiteral("启动失败");
    if (!check.evdevOk || !check.xdotoolOk) {
        shortStatus = QStringLiteral("启动失败 — 缺少依赖");
    } else if (!check.inputReadable) {
        shortStatus = QStringLiteral("启动失败 — 无设备权限");
    }
    updateStatus(shortStatus);

    QMessageBox::warning(
        this,
        QStringLiteral("无法启动映射"),
        QStringLiteral("设备映射未能启动。\n\n%1\n\n日志: %2")
            .arg(detail, manager->logFilePath()));
}
