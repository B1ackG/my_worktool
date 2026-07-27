#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LiChenYang"));
    QApplication::setApplicationName(QStringLiteral("LinuxHelper"));
    QApplication::setApplicationDisplayName(QStringLiteral("李晨阳的linux工作助手"));

    // Fusion gives consistent controls across distros; keep light industrial look.
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion")))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont appFont = a.font();
    if (appFont.pointSize() > 0 && appFont.pointSize() < 10)
        appFont.setPointSize(10);
    a.setFont(appFont);

    a.setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f4f6f8; }"
        "QMenuBar { background: #ffffff; border-bottom: 1px solid #dde3ea; padding: 2px 4px; }"
        "QMenuBar::item { padding: 4px 10px; border-radius: 4px; }"
        "QMenuBar::item:selected { background: #e8eef5; }"
        "QToolTip { background: #2c3e50; color: #fff; border: none; padding: 4px 8px; }"
        "QPushButton { padding: 5px 12px; border-radius: 4px; border: 1px solid #c5ced8; background: #ffffff; }"
        "QPushButton:hover { background: #f0f4f8; border-color: #9aa8b5; }"
        "QPushButton:pressed { background: #e4ebf2; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        "  padding: 4px 6px; border: 1px solid #c5ced8; border-radius: 4px; background: #ffffff; min-height: 22px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #3d8bfd; }"
        "QTableWidget { gridline-color: #e6ebf0; selection-background-color: #d6e6ff; selection-color: #1a1a1a; "
        "  border: 1px solid #d5dde5; border-radius: 4px; background: #ffffff; }"
        "QHeaderView::section { background: #eef2f6; padding: 6px 8px; border: none; "
        "  border-right: 1px solid #dde3ea; border-bottom: 1px solid #dde3ea; font-weight: 600; }"
        "QCheckBox { spacing: 6px; }"
        "QGroupBox { font-weight: 600; border: 1px solid #d5dde5; border-radius: 6px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QStatusBar { background: #ffffff; border-top: 1px solid #dde3ea; }"
    ));

    MainWindow w;
    w.show();
    return a.exec();
}
