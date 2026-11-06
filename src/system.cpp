// Branding and the boot screen, default tools, security setup, and
// troubleshooting: mostly Omarchy's own setup commands, with the state
// each one leaves behind read back so the window can show it.

#include "store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QTemporaryDir>
#include "theme.h"
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
    {
        Setting s;
        s.id = "desktop.oneWindowRatio"; s.pane = "desktop"; s.type = "bool"; s.label = "Keep a lone window from stretching";
        s.read = [this] { return QVariant(flag("hypr/single-window-aspect-ratio.lua")); };
        s.write = [this](const QVariant &v) {
            return runOk("omarchy-hyprland-toggle", {"single-window-aspect-ratio", v.toBool() ? "on" : "off"});
        };
        add(s);
    }
    {
        Setting s;
        s.id = "bar.batteryPercentage"; s.pane = "bar"; s.type = "bool"; s.label = "Show battery percentage";
        s.read = [this] { return QVariant(output("omarchy-bar", {"get", "omarchy.power", "showPercentage"}) == "true"); };
        s.write = [this](const QVariant &v) {
            if ((output("omarchy-bar", {"get", "omarchy.power", "showPercentage"}) == "true") == v.toBool())
                return QString();
            return runOk("omarchy-shell", {"omarchy.power", "togglePercentage"});
        };
        add(s);
    }
    // The laptop's own screen: off or mirrored, by Omarchy's toggles, which
    // only flip, so they run only when the state differs.
    for (const QString &which : {QStringLiteral("disable"), QStringLiteral("mirror")}) {
        Setting s;
        s.id = which == "disable" ? "display.laptopScreen" : "display.mirror"; s.pane = "displays"; s.type = "bool";
        s.label = which == "disable" ? "Use the laptop's screen" : "Mirror the laptop's screen";
        const QString toggle = "hypr/internal-monitor-" + which + ".lua";
        const QString command = which == "disable" ? "omarchy-hyprland-monitor-internal" : "omarchy-hyprland-monitor-internal-mirror";
        const bool inverted = which == "disable";
        s.read = [this, toggle, inverted] { return QVariant(flag(toggle) != inverted); };
        s.write = [this, toggle, inverted, command](const QVariant &v) {
            if ((flag(toggle) != inverted) == v.toBool())
                return QString();
            return runOk(command, {"toggle"}, 15000);
        };
        add(s);
    }
    {
        Setting s;
        s.id = "general.clockVerticalFormat"; s.pane = "general"; s.type = "choice"; s.label = "Clock in a side bar";
        s.choices = asVariants({"HH\n—\nmm", "h\n—\nmm\nAP", "dd\nMMM\n'W'ww\n''yy", "HH\nmm"});
        s.read = [this] { return QVariant(output("omarchy-bar", {"get", "omarchy.clock", "verticalFormat"})); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-bar", {"set", "omarchy.clock", "verticalFormat", v.toString()}); };
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

// The six Nerd Fonts Omarchy's menu offers to install: name, package, and
// the family it switches to.
static const QList<QStringList> &fontCatalog() {
    static const QList<QStringList> fonts{
        {"cascadia", "Cascadia Mono", "ttf-cascadia-mono-nerd", "CaskaydiaMono Nerd Font"},
        {"meslo", "Meslo LG Mono", "ttf-meslo-nerd", "MesloLGL Nerd Font"},
        {"fira", "Fira Code", "ttf-firacode-nerd", "FiraCode Nerd Font"},
        {"victor", "Victor Code", "ttf-victor-mono-nerd", "VictorMono Nerd Font"},
        {"bitstream", "Bitstream Vera Code", "ttf-bitstream-vera-mono-nerd", "BitstromWera Nerd Font"},
        {"iosevka", "Iosevka", "ttf-iosevka-nerd", "Iosevka Nerd Font Mono"},
    };
    return fonts;
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
    if (name == "hardware") {
        return QVariantMap{
            {"laptop", run("omarchy-hw-laptop", {}).ok()},
            {"hybridGpu", run("omarchy-hw-hybrid-gpu", {}).ok()},
            {"hybridMode", readText("/etc/supergfxd.conf").contains("\"mode\": \"Hybrid\"")},
            {"wifiConnected", run("omarchy-network-wifi-connected", {}).ok()},
            {"sshAgent", run("systemctl", {"--user", "-q", "is-enabled", "gcr-ssh-agent.socket"}).ok()}};
    }
    if (name == "reminders")
        return QJsonDocument::fromJson(run("omarchy-reminder", {"show", "--json"}).out).object().toVariantMap();
    if (name == "fontCatalog") {
        const QStringList installed = output("omarchy-font-list", {}, 10000).split('\n');
        QVariantList out;
        for (const QStringList &f : fontCatalog())
            out << QVariantMap{{"key", f[0]}, {"name", f[1]}, {"family", f[3]}, {"installed", installed.contains(f[3])}};
        return out;
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
    if (action == "menu.hide" || action == "menu.show" || action == "menu.hideInside" || action == "menu.showAll")
        return menuAction(action, a0);
    if (action == "reminder.set") {
        const int minutes = a0.toInt();
        const QString message = args.value(1).toString().trimmed();
        if (minutes < 1 || minutes > 1440)
            return tr("Reminders are 1 minute to 24 hours away");
        if (message.contains('\n'))
            return tr("Keep the message on one line");
        QStringList command{QString::number(minutes)};
        if (!message.isEmpty())
            command << message;
        return runOk("omarchy-reminder", command, 10000);
    }
    if (action == "reminder.clear")
        return runOk("omarchy-reminder", {"clear"}, 10000);
    if (action == "summon") {
        // Omarchy's own panels for these: they draw live results.
        static const QStringList panels{"omarchy.speedtest", "omarchy.disk-speedtest", "omarchy.wifiqr"};
        if (!panels.contains(a0))
            return tr("Unknown panel %1").arg(a0);
        return runOk("omarchy-shell", {"shell", "summon", a0}, 5000);
    }
    if (action == "plugin.clone") {
        static const QRegularExpression validId("^omarchy\\.[a-z0-9._-]+$");
        if (!validId.match(a0).hasMatch())
            return tr("Only Omarchy's own plugins can be copied");
        return runOk("omarchy-plugin-clone", {a0}, 30000);
    }
    if (action == "xcompose.edit") {
        // A fixed command line: the editor, then the compose daemon picks
        // up the changes.
        return launch("bash", {"-c", "omarchy-launch-config-editor ~/.XCompose && omarchy-restart-xcompose"});
    }
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
        {"hybridGpu", {"omarchy-toggle-hybrid-gpu"}},
        {"sshAgentRemove", {"omarchy-remove-service-ssh-agent"}},
    };
    if (commands.contains(id))
        return commands.value(id);
    const QString kind = id.section(':', 0, 0), value = id.section(':', 1);
    if (kind == "kernel" && output("omarchy-kernel", {"list"}).split('\n').contains(value))
        return {"omarchy-kernel", "set", value};
    if (kind == "bootScreen" && output("omarchy-plymouth-list", {}, 10000).split('\n').contains(value))
        return {"omarchy-plymouth-set-by-theme", value};
    if (kind == "font")
        for (const QStringList &f : fontCatalog())
            if (f[0] == value)
                return {"omarchy-install-font", f[1], f[2], f[3]};
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

// Hides and shows menu items in one pass over the file: read once, every
// edit made in memory, the result checked as the menu will read it, written
// once. Items set over several lines are the user's and are left alone.
QString Store::editMenu(const QStringList &hide, const QStringList &show, QJsonObject &kept, QStringList &skipped) {
    const QString file = menuFile();
    QString text = readText(file);
    if (text.trimmed().isEmpty())
        text = "{\n}\n";
    QStringList lines = text.split('\n');
    static const QRegularExpression startsEntry(R"re(^\s*"([a-z0-9][a-z0-9._-]*)"\s*:)re");

    // Where each id's top-level entries are, found afresh after each edit
    // since edits move lines.
    auto entriesFor = [&](const QString &id, bool &multi) {
        QList<int> found;
        multi = false;
        const QList<int> depths = lineDepths(lines);
        for (int i = 0; i < lines.size(); ++i) {
            const auto m = startsEntry.match(lines[i]);
            if (!m.hasMatch() || m.captured(1) != id || depths[i] != 1)
                continue;
            if (!oneLineEntry(id).match(lines[i]).hasMatch())
                multi = true;
            found << i;
        }
        return found;
    };
    auto rewrite = [&](const QString &id, int i, const QJsonObject &entry) {
        const auto m = oneLineEntry(id).match(lines[i]);
        if (entry.isEmpty())
            lines.removeAt(i);
        else
            lines[i] = m.captured(1) + "\"" + id + "\": "
                       + QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)) + ",";
    };
    int brace = -1;
    for (int i = 0; i < lines.size() && brace < 0; ++i)
        if (lines[i].trimmed().startsWith('{'))
            brace = i;
    if (brace < 0)
        return tr("Your menu file isn't a JSON object; fix it by hand first");

    QStringList added;
    for (const QString &id : hide) {
        bool multi = false;
        const QList<int> entries = entriesFor(id, multi);
        if (multi) {
            skipped << id;
            continue;
        }
        if (entries.isEmpty()) {
            added << "  \"" + id + "\": {\"when\": \"false\"},";
            continue;
        }
        const int last = entries.last();
        QJsonObject entry = QJsonDocument::fromJson(oneLineEntry(id).match(lines[last]).captured(2).toUtf8()).object();
        const QString when = entry.value("when").toString();
        if (when == "false")
            continue;
        // A condition the user wrote is kept aside while hidden.
        if (!when.isEmpty())
            kept.insert(id, when);
        entry.insert("when", "false");
        rewrite(id, last, entry);
    }
    // New lines go together, straight after the opening brace; the menu
    // ignores a trailing comma.
    for (int k = added.size() - 1; k >= 0; --k)
        lines.insert(brace + 1, added[k]);

    for (const QString &id : show) {
        bool multi = false;
        const QList<int> entries = entriesFor(id, multi);
        if (multi) {
            skipped << id;
            continue;
        }
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
            rewrite(id, i, entry);
        }
        kept.remove(id);
    }

    const QString next = lines.join('\n');
    if (next == text)
        return {};
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
    return {};
}

// The menu's search looks at every item, not only the ones a hidden
// section shows, so hiding a section hides what is in it too. Which
// children a section hid is remembered, so showing it again brings back
// those and leaves alone any the user had hidden one by one.
QString Store::menuAction(const QString &action, const QString &id) {
    static const QRegularExpression validId("^[a-z0-9][a-z0-9._-]*$");
    if (!id.isEmpty() && !validId.match(id).hasMatch())
        return tr("Unknown menu item %1").arg(id);

    QMutexLocker lock(&m_stateLock);
    QJsonObject state = managedState();
    QJsonObject kept = state.value("menuWhen").toObject();
    QJsonObject cascade = state.value("menuCascade").toObject();

    QHash<QString, bool> hiddenNow;
    QStringList all;
    for (const QVariant &v : listMenu().toList()) {
        const QVariantMap item = v.toMap();
        all << item.value("id").toString();
        hiddenNow.insert(item.value("id").toString(), item.value("hidden").toBool());
    }
    if (!id.isEmpty() && !all.contains(id))
        return tr("Unknown menu item %1").arg(id);
    auto visibleInside = [&](const QString &section) {
        QStringList out;
        for (const QString &other : all)
            if (other.startsWith(section + ".") && !hiddenNow.value(other))
                out << other;
        return out;
    };
    auto remember = [&](const QString &section, const QStringList &children) {
        QJsonArray list = cascade.value(section).toArray();
        for (const QString &c : children)
            if (!list.contains(c))
                list << c;
        if (!list.isEmpty())
            cascade.insert(section, list);
    };

    QStringList hide, show;
    if (action == "menu.hide") {
        const QStringList children = visibleInside(id);
        hide << id << children;
        remember(id, children);
    } else if (action == "menu.show") {
        show << id;
        for (const QJsonValue &c : cascade.value(id).toArray())
            show << c.toString();
        cascade.remove(id);
    } else if (action == "menu.hideInside") {
        // For sections hidden before this was done for them: each hidden
        // section's visible items, or every hidden section's when no id.
        QStringList sections;
        if (!id.isEmpty())
            sections << id;
        else
            for (const QString &other : all)
                if (hiddenNow.value(other))
                    sections << other;
        for (const QString &section : sections) {
            const QStringList children = visibleInside(section);
            hide << children;
            remember(section, children);
        }
        hide.removeDuplicates();
    } else if (action == "menu.showAll") {
        // Back to Omarchy's menu: every hidden item shown, conditions the
        // user wrote put back, and their own entries left as they are. A
        // copy of the file as it was is kept first, so the hiding can be
        // brought back by hand.
        for (const QString &other : all)
            if (hiddenNow.value(other))
                show << other;
        cascade = QJsonObject();
        const QString backup = menuFile() + ".bak.omasettings-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        if (!show.isEmpty() && QFileInfo::exists(menuFile()) && !QFile::copy(menuFile(), backup))
            return tr("Could not back up %1").arg(menuFile());
    } else {
        return tr("Unknown action %1").arg(action);
    }

    QStringList skipped;
    if (QString error = editMenu(hide, show, kept, skipped); !error.isEmpty())
        return error;
    state.insert("menuWhen", kept);
    state.insert("menuCascade", cascade);
    // Menu bookkeeping has nothing to do with Hyprland, so it is saved
    // without regenerating or reloading anything.
    QSaveFile out(statePath());
    const QByteArray json = reindentJson(QJsonDocument(state).toJson(QJsonDocument::Indented)).toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(json) != json.size() || !out.commit())
        return tr("Could not write %1").arg(statePath());
    if (!skipped.isEmpty())
        return tr("Left as they are, being set over several lines in your menu file: %1").arg(skipped.join(", "));
    return {};
}

// --- The app's icon ------------------------------------------------------------------
//
// Launchers like Keystroke draw Omarchy's own entries as a Nerd Font glyph
// in the theme's text color on a faint tile of the same color, and an app's
// icon as-is. So the icon is drawn the same way, from the same font's
// "tune" glyph, in the current theme's colors, and drawn again whenever the
// theme changes.

// md-tune (U+F062E) from JetBrainsMono Nerd Font, moved to the origin and
// flipped to SVG's y-down; it is 750 font units square.
static const char *tuneGlyph = "M0 584V666H250V584ZM0 84V166H416V84ZM416 750V666H750V584H416V500H334V750ZM166 250V334H0V416H166V500H250V250ZM750 416V334H334V416ZM500 250H584V166H750V84H584V0H500Z";

QString Store::iconSvg(const QString &foreground, double rounding) {
    // The glyph is set at 72% of the icon's size, as Keystroke sets its
    // glyphs against the space it gives an app icon.
    const double scale = 0.72 * 256 / 1000, size = 750 * scale, offset = (256 - size) / 2;
    const double radius = qBound(0.0, rounding, 9.0) * 256 / 24;
    return QStringLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\" width=\"256\" height=\"256\">\n"
               "  <rect width=\"256\" height=\"256\" rx=\"%1\" fill=\"%2\" fill-opacity=\"0.07\"/>\n"
               "  <path transform=\"translate(%3 %3) scale(%4)\" fill=\"%2\" fill-opacity=\"0.8\" d=\"%5\"/>\n"
               "</svg>\n")
        .arg(radius, 0, 'f', 1).arg(foreground).arg(offset, 0, 'f', 2).arg(scale, 0, 'f', 5).arg(QString::fromLatin1(tuneGlyph));
}

QString Store::iconPath() const {
    return m_paths.data + "/icons/hicolor/scalable/apps/omasettings.svg";
}

// The icon in the current theme's text color and window rounding, in the
// user's icon folder, which wins over the packaged one.
QString Store::writeThemedIcon() {
    const QVariantMap colors = Theme::readColors(m_paths.state + "/omarchy/current/theme/colors.toml");
    const QString foreground = colors.value("foreground", "#a9b1d6").toString();
    static const QRegularExpression hex("^#[0-9a-fA-F]{6}$");
    if (!hex.match(foreground).hasMatch())
        return tr("The theme's foreground color isn't a color");
    int rounding = 0;
    if (m_paths.hyprland)
        rounding = QJsonDocument::fromJson(run("hyprctl", {"-j", "getoption", "decoration:rounding"}, 2000).out)
                       .object().value("int").toInt(0);
    const QString svg = iconSvg(foreground, rounding);
    if (readText(iconPath()) == svg)
        return {};
    QDir().mkpath(QFileInfo(iconPath()).absolutePath());
    QSaveFile out(iconPath());
    if (!out.open(QIODevice::WriteOnly) || out.write(svg.toUtf8()) < 0 || !out.commit())
        return tr("Could not write %1").arg(iconPath());
    return {};
}

// A theme-set hook so the icon follows theme changes made anywhere, not only
// while Settings is open. Installed through `omarchy hook install`.
QString Store::ensureIconHook() {
    const QString hook = m_paths.config + "/omarchy/hooks/theme-set.d/omasettings-icon";
    if (QFileInfo::exists(hook))
        return {};
    QTemporaryDir dir;
    const QString script = dir.filePath("omasettings-icon");
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not write %1").arg(script);
    file.write("#!/bin/bash\n"
               "# Draws the Settings app icon in the new theme's colors (installed by Settings).\n"
               "command -v omasettings >/dev/null && omasettings icon >/dev/null 2>&1\n"
               "exit 0\n");
    file.close();
    return runOk("omarchy-hook-install", {"theme-set", script}, 10000);
}
