#ifndef CAPTUREWINDOW_H
#define CAPTUREWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class CaptureWindow : public QWidget {
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr) : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint) {
        setFixedSize(300, 120);
        setStyleSheet("background-color: #333; color: white; border: 2px solid #2196F3; border-radius: 10px;");
        
        QVBoxLayout *layout = new QVBoxLayout(this);
        titleLabel = new QLabel("捕获窗口: 正在提取...");
        procLabel = new QLabel("当前进程: 正在提取...");
        confirmBtn = new QPushButton("确定加入屏蔽库");
        confirmBtn->setStyleSheet("background-color: #2196F3; color: white; border-radius: 5px; padding: 5px;");
        
        layout->addWidget(titleLabel);
        layout->addWidget(procLabel);
        layout->addWidget(confirmBtn);
        
        connect(confirmBtn, &QPushButton::clicked, this, &CaptureWindow::onConfirm);
    }

    void updateInfo(const QString &title, const QString &proc) {
        currentProc = proc;
        titleLabel->setText("窗口: " + title.left(30));
        procLabel->setText("进程: " + proc);
    }

signals:
    void procConfirmed(const QString &proc);

private slots:
    void onConfirm() {
        emit procConfirmed(currentProc);
        hide();
    }

private:
    QLabel *titleLabel;
    QLabel *procLabel;
    QPushButton *confirmBtn;
    QString currentProc;
};

#endif
