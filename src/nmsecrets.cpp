#include "nmsecrets.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVariantMap>
#include <mutex>

// A connection as NetworkManager hands it over: setting name → its properties.
using NMSettings = QMap<QString, QVariantMap>;
using StringMap = QMap<QString, QString>;

static const QString nmService = QStringLiteral("org.freedesktop.NetworkManager");
static const QString nmConnection = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");

static QDBusMessage nmCall(const QString &path, const QString &interface, const QString &method,
                           const QVariantList &args = {}) {
    QDBusMessage message = QDBusMessage::createMethodCall(nmService, path, interface, method);
    message.setArguments(args);
    return QDBusConnection::systemBus().call(message, QDBus::Block, 15000);
}

QString writeNmSecret(const QString &uuid, const QString &setting, const QString &key, const QString &value) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        qDBusRegisterMetaType<NMSettings>();
        qDBusRegisterMetaType<StringMap>();
    });
    // An error may quote what it was given; never show the secret.
    const auto failure = [&value](const QDBusMessage &reply) {
        QString text = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
        if (!value.isEmpty())
            text.replace(value, QStringLiteral("…"));
        return text.isEmpty() ? QStringLiteral("NetworkManager did not answer") : text;
    };

    QDBusMessage reply = nmCall("/org/freedesktop/NetworkManager/Settings",
                                "org.freedesktop.NetworkManager.Settings", "GetConnectionByUuid", {uuid});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return failure(reply);
    const QString path = reply.arguments().at(0).value<QDBusObjectPath>().path();

    reply = nmCall(path, nmConnection, "GetSettings");
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return failure(reply);
    NMSettings settings = qdbus_cast<NMSettings>(reply.arguments().at(0));
    if (settings.isEmpty())
        return failure(reply);

    // GetSettings leaves secrets out, and an update that brings any secret
    // replaces them all, so the ones already saved go back in with it.
    StringMap vpnSecrets;
    for (const QString &name : {QStringLiteral("802-11-wireless-security"), QStringLiteral("802-1x"), QStringLiteral("vpn")}) {
        if (!settings.contains(name))
            continue;
        const QDBusMessage secrets = nmCall(path, nmConnection, "GetSecrets", {name});
        if (secrets.type() != QDBusMessage::ReplyMessage || secrets.arguments().isEmpty())
            continue; // Nothing saved for it.
        const QVariantMap saved = qdbus_cast<NMSettings>(secrets.arguments().at(0)).value(name);
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            if (name == "vpn" && it.key() == "secrets")
                vpnSecrets = qdbus_cast<StringMap>(it.value());
            else
                settings[name].insert(it.key(), it.value());
        }
    }
    if (setting == "vpn") {
        vpnSecrets.insert(key, value);
    } else {
        settings[setting].insert(key, value);
    }
    if (!vpnSecrets.isEmpty())
        settings["vpn"].insert("secrets", QVariant::fromValue(vpnSecrets));

    // Update saves to disk, as `save persistent` did.
    reply = nmCall(path, nmConnection, "Update", {QVariant::fromValue(settings)});
    if (reply.type() != QDBusMessage::ReplyMessage)
        return failure(reply);
    return {};
}

bool scrubNmcliHistory(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QList<QByteArray> lines = file.readAll().split('\n');
    file.close();

    // The history is a key file: a group for each connection, and its
    // commands numbered in order. A secret is the line after a bare `set`
    // of a secret property (how Settings gave it), or on the `set` line;
    // either way the `set` goes too.
    static const QRegularExpression setSecret(
        R"(^\d+=set\s+((802-11-wireless-security|wifi-sec)\.(psk|wep-key[0-3]|leap-password)|vpn\.secrets)\s*(.*)$)");
    static const QRegularExpression entry(R"(^\d+=)");
    QList<QByteArray> kept;
    bool changed = false, dropNext = false;
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw);
        if (dropNext) {
            dropNext = false;
            if (entry.match(line).hasMatch()) {
                changed = true;
                continue;
            }
        }
        const QRegularExpressionMatch m = setSecret.match(line);
        if (m.hasMatch()) {
            dropNext = m.captured(4).isEmpty();
            changed = true;
            continue;
        }
        kept << raw;
    }
    if (!changed)
        return false;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(kept.join('\n'));
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return out.commit();
}
