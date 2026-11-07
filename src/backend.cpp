#include "backend.h"

#include "monitorlayout.h"

#include <QJsonObject>
#include <QPointer>

Backend::Backend(std::shared_ptr<Store> store, QObject *parent) : QObject(parent), m_store(std::move(store)) {
    m_reads.setMaxThreadCount(4);
    m_writes.setMaxThreadCount(1);
}

Backend::~Backend() {
    finish();
}

void Backend::finish() {
    m_writes.waitForDone();
    m_reads.waitForDone();
}

QVariantList Backend::index() const {
    QVariantList out;
    for (const Setting &s : m_store->settings())
        out << QVariantMap{{"id", s.id}, {"pane", s.pane}, {"label", s.label}};
    return out;
}

QVariantMap Backend::meta(const QString &id) const {
    const Setting *s = m_store->setting(id);
    if (!s)
        return {};
    return {{"type", s->type}, {"label", s->label}, {"min", s->min}, {"max", s->max}, {"step", s->step},
            {"choices", s->choices}, {"choicesList", s->choicesList}};
}

QVariant Backend::savedMonitor(const QString &key, const QString &connector) const {
    const QJsonObject monitors = m_store->managedState().value("monitors").toObject();
    QJsonValue v = monitors.value(key);
    if (!v.isObject() && !connector.isEmpty())
        v = monitors.value(connector);
    return v.isObject() ? v.toVariant() : QVariant();
}

QVariantList Backend::placeMonitor(const QVariantList &rects, int moved, int x, int y, int snap) const {
    QList<QRect> boxes;
    for (const QVariant &r : rects) {
        const QVariantMap m = r.toMap();
        boxes << QRect(qRound(m.value("x").toDouble()), qRound(m.value("y").toDouble()),
                       qRound(m.value("width").toDouble()), qRound(m.value("height").toDouble()));
    }
    QVariantList out;
    for (const QPoint &p : Layout::place(boxes, moved, QPoint(x, y), qMax(0, snap)))
        out << QVariantMap{{"x", p.x()}, {"y", p.y()}};
    return out;
}

void Backend::setValue(const QString &id, const QVariant &value) {
    if (m_values.value(id) == value && m_values.contains(id))
        return;
    m_values.insert(id, value);
    emit valuesChanged();
}

void Backend::setBusy(const QString &key, bool busy) {
    const bool changed = busy ? !m_busy.contains(key) : m_busy.contains(key);
    if (busy)
        m_busy.insert(key);
    else
        m_busy.remove(key);
    if (changed)
        emit busyChanged();
}

void Backend::fail(const QString &error) {
    m_message = error;
    emit messageChanged();
}

void Backend::dismissMessage() {
    if (m_message.isEmpty())
        return;
    m_message.clear();
    emit messageChanged();
}

void Backend::refresh(const QString &id) {
    QPointer<Backend> self(this);
    auto store = m_store;
    m_reads.start([self, store, id] {
        const QVariant value = store->read(id);
        QMetaObject::invokeMethod(self.data(), [self, id, value] {
            // A queued change wins over a read that started before it.
            if (self && !self->m_writing.value(id))
                self->setValue(id, value);
        }, Qt::QueuedConnection);
    });
}

void Backend::refreshPane(const QString &pane) {
    for (const Setting &s : m_store->settings())
        if (s.pane == pane)
            refresh(s.id);
}

void Backend::load(const QString &list) {
    const QString key = "list:" + list;
    if (m_busy.contains(key))
        return;
    setBusy(key, true);
    QPointer<Backend> self(this);
    auto store = m_store;
    m_reads.start([self, store, list, key] {
        const QVariant data = store->list(list);
        QMetaObject::invokeMethod(self.data(), [self, list, key, data] {
            if (!self)
                return;
            self->m_lists.insert(list, data);
            emit self->listsChanged();
            self->setBusy(key, false);
        }, Qt::QueuedConnection);
    });
}

void Backend::set(const QString &id, const QVariant &value) {
    write(id, value, true);
}

void Backend::write(const QString &id, const QVariant &value, bool record) {
    if (!m_store->setting(id))
        return fail(tr("Unknown setting %1").arg(id));
    const QVariant before = m_values.value(id);
    if (before == value && m_values.contains(id) && !m_writing.value(id))
        return;

    if (record) {
        // A slider drag or a run of key presses on one setting is one change
        // to undo, not dozens.
        const bool merge = !m_undo.isEmpty() && m_lastId == id && m_lastChange.isValid() && m_lastChange.elapsed() < 1500;
        if (merge)
            m_undo.last().after = value;
        else
            m_undo << Change{id, before, value};
        m_redo.clear();
        m_lastId = id;
        m_lastChange.restart();
        emit historyChanged();
    }

    // Show the new value straight away; it is taken back if the write fails.
    setValue(id, value);
    setBusy(id, true);
    dismissMessage();

    // Writes to one setting coalesce: a queued write uses the newest value,
    // so dragging a slider sends only what is current when it runs.
    {
        QMutexLocker lock(&m_pendingLock);
        const bool queued = m_pending.contains(id);
        m_pending.insert(id, value);
        if (queued)
            return;
    }
    m_writing[id]++;

    QPointer<Backend> self(this);
    auto store = m_store;
    m_writes.start([self, store, id, before] {
        QVariant latest;
        {
            if (!self)
                return;
            QMutexLocker lock(&self->m_pendingLock);
            latest = self->m_pending.take(id);
        }
        const QString error = store->write(id, latest);
        const QVariant actual = store->read(id);
        QMetaObject::invokeMethod(self.data(), [self, id, before, latest, error, actual] {
            if (!self)
                return;
            if (--self->m_writing[id] <= 0)
                self->m_writing.remove(id);
            bool more;
            {
                QMutexLocker lock(&self->m_pendingLock);
                more = self->m_pending.contains(id);
            }
            if (!error.isEmpty()) {
                self->fail(error);
                if (!self->m_undo.isEmpty() && self->m_undo.last().id == id && self->m_undo.last().after == latest) {
                    self->m_undo.removeLast();
                    emit self->historyChanged();
                }
            }
            // Show what the system really has now, unless a newer change is
            // still on its way.
            if (!more) {
                self->setValue(id, actual.isValid() ? actual : before);
                self->setBusy(id, false);
            }
        }, Qt::QueuedConnection);
    });
}

void Backend::act(const QString &action, const QVariantList &args, const QStringList &reload, const QString &pane) {
    setBusy(action, true);
    dismissMessage();
    QPointer<Backend> self(this);
    auto store = m_store;
    m_writes.start([self, store, action, args, reload, pane] {
        const QString error = store->act(action, args);
        QMetaObject::invokeMethod(self.data(), [self, action, error, reload, pane] {
            if (!self)
                return;
            self->setBusy(action, false);
            if (!error.isEmpty())
                self->fail(error);
            for (const QString &list : reload)
                self->load(list);
            if (!pane.isEmpty())
                self->refreshPane(pane);
            emit self->actionFinished(action, error);
        }, Qt::QueuedConnection);
    });
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const Change c = m_undo.takeLast();
    m_redo << c;
    m_lastId.clear();
    emit historyChanged();
    write(c.id, c.before, false);
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const Change c = m_redo.takeLast();
    m_undo << c;
    m_lastId.clear();
    emit historyChanged();
    write(c.id, c.after, false);
}
