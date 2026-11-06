#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>
#include <QVariant>
#include <functional>

#include "proc.h"

// Where Settings reads and writes. Tests point these at temp directories.
struct Paths {
    QString config;  // ~/.config
    QString state;   // ~/.local/state
    QString omarchy; // $OMARCHY_PATH, Omarchy's read-only install
    QString data;    // ~/.local/share
    bool hyprland = true; // whether a Hyprland session is there to reload

    static Paths system();
};

// One setting: what it is, and how to read and change it on this system.
struct Setting {
    QString id;       // "keyboard.repeatRate"
    QString pane;     // "keyboard"
    QString type;     // bool, int, real, choice, text
    QString label;    // English, for the CLI and search
    double min = 0, max = 0, step = 1;
    QVariantList choices; // fixed choices: values
    QString choicesList;  // or the name of the list that holds them
    QString hyprOption;   // for settings kept in the generated Hyprland file
    std::function<QVariant()> read;
    // Returns an empty string on success, or what went wrong.
    std::function<QString(const QVariant &)> write;
};

// Everything Settings knows how to read and change. Its methods block, so the
// window calls them from worker threads (see Backend) and the CLI directly.
class Store {
public:
    explicit Store(Paths paths = Paths::system(), Exec exec = runProcess);

    const QList<Setting> &settings() const { return m_settings; }
    const Setting *setting(const QString &id) const;

    QVariant read(const QString &id) const;
    QString write(const QString &id, const QVariant &value);

    // Live data the panes show: networks, devices, monitors, themes, ...
    QVariant list(const QString &name);
    QStringList listNames() const;
    // Things a pane does that are not one setting's value.
    QString act(const QString &action, const QVariantList &args);

    // The saved choices behind the generated Hyprland file.
    QJsonObject managedState() const;
    QString managedLuaPath() const { return m_paths.config + "/hypr/omasettings.lua"; }
    QString statePath() const { return m_paths.config + "/omarchy/omasettings.json"; }

    // Hands a Hyprland option back to the user's own config.
    QString resetHypr(const QString &option);

    // Exposed for tests.
    static QString reindentJson(const QByteArray &json);
    static QVariantList parseWifi(const QByteArray &nmcli);
    static QStringList splitTerse(const QString &line);
    static QVariantList parseKeybindings(const QString &text);
    static QVariantList parseXkbLayouts(const QString &baseLst);
    static int parsePercent(const QString &text);

private:
    ProcResult run(const QString &program, const QStringList &args, int timeoutMs = 5000) const;
    QString runOk(const QString &program, const QStringList &args, int timeoutMs = 5000) const;
    QString launch(const QString &program, const QStringList &args) const;
    QString output(const QString &program, const QStringList &args, int timeoutMs = 5000) const;

    QVariant hyprGet(const QString &option) const;
    QString hyprSet(const QVariantMap &options);
    QString applyState(const QJsonObject &next);
    QString ensureRequired();

    QJsonValue shellGet(const QString &path, const QJsonValue &fallback) const;
    QString shellSet(const QString &path, const QJsonValue &value);

    bool flag(const QString &relative) const;

    // extras.cpp: VPN connections, shell plugins, the bar's layout.
    QVariant listVpn();
    QVariant listPlugins();
    QVariant listPluginUpdates();
    QVariant listBar();
    QString actVpn(const QString &action, const QVariantList &args);
    QString actPlugin(const QString &action, const QVariantList &args);
    QString actBar(const QString &action, const QVariantList &args);
    QString pluginDir(const QString &id) const;
    QHash<QString, QJsonObject> manifests() const;
    void waitForShellWrite(const QDateTime &before);

    // features.cpp: apps, shortcuts, login items, rules, Night Shift, network
    // and power details, firewall, printers, storage, export and import.
    void buildMore();
    QVariant listMore(const QString &name);
    QString actMore(const QString &action, const QVariantList &args);
    QVariantList apps() const;
    QString keyboardBacklight() const;
    QString setNightShift(const QString &key, const QVariant &value);
    QString writeNightShift(const QJsonObject &night);
    QString setMenuEntry(bool on);
    QString updateState(const std::function<QString(QJsonObject &)> &change);
    QJsonObject exportSettings();

    // system.cpp: branding, default tools, security setup, troubleshooting.
    void buildSystem();
    QVariant listSystem(const QString &name);
    QString actSystem(const QString &action, const QVariantList &args);
    QString directBootProblem();
    QString menuFile() const;
    QVariant listMenu();
    QString setMenuHidden(const QString &id, bool hidden);

public:
    // The app icon, drawn like Omarchy's own menu entries in the theme's colors.
    static QString iconSvg(const QString &foreground, double rounding);
    QString iconPath() const;
    QString writeThemedIcon();
    QString ensureIconHook();

private:
    QString importSettings(const QJsonObject &doc);

public:
    static QString normalizeKeys(const QString &keys);
    // The command a terminal launch id stands for, or {} if it isn't one.
    QStringList terminalCommand(const QString &id);

private:
    QString monitorAction(const QString &action, const QVariantList &args);
    QString launchInTerminal(const QString &id);

    void addHypr(const QString &id, const QString &pane, const QString &type, const QString &option,
                 const QString &label, double min = 0, double max = 0, double step = 1);
    void add(Setting setting);
    void build();

    Paths m_paths;
    Exec m_exec;
    std::function<bool(const QString &, const QStringList &)> m_launch;
    QList<Setting> m_settings;
    mutable QMutex m_stateLock;
};
