#include "keyboardsteprow.h"

#include "quickerkeyboardutils.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

KeyboardStepRow::KeyboardStepRow(int stepIndex, QWidget *parent)
    : QWidget(parent)
    , index(stepIndex)
    , stepLabel(new QLabel(this))
    , part1Combo(createCategorizedKeyboardCombo(this))
    , part2Combo(createCategorizedKeyboardCombo(this))
    , part3Combo(createCategorizedKeyboardCombo(this))
    , captureButton(new QPushButton(QStringLiteral("录制"), this))
    , removeButton(new QPushButton(QStringLiteral("删除"), this))
{
    stepLabel->setText(QStringLiteral("步骤 %1:").arg(index + 1));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stepLabel);
    layout->addWidget(part1Combo, 1);
    layout->addWidget(new QLabel(QStringLiteral("+"), this));
    layout->addWidget(part2Combo, 1);
    layout->addWidget(new QLabel(QStringLiteral("+"), this));
    layout->addWidget(part3Combo, 1);
    layout->addWidget(captureButton);
    layout->addWidget(removeButton);

    captureButton->setToolTip(QStringLiteral("按下键盘组合键，自动填入本步骤"));
    connect(captureButton, &QPushButton::clicked, this, &KeyboardStepRow::onCaptureClicked);
    connect(removeButton, &QPushButton::clicked, this, &KeyboardStepRow::onRemoveClicked);
    connectPartSignals(part1Combo);
    connectPartSignals(part2Combo);
    connectPartSignals(part3Combo);
}

int KeyboardStepRow::stepIndex() const
{
    return index;
}

void KeyboardStepRow::setStepIndex(int stepIndex)
{
    index = stepIndex;
    stepLabel->setText(QStringLiteral("步骤 %1:").arg(index + 1));
}

void KeyboardStepRow::setRemovable(bool removable)
{
    removeButton->setVisible(removable);
}

QString KeyboardStepRow::stepCombo() const
{
    return joinStepParts({keyboardPartText(part1Combo),
                          keyboardPartText(part2Combo),
                          keyboardPartText(part3Combo)});
}

void KeyboardStepRow::setStepCombo(const QString &step)
{
    const QStringList parts = splitStepParts(step);
    setKeyboardPartCombo(part1Combo, parts.value(0));
    setKeyboardPartCombo(part2Combo, parts.value(1));
    setKeyboardPartCombo(part3Combo, parts.value(2));
}

void KeyboardStepRow::setRecording(bool recording)
{
    captureButton->setText(recording ? QStringLiteral("录制中…") : QStringLiteral("录制"));
    captureButton->setEnabled(!recording);
}

void KeyboardStepRow::connectPartSignals(QComboBox *combo)
{
    connect(combo, &QComboBox::currentTextChanged, this, &KeyboardStepRow::onPartChanged);
    if (combo->lineEdit()) {
        connect(combo->lineEdit(), &QLineEdit::textChanged, this, &KeyboardStepRow::onPartChanged);
    }
}

void KeyboardStepRow::onPartChanged()
{
    emit stepChanged();
}

void KeyboardStepRow::onCaptureClicked()
{
    emit captureRequested(this);
}

void KeyboardStepRow::onRemoveClicked()
{
    emit removeRequested(this);
}
