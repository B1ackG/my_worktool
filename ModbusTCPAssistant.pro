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
    platformprefs.cpp

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
    platformprefs.h

win32: LIBS += -lpsapi -luser32

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
