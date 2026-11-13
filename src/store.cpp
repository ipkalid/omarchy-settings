#include "store.h"

#include "hyprconf.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QUrl>
#include <QVariantMap>

// Messages people see go through the app's translator.
static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

// --- Paths ------------------------------------------------------------------

Paths Paths::system() {
    Paths p;
    const QString home = QDir::homePath();
    p.config = qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME") ? home + "/.config" : qEnvironmentVariable("XDG_CONFIG_HOME");
    p.state = qEnvironmentVariableIsEmpty("XDG_STATE_HOME") ? home + "/.local/state" : qEnvironmentVariable("XDG_STATE_HOME");
    p.data = qEnvironmentVariableIsEmpty("XDG_DATA_HOME") ? home + "/.local/share" : qEnvironmentVariable("XDG_DATA_HOME");
    p.home = home;
    p.downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    p.omarchy = qEnvironmentVariableIsEmpty("OMARCHY_PATH") ? "/usr/share/omarchy" : qEnvironmentVariable("OMARCHY_PATH");
    p.hyprland = !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
    return p;
}

// --- Small helpers ----------------------------------------------------------

static QString readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// Writes beside the target and renames over it, so a crash or a full disk
// never leaves a half-written config behind.
static QString writeAtomically(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return tr("Could not write %1").arg(path);
    return {};
}

static bool truthy(const QVariant &v) {
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().toLower();
        return s == "true" || s == "on" || s == "yes" || s == "1";
    }
    return v.toBool();
}

static QVariantMap entry(std::initializer_list<std::pair<QString, QVariant>> items) {
    QVariantMap m;
    for (const auto &[k, v] : items)
        m.insert(k, v);
    return m;
}

Store::Store(Paths paths, Exec exec, SecretWriter secrets)
    : m_paths(std::move(paths)), m_exec(std::move(exec)), m_secrets(std::move(secrets)),
      m_launch([](const QString &program, const QStringList &args) { return QProcess::startDetached(program, args); }) {
    build();
    // A copy of Sync an earlier version left open to other accounts is closed.
    makeSyncDirPrivate();
}

ProcResult Store::run(const QString &program, const QStringList &args, int timeoutMs, const QByteArray &input) const {
    // Omarchy's commands live in $OMARCHY_PATH/bin, which the session puts on
    // PATH; look there too so a launch from a bare environment still works.
    QString resolved = program;
    if (!program.contains('/')) {
        resolved = QStandardPaths::findExecutable(program);
        if (resolved.isEmpty())
            resolved = QStandardPaths::findExecutable(program, {m_paths.omarchy + "/bin"});
        if (resolved.isEmpty())
            resolved = program;
    }
    return m_exec(resolved, args, timeoutMs, input);
}

// Windows the app opens (terminals, editors) outlive it, so they start
// detached instead of being waited on.
QString Store::launch(const QString &program, const QStringList &args) const {
    QString resolved = QStandardPaths::findExecutable(program);
    if (resolved.isEmpty())
        resolved = QStandardPaths::findExecutable(program, {m_paths.omarchy + "/bin"});
    if (resolved.isEmpty() || !m_launch(resolved, args))
        return tr("%1 is not installed").arg(program);
    return {};
}

// Omarchy's floating terminal runs what it's given as one line of shell
// ("$*" into bash -c), so each word is quoted: a value such as a theme's
// folder name or a font's name stays one argument, whatever it holds.
QString Store::launchTerminal(const QStringList &command) const {
    QStringList words;
    for (const QString &word : command)
        words << "'" + QString(word).replace("'", "'\\''") + "'";
    return launch("omarchy-launch-floating-terminal-with-presentation", {words.join(' ')});
}

QString Store::runOk(const QString &program, const QStringList &args, int timeoutMs) const {
    const ProcResult r = run(program, args, timeoutMs);
    return r.ok() ? QString() : r.failure(program);
}

QString Store::output(const QString &program, const QStringList &args, int timeoutMs) const {
    const ProcResult r = run(program, args, timeoutMs);
    return r.ok() ? r.text() : QString();
}

bool Store::flag(const QString &relative) const {
    return QFileInfo::exists(m_paths.state + "/omarchy/toggles/" + relative);
}

// --- Hyprland -----------------------------------------------------------------

QJsonObject Store::managedState() const {
    return QJsonDocument::fromJson(readFile(statePath()).toUtf8()).object();
}

QVariant Store::hyprGet(const QString &option) const {
    if (m_paths.hyprland) {
        const ProcResult r = run("hyprctl", {"-j", "getoption", option}, 2000);
        if (r.ok()) {
            const QVariant v = Hypr::parseOption(r.out);
            if (v.isValid())
                return v;
        }
    }
    // Without a running Hyprland, show what Settings last saved.
    const QJsonValue saved = managedState().value("hypr").toObject().value(option);
    return saved.isUndefined() ? QVariant() : saved.toVariant();
}

QString Store::hyprSet(const QVariantMap &options) {
    QMutexLocker lock(&m_stateLock);
    QJsonObject state = managedState();
    QJsonObject hypr = state.value("hypr").toObject();
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (!Hypr::validOption(it.key()))
            return tr("Unknown option %1").arg(it.key());
        if (it.value().isNull())
            hypr.remove(it.key());
        else
            hypr.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    state.insert("hypr", hypr);
    return applyState(state);
}

QString Store::resetHypr(const QString &option) {
    return hyprSet({{option, QVariant()}});
}

// Makes sure hyprland.lua loads the generated file. Omarchy's hyprland.lua
// invites personal additions at its end, so the line goes there, after a
// backup.
QString Store::ensureRequired() {
    const QString main = m_paths.config + "/hypr/hyprland.lua";
    const QString text = readFile(main);
    if (text.isEmpty() || text.contains("require(\"hypr.omasettings\")"))
        return {};
    const QString backup = main + ".bak." + QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
    if (!QFile::copy(main, backup))
        return tr("Could not back up %1").arg(main);
    QString next = text;
    if (!next.endsWith('\n'))
        next += '\n';
    next += "\n-- Load settings written by the Settings app (omasettings:managed).\nrequire(\"hypr.omasettings\")\n";
    return writeAtomically(main, next.toUtf8());
}

// Writes the generated file, reloads Hyprland, and takes the change back if
// Hyprland reports a new error in it, so a bad value never sticks.
QString Store::applyState(const QJsonObject &next) {
    const QString lua = managedLuaPath();
    const bool existed = QFileInfo::exists(lua);
    const QString previous = readFile(lua);
    const QString rendered = Hypr::renderLua(next);

    if (QString error = ensureRequired(); !error.isEmpty())
        return error;

    QString errorsBefore;
    if (m_paths.hyprland)
        errorsBefore = output("hyprctl", {"configerrors"}, 3000);

    if (QString error = writeAtomically(lua, rendered.toUtf8()); !error.isEmpty())
        return error;

    if (m_paths.hyprland) {
        run("hyprctl", {"reload"}, 5000);
        const QString errorsAfter = output("hyprctl", {"configerrors"}, 3000);
        QStringList fresh;
        for (const QString &line : errorsAfter.split('\n', Qt::SkipEmptyParts))
            if (line.contains("omasettings.lua") && !errorsBefore.contains(line))
                fresh << line.section(':', 2).trimmed();
        if (!fresh.isEmpty()) {
            if (existed)
                writeAtomically(lua, previous.toUtf8());
            else
                QFile::remove(lua);
            run("hyprctl", {"reload"}, 5000);
            return tr("Hyprland rejected the change: %1").arg(fresh.join("; "));
        }
    }

    QByteArray json = QJsonDocument(next).toJson(QJsonDocument::Indented);
    return writeAtomically(statePath(), reindentJson(json).toUtf8());
}

// --- Omarchy shell config ------------------------------------------------------

static QJsonValue valueAt(const QJsonObject &root, const QStringList &path) {
    QJsonValue v = root;
    for (const QString &key : path) {
        if (!v.isObject())
            return QJsonValue::Undefined;
        v = v.toObject().value(key);
    }
    return v;
}

static QJsonObject withValue(QJsonObject root, const QStringList &path, int index, const QJsonValue &value) {
    if (index == path.size() - 1) {
        root.insert(path[index], value);
        return root;
    }
    root.insert(path[index], withValue(root.value(path[index]).toObject(), path, index + 1, value));
    return root;
}

QJsonValue Store::shellGet(const QString &path, const QJsonValue &fallback) const {
    const QStringList keys = path.split('.');
    for (const QString &file : {m_paths.config + "/omarchy/shell.json", m_paths.omarchy + "/config/omarchy/shell.json"}) {
        const QJsonValue v = valueAt(QJsonDocument::fromJson(readFile(file).toUtf8()).object(), keys);
        if (!v.isUndefined() && !v.isNull())
            return v;
    }
    return fallback;
}

QString Store::shellSet(const QString &path, const QJsonValue &value) {
    const QString file = m_paths.config + "/omarchy/shell.json";
    QJsonParseError parseError;
    const QString text = readFile(file);
    QJsonObject root;
    if (!text.trimmed().isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        // Never overwrite a file we could not understand.
        if (parseError.error != QJsonParseError::NoError)
            return tr("%1 has a syntax error: %2").arg(file, parseError.errorString());
        root = doc.object();
    }
    root = withValue(root, path.split('.'), 0, value);
    return writeAtomically(file, reindentJson(QJsonDocument(root).toJson(QJsonDocument::Indented)).toUtf8());
}

// Qt indents JSON by four spaces; Omarchy's files use two.
QString Store::reindentJson(const QByteArray &json) {
    QStringList lines = QString::fromUtf8(json).split('\n');
    for (QString &line : lines) {
        int n = 0;
        while (n < line.size() && line[n] == ' ')
            ++n;
        line = QString(n / 2, ' ') + line.mid(n);
    }
    // Qt also opens empty arrays and objects across two lines; keep them [].
    static const QRegularExpression empty(R"(([\[{])\n\s*([\]}]))");
    return lines.join('\n').replace(empty, "\\1\\2");
}

// --- Parsers ---------------------------------------------------------------------

QStringList Store::splitTerse(const QString &line) {
    // nmcli -t separates fields with ':' and escapes literal ones as '\:'.
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            current += line[++i];
        } else if (line[i] == ':') {
            fields << current;
            current.clear();
        } else {
            current += line[i];
        }
    }
    fields << current;
    return fields;
}

QVariantList Store::parseWifi(const QByteArray &nmcli) {
    QMap<QString, QVariantMap> best;
    for (const QString &line : QString::fromUtf8(nmcli).split('\n', Qt::SkipEmptyParts)) {
        const QStringList f = splitTerse(line);
        if (f.size() < 4 || f[1].isEmpty())
            continue;
        const QVariantMap network = entry({{"active", f[0] == "*"}, {"ssid", f[1]}, {"signal", f[2].toInt()},
                                           {"security", f[3] == "--" ? QString() : f[3]}});
        auto it = best.find(f[1]);
        if (it == best.end() || network["active"].toBool() ||
            (!it->value("active").toBool() && network["signal"].toInt() > it->value("signal").toInt()))
            best.insert(f[1], network);
    }
    QVariantList out;
    for (const QVariantMap &n : best)
        out << n;
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x["active"].toBool() != y["active"].toBool())
            return x["active"].toBool();
        return x["signal"].toInt() > y["signal"].toInt();
    });
    return out;
}

QVariantList Store::parseKeybindings(const QString &text) {
    QVariantList out;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const int arrow = line.indexOf(QStringLiteral("→"));
        if (arrow < 0)
            continue;
        const QString keys = line.left(arrow).simplified();
        const QString action = line.mid(arrow + 1).simplified();
        if (!keys.isEmpty())
            out << entry({{"keys", keys}, {"action", action}});
    }
    return out;
}

QVariantList Store::parseXkbLayouts(const QString &baseLst) {
    QVariantList out;
    bool inLayouts = false;
    static const QRegularExpression row(R"(^\s+(\S+)\s+(.+)$)");
    for (const QString &line : baseLst.split('\n')) {
        if (line.startsWith('!')) {
            inLayouts = line.trimmed() == "! layout";
            continue;
        }
        if (!inLayouts)
            continue;
        const auto m = row.match(line);
        // "custom" is xkb's placeholder for a hand-written layout file.
        if (m.hasMatch() && m.captured(1) != "custom")
            out << entry({{"code", m.captured(1)}, {"name", m.captured(2).trimmed()}});
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["name"].toString().localeAwareCompare(b.toMap()["name"].toString()) < 0;
    });
    return out;
}

int Store::parsePercent(const QString &text) {
    static const QRegularExpression percent(R"((\d+)%)");
    const auto m = percent.match(text);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

// --- Settings ---------------------------------------------------------------------

const Setting *Store::setting(const QString &id) const {
    for (const Setting &s : m_settings)
        if (s.id == id)
            return &s;
    return nullptr;
}

QVariant Store::read(const QString &id) const {
    const Setting *s = setting(id);
    return s && s->read ? s->read() : QVariant();
}

QString Store::write(const QString &id, const QVariant &value) {
    const Setting *s = setting(id);
    if (!s)
        return tr("Unknown setting %1").arg(id);
    // No value means "as it was before Settings changed it": undoing the
    // first change to an option hands it back to the user's own config.
    if ((!value.isValid() || value.isNull()) && !s->hyprOption.isEmpty())
        return resetHypr(s->hyprOption);
    QVariant v;
    if (const QString error = normalize(*s, value, v); !error.isEmpty())
        return error;
    return s->write(v);
}

// A value in the form a setting stores it, checked against its type, range,
// and choices.
QString Store::normalize(const Setting &setting, const QVariant &value, QVariant &out) const {
    const Setting *s = &setting;
    const QString &id = s->id;
    QVariant v = value;
    if (s->type == "bool") {
        v = truthy(value);
    } else if (s->type == "int" || s->type == "real") {
        bool ok = false;
        const double d = value.toDouble(&ok);
        if (!ok)
            return tr("%1 needs a number").arg(id);
        if (s->max > s->min && (d < s->min || d > s->max))
            return tr("%1 must be between %2 and %3").arg(id).arg(s->min).arg(s->max);
        v = s->type == "int" ? QVariant(qRound(d)) : QVariant(d);
    } else if (s->type == "choice" && !s->choices.isEmpty()) {
        bool found = false;
        for (const QVariant &c : s->choices)
            if (c.toString() == value.toString()) {
                v = c;
                found = true;
            }
        if (!found)
            return tr("%1 is not one of the choices for %2").arg(value.toString(), id);
    }
    out = v;
    return {};
}

void Store::add(Setting setting) {
    m_settings << std::move(setting);
}

void Store::addHypr(const QString &id, const QString &pane, const QString &type, const QString &option,
                    const QString &label, double min, double max, double step) {
    Setting s;
    s.id = id;
    s.pane = pane;
    s.type = type;
    s.label = label;
    s.min = min;
    s.max = max;
    s.step = step;
    s.hyprOption = option;
    s.read = [this, option, type] {
        const QVariant v = hyprGet(option);
        if (type == "bool")
            return QVariant(v.toBool());
        return v;
    };
    s.write = [this, option](const QVariant &v) { return hyprSet({{option, v}}); };
    add(std::move(s));
}

// A setting backed by an Omarchy command that prints the current value with
// no arguments and sets it with one.
// omarchy-font-set puts the family name into sed expressions and terminal
// configs as it is, and a font's family comes from the font file, so anyone
// who can get a font installed chooses it. Only plain names go through:
// nothing sed, a config file, or fontconfig's XML would read as syntax.
bool safeFontName(const QString &name) {
    static const QRegularExpression plain(R"(^[\p{L}\p{N}][\p{L}\p{N} ._+-]{0,99}$)");
    return plain.match(name).hasMatch() && !name.endsWith(' ');
}

static Setting commandSetting(const QString &id, const QString &pane, const QString &label,
                              const QVariantList &choices, std::function<QString()> get,
                              std::function<QString(const QString &)> set) {
    Setting s;
    s.id = id;
    s.pane = pane;
    s.type = "choice";
    s.label = label;
    s.choices = choices;
    s.read = [get] { return QVariant(get()); };
    s.write = [set](const QVariant &v) { return set(v.toString()); };
    return s;
}

static QVariantList strings(std::initializer_list<const char *> values) {
    QVariantList out;
    for (const char *v : values)
        out << QString::fromUtf8(v);
    return out;
}

static QVariantList numbers(std::initializer_list<double> values) {
    QVariantList out;
    for (double v : values)
        out << v;
    return out;
}

void Store::build() {
    // ---- Wi-Fi, Bluetooth --------------------------------------------------
    {
        Setting s;
        s.id = "wifi.enabled"; s.pane = "wifi"; s.type = "bool"; s.label = "Wi-Fi";
        s.read = [this] { return QVariant(output("nmcli", {"radio", "wifi"}) == "enabled"); };
        s.write = [this](const QVariant &v) { return runOk("nmcli", {"radio", "wifi", v.toBool() ? "on" : "off"}, 10000); };
        add(s);
    }
    {
        Setting s;
        s.id = "bluetooth.enabled"; s.pane = "bluetooth"; s.type = "bool"; s.label = "Bluetooth";
        s.read = [this] { return QVariant(output("bluetoothctl", {"show"}).contains(QRegularExpression("Powered:\\s+yes"))); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-bluetooth-power", {v.toBool() ? "on" : "off"}, 10000); };
        add(s);
    }

    // ---- Sound -----------------------------------------------------------------
    for (const QString &kind : {QStringLiteral("output"), QStringLiteral("input")}) {
        const bool out = kind == "output";
        const QString device = out ? "@DEFAULT_SINK@" : "@DEFAULT_SOURCE@";
        const QString noun = out ? "sink" : "source";
        Setting d;
        d.id = "sound." + kind; d.pane = "sound"; d.type = "choice";
        d.label = out ? "Output device" : "Input device";
        d.choicesList = out ? "sinks" : "sources";
        d.read = [this, noun] { return QVariant(output("pactl", {"get-default-" + noun})); };
        d.write = [this, noun](const QVariant &v) { return runOk("pactl", {"set-default-" + noun, v.toString()}); };
        add(d);

        Setting vol;
        vol.id = "sound." + kind + "Volume"; vol.pane = "sound"; vol.type = "int";
        vol.label = out ? "Output volume" : "Input volume"; vol.min = 0; vol.max = 150; vol.step = 1;
        vol.read = [this, noun, device] { return QVariant(parsePercent(output("pactl", {"get-" + noun + "-volume", device}))); };
        vol.write = [this, noun, device](const QVariant &v) {
            return runOk("pactl", {"set-" + noun + "-volume", device, QString::number(v.toInt()) + "%"});
        };
        add(vol);

        Setting mute;
        mute.id = "sound." + kind + "Muted"; mute.pane = "sound"; mute.type = "bool";
        mute.label = out ? "Mute output" : "Mute input";
        mute.read = [this, noun, device] { return QVariant(output("pactl", {"get-" + noun + "-mute", device}).contains("yes")); };
        mute.write = [this, noun, device](const QVariant &v) {
            return runOk("pactl", {"set-" + noun + "-mute", device, v.toBool() ? "1" : "0"});
        };
        add(mute);
    }

    // ---- Battery & Power ---------------------------------------------------------
    {
        Setting s;
        s.id = "power.profile"; s.pane = "power"; s.type = "choice"; s.label = "Power mode";
        s.choicesList = "powerProfiles";
        s.read = [this] { return QVariant(output("powerprofilesctl", {"get"})); };
        s.write = [this](const QVariant &v) { return runOk("powerprofilesctl", {"set", v.toString()}); };
        add(s);
    }
    {
        Setting s;
        s.id = "power.suspendInMenu"; s.pane = "power"; s.type = "bool"; s.label = "Show Suspend in the system menu";
        s.read = [this] { return QVariant(!flag("suspend-off")); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-toggle", {"suspend-off", v.toBool() ? "off" : "on"}); };
        add(s);
    }

    // ---- General ---------------------------------------------------------------------
    {
        Setting s;
        s.id = "general.hostname"; s.pane = "general"; s.type = "text"; s.label = "Computer name";
        s.read = [this] {
            const QString name = output("hostnamectl", {"hostname"});
            return QVariant(name.isEmpty() ? QSysInfo::machineHostName() : name);
        };
        s.write = [this](const QVariant &v) {
            static const QRegularExpression valid("^[A-Za-z0-9]([A-Za-z0-9-]{0,62})$");
            if (!valid.match(v.toString()).hasMatch())
                return tr("Use letters, numbers, and dashes, up to 63 characters");
            // Asks for the password through the session's polkit agent.
            return runOk("hostnamectl", {"set-hostname", v.toString()}, 120000);
        };
        add(s);
    }
    add(commandSetting("general.language", "general", "Language", {},
        [this] { return output("omarchy-language", {}); },
        [this](const QString &v) {
            static const QRegularExpression valid("^[a-z]{2,3}(-[A-Za-z]{2})?$");
            if (!valid.match(v).hasMatch())
                return tr("Unknown language %1").arg(v);
            return runOk("omarchy-language", {v}, 180000);
        }));
    m_settings.last().choicesList = "languages";
    add(commandSetting("general.timezone", "general", "Time zone", {},
        [this] { return output("timedatectl", {"show", "-p", "Timezone", "--value"}); },
        [this](const QString &v) {
            static const QRegularExpression valid("^[A-Za-z0-9_+-]+(/[A-Za-z0-9_+-]+)*$");
            if (!valid.match(v).hasMatch())
                return tr("Unknown time zone %1").arg(v);
            const QString error = runOk("timedatectl", {"set-timezone", v}, 120000);
            if (error.isEmpty())
                run("omarchy-shell", {"-q", "omarchy.clock", "refresh"}, 3000);
            return error;
        }));
    m_settings.last().choicesList = "timezones";
    {
        Setting s;
        s.id = "general.autoTime"; s.pane = "general"; s.type = "bool"; s.label = "Set time automatically";
        s.read = [this] { return QVariant(output("timedatectl", {"show", "-p", "NTP", "--value"}) == "yes"); };
        s.write = [this](const QVariant &v) { return runOk("timedatectl", {"set-ntp", v.toBool() ? "true" : "false"}, 120000); };
        add(s);
    }
    add(commandSetting("general.clockFormat", "general", "Clock in the bar",
        strings({"dddd HH:mm", "dddd h:mm AP", "dddd HH:mm:ss", "dddd h:mm:ss AP", "HH:mm", "h:mm AP",
                 "ddd d MMM HH:mm", "ddd d MMM h:mm AP", "d MMMM 'W'ww yyyy", "yyyy-MM-dd HH:mm"}),
        [this] { return output("omarchy-bar", {"get", "omarchy.clock", "format"}); },
        [this](const QString &v) { return runOk("omarchy-bar", {"set", "omarchy.clock", "format", v}); }));
    add(commandSetting("general.browser", "general", "Web browser",
        strings({"chromium", "chrome", "brave", "brave-origin", "edge", "firefox", "zen"}),
        [this] { return output("omarchy-default-browser", {}); },
        [this](const QString &v) { return runOk("omarchy-default-browser", {v}, 15000); }));
    add(commandSetting("general.terminal", "general", "Terminal",
        strings({"alacritty", "foot", "ghostty", "kitty"}),
        [this] { return output("omarchy-default-terminal", {}); },
        [this](const QString &v) { return runOk("omarchy-default-terminal", {v}, 15000); }));
    add(commandSetting("general.editor", "general", "Text editor",
        strings({"nvim", "code", "cursor", "zed", "sublime_text", "helix", "vim", "emacs"}),
        [this] {
            const QString e = output("omarchy-default-editor", {});
            return e == "subl" ? QStringLiteral("sublime_text") : e == "zeditor" ? QStringLiteral("zed") : e;
        },
        [this](const QString &v) { return runOk("omarchy-default-editor", {v}, 15000); }));

    // ---- Appearance --------------------------------------------------------------------
    add(commandSetting("appearance.theme", "appearance", "Theme", {},
        [this] { return output("omarchy-theme-current", {}); },
        [this](const QString &v) { return runOk("omarchy-theme-set", {v}, 120000); }));
    m_settings.last().choicesList = "themes";
    add(commandSetting("appearance.corners", "appearance", "Window corners", strings({"theme", "sharp", "rounded"}),
        [this] { return output("omarchy-hyprland-corners", {}); },
        [this](const QString &v) { return runOk("omarchy-hyprland-corners", {v}, 15000); }));
    add(commandSetting("appearance.transparency", "appearance", "Window transparency",
        strings({"opaque", "transparent", "glassy"}),
        [this] { return output("omarchy-hyprland-window-transparency", {}); },
        [this](const QString &v) { return runOk("omarchy-hyprland-window-transparency", {v}, 30000); }));
    {
        Setting s;
        s.id = "appearance.animations"; s.pane = "appearance"; s.type = "bool"; s.label = "Animations, blur, and shadows";
        s.read = [this] { return QVariant(!flag("hypr/no-animations.lua")); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-toggle-animations", {v.toBool() ? "on" : "off"}, 10000); };
        add(s);
    }
    add(commandSetting("appearance.font", "fonts", "Monospace font", {},
        [this] { return output("omarchy-font-current", {}); },
        [this](const QString &v) {
            if (!safeFontName(v))
                return tr("%1 is not one of the choices for %2").arg(v, QStringLiteral("appearance.font"));
            return runOk("omarchy-font-set", {v}, 60000);
        }));
    m_settings.last().choicesList = "fonts";
    {
        Setting s;
        s.id = "appearance.textSize"; s.pane = "fonts"; s.type = "int"; s.label = "Text size";
        s.min = 9; s.max = 20; s.step = 1;
        s.read = [this] {
            static const QRegularExpression size(R"(text size:\s*(\d+))");
            const auto m = size.match(output("omarchy-display-text-size", {}));
            return m.hasMatch() ? QVariant(m.captured(1).toInt()) : QVariant(12);
        };
        s.write = [this](const QVariant &v) { return runOk("omarchy-display-text-size", {QString::number(v.toInt())}, 30000); };
        add(s);
    }
    add(commandSetting("appearance.barBackground", "bar", "Bar background", strings({"opaque", "glassy", "none"}),
        [this] { return shellGet("bar.background", "opaque").toString(); },
        [this](const QString &v) { return runOk("omarchy-bar", {"background", v}); }));

    // ---- Wallpaper (background picker is a list + action) -------------------------------

    // ---- Desktop & Bar -----------------------------------------------------------------
    {
        Setting s;
        s.id = "desktop.barVisible"; s.pane = "bar"; s.type = "bool"; s.label = "Show the bar";
        s.read = [this] { return QVariant(!flag("bar-off")); };
        // omarchy toggle bar "on" turns the bar-off flag on, hiding the bar.
        s.write = [this](const QVariant &v) { return runOk("omarchy-toggle-bar", {v.toBool() ? "off" : "on"}); };
        add(s);
    }
    add(commandSetting("desktop.barPosition", "bar", "Bar position", strings({"top", "bottom", "left", "right"}),
        [this] { return shellGet("bar.position", "top").toString(); },
        [this](const QString &v) { return runOk("omarchy-bar", {"position", v}); }));
    add(commandSetting("desktop.dockPosition", "desktop", "Dock position", strings({"bottom", "left", "right", "top"}),
        [this] { return shellGet("dock.position", "bottom").toString(); },
        [this](const QString &v) { return runOk("omarchy-dock", {"position", v}); }));
    {
        Setting s;
        s.id = "desktop.dockAutohide"; s.pane = "desktop"; s.type = "bool"; s.label = "Automatically hide the dock";
        s.read = [this] { return QVariant(shellGet("dock.autohide", false).toBool()); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-dock", {"autohide", v.toBool() ? "on" : "off"}); };
        add(s);
    }
    add(commandSetting("desktop.layout", "desktop", "Window layout", strings({"dwindle", "scrolling", "floating"}),
        [this] { return output("omarchy-hyprland-workspace-layout-default", {}); },
        [this](const QString &v) { return runOk("omarchy-hyprland-workspace-layout-default", {v}, 10000); }));
    {
        Setting s;
        s.id = "desktop.gaps"; s.pane = "desktop"; s.type = "bool"; s.label = "Gaps between windows";
        s.read = [this] { return QVariant(!flag("hypr/window-no-gaps.lua")); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-hyprland-toggle", {"window-no-gaps", v.toBool() ? "off" : "on"}); };
        add(s);
    }
    addHypr("desktop.gapsInner", "desktop", "int", "general:gaps_in", "Gap between windows", 0, 30);
    addHypr("desktop.gapsOuter", "desktop", "int", "general:gaps_out", "Gap at screen edges", 0, 60);
    addHypr("desktop.borderSize", "desktop", "int", "general:border_size", "Window border", 0, 10);
    addHypr("desktop.dimInactive", "desktop", "bool", "decoration:dim_inactive", "Dim inactive windows");
    {
        Setting s;
        s.id = "desktop.focusFollowsMouse"; s.pane = "desktop"; s.type = "bool"; s.label = "Focus follows the mouse";
        s.read = [this] { return QVariant(hyprGet("input:follow_mouse").toInt() != 0); };
        s.write = [this](const QVariant &v) { return hyprSet({{"input:follow_mouse", v.toBool() ? 1 : 0}}); };
        add(s);
    }
    {
        Setting s;
        s.id = "desktop.cursorWarps"; s.pane = "desktop"; s.type = "bool"; s.label = "Move the pointer to focused windows";
        s.read = [this] { return QVariant(!hyprGet("cursor:no_warps").toBool()); };
        s.write = [this](const QVariant &v) { return hyprSet({{"cursor:no_warps", !v.toBool()}}); };
        add(s);
    }
    {
        Setting s;
        s.id = "desktop.hideCursor"; s.pane = "desktop"; s.type = "choice"; s.label = "Hide the pointer when idle";
        s.choices = numbers({0, 3, 5, 10, 30});
        s.read = [this] { return QVariant(hyprGet("cursor:inactive_timeout").toDouble()); };
        s.write = [this](const QVariant &v) { return hyprSet({{"cursor:inactive_timeout", v.toDouble()}}); };
        add(s);
    }

    // ---- Displays ------------------------------------------------------------------------
    {
        Setting s;
        s.id = "display.nightLight"; s.pane = "displays"; s.type = "bool"; s.label = "Night Shift";
        auto status = [this] {
            return QJsonDocument::fromJson(output("omarchy-toggle-nightlight", {"--status"}).toUtf8()).object().value("enabled").toBool();
        };
        s.read = [status] { return QVariant(status()); };
        s.write = [this, status](const QVariant &v) {
            if (status() == v.toBool())
                return QString();
            return runOk("omarchy-toggle-nightlight", {}, 10000);
        };
        add(s);
    }
    {
        Setting s;
        s.id = "display.brightness"; s.pane = "displays"; s.type = "int"; s.label = "Brightness";
        s.min = 1; s.max = 100;
        s.read = [this] {
            const QStringList f = output("brightnessctl", {"-m", "-c", "backlight"}).split(',');
            return f.size() > 3 ? QVariant(parsePercent(f[3])) : QVariant();
        };
        s.write = [this](const QVariant &v) {
            return runOk("omarchy-brightness-display", {"--no-osd", QString::number(v.toInt()) + "%"});
        };
        add(s);
    }

    // ---- Notifications --------------------------------------------------------------------
    {
        Setting s;
        s.id = "notifications.doNotDisturb"; s.pane = "notifications"; s.type = "bool"; s.label = "Do Not Disturb";
        s.read = [this] { return QVariant(output("omarchy-shell", {"notifications", "isDnd"}) == "on"); };
        s.write = [this](const QVariant &v) {
            const QString error = runOk("omarchy-shell", {"notifications", "setDnd", v.toBool() ? "on" : "off"});
            run("omarchy-shell", {"-q", "omarchy.indicators", "refresh"}, 3000);
            return error;
        };
        add(s);
    }

    // ---- Lock Screen ------------------------------------------------------------------------
    const QVariantList idleChoices = numbers({0, 60, 120, 150, 300, 600, 900, 1200, 1800, 3600});
    {
        Setting s;
        s.id = "lock.screensaverAfter"; s.pane = "lock"; s.type = "choice"; s.label = "Start the screensaver when idle";
        s.choices = idleChoices;
        s.read = [this] { return QVariant(shellGet("idle.screensaver", 150).toDouble()); };
        s.write = [this](const QVariant &v) { return shellSet("idle.screensaver", v.toInt()); };
        add(s);
    }
    {
        Setting s;
        s.id = "lock.lockAfter"; s.pane = "lock"; s.type = "choice"; s.label = "Lock the screen when idle";
        s.choices = idleChoices;
        s.read = [this] { return QVariant(shellGet("idle.lock", 300).toDouble()); };
        s.write = [this](const QVariant &v) { return shellSet("idle.lock", v.toInt()); };
        add(s);
    }
    {
        Setting s;
        s.id = "lock.screensaver"; s.pane = "lock"; s.type = "bool"; s.label = "Screensaver";
        s.read = [this] { return QVariant(!flag("screensaver-off")); };
        s.write = [this](const QVariant &v) { return runOk("omarchy-toggle", {"screensaver-off", v.toBool() ? "off" : "on"}); };
        add(s);
    }
    {
        Setting s;
        s.id = "lock.stayAwake"; s.pane = "lock"; s.type = "bool"; s.label = "Keep the computer awake";
        s.read = [this] {
            return QVariant(QJsonDocument::fromJson(output("omarchy-toggle-idle", {"status"}).toUtf8()).object().value("enabled").toBool());
        };
        s.write = [this](const QVariant &v) { return runOk("omarchy-toggle-idle", {v.toBool() ? "stay-awake" : "allow-idle"}); };
        add(s);
    }

    // ---- Keyboard ---------------------------------------------------------------------------
    addHypr("keyboard.repeatRate", "keyboard", "int", "input:repeat_rate", "Key repeat rate", 10, 80);
    addHypr("keyboard.repeatDelay", "keyboard", "int", "input:repeat_delay", "Delay until repeat", 150, 1000, 10);
    addHypr("keyboard.numlock", "keyboard", "bool", "input:numlock_by_default", "Num Lock on at login");
    {
        Setting s;
        s.id = "keyboard.layouts"; s.pane = "keyboard"; s.type = "text"; s.label = "Input sources";
        s.choicesList = "keyboardLayouts";
        s.read = [this] { return hyprGet("input:kb_layout"); };
        s.write = [this](const QVariant &v) {
            static const QRegularExpression valid("^[a-z]{2,8}(,[a-z]{2,8})*$");
            const QString layouts = v.toString().remove(' ');
            if (!valid.match(layouts).hasMatch())
                return tr("Layouts are xkb codes like us,de");
            // kb_variant pairs with kb_layout by position, so keep each kept
            // layout's variant and give new ones the default.
            const QStringList oldLayouts = hyprGet("input:kb_layout").toString().split(',');
            const QStringList oldVariants = hyprGet("input:kb_variant").toString().split(',');
            QStringList variants;
            bool any = false;
            for (const QString &layout : layouts.split(',')) {
                const int i = oldLayouts.indexOf(layout);
                const QString variant = i >= 0 && i < oldVariants.size() ? oldVariants[i] : QString();
                any = any || !variant.isEmpty();
                variants << variant;
            }
            return hyprSet({{"input:kb_layout", layouts}, {"input:kb_variant", any ? variants.join(',') : QString()}});
        };
        add(s);
    }
    struct KbChoice { const char *id; const char *label; QStringList prefixes; QVariantList choices; };
    const QList<KbChoice> kbChoices{
        {"keyboard.switchLayout", "Switch input source with", {"grp:"},
         strings({"", "grp:alt_shift_toggle", "grp:ctrl_shift_toggle", "grp:alts_toggle", "grp:caps_toggle", "grp:win_space_toggle"})},
        {"keyboard.capsLock", "Caps Lock key", {"caps:", "ctrl:nocaps", "ctrl:swapcaps"},
         strings({"", "caps:escape", "ctrl:nocaps", "caps:swapescape", "caps:backspace", "caps:none"})},
        {"keyboard.compose", "Compose key", {"compose:"},
         strings({"", "compose:ralt", "compose:rctrl", "compose:caps", "compose:menu", "compose:rwin"})},
    };
    for (const KbChoice &c : kbChoices) {
        Setting s;
        s.id = c.id; s.pane = "keyboard"; s.type = "choice"; s.label = c.label; s.choices = c.choices;
        const QStringList prefixes = c.prefixes;
        s.read = [this, prefixes] { return QVariant(Hypr::findKbOption(hyprGet("input:kb_options").toString(), prefixes)); };
        s.write = [this, prefixes](const QVariant &v) {
            return hyprSet({{"input:kb_options", Hypr::replaceKbOption(hyprGet("input:kb_options").toString(), prefixes, v.toString())}});
        };
        add(s);
    }

    // ---- Mouse & Trackpad -------------------------------------------------------------------
    addHypr("pointer.sensitivity", "pointer", "real", "input:sensitivity", "Tracking speed", -1, 1, 0.05);
    {
        Setting s;
        s.id = "pointer.acceleration"; s.pane = "pointer"; s.type = "choice"; s.label = "Pointer acceleration";
        s.choices = strings({"adaptive", "flat"});
        s.read = [this] {
            const QString v = hyprGet("input:accel_profile").toString();
            return QVariant(v.isEmpty() ? QStringLiteral("adaptive") : v);
        };
        s.write = [this](const QVariant &v) { return hyprSet({{"input:accel_profile", v.toString()}}); };
        add(s);
    }
    addHypr("pointer.naturalScroll", "pointer", "bool", "input:natural_scroll", "Natural scrolling");
    addHypr("pointer.scrollSpeed", "pointer", "real", "input:scroll_factor", "Scrolling speed", 0.2, 3, 0.1);
    addHypr("pointer.leftHanded", "pointer", "bool", "input:left_handed", "Swap left and right buttons");
    addHypr("trackpad.tapToClick", "pointer", "bool", "input:touchpad:tap-to-click", "Tap to click");
    addHypr("trackpad.naturalScroll", "pointer", "bool", "input:touchpad:natural_scroll", "Natural scrolling");
    addHypr("trackpad.twoFingerClick", "pointer", "bool", "input:touchpad:clickfinger_behavior", "Secondary click with two fingers");
    addHypr("trackpad.scrollSpeed", "pointer", "real", "input:touchpad:scroll_factor", "Scrolling speed", 0.1, 3, 0.05);
    addHypr("trackpad.disableWhileTyping", "pointer", "bool", "input:touchpad:disable_while_typing", "Ignore the trackpad while typing");
    {
        Setting s;
        s.id = "trackpad.threeFingerDrag"; s.pane = "pointer"; s.type = "bool"; s.label = "Three-finger drag";
        s.read = [this] { return QVariant(hyprGet("input:touchpad:drag_3fg").toInt() != 0); };
        s.write = [this](const QVariant &v) { return hyprSet({{"input:touchpad:drag_3fg", v.toBool() ? 1 : 0}}); };
        add(s);
    }
    {
        Setting s;
        s.id = "trackpad.swipeWorkspaces"; s.pane = "pointer"; s.type = "bool"; s.label = "Swipe between workspaces";
        s.read = [this] { return QVariant(managedState().value("gestures").toObject().value("workspaceSwipe").toBool()); };
        s.write = [this](const QVariant &v) {
            QMutexLocker lock(&m_stateLock);
            QJsonObject state = managedState();
            QJsonObject gestures = state.value("gestures").toObject();
            gestures.insert("workspaceSwipe", v.toBool());
            state.insert("gestures", gestures);
            return applyState(state);
        };
        add(s);
    }

    buildMore();
}

// --- Lists --------------------------------------------------------------------------------

QStringList Store::listNames() const {
    return {"capabilities", "about", "battery", "updates", "wifi", "wifiScan", "bluetooth", "sinks", "sources", "monitors",
            "themes", "backgrounds", "fonts", "languages", "timezones", "keyboardLayouts", "keybindings",
            "powerProfiles", "vpn", "plugins", "pluginUpdates", "bar", "apps", "fileTypes", "webapps", "dock",
            "wifiBand", "power", "crashMutes", "shortcuts", "loginItems", "windowRules", "firewall", "printers",
            "storage", "homeUsage", "snapshots", "managed", "menuSearch",
            "security", "system", "branding", "bootScreens", "menu", "hardware", "reminders", "fontCatalog", "menuCustom", "software", "defaultApps", "appimages", "appimageUpdates", "sync", "syncPreview", "configFiles"};
}

static QVariantList pactlDevices(const QByteArray &json, const QString &defaultName, bool skipMonitors) {
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        if (skipMonitors && name.endsWith(".monitor"))
            continue;
        const QJsonObject volume = o.value("volume").toObject();
        const int percent = volume.isEmpty() ? 0
            : Store::parsePercent(volume.begin().value().toObject().value("value_percent").toString());
        out << entry({{"name", name}, {"description", o.value("description").toString()},
                      {"volume", percent}, {"muted", o.value("mute").toBool()}, {"isDefault", name == defaultName}});
    }
    return out;
}

// --- Config files -----------------------------------------------------------

// The files a page's settings live in, which it offers to open in the
// person's editor. The key is all a page or the CLI can name.
namespace {
struct ConfigFile {
    const char *key;
    bool inHome; // under ~ rather than ~/.config
    const char *path;
};
const ConfigFile configFiles[] = {
    {"hyprland", false, "hypr/hyprland.lua"},
    {"bindings", false, "hypr/bindings.lua"},
    {"input", false, "hypr/input.lua"},
    {"looknfeel", false, "hypr/looknfeel.lua"},
    {"monitors", false, "hypr/monitors.lua"},
    {"autostart", false, "hypr/autostart.lua"},
    {"hyprsunset", false, "hypr/hyprsunset.conf"},
    {"managed", false, "hypr/omasettings.lua"},
    {"shell", false, "omarchy/shell.json"},
    {"shelltoml", false, "omarchy/shell.toml"},
    {"menu", false, "omarchy/extensions/omarchy-menu.jsonc"},
    {"screensaver", false, "omarchy/branding/screensaver.txt"},
    {"about", false, "omarchy/branding/about.txt"},
    {"mimeapps", false, "mimeapps.list"},
    {"terminals", false, "xdg-terminals.list"},
    {"fontconfig", false, "fontconfig/fonts.conf"},
    {"alacritty", false, "alacritty/alacritty.toml"},
    {"kitty", false, "kitty/kitty.conf"},
    {"ghostty", false, "ghostty/config"},
    {"foot", false, "foot/foot.ini"},
    {"gitconfig", true, ".gitconfig"},
    {"xcompose", true, ".XCompose"},
};
} // namespace

QString Store::configFilePath(const QString &key) const {
    // Git's global file is ~/.gitconfig, or its XDG one when only that is
    // there, the way `git config --global` picks.
    if (key == "gitconfig") {
        const QString xdg = m_paths.config + "/git/config";
        return !QFileInfo::exists(m_paths.home + "/.gitconfig") && QFileInfo::exists(xdg) ? xdg : m_paths.home + "/.gitconfig";
    }
    for (const ConfigFile &f : configFiles)
        if (key == QLatin1String(f.key))
            return (f.inHome ? m_paths.home : m_paths.config) + "/" + QString::fromLatin1(f.path);
    return {};
}

QVariant Store::listConfigFiles() const {
    QVariantList out;
    for (const ConfigFile &f : configFiles) {
        const QString path = configFilePath(QString::fromLatin1(f.key));
        QString shown = path;
        if (!m_paths.home.isEmpty() && path.startsWith(m_paths.home + "/"))
            shown = "~" + path.mid(m_paths.home.size());
        // A bare "config" says more with its folder: git/config.
        const QFileInfo info(path);
        const QString name = info.suffix().isEmpty() && !info.fileName().startsWith('.')
                                 ? info.dir().dirName() + "/" + info.fileName() : info.fileName();
        out << entry({{"key", QString::fromLatin1(f.key)}, {"name", name},
                      {"path", shown}, {"exists", QFileInfo(path).isFile()}});
    }
    return out;
}

QVariant Store::list(const QString &name) {
    if (name == "capabilities") {
        const QString devices = output("nmcli", {"-t", "-f", "TYPE", "device"});
        bool touchpad = false;
        if (m_paths.hyprland) {
            for (const QJsonValue &m : QJsonDocument::fromJson(run("hyprctl", {"-j", "devices"}).out).object().value("mice").toArray()) {
                const QString n = m.toObject().value("name").toString().toLower();
                touchpad = touchpad || n.contains("touchpad") || n.contains("trackpad");
            }
        }
        return entry({{"wifi", devices.split('\n').contains("wifi")},
                      {"bluetooth", !QDir("/sys/class/bluetooth").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()},
                      {"backlight", !QDir("/sys/class/backlight").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()},
                      {"battery", !QDir("/sys/class/power_supply").entryList({"BAT*"}, QDir::AllEntries).isEmpty()},
                      {"touchpad", touchpad}, {"hyprland", m_paths.hyprland}});
    }
    if (name == "about") {
        QString cpu;
        for (const QString &line : readFile("/proc/cpuinfo").split('\n'))
            if (line.startsWith("model name")) {
                cpu = line.section(':', 1).simplified();
                break;
            }
        static const QRegularExpression memTotal(R"(MemTotal:\s+(\d+))");
        const double memoryGb = memTotal.match(readFile("/proc/meminfo")).captured(1).toDouble() / 1024 / 1024;
        QStringList gpus;
        for (const QString &line : output("lspci", {"-mm"}).split('\n')) {
            if (!line.contains("\"VGA compatible controller\"") && !line.contains("\"3D controller\"") &&
                !line.contains("\"Display controller\""))
                continue;
            const QStringList f = line.split('"');
            if (f.size() > 5) {
                QString vendor = f[3];
                vendor.replace("Advanced Micro Devices, Inc. [AMD/ATI]", "AMD").replace("NVIDIA Corporation", "NVIDIA")
                      .replace("Intel Corporation", "Intel");
                gpus << (vendor + " " + f[5]).simplified();
            }
        }
        const QStorageInfo root("/");
        const QString vendor = readFile("/sys/class/dmi/id/sys_vendor").trimmed();
        const QString product = readFile("/sys/class/dmi/id/product_name").trimmed();
        const double uptime = readFile("/proc/uptime").section(' ', 0, 0).toDouble();
        return entry({{"omarchy", output("omarchy-version", {})}, {"channel", output("omarchy-channel-current", {})},
                      {"hostname", QSysInfo::machineHostName()}, {"kernel", QSysInfo::kernelVersion()},
                      {"os", QSysInfo::prettyProductName()}, {"model", (vendor + " " + product).simplified()},
                      {"cpu", cpu}, {"gpu", gpus.join(", ")}, {"memoryGb", memoryGb},
                      {"diskTotal", double(root.bytesTotal())}, {"diskFree", double(root.bytesAvailable())},
                      {"uptimeSeconds", uptime}});
    }
    if (name == "battery") {
        const QStringList batteries = QDir("/sys/class/power_supply").entryList({"BAT*"}, QDir::AllEntries);
        if (batteries.isEmpty())
            return QVariantMap();
        const QString dir = "/sys/class/power_supply/" + batteries.first();
        return entry({{"percent", readFile(dir + "/capacity").trimmed().toInt()},
                      {"status", readFile(dir + "/status").trimmed()}});
    }
    if (name == "updates") {
        const ProcResult r = run("omarchy-update-available", {}, 60000);
        return entry({{"checked", true}, {"available", r.ok()}, {"text", r.text()}});
    }
    // What NetworkManager last saw, at once: a list that may start a scan
    // first waits for it, 4 to 8 seconds with nothing to show. wifiScan
    // looks again, and the Wi-Fi pane reloads this when it's done.
    if (name == "wifiScan")
        return run("nmcli", {"-t", "-f", "SSID", "device", "wifi", "list", "--rescan", "yes"}, 20000).ok();
    if (name == "wifi") {
        QVariantList networks = parseWifi(run("nmcli", {"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "no"}, 15000).out);
        QStringList known;
        for (const QString &line : output("nmcli", {"-t", "-f", "NAME,TYPE", "connection", "show"}).split('\n')) {
            const QStringList f = splitTerse(line);
            if (f.size() >= 2 && f[1] == "802-11-wireless")
                known << f[0];
        }
        for (QVariant &n : networks) {
            QVariantMap m = n.toMap();
            m["known"] = known.contains(m["ssid"].toString());
            n = m;
        }
        return networks;
    }
    if (name == "bluetooth") {
        QVariantList devices;
        static const QRegularExpression row(R"(^Device\s+([0-9A-F:]{17})\s+(.*)$)");
        int count = 0;
        for (const QString &line : output("bluetoothctl", {"devices"}).split('\n')) {
            const auto m = row.match(line.trimmed());
            if (!m.hasMatch() || ++count > 40)
                continue;
            const QString info = output("bluetoothctl", {"info", m.captured(1)}, 3000);
            auto field = [&info](const QString &key) {
                const QRegularExpression re("\\n\\s*" + key + ":\\s*([^\\n]*)");
                return re.match("\n" + info).captured(1).trimmed();
            };
            const QString battery = field("Battery Percentage");
            devices << entry({{"address", m.captured(1)}, {"name", m.captured(2).trimmed()},
                              {"paired", field("Paired") == "yes"}, {"connected", field("Connected") == "yes"},
                              {"icon", field("Icon")}, {"battery", parsePercent(battery.section('(', 1) + "%")}});
        }
        std::stable_sort(devices.begin(), devices.end(), [](const QVariant &a, const QVariant &b) {
            const QVariantMap x = a.toMap(), y = b.toMap();
            return int(x["connected"].toBool()) * 2 + int(x["paired"].toBool()) >
                   int(y["connected"].toBool()) * 2 + int(y["paired"].toBool());
        });
        return devices;
    }
    if (name == "sinks" || name == "sources") {
        const bool sinks = name == "sinks";
        const QString def = output("pactl", {sinks ? "get-default-sink" : "get-default-source"});
        return pactlDevices(run("pactl", {"-f", "json", "list", name}).out, def, !sinks);
    }
    if (name == "monitors") {
        QVariantList out;
        const QJsonObject state = managedState();
        const QJsonObject managed = state.value("monitors").toObject();
        const QJsonObject workspaces = state.value("workspaces").toObject();
        const QJsonArray live = QJsonDocument::fromJson(run("hyprctl", {"-j", "monitors", "all"}).out).array();
        const QHash<QString, QString> keys = monitorKeys(live);
        for (const QJsonValue &v : live) {
            const QJsonObject o = v.toObject();
            const QString connector = o.value("name").toString(), key = keys.value(connector, connector);
            QStringList modes;
            for (const QJsonValue &mode : o.value("availableModes").toArray()) {
                const QString m = mode.toString().remove("Hz");
                if (!modes.contains(m))
                    modes << m;
            }
            const QString current = QStringLiteral("%1x%2@%3").arg(o.value("width").toInt()).arg(o.value("height").toInt())
                                        .arg(o.value("refreshRate").toDouble(), 0, 'f', 2);
            // What Settings saved, under either name, so a choice made
            // before monitors were matched by description still shows.
            const QJsonObject saved = managed.value(key).toObject(managed.value(connector).toObject());
            QVariantList assigned;
            for (auto it = workspaces.begin(); it != workspaces.end(); ++it)
                if (it.value().toString() == key || it.value().toString() == connector)
                    assigned << it.key().toInt();
            // hyprctl gives the id of the display a mirror shows, or "none";
            // the page knows displays by connector name.
            QString mirrorOf = o.value("mirrorOf").toString();
            if (mirrorOf == "none")
                mirrorOf.clear();
            for (const QJsonValue &t : live)
                if (!mirrorOf.isEmpty() && QString::number(t.toObject().value("id").toInt(-1)) == mirrorOf)
                    mirrorOf = t.toObject().value("name").toString();
            out << entry({{"name", connector}, {"key", key}, {"id", o.value("id").toInt()},
                          {"description", o.value("description").toString()},
                          {"make", o.value("make").toString()}, {"model", o.value("model").toString()},
                          {"serial", o.value("serial").toString()},
                          {"width", o.value("width").toInt()}, {"height", o.value("height").toInt()},
                          {"refreshRate", o.value("refreshRate").toDouble()}, {"mode", current},
                          {"scale", o.value("scale").toDouble()}, {"x", o.value("x").toInt()}, {"y", o.value("y").toInt()},
                          {"transform", o.value("transform").toInt()}, {"disabled", o.value("disabled").toBool()},
                          {"focused", o.value("focused").toBool()}, {"modes", modes},
                          {"managed", managed.contains(key) || managed.contains(connector)},
                          {"saved", saved.toVariantMap()},
                          {"mirrorOf", mirrorOf},
                          {"vrr", saved.contains("vrr") ? saved.value("vrr").toInt() : int(o.value("vrr").toBool())},
                          {"tenBit", o.value("currentFormat").toString().contains("2101010")},
                          {"cm", o.value("colorManagementPreset").toString()},
                          {"sdrBrightness", o.value("sdrBrightness").toDouble(1)},
                          {"sdrSaturation", o.value("sdrSaturation").toDouble(1)},
                          {"primary", state.value("primary").toString() == key || state.value("primary").toString() == connector},
                          {"workspaces", assigned},
                          {"physicalWidth", o.value("physicalWidth").toInt()}, {"physicalHeight", o.value("physicalHeight").toInt()}});
        }
        return out;
    }
    if (name == "themes") {
        QVariantList out;
        const QString current = output("omarchy-theme-current", {});
        for (const QString &theme : output("omarchy-theme-list", {}, 10000).split('\n', Qt::SkipEmptyParts)) {
            const QString slug = theme.toLower().replace(' ', '-');
            QString preview;
            for (const QString &dir : {m_paths.config + "/omarchy/themes/" + slug, m_paths.omarchy + "/themes/" + slug})
                if (preview.isEmpty() && QFileInfo::exists(dir + "/preview.png"))
                    preview = QUrl::fromLocalFile(dir + "/preview.png").toString();
            // Themes the user added (or customized) live in their config;
            // only those can be removed.
            out << entry({{"name", theme}, {"slug", slug}, {"preview", preview}, {"current", theme == current},
                          {"custom", QFileInfo(m_paths.config + "/omarchy/themes/" + slug).isDir()},
                          {"stock", QFileInfo(m_paths.omarchy + "/themes/" + slug).isDir()}});
        }
        return out;
    }
    if (name == "backgrounds") {
        QVariantList out;
        const QString themeName = readFile(m_paths.state + "/omarchy/current/theme.name").trimmed();
        const QString current = QFileInfo(m_paths.state + "/omarchy/current/background").symLinkTarget();
        const QStringList images{"jpg", "jpeg", "png", "webp", "gif", "bmp"};
        const QStringList videos{"mp4", "m4v", "mov", "webm", "mkv", "avi"};
        QStringList seen;
        for (const QString &dir : {m_paths.config + "/omarchy/backgrounds/" + themeName,
                                   m_paths.state + "/omarchy/current/theme/backgrounds"}) {
            QStringList files = QDir(dir).entryList(QDir::Files, QDir::Name);
            for (const QString &file : files) {
                const QString path = QFileInfo(dir + "/" + file).canonicalFilePath();
                const QString suffix = QFileInfo(file).suffix().toLower();
                if (path.isEmpty() || seen.contains(path) || (!images.contains(suffix) && !videos.contains(suffix)))
                    continue;
                seen << path;
                out << entry({{"path", dir + "/" + file}, {"url", QUrl::fromLocalFile(path).toString()},
                              {"name", QFileInfo(file).completeBaseName()}, {"video", videos.contains(suffix)},
                              {"current", path == QFileInfo(current).canonicalFilePath()}});
            }
        }
        return out;
    }
    if (name == "fonts") {
        QVariantList out;
        for (const QString &f : output("omarchy-font-list", {}, 10000).split('\n', Qt::SkipEmptyParts))
            if (safeFontName(f))
                out << f;
        return out;
    }
    if (name == "languages") {
        QVariantList out;
        for (const QString &line : output("omarchy-language", {"list"}).split('\n', Qt::SkipEmptyParts)) {
            const QStringList f = line.split('\t');
            if (f.size() >= 3)
                out << entry({{"name", f[0]}, {"code", f[1]}, {"englishName", f[2]}});
        }
        return out;
    }
    if (name == "timezones") {
        QVariantList out;
        for (const QString &tz : output("timedatectl", {"list-timezones"}, 10000).split('\n', Qt::SkipEmptyParts))
            out << tz;
        return out;
    }
    if (name == "keyboardLayouts")
        return parseXkbLayouts(readFile("/usr/share/X11/xkb/rules/base.lst"));
    if (name == "configFiles")
        return listConfigFiles();
    if (name == "keybindings")
        return parseKeybindings(output("omarchy-menu-keybindings", {"--print"}, 10000));
    static const QStringList more{"apps", "fileTypes", "webapps", "dock", "wifiBand", "power", "crashMutes", "shortcuts",
                                  "loginItems", "windowRules", "firewall", "printers", "storage", "homeUsage",
                                  "snapshots", "managed", "menuSearch"};
    if (more.contains(name))
        return listMore(name);
    if (QStringList{"security", "system", "branding", "bootScreens", "hardware", "reminders", "fontCatalog"}.contains(name))
        return listSystem(name);
    if (name == "sync")
        return listSync();
    if (name == "syncPreview")
        return listSyncPreview();
    if (name == "menu")
        return listMenu();
    if (name == "menuCustom")
        return listMenuCustom();
    if (name == "software")
        return listSoftware();
    if (name == "defaultApps")
        return listDefaultApps();
    if (name == "appimages")
        return listAppImages();
    if (name == "appimageUpdates")
        return listAppImageUpdates();
    if (name.startsWith("defaults:"))
        return defaultsFor(name.section(':', 1));
    if (name == "vpn")
        return listVpn();
    if (name == "plugins")
        return listPlugins();
    if (name == "pluginUpdates")
        return listPluginUpdates();
    if (name == "bar")
        return listBar();
    if (name == "powerProfiles") {
        QVariantList out;
        static const QRegularExpression row(R"(^\*?\s*([a-z-]+):\s*$)");
        for (const QString &line : output("powerprofilesctl", {"list"}).split('\n')) {
            const auto m = row.match(line);
            if (m.hasMatch())
                out.prepend(m.captured(1));
        }
        return out;
    }
    return {};
}

// --- Actions ------------------------------------------------------------------------------

QString Store::launchInTerminal(const QString &id) {
    // Interactive setup runs in a floating terminal, the way Omarchy's own
    // menus do it, so prompts and passwords stay in a real terminal.
    static const QMap<QString, QStringList> commands{
        {"update", {"omarchy-update"}},
        {"fingerprint", {"omarchy-setup-security-fingerprint"}},
        {"fido2", {"omarchy-setup-security-fido2"}},
        {"sshAgent", {"omarchy-setup-security-ssh-agent"}},
        {"password", {"passwd"}},
        {"drivePassword", {"omarchy-drive-password"}},
        {"inputMethods", {"omarchy-setup-input"}},
        {"firmware", {"omarchy-update-firmware"}},
        // VPN support, installed the way Omarchy installs packages.
        {"installWireguard", {"omarchy-pkg-add", "wireguard-tools"}},
        {"installOpenvpn", {"omarchy-pkg-add", "networkmanager-openvpn"}},
        {"hibernationSetup", {"omarchy-hibernation-setup"}},
        {"hibernationRemove", {"omarchy-hibernation-remove"}},
        {"pkgPrune", {"omarchy-update-pkg-prune"}},
        {"installCups", {"omarchy-pkg-add", "cups", "cups-browsed", "cups-pdf"}},
        {"installUfw", {"omarchy-pkg-add", "ufw"}},
    };
    // The update channel takes the channel as an argument; dev is for
    // working on Omarchy itself and is left to the command line.
    if (id.startsWith("channel:")) {
        const QString channel = id.section(':', 1);
        if (!QStringList{"stable", "rc", "edge"}.contains(channel))
            return tr("Unknown channel %1").arg(channel);
        return launchTerminal({"omarchy-channel-set", channel});
    }
    if (!commands.contains(id)) {
        const QStringList command = terminalCommand(id);
        if (command.isEmpty())
            return tr("Unknown command %1").arg(id);
        return launchTerminal(command);
    }
    return launchTerminal(commands.value(id));
}

// The name Settings saves a display under: its description, which follows
// it to any port, unless another connected display has the same one (two
// identical monitors without serial numbers); then its connector.
QHash<QString, QString> Store::monitorKeys(const QJsonArray &live) {
    QHash<QString, int> seen;
    for (const QJsonValue &v : live)
        ++seen[v.toObject().value("description").toString().trimmed()];
    QHash<QString, QString> keys;
    for (const QJsonValue &v : live) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString(), description = o.value("description").toString().trimmed();
        const QString key = "desc:" + description;
        keys.insert(name, !description.isEmpty() && seen.value(description) == 1 && Hypr::validMonitorKey(key) ? key : name);
    }
    return keys;
}

QString Store::monitorAction(const QString &action, const QVariantList &args) {
    if (action == "monitors.identify") {
        // Wayland leaves where a window opens to the compositor, so each
        // Identify number is moved to its own display and made full screen.
        const QString name = args.value(0).toString();
        if (!Hypr::validMonitorName(name))
            return tr("Unknown display %1").arg(name);
        if (!m_paths.hyprland)
            return {};
        // Hyprland matches the whole title, which Qt ends with " — Settings".
        const QString window = Hypr::luaString("title:^omasettings-identify-" + QString(name).replace(".", "\\.") + "( .*)?$");
        run("hyprctl", {"dispatch", "hl.dsp.window.move({ monitor = " + Hypr::luaString(name) + ", window = " + window + " })"}, 3000);
        run("hyprctl", {"dispatch", "hl.dsp.window.fullscreen({ mode = \"fullscreen\", action = \"set\", window = " + window + " })"}, 3000);
        return {};
    }
    // One display by name: the form scripts and the CLI use.
    if (action == "monitor.apply" || action == "monitor.restore" || action == "monitor.reset") {
        const QString name = args.value(0).toString();
        if (!Hypr::validMonitorKey(name))
            return tr("Unknown display %1").arg(name);
        const QVariant config = action == "monitor.reset" ? QVariant() : args.value(1);
        return monitorAction(action == "monitor.apply" ? "monitors.apply" : "monitors.restore", {QVariantMap{{name, config}}});
    }
    if (action != "monitors.apply" && action != "monitors.restore" && action != "monitors.workspaces" && action != "monitors.primary")
        return tr("Unknown action %1").arg(action);

    QMutexLocker lock(&m_stateLock);
    QJsonObject state = managedState();
    QJsonObject monitors = state.value("monitors").toObject();
    QJsonObject workspaces = state.value("workspaces").toObject();
    const QJsonArray live = QJsonDocument::fromJson(run("hyprctl", {"-j", "monitors", "all"}).out).array();
    const QHash<QString, QString> keys = monitorKeys(live);

    // Every name a key may also be saved under (its connector, or its
    // description), so writing one forgets the others.
    auto aliases = [&](const QString &key) {
        QStringList out;
        for (auto it = keys.cbegin(); it != keys.cend(); ++it)
            if (it.key() == key || it.value() == key)
                out << it.key() << it.value();
        out.removeDuplicates();
        out.removeAll(key);
        return out;
    };
    auto rename = [&](const QString &from, const QString &to) {
        for (auto it = workspaces.begin(); it != workspaces.end(); ++it)
            if (it.value().toString() == from)
                it.value() = to;
        if (state.value("primary").toString() == from)
            state.insert("primary", to);
    };

    if (action == "monitors.apply" || action == "monitors.restore") {
        const QVariantMap changes = args.value(0).toMap();
        if (changes.isEmpty())
            return tr("Choose a display");
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            const QString key = it.key();
            if (!Hypr::validMonitorKey(key))
                return tr("Unknown display %1").arg(key);
            for (const QString &alias : aliases(key)) {
                monitors.remove(alias);
                rename(alias, key);
            }
            if (it.value().isNull() || !it.value().isValid() || it.value().toMap().isEmpty()) {
                monitors.remove(key);
                continue;
            }
            const QJsonObject m = QJsonObject::fromVariantMap(it.value().toMap());
            if (!m.value("disabled").toBool() &&
                (!Hypr::validMode(m.value("mode").toString("preferred")) || !Hypr::validPosition(m.value("position").toString("auto"))))
                return tr("That display mode is not valid");
            const QString mirror = m.value("mirror").toString();
            if (!mirror.isEmpty() && (!Hypr::validMonitorKey(mirror) || mirror == key))
                return tr("A display can't mirror itself");
            monitors.insert(key, m);
        }

        // What each connected display will be: never leave none showing a
        // picture of its own, and only mirror one that does.
        if (action == "monitors.apply") {
            auto saved = [&](const QString &connector) {
                const QString key = keys.value(connector, connector);
                return monitors.value(key).toObject(monitors.value(connector).toObject());
            };
            int showing = 0;
            for (const QJsonValue &v : live) {
                const QString connector = v.toObject().value("name").toString();
                const QJsonObject m = saved(connector);
                const bool off = m.isEmpty() ? v.toObject().value("disabled").toBool() : m.value("disabled").toBool();
                const QString mirror = m.value("mirror").toString();
                if (!off && mirror.isEmpty())
                    ++showing;
                if (!off && !mirror.isEmpty()) {
                    bool found = false;
                    for (const QJsonValue &t : live) {
                        const QString target = t.toObject().value("name").toString();
                        if (target != mirror && keys.value(target) != mirror)
                            continue;
                        const QJsonObject tm = saved(target);
                        found = !(tm.isEmpty() ? t.toObject().value("disabled").toBool() : tm.value("disabled").toBool())
                                && tm.value("mirror").toString().isEmpty();
                    }
                    if (!found)
                        return tr("A display can only mirror one that is on and shows its own picture");
                }
            }
            if (!live.isEmpty() && showing == 0)
                return tr("This is the only display, so it stays on");
        }
    } else if (action == "monitors.workspaces") {
        // args: {"6": "desc:…", "7": null}
        const QVariantMap changes = args.value(0).toMap();
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            bool ok = false;
            const int id = it.key().toInt(&ok);
            if (!ok || id < 1 || id > 10)
                return tr("Workspaces are numbered 1 to 10");
            const QString key = it.value().toString();
            if (key.isEmpty())
                workspaces.remove(QString::number(id));
            else if (!Hypr::validMonitorKey(key))
                return tr("Unknown display %1").arg(key);
            else
                workspaces.insert(QString::number(id), key);
        }
    } else if (action == "monitors.primary") {
        const QString key = args.value(0).toString();
        if (key.isEmpty())
            state.remove("primary");
        else if (!Hypr::validMonitorKey(key))
            return tr("Unknown display %1").arg(key);
        else
            state.insert("primary", key);
    }

    if (monitors.isEmpty())
        state.remove("monitors");
    else
        state.insert("monitors", monitors);
    if (workspaces.isEmpty())
        state.remove("workspaces");
    else
        state.insert("workspaces", workspaces);
    if (QString error = applyState(state); !error.isEmpty())
        return error;

    // Rules only place workspaces as they are made, so move the ones that
    // are open now, then go back to the workspace that was showing.
    if (action == "monitors.workspaces" && m_paths.hyprland) {
        const QString active = QString::number(QJsonDocument::fromJson(run("hyprctl", {"-j", "activeworkspace"}).out)
                                                   .object().value("id").toInt());
        bool moved = false;
        for (const QJsonValue &v : QJsonDocument::fromJson(run("hyprctl", {"-j", "workspaces"}).out).array()) {
            const QJsonObject w = v.toObject();
            const QString id = QString::number(w.value("id").toInt()), want = workspaces.value(id).toString();
            const QString on = w.value("monitor").toString();
            if (want.isEmpty() || want == on || keys.value(on) == want)
                continue;
            run("hyprctl", {"dispatch", "hl.dsp.workspace.move({ workspace = " + Hypr::luaString(id) +
                                            ", monitor = " + Hypr::luaString(want) + " })"}, 3000);
            moved = true;
        }
        if (moved && active != "0")
            run("hyprctl", {"dispatch", "hl.dsp.focus({ workspace = " + Hypr::luaString(active) + " })"}, 3000);
    }
    return {};
}

// nmcli shows a password to other local accounts whichever way it is given:
// as an argument, in the process list while the command runs; typed into
// its connection editor, in the history file the editor keeps. So secrets
// go to NetworkManager directly instead (see nmsecrets.h).
QString Store::setNmSecret(const QString &uuid, const QString &setting, const QString &key, const QString &value) {
    for (const QChar c : value)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return tr("A password can't contain tabs or line breaks");
    QString error = m_secrets(uuid, setting, key, value);
    // An error may quote what it was given; never show the password.
    if (!value.isEmpty())
        error.replace(value, QStringLiteral("…"));
    return error;
}

// Joins a Wi-Fi network with a password the way `nmcli device wifi connect`
// does (a saved profile named after the network, then activated), with the
// password set through setNmSecret.
QString Store::joinWifi(const QString &ssid, const QString &password, bool hidden) {
    for (const QChar c : password)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return tr("A password can't contain tabs or line breaks");
    // The scan says how the network is secured; a hidden one isn't in it.
    QString security;
    if (!hidden)
        for (const QString &line : output("nmcli", {"-t", "-f", "SSID,SECURITY", "device", "wifi", "list", "--rescan", "no"}, 15000).split('\n')) {
            const QStringList f = splitTerse(line);
            if (f.size() >= 2 && f[0] == ssid) {
                security = f[1];
                break;
            }
        }
    const bool wep = security.contains("WEP");
    const bool saeOnly = security.contains("WPA3") && !security.contains("WPA2") && !security.contains("WPA1");
    const QString keyMgmt = wep ? "none" : saeOnly ? "sae" : "wpa-psk";

    // A profile already saved for this network gets the new password;
    // otherwise one is made, and taken away again if joining fails.
    QString uuid;
    for (const QString &line : output("nmcli", {"-t", "-f", "NAME,UUID,TYPE", "connection", "show"}).split('\n')) {
        const QStringList f = splitTerse(line);
        if (f.size() >= 3 && f[0] == ssid && f[2] == "802-11-wireless") {
            uuid = f[1];
            break;
        }
    }
    const bool made = uuid.isEmpty();
    if (made) {
        QStringList add{"connection", "add", "type", "wifi", "con-name", ssid, "ssid", ssid,
                        "wifi-sec.key-mgmt", keyMgmt};
        if (hidden)
            add << "802-11-wireless.hidden" << "yes";
        const ProcResult r = run("nmcli", add, 15000);
        if (!r.ok())
            return r.failure("nmcli");
        static const QRegularExpression added(R"(\(([0-9a-fA-F-]{36})\))");
        uuid = added.match(r.text()).captured(1);
        if (uuid.isEmpty())
            return r.failure("nmcli");
    }
    QString error;
    if (wep) {
        // 5 or 13 characters (or 10 or 26 hex digits) is a key; anything
        // else is a passphrase.
        static const QRegularExpression hexKey("^([0-9a-fA-F]{10}|[0-9a-fA-F]{26})$");
        const bool key = password.size() == 5 || password.size() == 13 || hexKey.match(password).hasMatch();
        error = runOk("nmcli", {"connection", "modify", "uuid", uuid, "wifi-sec.wep-key-type", key ? "1" : "2"}, 15000);
        if (error.isEmpty())
            error = setNmSecret(uuid, "802-11-wireless-security", "wep-key0", password);
    } else {
        error = setNmSecret(uuid, "802-11-wireless-security", "psk", password);
    }
    if (error.isEmpty())
        error = runOk("nmcli", {"--wait", "30", "connection", "up", "uuid", uuid}, 40000);
    if (!error.isEmpty() && made)
        run("nmcli", {"connection", "delete", "uuid", uuid}, 15000);
    return error;
}

QString Store::act(const QString &action, const QVariantList &args) {
    const QString a0 = args.value(0).toString();
    static const QRegularExpression mac("^[0-9A-F]{2}(:[0-9A-F]{2}){5}$");

    if (action == "wifi.connect") {
        if (a0.isEmpty())
            return tr("Choose a network");
        const QString password = args.value(1).toString();
        const bool known = output("nmcli", {"-t", "-f", "NAME", "connection", "show"}).split('\n').contains(a0);
        if (known && password.isEmpty())
            return runOk("nmcli", {"--wait", "30", "connection", "up", "id", a0}, 40000);
        if (password.isEmpty())
            return runOk("nmcli", {"--wait", "30", "device", "wifi", "connect", a0}, 40000);
        return joinWifi(a0, password, false);
    }
    if (action == "wifi.disconnect") {
        for (const QString &line : output("nmcli", {"-t", "-f", "DEVICE,TYPE,STATE", "device"}).split('\n')) {
            const QStringList f = splitTerse(line);
            if (f.size() >= 3 && f[1] == "wifi" && f[2] == "connected")
                return runOk("nmcli", {"device", "disconnect", f[0]}, 15000);
        }
        return {};
    }
    if (action == "wifi.forget")
        return runOk("nmcli", {"connection", "delete", "id", a0}, 15000);
    if (action == "wifi.connectHidden") {
        // A network that doesn't broadcast its name never shows in a scan,
        // so it is joined by name.
        const QString password = args.value(1).toString();
        if (a0.isEmpty() || a0.toUtf8().size() > 32)
            return tr("A network name is 1 to 32 characters");
        if (password.isEmpty())
            return runOk("nmcli", {"--wait", "30", "device", "wifi", "connect", a0, "hidden", "yes"}, 45000);
        return joinWifi(a0, password, true);
    }
    if (action.startsWith("vpn."))
        return actVpn(action, args);
    if (action.startsWith("plugin.") && action != "plugin.clone")
        return actPlugin(action, args);
    if (action.startsWith("bar."))
        return actBar(action, args);
    if (action == "wifi.rescan") {
        run("nmcli", {"device", "wifi", "rescan"}, 15000);
        return {};
    }

    if (action == "bluetooth.scan") {
        // Discovery has to run for a while to find anything; the device
        // list is reloaded when it ends.
        run("bluetoothctl", {"--timeout", "8", "scan", "on"}, 12000);
        return {};
    }
    if (action.startsWith("bluetooth.")) {
        const QString verb = action.section('.', 1);
        if (!QStringList{"pair", "connect", "disconnect", "forget"}.contains(verb))
            return tr("Unknown action %1").arg(action);
        if (!mac.match(a0).hasMatch())
            return tr("Unknown device %1").arg(a0);
        return runOk("omarchy-bluetooth-device", {verb, a0}, 40000);
    }

    if (action == "sound.deviceVolume" || action == "sound.deviceMute") {
        // args: kind (sink|source), device name, value
        const QString kind = a0 == "source" ? "source" : "sink";
        const QString device = args.value(1).toString();
        if (action == "sound.deviceVolume")
            return runOk("pactl", {"set-" + kind + "-volume", device, QString::number(qBound(0, args.value(2).toInt(), 150)) + "%"});
        return runOk("pactl", {"set-" + kind + "-mute", device, args.value(2).toBool() ? "1" : "0"});
    }

    if (action.startsWith("monitor.") || action.startsWith("monitors."))
        return monitorAction(action, args);

    if (action == "background.set") {
        for (const QVariant &b : list("backgrounds").toList())
            if (b.toMap().value("path").toString() == a0)
                return runOk("omarchy-theme-bg-set", {a0}, 15000);
        return tr("That background is not in the current theme");
    }
    if (action == "background.next")
        return runOk("omarchy-theme-bg-next", {}, 15000);

    if (action == "launch")
        return launchInTerminal(a0);

    if (action.startsWith("appimage."))
        return actAppImage(action, args);
    if (action.startsWith("sync."))
        return actSync(action, args);

    if (action == "software.run" || action == "software.drop") {
        QStringList command;
        if (const QString error = softwareCommand(action, a0, command); !error.isEmpty())
            return error;
        return launch(command.first(), command.mid(1));
    }

    if (action == "edit") {
        // Only the files in the table, never a path from the caller.
        const QString path = configFilePath(a0);
        if (path.isEmpty())
            return tr("Unknown file %1").arg(a0);
        // A fixed command line with the path passed as an argument. Compose
        // sequences take effect once the compose daemon reads them again.
        if (a0 == "xcompose")
            return launch("bash", {"-c", "omarchy-launch-config-editor \"$1\" && omarchy-restart-xcompose", "bash", path});
        return launch("omarchy-launch-config-editor", {path});
    }

    if (action == "hypr.reset")
        return resetHypr(a0);
    if (action == "system.lock")
        return runOk("omarchy-system-lock", {}, 5000);
    if (action == "notifications.test")
        return runOk("omarchy-notification-send", {"-g", "󰂚", "Settings", "This is how notifications look."}, 5000);
    if (action == "about.open")
        return launch("omarchy-launch-about", {});

    return actMore(action, args);
}
