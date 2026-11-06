#include "cli.h"

#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

static QTextStream &out() {
    static QTextStream s(stdout);
    return s;
}

static QTextStream &err() {
    static QTextStream s(stderr);
    return s;
}

static const char *usage =
    "Usage: omasettings [open [pane]]\n"
    "       omasettings list [pane] [--json]\n"
    "       omasettings get <id> [--json]\n"
    "       omasettings set <id> <value> [--json]\n"
    "       omasettings reset <id> [--json]\n"
    "       omasettings data <list> [--json]\n"
    "       omasettings skill install [--dir <path>]\n"
    "\n"
    "Panes: wifi bluetooth sound power general appearance wallpaper desktop\n"
    "       displays notifications lock security keyboard pointer\n";

bool isCliCommand(const QStringList &args) {
    const QString command = args.value(1);
    return QStringList{"list", "get", "set", "reset", "data", "skill", "help", "--help", "-h", "--version"}.contains(command);
}

static QString plain(const QVariant &v) {
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? "true" : "false";
    if (v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QVariantMap)
        return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
    return v.toString();
}

static int fail(bool json, const QString &message) {
    if (json)
        out() << QJsonDocument(QJsonObject{{"ok", false}, {"error", message}}).toJson(QJsonDocument::Compact) << "\n";
    err() << "omasettings: " << message << "\n";
    return 1;
}

static QJsonObject describe(const Setting &s, const QVariant &value) {
    QJsonObject o{{"id", s.id}, {"pane", s.pane}, {"type", s.type}, {"label", s.label},
                  {"value", QJsonValue::fromVariant(value)}};
    if (s.max > s.min) {
        o.insert("min", s.min);
        o.insert("max", s.max);
    }
    if (!s.choices.isEmpty())
        o.insert("choices", QJsonArray::fromVariantList(s.choices));
    if (!s.choicesList.isEmpty())
        o.insert("choicesFrom", "omasettings data " + s.choicesList);
    return o;
}

static const char *skill = R"(---
name: omasettings
description: Read and change Omarchy desktop settings (Wi-Fi, sound, displays, keyboard, mouse and trackpad, appearance, theme, lock screen, default apps) through the omasettings CLI instead of editing config files. Use when asked to change a desktop setting on Omarchy.
---

# Omarchy settings

`omasettings` is the Settings app's command line. Every setting the window
shows can be read and changed here, with the same safety checks: Hyprland
changes go to one generated file and are rolled back if Hyprland rejects them.

```sh
omasettings list --json               # every setting, its type, range, choices, value
omasettings list keyboard --json      # one pane
omasettings get keyboard.repeatRate
omasettings set keyboard.repeatRate 45 --json
omasettings data monitors --json      # live data: wifi, bluetooth, sinks, monitors, themes, ...
```

- Commands exit 0 on success and 1 on failure, with the reason on stderr.
- Values: booleans as `true`/`false`, numbers as numbers, choices exactly as
  listed. `choicesFrom` names the `data` list that holds a setting's choices.
- Run `list` first rather than guessing ids.
- Some changes ask for a password through the desktop's polkit prompt (time
  zone, computer name); tell the user to watch for it.
- `omasettings open <pane>` opens the window on a pane for the user.
)";

static int installSkill(const QStringList &args) {
    QString dir = QDir::homePath() + "/.claude/skills/omasettings";
    const int at = args.indexOf("--dir");
    if (at >= 0 && at + 1 < args.size())
        dir = args.value(at + 1);
    QDir().mkpath(dir);
    QSaveFile file(dir + "/SKILL.md");
    const QByteArray data(skill);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        err() << "omasettings: could not write " << file.fileName() << "\n";
        return 1;
    }
    out() << "Installed " << dir << "/SKILL.md\n";
    return 0;
}

int runCli(const QStringList &all, Store &store) {
    QStringList args = all.mid(1);
    const bool json = args.removeAll("--json") > 0;
    QString command = args.value(0);

    if (command == "help" || command == "--help" || command == "-h") {
        out() << usage;
        return 0;
    }
    if (command == "--version") {
        out() << "omasettings 0.1.0\n";
        return 0;
    }
    if (command == "skill") {
        if (args.value(1) != "install")
            return fail(false, "usage: omasettings skill install [--dir <path>]");
        return installSkill(args);
    }

    if (command == "list") {
        const QString pane = args.value(1);
        QJsonArray rows;
        for (const Setting &s : store.settings()) {
            if (!pane.isEmpty() && s.pane != pane)
                continue;
            const QVariant value = store.read(s.id);
            rows << describe(s, value);
            if (!json)
                out() << s.id.leftJustified(32) << " " << plain(value) << "\n";
        }
        if (rows.isEmpty())
            return fail(json, "no settings in pane " + pane);
        if (json)
            out() << QJsonDocument(rows).toJson(QJsonDocument::Compact) << "\n";
        return 0;
    }

    if (command == "reset") {
        const QString id = args.value(1);
        const Setting *s = store.setting(id);
        if (!s)
            return fail(json, "unknown setting " + id + " (see omasettings list)");
        if (s->hyprOption.isEmpty())
            return fail(json, id + " is not kept by Settings, so there is nothing to reset; use set");
        const QString error = store.resetHypr(s->hyprOption);
        if (!error.isEmpty())
            return fail(json, error);
        command = "get";
    }

    if (command == "get" || command == "set") {
        const QString id = args.value(1);
        const Setting *s = store.setting(id);
        if (!s)
            return fail(json, "unknown setting " + id + " (see omasettings list)");
        if (command == "set") {
            if (args.size() < 3)
                return fail(json, "usage: omasettings set <id> <value>");
            const QString error = store.write(id, args.value(2));
            if (!error.isEmpty())
                return fail(json, error);
        }
        const QVariant value = store.read(id);
        if (json) {
            QJsonObject o = describe(*s, value);
            o.insert("ok", true);
            out() << QJsonDocument(o).toJson(QJsonDocument::Compact) << "\n";
        } else {
            out() << plain(value) << "\n";
        }
        return 0;
    }

    if (command == "data") {
        const QString name = args.value(1);
        if (!store.listNames().contains(name))
            return fail(json, "unknown list " + name + "; one of: " + store.listNames().join(", "));
        const QVariant data = store.list(name);
        out() << QJsonDocument::fromVariant(data).toJson(json ? QJsonDocument::Compact : QJsonDocument::Indented);
        if (json)
            out() << "\n";
        return 0;
    }

    return -1;
}
