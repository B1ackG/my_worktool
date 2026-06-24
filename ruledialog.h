#ifndef RULEDIALOG_H
#define RULEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QTimeEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include <QRadioButton>
#include <QSpinBox>
#include <QDateTime>
#include <QListWidget>
#include <QGroupBox>
#include <QTextEdit>

struct ShieldRule {
    QString name;
    int index;
    bool isWhiteList; // true: 白名单, false: 黑名单
    QString remark;
    int starLevel;
    QString createDate;
    bool enabled;
    
    struct TimeSlot {
        bool enabled;
        QString type; // "每天" etc
        QTime start;
        QTime end;
    };
    QList<TimeSlot> timeSlots;
    
    // 细分规则 (带有启用状态)
    struct RuleEntry {
        QString text;
        bool enabled;
    };
    QList<RuleEntry> entryTitles;
    QList<RuleEntry> entryWebs;
    QList<RuleEntry> entrySofts;
    QList<RuleEntry> entryApps;
    QList<RuleEntry> entryComms;

    // 为了兼容旧逻辑，保留 stringlist 接口供检测使用
    QStringList enabledTitles() const;
    QStringList enabledWebs() const;
    QStringList enabledSoftwares() const;
    QStringList enabledApps() const;
    QStringList enabledCommTools() const;
};

class RuleDialog : public QDialog {
    Q_OBJECT
public:
    explicit RuleDialog(QWidget *parent = nullptr);
    void setRule(const ShieldRule &rule);
    ShieldRule getRule() const;

private slots:
    void addTimeSlot();
    void addRuleEntry();
    void batchInput();

private:
    // 上方组信息
    QLineEdit *nameInput;
    QSpinBox *indexSpin;
    QRadioButton *whiteRadio;
    QRadioButton *blackRadio;
    QLineEdit *remarkInput;
    QSpinBox *starSpin;
    QLabel *createDateLabel;

    // 中部左侧：规则标签页
    QTabWidget *ruleTabs;
    QListWidget *titleList;
    QListWidget *webList;
    QListWidget *softList;
    QListWidget *appList;
    QListWidget *commList;
    QLineEdit *ruleEntryInput;

    // 中部右侧：时间段
    QComboBox *timeTypeCombo;
    QTimeEdit *startTimeEdit;
    QTimeEdit *endTimeEdit;
    QTableWidget *timeTable;
};

#endif // RULEDIALOG_H
