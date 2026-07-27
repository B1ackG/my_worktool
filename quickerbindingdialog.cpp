#include "quickerbindingdialog.h"

#include "keyboardsteprow.h"
#include "quickerkeyboardutils.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QVBoxLayout>

QuickerBindingDialog::QuickerBindingDialog(InputQuickerManager *manager, QWidget *parent)
    : QDialog(parent)
    , manager(manager)
    , editMode(false)
    , recordingStepRow(nullptr)
{
    setWindowTitle(QStringLiteral("编辑快捷规则"));
    resize(620, 520);
    setFocusPolicy(Qt::StrongFocus);

    nameEdit = new QLineEdit(this);
    enabledCheck = new QCheckBox(QStringLiteral("启用此规则"), this);
    enabledCheck->setChecked(true);

    triggerTypeCombo = new QComboBox(this);
    triggerTypeCombo->addItem(QStringLiteral("鼠标按键"), QStringLiteral("mouse_button"));
    triggerTypeCombo->addItem(QStringLiteral("滚轮"), QStringLiteral("wheel"));

    buttonCodeCombo = new QComboBox(this);
    buttonCodeCombo->addItem(QStringLiteral("侧键 4 (BTN_SIDE)"), QStringLiteral("BTN_SIDE"));
    buttonCodeCombo->addItem(QStringLiteral("侧键 5 (BTN_EXTRA)"), QStringLiteral("BTN_EXTRA"));
    buttonCodeCombo->addItem(QStringLiteral("后退 (BTN_BACK)"), QStringLiteral("BTN_BACK"));
    buttonCodeCombo->addItem(QStringLiteral("前进 (BTN_FORWARD)"), QStringLiteral("BTN_FORWARD"));
    buttonCodeCombo->addItem(QStringLiteral("中键 (BTN_MIDDLE)"), QStringLiteral("BTN_MIDDLE"));

    wheelAxisCombo = new QComboBox(this);
    wheelAxisCombo->addItem(QStringLiteral("REL_HWHEEL (第二滚轮/横向)"), QStringLiteral("REL_HWHEEL"));
    wheelAxisCombo->addItem(QStringLiteral("REL_HWHEEL_HI_RES (高精度横向)"), QStringLiteral("REL_HWHEEL_HI_RES"));
    wheelAxisCombo->addItem(QStringLiteral("REL_WHEEL (普通滚轮)"), QStringLiteral("REL_WHEEL"));
    wheelAxisCombo->addItem(QStringLiteral("REL_WHEEL_HI_RES (高精度滚轮)"), QStringLiteral("REL_WHEEL_HI_RES"));

    wheelDirectionCombo = new QComboBox(this);
    wheelDirectionCombo->addItem(QStringLiteral("反向 (negative)"), QStringLiteral("negative"));
    wheelDirectionCombo->addItem(QStringLiteral("正向 (positive)"), QStringLiteral("positive"));

    captureButton = new QPushButton(QStringLiteral("录制触发器"), this);
    captureButton->setToolTip(QStringLiteral("也可在主页「输入检测」区点击事件来绑定触发器"));
    triggerSummaryLabel = new QLabel(this);
    triggerSummaryLabel->setStyleSheet(QStringLiteral("color: #555;"));

    actionTypeCombo = new QComboBox(this);
    actionTypeCombo->addItem(QStringLiteral("内置预设"), QStringLiteral("preset"));
    actionTypeCombo->addItem(QStringLiteral("键盘快捷键"), QStringLiteral("keyboard"));
    actionTypeCombo->addItem(QStringLiteral("Shell 命令"), QStringLiteral("command"));

    commandEdit = new QLineEdit(this);
    commandEdit->setPlaceholderText(QStringLiteral("例如: gnome-terminal 或 xdg-open ."));

    presetCombo = new QComboBox(this);
    presetCombo->addItem(QStringLiteral("上一工作区"), QStringLiteral("workspace_prev"));
    presetCombo->addItem(QStringLiteral("下一工作区"), QStringLiteral("workspace_next"));
    presetCombo->addItem(QStringLiteral("音量增加"), QStringLiteral("volume_up"));
    presetCombo->addItem(QStringLiteral("音量减少"), QStringLiteral("volume_down"));
    presetCombo->addItem(QStringLiteral("静音"), QStringLiteral("mute"));
    presetCombo->addItem(QStringLiteral("下一曲"), QStringLiteral("media_next"));
    presetCombo->addItem(QStringLiteral("上一曲"), QStringLiteral("media_prev"));
    presetCombo->addItem(QStringLiteral("Super 键 (系统键)"), QStringLiteral("super"));

    keyboardWidget = new QWidget(this);
    auto *keyboardLayout = new QVBoxLayout(keyboardWidget);
    keyboardLayout->setContentsMargins(0, 0, 0, 0);
    keyboardStepsLayout = new QVBoxLayout();
    keyboardLayout->addLayout(keyboardStepsLayout);

    addKeyboardStepButton = new QPushButton(QStringLiteral("+ 添加步骤"), keyboardWidget);
    keyboardLayout->addWidget(addKeyboardStepButton);

    keyboardComboPreview = new QLabel(keyboardWidget);
    keyboardComboPreview->setStyleSheet(QStringLiteral("color: #555;"));
    keyboardLayout->addWidget(keyboardComboPreview);

    keyboardRecordingLabel = new QLabel(keyboardWidget);
    keyboardRecordingLabel->setStyleSheet(QStringLiteral("color: #0066cc;"));
    keyboardRecordingLabel->hide();
    keyboardLayout->addWidget(keyboardRecordingLabel);

    keyboardValidationLabel = new QLabel(keyboardWidget);
    keyboardValidationLabel->setWordWrap(true);
    keyboardLayout->addWidget(keyboardValidationLabel);

    testKeyboardButton = new QPushButton(QStringLiteral("试按"), keyboardWidget);
    keyboardLayout->addWidget(testKeyboardButton);

    commandWidget = new QWidget(this);
    QFormLayout *commandLayout = new QFormLayout(commandWidget);
    commandLayout->addRow(QStringLiteral("命令:"), commandEdit);

    presetWidget = new QWidget(this);
    QFormLayout *presetLayout = new QFormLayout(presetWidget);
    presetLayout->addRow(QStringLiteral("预设:"), presetCombo);

    QGroupBox *triggerGroup = new QGroupBox(QStringLiteral("触发器"), this);
    QFormLayout *triggerLayout = new QFormLayout(triggerGroup);
    triggerLayout->addRow(QStringLiteral("类型:"), triggerTypeCombo);
    triggerLayout->addRow(QStringLiteral("鼠标键:"), buttonCodeCombo);
    triggerLayout->addRow(QStringLiteral("滚轮轴:"), wheelAxisCombo);
    triggerLayout->addRow(QStringLiteral("滚轮方向:"), wheelDirectionCombo);
    QHBoxLayout *captureLayout = new QHBoxLayout();
    captureLayout->addWidget(captureButton);
    captureLayout->addWidget(triggerSummaryLabel, 1);
    triggerLayout->addRow(captureLayout);

    QGroupBox *actionGroup = new QGroupBox(QStringLiteral("动作"), this);
    QVBoxLayout *actionVBox = new QVBoxLayout(actionGroup);
    actionVBox->addWidget(actionTypeCombo);
    actionVBox->addWidget(keyboardWidget);
    actionVBox->addWidget(commandWidget);
    actionVBox->addWidget(presetWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *topLayout = new QFormLayout();
    topLayout->addRow(QStringLiteral("名称:"), nameEdit);
    topLayout->addRow(enabledCheck);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(triggerGroup);
    mainLayout->addWidget(actionGroup);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton(QStringLiteral("确定"), this);
    QPushButton *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(triggerTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QuickerBindingDialog::onTriggerTypeChanged);
    connect(actionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QuickerBindingDialog::onActionTypeChanged);
    connect(captureButton, &QPushButton::clicked, this, &QuickerBindingDialog::onCaptureTriggerClicked);
    connect(addKeyboardStepButton, &QPushButton::clicked, this, &QuickerBindingDialog::onAddKeyboardStepClicked);
    connect(testKeyboardButton, &QPushButton::clicked, this, &QuickerBindingDialog::onTestKeyboardClicked);

    addKeyboardStepRow();
    updateKeyboardPreview();
    onTriggerTypeChanged(triggerTypeCombo->currentIndex());
    onActionTypeChanged(actionTypeCombo->currentIndex());
}

KeyboardStepRow *QuickerBindingDialog::addKeyboardStepRow(const QString &step)
{
    auto *row = new KeyboardStepRow(keyboardStepRows.size(), keyboardWidget);
    row->setRemovable(!keyboardStepRows.isEmpty());
    row->setStepCombo(step);
    keyboardStepsLayout->addWidget(row);
    keyboardStepRows.append(row);

    connect(row, &KeyboardStepRow::stepChanged, this, &QuickerBindingDialog::onKeyboardStepChanged);
    connect(row, &KeyboardStepRow::captureRequested, this, &QuickerBindingDialog::onCaptureKeyboardStepRequested);
    connect(row, &KeyboardStepRow::removeRequested, this, &QuickerBindingDialog::onRemoveKeyboardStepRequested);

    reindexKeyboardSteps();
    return row;
}

void QuickerBindingDialog::clearKeyboardSteps()
{
    stopKeyboardRecording();
    for (KeyboardStepRow *row : keyboardStepRows) {
        keyboardStepsLayout->removeWidget(row);
        row->deleteLater();
    }
    keyboardStepRows.clear();
}

void QuickerBindingDialog::reindexKeyboardSteps()
{
    for (int i = 0; i < keyboardStepRows.size(); ++i) {
        keyboardStepRows.at(i)->setStepIndex(i);
        keyboardStepRows.at(i)->setRemovable(i > 0);
    }
}

QString QuickerBindingDialog::combinedKeyboardCombo() const
{
    QStringList steps;
    for (const KeyboardStepRow *row : keyboardStepRows) {
        const QString step = row->stepCombo();
        if (!step.isEmpty()) {
            steps.append(step);
        }
    }
    return joinComboSteps(steps);
}

void QuickerBindingDialog::setKeyboardComboParts(const QString &combo)
{
    clearKeyboardSteps();
    const QStringList steps = splitComboSteps(combo);
    if (steps.isEmpty()) {
        addKeyboardStepRow();
    } else {
        for (const QString &step : steps) {
            addKeyboardStepRow(step);
        }
    }
    updateKeyboardPreview();
}

void QuickerBindingDialog::updateKeyboardPreview()
{
    const QString combo = combinedKeyboardCombo();
    if (combo.isEmpty()) {
        keyboardComboPreview->setText(QStringLiteral("预览: （未设置）"));
    } else {
        keyboardComboPreview->setText(QStringLiteral("预览: %1").arg(formatComboPreview(combo)));
    }
    updateKeyboardValidation();
}

void QuickerBindingDialog::updateKeyboardValidation()
{
    const QString combo = combinedKeyboardCombo();
    const QStringList unknown = validateComboParts(combo);
    if (unknown.isEmpty()) {
        keyboardValidationLabel->clear();
        return;
    }
    keyboardValidationLabel->setStyleSheet(QStringLiteral("color: #b8860b;"));
    keyboardValidationLabel->setText(
        QStringLiteral("校验: 未知键名 %1（仍可保存，请确认 xdotool 是否支持）")
            .arg(unknown.join(QStringLiteral(", "))));
}

void QuickerBindingDialog::stopKeyboardRecording()
{
    if (recordingStepRow) {
        recordingStepRow->setRecording(false);
        recordingStepRow = nullptr;
    }
    keyboardRecordingLabel->hide();
    releaseKeyboard();
}

void QuickerBindingDialog::startKeyboardRecording(KeyboardStepRow *row)
{
    stopKeyboardRecording();
    recordingStepRow = row;
    row->setRecording(true);
    keyboardRecordingLabel->setText(
        QStringLiteral("正在录制步骤 %1，请按键…（Esc 取消）").arg(row->stepIndex() + 1));
    keyboardRecordingLabel->show();
    setFocus();
    grabKeyboard();
}

void QuickerBindingDialog::keyPressEvent(QKeyEvent *event)
{
    if (!recordingStepRow) {
        QDialog::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        stopKeyboardRecording();
        event->accept();
        return;
    }

    const QString step = qtKeyEventToXdotoolStep(event);
    if (step.isEmpty()) {
        event->accept();
        return;
    }

    recordingStepRow->setStepCombo(step);
    stopKeyboardRecording();
    updateKeyboardPreview();
    event->accept();
}

void QuickerBindingDialog::accept()
{
    stopKeyboardRecording();

    const QString actionType = actionTypeCombo->currentData().toString();
    if (actionType == QStringLiteral("keyboard")) {
        const QString combo = combinedKeyboardCombo();
        if (combo.isEmpty()) {
            QMessageBox::warning(this,
                                 QStringLiteral("组合键无效"),
                                 QStringLiteral("请至少设置一个键盘组合步骤。"));
            return;
        }

        const QStringList unknown = validateComboParts(combo);
        if (!unknown.isEmpty()) {
            const auto reply = QMessageBox::question(
                this,
                QStringLiteral("未知键名"),
                QStringLiteral("以下键名不在常用列表中：%1\n是否仍要保存？")
                    .arg(unknown.join(QStringLiteral(", "))));
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
    }
    QDialog::accept();
}

void QuickerBindingDialog::onAddKeyboardStepClicked()
{
    addKeyboardStepRow();
    updateKeyboardPreview();
}

void QuickerBindingDialog::onTestKeyboardClicked()
{
#ifdef Q_OS_WIN
    QMessageBox::information(this,
                             QStringLiteral("试按"),
                             QStringLiteral("试按依赖 xdotool，仅在 Linux 下可用。"));
    return;
#else
    const QString combo = combinedKeyboardCombo();
    if (combo.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("试按"), QStringLiteral("请先设置组合键。"));
        return;
    }

    QProcess process;
    process.start(QStringLiteral("xdotool"), {QStringLiteral("key"), combo});
    if (!process.waitForFinished(3000)) {
        process.kill();
        QMessageBox::warning(this,
                             QStringLiteral("试按失败"),
                             QStringLiteral("xdotool 执行超时。请确认已安装 xdotool，且在 X11 下试按有效（Wayland 可能无效）。"));
        return;
    }

    if (process.exitCode() != 0) {
        const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        QMessageBox::warning(
            this,
            QStringLiteral("试按失败"),
            stderrText.isEmpty()
                ? QStringLiteral("xdotool 返回错误。请确认已安装: sudo apt install xdotool")
                : stderrText);
    }
#endif
}

void QuickerBindingDialog::onKeyboardStepChanged()
{
    updateKeyboardPreview();
}

void QuickerBindingDialog::onCaptureKeyboardStepRequested(KeyboardStepRow *row)
{
    if (!row) {
        return;
    }
    startKeyboardRecording(row);
}

void QuickerBindingDialog::onRemoveKeyboardStepRequested(KeyboardStepRow *row)
{
    if (!row || keyboardStepRows.size() <= 1) {
        return;
    }
    if (recordingStepRow == row) {
        stopKeyboardRecording();
    }

    keyboardStepsLayout->removeWidget(row);
    keyboardStepRows.removeOne(row);
    row->deleteLater();
    reindexKeyboardSteps();
    updateKeyboardPreview();
}

void QuickerBindingDialog::setDevicePath(const QString &devicePath)
{
    captureDevicePath = devicePath;
}

void QuickerBindingDialog::setBinding(const QuickerBinding &binding)
{
    currentBinding = binding;
    editMode = !binding.id.isEmpty();
    nameEdit->setText(binding.name);
    enabledCheck->setChecked(binding.enabled);
    applyTriggerToUi(binding.trigger);

    const QString actionType = binding.action.value(QStringLiteral("type")).toString();
    const int actionIndex = actionTypeCombo->findData(actionType);
    if (actionIndex >= 0) {
        actionTypeCombo->setCurrentIndex(actionIndex);
    }

    setKeyboardComboParts(binding.action.value(QStringLiteral("combo")).toString());
    commandEdit->setText(binding.action.value(QStringLiteral("command")).toString());
    const int presetIndex = presetCombo->findData(binding.action.value(QStringLiteral("preset")).toString());
    if (presetIndex >= 0) {
        presetCombo->setCurrentIndex(presetIndex);
    }

    onTriggerTypeChanged(triggerTypeCombo->currentIndex());
    onActionTypeChanged(actionTypeCombo->currentIndex());
}

void QuickerBindingDialog::ensureWheelAxisInCombo(const QString &axis)
{
    if (axis.isEmpty() || wheelAxisCombo->findData(axis) >= 0) {
        return;
    }
    wheelAxisCombo->addItem(QStringLiteral("%1 (检测到)").arg(axis), axis);
}

void QuickerBindingDialog::applyTriggerToUi(const QJsonObject &trigger)
{
    const QString triggerType = trigger.value(QStringLiteral("type")).toString();
    const int triggerIndex = triggerTypeCombo->findData(triggerType);
    if (triggerIndex >= 0) {
        triggerTypeCombo->setCurrentIndex(triggerIndex);
    }

    const int buttonIndex = buttonCodeCombo->findData(trigger.value(QStringLiteral("code")).toString());
    if (buttonIndex >= 0) {
        buttonCodeCombo->setCurrentIndex(buttonIndex);
    }

    const QString axis = trigger.value(QStringLiteral("axis")).toString();
    ensureWheelAxisInCombo(axis);
    const int axisIndex = wheelAxisCombo->findData(axis);
    if (axisIndex >= 0) {
        wheelAxisCombo->setCurrentIndex(axisIndex);
    }

    const int directionIndex = wheelDirectionCombo->findData(trigger.value(QStringLiteral("direction")).toString());
    if (directionIndex >= 0) {
        wheelDirectionCombo->setCurrentIndex(directionIndex);
    }

    triggerSummaryLabel->setText(triggerSummary(trigger));
}

QuickerBinding QuickerBindingDialog::binding() const
{
    QuickerBinding result = currentBinding;
    result.name = nameEdit->text().trimmed();
    result.enabled = enabledCheck->isChecked();
    result.trigger = currentTrigger();

    const QString actionType = actionTypeCombo->currentData().toString();
    QJsonObject action;
    action.insert(QStringLiteral("type"), actionType);
    if (actionType == QStringLiteral("keyboard")) {
        action.insert(QStringLiteral("combo"), combinedKeyboardCombo());
    } else if (actionType == QStringLiteral("command")) {
        action.insert(QStringLiteral("command"), commandEdit->text().trimmed());
    } else {
        action.insert(QStringLiteral("preset"), presetCombo->currentData().toString());
    }
    result.action = action;
    return result;
}

bool QuickerBindingDialog::isEditMode() const
{
    return editMode;
}

void QuickerBindingDialog::onActionTypeChanged(int index)
{
    Q_UNUSED(index)
    const QString actionType = actionTypeCombo->currentData().toString();
    keyboardWidget->setVisible(actionType == QStringLiteral("keyboard"));
    commandWidget->setVisible(actionType == QStringLiteral("command"));
    presetWidget->setVisible(actionType == QStringLiteral("preset"));
}

void QuickerBindingDialog::onTriggerTypeChanged(int index)
{
    Q_UNUSED(index)
    const QString triggerType = triggerTypeCombo->currentData().toString();
    const bool isButton = triggerType == QStringLiteral("mouse_button");
    buttonCodeCombo->setVisible(isButton);
    wheelAxisCombo->setVisible(!isButton);
    wheelDirectionCombo->setVisible(!isButton);
    triggerSummaryLabel->setText(triggerSummary(currentTrigger()));
}

void QuickerBindingDialog::onCaptureTriggerClicked()
{
    if (!manager) {
        return;
    }

    QString devicePath = captureDevicePath;
    if (devicePath.isEmpty()) {
        devicePath = manager->devicePath();
    }

    if (manager->isMonitorRunning()) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("录制触发器"),
            QStringLiteral("录制前需要暂时停止输入监听，是否继续？"));
        if (reply != QMessageBox::Yes) {
            return;
        }
        manager->stopInputMonitor();
    }

    if (manager->isDaemonRunning()) {
        const auto reply = QMessageBox::question(
            this,
            QStringLiteral("录制触发器"),
            QStringLiteral("录制前需要暂时停止 daemon，是否继续？"));
        if (reply != QMessageBox::Yes) {
            return;
        }
        manager->stopDaemon();
    }

    captureButton->setEnabled(false);
    captureButton->setText(QStringLiteral("请按下鼠标键或滚动滚轮..."));
    QJsonObject trigger;
    QString error;
    const bool ok = manager->captureTrigger(devicePath, 10000, trigger, error);
    captureButton->setEnabled(true);
    captureButton->setText(QStringLiteral("录制触发器"));

    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("录制失败"), error);
        return;
    }

    applyTriggerToUi(trigger);
    onTriggerTypeChanged(triggerTypeCombo->currentIndex());
}

QJsonObject QuickerBindingDialog::currentTrigger() const
{
    const QString triggerType = triggerTypeCombo->currentData().toString();
    if (triggerType == QStringLiteral("mouse_button")) {
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("mouse_button")},
            {QStringLiteral("code"), buttonCodeCombo->currentData().toString()}
        };
    }
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("wheel")},
        {QStringLiteral("axis"), wheelAxisCombo->currentData().toString()},
        {QStringLiteral("direction"), wheelDirectionCombo->currentData().toString()}
    };
}

QString QuickerBindingDialog::triggerSummary(const QJsonObject &trigger) const
{
    const QString type = trigger.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("mouse_button")) {
        return QStringLiteral("当前: 鼠标键 %1").arg(trigger.value(QStringLiteral("code")).toString());
    }
    if (type == QStringLiteral("wheel")) {
        const QString direction = trigger.value(QStringLiteral("direction")).toString();
        const QString dirText = direction == QStringLiteral("positive") ? QStringLiteral("正向")
                                                                        : QStringLiteral("反向");
        return QStringLiteral("当前: 滚轮 %1 %2")
            .arg(trigger.value(QStringLiteral("axis")).toString(), dirText);
    }
    return QStringLiteral("当前: 未设置");
}
