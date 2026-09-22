#include "mainwindow.h"
#include <QApplication>
#include <QFont>
#include <QProcess>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <string>
#endif

// GNOME autostart often races Xft.dpi: Qt then uses physical DPI (~188 on HiDPI)
// as logical DPI and point fonts become ~2x. Prefer X resources; else 96.
static int resolveSessionFontDpi()
{
#ifdef Q_OS_WIN
    return 96;
#else
    QProcess xrdb;
    xrdb.start(QStringLiteral("xrdb"), {QStringLiteral("-query")});
    if (!xrdb.waitForFinished(800) || xrdb.exitStatus() != QProcess::NormalExit || xrdb.exitCode() != 0)
        return 96;
    const QList<QByteArray> lines = xrdb.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("Xft.dpi:"))
            continue;
        bool ok = false;
        const int dpi = line.mid(8).trimmed().toInt(&ok);
        if (ok && dpi >= 48 && dpi <= 480)
            return dpi;
    }
    return 96;
#endif
}

#ifdef Q_OS_WIN
// HKCU Run / Startup shortcuts often start with CWD=System32. Pin CWD, PATH and
// the platform plugin dir to the exe folder before QApplication loads QPA.
static void prepareWindowsAppEnvironment()
{
    wchar_t modulePath[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;

    wchar_t *lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash)
        return;
    *lastSlash = L'\0';

    const std::wstring dir(modulePath);
    SetCurrentDirectoryW(dir.c_str());
    SetDllDirectoryW(dir.c_str());

    const std::wstring platforms = dir + L"\\platforms";
    const DWORD attrs = GetFileAttributesW(platforms.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", platforms.c_str());

    wchar_t oldPath[32768];
    const DWORD oldLen = GetEnvironmentVariableW(L"PATH", oldPath, 32768);
    if (oldLen > 0 && oldLen < 32767) {
        const std::wstring combined = dir + L';' + oldPath;
        SetEnvironmentVariableW(L"PATH", combined.c_str());
    } else {
        SetEnvironmentVariableW(L"PATH", dir.c_str());
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    prepareWindowsAppEnvironment();
#endif

    if (qEnvironmentVariableIsEmpty("QT_FONT_DPI"))
        qputenv("QT_FONT_DPI", QByteArray::number(resolveSessionFontDpi()));

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

    // Keep process alive when main window is hidden to the system tray.
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow w;
    w.show();
    return a.exec();
}
