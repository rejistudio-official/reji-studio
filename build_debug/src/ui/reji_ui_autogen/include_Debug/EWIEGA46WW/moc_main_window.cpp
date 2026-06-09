/****************************************************************************
** Meta object code from reading C++ file 'main_window.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../src/ui/main_window.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'main_window.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSMainWindowENDCLASS_t {};
constexpr auto qt_meta_stringdata_CLASSMainWindowENDCLASS = QtMocHelpers::stringData(
    "MainWindow",
    "streamStarted",
    "",
    "streamStopped",
    "sceneActivated",
    "scene_index",
    "onMetricsUpdate",
    "uint32_t",
    "bitrate_kbps",
    "fps_actual",
    "connected",
    "onHealingNotification",
    "message",
    "startStream",
    "stopStream",
    "onCutTransition",
    "onFadeTransition",
    "pollMetrics",
    "onReduceBitrate",
    "target_kbps",
    "reason",
    "addScene",
    "removeScene",
    "onSettingsClicked"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSMainWindowENDCLASS[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   98,    2, 0x06,    1 /* Public */,
       3,    0,   99,    2, 0x06,    2 /* Public */,
       4,    1,  100,    2, 0x06,    3 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       6,    3,  103,    2, 0x0a,    5 /* Public */,
      11,    1,  110,    2, 0x0a,    9 /* Public */,
      13,    0,  113,    2, 0x08,   11 /* Private */,
      14,    0,  114,    2, 0x08,   12 /* Private */,
      15,    0,  115,    2, 0x08,   13 /* Private */,
      16,    0,  116,    2, 0x08,   14 /* Private */,
      17,    0,  117,    2, 0x08,   15 /* Private */,
      18,    2,  118,    2, 0x08,   16 /* Private */,
      21,    0,  123,    2, 0x08,   19 /* Private */,
      22,    0,  124,    2, 0x08,   20 /* Private */,
      23,    0,  125,    2, 0x08,   21 /* Private */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    5,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 7, QMetaType::Float, QMetaType::Bool,    8,    9,   10,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7, QMetaType::QString,   19,   20,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_CLASSMainWindowENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSMainWindowENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSMainWindowENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'streamStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'streamStopped'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sceneActivated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onMetricsUpdate'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onHealingNotification'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'startStream'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stopStream'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCutTransition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onFadeTransition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'pollMetrics'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onReduceBitrate'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'addScene'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'removeScene'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onSettingsClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->streamStarted(); break;
        case 1: _t->streamStopped(); break;
        case 2: _t->sceneActivated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->onMetricsUpdate((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 4: _t->onHealingNotification((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 5: _t->startStream(); break;
        case 6: _t->stopStream(); break;
        case 7: _t->onCutTransition(); break;
        case 8: _t->onFadeTransition(); break;
        case 9: _t->pollMetrics(); break;
        case 10: _t->onReduceBitrate((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2]))); break;
        case 11: _t->addScene(); break;
        case 12: _t->removeScene(); break;
        case 13: _t->onSettingsClicked(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MainWindow::*)();
            if (_t _q_method = &MainWindow::streamStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)();
            if (_t _q_method = &MainWindow::streamStopped; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MainWindow::*)(int );
            if (_t _q_method = &MainWindow::sceneActivated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSMainWindowENDCLASS.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void MainWindow::streamStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void MainWindow::streamStopped()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void MainWindow::sceneActivated(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
