// Branding and the boot screen, default tools, security setup, and
// troubleshooting: mostly Omarchy's own setup commands, with the state
// each one leaves behind read back so the window can show it.

#include "store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

static QString readText(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static QVariantList asVariants(const QStringList &list) {
    QVariantList out;
    for (const QString &s : list)
        out << s;
    return out;
}

static const QStringList agents{"pi", "omp", "opencode", "ori", "claude", "codex", "grok", "openclaw", "agy", "hermes",
                                "copilot", "crush", "cursor-agent", "muse", "kimi", "qwen", "dsh", "mcode", "kiro-cli",
                                "freebuff", "kilo", "devin", "amp", "commandcode"};

void Store::buildSystem() {
    {
        Setting s;
        s.id = "general.agent"; s.pane = "general"; s.type = "choice"; s.label = "Coding agent";
        s.choices = asVariants(agents);
        s.read = [this] { return QVariant(output("omarchy-default-agent", {})); };
        // An agent that isn't installed is installed first, in a terminal.
        s.write = [this](const QVariant &v) { return runOk("omarchy-default-agent", {v.toString()}, 20000); };
        add(s);
    }
    {
        Setting s;
        s.id = "general.dictation"; s.pane = "general"; s.type = "choice"; s.label = "Dictation";
        s.choices = asVariants({"superwhisper", "voxtype"});
        s.read = [this] { return QVariant(output("omarchy-default-dictation", {})); };
        // Choosing one runs its installer in a terminal, which outlives us.
        s.write = [this](const QVariant &v) { return launch("omarchy-default-dictation", {v.toString()}); };
        add(s);
    }
    for (const QString &field : {QStringLiteral("name"), QStringLiteral("email")}) {
        Setting s;
        s.id = field == "name" ? "security.gitName" : "security.gitEmail"; s.pane = "security"; s.type = "text";
        s.label = field == "name" ? "Name on your git commits" : "Email on your git commits";
        s.read = [this, field] { return QVariant(output("git", {"config", "--global", "user." + field})); };
        s.write = [this, field](const QVariant &v) {
            const QString value = v.toString().trimmed();
            if (value.isEmpty() || value.contains('\n'))
                return tr("Enter a value");
            static const QRegularExpression email(R"(^[^@\s]+@[^@\s]+$)");
            if (field == "email" && !email.match(value).hasMatch())
                return tr("That doesn't look like an email address");
            return runOk("omarchy-setup-git", {"--" + field, value}, 15000);
        };
        add(s);
    }
}

// Why the firmware can't take a direct boot entry, by the same rules
// omarchy-setup-direct-boot applies, or "" when it can.
QString Store::directBootProblem() {
    const QString vendor = readText("/sys/class/dmi/id/bios_vendor").toLower();
    if (vendor.contains("american megatrends"))
        return tr("This computer's firmware (American Megatrends) may not safely take a custom boot entry.");
    if (vendor.contains("apple"))
        return tr("Apple firmware uses its own boot manager.");
    if (!run("efibootmgr", {}, 5000).ok())
        return tr("This computer doesn't boot with UEFI, or efibootmgr can't reach it.");
    return {};
}

QVariant Store::listSystem(const QString &name) {
    if (name == "security") {
        return QVariantMap{
            {"sshd", output("systemctl", {"is-enabled", "sshd"}) == "enabled"},
            {"fingerprint", run("omarchy-pkg-present", {"fprintd"}).ok()},
            {"fido2", run("omarchy-pkg-present", {"pam-u2f"}).ok()},
            {"docker", !QStandardPaths::findExecutable("docker").isEmpty()},
            // omarchy-sudo-docker --configured fails once the user is in the
            // docker group.
            {"dockerSudoless", !run("omarchy-sudo-docker", {"--configured"}).ok()},
            {"passwordlessSudo", run("omarchy-sudo-passwordless", {"--active"}, 5000).ok()},
            {"directBoot", !output("omarchy-boot-direct", {"--number"}).isEmpty()},
            {"directBootSupported", directBootProblem().isEmpty()},
            {"directBootProblem", directBootProblem()}};
    }
    if (name == "system") {
        return QVariantMap{
            {"haptics", run("omarchy-hw-dell-xps-haptic-touchpad", {}).ok()
                            && !QStandardPaths::findExecutable("dell-xps-touchpad-haptics").isEmpty()},
            {"themeExtras", run("omarchy-theme-extras", {}).ok()},
            {"kernel", output("omarchy-kernel", {"get"})},
            {"kernels", output("omarchy-kernel", {"list"}).split('\n', Qt::SkipEmptyParts)}};
    }
    if (name == "branding") {
        return QVariantMap{{"about", readText(m_paths.config + "/omarchy/branding/about.txt")},
                           {"screensaver", readText(m_paths.config + "/omarchy/branding/screensaver.txt")}};
    }
    if (name == "bootScreens") {
        // The disk-unlock screen can take any theme's look; each theme ships a
        // preview of it.
        QVariantList out;
        const QString current = output("omarchy-plymouth-current", {});
        auto preview = [this](const QString &slug) {
            for (const QString &dir : {m_paths.config + "/omarchy/themes/" + slug, m_paths.omarchy + "/themes/" + slug})
                if (QFileInfo::exists(dir + "/preview-unlock.png"))
                    return "file://" + dir + "/preview-unlock.png";
            return QString();
        };
        out << QVariantMap{{"name", "default"}, {"preview", "file://" + m_paths.omarchy + "/default/plymouth/preview-unlock.png"},
                           {"current", current == "default" || current.isEmpty()}};
        for (const QString &slug : output("omarchy-plymouth-list", {}, 10000).split('\n', Qt::SkipEmptyParts))
            out << QVariantMap{{"name", slug}, {"preview", preview(slug)}, {"current", slug == current}};
        return out;
    }
    return {};
}

QString Store::actSystem(const QString &action, const QVariantList &args) {
    const QString a0 = args.value(0).toString();
    if (action.startsWith("branding.")) {
        if (a0 != "about" && a0 != "screensaver")
            return tr("Unknown branding %1").arg(a0);
        const QString command = "omarchy-branding-" + a0;
        // Editing opens an editor and an image opens the file chooser; both
        // outlive the click.
        if (action == "branding.edit")
            return launch(command, {"text"});
        if (action == "branding.image")
            return launch(command, {"image"});
        if (action == "branding.reset")
            return runOk(command, {"reset"}, 15000);
    }
    if (action == "haptics.set") {
        if (!QStringList{"low", "mid", "high"}.contains(a0))
            return tr("Unknown strength %1").arg(a0);
        return runOk("dell-xps-touchpad-haptics", {"set", a0}, 10000);
    }
    if (action == "menu.hide" || action == "menu.show")
        return setMenuHidden(a0, action == "menu.hide");
    if (action == "restart.shell")
        return launch("omarchy-restart-shell", {});
    if (action == "restart.nightLight")
        return runOk("omarchy-restart-hyprsunset", {}, 15000);
    return tr("Unknown action %1").arg(action);
}

// Commands that run in a terminal, with an argument that must be one of a
// known set.
QStringList Store::terminalCommand(const QString &id) {
    static const QMap<QString, QStringList> commands{
        {"sshdSetup", {"omarchy-setup-security-sshd"}},
        {"sshdRemove", {"omarchy-remove-security-sshd"}},
        {"fingerprintRemove", {"omarchy-remove-security-fingerprint"}},
        {"fido2Remove", {"omarchy-remove-security-fido2"}},
        {"dockerSetup", {"omarchy-setup-security-sudoless-docker"}},
        {"dockerRemove", {"omarchy-remove-security-sudoless-docker"}},
        {"sudoPasswordless", {"omarchy-sudo-passwordless"}},
        {"sudoPasswordlessOff", {"omarchy-sudo-passwordless", "--disable"}},
        {"directBoot", {"omarchy-setup-direct-boot"}},
        {"gitSetup", {"omarchy-setup-git"}},
        {"themeUpdate", {"omarchy-theme-update"}},
        {"timeSync", {"omarchy-update-time"}},
        {"restartAudio", {"omarchy-restart-audio"}},
        {"restartWifi", {"omarchy-restart-wifi"}},
        {"restartBluetooth", {"omarchy-restart-bluetooth"}},
        {"restartTrackpad", {"omarchy-restart-trackpad"}},
        {"refreshHyprland", {"omarchy-refresh-hyprland"}},
        {"refreshShell", {"omarchy-refresh-shell"}},
        {"refreshHyprsunset", {"omarchy-refresh-hyprsunset"}},
        {"refreshPlymouth", {"omarchy-refresh-plymouth"}},
        {"refreshTmux", {"omarchy-refresh-tmux"}},
        {"plymouthReset", {"omarchy-plymouth-reset"}},
        {"factoryReset", {"omarchy-system-factory-reset"}},
    };
    if (commands.contains(id))
        return commands.value(id);
    const QString kind = id.section(':', 0, 0), value = id.section(':', 1);
    if (kind == "kernel" && output("omarchy-kernel", {"list"}).split('\n').contains(value))
        return {"omarchy-kernel", "set", value};
    if (kind == "bootScreen" && output("omarchy-plymouth-list", {}, 10000).split('\n').contains(value))
        return {"omarchy-plymouth-set-by-theme", value};
    return {};
}

// --- The Omarchy menu ---------------------------------------------------------------
//
// Items are hidden the way Omarchy's menu extension documents: an entry in
// ~/.config/omarchy/extensions/omarchy-menu.jsonc whose "when" is "false".
// The menu reads that file as JSON once whole-line // comments are gone and
// trailing commas dropped, and a later entry for the same id replaces an
// earlier one, so edits are made line by line and checked the same way.

static QString stripJsonc(const QString &raw) {
    static const QRegularExpression comments(R"(^\s*//[^\n]*(\n|$))", QRegularExpression::MultilineOption);
    static const QRegularExpression trailing(R"(,(\s*[}\]]))");
    QString out = raw;
    out.replace(comments, "");
    out.replace(trailing, "\\1");
    return out;
}

static bool validMenuText(const QString &text) {
    const QString stripped = stripJsonc(text);
    if (stripped.trimmed().isEmpty())
        return true;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(stripped.toUtf8(), &error);
    return error.error == QJsonParseError::NoError && doc.isObject();
}

// An entry written on one line: `  "id": {...},`.
static QRegularExpression oneLineEntry(const QString &id) {
    return QRegularExpression("^(\\s*)\"" + QRegularExpression::escape(id) + "\"\\s*:\\s*(\\{.*\\})\\s*(,?)\\s*$");
}

// The brace depth at the start of each line (outside strings), so only the
// top-level keys of the file are taken for menu entries.
static QList<int> lineDepths(const QStringList &lines) {
    QList<int> depths;
    int depth = 0;
    for (const QString &line : lines) {
        depths << depth;
        if (line.trimmed().startsWith("//"))
            continue;
        bool inString = false;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line[i];
            if (inString) {
                if (c == '\\') ++i;
                else if (c == '"') inString = false;
            } else if (c == '"') {
                inString = true;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
            }
        }
    }
    return depths;
}

QString Store::menuFile() const {
    return m_paths.config + "/omarchy/extensions/omarchy-menu.jsonc";
}

QVariant Store::listMenu() {
    // Omarchy's own items, in the order its file lists them (one per line).
    const QString defaults = readText(m_paths.omarchy + "/default/omarchy/omarchy-menu.jsonc");
    const QJsonObject defaultItems = QJsonDocument::fromJson(stripJsonc(defaults).toUtf8()).object();
    static const QRegularExpression key(R"re(^\s*"([a-z0-9][a-z0-9._-]*)"\s*:)re");
    QStringList order;
    const QStringList defaultLines = defaults.split('\n');
    const QList<int> defaultDepths = lineDepths(defaultLines);
    for (int i = 0; i < defaultLines.size(); ++i) {
        const auto m = key.match(defaultLines[i]);
        if (m.hasMatch() && defaultDepths[i] == 1 && !order.contains(m.captured(1)))
            order << m.captured(1);
    }

    // The user's entries: the last one-line entry for an id is the one in
    // force; an entry over several lines is theirs to edit by hand.
    const QStringList lines = readText(menuFile()).split('\n');
    QHash<QString, QJsonObject> user;
    QSet<QString> multiLine;
    QStringList userOrder;
    const QList<int> depths = lineDepths(lines);
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const auto m = key.match(line);
        if (!m.hasMatch() || depths[i] != 1)
            continue;
        const QString id = m.captured(1);
        if (!userOrder.contains(id))
            userOrder << id;
        const auto one = oneLineEntry(id).match(line);
        if (one.hasMatch())
            user.insert(id, QJsonDocument::fromJson(one.captured(2).toUtf8()).object());
        else
            multiLine.insert(id);
    }
    for (const QString &id : userOrder)
        if (!order.contains(id))
            order << id;

    QVariantList out;
    for (const QString &id : order) {
        const QJsonObject d = defaultItems.value(id).toObject();
        const QJsonObject u = user.value(id);
        QVariantMap item;
        item["id"] = id;
        item["label"] = u.value("label").toString(d.value("label").toString(id));
        item["icon"] = u.value("icon").toString(d.value("icon").toString());
        item["depth"] = id.count('.');
        item["parent"] = id.contains('.') ? id.section('.', 0, -2) : QString();
        item["hidden"] = u.value("when").toString() == "false";
        item["editable"] = !multiLine.contains(id);
        item["custom"] = !defaultItems.contains(id);
        out << item;
    }
    return out;
}

QString Store::setMenuHidden(const QString &id, bool hidden) {
    static const QRegularExpression validId("^[a-z0-9][a-z0-9._-]*$");
    if (!validId.match(id).hasMatch())
        return tr("Unknown menu item %1").arg(id);
    const QString file = menuFile();
    QString text = readText(file);
    if (text.trimmed().isEmpty())
        text = "{\n}\n";
    QStringList lines = text.split('\n');

    static const QRegularExpression startsEntry(R"re(^\s*"([a-z0-9][a-z0-9._-]*)"\s*:)re");
    QList<int> entries;
    const QList<int> depths = lineDepths(lines);
    for (int i = 0; i < lines.size(); ++i) {
        const auto m = startsEntry.match(lines[i]);
        if (!m.hasMatch() || m.captured(1) != id || depths[i] != 1)
            continue;
        if (!oneLineEntry(id).match(lines[i]).hasMatch())
            return tr("“%1” is set over several lines in your menu file; change it there").arg(id);
        entries << i;
    }

    // A condition the user wrote is kept aside while the item is hidden, and
    // put back when it is shown again.
    QMutexLocker lock(&m_stateLock);
    QJsonObject state = managedState();
    QJsonObject kept = state.value("menuWhen").toObject();

    auto rewrite = [&](int i, QJsonObject entry) {
        const auto m = oneLineEntry(id).match(lines[i]);
        if (entry.isEmpty()) {
            lines.removeAt(i);
            return;
        }
        lines[i] = m.captured(1) + "\"" + id + "\": "
                   + QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)) + ",";
    };

    if (hidden) {
        if (entries.isEmpty()) {
            // A new line straight after the opening brace; the menu ignores
            // a trailing comma.
            int brace = -1;
            for (int i = 0; i < lines.size() && brace < 0; ++i)
                if (lines[i].trimmed().startsWith('{'))
                    brace = i;
            if (brace < 0)
                return tr("Your menu file isn't a JSON object; fix it by hand first");
            lines.insert(brace + 1, "  \"" + id + "\": {\"when\": \"false\"},");
        } else {
            const int last = entries.last();
            QJsonObject entry = QJsonDocument::fromJson(oneLineEntry(id).match(lines[last]).captured(2).toUtf8()).object();
            const QString when = entry.value("when").toString();
            if (!when.isEmpty() && when != "false")
                kept.insert(id, when);
            entry.insert("when", "false");
            rewrite(last, entry);
        }
    } else {
        // Every entry that hides it, from the bottom up so line numbers hold.
        for (int k = entries.size() - 1; k >= 0; --k) {
            const int i = entries[k];
            QJsonObject entry = QJsonDocument::fromJson(oneLineEntry(id).match(lines[i]).captured(2).toUtf8()).object();
            if (entry.value("when").toString() != "false")
                continue;
            if (kept.contains(id))
                entry.insert("when", kept.value(id));
            else
                entry.remove("when");
            rewrite(i, entry);
        }
        kept.remove(id);
    }

    const QString next = lines.join('\n');
    if (!validMenuText(next))
        return tr("Changing the menu file would break it, so nothing was changed");
    // One backup a day keeps the hand-written version without piling up.
    const QString backup = file + ".bak.omasettings-" + QDateTime::currentDateTime().toString("yyyyMMdd");
    if (QFileInfo::exists(file) && !QFileInfo::exists(backup))
        QFile::copy(file, backup);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    const QByteArray data = next.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
        return tr("Could not write %1").arg(file);

    state.insert("menuWhen", kept);
    return applyState(state);
}
