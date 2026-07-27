QT       += core gui network serialport


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    modbusslave.cpp \
    lifeassistantwidget.cpp \
    ruledialog.cpp \
    inputquickermanager.cpp \
    inputquickerwidget.cpp \
    quickerbindingdialog.cpp \
    quickerkeyboardutils.cpp \
    keyboardsteprow.cpp \
    gitworktreerunner.cpp \
    gitworktreesetup.cpp \
    gitworktreeapplydialog.cpp \
    gitworktreewizard.cpp \
    gitworktreedialog.cpp \
    gitstageguard.cpp \
    gitstagereviewdialog.cpp \
    cursorskillsdialog.cpp \
    platformprefs.cpp \
    deepseekclient.cpp

HEADERS += \
    mainwindow.h \
    modbusslave.h \
    lifeassistantwidget.h \
    capturewindow.h \
    ruledialog.h \
    inputquickermanager.h \
    inputquickerwidget.h \
    quickerbindingdialog.h \
    quickerkeyboardutils.h \
    keyboardsteprow.h \
    gitworktreeinfo.h \
    gitworktreerunner.h \
    gitworktreesetup.h \
    gitworktreeapplydialog.h \
    gitworktreewizard.h \
    gitworktreedialog.h \
    gitstageguard.h \
    gitstagereviewdialog.h \
    cursorskillsdialog.h \
    platformprefs.h \
    deepseekclient.h

win32: LIBS += -lpsapi -luser32

# Qt 5 MinGW needs OpenSSL 1.1 DLLs beside the exe for HTTPS (DeepSeek, etc.).
win32 {
    OPENSSL_WIN64 = $$PWD/third_party/openssl/win64
    CONFIG(debug, debug|release): OPENSSL_OUT = $$OUT_PWD/debug
    else: OPENSSL_OUT = $$OUT_PWD/release
    QMAKE_POST_LINK += \
        $$quote(cmd /c copy /y $$shell_path($$OPENSSL_WIN64/libssl-1_1-x64.dll) $$shell_path($$OPENSSL_OUT)) $$escape_expand(\\n\\t) \
        $$quote(cmd /c copy /y $$shell_path($$OPENSSL_WIN64/libcrypto-1_1-x64.dll) $$shell_path($$OPENSSL_OUT))
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

scripts.files = \
    scripts/input_device_utils.py \
    scripts/monitor_input_device.py \
    scripts/capture_input_trigger.py \
    scripts/input_quicker_daemon.py
scripts.path = $${target.path}/scripts
INSTALLS += scripts
