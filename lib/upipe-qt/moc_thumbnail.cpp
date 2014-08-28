/****************************************************************************
** Meta object code from reading C++ file 'thumbnail.h'
**
** Created: Thu Aug 28 14:58:53 2014
**      by: The Qt Meta Object Compiler version 63 (Qt 4.8.4)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "thumbnail.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'thumbnail.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.4. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_Thumbnail[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: signature, parameters, type, tag, flags
      11,   10,   10,   10, 0x05,

 // slots: signature, parameters, type, tag, flags
      22,   10,   10,   10, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_Thumbnail[] = {
    "Thumbnail\0\0finished()\0render()\0"
};

void Thumbnail::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        Thumbnail *_t = static_cast<Thumbnail *>(_o);
        switch (_id) {
        case 0: _t->finished(); break;
        case 1: _t->render(); break;
        default: ;
        }
    }
    Q_UNUSED(_a);
}

const QMetaObjectExtraData Thumbnail::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject Thumbnail::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_Thumbnail,
      qt_meta_data_Thumbnail, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &Thumbnail::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *Thumbnail::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *Thumbnail::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_Thumbnail))
        return static_cast<void*>(const_cast< Thumbnail*>(this));
    return QObject::qt_metacast(_clname);
}

int Thumbnail::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void Thumbnail::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 0, 0);
}
QT_END_MOC_NAMESPACE
