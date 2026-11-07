// VPN connections, shell plugins, and the bar's layout: the parts of the
// Store that manage collections rather than single settings.

#include "store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QVariantMap>

static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

static bool validId(const QString &id) {
    // The same rule omarchy plugin update applies to plugin ids.
    static const QRegularExpression valid("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return valid.match(id).hasMatch() && !id.contains("..");
}

static bool validUuid(const QString &uuid) {
    static const QRegularExpression valid("^[0-9a-fA-F-]{36}$");
    return valid.match(uuid).hasMatch();
}

static const QStringList barSections{"left", "centerBegin", "center", "centerEnd", "right", "tray"};

// --- VPN ------------------------------------------------------------------------

QVariant Store::listVpn() {
    QVariantList connections;
    for (const QString &line : output("nmcli", {"-t", "-f", "NAME,UUID,TYPE,DEVICE,ACTIVE", "connection", "show"}).split('\n')) {
        const QStringList f = splitTerse(line);
        if (f.size() < 5 || (f[2] != "vpn" && f[2] != "wireguard"))
            continue;
        QString kind = "wireguard";
        if (f[2] == "vpn") {
            const QString service = output("nmcli", {"-g", "vpn.service-type", "connection", "show", "uuid", f[1]});
            kind = service.contains("openvpn") ? "openvpn" : service.section('.', -1);
        }
        QVariantMap c;
        c["name"] = f[0];
        c["uuid"] = f[1];
        c["kind"] = kind;
        c["active"] = f[4] == "yes";
        connections << c;
    }
    QVariantMap out;
    out["connections"] = connections;
    // NetworkManager speaks WireGuard itself; the tools add `wg` for status.
    // OpenVPN needs NetworkManager's plugin, not just the openvpn program.
    out["wireguard"] = !QStandardPaths::findExecutable("wg").isEmpty();
    out["openvpn"] = QFileInfo::exists("/usr/lib/NetworkManager/VPN/nm-openvpn-service.name");
    return out;
}

QString Store::actVpn(const QString &action, const QVariantList &args) {
    const QString a0 = args.value(0).toString();
    if (action == "vpn.import") {
        const QString kind = a0;
        if (kind != "wireguard" && kind != "openvpn")
            return tr("Unknown VPN type %1").arg(kind);
        // The desktop's file chooser, through the portal, as Omarchy picks
        // files everywhere else.
        const ProcResult picked = run("omarchy-file-select",
                                      {"--title", tr("Choose a VPN configuration"),
                                       "--extensions", kind == "wireguard" ? "conf" : "ovpn conf"}, 600000);
        const QString path = picked.text().section('\n', 0, 0);
        if (!picked.ok() || path.isEmpty())
            return {}; // Nothing chosen is not an error.
        if (!QFileInfo(path).isFile())
            return tr("Could not read %1").arg(path);
        const QString error = runOk("nmcli", {"connection", "import", "type", kind, "file", path}, 30000);
        if (!error.isEmpty() && kind == "wireguard" && QFileInfo(path).completeBaseName().size() > 15)
            return tr("%1 (WireGuard names the connection after the file; rename it to 15 characters or fewer)").arg(error);
        return error;
    }
    if (!validUuid(a0))
        return tr("Unknown VPN %1").arg(a0);
    if (action == "vpn.connect")
        return runOk("nmcli", {"--wait", "30", "connection", "up", "uuid", a0}, 40000);
    if (action == "vpn.disconnect")
        return runOk("nmcli", {"connection", "down", "uuid", a0}, 20000);
    if (action == "vpn.delete")
        return runOk("nmcli", {"connection", "delete", "uuid", a0}, 20000);
    if (action == "vpn.credentials") {
        const QString user = args.value(1).toString(), password = args.value(2).toString();
        if (user.contains('\n') || password.contains('\n') || user.contains(','))
            return tr("Names and passwords can't contain line breaks or commas");
        // Saved with the connection (password-flags 0), so connecting needs
        // no prompt.
        QString error = runOk("nmcli", {"connection", "modify", "uuid", a0, "+vpn.data", "username=" + user}, 15000);
        if (error.isEmpty() && !password.isEmpty())
            error = runOk("nmcli", {"connection", "modify", "uuid", a0, "+vpn.data", "password-flags=0"}, 15000);
        // The password itself never goes in nmcli's arguments.
        if (error.isEmpty() && !password.isEmpty())
            error = setNmSecret(a0, "vpn.secrets", "password=" + password);
        return error;
    }
    return tr("Unknown action %1").arg(action);
}

// --- Plugins ------------------------------------------------------------------------

QString Store::pluginDir(const QString &id) const {
    return m_paths.config + "/omarchy/plugins/" + id;
}

// Every manifest.json under Omarchy's plugins and the user's, by plugin id.
QHash<QString, QJsonObject> Store::manifests() const {
    QHash<QString, QJsonObject> out;
    for (const QString &root : {m_paths.omarchy + "/shell/plugins", m_paths.config + "/omarchy/plugins"}) {
        QDirIterator it(root, {"manifest.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            // Plugins keep their manifest at their top or one level down
            // (panels/network); deeper ones belong to test fixtures.
            if (QDir(root).relativeFilePath(path).count('/') > 2)
                continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
            if (m.value("id").isString())
                out.insert(m.value("id").toString(), m);
        }
    }
    return out;
}

QVariant Store::listPlugins() {
    const QHash<QString, QJsonObject> found = manifests();
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(run("omarchy-plugin-list", {"--json"}, 15000).out).array()) {
        QVariantMap p = v.toObject().toVariantMap();
        const QString id = p.value("id").toString();
        const QJsonObject m = found.value(id);
        p["description"] = m.value("description").toString();
        p["version"] = m.value("version").toString();
        p["author"] = m.value("author").toString();
        p["git"] = !p.value("firstParty").toBool() && QFileInfo::exists(pluginDir(id) + "/.git");
        p["removable"] = !p.value("firstParty").toBool() && QFileInfo(pluginDir(id)).isDir();
        out << p;
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("name").toString().compare(b.toMap().value("name").toString(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

// Which git-managed plugins have new commits upstream. Fetching is what
// omarchy plugin update does first, so this leaves nothing behind it would
// not.
QVariant Store::listPluginUpdates() {
    QVariantMap out;
    QDir dir(m_paths.config + "/omarchy/plugins");
    for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!validId(id) || !QFileInfo::exists(pluginDir(id) + "/.git"))
            continue;
        if (!run("git", {"-C", pluginDir(id), "fetch", "--quiet", "origin", "HEAD"}, 30000).ok()) {
            out[id] = "unreachable";
            continue;
        }
        const QString head = output("git", {"-C", pluginDir(id), "rev-parse", "HEAD"});
        const QString fetched = output("git", {"-C", pluginDir(id), "rev-parse", "FETCH_HEAD"});
        out[id] = !fetched.isEmpty() && head != fetched ? "available" : "current";
    }
    out["checkedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return out;
}

QString Store::actPlugin(const QString &action, const QVariantList &args) {
    const QString id = args.value(0).toString();
    if (action == "plugin.add") {
        static const QRegularExpression url(R"(^(https://[^\s'"]+|git@[A-Za-z0-9.-]+:[^\s'"]+)$)");
        if (!url.match(id).hasMatch())
            return tr("Enter a git URL, like https://github.com/someone/plugin");
        return runOk("omarchy-plugin-add", {id, "--enable", "--yes"}, 180000);
    }
    if (action == "plugin.updateAll")
        return runOk("omarchy-plugin-update", {"--yes"}, 300000);
    if (!validId(id))
        return tr("Unknown plugin %1").arg(id);
    if (action == "plugin.enable")
        return runOk("omarchy-plugin-enable", {id}, 15000);
    if (action == "plugin.disable")
        return runOk("omarchy-plugin-disable", {id}, 15000);
    if (action == "plugin.update")
        return runOk("omarchy-plugin-update", {id, "--yes"}, 120000);
    if (action == "plugin.remove")
        return runOk("omarchy-plugin-remove", {id, "--yes"}, 30000);
    return tr("Unknown action %1").arg(action);
}

// --- The bar --------------------------------------------------------------------------

static QString entryId(const QJsonValue &entry) {
    return entry.isString() ? entry.toString() : entry.toObject().value("id").toString();
}

QVariant Store::listBar() {
    QHash<QString, QVariantMap> plugins;
    for (const QVariant &p : listPlugins().toList())
        plugins.insert(p.toMap().value("id").toString(), p.toMap());

    const QJsonObject layout = shellGet("bar.layout", QJsonObject()).toObject();
    QVariantMap sections;
    QStringList placed;
    for (const QString &section : barSections) {
        QVariantList widgets;
        const QJsonArray entries = layout.value(section).toArray();
        for (int i = 0; i < entries.size(); ++i) {
            const QString id = entryId(entries[i]);
            placed << id;
            QVariantMap w;
            w["id"] = id;
            w["index"] = i;
            w["name"] = plugins.value(id).value("name", id);
            w["description"] = plugins.value(id).value("description");
            widgets << w;
        }
        sections[section] = widgets;
    }
    // What can go on the bar: widgets that are not there yet.
    QVariantList available;
    for (const QVariantMap &p : plugins) {
        const QStringList kinds = p.value("kinds").toStringList();
        if (kinds.contains("bar-widget") && !placed.contains(p.value("id").toString()))
            available << QVariantMap{{"id", p.value("id")}, {"name", p.value("name")}, {"description", p.value("description")}};
    }
    std::sort(available.begin(), available.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("name").toString().compare(b.toMap().value("name").toString(), Qt::CaseInsensitive) < 0;
    });
    return QVariantMap{{"sections", sections}, {"available", available}};
}

// The shell saves shell.json a moment after it changes the layout; wait for
// that so the pane reads back what is really there.
void Store::waitForShellWrite(const QDateTime &before) {
    const QString file = m_paths.config + "/omarchy/shell.json";
    for (int i = 0; i < 20 && QFileInfo(file).lastModified() == before; ++i)
        QThread::msleep(50);
}

QString Store::actBar(const QString &action, const QVariantList &args) {
    const QString id = args.value(0).toString();
    if (!validId(id))
        return tr("Unknown widget %1").arg(id);
    const QDateTime before = QFileInfo(m_paths.config + "/omarchy/shell.json").lastModified();
    QString error;
    if (action == "bar.move") {
        // args: id, from section, from index, to section, to index
        const QString from = args.value(1).toString(), to = args.value(3).toString();
        if (!barSections.contains(from) || !barSections.contains(to))
            return tr("Unknown bar section");
        error = runOk("omarchy-bar", {"move", id, "--from-section", from, "--from-index", QString::number(args.value(2).toInt()),
                                      "--section", to, "--index", QString::number(qMax(0, args.value(4).toInt()))}, 15000);
    } else if (action == "bar.add") {
        const QString section = args.value(1).toString();
        if (!barSections.contains(section))
            return tr("Unknown bar section");
        error = runOk("omarchy-plugin-enable", {id, "--section", section}, 15000);
    } else if (action == "bar.remove") {
        // The shell takes a widget off the bar by disabling it; enabling it
        // again puts it back.
        error = runOk("omarchy-plugin-disable", {id}, 15000);
    } else {
        return tr("Unknown action %1").arg(action);
    }
    if (error.isEmpty())
        waitForShellWrite(before);
    return error;
}
