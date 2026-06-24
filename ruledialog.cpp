#include "ruledialog.h"
#include <QHeaderView>
#include <QCheckBox>
#include <QTimeEdit>
#include <QComboBox>
#include <QMessageBox>

#include <QInputDialog>
#include <QDateTime>

RuleDialog::RuleDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("编辑");
    setModal(true);
    setMinimumSize(1000, 700);
    
    QVBoxLayout *mainV = new QVBoxLayout(this);
    
    // 1. 上方组信息行
    QGroupBox *groupInfo = new QGroupBox("组信息");
    QHBoxLayout *topH = new QHBoxLayout(groupInfo);
    
    nameInput = new QLineEdit(); nameInput->setPlaceholderText("学习相关");
    indexSpin = new QSpinBox(); indexSpin->setRange(1, 999); indexSpin->setValue(1);
    whiteRadio = new QRadioButton("白名单");
    blackRadio = new QRadioButton("黑名单");
    blackRadio->setChecked(true);
    remarkInput = new QLineEdit(); remarkInput->setPlaceholderText("防止黑名单误杀");
    starSpin = new QSpinBox(); starSpin->setRange(1, 5); starSpin->setSuffix(" 星");
    createDateLabel = new QLabel(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));

    topH->addWidget(new QLabel("组名:")); topH->addWidget(nameInput);
    topH->addWidget(new QLabel("序号:")); topH->addWidget(indexSpin);
    topH->addWidget(whiteRadio); topH->addWidget(blackRadio);
    topH->addWidget(new QLabel("备注:")); topH->addWidget(remarkInput);
    topH->addWidget(new QLabel("等级:")); topH->addWidget(starSpin);
    topH->addWidget(new QLabel("新建:")); topH->addWidget(createDateLabel);
    
    mainV->addWidget(groupInfo);

    // 2. 中部左右分割
    QHBoxLayout *midH = new QHBoxLayout();
    
    // 左侧：规则列表区
    QGroupBox *leftGroup = new QGroupBox("规则列表区");
    QVBoxLayout *leftV = new QVBoxLayout(leftGroup);
    
    ruleTabs = new QTabWidget();
    titleList = new QListWidget();
    webList = new QListWidget();
    softList = new QListWidget();
    appList = new QListWidget();
    commList = new QListWidget();
    
    ruleTabs->addTab(titleList, "窗口标题");
    ruleTabs->addTab(webList, "网站");
    ruleTabs->addTab(softList, "软件");
    ruleTabs->addTab(appList, "应用");
    ruleTabs->addTab(commList, "通讯工具");
    
    QHBoxLayout *inputH = new QHBoxLayout();
    ruleEntryInput = new QLineEdit();
    ruleEntryInput->setPlaceholderText("输入单条规则后回车...");
    QPushButton *batchBtn = new QPushButton("批量输入");
    inputH->addWidget(ruleEntryInput);
    inputH->addWidget(batchBtn);
    
    leftV->addWidget(ruleTabs);
    leftV->addLayout(inputH);
    midH->addWidget(leftGroup, 1);

    // 右侧：时间段配置区
    QGroupBox *rightGroup = new QGroupBox("时间段配置区");
    QVBoxLayout *rightV = new QVBoxLayout(rightGroup);
    
    QHBoxLayout *baseTimeH = new QHBoxLayout();
    timeTypeCombo = new QComboBox();
    timeTypeCombo->addItems({"每天", "工作日", "休息日"});
    startTimeEdit = new QTimeEdit(QTime(0, 0));
    endTimeEdit = new QTimeEdit(QTime(23, 59));
    QPushButton *addT = new QPushButton("+");
    baseTimeH->addWidget(timeTypeCombo);
    baseTimeH->addWidget(startTimeEdit);
    baseTimeH->addWidget(new QLabel("到"));
    baseTimeH->addWidget(endTimeEdit);
    baseTimeH->addWidget(addT);
    
    timeTable = new QTableWidget(0, 4);
    timeTable->setHorizontalHeaderLabels({"段类型", "开始", "结束", "删除"});
    timeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    rightV->addLayout(baseTimeH);
    rightV->addWidget(timeTable);
    midH->addWidget(rightGroup, 1);
    
    mainV->addLayout(midH);

    // 3. 底部操作区
    QHBoxLayout *bottomH = new QHBoxLayout();
    QPushButton *saveBtn = new QPushButton("保存");
    saveBtn->setStyleSheet("background-color: #4CAF50; color: white; min-width: 100px; padding: 5px;");
    QPushButton *cancelBtn = new QPushButton("取消");
    bottomH->addStretch();
    bottomH->addWidget(saveBtn);
    bottomH->addWidget(cancelBtn);
    mainV->addLayout(bottomH);

    // 信号绑定
    connect(addT, &QPushButton::clicked, this, &RuleDialog::addTimeSlot);
    connect(ruleEntryInput, &QLineEdit::returnPressed, this, &RuleDialog::addRuleEntry);
    connect(batchBtn, &QPushButton::clicked, this, &RuleDialog::batchInput);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void RuleDialog::addTimeSlot() {
    int r = timeTable->rowCount();
    timeTable->insertRow(r);
    timeTable->setItem(r, 0, new QTableWidgetItem(timeTypeCombo->currentText()));
    timeTable->setItem(r, 1, new QTableWidgetItem(startTimeEdit->time().toString("HH:mm")));
    timeTable->setItem(r, 2, new QTableWidgetItem(endTimeEdit->time().toString("HH:mm")));
    QPushButton *del = new QPushButton("删除");
    connect(del, &QPushButton::clicked, [this](){ timeTable->removeRow(timeTable->currentRow()); });
    timeTable->setCellWidget(r, 3, del);
}

void RuleDialog::addRuleEntry() {
    QString t = ruleEntryInput->text().trimmed();
    if(t.isEmpty()) return;
    QListWidget *list = qobject_cast<QListWidget*>(ruleTabs->currentWidget());
    if(list) {
        QListWidgetItem *item = new QListWidgetItem(t);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        list->addItem(item);
    }
    ruleEntryInput->clear();
}

void RuleDialog::batchInput() {
    bool ok;
    QString text = QInputDialog::getMultiLineText(this, "批量输入", "请输入规则项 (每行一个):", "", &ok);
    if (ok && !text.isEmpty()) {
        QStringList lines = text.split("\n", Qt::SkipEmptyParts);
        QListWidget *list = qobject_cast<QListWidget*>(ruleTabs->currentWidget());
        for(auto l : lines) {
            QListWidgetItem *item = new QListWidgetItem(l.trimmed());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            list->addItem(item);
        }
    }
}

void RuleDialog::setRule(const ShieldRule &rule) {
    nameInput->setText(rule.name);
    indexSpin->setValue(rule.index);
    if(rule.isWhiteList) whiteRadio->setChecked(true); else blackRadio->setChecked(true);
    remarkInput->setText(rule.remark);
    starSpin->setValue(rule.starLevel);
    createDateLabel->setText(rule.createDate);
    
    auto loadList = [](QListWidget *list, const QList<ShieldRule::RuleEntry> &data) {
        list->clear();
        for(const auto &e : data) {
            QListWidgetItem *item = new QListWidgetItem(e.text);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(e.enabled ? Qt::Checked : Qt::Unchecked);
            list->addItem(item);
        }
    };
    loadList(titleList, rule.entryTitles);
    loadList(webList, rule.entryWebs);
    loadList(softList, rule.entrySofts);
    loadList(appList, rule.entryApps);
    loadList(commList, rule.entryComms);

    timeTable->setRowCount(0);
    for (const auto &ts : rule.timeSlots) {
        int r = timeTable->rowCount();
        timeTable->insertRow(r);
        timeTable->setItem(r, 0, new QTableWidgetItem(ts.type));
        timeTable->setItem(r, 1, new QTableWidgetItem(ts.start.toString("HH:mm")));
        timeTable->setItem(r, 2, new QTableWidgetItem(ts.end.toString("HH:mm")));
        QPushButton *del = new QPushButton("删除");
        connect(del, &QPushButton::clicked, [this]() {
            timeTable->removeRow(timeTable->currentRow());
        });
        timeTable->setCellWidget(r, 3, del);
    }
}

ShieldRule RuleDialog::getRule() const {
    ShieldRule rule;
    rule.name = nameInput->text();
    rule.index = indexSpin->value();
    rule.isWhiteList = whiteRadio->isChecked();
    rule.remark = remarkInput->text();
    rule.starLevel = starSpin->value();
    rule.createDate = createDateLabel->text();
    rule.enabled = true;

    auto saveList = [](QListWidget *list) {
        QList<ShieldRule::RuleEntry> res;
        for(int i=0; i<list->count(); ++i) {
            ShieldRule::RuleEntry e;
            e.text = list->item(i)->text();
            e.enabled = (list->item(i)->checkState() == Qt::Checked);
            res.append(e);
        }
        return res;
    };
    rule.entryTitles = saveList(titleList);
    rule.entryWebs = saveList(webList);
    rule.entrySofts = saveList(softList);
    rule.entryApps = saveList(appList);
    rule.entryComms = saveList(commList);

    for (int i = 0; i < timeTable->rowCount(); ++i) {
        ShieldRule::TimeSlot ts;
        ts.type = timeTable->item(i, 0)->text();
        ts.start = QTime::fromString(timeTable->item(i, 1)->text(), "HH:mm");
        ts.end = QTime::fromString(timeTable->item(i, 2)->text(), "HH:mm");
        ts.enabled = true;
        rule.timeSlots.append(ts);
    }

    return rule;
}

QStringList ShieldRule::enabledTitles() const {
    QStringList res;
    for(const auto &e : entryTitles) if(e.enabled) res << e.text;
    return res;
}
QStringList ShieldRule::enabledWebs() const {
    QStringList res;
    for(const auto &e : entryWebs) if(e.enabled) res << e.text;
    return res;
}
QStringList ShieldRule::enabledSoftwares() const {
    QStringList res;
    for(const auto &e : entrySofts) if(e.enabled) res << e.text;
    return res;
}
QStringList ShieldRule::enabledApps() const {
    QStringList res;
    for(const auto &e : entryApps) if(e.enabled) res << e.text;
    return res;
}
QStringList ShieldRule::enabledCommTools() const {
    QStringList res;
    for(const auto &e : entryComms) if(e.enabled) res << e.text;
    return res;
}
