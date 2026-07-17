#ifndef INPUTQUICKERWIDGET_H
#define INPUTQUICKERWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

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
    void onEnabledToggled(bool checked);
    void onDeviceChanged();
    void onAddBindingClicked();
    void onEditBindingClicked();
    void onDeleteBindingClicked();
    void onBindingEnabledToggled();
    void updateStatus(const QString &status);
    void pollStatus();

private:
    QString triggerDisplayText(const QJsonObject &trigger) const;
    QString actionDisplayText(const QJsonObject &action) const;
    void syncManagerFromUi();
    QString selectedDevicePath() const;
    bool applyChanges(bool showFailureDialog = true);
    void ensureDefaultsIfEmpty();
    void reportApplyFailure();

    InputQuickerManager *manager;
    QCheckBox *chkEnabled;
    QComboBox *cmbDevice;
    QPushButton *btnRefreshDevices;
    QLabel *lblStatus;
    QTableWidget *tblBindings;
    QPushButton *btnAddBinding;
    QTimer *statusTimer;
};

#endif // INPUTQUICKERWIDGET_H
