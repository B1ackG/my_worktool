#include "inputquickerwidget.h"
#include "quickerbindingdialog.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QColor>
#include <QBrush>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int kMonitorTriggerRole = Qt::UserRole;
constexpr int kMonitorMaxRows = 500;
}

InputQuickerWidget::InputQuickerWidget(QWidget *parent)
    : QWidget(parent)
    , manager(new InputQuickerManager(this))
{
    QLabel *header = new QLabel(QStringLiteral("快捷助手"), this);
    header->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold; color: #333; margin-bottom: 8px;"));

    chkEnabled = new QCheckBox(QStringLiteral("启用快捷助手"), this);
    chkEnabled->setChecked(true);

    chkGrabDevice = new QCheckBox(QStringLiteral("独占捕获设备（可能影响鼠标正常功能）"), this);
    chkGrabDevice->setToolTip(
        QStringLiteral("默认不勾选：监听设备但不独占，鼠标/滚轮仍交给系统。\n"
                       "映射主滚轮时，系统滚动与快捷动作会同时触发。\n"
                       "仅当不勾选时收不到事件再开启此项。"));
    chkGrabDevice->setChecked(false);

    cmbDevice = new QComboBox(this);
    cmbDevice->setMinimumWidth(360);
    cmbWheelAxis = new QComboBox(this);
    cmbWheelAxis->addItem(QStringLiteral("REL_HWHEEL (第二滚轮/横向)"), QStringLiteral("REL_HWHEEL"));
    cmbWheelAxis->addItem(QStringLiteral("REL_WHEEL (普通滚轮)"), QStringLiteral("REL_WHEEL"));

    btnRefreshDevices = new QPushButton(QStringLiteral("刷新设备"), this);
    btnEnvCheck = new QPushButton(QStringLiteral("环境检测"), this);
    btnStartMonitor = new QPushButton(QStringLiteral("开始监听"), this);
    btnStartMonitor->setStyleSheet(QStringLiteral("background-color: #e3f2fd; font-weight: bold;"));
    btnStopMonitor = new QPushButton(QStringLiteral("停止监听"), this);
    btnStopMonitor->setEnabled(false);
    btnApply = new QPushButton(QStringLiteral("应用配置"), this);
    btnApply->setStyleSheet(QStringLiteral("font-weight: bold;"));
    btnStop = new QPushButton(QStringLiteral("停止 daemon"), this);
    lblStatus = new QLabel(QStringLiteral("状态: 未加载"), this);
    lblStatus->setStyleSheet(QStringLiteral("font-weight: bold; color: #555;"));

    QGroupBox *topGroup = new QGroupBox(QStringLiteral("设备与控制"), this);
    QGridLayout *topGrid = new QGridLayout(topGroup);
    topGrid->addWidget(chkEnabled, 0, 0, 1, 2);
    topGrid->addWidget(chkGrabDevice, 1, 0, 1, 2);
    topGrid->addWidget(new QLabel(QStringLiteral("输入设备:")), 2, 0);
    topGrid->addWidget(cmbDevice, 2, 1);
    topGrid->addWidget(new QLabel(QStringLiteral("默认滚轮轴:")), 3, 0);
    topGrid->addWidget(cmbWheelAxis, 3, 1);

    QHBoxLayout *topButtonLayout = new QHBoxLayout();
    topButtonLayout->addWidget(btnRefreshDevices);
    topButtonLayout->addWidget(btnEnvCheck);
    topButtonLayout->addWidget(btnStartMonitor);
    topButtonLayout->addWidget(btnStopMonitor);
    topButtonLayout->addWidget(btnApply);
    topButtonLayout->addWidget(btnStop);
    topButtonLayout->addStretch();
    topButtonLayout->addWidget(lblStatus);
    topGrid->addLayout(topButtonLayout, 4, 0, 1, 2);

    tblMonitor = new QTableWidget(0, 3, this);
    tblMonitor->setHorizontalHeaderLabels(
        {QStringLiteral("时间"), QStringLiteral("事件"), QStringLiteral("可绑定")});
    tblMonitor->horizontalHeader()->setStretchLastSection(true);
    tblMonitor->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tblMonitor->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tblMonitor->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblMonitor->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tblMonitor->setAlternatingRowColors(true);
    tblMonitor->setToolTip(QStringLiteral("单击或双击可绑定事件可创建快捷规则"));

    QGroupBox *monitorGroup = new QGroupBox(QStringLiteral("输入检测"), this);
    QVBoxLayout *monitorLayout = new QVBoxLayout(monitorGroup);
    QLabel *monitorHint = new QLabel(
        QStringLiteral("开始监听后，在此查看鼠标按键/滚轮事件。单击或双击带「可绑定」的事件可创建规则。"), this);
    monitorHint->setWordWrap(true);
    monitorHint->setStyleSheet(QStringLiteral("color: #666;"));
    monitorLayout->addWidget(monitorHint);
    monitorLayout->addWidget(tblMonitor, 1);

    tblBindings = new QTableWidget(0, 5, this);
    tblBindings->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("触发器"), QStringLiteral("动作"), QStringLiteral("启用"), QStringLiteral("操作")});
    tblBindings->horizontalHeader()->setStretchLastSection(true);
    tblBindings->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tblBindings->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tblBindings->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tblBindings->setSelectionBehavior(QAbstractItemView::SelectRows);
    tblBindings->setEditTriggers(QAbstractItemView::NoEditTriggers);

    btnAddBinding = new QPushButton(QStringLiteral("新增规则"), this);
    btnImportDefaults = new QPushButton(QStringLiteral("导入默认工作区规则"), this);

    QGroupBox *rulesGroup = new QGroupBox(QStringLiteral("快捷规则"), this);
    QVBoxLayout *rulesLayout = new QVBoxLayout(rulesGroup);
    QHBoxLayout *bindingButtonLayout = new QHBoxLayout();
    bindingButtonLayout->addWidget(btnAddBinding);
    bindingButtonLayout->addWidget(btnImportDefaults);
    bindingButtonLayout->addStretch();
    rulesLayout->addLayout(bindingButtonLayout);
    rulesLayout->addWidget(tblBindings, 1);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(monitorGroup);
    splitter->addWidget(rulesGroup);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    txtLog = new QTextEdit(this);
    txtLog->setReadOnly(true);
    txtLog->setMaximumHeight(140);
    txtLog->setPlaceholderText(QStringLiteral("快捷助手运行日志..."));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(header);
    layout->addWidget(topGroup);
    layout->addWidget(splitter, 1);
    layout->addWidget(new QLabel(QStringLiteral("运行日志:"), this));
    layout->addWidget(txtLog);

    connect(btnRefreshDevices, &QPushButton::clicked, this, &InputQuickerWidget::populateDevices);
    connect(btnEnvCheck, &QPushButton::clicked, this, &InputQuickerWidget::onEnvCheckClicked);
    connect(btnStartMonitor, &QPushButton::clicked, this, &InputQuickerWidget::onStartMonitorClicked);
    connect(btnStopMonitor, &QPushButton::clicked, this, &InputQuickerWidget::onStopMonitorClicked);
    connect(btnApply, &QPushButton::clicked, this, &InputQuickerWidget::onApplyClicked);
    connect(btnStop, &QPushButton::clicked, this, &InputQuickerWidget::onStopClicked);
    connect(btnAddBinding, &QPushButton::clicked, this, &InputQuickerWidget::onAddBindingClicked);
    connect(btnImportDefaults, &QPushButton::clicked, this, &InputQuickerWidget::onImportDefaultsClicked);
    connect(manager, &InputQuickerManager::bindingsChanged, this, &InputQuickerWidget::refreshBindingsTable);
    connect(manager, &InputQuickerManager::statusChanged, this, &InputQuickerWidget::updateStatus);
    connect(manager, &InputQuickerManager::logMessage, this, &InputQuickerWidget::appendLog);
    connect(manager, &InputQuickerManager::monitorEventReceived, this, &InputQuickerWidget::onMonitorEvent);
    connect(manager, &InputQuickerManager::monitorStarted, this, &InputQuickerWidget::onMonitorStarted);
    connect(manager, &InputQuickerManager::monitorStopped, this, &InputQuickerWidget::onMonitorStopped);
    connect(tblMonitor, &QTableWidget::cellClicked, this, &InputQuickerWidget::onMonitorRowActivated);
    connect(tblMonitor, &QTableWidget::cellDoubleClicked, this, &InputQuickerWidget::onMonitorRowDoubleClicked);

    manager->loadSettings();
    populateDevices();

    chkEnabled->setChecked(manager->enabled());
    chkGrabDevice->setChecked(manager->grabDevice());
    const int axisIndex = cmbWheelAxis->findData(manager->wheel2Axis());
    if (axisIndex >= 0) {
        cmbWheelAxis->setCurrentIndex(axisIndex);
    }
    refreshBindingsTable();
    updateStatus(manager->statusText());
    updateMonitorButtons();
}

InputQuickerWidget::~InputQuickerWidget() = default;

QString InputQuickerWidget::selectedDevicePath() const
{
    return cmbDevice->currentData().toString();
}

void InputQuickerWidget::populateDevices()
{
    const QString selectedPath = selectedDevicePath().isEmpty() ? manager->devicePath() : selectedDevicePath();
    const QSignalBlocker blocker(cmbDevice);
    cmbDevice->clear();
    cmbDevice->addItem(QStringLiteral("自动选择（按能力评分）"), QString());

    const QList<InputQuickerManager::DeviceInfo> devices = manager->refreshDeviceList();
    for (const InputQuickerManager::DeviceInfo &device : devices) {
        QString label;
        if (device.recommended) {
            label = QStringLiteral("[推荐] %1  (%2)").arg(device.name, device.path);
        } else {
            label = QStringLiteral("%1  (%2)").arg(device.name, device.path);
        }
        if (!device.capsSummary.isEmpty()) {
            label += QStringLiteral(" [%1]").arg(device.capsSummary);
        }
        if (!device.accessible) {
            label += QStringLiteral(" (无权限)");
        }
        cmbDevice->addItem(label, device.path);
    }

    const int index = cmbDevice->findData(selectedPath);
    if (index >= 0) {
        cmbDevice->setCurrentIndex(index);
    } else if (!selectedPath.isEmpty()) {
        cmbDevice->addItem(QStringLiteral("手动设备: %1").arg(selectedPath), selectedPath);
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

void InputQuickerWidget::onApplyClicked()
{
    syncManagerFromUi();
    if (!manager->applySettings()) {
        appendLog(QStringLiteral("快捷助手启动失败，请检查 python3-evdev、xdotool 和 /dev/input 权限。"));
    }
}

void InputQuickerWidget::onStopClicked()
{
    manager->stopDaemon();
}

void InputQuickerWidget::onEnvCheckClicked()
{
    const InputQuickerEnvCheck check = manager->runEnvironmentCheck();
    const QString body = check.messages.join(QStringLiteral("\n"));
    if (check.allOk()) {
        QMessageBox::information(this, QStringLiteral("环境检测"), body);
    } else {
        QMessageBox::warning(this, QStringLiteral("环境检测"), body);
    }
}

void InputQuickerWidget::updateMonitorButtons()
{
    const bool running = manager->isMonitorRunning();
    btnStartMonitor->setEnabled(!running);
    btnStopMonitor->setEnabled(running);
}

void InputQuickerWidget::onStartMonitorClicked()
{
    if (manager->isDaemonRunning()) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("开始监听"),
            QStringLiteral("监听前建议停止 daemon，否则可能抢不到设备。是否继续？"));
        if (reply != QMessageBox::Yes) {
            return;
        }
        manager->stopDaemon();
    }

    syncManagerFromUi();
    tblMonitor->setRowCount(0);
    if (!manager->startInputMonitor(selectedDevicePath())) {
        const InputQuickerEnvCheck check = manager->runEnvironmentCheck();
        QString detail = check.messages.join(QStringLiteral("\n"));
        if (!check.inInputGroup || !check.inputReadable) {
            if (check.inputGroupConfigured && !check.inInputGroup) {
                detail += QStringLiteral(
                    "\n\n你已加入 input 组，但当前桌面会话尚未刷新。"
                    "请注销并重新登录（或重启）后再试。");
            } else if (!check.inputGroupConfigured) {
                detail += QStringLiteral(
                    "\n\n请将当前用户加入 input 组后重新登录：\n"
                    "sudo usermod -aG input $USER");
            }
        }
        QMessageBox::warning(this, QStringLiteral("无法开始监听"), detail);
    }
    updateMonitorButtons();
}

void InputQuickerWidget::onStopMonitorClicked()
{
    manager->stopInputMonitor();
    updateMonitorButtons();
}

void InputQuickerWidget::onMonitorStarted(const QString &devicePath, const QString &deviceName)
{
    appendLog(QStringLiteral("输入监听已启动: %1 (%2)").arg(deviceName, devicePath));
    updateMonitorButtons();
}

void InputQuickerWidget::onMonitorStopped()
{
    updateMonitorButtons();
}

void InputQuickerWidget::onMonitorEvent(const QJsonObject &event)
{
    const QString ts = event.value(QStringLiteral("ts")).toString();
    const QString label = event.value(QStringLiteral("label")).toString();
    const QJsonObject trigger = event.value(QStringLiteral("trigger")).toObject();
    const bool bindable = !trigger.isEmpty();

    if (tblMonitor->rowCount() >= kMonitorMaxRows) {
        tblMonitor->removeRow(0);
    }

    const int row = tblMonitor->rowCount();
    tblMonitor->insertRow(row);
    tblMonitor->setItem(row, 0, new QTableWidgetItem(ts));
    tblMonitor->setItem(row, 1, new QTableWidgetItem(label));

    auto *bindItem = new QTableWidgetItem(bindable ? QStringLiteral("是") : QStringLiteral("—"));
    if (bindable) {
        bindItem->setData(kMonitorTriggerRole, trigger);
        bindItem->setForeground(QBrush(QColor(QStringLiteral("#1565c0"))));
        bindItem->setToolTip(triggerDisplayText(trigger));
    }
    tblMonitor->setItem(row, 2, bindItem);
    tblMonitor->scrollToBottom();
}

void InputQuickerWidget::onMonitorRowActivated(int row, int column)
{
    Q_UNUSED(column)
    if (row < 0) {
        return;
    }
    QTableWidgetItem *bindItem = tblMonitor->item(row, 2);
    if (!bindItem || !bindItem->data(kMonitorTriggerRole).isValid()) {
        return;
    }
    const QJsonObject trigger = bindItem->data(kMonitorTriggerRole).toJsonObject();
    openBindingDialogWithTrigger(trigger, false);
}

void InputQuickerWidget::onMonitorRowDoubleClicked(int row, int column)
{
    onMonitorRowActivated(row, column);
}

bool InputQuickerWidget::openBindingDialogWithTrigger(const QJsonObject &trigger, bool editExisting)
{
    Q_UNUSED(editExisting)

    QuickerBindingDialog dialog(manager, this);
    QuickerBinding binding;
    binding.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    binding.enabled = true;
    binding.trigger = trigger;
    binding.action = QJsonObject{
        {QStringLiteral("type"), QStringLiteral("preset")},
        {QStringLiteral("preset"), QStringLiteral("workspace_prev")}
    };
    binding.name = triggerDisplayText(trigger);
    dialog.setBinding(binding);
    dialog.setDevicePath(selectedDevicePath());

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const QuickerBinding result = dialog.binding();
    if (result.name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("无效规则"), QStringLiteral("请填写规则名称。"));
        return false;
    }
    if (manager->isTriggerUsed(result.trigger)) {
        QMessageBox::warning(this, QStringLiteral("触发器冲突"), QStringLiteral("该触发器已被其他规则使用。"));
        return false;
    }

    manager->addBinding(result);
    return true;
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
}

void InputQuickerWidget::onImportDefaultsClicked()
{
    manager->setWheel2Axis(cmbWheelAxis->currentData().toString());
    manager->importDefaultWorkspaceBindings();
    appendLog(QStringLiteral("已导入默认工作区规则（跳过已存在的触发器）。"));
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
}

void InputQuickerWidget::updateStatus(const QString &status)
{
    lblStatus->setText(QStringLiteral("状态: %1").arg(status));
    if (status == QStringLiteral("运行中")) {
        lblStatus->setStyleSheet(QStringLiteral("font-weight: bold; color: #008000;"));
    } else if (status == QStringLiteral("未启用")) {
        lblStatus->setStyleSheet(QStringLiteral("font-weight: bold; color: #777;"));
    } else {
        lblStatus->setStyleSheet(QStringLiteral("font-weight: bold; color: #d84315;"));
    }
}

void InputQuickerWidget::appendLog(const QString &message)
{
    txtLog->append(QStringLiteral("[%1] %2")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                       .arg(message));
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
    manager->setEnabled(chkEnabled->isChecked());
    manager->setGrabDevice(chkGrabDevice->isChecked());
    manager->setDevicePath(selectedDevicePath());
    manager->setWheel2Axis(cmbWheelAxis->currentData().toString());
}
