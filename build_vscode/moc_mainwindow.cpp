/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.13)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.13. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[71];
    char stringdata0[1311];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 21), // "onNavSelectionChanged"
QT_MOC_LITERAL(2, 33, 0), // ""
QT_MOC_LITERAL(3, 34, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(4, 51, 7), // "current"
QT_MOC_LITERAL(5, 59, 8), // "previous"
QT_MOC_LITERAL(6, 68, 16), // "onConnectClicked"
QT_MOC_LITERAL(7, 85, 19), // "onDisconnectClicked"
QT_MOC_LITERAL(8, 105, 18), // "onReadCoilsClicked"
QT_MOC_LITERAL(9, 124, 19), // "onReadInputsClicked"
QT_MOC_LITERAL(10, 144, 29), // "onReadHoldingRegistersClicked"
QT_MOC_LITERAL(11, 174, 27), // "onReadInputRegistersClicked"
QT_MOC_LITERAL(12, 202, 24), // "onWriteSingleCoilClicked"
QT_MOC_LITERAL(13, 227, 28), // "onWriteSingleRegisterClicked"
QT_MOC_LITERAL(14, 256, 27), // "onWriteMultipleCoilsClicked"
QT_MOC_LITERAL(15, 284, 31), // "onWriteMultipleRegistersClicked"
QT_MOC_LITERAL(16, 316, 22), // "onDisplayFormatChanged"
QT_MOC_LITERAL(17, 339, 5), // "index"
QT_MOC_LITERAL(18, 345, 23), // "onContinuousReadToggled"
QT_MOC_LITERAL(19, 369, 7), // "checked"
QT_MOC_LITERAL(20, 377, 17), // "onSocketConnected"
QT_MOC_LITERAL(21, 395, 20), // "onSocketDisconnected"
QT_MOC_LITERAL(22, 416, 17), // "onSocketReadyRead"
QT_MOC_LITERAL(23, 434, 13), // "onSocketError"
QT_MOC_LITERAL(24, 448, 28), // "QAbstractSocket::SocketError"
QT_MOC_LITERAL(25, 477, 5), // "error"
QT_MOC_LITERAL(26, 483, 21), // "onContinuousReadTimer"
QT_MOC_LITERAL(27, 505, 21), // "saveConnectionHistory"
QT_MOC_LITERAL(28, 527, 2), // "ip"
QT_MOC_LITERAL(29, 530, 21), // "loadConnectionHistory"
QT_MOC_LITERAL(30, 552, 13), // "saveAutoScene"
QT_MOC_LITERAL(31, 566, 13), // "loadAutoScene"
QT_MOC_LITERAL(32, 580, 19), // "onSerialOpenClicked"
QT_MOC_LITERAL(33, 600, 20), // "onSerialCloseClicked"
QT_MOC_LITERAL(34, 621, 19), // "onSerialSendClicked"
QT_MOC_LITERAL(35, 641, 12), // "onSerialRead"
QT_MOC_LITERAL(36, 654, 18), // "refreshSerialPorts"
QT_MOC_LITERAL(37, 673, 21), // "onGitSelectDirClicked"
QT_MOC_LITERAL(38, 695, 27), // "onGitRefreshBranchesClicked"
QT_MOC_LITERAL(39, 723, 20), // "onGitCheckoutClicked"
QT_MOC_LITERAL(40, 744, 15), // "onGitAddClicked"
QT_MOC_LITERAL(41, 760, 18), // "onGitCommitClicked"
QT_MOC_LITERAL(42, 779, 16), // "onGitPushClicked"
QT_MOC_LITERAL(43, 796, 16), // "onGitPullClicked"
QT_MOC_LITERAL(44, 813, 17), // "onGitMergeClicked"
QT_MOC_LITERAL(45, 831, 18), // "onGitStatusClicked"
QT_MOC_LITERAL(46, 850, 22), // "onGitOpenIgnoreClicked"
QT_MOC_LITERAL(47, 873, 23), // "onGitCheckIgnoreClicked"
QT_MOC_LITERAL(48, 897, 22), // "onGitRefreshLogClicked"
QT_MOC_LITERAL(49, 920, 17), // "onGitResetClicked"
QT_MOC_LITERAL(50, 938, 30), // "onGitCopyForDailyReportClicked"
QT_MOC_LITERAL(51, 969, 20), // "onScpTransferClicked"
QT_MOC_LITERAL(52, 990, 17), // "onClearLogClicked"
QT_MOC_LITERAL(53, 1008, 23), // "onStartSimulatorClicked"
QT_MOC_LITERAL(54, 1032, 22), // "onStopSimulatorClicked"
QT_MOC_LITERAL(55, 1055, 23), // "onSimSetRegisterClicked"
QT_MOC_LITERAL(56, 1079, 18), // "onSimSetBitClicked"
QT_MOC_LITERAL(57, 1098, 23), // "onSimWriteValuesClicked"
QT_MOC_LITERAL(58, 1122, 24), // "onSimRandomValuesClicked"
QT_MOC_LITERAL(59, 1147, 27), // "onApplyFaultSettingsClicked"
QT_MOC_LITERAL(60, 1175, 22), // "onExportHistoryClicked"
QT_MOC_LITERAL(61, 1198, 19), // "onRegisterOperation"
QT_MOC_LITERAL(62, 1218, 4), // "addr"
QT_MOC_LITERAL(63, 1223, 5), // "value"
QT_MOC_LITERAL(64, 1229, 6), // "opType"
QT_MOC_LITERAL(65, 1236, 20), // "onSimShowContextMenu"
QT_MOC_LITERAL(66, 1257, 3), // "pos"
QT_MOC_LITERAL(67, 1261, 14), // "onSimSetFormat"
QT_MOC_LITERAL(68, 1276, 6), // "format"
QT_MOC_LITERAL(69, 1283, 23), // "onSimShowWaveformEditor"
QT_MOC_LITERAL(70, 1307, 3) // "row"

    },
    "MainWindow\0onNavSelectionChanged\0\0"
    "QListWidgetItem*\0current\0previous\0"
    "onConnectClicked\0onDisconnectClicked\0"
    "onReadCoilsClicked\0onReadInputsClicked\0"
    "onReadHoldingRegistersClicked\0"
    "onReadInputRegistersClicked\0"
    "onWriteSingleCoilClicked\0"
    "onWriteSingleRegisterClicked\0"
    "onWriteMultipleCoilsClicked\0"
    "onWriteMultipleRegistersClicked\0"
    "onDisplayFormatChanged\0index\0"
    "onContinuousReadToggled\0checked\0"
    "onSocketConnected\0onSocketDisconnected\0"
    "onSocketReadyRead\0onSocketError\0"
    "QAbstractSocket::SocketError\0error\0"
    "onContinuousReadTimer\0saveConnectionHistory\0"
    "ip\0loadConnectionHistory\0saveAutoScene\0"
    "loadAutoScene\0onSerialOpenClicked\0"
    "onSerialCloseClicked\0onSerialSendClicked\0"
    "onSerialRead\0refreshSerialPorts\0"
    "onGitSelectDirClicked\0onGitRefreshBranchesClicked\0"
    "onGitCheckoutClicked\0onGitAddClicked\0"
    "onGitCommitClicked\0onGitPushClicked\0"
    "onGitPullClicked\0onGitMergeClicked\0"
    "onGitStatusClicked\0onGitOpenIgnoreClicked\0"
    "onGitCheckIgnoreClicked\0onGitRefreshLogClicked\0"
    "onGitResetClicked\0onGitCopyForDailyReportClicked\0"
    "onScpTransferClicked\0onClearLogClicked\0"
    "onStartSimulatorClicked\0onStopSimulatorClicked\0"
    "onSimSetRegisterClicked\0onSimSetBitClicked\0"
    "onSimWriteValuesClicked\0"
    "onSimRandomValuesClicked\0"
    "onApplyFaultSettingsClicked\0"
    "onExportHistoryClicked\0onRegisterOperation\0"
    "addr\0value\0opType\0onSimShowContextMenu\0"
    "pos\0onSimSetFormat\0format\0"
    "onSimShowWaveformEditor\0row"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      55,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    2,  289,    2, 0x08 /* Private */,
       6,    0,  294,    2, 0x08 /* Private */,
       7,    0,  295,    2, 0x08 /* Private */,
       8,    0,  296,    2, 0x08 /* Private */,
       9,    0,  297,    2, 0x08 /* Private */,
      10,    0,  298,    2, 0x08 /* Private */,
      11,    0,  299,    2, 0x08 /* Private */,
      12,    0,  300,    2, 0x08 /* Private */,
      13,    0,  301,    2, 0x08 /* Private */,
      14,    0,  302,    2, 0x08 /* Private */,
      15,    0,  303,    2, 0x08 /* Private */,
      16,    1,  304,    2, 0x08 /* Private */,
      18,    1,  307,    2, 0x08 /* Private */,
      20,    0,  310,    2, 0x08 /* Private */,
      21,    0,  311,    2, 0x08 /* Private */,
      22,    0,  312,    2, 0x08 /* Private */,
      23,    1,  313,    2, 0x08 /* Private */,
      26,    0,  316,    2, 0x08 /* Private */,
      27,    1,  317,    2, 0x08 /* Private */,
      29,    0,  320,    2, 0x08 /* Private */,
      30,    0,  321,    2, 0x08 /* Private */,
      31,    0,  322,    2, 0x08 /* Private */,
      32,    0,  323,    2, 0x08 /* Private */,
      33,    0,  324,    2, 0x08 /* Private */,
      34,    0,  325,    2, 0x08 /* Private */,
      35,    0,  326,    2, 0x08 /* Private */,
      36,    0,  327,    2, 0x08 /* Private */,
      37,    0,  328,    2, 0x08 /* Private */,
      38,    0,  329,    2, 0x08 /* Private */,
      39,    0,  330,    2, 0x08 /* Private */,
      40,    0,  331,    2, 0x08 /* Private */,
      41,    0,  332,    2, 0x08 /* Private */,
      42,    0,  333,    2, 0x08 /* Private */,
      43,    0,  334,    2, 0x08 /* Private */,
      44,    0,  335,    2, 0x08 /* Private */,
      45,    0,  336,    2, 0x08 /* Private */,
      46,    0,  337,    2, 0x08 /* Private */,
      47,    0,  338,    2, 0x08 /* Private */,
      48,    0,  339,    2, 0x08 /* Private */,
      49,    0,  340,    2, 0x08 /* Private */,
      50,    0,  341,    2, 0x08 /* Private */,
      51,    0,  342,    2, 0x08 /* Private */,
      52,    0,  343,    2, 0x08 /* Private */,
      53,    0,  344,    2, 0x08 /* Private */,
      54,    0,  345,    2, 0x08 /* Private */,
      55,    0,  346,    2, 0x08 /* Private */,
      56,    0,  347,    2, 0x08 /* Private */,
      57,    0,  348,    2, 0x08 /* Private */,
      58,    0,  349,    2, 0x08 /* Private */,
      59,    0,  350,    2, 0x08 /* Private */,
      60,    0,  351,    2, 0x08 /* Private */,
      61,    3,  352,    2, 0x08 /* Private */,
      65,    1,  359,    2, 0x08 /* Private */,
      67,    1,  362,    2, 0x08 /* Private */,
      69,    1,  365,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 3,    4,    5,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   17,
    QMetaType::Void, QMetaType::Bool,   19,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 24,   25,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   28,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::UShort, QMetaType::UShort, QMetaType::QString,   62,   63,   64,
    QMetaType::Void, QMetaType::QPoint,   66,
    QMetaType::Void, QMetaType::QString,   68,
    QMetaType::Void, QMetaType::Int,   70,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onNavSelectionChanged((*reinterpret_cast< QListWidgetItem*(*)>(_a[1])),(*reinterpret_cast< QListWidgetItem*(*)>(_a[2]))); break;
        case 1: _t->onConnectClicked(); break;
        case 2: _t->onDisconnectClicked(); break;
        case 3: _t->onReadCoilsClicked(); break;
        case 4: _t->onReadInputsClicked(); break;
        case 5: _t->onReadHoldingRegistersClicked(); break;
        case 6: _t->onReadInputRegistersClicked(); break;
        case 7: _t->onWriteSingleCoilClicked(); break;
        case 8: _t->onWriteSingleRegisterClicked(); break;
        case 9: _t->onWriteMultipleCoilsClicked(); break;
        case 10: _t->onWriteMultipleRegistersClicked(); break;
        case 11: _t->onDisplayFormatChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 12: _t->onContinuousReadToggled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 13: _t->onSocketConnected(); break;
        case 14: _t->onSocketDisconnected(); break;
        case 15: _t->onSocketReadyRead(); break;
        case 16: _t->onSocketError((*reinterpret_cast< QAbstractSocket::SocketError(*)>(_a[1]))); break;
        case 17: _t->onContinuousReadTimer(); break;
        case 18: _t->saveConnectionHistory((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 19: _t->loadConnectionHistory(); break;
        case 20: _t->saveAutoScene(); break;
        case 21: _t->loadAutoScene(); break;
        case 22: _t->onSerialOpenClicked(); break;
        case 23: _t->onSerialCloseClicked(); break;
        case 24: _t->onSerialSendClicked(); break;
        case 25: _t->onSerialRead(); break;
        case 26: _t->refreshSerialPorts(); break;
        case 27: _t->onGitSelectDirClicked(); break;
        case 28: _t->onGitRefreshBranchesClicked(); break;
        case 29: _t->onGitCheckoutClicked(); break;
        case 30: _t->onGitAddClicked(); break;
        case 31: _t->onGitCommitClicked(); break;
        case 32: _t->onGitPushClicked(); break;
        case 33: _t->onGitPullClicked(); break;
        case 34: _t->onGitMergeClicked(); break;
        case 35: _t->onGitStatusClicked(); break;
        case 36: _t->onGitOpenIgnoreClicked(); break;
        case 37: _t->onGitCheckIgnoreClicked(); break;
        case 38: _t->onGitRefreshLogClicked(); break;
        case 39: _t->onGitResetClicked(); break;
        case 40: _t->onGitCopyForDailyReportClicked(); break;
        case 41: _t->onScpTransferClicked(); break;
        case 42: _t->onClearLogClicked(); break;
        case 43: _t->onStartSimulatorClicked(); break;
        case 44: _t->onStopSimulatorClicked(); break;
        case 45: _t->onSimSetRegisterClicked(); break;
        case 46: _t->onSimSetBitClicked(); break;
        case 47: _t->onSimWriteValuesClicked(); break;
        case 48: _t->onSimRandomValuesClicked(); break;
        case 49: _t->onApplyFaultSettingsClicked(); break;
        case 50: _t->onExportHistoryClicked(); break;
        case 51: _t->onRegisterOperation((*reinterpret_cast< quint16(*)>(_a[1])),(*reinterpret_cast< quint16(*)>(_a[2])),(*reinterpret_cast< const QString(*)>(_a[3]))); break;
        case 52: _t->onSimShowContextMenu((*reinterpret_cast< const QPoint(*)>(_a[1]))); break;
        case 53: _t->onSimSetFormat((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 54: _t->onSimShowWaveformEditor((*reinterpret_cast< int(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 16:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QAbstractSocket::SocketError >(); break;
            }
            break;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 55)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 55;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 55)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 55;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
