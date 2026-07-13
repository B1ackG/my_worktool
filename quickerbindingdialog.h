#ifndef QUICKERBINDINGDIALOG_H
#define QUICKERBINDINGDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "inputquickermanager.h"

class KeyboardStepRow;

class QuickerBindingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickerBindingDialog(InputQuickerManager *manager, QWidget *parent = nullptr);

    void setBinding(const QuickerBinding &binding);
    void setDevicePath(const QString &devicePath);
    QuickerBinding binding() const;
    bool isEditMode() const;

public slots:
    void accept() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onActionTypeChanged(int index);
    void onTriggerTypeChanged(int index);
    void onCaptureTriggerClicked();
    void onAddKeyboardStepClicked();
    void onTestKeyboardClicked();
    void onKeyboardStepChanged();
    void onCaptureKeyboardStepRequested(KeyboardStepRow *row);
    void onRemoveKeyboardStepRequested(KeyboardStepRow *row);
    void updateKeyboardPreview();

private:
    QJsonObject currentTrigger() const;
    QString triggerSummary(const QJsonObject &trigger) const;
    void applyTriggerToUi(const QJsonObject &trigger);
    void ensureWheelAxisInCombo(const QString &axis);
    QString combinedKeyboardCombo() const;
    void setKeyboardComboParts(const QString &combo);
    void clearKeyboardSteps();
    KeyboardStepRow *addKeyboardStepRow(const QString &step = QString());
    void reindexKeyboardSteps();
    void stopKeyboardRecording();
    void startKeyboardRecording(KeyboardStepRow *row);
    void updateKeyboardValidation();

    InputQuickerManager *manager;
    QString captureDevicePath;
    QuickerBinding currentBinding;
    bool editMode;

    QLineEdit *nameEdit;
    QCheckBox *enabledCheck;
    QComboBox *triggerTypeCombo;
    QComboBox *buttonCodeCombo;
    QComboBox *wheelAxisCombo;
    QComboBox *wheelDirectionCombo;
    QPushButton *captureButton;
    QLabel *triggerSummaryLabel;

    QComboBox *actionTypeCombo;
    QWidget *keyboardWidget;
    QVBoxLayout *keyboardStepsLayout;
    QList<KeyboardStepRow *> keyboardStepRows;
    QPushButton *addKeyboardStepButton;
    QLabel *keyboardComboPreview;
    QLabel *keyboardRecordingLabel;
    QLabel *keyboardValidationLabel;
    QPushButton *testKeyboardButton;
    KeyboardStepRow *recordingStepRow;
    QLineEdit *commandEdit;
    QComboBox *presetCombo;
    QWidget *commandWidget;
    QWidget *presetWidget;
};

#endif // QUICKERBINDINGDIALOG_H
