#ifndef KEYBOARDSTEPROW_H
#define KEYBOARDSTEPROW_H

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

class KeyboardStepRow : public QWidget
{
    Q_OBJECT

public:
    explicit KeyboardStepRow(int stepIndex, QWidget *parent = nullptr);

    int stepIndex() const;
    void setStepIndex(int index);
    void setRemovable(bool removable);

    QString stepCombo() const;
    void setStepCombo(const QString &step);
    void setRecording(bool recording);

signals:
    void stepChanged();
    void captureRequested(KeyboardStepRow *row);
    void removeRequested(KeyboardStepRow *row);

private slots:
    void onPartChanged();
    void onCaptureClicked();
    void onRemoveClicked();

private:
    void connectPartSignals(QComboBox *combo);

    int index;
    QLabel *stepLabel;
    QComboBox *part1Combo;
    QComboBox *part2Combo;
    QComboBox *part3Combo;
    QPushButton *captureButton;
    QPushButton *removeButton;
};

#endif // KEYBOARDSTEPROW_H
