#ifndef LIFEASSISTANTWIDGET_H
#define LIFEASSISTANTWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTimeEdit>
#include <QSettings>
#include <QComboBox>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QTableWidget>
#include <QCheckBox>
#include <QCalendarWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QElapsedTimer>
#include <QTabWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QProcess>
#include "capturewindow.h"
#include "ruledialog.h"

class LifeAssistantWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LifeAssistantWidget(QWidget *parent = nullptr);
    ~LifeAssistantWidget();
    bool isWorkdayToday() const { return isCurrentDayWorkday; }

private slots:
    void updateFocusWindow();
    void onFocusProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onFocusProcessTimeout();
    void checkTasks();
    void addTask();
    void saveSettings();
    void loadSettings();
    void onHolidayDataReceived(QNetworkReply *reply);
    void updateHolidayColors();
    void startPickWindow();
    void addShieldRule();
    void editShieldRule();
    void removeShieldRule();
    void showRuleContextMenu(const QPoint &pos);
    void showTaskContextMenu(const QPoint &pos);
    void editTaskAt(int row);
    void onProcessCaptured(const QString &proc);
    void toggleFatigue(bool enabled);
    void onSidebarChanged(int index);
    void checkWorkDay();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QListWidget *sidebarList = nullptr;
    QStackedWidget *stackedWidget = nullptr;

    QLabel *focusInfoLabel = nullptr;
    QLabel *processNameLabel = nullptr;
    QTableWidget *taskTable = nullptr;

    QListWidget *ruleListWidget = nullptr;
    QList<ShieldRule> rules;

    QTableWidget *workTable = nullptr;
    QComboBox *provinceCombo = nullptr;
    QLineEdit *companySearchEdit = nullptr;

    QCalendarWidget *calendar = nullptr;
    QList<QDate> holidays;

    QCheckBox *fatigueCheck = nullptr;
    QSpinBox *workSpin = nullptr;
    QSpinBox *restSpin = nullptr;
    QLabel *workDayLabel = nullptr;
    QTimer *focusTimer = nullptr;
    QTimer *checkTimer = nullptr;
    QTimer *focusTimeoutTimer = nullptr;
    QProcess *focusProcess = nullptr;

    QPushButton *pickBtn = nullptr;
    bool isPicking = false;

    QElapsedTimer useTimer;
    bool isBlockingState = false;

    CaptureWindow *captureWin = nullptr;
    QNetworkAccessManager *networkManager = nullptr;
    bool isCurrentDayWorkday = true;

    QString lastFocusTitle;
    QString lastFocusProc;

    QString getActiveWindowTitle();
    QString getActiveWindowProcessName();
    void requestFocusSnapshot();
    void applyFocusSnapshot(const QString &title, const QString &proc);
    void shutdownSystem();
    void executeBlockAction(const QString &title, const QString &proc);
    bool checkSingleRule(const ShieldRule &rule, const QString &title, const QString &proc);
    bool isBrowser(const QString &proc);
    void updateRuleListUI();
    bool isSelfProcess(const QString &proc) const;
};

#endif // LIFEASSISTANTWIDGET_H
