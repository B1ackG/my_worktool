#ifndef CAPTUREWINDOW_H
#define CAPTUREWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class CaptureWindow : public QWidget {
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr) : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint) {
        setFixedSize(360, 160);
        setStyleSheet("background-color: #333; color: white; border: 2px solid #2196F3; border-radius: 10px;");

        QVBoxLayout *layout = new QVBoxLayout(this);
        titleLabel = new QLabel("捕获窗口: 正在提取...");
        procLabel = new QLabel("当前进程: 正在提取...");
        confirmBtn = new QPushButton("确定加入屏蔽库");
        confirmBtn->setStyleSheet("background-color: #2196F3; color: white; border-radius: 5px; padding: 5px;");
        watchBtn = new QPushButton("设为关机监控窗口");
        watchBtn->setStyleSheet("background-color: #2e7d32; color: white; border-radius: 5px; padding: 5px;");

        layout->addWidget(titleLabel);
        layout->addWidget(procLabel);
        QHBoxLayout *btnH = new QHBoxLayout();
        btnH->addWidget(confirmBtn);
        btnH->addWidget(watchBtn);
        layout->addLayout(btnH);

        connect(confirmBtn, &QPushButton::clicked, this, &CaptureWindow::onConfirm);
        connect(watchBtn, &QPushButton::clicked, this, &CaptureWindow::onSetWatch);
    }

    void updateInfo(const QString &title, const QString &proc) {
        currentTitle = title;
        currentProc = proc;
        titleLabel->setText("窗口: " + title.left(30));
        procLabel->setText("进程: " + proc);
    }

signals:
    void procConfirmed(const QString &proc);
    void watchProcConfirmed(const QString &title);

private slots:
    void onConfirm() {
        emit procConfirmed(currentProc);
        hide();
    }

    void onSetWatch() {
        emit watchProcConfirmed(currentTitle);
        hide();
    }

private:
    QLabel *titleLabel;
    QLabel *procLabel;
    QPushButton *confirmBtn;
    QPushButton *watchBtn;
    QString currentTitle;
    QString currentProc;
};

#endif
