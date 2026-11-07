// The apps and tools Omarchy's Install and Remove menus offer, read from
// the menu as Omarchy ships it, so they stay reachable when those menus are
// hidden and new ones show up without a change here. What is installed is
// decided by the menu's own conditions (an install item's "disabled", a
// remove item's "when"), and installing or removing runs the menu's own
// action. Only the shipped, root-owned menu is read: the user's extension
// file can hide items, and here hiding them is the point.

#include "store.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

namespace {

// Sections of Install and Remove listed as apps, in the order shown.
const QStringList categories{"browser", "ai", "agent", "service", "editor", "terminal", "gaming", "development", "dictation"};
// Leaves at the top of Install and Remove that open a picker or a tool.
const QStringList tools{"package", "aur", "tui", "windows", "preinstalls"};

QString shellQuote(const QString &s) {
    return "'" + QString(s).replace("'", "'\\''") + "'";
}

// A coding agent as omarchy-default-agent defines it: its definitions
// (everything above its install check) are read from the script, which sets
// $name and $agent_tool and defines agent_present. When the script no longer
// has that check, nothing is run and this fails.
QString agentDefinitions(const QString &name) {
    return QStringLiteral(
               "src=$(awk '/^if \\[\\[ \\$installing == \"false\" \\]\\] && ! agent_present/{found=1; exit} {print} "
               "END{exit !found}' \"$(command -v omarchy-default-agent)\") && set -- %1 && eval \"$src\"")
        .arg(shellQuote(name));
}

// Whether the agent is installed, as omarchy-default-agent decides it before
// offering to install one.
QString agentPresent(const QString &name) {
    return agentDefinitions(name) + " && agent_present";
}

// Removing a mise-installed agent the way Omarchy retires a mise copy of
// Hermes: out of the user's global config (where `mise use -g` put it), then
// every installed version. Omarchy's own lazy declaration stays, as for any
// tool it declares. A copy the user installed in ~/.local/bin is theirs.
QString agentRemoval(const QString &name) {
    return QStringLiteral(
               "( %1 || exit 1; echo \"Removing $name\"; mise rm -g \"$agent_tool\" >/dev/null 2>&1; "
               "mise uninstall --all \"$agent_tool\" >/dev/null 2>&1; "
               "if agent_present; then echo \"$name is still installed, perhaps a copy of your own in ~/.local/bin.\"; exit 1; fi; "
               "echo \"$name is removed.\" )")
        .arg(agentDefinitions(name));
}

// The names a default-app setting uses for an item ("code" for VSCode).
QStringList defaultNames(const QString &name) {
    static const QHash<QString, QStringList> aliases{
        {"vscode", {"code", "visual-studio-code"}}, {"sublime", {"subl", "sublime_text", "sublime-text"}},
        {"helix", {"hx", "helix"}}, {"vim", {"vim"}}, {"chrome", {"chrome", "google-chrome"}},
        {"edge", {"edge", "microsoft-edge"}}, {"zen", {"zen", "zen-browser"}}};
    return aliases.value(name, {name});
}

} // namespace

QList<QVariantMap> Store::softwareItems(bool evaluate) {
    const QJsonObject menu = defaultMenuItems();
    auto entry = [&](const QString &id) { return menu.value(id).toObject(); };
    auto hasAction = [&](const QString &id) { return !entry(id).value("action").toString().isEmpty(); };

    // Each item by its key: the path under install./remove. ("browser.zen"),
    // or "dictation.voxtype" and "agent.claude", whose installs live under
    // Setup's default dictation and agent, which install what's missing.
    QMap<QString, QPair<QString, QString>> found;  // key -> install id, remove id
    for (auto it = menu.begin(); it != menu.end(); ++it) {
        const QString id = it.key();
        if (!hasAction(id))
            continue;
        QString key;
        bool install = true;
        if (id.startsWith("install.")) {
            key = id.mid(8);
        } else if (id.startsWith("remove.")) {
            key = id.mid(7);
            install = false;
        } else if (id.startsWith("setup.default.dictation.")) {
            key = "dictation." + id.mid(24);
        } else if (id.startsWith("setup.default.agent.")) {
            key = "agent." + id.mid(20);
        } else {
            continue;
        }
        if (!categories.contains(key.section('.', 0, 0)) && !tools.contains(key))
            continue;
        auto &pair = found[key];
        (install ? pair.first : pair.second) = id;
    }

    // The terminal and editor in use are never removed; the browser only
    // gets a warning, since Omarchy's own menu offers to remove it.
    const QString terminal = read("general.terminal").toString().toLower();
    const QString editor = read("general.editor").toString().toLower();
    const QString browser = evaluate ? read("general.browser").toString().toLower() : QString();
    const QString agent = evaluate ? read("general.agent").toString().toLower() : QString();
    static const QRegularExpression pkgPresent(R"(^omarchy-pkg-present ([a-z0-9@._+-]+)$)");

    QList<QVariantMap> items;
    QStringList checks;
    for (auto it = found.begin(); it != found.end(); ++it) {
        const QString key = it.key(), installId = it.value().first;
        QString removeId = it.value().second;
        // A removal with nothing to install is a setup switch (the SSH
        // agent), which Login & Security already has.
        if (installId.isEmpty())
            continue;
        const QJsonObject in = entry(installId), out = entry(removeId);
        const bool isTool = tools.contains(key);
        const QString category = isTool ? QStringLiteral("tools") : key.section('.', 0, 0);
        const QString name = key.section('.', -1);
        // Hermes and OpenClaw are installed with their desktop apps (their
        // agent_installer in omarchy-default-agent), which the menu removes;
        // Settings removes the agents mise installed. Claude and Amp also
        // have desktop apps, but those aren't where their agents come from.
        static const QStringList withApp{"hermes", "openclaw"};
        if (category == "agent" && removeId.isEmpty() && withApp.contains(name) && hasAction("remove.ai." + name))
            removeId = "remove.ai." + name;
        const bool agentDrop = category == "agent" && removeId.isEmpty();

        QString label = (installId.isEmpty() ? out : in).value("label").toString();
        // Node.js sits under JavaScript in the menu; here the list is flat.
        if (key.count('.') == 2) {
            const QString parent = entry((installId.isEmpty() ? removeId : installId).section('.', 0, -2)).value("label").toString();
            if (!parent.isEmpty() && parent != label)
                label = QStringLiteral("%1 (%2)").arg(label, parent);
        }

        const QString availableIf = in.value("when").toString();
        QStringList installedIf;
        for (const QString &c : {in.value("disabled").toString(), out.value("when").toString()})
            if (!c.isEmpty())
                installedIf << c;
        // The agent menu marks only the default; the script knows the rest.
        if (category == "agent")
            installedIf = QStringList{agentPresent(name)};
        const auto pkg = pkgPresent.match(in.value("disabled").toString().trimmed());
        const QString package = pkg.hasMatch() ? pkg.captured(1) : QString();

        bool isDefault = false;
        QString defaultKind;
        if (category == "terminal" && defaultNames(name).contains(terminal)) {
            isDefault = true;
            defaultKind = "terminal";
        } else if (category == "editor" && defaultNames(name).contains(editor)) {
            isDefault = true;
            defaultKind = "editor";
        } else if (category == "browser" && !browser.isEmpty() && defaultNames(name).contains(browser)) {
            defaultKind = "browser";
        } else if (category == "agent" && !agent.isEmpty() && name == agent) {
            defaultKind = "agent";
        }

        QVariantMap item{{"key", key}, {"category", category}, {"label", label},
                         {"icon", (installId.isEmpty() ? out : in).value("icon").toString()},
                         {"iconFont", (installId.isEmpty() ? out : in).value("iconFont").toString()},
                         {"installId", installId}, {"removeId", removeId}, {"package", package},
                         {"dropRemove", agentDrop || (removeId.isEmpty() && !package.isEmpty() && !isDefault)},
                         {"isDefault", isDefault}, {"defaultKind", defaultKind},
                         {"known", !installedIf.isEmpty()}, {"available", true}, {"installed", false}};
        items << item;

        if (!evaluate)
            continue;
        if (!availableIf.isEmpty())
            checks << QStringLiteral("{ ( eval %1 ) >/dev/null 2>&1 </dev/null && echo %2; } &")
                          .arg(shellQuote(availableIf), shellQuote("a:" + key));
        for (const QString &c : installedIf)
            checks << QStringLiteral("{ ( eval %1 ) >/dev/null 2>&1 </dev/null && echo %2; } &")
                          .arg(shellQuote(c), shellQuote("i:" + key));
    }

    if (evaluate && !checks.isEmpty()) {
        // The menu's own conditions, all at once in one shell: they're the
        // same commands the menu runs to grey items out.
        const QString script = checks.join('\n') + "\nwait\n";
        QSet<QString> passed;
        for (const QString &line : run("bash", {"-c", script}, 30000).text().split('\n'))
            passed.insert(line.trimmed());
        for (QVariantMap &item : items) {
            const QString key = item.value("key").toString();
            if (!entry(item.value("installId").toString()).value("when").toString().isEmpty())
                item["available"] = passed.contains("a:" + key);
            item["installed"] = passed.contains("i:" + key);
        }
    }

    std::sort(items.begin(), items.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const int ca = a.value("category") == "tools" ? -1 : int(categories.indexOf(a.value("category").toString()));
        const int cb = b.value("category") == "tools" ? -1 : int(categories.indexOf(b.value("category").toString()));
        if (ca != cb)
            return ca < cb;
        if (ca == -1)
            return tools.indexOf(a.value("key").toString()) < tools.indexOf(b.value("key").toString());
        return a.value("label").toString().compare(b.value("label").toString(), Qt::CaseInsensitive) < 0;
    });
    return items;
}

QVariant Store::listSoftware() {
    QVariantList out;
    for (const QVariantMap &item : softwareItems(true))
        out << item;
    return out;
}

QVariant Store::listDefaultApps() {
    // Each choice and the command omarchy-default-browser, -terminal, and
    // -editor look for to decide whether it's installed.
    using Choices = QList<QPair<QString, QString>>;
    static const QList<QPair<QString, Choices>> kinds{
        {"browser", {{"chromium", "chromium"}, {"chrome", "google-chrome-stable"}, {"brave", "brave"},
                     {"brave-origin", "brave-origin"}, {"edge", "microsoft-edge-stable"}, {"firefox", "firefox"},
                     {"zen", "zen-browser"}}},
        {"terminal", {{"alacritty", "alacritty"}, {"foot", "foot"}, {"ghostty", "ghostty"}, {"kitty", "kitty"}}},
        {"editor", {{"nvim", "nvim"}, {"code", "code"}, {"cursor", "cursor"}, {"zed", "zeditor"},
                    {"sublime_text", "subl"}, {"helix", "helix"}, {"vim", "vim"}, {"emacs", "emacs"}}}};
    QVariantMap out;
    for (const auto &[kind, choices] : kinds) {
        QVariantList installed;
        for (const auto &[value, command] : choices)
            if (!QStandardPaths::findExecutable(command).isEmpty())
                installed << value;
        out.insert(kind, installed);
    }
    return out;
}

QString Store::softwareCommand(const QString &action, const QString &arg, QStringList &command) {
    if (action == "software.run") {
        // Only an action from Omarchy's shipped menu, under Install, Remove,
        // or the default dictation or agent, which install what they pick.
        if (!arg.startsWith("install.") && !arg.startsWith("remove.") && !arg.startsWith("setup.default.dictation.")
            && !arg.startsWith("setup.default.agent."))
            return tr("Unknown menu item %1").arg(arg);
        const QString run = defaultMenuItems().value(arg).toObject().value("action").toString();
        if (run.isEmpty())
            return tr("Unknown menu item %1").arg(arg);
        command = {"bash", "-c", run};
        return {};
    }
    if (action == "software.drop") {
        // Apps Omarchy installs but has no removal for: drop the package it
        // checks for, never the terminal or editor in use; or a coding agent
        // mise installed.
        for (const QVariantMap &item : softwareItems(false)) {
            if (item.value("key") != arg)
                continue;
            if (item.value("isDefault").toBool())
                return item.value("defaultKind") == "terminal"
                    ? tr("%1 is your default terminal; pick another under General › Default Apps first").arg(item.value("label").toString())
                    : tr("%1 is your default editor; pick another under General › Default Apps first").arg(item.value("label").toString());
            if (!item.value("dropRemove").toBool())
                return tr("Settings can't remove %1").arg(item.value("label").toString());
            if (item.value("category") == "agent") {
                command = {"omarchy-launch-floating-terminal-with-presentation", agentRemoval(arg.section('.', 1))};
                return {};
            }
            command = {"omarchy-launch-floating-terminal-with-presentation", "omarchy-pkg-drop " + item.value("package").toString()};
            return {};
        }
        return tr("Unknown app %1").arg(arg);
    }
    return tr("Unknown action %1").arg(action);
}
