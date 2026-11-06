#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QVariantMap>
#include <memory>

#include "store.h"

// The window's view of the Store. Reads run on a small thread pool and writes
// run one at a time in order, so a slow command (a theme switch, a polkit
// prompt) never freezes the window, and two changes never race each other.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantMap lists READ lists NOTIFY listsChanged)
    Q_PROPERTY(QStringList busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QVariantList index READ index CONSTANT)

public:
    explicit Backend(std::shared_ptr<Store> store, QObject *parent = nullptr);
    ~Backend() override;

    QVariantMap values() const { return m_values; }
    QVariantMap lists() const { return m_lists; }
    QStringList busy() const { return QStringList(m_busy.begin(), m_busy.end()); }
    QString message() const { return m_message; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QVariantList index() const;

    // Describes one setting for its row: type, range, fixed choices.
    Q_INVOKABLE QVariantMap meta(const QString &id) const;

    Q_INVOKABLE void refreshPane(const QString &pane);
    Q_INVOKABLE void refresh(const QString &id);
    Q_INVOKABLE void load(const QString &list);
    Q_INVOKABLE void set(const QString &id, const QVariant &value);
    // Runs an action, then reloads the named lists and the pane's settings.
    Q_INVOKABLE void act(const QString &action, const QVariantList &args, const QStringList &reload = {},
                         const QString &pane = {});
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void dismissMessage();
    // What Settings saved for a display, or null, so a change can be undone.
    Q_INVOKABLE QVariant savedMonitor(const QString &name) const;

    // Waits for every queued change; used before quitting.
    void finish();

signals:
    void valuesChanged();
    void listsChanged();
    void busyChanged();
    void messageChanged();
    void historyChanged();
    // An action finished; `error` is empty when it worked.
    void actionFinished(const QString &action, const QString &error);

private:
    struct Change { QString id; QVariant before; QVariant after; };
    void write(const QString &id, const QVariant &value, bool record);
    void setValue(const QString &id, const QVariant &value);
    void setBusy(const QString &key, bool busy);
    void fail(const QString &error);

    std::shared_ptr<Store> m_store;
    QThreadPool m_reads, m_writes;
    QVariantMap m_values, m_lists;
    QSet<QString> m_busy;
    QHash<QString, QVariant> m_pending; // newest queued value per setting
    QMutex m_pendingLock;
    QHash<QString, int> m_writing;      // queued writes per setting
    QString m_message;
    QList<Change> m_undo, m_redo;
    QString m_lastId;
    QElapsedTimer m_lastChange;
};
