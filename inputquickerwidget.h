#ifndef INPUTQUICKERWIDGET_H
#define INPUTQUICKERWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QJsonObject>

#include "inputquickermanager.h"

class InputQuickerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit InputQuickerWidget(QWidget *parent = nullptr);
    ~InputQuickerWidget() override;

private slots:
    void populateDevices();
    void refreshBindingsTable();
    void onApplyClicked();
    void onStopClicked();
    void onAddBindingClicked();
    void onImportDefaultsClicked();
    void onEditBindingClicked();
    void onDeleteBindingClicked();
    void onBindingEnabledToggled();
    void onEnvCheckClicked();
    void onStartMonitorClicked();
    void onStopMonitorClicked();
    void onMonitorEvent(const QJsonObject &event);
    void onMonitorStarted(const QString &devicePath, const QString &deviceName);
    void onMonitorStopped();
    void onMonitorRowActivated(int row, int column);
    void onMonitorRowDoubleClicked(int row, int column);
    void updateStatus(const QString &status);
    void appendLog(const QString &message);

private:
    QString triggerDisplayText(const QJsonObject &trigger) const;
    QString actionDisplayText(const QJsonObject &action) const;
    void syncManagerFromUi();
    QString selectedDevicePath() const;
    bool openBindingDialogWithTrigger(const QJsonObject &trigger, bool editExisting = false);
    void updateMonitorButtons();

    InputQuickerManager *manager;
    QCheckBox *chkGrabDevice;
    QCheckBox *chkEnabled;
    QComboBox *cmbDevice;
    QComboBox *cmbWheelAxis;
    QPushButton *btnRefreshDevices;
    QPushButton *btnEnvCheck;
    QPushButton *btnStartMonitor;
    QPushButton *btnStopMonitor;
    QPushButton *btnApply;
    QPushButton *btnStop;
    QLabel *lblStatus;
    QTableWidget *tblMonitor;
    QTableWidget *tblBindings;
    QPushButton *btnAddBinding;
    QPushButton *btnImportDefaults;
    QTextEdit *txtLog;
};

#endif // INPUTQUICKERWIDGET_H
