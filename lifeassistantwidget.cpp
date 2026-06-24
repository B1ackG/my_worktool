#include "lifeassistantwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QProcess>
#include <QDebug>
#include <QDate>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QCheckBox>
#include <QTimeEdit>
#include <QComboBox>
#include <QSplitter>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QAction>
#include <QCompleter>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QTextCharFormat>
#include <QMenu>
#include <QAction>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

LifeAssistantWidget::LifeAssistantWidget(QWidget *parent)
    : QWidget(parent), isPicking(false), isBlockingState(false), isCurrentDayWorkday(true)
{

    // 初始化悬浮窗
    captureWin = new CaptureWindow();
    connect(captureWin, &CaptureWindow::procConfirmed, this, &LifeAssistantWidget::onProcessCaptured);
    useTimer.start();

    networkManager = new QNetworkAccessManager(this);
    connect(networkManager, &QNetworkAccessManager::finished, this, &LifeAssistantWidget::onHolidayDataReceived);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    // 1. 侧边栏导航
    sidebarList = new QListWidget();
    sidebarList->setFixedWidth(180);
    sidebarList->setIconSize(QSize(32, 32));
    sidebarList->setStyleSheet(
        "QListWidget { background-color: #f5f5f5; border: none; border-right: 1px solid #ddd; outline: 0; }"
        "QListWidget::item { height: 50px; padding-left: 15px; border-bottom: 1px solid #eee; }"
        "QListWidget::item:selected { background-color: #e3f2fd; color: #1976d2; border-right: 3px solid #1976d2; }"
    );
    
    QListWidgetItem *itemWork = new QListWidgetItem("💼 工作助手");
    QListWidgetItem *itemTool = new QListWidgetItem("💻 电脑合理使用");
    sidebarList->addItem(itemWork);
    sidebarList->addItem(itemTool);
    mainLayout->addWidget(sidebarList);

    // 2. 堆叠窗口
    stackedWidget = new QStackedWidget();
    mainLayout->addWidget(stackedWidget, 1);

    connect(sidebarList, &QListWidget::currentRowChanged, this, &LifeAssistantWidget::onSidebarChanged);

    // --- 页面 1: 工作助手 ---
    QWidget *workPage = new QWidget();
    QVBoxLayout *workPageLayout = new QVBoxLayout(workPage);
    stackedWidget->addWidget(workPage);

    QGroupBox *workSearchGroup = new QGroupBox("🔍 企业查询 (小巨人/专精特新)");
    QHBoxLayout *workSearchH = new QHBoxLayout(workSearchGroup);
    
    provinceCombo = new QComboBox();
    provinceCombo->addItems({"全部", "高新区", "天府新区", "武侯区", "双流区", "成都其他", "四川其他"}); 
    provinceCombo->setMinimumHeight(35);
    
    companySearchEdit = new QLineEdit();
    companySearchEdit->setPlaceholderText("输入企业名称关键字...");
    companySearchEdit->setMinimumHeight(35);
    
    QPushButton *workQueryBtn = new QPushButton("🚀 开始查询");
    workQueryBtn->setMinimumHeight(35);
    workQueryBtn->setStyleSheet("background-color: #2e7d32; color: white; border-radius: 4px; padding: 0 15px;");
    
    workSearchH->addWidget(new QLabel("地区:"));
    workSearchH->addWidget(provinceCombo, 1);
    workSearchH->addWidget(new QLabel("名称:"));
    workSearchH->addWidget(companySearchEdit, 3);
    workSearchH->addWidget(workQueryBtn, 1);
    workPageLayout->addWidget(workSearchGroup);

    workTable = new QTableWidget(0, 6);
    workTable->setHorizontalHeaderLabels({"企业名称", "所在地区", "核心技术/专利", "发展稳健度", "匹配度/岗位", "🔍 深度背调"});
    workTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    workTable->setAlternatingRowColors(true);
    workTable->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(workTable, &QTableWidget::itemDoubleClicked, [this](QTableWidgetItem *item){
        if(!item || item->column() != 0) return; 
        QString companyName = item->text();
        QString url = QString("https://aiqicha.baidu.com/s?q=%1").arg(companyName);
        QDesktopServices::openUrl(QUrl(url));
    });

    connect(workTable, &QTableWidget::cellClicked, [this](int row, int col){
        if(col != 5) return; 
        QTableWidgetItem *nameItem = workTable->item(row, 0);
        QTableWidgetItem *fieldItem = workTable->item(row, 4);
        if(!nameItem || !fieldItem) return;

        QString company = nameItem->text();
        QString field = fieldItem->text();
        
        QString hrInfo = "【暂无符合嵌入式/QT方向的公开招聘信息】";
        if(field.contains("嵌入式软件")) {
            hrInfo = "🔥 招聘中：嵌入式软件工程师 (Linux/C++)\n要求：精通驱动开发，有QT实战经验。\n薪资：15k-30k";
        } else if(field.contains("QT上位机")) {
            hrInfo = "🔥 招聘中：QT高级开发工程师\n要求：精通QSS/自定义控件，熟悉多线程通信。\n薪资：12k-25k";
        } else if(field.contains("硬件")) {
            hrInfo = "🔥 招聘中：硬件研发工程师 / PCB设计\n要求：精通Allegro/Altium，熟悉ARM架构。\n薪资：18k-35k";
        } else if(field.contains("底层开发")) {
            hrInfo = "🔥 招聘中：BSP固件工程师\n要求：精通U-Boot/Kernel移植，熟悉各种总线协议。\n薪资：20k+";
        }

        QMessageBox *box = new QMessageBox(this);
        box->setWindowTitle("🔍 小而美公司深度背调: " + company);
        box->setText(QString("<b>1. 技术护城河 (爱企查-知识产权):</b><br>"
                             "• 核心方向: %1<br>"
                             "• 深度建议: 重点查看其[发明专利]数量。专利多的公司技术壁垒高，适合开发者长期成长。<br><br>"
                             "<b>2. 经营稳健度 (爱企查-年报):</b><br>"
                             "• 发展评测: 专精特新认证企业<br>"
                             "• 避坑建议: 在爱企查查看[社保人数]趋势。稳健增长的中小型公司(50-200人)是理想的“小而美”。<br><br>"
                             "<b>3. 投递方案建议:</b><br>"
                             "• 实时在招岗位 (点击下方链接查看):<br>"
                             "• 实时背调操作: 建议优先点击[爱企查-专利/年报], 查看[股权穿透图]判定是否为大厂全资控股的「小而美」。搜索其【技术总监/主管】，投递作品集往往比盲投HR更有效。")
                     .arg(field).arg(hrInfo));
        box->setIcon(QMessageBox::Information);
        box->addButton("爱企查查年报/专利", QMessageBox::ActionRole);
        box->addButton("BOSS直聘找主管", QMessageBox::ActionRole);
        box->addButton("智联招聘找人", QMessageBox::ActionRole);
        box->addButton("关闭", QMessageBox::RejectRole);
        
        int res = box->exec();
        if(res == 0) QDesktopServices::openUrl(QUrl(QString("https://aiqicha.baidu.com/s?q=%1").arg(company)));
        else if(res == 1) QDesktopServices::openUrl(QUrl(QString("https://www.zhipin.com/web/geek/job?query=%1").arg(company)));
        else if(res == 2) QDesktopServices::openUrl(QUrl(QString("https://sou.zhaopin.com/?jl=801&kw=%1").arg(company)));
        delete box;
    });

    connect(workTable, &QTableWidget::customContextMenuRequested, [this](const QPoint &pos){
        QTableWidgetItem *item = workTable->itemAt(pos);
        if(!item) return;
        QMenu menu;
        QAction *copyAction = menu.addAction("📋 复制内容");
        connect(copyAction, &QAction::triggered, [item](){
            QApplication::clipboard()->setText(item->text());
        });
        menu.exec(workTable->viewport()->mapToGlobal(pos));
    });
    workPageLayout->addWidget(workTable);

    auto performWorkQuery = [this](){
        QString districtFilter = provinceCombo->currentText();
        QString keyword = companySearchEdit->text().trimmed();

        workTable->setRowCount(0);
        
        struct Firm { QString name; QString loc; QString batch; QString score; QString field; };
        QList<Firm> firms = {
            {"零八一电子集团四川力源电子有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "嵌入式硬件/军工"},
            {"四川思创激光科技有限公司", "武侯区", "第七批", "⭐⭐⭐⭐", "激光控制/嵌入式软件"},
            {"成都普什信息自动化有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "QT上位机/自动化"},
            {"成都中科卓尔智能科技集团有限公司", "双流区", "第七批", "⭐⭐⭐", "智能制造/软件"},
            {"四川易诚智讯科技有限公司", "成都其他", "第七批", "⭐⭐⭐", "物联网/嵌入式"},
            {"四川中科朗星光电科技有限公司", "双流区", "第七批", "⭐⭐⭐⭐", "光电/嵌入式硬件"},
            {"成都赛英科技有限公司", "成都其他", "第七批", "⭐⭐⭐⭐⭐", "雷达/嵌入式系统"},
            {"成都中科微信息技术研究院有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "芯片设计/底层开发"},
            {"成都易瞳科技有限公司", "高新区", "第七批", "⭐⭐⭐⭐", "视觉算法/QT驱动"},
            {"成都睿沿科技有限公司", "成都其他", "第七批", "⭐⭐⭐", "AI/嵌入式软件"},
            {"四川安迪科技实业有限公司", "武侯区", "第七批", "⭐⭐⭐⭐", "射频/硬件设计"},
            {"成都市易冲半导体有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "电源管理/硬件"},
            {"零八一电子集团四川力源电子有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "嵌入式硬件/军工"},
            {"四川思创激光科技有限公司", "武侯区", "第七批", "⭐⭐⭐⭐", "激光控制/嵌入式软件"},
            {"成都普什信息自动化有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "QT上位机/自动化"},
            {"成都中科卓尔智能科技集团有限公司", "双流区", "第七批", "⭐⭐⭐", "智能制造/软件"},
            {"四川易诚智讯科技有限公司", "成都其他", "第七批", "⭐⭐⭐", "物联网/嵌入式"},
            {"四川中科朗星光电科技有限公司", "双流区", "第七批", "⭐⭐⭐⭐", "光电/嵌入式硬件"},
            {"成都赛英科技有限公司", "成都其他", "第七批", "⭐⭐⭐⭐⭐", "雷达/嵌入式系统"},
            {"成都中科微信息技术研究院有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "芯片设计/底层开发"},
            {"成都易瞳科技有限公司", "高新区", "第七批", "⭐⭐⭐⭐", "视觉算法/QT驱动"},
            {"成都睿沿科技有限公司", "成都其他", "第七批", "⭐⭐⭐", "AI/嵌入式软件"},
            {"四川安迪科技实业有限公司", "武侯区", "第七批", "⭐⭐⭐⭐", "射频/硬件设计"},
            {"成都市易冲半导体有限公司", "高新区", "第七批", "⭐⭐⭐⭐⭐", "电源管理/硬件"},
            {"成都虚谷伟业科技有限公司", "高新区", "第七批", "⭐⭐⭐", "数据库/后端"},
            {"佳缘科技股份有限公司", "高新区", "第七批", "⭐⭐⭐⭐", "网络安全/嵌入式"},
            {"英诺达（成都）电子科技有限公司", "高新区", "第七批", "⭐⭐⭐⭐", "EDA/QT开发"},
            {"德能森智能科技（成都）有限公司", "天府新区", "第七批", "⭐⭐⭐⭐⭐", "智能家居/QT上位机"},
            {"四川锦江电子医疗器械科技股份有限公司", "武侯区", "第七批", "⭐⭐⭐⭐⭐", "医疗器械/嵌入式软件"},
            {"成都華興匯明科技有限公司", "高新区", "第七批", "⭐⭐⭐⭐", "航空电子/硬件"},
            {"四川荣创新能动力系统有限公司", "成都其他", "第七批", "⭐⭐⭐⭐", "氢能控制/嵌入式"},
            {"成都川缆电缆有限公司", "成都其他", "新第四批", "⭐", "电缆制造"},
            {"成都工具研究所有限公司", "成都其他", "新第四批", "⭐⭐⭐", "精密刀具/制造"},
            {"成都光明光学元件有限公司", "成都其他", "新第四批", "⭐⭐⭐⭐", "光学玻璃/精密仪器"},
            {"成都玖锦科技有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "电子测量/嵌入式硬件"},
            {"成都盟升电子技术股份有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "卫星导航/嵌入式软件"},
            {"成都南方电子仪表有限公司", "成都其他", "新第四批", "⭐⭐⭐", "智能仪表/嵌入式"},
            {"成都瑞雪丰泰精密电子股份有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "微波组件/硬件"},
            {"成都新航工业科技股份有限公司", "双流区", "新第四批", "⭐⭐⭐⭐", "航空电子/嵌入式"},
            {"四川华控图形科技有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "图形渲染/QT上位机"},
            {"中电九天智能科技有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "工业软件/MES"},
            {"成都雷电微力科技股份有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "微波集成/硬件设计"},
            {"成都瑞迪威科技有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "毫米波芯片/嵌入式"},
            {"成都菲斯洛克电子技术有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "频率源/底层开发"},
            {"成都宜泊信息科技有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "智慧泊车/QT+云端"},
            {"成都华光瑞芯微电子股份有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "模拟集成电路/硬件"},
            {"成都仕芯半导体有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "微波半导体/芯片设计"},
            {"成都新西旺自动化科技有限公司", "双流区", "新第四批", "⭐⭐⭐⭐", "视觉检测/QT界面"},
            {"四川观想科技股份有限公司", "天府新区", "新第四批", "⭐⭐⭐⭐⭐", "装备信息化/嵌入式软件"},
            {"四川赛狄信息技术股份公司", "高新区", "新第四批", "⭐⭐⭐⭐", "数据采集/嵌入式"},
            {"成都泰盟软件有限公司", "成都其他", "新第四批", "⭐⭐⭐⭐", "医学仪器/QT上位机"},
            {"成都优博创通信技术有限公司", "高新区", "新第四批", "⭐⭐⭐⭐", "光收发模块/嵌入式"},
            {"成都振芯科技股份有限公司", "高新区", "新第四批", "⭐⭐⭐⭐⭐", "北斗终端/嵌入式+QT"},
            {"中材科技（成都）有限公司", "新津区", "新第四批", "⭐⭐⭐", "压力容器/控制系统"}
        };

        for(const auto &f : firms) {
            if(!districtFilter.isEmpty() && districtFilter != "全部" && !f.loc.contains(districtFilter)) continue;
            if(!keyword.isEmpty() && !f.name.contains(keyword) && !f.field.contains(keyword)) continue;
            
            int r = workTable->rowCount();
            workTable->insertRow(r);
            
            QString techMoat = "高";
            if(f.field.contains("医疗") || f.field.contains("光学")) techMoat = "极高 (专利墙)";
            else if(f.field.contains("集成") || f.field.contains("半导体")) techMoat = "极高 (芯片技术)";
            
            QString stability = "⭐⭐⭐⭐";
            if(f.batch.contains("第七批")) stability = "⭐⭐⭐⭐⭐ (极稳)";

            workTable->setItem(r, 0, new QTableWidgetItem(f.name));
            workTable->setItem(r, 1, new QTableWidgetItem(f.loc));
            workTable->setItem(r, 2, new QTableWidgetItem(techMoat));
            workTable->setItem(r, 3, new QTableWidgetItem(stability));
            workTable->setItem(r, 4, new QTableWidgetItem(f.field));
            
            QTableWidgetItem *actionItem = new QTableWidgetItem("🔍 深度背调");
            actionItem->setTextAlignment(Qt::AlignCenter);
            actionItem->setForeground(Qt::blue);
            workTable->setItem(r, 5, actionItem);
        }
    };
    connect(workQueryBtn, &QPushButton::clicked, performWorkQuery);
    connect(companySearchEdit, &QLineEdit::returnPressed, performWorkQuery);
    QTimer::singleShot(50, workQueryBtn, &QPushButton::click);

    // --- 页面 2: 电脑合理使用助手 ---
    QWidget *toolPage = new QWidget();
    QVBoxLayout *toolPageLayout = new QVBoxLayout(toolPage);
    stackedWidget->addWidget(toolPage);

    // 监控预览 & 捕获
    QGroupBox *topGroup = new QGroupBox("实时状态与程序捕获");
    QHBoxLayout *topH = new QHBoxLayout(topGroup);
    focusInfoLabel = new QLabel("窗口: 正在检测...");
    processNameLabel = new QLabel("进程: 正在检测...");
    pickBtn = new QPushButton("捕获当前程序 (5s)");
    pickBtn->setStyleSheet("background-color: #2196F3; color: white;");
    topH->addWidget(focusInfoLabel, 2);
    topH->addWidget(processNameLabel, 1);
    topH->addWidget(pickBtn, 1);
    toolPageLayout->addWidget(topGroup);

    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal);
    QTabWidget *mainTabs = new QTabWidget();

    // 标签页 1：屏蔽规则管理
    QWidget *ruleTab = new QWidget();
    QVBoxLayout *ruleV = new QVBoxLayout(ruleTab);
    ruleListWidget = new QListWidget();
    ruleV->addWidget(ruleListWidget);
    mainTabs->addTab(ruleTab, "🛡 屏蔽规则");
    ruleListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ruleListWidget, &QListWidget::customContextMenuRequested, this, &LifeAssistantWidget::showRuleContextMenu);

    // 标签页 2：定时任务
    QWidget *taskTabWrapper = new QWidget();
    QVBoxLayout *taskV = new QVBoxLayout(taskTabWrapper);
    taskTable = new QTableWidget(0, 4);
    taskTable->setHorizontalHeaderLabels({"启用", "时间", "类型", "删除"});
    taskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    taskV->addWidget(taskTable);
    mainTabs->addTab(taskTabWrapper, "🕒 定时任务");
    taskTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(taskTable, &QTableWidget::customContextMenuRequested, this, &LifeAssistantWidget::showTaskContextMenu);

    mainSplitter->addWidget(mainTabs);

    // 右侧：日历
    QGroupBox *calGroup = new QGroupBox("节假日助手");
    QVBoxLayout *calV = new QVBoxLayout(calGroup);
    calendar = new QCalendarWidget();
    calendar->setGridVisible(true);
    calV->addWidget(calendar);
    mainSplitter->addWidget(calGroup);
    mainSplitter->setStretchFactor(0, 3);
    toolPageLayout->addWidget(mainSplitter);

    // 底部控制
    QHBoxLayout *bottom = new QHBoxLayout();
    QVBoxLayout *finalV = new QVBoxLayout();
    workDayLabel = new QLabel("节假日状态同步中...");
    QPushButton *saveBtn = new QPushButton("✅ 保存所有设置并生效");
    saveBtn->setFixedHeight(50);
    saveBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");

    QHBoxLayout *fH = new QHBoxLayout();
    fatigueCheck = new QCheckBox("开启疲劳提醒");
    workSpin = new QSpinBox(); workSpin->setRange(1, 120); workSpin->setValue(40); workSpin->setSuffix("分专注");
    restSpin = new QSpinBox(); restSpin->setRange(1, 60); restSpin->setValue(10); restSpin->setSuffix("分休息");
    fH->addWidget(fatigueCheck); fH->addWidget(workSpin); fH->addWidget(restSpin);

    finalV->addLayout(fH);
    finalV->addWidget(workDayLabel);
    finalV->addWidget(saveBtn);
    bottom->addLayout(finalV);
    toolPageLayout->addLayout(bottom);

    // 定时器初始化
    focusTimer = new QTimer(this);
    connect(focusTimer, &QTimer::timeout, this, &LifeAssistantWidget::updateFocusWindow);
    focusTimer->start(1000);

    checkTimer = new QTimer(this);
    connect(checkTimer, &QTimer::timeout, this, &LifeAssistantWidget::checkTasks);
    checkTimer->start(1000);

    // 信号绑定
    connect(pickBtn, &QPushButton::clicked, this, &LifeAssistantWidget::startPickWindow);
    connect(saveBtn, &QPushButton::clicked, this, &LifeAssistantWidget::saveSettings);
    connect(fatigueCheck, &QCheckBox::toggled, this, &LifeAssistantWidget::toggleFatigue);

    loadSettings();
    checkWorkDay();
}

void LifeAssistantWidget::onSidebarChanged(int index) {
    stackedWidget->setCurrentIndex(index);
}

LifeAssistantWidget::~LifeAssistantWidget() {
    delete captureWin;
}

bool LifeAssistantWidget::isSelfProcess(const QString &proc) const
{
    if (proc.isEmpty())
        return true;
    const QString lower = proc.toLower();
    if (lower.contains(QStringLiteral("lifeassistant"))
        || lower.contains(QStringLiteral("modbustcpassistant"))) {
        return true;
    }
    const QString selfBase = QFileInfo(QCoreApplication::applicationFilePath()).fileName().toLower();
    return lower == selfBase;
}

void LifeAssistantWidget::toggleFatigue(bool enabled) {
    if (enabled) {
        useTimer.start();
        isBlockingState = false;
    }
}

void LifeAssistantWidget::onProcessCaptured(const QString &proc) {
    if (rules.isEmpty()) {
        QMessageBox::warning(this, "提示", "请创建一个规则组！");
        return;
    }

    bool addedAny = false;
    for (auto &rule : rules) {
        bool exists = false;
        for (const auto &e : rule.entrySofts)
            if (e.text == proc) { exists = true; break; }

        if (!exists) {
            ShieldRule::RuleEntry e;
            e.text = proc;
            e.enabled = false;
            rule.entrySofts.append(e);
            addedAny = true;
        }
    }

    if (addedAny) {
        QMessageBox::information(this, "捕获成功",
            QString("程序 [%1] 已同步到所有规则组。\n请编辑规则勾选 [启用] 以生效。").arg(proc));
        saveSettings();
    } else {
        QMessageBox::information(this, "提示", "该程序已在所有规则组中。");
    }
}

void LifeAssistantWidget::addShieldRule() {
    RuleDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        rules.append(dlg.getRule());
        updateRuleListUI();
        saveSettings();
    }
}

void LifeAssistantWidget::editShieldRule() {
    int idx = ruleListWidget->currentRow();
    if (idx < 0) return;

    RuleDialog dlg(this);
    dlg.setRule(rules[idx]);
    if (dlg.exec() == QDialog::Accepted) {
        rules[idx] = dlg.getRule();
        updateRuleListUI();
        saveSettings();
    }
}

void LifeAssistantWidget::removeShieldRule() {
    int idx = ruleListWidget->currentRow();
    if (idx < 0) return;
    if (QMessageBox::question(this, "确认", "确定要删除该规则组吗？") == QMessageBox::Yes) {
        rules.removeAt(idx);
        updateRuleListUI();
        saveSettings();
    }
}

void LifeAssistantWidget::showRuleContextMenu(const QPoint &pos) {
    QMenu menu(this);
    QAction *addAct = menu.addAction("新增规则");
    QAction *editAct = menu.addAction("编辑规则");
    QAction *delAct = menu.addAction("删除规则");

    QListWidgetItem *item = ruleListWidget->itemAt(pos);
    if (!item) {
        editAct->setEnabled(false);
        delAct->setEnabled(false);
    }

    connect(addAct, &QAction::triggered, this, &LifeAssistantWidget::addShieldRule);
    connect(editAct, &QAction::triggered, this, &LifeAssistantWidget::editShieldRule);
    connect(delAct, &QAction::triggered, this, &LifeAssistantWidget::removeShieldRule);

    menu.exec(ruleListWidget->mapToGlobal(pos));
}

void LifeAssistantWidget::showTaskContextMenu(const QPoint &pos) {
    QMenu menu(this);
    QAction *addAct = menu.addAction("新增关机任务");
    QAction *editAct = menu.addAction("编辑时间/类型");
    QAction *delAct = menu.addAction("删除任务");

    QTableWidgetItem *item = taskTable->itemAt(pos);
    int row = item ? item->row() : -1;
    if (row < 0) {
        editAct->setEnabled(false);
        delAct->setEnabled(false);
    }

    connect(addAct, &QAction::triggered, this, &LifeAssistantWidget::addTask);
    connect(editAct, &QAction::triggered, this, [this, row]() { editTaskAt(row); });
    connect(delAct, &QAction::triggered, this, [this, row]() {
        if (row >= 0) taskTable->removeRow(row);
    });

    menu.exec(taskTable->mapToGlobal(pos));
}

void LifeAssistantWidget::editTaskAt(int row) {
    if (row < 0) return;
    QTimeEdit *te = qobject_cast<QTimeEdit *>(taskTable->cellWidget(row, 1));
    if (te) te->setFocus();
}

void LifeAssistantWidget::updateRuleListUI() {
    ruleListWidget->clear();
    for (const auto &r : rules) {
        QListWidgetItem *item = new QListWidgetItem(r.name);
        if (!r.enabled) item->setForeground(Qt::gray);
        ruleListWidget->addItem(item);
    }
}

void LifeAssistantWidget::saveSettings() {
    QSettings s("MyCompany", "LifeAssistant");

    QJsonArray rArr;
    for (const auto &rule : rules) {
        QJsonObject rObj;
        rObj["name"] = rule.name;
        rObj["idx"] = rule.index;
        rObj["isWhite"] = rule.isWhiteList;
        rObj["rem"] = rule.remark;
        rObj["star"] = rule.starLevel;
        rObj["date"] = rule.createDate;
        rObj["en"] = rule.enabled;

        QJsonArray tsArr;
        for (const auto &ts : rule.timeSlots) {
            QJsonObject tsObj;
            tsObj["type"] = ts.type;
            tsObj["s"] = ts.start.toString("HH:mm");
            tsObj["n"] = ts.end.toString("HH:mm");
            tsArr.append(tsObj);
        }
        rObj["slots"] = tsArr;

        auto saveHelper = [](const QList<ShieldRule::RuleEntry> &list) {
            QJsonArray arr;
            for (const auto &e : list) {
                QJsonObject o;
                o["t"] = e.text;
                o["e"] = e.enabled;
                arr.append(o);
            }
            return arr;
        };
        rObj["titles_v5"] = saveHelper(rule.entryTitles);
        rObj["webs_v5"] = saveHelper(rule.entryWebs);
        rObj["softs_v5"] = saveHelper(rule.entrySofts);
        rObj["apps_v5"] = saveHelper(rule.entryApps);
        rObj["comms_v5"] = saveHelper(rule.entryComms);
        rArr.append(rObj);
    }
    s.setValue("rules_v5", QJsonDocument(rArr).toJson());

    QJsonArray sArr;
    for (int i = 0; i < taskTable->rowCount(); ++i) {
        QJsonObject o;
        o["e"] = taskTable->cellWidget(i, 0)->findChild<QCheckBox *>()->isChecked();
        o["t"] = qobject_cast<QTimeEdit *>(taskTable->cellWidget(i, 1))->time().toString("HH:mm");
        o["y"] = qobject_cast<QComboBox *>(taskTable->cellWidget(i, 2))->currentIndex();
        sArr.append(o);
    }
    s.setValue("st_v2", QJsonDocument(sArr).toJson());

    s.setValue("f_en", fatigueCheck->isChecked());
    s.setValue("f_w", workSpin->value());
    s.setValue("f_r", restSpin->value());

    QMessageBox::information(this, "结果", "配置已保存！");
}

void LifeAssistantWidget::loadSettings() {
    QSettings s("MyCompany", "LifeAssistant");

    rules.clear();
    QByteArray data = s.value("rules_v5").toByteArray();
    if (!data.isEmpty()) {
        QJsonArray rArr = QJsonDocument::fromJson(data).array();
        for (auto rv : rArr) {
            QJsonObject o = rv.toObject();
            ShieldRule r;
            r.name = o["name"].toString();
            r.index = o["idx"].toInt();
            r.isWhiteList = o["isWhite"].toBool();
            r.remark = o["rem"].toString();
            r.starLevel = o["star"].toInt();
            r.createDate = o["date"].toString();
            r.enabled = o["en"].toBool(true);

            QJsonArray tsArr = o["slots"].toArray();
            for (auto tsv : tsArr) {
                QJsonObject tsObj = tsv.toObject();
                ShieldRule::TimeSlot ts;
                ts.type = tsObj["type"].toString();
                ts.start = QTime::fromString(tsObj["s"].toString(), "HH:mm");
                ts.end = QTime::fromString(tsObj["n"].toString(), "HH:mm");
                r.timeSlots.append(ts);
            }

            auto loadEntries = [&](const QString &key) {
                QList<ShieldRule::RuleEntry> list;
                QJsonArray arr = o[key].toArray();
                for (auto v : arr) {
                    QJsonObject obj = v.toObject();
                    ShieldRule::RuleEntry e;
                    e.text = obj["t"].toString();
                    e.enabled = obj["e"].toBool();
                    list.append(e);
                }
                return list;
            };
            r.entryTitles = loadEntries("titles_v5");
            r.entryWebs = loadEntries("webs_v5");
            r.entrySofts = loadEntries("softs_v5");
            r.entryApps = loadEntries("apps_v5");
            r.entryComms = loadEntries("comms_v5");
            rules.append(r);
        }
    }
    updateRuleListUI();

    fatigueCheck->setChecked(s.value("f_en", false).toBool());
    workSpin->setValue(s.value("f_w", 40).toInt());
    restSpin->setValue(s.value("f_r", 10).toInt());

    QJsonArray sArr = QJsonDocument::fromJson(s.value("st_v2").toByteArray()).array();
    taskTable->setRowCount(0);
    for (auto v : sArr) {
        addTask();
        int r = taskTable->rowCount() - 1;
        QJsonObject o = v.toObject();
        taskTable->cellWidget(r, 0)->findChild<QCheckBox *>()->setChecked(o["e"].toBool());
        qobject_cast<QTimeEdit *>(taskTable->cellWidget(r, 1))->setTime(QTime::fromString(o["t"].toString(), "HH:mm"));
        qobject_cast<QComboBox *>(taskTable->cellWidget(r, 2))->setCurrentIndex(o["y"].toInt());
    }
}

bool LifeAssistantWidget::isBrowser(const QString &p) {
    QString s = p.toLower();
    return s.contains("chrome") || s.contains("firefox") || s.contains("edge") || s.contains("brave");
}

void LifeAssistantWidget::executeBlockAction(const QString &title, const QString &proc) {
    if (isSelfProcess(proc)) return;

    for (const auto &rule : rules) {
        if (!rule.enabled) continue;

        if (checkSingleRule(rule, title, proc)) {
            if (isBrowser(proc)) {
#ifdef Q_OS_WIN
                keybd_event(VK_CONTROL, 0, 0, 0);
                keybd_event('W', 0, 0, 0);
                keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
                keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
#else
                QProcess::execute("xdotool", QStringList() << "key" << "ctrl+w");
#endif
            } else {
#ifdef Q_OS_WIN
                HWND h = GetForegroundWindow();
                if (h) PostMessage(h, WM_SYSCOMMAND, SC_MINIMIZE, 0);
#else
                QProcess::execute("pkill", QStringList() << "-f" << proc);
#endif
            }
            return;
        }
    }
}

bool LifeAssistantWidget::checkSingleRule(const ShieldRule &rule, const QString &title, const QString &proc) {
    bool timeMatch = false;
    QTime now = QTime::currentTime();
    for (const auto &ts : rule.timeSlots) {
        bool tm = (ts.start <= ts.end) ? (now >= ts.start && now <= ts.end)
                                        : (now >= ts.start || now <= ts.end);
        bool dm = (ts.type == "每天")
                  || (ts.type == "工作日" && isCurrentDayWorkday)
                  || (ts.type == "休息日" && !isCurrentDayWorkday);
        if (tm && dm) { timeMatch = true; break; }
    }
    if (!timeMatch) return false;

    auto contains = [&](const QStringList &list, const QString &target) {
        for (const auto &item : list)
            if (target.contains(item, Qt::CaseInsensitive)) return true;
        return false;
    };

    bool hit = contains(rule.enabledTitles(), title)
               || contains(rule.enabledWebs(), title)
               || contains(rule.enabledSoftwares(), proc)
               || contains(rule.enabledApps(), proc)
               || contains(rule.enabledCommTools(), proc);

    return rule.isWhiteList ? !hit : hit;
}

void LifeAssistantWidget::startPickWindow() {
    isPicking = true;
    pickBtn->setText("正在捕获焦点...");
    pickBtn->setEnabled(false);

    QTimer::singleShot(5000, this, [this]() {
        isPicking = false;
        pickBtn->setText("捕获当前程序 (5s)");
        pickBtn->setEnabled(true);

        QString t = getActiveWindowTitle();
        QString p = getActiveWindowProcessName();

        if (!p.isEmpty() && !isSelfProcess(p)) {
            captureWin->updateInfo(t, p);
            captureWin->show();
            captureWin->raise();
            captureWin->activateWindow();
        }
    });
}

void LifeAssistantWidget::updateFocusWindow() {
    QString t = getActiveWindowTitle();
    QString p = getActiveWindowProcessName();

    focusInfoLabel->setText("窗口: " + t.left(50));
    processNameLabel->setText("进程: " + p);

    if (fatigueCheck->isChecked()) {
        qint64 elapsed = useTimer.elapsed() / 1000 / 60;
        if (!isBlockingState) {
            if (elapsed >= workSpin->value()) {
                isBlockingState = true;
                useTimer.restart();
                QMessageBox::warning(this, "疲劳提醒",
                    QString("您已连续工作 %1 分钟，请强制休息 %2 分钟！")
                        .arg(workSpin->value()).arg(restSpin->value()));
            }
        } else {
            if (elapsed >= restSpin->value()) {
                isBlockingState = false;
                useTimer.restart();
                QMessageBox::information(this, "休息结束", "休息时间到，可以继续工作了。");
            }
        }
    }

    if (!isPicking) {
        if (fatigueCheck->isChecked()) {
            if (!isBlockingState)
                return;
            executeBlockAction(t, p);
        } else {
            executeBlockAction(t, p);
        }
    }
}

void LifeAssistantWidget::addTask() {
    int r = taskTable->rowCount();
    taskTable->insertRow(r);
    QCheckBox *c = new QCheckBox();
    c->setChecked(true);
    QWidget *cnt = new QWidget();
    QHBoxLayout *l = new QHBoxLayout(cnt);
    l->addWidget(c);
    l->setAlignment(Qt::AlignCenter);
    l->setContentsMargins(0, 0, 0, 0);
    taskTable->setCellWidget(r, 0, cnt);
    taskTable->setCellWidget(r, 1, new QTimeEdit(QTime(23, 30)));
    QComboBox *cb = new QComboBox();
    cb->addItems({"每天", "工作日", "休息日"});
    taskTable->setCellWidget(r, 2, cb);
    QPushButton *d = new QPushButton("删除");
    connect(d, &QPushButton::clicked, this, [this]() {
        taskTable->removeRow(taskTable->currentRow());
    });
    taskTable->setCellWidget(r, 3, d);
}

void LifeAssistantWidget::checkTasks() {
    QTime now = QTime::currentTime();
    if (now.second() != 0) return;
    for (int i = 0; i < taskTable->rowCount(); ++i) {
        if (!taskTable->cellWidget(i, 0)->findChild<QCheckBox *>()->isChecked()) continue;
        QTime t = qobject_cast<QTimeEdit *>(taskTable->cellWidget(i, 1))->time();
        if (now.hour() == t.hour() && now.minute() == t.minute()) {
            int ty = qobject_cast<QComboBox *>(taskTable->cellWidget(i, 2))->currentIndex();
            if (ty == 0 || (ty == 1 && isCurrentDayWorkday) || (ty == 2 && !isCurrentDayWorkday))
                shutdownSystem();
        }
    }
}

void LifeAssistantWidget::checkWorkDay() {
    networkManager->get(QNetworkRequest(QUrl(
        QString("https://timor.tech/api/holiday/info/%1").arg(QDate::currentDate().toString("yyyy-MM-dd")))));
    networkManager->get(QNetworkRequest(QUrl(
        QString("https://timor.tech/api/holiday/year/%1").arg(QDate::currentDate().year()))));
}

void LifeAssistantWidget::onHolidayDataReceived(QNetworkReply *r) {
    if (r->error() == QNetworkReply::NoError) {
        QByteArray data = r->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject rt = doc.object();

        if (rt.contains("type") && !rt.contains("holiday")) {
            int t = rt["type"].toObject()["type"].toInt();
            isCurrentDayWorkday = (t == 0 || t == 3);
            QString name = rt.contains("holiday")
                ? rt["holiday"].toObject()["name"].toString()
                : (isCurrentDayWorkday ? "工作日" : "休息日");
            workDayLabel->setText(QString("今日状态：%1").arg(name));
        } else if (rt.contains("holiday")) {
            QJsonObject hMap = rt["holiday"].toObject();
            holidays.clear();
            for (auto it = hMap.begin(); it != hMap.end(); ++it) {
                QJsonObject dayObj = it.value().toObject();
                if (dayObj["holiday"].toBool()) {
                    QDate d = QDate::fromString(dayObj["date"].toString(), "yyyy-MM-dd");
                    holidays.append(d);
                }
            }
            updateHolidayColors();
        }
    }
    r->deleteLater();
}

void LifeAssistantWidget::updateHolidayColors() {
    QTextCharFormat holidayFormat;
    holidayFormat.setForeground(Qt::green);
    holidayFormat.setToolTip("法定节假日/调休");

    for (const QDate &date : holidays)
        calendar->setDateTextFormat(date, holidayFormat);
}

void LifeAssistantWidget::shutdownSystem() {
#ifdef Q_OS_WIN
    QProcess::startDetached("shutdown", QStringList() << "/s" << "/t" << "0");
#else
    QProcess::startDetached("shutdown", QStringList() << "now");
#endif
}

QString LifeAssistantWidget::getActiveWindowTitle() {
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "";
    wchar_t buf[256];
    GetWindowTextW(hwnd, buf, 256);
    return QString::fromWCharArray(buf);
#else
    QProcess p;
    p.start("sh", QStringList() << "-c"
        << "xprop -id $(xprop -root _NET_ACTIVE_WINDOW | cut -d ' ' -f 5) WM_NAME | cut -d '\"' -f 2");
    p.waitForFinished();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#endif
}

QString LifeAssistantWidget::getActiveWindowProcessName() {
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "";
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return "";
    wchar_t buf[MAX_PATH];
    if (GetModuleBaseNameW(h, NULL, buf, MAX_PATH)) {
        CloseHandle(h);
        return QString::fromWCharArray(buf);
    }
    CloseHandle(h);
    return "";
#else
    QProcess p;
    p.start("sh", QStringList() << "-c"
        << "cat /proc/$(xprop -id $(xprop -root _NET_ACTIVE_WINDOW | cut -d ' ' -f 5) _NET_WM_PID | cut -d ' ' -f 3)/comm");
    p.waitForFinished();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#endif
}
