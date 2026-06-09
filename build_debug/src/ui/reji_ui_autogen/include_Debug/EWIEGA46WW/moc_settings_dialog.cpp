/****************************************************************************
** Meta object code from reading C++ file 'settings_dialog.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../../src/ui/settings_dialog.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'settings_dialog.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_CLASSrejiSCOPESettingsDialogENDCLASS_t {};
constexpr auto qt_meta_stringdata_CLASSrejiSCOPESettingsDialogENDCLASS = QtMocHelpers::stringData(
    "reji::SettingsDialog",
    "healingModeChanged",
    "",
    "HealingMode",
    "mode",
    "editRulesRequested",
    "autoReloadToggled",
    "enabled",
    "onModeChanged",
    "index",
    "onOkClicked",
    "onEditRulesClicked",
    "onAutoReloadToggled",
    "state"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSrejiSCOPESettingsDialogENDCLASS[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   56,    2, 0x06,    1 /* Public */,
       5,    0,   59,    2, 0x06,    3 /* Public */,
       6,    1,   60,    2, 0x06,    4 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       8,    1,   63,    2, 0x08,    6 /* Private */,
      10,    0,   66,    2, 0x08,    8 /* Private */,
      11,    0,   67,    2, 0x08,    9 /* Private */,
      12,    1,   68,    2, 0x08,   10 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    7,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    9,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   13,

       0        // eod
};

Q_CONSTINIT const QMetaObject reji::SettingsDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_CLASSrejiSCOPESettingsDialogENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSrejiSCOPESettingsDialogENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSrejiSCOPESettingsDialogENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<SettingsDialog, std::true_type>,
        // method 'healingModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<HealingMode, std::false_type>,
        // method 'editRulesRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'autoReloadToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'onModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onOkClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onEditRulesClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAutoReloadToggled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>
    >,
    nullptr
} };

void reji::SettingsDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SettingsDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->healingModeChanged((*reinterpret_cast< std::add_pointer_t<HealingMode>>(_a[1]))); break;
        case 1: _t->editRulesRequested(); break;
        case 2: _t->autoReloadToggled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->onModeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onOkClicked(); break;
        case 5: _t->onEditRulesClicked(); break;
        case 6: _t->onAutoReloadToggled((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SettingsDialog::*)(HealingMode );
            if (_t _q_method = &SettingsDialog::healingModeChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SettingsDialog::*)();
            if (_t _q_method = &SettingsDialog::editRulesRequested; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SettingsDialog::*)(bool );
            if (_t _q_method = &SettingsDialog::autoReloadToggled; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *reji::SettingsDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *reji::SettingsDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSrejiSCOPESettingsDialogENDCLASS.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int reji::SettingsDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void reji::SettingsDialog::healingModeChanged(HealingMode _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void reji::SettingsDialog::editRulesRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void reji::SettingsDialog::autoReloadToggled(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
