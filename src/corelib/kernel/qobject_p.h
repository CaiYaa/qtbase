/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
** Copyright (C) 2013 Olivier Goffart <ogoffart@woboq.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QOBJECT_P_H
#define QOBJECT_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of qapplication_*.cpp, qwidget*.cpp and qfiledialog.cpp.  This header
// file may change from version to version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/private/qglobal_p.h>
#include "QtCore/qobject.h"
#include "QtCore/qpointer.h"
#include "QtCore/qsharedpointer.h"
#include "QtCore/qcoreevent.h"
#include "QtCore/qlist.h"
#include "QtCore/qvector.h"
#include "QtCore/qvariant.h"
#include "QtCore/qreadwritelock.h"

QT_BEGIN_NAMESPACE

class QVariant;
class QThreadData;
class QObjectConnectionListVector;
namespace QtSharedPointer { struct ExternalRefCountData; }

/* for Qt Test */
struct QSignalSpyCallbackSet
{
    typedef void (*BeginCallback)(QObject *caller, int signal_or_method_index, void **argv);
    typedef void (*EndCallback)(QObject *caller, int signal_or_method_index);
    BeginCallback signal_begin_callback,
                    slot_begin_callback;
    EndCallback signal_end_callback,
                slot_end_callback;
};
void Q_CORE_EXPORT qt_register_signal_spy_callbacks(QSignalSpyCallbackSet *callback_set);

extern Q_CORE_EXPORT QBasicAtomicPointer<QSignalSpyCallbackSet> qt_signal_spy_callback_set;

enum { QObjectPrivateVersion = QT_VERSION };

class Q_CORE_EXPORT QAbstractDeclarativeData
{
public:
    static void (*destroyed)(QAbstractDeclarativeData *, QObject *);
    static void (*destroyed_qml1)(QAbstractDeclarativeData *, QObject *);
    static void (*parentChanged)(QAbstractDeclarativeData *, QObject *, QObject *);
    static void (*signalEmitted)(QAbstractDeclarativeData *, QObject *, int, void **);
    static int  (*receivers)(QAbstractDeclarativeData *, const QObject *, int);
    static bool (*isSignalConnected)(QAbstractDeclarativeData *, const QObject *, int);
    static void (*setWidgetParent)(QObject *, QObject *); // Used by the QML engine to specify parents for widgets. Set by QtWidgets.
};

// This is an implementation of QAbstractDeclarativeData that is identical with
// the implementation in QtDeclarative and QtQml for the first bit
struct QAbstractDeclarativeDataImpl : public QAbstractDeclarativeData
{
    quint32 ownedByQml1:1;
    quint32 unused: 31;
};

class Q_CORE_EXPORT QObjectPrivate : public QObjectData
{
    Q_DECLARE_PUBLIC(QObject)

public:
    struct ExtraData
    {
        ExtraData() {}
    #ifndef QT_NO_USERDATA
        QVector<QObjectUserData *> userData;
    #endif
        QList<QByteArray> propertyNames;
        QVector<QVariant> propertyValues;
        QVector<int> runningTimers;
        QList<QPointer<QObject> > eventFilters;
        QString objectName;
    };

    typedef void (*StaticMetaCallFunction)(QObject *, QMetaObject::Call, int, void **);
    struct Connection;
    struct SignalVector;

    struct ConnectionOrSignalVector {
        union {
            // linked list of orphaned connections that need cleaning up
            ConnectionOrSignalVector *nextInOrphanList;
            // linked list of connections connected to slots in this object
            Connection *next;
        };

        static SignalVector *asSignalVector(ConnectionOrSignalVector *c) {
            if (reinterpret_cast<quintptr>(c) & 1)
                return reinterpret_cast<SignalVector *>(reinterpret_cast<quintptr>(c) & ~quintptr(1u));
            return nullptr;
        }
        static Connection *fromSignalVector(SignalVector *v) {
            return reinterpret_cast<Connection *>(reinterpret_cast<quintptr>(v) | quintptr(1u));
        }
    };

    struct Connection : public ConnectionOrSignalVector
    {
        // linked list of connections connected to slots in this object, next is in base class
        Connection **prev;
        // linked list of connections connected to signals in this object
        Connection *nextConnectionList;
        Connection *prevConnectionList;

        QObject *sender;
        QObject *receiver;
        union {
            StaticMetaCallFunction callFunction;
            QtPrivate::QSlotObjectBase *slotObj;
        };
        QAtomicPointer<const int> argumentTypes;
        QAtomicInt ref_;
        uint id = 0;
        ushort method_offset;
        ushort method_relative;
        int signal_index : 27; // In signal range (see QObjectPrivate::signalIndex())
        ushort connectionType : 3; // 0 == auto, 1 == direct, 2 == queued, 4 == blocking
        ushort isSlotObject : 1;
        ushort ownArgumentTypes : 1;
        Connection() : nextConnectionList(nullptr), ref_(2), ownArgumentTypes(true) {
            //ref_ is 2 for the use in the internal lists, and for the use in QMetaObject::Connection
        }
        ~Connection();
        int method() const { Q_ASSERT(!isSlotObject); return method_offset + method_relative; }
        void ref() { ref_.ref(); }
        void freeSlotObject()
        {
            if (isSlotObject) {
                slotObj->destroyIfLastRef();
                isSlotObject = false;
            }
        }
        void deref() {
            if (!ref_.deref()) {
                Q_ASSERT(!receiver);
                Q_ASSERT(!isSlotObject);
                delete this;
            }
        }
    };
    // ConnectionList is a singly-linked list
    struct ConnectionList {
        ConnectionList() : first(nullptr), last(nullptr) {}
        Connection *first;
        Connection *last;
    };

    struct Sender
    {
        Sender(QObject *receiver, QObject *sender, int signal)
            : receiver(receiver), sender(sender), signal(signal)
        {
            if (receiver) {
                ConnectionData *cd = receiver->d_func()->connections.load();
                previous = cd->currentSender;
                cd->currentSender = this;
            }
        }
        ~Sender()
        {
            if (receiver)
                receiver->d_func()->connections.load()->currentSender = previous;
        }
        void receiverDeleted()
        {
            Sender *s = this;
            while (s) {
                s->receiver = nullptr;
                s = s->previous;
            }
        }
        Sender *previous;
        QObject *receiver;
        QObject *sender;
        int signal;
    };

    struct SignalVector : public ConnectionOrSignalVector {
        quintptr allocated;
        // ConnectionList signals[]
        ConnectionList &at(int i)
        {
            return reinterpret_cast<ConnectionList *>(this + 1)[i + 1];
        }
        const ConnectionList &at(int i) const
        {
            return reinterpret_cast<const ConnectionList *>(this + 1)[i + 1];
        }
        int count() { return static_cast<int>(allocated); }
    };



    /*
        This contains the all connections from and to an object.

        The signalVector contains the lists of connections for a given signal. The index in the vector correspond
        to the signal index. The signal index is the one returned by QObjectPrivate::signalIndex (not
        QMetaObject::indexOfSignal). allsignals contains a list of special connections that will get invoked on
        any signal emission. This is done by connecting to signal index -1.

        This vector is protected by the object mutex (signalSlotLock())

        Each Connection is also part of a 'senders' linked list. This one contains all connections connected
        to a slot in this object. The mutex of the receiver must be locked when touching the pointers of this
        linked list.
    */
    struct ConnectionData {
        // the id below is used to avoid activating new connections. When the object gets
        // deleted it's set to 0, so that signal emission stops
        QAtomicInteger<uint> currentConnectionId;
        struct Ref {
            int _ref = 0;
            void ref() { ++_ref; }
            int deref() { return --_ref; }
            operator int() const { return _ref; }
        };

        Ref ref;
        SignalVector *signalVector = nullptr;
        Connection *senders = nullptr;
        Sender *currentSender = nullptr;   // object currently activating the object
        QAtomicPointer<Connection> orphaned;

        ~ConnectionData()
        {
            deleteOrphaned(orphaned.load());
            if (signalVector)
                free(signalVector);
        }

        // must be called on the senders connection data
        // assumes the senders and receivers lock are held
        void removeConnection(Connection *c)
        {
            Q_ASSERT(c->receiver);
            ConnectionList &connections = signalVector->at(c->signal_index);
            c->receiver = nullptr;

#ifndef QT_NO_DEBUG
            bool found = false;
            for (Connection *cc = connections.first; cc; cc = cc->nextConnectionList) {
                if (cc == c) {
                    found = true;
                    break;
                }
            }
            Q_ASSERT(found);
#endif

            // remove from the senders linked list
            *c->prev = c->next;
            if (c->next)
                c->next->prev = c->prev;
            c->prev = nullptr;

            if (connections.first == c)
                connections.first = c->nextConnectionList;
            if (connections.last == c)
                connections.last = c->prevConnectionList;
            Q_ASSERT(signalVector->at(c->signal_index).first != c);
            Q_ASSERT(signalVector->at(c->signal_index).last != c);

            // keep c->nextConnectionList intact, as it might still get accessed by activate
            if (c->nextConnectionList)
                c->nextConnectionList->prevConnectionList = c->prevConnectionList;
            if (c->prevConnectionList)
                c->prevConnectionList->nextConnectionList = c->nextConnectionList;
            c->prevConnectionList = nullptr;

            Q_ASSERT(c != orphaned.load());
            // add c to orphanedConnections
            c->nextInOrphanList = orphaned.load();
            orphaned.store(c);

#ifndef QT_NO_DEBUG
            found = false;
            for (Connection *cc = connections.first; cc; cc = cc->nextConnectionList) {
                if (cc == c) {
                    found = true;
                    break;
                }
            }
            Q_ASSERT(!found);
#endif

        }
        void cleanOrphanedConnections(QObject *sender)
        {
            if (orphaned.load() && ref == 1)
                cleanOrphanedConnectionsImpl(sender);
        }
        void cleanOrphanedConnectionsImpl(QObject *sender);

        ConnectionList &connectionsForSignal(int signal)
        {
            return signalVector->at(signal);
        }

        void resizeSignalVector(uint size) {
            if (signalVector && signalVector->allocated > size)
                return;
            size = (size + 7) & ~7;
            SignalVector *v = reinterpret_cast<SignalVector *>(malloc(sizeof(SignalVector) + (size + 1) * sizeof(ConnectionList)));
            int start = -1;
            if (signalVector) {
                memcpy(v, signalVector, sizeof(SignalVector) + (signalVector->allocated + 1) * sizeof(ConnectionList));
                start = signalVector->count();
            }
            for (int i = start; i < int(size); ++i)
                v->at(i) = ConnectionList();
            v->next = nullptr;
            v->allocated = size;

            qSwap(v, signalVector);
            if (v) {
                v->next = orphaned.load();
                orphaned.store(ConnectionOrSignalVector::fromSignalVector(v));
            }
        }
        int signalVectorCount() const {
            return  signalVector ? signalVector->count() : -1;
        }

        static void deleteOrphaned(ConnectionOrSignalVector *c);
    };

    QObjectPrivate(int version = QObjectPrivateVersion);
    virtual ~QObjectPrivate();
    void deleteChildren();

    void setParent_helper(QObject *);
    void moveToThread_helper();
    void setThreadData_helper(QThreadData *currentData, QThreadData *targetData);
    void _q_reregisterTimers(void *pointer);

    bool isSender(const QObject *receiver, const char *signal) const;
    QObjectList receiverList(const char *signal) const;
    QObjectList senderList() const;

    void addConnection(int signal, Connection *c);

    static QObjectPrivate *get(QObject *o) {
        return o->d_func();
    }
    static const QObjectPrivate *get(const QObject *o) { return o->d_func(); }

    int signalIndex(const char *signalName, const QMetaObject **meta = nullptr) const;
    bool isSignalConnected(uint signalIdx, bool checkDeclarative = true) const;
    bool maybeSignalConnected(uint signalIndex) const;
    inline bool isDeclarativeSignalConnected(uint signalIdx) const;

    // To allow abitrary objects to call connectNotify()/disconnectNotify() without making
    // the API public in QObject. This is used by QQmlNotifierEndpoint.
    inline void connectNotify(const QMetaMethod &signal);
    inline void disconnectNotify(const QMetaMethod &signal);

    template <typename Func1, typename Func2>
    static inline QMetaObject::Connection connect(const typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal,
                                                  const typename QtPrivate::FunctionPointer<Func2>::Object *receiverPrivate, Func2 slot,
                                                  Qt::ConnectionType type = Qt::AutoConnection);

    template <typename Func1, typename Func2>
    static inline bool disconnect(const typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal,
                                  const typename QtPrivate::FunctionPointer<Func2>::Object *receiverPrivate, Func2 slot);

    static QMetaObject::Connection connectImpl(const QObject *sender, int signal_index,
                                               const QObject *receiver, void **slot,
                                               QtPrivate::QSlotObjectBase *slotObj, Qt::ConnectionType type,
                                               const int *types, const QMetaObject *senderMetaObject);
    static QMetaObject::Connection connect(const QObject *sender, int signal_index, QtPrivate::QSlotObjectBase *slotObj, Qt::ConnectionType type);
    static bool disconnect(const QObject *sender, int signal_index, void **slot);

    void ensureConnectionData()
    {
        if (connections.load())
            return;
        ConnectionData *cd = new ConnectionData;
        cd->ref.ref();
        connections.store(cd);
    }
public:
    ExtraData *extraData;    // extra data set by the user
    QThreadData *threadData; // id of the thread that owns the object

    using ConnectionDataPointer = QExplicitlySharedDataPointer<ConnectionData>;
    QAtomicPointer<ConnectionData> connections;

    union {
        QObject *currentChildBeingDeleted; // should only be used when QObjectData::isDeletingChildren is set
        QAbstractDeclarativeData *declarativeData; //extra data used by the declarative module
    };

    // these objects are all used to indicate that a QObject was deleted
    // plus QPointer, which keeps a separate list
    QAtomicPointer<QtSharedPointer::ExternalRefCountData> sharedRefcount;
};

Q_DECLARE_TYPEINFO(QObjectPrivate::ConnectionList, Q_MOVABLE_TYPE);

inline bool QObjectPrivate::isDeclarativeSignalConnected(uint signal_index) const
{
    return declarativeData && QAbstractDeclarativeData::isSignalConnected
            && QAbstractDeclarativeData::isSignalConnected(declarativeData, q_func(), signal_index);
}

inline void QObjectPrivate::connectNotify(const QMetaMethod &signal)
{
    q_ptr->connectNotify(signal);
}

inline void QObjectPrivate::disconnectNotify(const QMetaMethod &signal)
{
    q_ptr->disconnectNotify(signal);
}

namespace QtPrivate {
template<typename Func, typename Args, typename R> class QPrivateSlotObject : public QSlotObjectBase
{
    typedef QtPrivate::FunctionPointer<Func> FuncType;
    Func function;
    static void impl(int which, QSlotObjectBase *this_, QObject *r, void **a, bool *ret)
    {
        switch (which) {
            case Destroy:
                delete static_cast<QPrivateSlotObject*>(this_);
                break;
            case Call:
                FuncType::template call<Args, R>(static_cast<QPrivateSlotObject*>(this_)->function,
                                                 static_cast<typename FuncType::Object *>(QObjectPrivate::get(r)), a);
                break;
            case Compare:
                *ret = *reinterpret_cast<Func *>(a) == static_cast<QPrivateSlotObject*>(this_)->function;
                break;
            case NumOperations: ;
        }
    }
public:
    explicit QPrivateSlotObject(Func f) : QSlotObjectBase(&impl), function(f) {}
};
} //namespace QtPrivate

template <typename Func1, typename Func2>
inline QMetaObject::Connection QObjectPrivate::connect(const typename QtPrivate::FunctionPointer<Func1>::Object *sender, Func1 signal,
                                                       const typename QtPrivate::FunctionPointer<Func2>::Object *receiverPrivate, Func2 slot,
                                                       Qt::ConnectionType type)
{
    typedef QtPrivate::FunctionPointer<Func1> SignalType;
    typedef QtPrivate::FunctionPointer<Func2> SlotType;
    Q_STATIC_ASSERT_X(QtPrivate::HasQ_OBJECT_Macro<typename SignalType::Object>::Value,
                      "No Q_OBJECT in the class with the signal");

    //compilation error if the arguments does not match.
    Q_STATIC_ASSERT_X(int(SignalType::ArgumentCount) >= int(SlotType::ArgumentCount),
                      "The slot requires more arguments than the signal provides.");
    Q_STATIC_ASSERT_X((QtPrivate::CheckCompatibleArguments<typename SignalType::Arguments, typename SlotType::Arguments>::value),
                      "Signal and slot arguments are not compatible.");
    Q_STATIC_ASSERT_X((QtPrivate::AreArgumentsCompatible<typename SlotType::ReturnType, typename SignalType::ReturnType>::value),
                      "Return type of the slot is not compatible with the return type of the signal.");

    const int *types = nullptr;
    if (type == Qt::QueuedConnection || type == Qt::BlockingQueuedConnection)
        types = QtPrivate::ConnectionTypes<typename SignalType::Arguments>::types();

    return QObject::connectImpl(sender, reinterpret_cast<void **>(&signal),
        receiverPrivate->q_ptr, reinterpret_cast<void **>(&slot),
        new QtPrivate::QPrivateSlotObject<Func2, typename QtPrivate::List_Left<typename SignalType::Arguments, SlotType::ArgumentCount>::Value,
                                        typename SignalType::ReturnType>(slot),
        type, types, &SignalType::Object::staticMetaObject);
}

template <typename Func1, typename Func2>
bool QObjectPrivate::disconnect(const typename QtPrivate::FunctionPointer< Func1 >::Object* sender, Func1 signal,
                                const typename QtPrivate::FunctionPointer< Func2 >::Object* receiverPrivate, Func2 slot)
{
    typedef QtPrivate::FunctionPointer<Func1> SignalType;
    typedef QtPrivate::FunctionPointer<Func2> SlotType;
    Q_STATIC_ASSERT_X(QtPrivate::HasQ_OBJECT_Macro<typename SignalType::Object>::Value,
                      "No Q_OBJECT in the class with the signal");
    //compilation error if the arguments does not match.
    Q_STATIC_ASSERT_X((QtPrivate::CheckCompatibleArguments<typename SignalType::Arguments, typename SlotType::Arguments>::value),
                      "Signal and slot arguments are not compatible.");
    return QObject::disconnectImpl(sender, reinterpret_cast<void **>(&signal),
                          receiverPrivate->q_ptr, reinterpret_cast<void **>(&slot),
                          &SignalType::Object::staticMetaObject);
}

Q_DECLARE_TYPEINFO(QObjectPrivate::Connection, Q_MOVABLE_TYPE);
Q_DECLARE_TYPEINFO(QObjectPrivate::Sender, Q_MOVABLE_TYPE);

class QSemaphore;
class Q_CORE_EXPORT QAbstractMetaCallEvent : public QEvent
{
public:
    QAbstractMetaCallEvent(const QObject *sender, int signalId, QSemaphore *semaphore = nullptr)
        : QEvent(MetaCall), signalId_(signalId), sender_(sender), semaphore_(semaphore)
    {}
    ~QAbstractMetaCallEvent();

    virtual void placeMetaCall(QObject *object) = 0;

    inline const QObject *sender() const { return sender_; }
    inline int signalId() const { return signalId_; }

private:
    int signalId_;
    const QObject *sender_;
    QSemaphore *semaphore_;
};

class Q_CORE_EXPORT QMetaCallEvent : public QAbstractMetaCallEvent
{
public:
    QMetaCallEvent(ushort method_offset, ushort method_relative, QObjectPrivate::StaticMetaCallFunction callFunction , const QObject *sender, int signalId,
                   int nargs = 0, int *types = nullptr, void **args = nullptr, QSemaphore *semaphore = nullptr);
    /*! \internal
        \a signalId is in the signal index range (see QObjectPrivate::signalIndex()).
    */
    QMetaCallEvent(QtPrivate::QSlotObjectBase *slotObj, const QObject *sender, int signalId,
                   int nargs = 0, int *types = nullptr, void **args = nullptr, QSemaphore *semaphore = nullptr);

    ~QMetaCallEvent() override;

    inline int id() const { return method_offset_ + method_relative_; }
    inline void **args() const { return args_; }

    virtual void placeMetaCall(QObject *object) override;

private:
    QtPrivate::QSlotObjectBase *slotObj_;
    int nargs_;
    int *types_;
    void **args_;
    QObjectPrivate::StaticMetaCallFunction callFunction_;
    ushort method_offset_;
    ushort method_relative_;
};

class QBoolBlocker
{
    Q_DISABLE_COPY_MOVE(QBoolBlocker)
public:
    explicit inline QBoolBlocker(bool &b, bool value=true):block(b), reset(b){block = value;}
    inline ~QBoolBlocker(){block = reset; }
private:
    bool &block;
    bool reset;
};

void Q_CORE_EXPORT qDeleteInEventHandler(QObject *o);

struct QAbstractDynamicMetaObject;
struct Q_CORE_EXPORT QDynamicMetaObjectData
{
    virtual ~QDynamicMetaObjectData();
    virtual void objectDestroyed(QObject *) { delete this; }

    virtual QAbstractDynamicMetaObject *toDynamicMetaObject(QObject *) = 0;
    virtual int metaCall(QObject *, QMetaObject::Call, int _id, void **) = 0;
};

struct Q_CORE_EXPORT QAbstractDynamicMetaObject : public QDynamicMetaObjectData, public QMetaObject
{
    ~QAbstractDynamicMetaObject();

    QAbstractDynamicMetaObject *toDynamicMetaObject(QObject *) override { return this; }
    virtual int createProperty(const char *, const char *) { return -1; }
    int metaCall(QObject *, QMetaObject::Call c, int _id, void **a) override
    { return metaCall(c, _id, a); }
    virtual int metaCall(QMetaObject::Call, int _id, void **) { return _id; } // Compat overload
};

QT_END_NAMESPACE

#endif // QOBJECT_P_H
