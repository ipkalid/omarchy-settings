#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "hyprconf.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

// A pretend system: every command the store runs is recorded, and answered
// from a table keyed by "program arg arg...".
struct FakeSystem {
    QStringList calls;
    QHash<QString, ProcResult> answers;
    QString configErrorsAfterReload;
    bool reloaded = false;

    Exec exec() {
        return [this](const QString &program, const QStringList &args, int) {
            const QString call = (QStringList{QFileInfo(program).fileName()} + args).join(' ');
            calls << call;
            if (call == "hyprctl reload")
                reloaded = true;
            if (call == "hyprctl configerrors") {
                ProcResult r{true, 0, reloaded ? configErrorsAfterReload.toUtf8() : QByteArray(), {}};
                return r;
            }
            for (auto it = answers.begin(); it != answers.end(); ++it)
                if (call == it.key())
                    return it.value();
            return ProcResult{true, 0, {}, {}};
        };
    }
    void answer(const QString &call, const QByteArray &out, int code = 0) { answers.insert(call, ProcResult{true, code, out, {}}); }
};

static QString readText(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void writeText(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

class OmasettingsTests : public QObject {
    Q_OBJECT

    Paths pathsIn(const QTemporaryDir &dir) {
        Paths p;
        p.config = dir.path() + "/config";
        p.state = dir.path() + "/state";
        p.omarchy = dir.path() + "/omarchy";
        p.hyprland = true;
        return p;
    }

private slots:
    // ---- The generated Hyprland file ------------------------------------------
    void luaNestsOptionsAndSpellsKeysTheLuaWay() {
        QJsonObject state{{"hypr", QJsonObject{{"input:repeat_rate", 45}, {"input:touchpad:tap-to-click", false},
                                               {"general:gaps_in", 4}, {"input:sensitivity", 0.25}}}};
        const QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains("hl.config({"));
        QVERIFY(lua.contains("    repeat_rate = 45,"));
        QVERIFY(lua.contains("tap_to_click = false,"));
        QVERIFY(!lua.contains("tap-to-click"));
        QVERIFY(lua.contains("sensitivity = 0.25,"));
        QVERIFY(lua.indexOf("general = {") < lua.indexOf("input = {"));
    }

    void luaEscapesStringsAndDropsUnsafeNames() {
        QJsonObject state{
            {"hypr", QJsonObject{{"input:kb_layout", "us\"); os.execute(\"rm -rf ~"}, {"bad name", 1}}},
            {"monitors", QJsonObject{{"DP-1\"); os.execute(\"x", QJsonObject{{"mode", "preferred"}}},
                                     {"HDMI-A-1", QJsonObject{{"mode", "1920x1080@60"}, {"position", "0x0"}, {"scale", 1.5}}},
                                     {"DP-3", QJsonObject{{"mode", "evil"}}}}}};
        const QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(kb_layout = "us\"); os.execute(\"rm -rf ~",)"));
        QVERIFY(!lua.contains("bad name"));
        QVERIFY(!lua.contains("os.execute(\"x"));
        QVERIFY(!lua.contains("DP-3"));
        QVERIFY(lua.contains(R"(hl.monitor({ output = "HDMI-A-1", mode = "1920x1080@60", position = "0x0", scale = 1.5 }))"));
    }

    void luaWritesDisabledMonitorsAndGestures() {
        QJsonObject state{{"monitors", QJsonObject{{"eDP-1", QJsonObject{{"disabled", true}}}}},
                          {"gestures", QJsonObject{{"workspaceSwipe", true}}}};
        const QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(hl.monitor({ output = "eDP-1", disabled = true }))"));
        QVERIFY(lua.contains("hl.gesture({ fingers = 3"));
        QVERIFY(!lua.contains("hl.config"));
    }

    void parsesHyprctlOptions() {
        QCOMPARE(Hypr::parseOption(R"({"option":"a","int":40,"set":true})"), QVariant(40));
        QCOMPARE(Hypr::parseOption(R"({"option":"a","float":0.4,"set":true})").toDouble(), 0.4);
        QCOMPARE(Hypr::parseOption(R"({"option":"a","bool":true})"), QVariant(true));
        QCOMPARE(Hypr::parseOption(R"({"option":"a","css":"2222"})"), QVariant(2));
        QCOMPARE(Hypr::parseOption(R"({"option":"a","css":"10101010"})"), QVariant(10));
        QCOMPARE(Hypr::parseOption(R"({"option":"a","str":"[[EMPTY]]"})").toString(), QString());
        QCOMPARE(Hypr::parseOption(R"({"option":"a","str":"us,ara"})").toString(), QString("us,ara"));
    }

    void keyboardOptionsKeepWhatTheyDoNotOwn() {
        QCOMPARE(Hypr::replaceKbOption("compose:ralt,grp:alts_toggle", {"grp:"}, "grp:alt_shift_toggle"),
                 QString("compose:ralt,grp:alt_shift_toggle"));
        QCOMPARE(Hypr::replaceKbOption("compose:ralt,ctrl:nocaps", {"caps:", "ctrl:nocaps"}, ""), QString("compose:ralt"));
        QCOMPARE(Hypr::replaceKbOption("", {"compose:"}, "compose:caps"), QString("compose:caps"));
        QCOMPARE(Hypr::findKbOption("grp_led:scroll,grp:caps_toggle", {"grp:"}), QString("grp:caps_toggle"));
    }

    // ---- Writing through the store ----------------------------------------------
    void hyprSettingWritesTheManagedFileAndReloads() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j getoption input:repeat_rate", R"({"option":"input:repeat_rate","int":45})");
        writeText(dir.path() + "/config/hypr/hyprland.lua", "require(\"default.hypr.omarchy\")\n");
        Store store(pathsIn(dir), sys.exec());

        QCOMPARE(store.write("keyboard.repeatRate", 45), QString());
        QVERIFY(readText(store.managedLuaPath()).contains("repeat_rate = 45,"));
        QVERIFY(sys.calls.contains("hyprctl reload"));
        QCOMPARE(store.managedState().value("hypr").toObject().value("input:repeat_rate").toInt(), 45);
        // hyprland.lua now loads the file, once, and was backed up first.
        const QString main = readText(dir.path() + "/config/hypr/hyprland.lua");
        QCOMPARE(main.count("require(\"hypr.omasettings\")"), 1);
        QCOMPARE(QDir(dir.path() + "/config/hypr").entryList({"hyprland.lua.bak.*"}).size(), 1);
        QCOMPARE(store.write("keyboard.repeatRate", 50), QString());
        QCOMPARE(readText(dir.path() + "/config/hypr/hyprland.lua").count("require(\"hypr.omasettings\")"), 1);
    }

    void hyprRejectionIsRolledBack() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("keyboard.repeatRate", 45), QString());
        const QString before = readText(store.managedLuaPath());

        sys.reloaded = false;
        sys.configErrorsAfterReload = store.managedLuaPath() + ":5: unknown config key 'input.repeat_rate'";
        const QString error = store.write("keyboard.repeatRate", 55);
        QVERIFY(error.contains("Hyprland rejected"));
        QCOMPARE(readText(store.managedLuaPath()), before);
        QCOMPARE(store.managedState().value("hypr").toObject().value("input:repeat_rate").toInt(), 45);
    }

    void resetHandsTheOptionBack() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("desktop.gapsInner", 8), QString());
        QCOMPARE(store.resetHypr(store.setting("desktop.gapsInner")->hyprOption), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("gaps_in"));
    }

    void managedStateKeepsKeysItDoesNotKnow() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        writeText(store.statePath(), R"({"monitorsSchema":3,"hypr":{}})");
        QCOMPARE(store.write("desktop.borderSize", 3), QString());
        QCOMPARE(store.managedState().value("monitorsSchema").toInt(), 3);
    }

    void writesAreValidated() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.write("keyboard.repeatRate", 500).contains("between"));
        QVERIFY(store.write("keyboard.repeatRate", "fast").contains("number"));
        QVERIFY(store.write("appearance.corners", "wobbly").contains("not one of"));
        QVERIFY(store.write("no.such", 1).contains("Unknown"));
        QVERIFY(store.write("keyboard.layouts", "us;rm").contains("xkb"));
        QVERIFY(store.write("general.hostname", "-bad name").contains("letters"));
        QVERIFY(!readText(store.managedLuaPath()).contains("rm"));
    }

    void layoutsKeepTheirVariants() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j getoption input:kb_layout", R"({"str":"us,de"})");
        sys.answer("hyprctl -j getoption input:kb_variant", R"({"str":"intl,nodeadkeys"})");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("keyboard.layouts", "de,fr"), QString());
        const QJsonObject hypr = store.managedState().value("hypr").toObject();
        QCOMPARE(hypr.value("input:kb_layout").toString(), QString("de,fr"));
        QCOMPARE(hypr.value("input:kb_variant").toString(), QString("nodeadkeys,"));
    }

    void boolsWriteThroughOmarchyCommands() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("desktop.barVisible", "false"), QString());
        QVERIFY(sys.calls.contains("omarchy-toggle-bar on"));
        QCOMPARE(store.write("lock.screensaver", true), QString());
        QVERIFY(sys.calls.contains("omarchy-toggle screensaver-off off"));
        QCOMPARE(store.write("appearance.animations", false), QString());
        QVERIFY(sys.calls.contains("omarchy-toggle-animations off"));
    }

    void commandFailuresAreReported() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answers.insert("powerprofilesctl set performance", ProcResult{true, 1, {}, "Unknown profile\n"});
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("power.profile", "performance"), QString("Unknown profile"));
    }

    void flagsReadFromOmarchyState() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.read("desktop.barVisible"), QVariant(true));
        writeText(dir.path() + "/state/omarchy/toggles/bar-off", "");
        QCOMPARE(store.read("desktop.barVisible"), QVariant(false));
        writeText(dir.path() + "/state/omarchy/toggles/hypr/no-animations.lua", "");
        QCOMPARE(store.read("appearance.animations"), QVariant(false));
    }

    // ---- The shell's JSON ------------------------------------------------------------
    void idleTimesEditShellJsonInPlace() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/config/omarchy/shell.json", R"({"idle":{"screensaver":150,"lock":300}})");
        writeText(dir.path() + "/config/omarchy/shell.json", "{\n  \"bar\": {\n    \"position\": \"top\"\n  },\n  \"version\": 1\n}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.read("lock.lockAfter").toInt(), 300); // from Omarchy's defaults
        QCOMPARE(store.write("lock.lockAfter", 900), QString());
        QCOMPARE(store.read("lock.lockAfter").toInt(), 900);
        const QString text = readText(dir.path() + "/config/omarchy/shell.json");
        QVERIFY(text.contains("\n  \"bar\": {\n    \"position\": \"top\""));
        QVERIFY(text.contains("\"lock\": 900"));
        QCOMPARE(store.read("lock.screensaverAfter").toInt(), 150);
    }

    void brokenShellJsonIsNeverOverwritten() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/config/omarchy/shell.json", "{ \"bar\": ");
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.write("lock.lockAfter", 600).contains("syntax error"));
        QCOMPARE(readText(dir.path() + "/config/omarchy/shell.json"), QString("{ \"bar\": "));
    }

    // ---- Parsers --------------------------------------------------------------------------
    void wifiListIsDedupedAndSorted() {
        const QVariantList n = Store::parseWifi("  :Cafe:40:WPA2\n*:Home\\:5G:70:WPA2\n  :Cafe:80:WPA2\n  ::90:--\n  :Open:60:--\n");
        QCOMPARE(n.size(), 3);
        QCOMPARE(n[0].toMap()["ssid"].toString(), QString("Home:5G"));
        QCOMPARE(n[1].toMap()["ssid"].toString(), QString("Cafe"));
        QCOMPARE(n[1].toMap()["signal"].toInt(), 80);
        QCOMPARE(n[2].toMap()["security"].toString(), QString());
    }

    void keybindingsAndLayoutsParse() {
        const QVariantList b = Store::parseKeybindings("SUPER + K          → Keybindings\nnoise\nSUPER SHIFT + F    → File manager\n");
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[1].toMap()["keys"].toString(), QString("SUPER SHIFT + F"));
        const QVariantList l = Store::parseXkbLayouts("! model\n  pc105 Generic\n\n! layout\n  us   English (US)\n  custom  A custom one\n  ara  Arabic\n\n! variant\n  intl us: X\n");
        QCOMPARE(l.size(), 2);
        QCOMPARE(l[0].toMap()["code"].toString(), QString("ara"));
        QCOMPARE(Store::parsePercent("Volume: front-left: 52428 /  80% / -5.81 dB"), 80);
    }

    void jsonIsReindentedToTwoSpaces() {
        QCOMPARE(Store::reindentJson("{\n    \"a\": {\n        \"b\": 1\n    }\n}\n"), QString("{\n  \"a\": {\n    \"b\": 1\n  }\n}\n"));
        QCOMPARE(Store::reindentJson("{\n    \"a\": [\n    ],\n    \"b\": {\n    }\n}"), QString("{\n  \"a\": [],\n  \"b\": {}\n}"));
    }

    void lastDisplayCannotBeTurnedOff() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j monitors", R"([{"name":"DP-1"}])");
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.act("monitor.apply", {"DP-1", QVariantMap{{"disabled", true}}}).contains("only display"));
        QVERIFY(!readText(store.managedLuaPath()).contains("disabled"));
        sys.answer("hyprctl -j monitors", R"([{"name":"DP-1"},{"name":"HDMI-A-1"}])");
        QCOMPARE(store.act("monitor.apply", {"DP-1", QVariantMap{{"disabled", true}}}), QString());
        QVERIFY(readText(store.managedLuaPath()).contains(R"(output = "DP-1", disabled = true)"));
        QCOMPARE(store.act("monitor.restore", {"DP-1", QVariant()}), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("DP-1"));
    }

    void actionsRejectUnsafeArguments() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.act("bluetooth.connect", {"; reboot"}).contains("Unknown device"));
        QVERIFY(store.act("monitor.apply", {"DP-1;x", QVariantMap{}}).contains("Unknown display"));
        QVERIFY(store.act("launch", {"rm"}).contains("Unknown command"));
        QVERIFY(store.act("background.set", {"/etc/passwd"}).contains("not in the current theme"));
    }

    // ---- Hidden Wi-Fi, VPN, plugins, the bar ----------------------------------------
    void hiddenNetworksAreJoinedByName() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("wifi.connectHidden", {"Secret Lab", "hunter22"}), QString());
        QVERIFY(sys.calls.contains("nmcli --wait 30 device wifi connect Secret Lab password hunter22 hidden yes"));
        QCOMPARE(store.act("wifi.connectHidden", {"Open Lab", ""}), QString());
        QVERIFY(sys.calls.contains("nmcli --wait 30 device wifi connect Open Lab hidden yes"));
        QVERIFY(store.act("wifi.connectHidden", {"", ""}).contains("1 to 32"));
        QVERIFY(store.act("wifi.connectHidden", {QString(33, 'x'), ""}).contains("1 to 32"));
    }

    void vpnConnectionsAreListedAndChecked() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString uuid = "0b1c2d3e-4f50-6172-8394-a5b6c7d8e9f0";
        sys.answer("nmcli -t -f NAME,UUID,TYPE,DEVICE,ACTIVE connection show",
                   ("Home:x:802-11-wireless:wlan0:yes\nwork-wg:" + uuid + ":wireguard:work-wg:yes\n"
                    "Office:11111111-2222-3333-4444-555555555555:vpn::no\n").toUtf8());
        sys.answer("nmcli -g vpn.service-type connection show uuid 11111111-2222-3333-4444-555555555555",
                   "org.freedesktop.NetworkManager.openvpn");
        Store store(pathsIn(dir), sys.exec());
        const QVariantList c = store.list("vpn").toMap().value("connections").toList();
        QCOMPARE(c.size(), 2);
        QCOMPARE(c[0].toMap().value("kind").toString(), QString("wireguard"));
        QVERIFY(c[0].toMap().value("active").toBool());
        QCOMPARE(c[1].toMap().value("kind").toString(), QString("openvpn"));

        QCOMPARE(store.act("vpn.connect", {uuid}), QString());
        QVERIFY(sys.calls.contains("nmcli --wait 30 connection up uuid " + uuid));
        QVERIFY(store.act("vpn.delete", {"; rm -rf ~"}).contains("Unknown VPN"));
        QCOMPARE(store.act("vpn.credentials", {uuid, "me", "pw"}), QString());
        QVERIFY(sys.calls.contains("nmcli connection modify uuid " + uuid + " +vpn.data username=me"));
        QVERIFY(sys.calls.contains("nmcli connection modify uuid " + uuid + " +vpn.data password-flags=0 vpn.secrets password=pw"));
        QVERIFY(store.act("vpn.credentials", {uuid, "a,b", ""}).contains("commas"));
    }

    void vpnImportsThePickedFile() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString conf = dir.path() + "/work.conf";
        writeText(conf, "[Interface]\n");
        sys.answers.insert("omarchy-file-select --title Choose a VPN configuration --extensions conf",
                           ProcResult{true, 0, conf.toUtf8() + "\n", {}});
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("vpn.import", {"wireguard"}), QString());
        QVERIFY(sys.calls.contains("nmcli connection import type wireguard file " + conf));
        // Closing the chooser without a file is not an error, and imports nothing.
        sys.calls.clear();
        sys.answers.insert("omarchy-file-select --title Choose a VPN configuration --extensions ovpn conf", ProcResult{true, 1, {}, {}});
        QCOMPARE(store.act("vpn.import", {"openvpn"}), QString());
        QVERIFY(!sys.calls.join("|").contains("connection import"));
        QVERIFY(store.act("vpn.import", {"pptp"}).contains("Unknown VPN type"));
    }

    void pluginActionsGoThroughOmarchy() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("plugin.disable", {"omarchy.weather"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-disable omarchy.weather"));
        QCOMPARE(store.act("plugin.update", {"someone.thing"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-update someone.thing --yes"));
        QVERIFY(store.act("plugin.remove", {"../../etc"}).contains("Unknown plugin"));
        QVERIFY(store.act("plugin.add", {"file:///etc"}).contains("git URL"));
        QCOMPARE(store.act("plugin.add", {"https://github.com/someone/plugin"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-add https://github.com/someone/plugin --enable --yes"));
    }

    void pluginsReadTheirManifests() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/config/omarchy/plugins/me.clock/manifest.json",
                  R"({"id":"me.clock","version":"2.1.0","description":"A clock"})");
        QDir().mkpath(dir.path() + "/config/omarchy/plugins/me.clock/.git");
        sys.answer("omarchy-plugin-list --json",
                   R"([{"id":"me.clock","name":"Clock","kinds":["bar-widget"],"enabled":true,"firstParty":false}])");
        Store store(pathsIn(dir), sys.exec());
        const QVariantMap p = store.list("plugins").toList().value(0).toMap();
        QCOMPARE(p.value("version").toString(), QString("2.1.0"));
        QCOMPARE(p.value("description").toString(), QString("A clock"));
        QVERIFY(p.value("git").toBool());
        QVERIFY(p.value("removable").toBool());
    }

    void barLayoutListsSectionsAndWhatIsLeft() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/config/omarchy/shell.json",
                  R"({"bar":{"layout":{"left":["omarchy.workspaces"],"right":[{"id":"omarchy.audio"},{"id":"omarchy.network"}]}}})");
        sys.answer("omarchy-plugin-list --json",
                   R"([{"id":"omarchy.audio","name":"Audio","kinds":["bar-widget"]},{"id":"omarchy.weather","name":"Weather","kinds":["bar-widget"]},{"id":"omarchy.osd","name":"OSD","kinds":["service"]}])");
        Store store(pathsIn(dir), sys.exec());
        const QVariantMap bar = store.list("bar").toMap();
        const QVariantList right = bar.value("sections").toMap().value("right").toList();
        QCOMPARE(right.size(), 2);
        QCOMPARE(right[0].toMap().value("name").toString(), QString("Audio"));
        QCOMPARE(right[1].toMap().value("index").toInt(), 1);
        QCOMPARE(bar.value("sections").toMap().value("left").toList().value(0).toMap().value("id").toString(), QString("omarchy.workspaces"));
        const QVariantList available = bar.value("available").toList();
        QCOMPARE(available.size(), 1);
        QCOMPARE(available[0].toMap().value("id").toString(), QString("omarchy.weather"));
    }

    void barChangesUseTheShellsCommands() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("bar.move", {"omarchy.audio", "right", 2, "left", 0}), QString());
        QVERIFY(sys.calls.contains("omarchy-bar move omarchy.audio --from-section right --from-index 2 --section left --index 0"));
        QCOMPARE(store.act("bar.add", {"omarchy.weather", "centerEnd"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-enable omarchy.weather --section centerEnd"));
        QCOMPARE(store.act("bar.remove", {"omarchy.weather"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-disable omarchy.weather"));
        QVERIFY(store.act("bar.move", {"omarchy.audio", "right", 0, "middle", 0}).contains("section"));
    }

    // ---- The window's backend -------------------------------------------------------------
    void backendShowsChangesAndUndoesThem() {
        QTemporaryDir dir;
        auto sys = std::make_shared<FakeSystem>();
        auto store = std::make_shared<Store>(pathsIn(dir), sys->exec());
        Backend backend(store);
        backend.refresh("desktop.borderSize");
        QTRY_VERIFY(backend.values().contains("desktop.borderSize"));
        const QVariant before = backend.values().value("desktop.borderSize");

        backend.set("desktop.borderSize", 4);
        QCOMPARE(backend.values().value("desktop.borderSize"), QVariant(4)); // shown at once
        QVERIFY(backend.canUndo());
        QTRY_VERIFY(backend.busy().isEmpty());
        QCOMPARE(store->managedState().value("hypr").toObject().value("general:border_size").toInt(), 4);

        backend.undo();
        QTRY_VERIFY(backend.busy().isEmpty());
        QVERIFY(backend.canRedo());
        // Nothing was known before, so undo hands the option back.
        QVERIFY(!before.isValid());
        QVERIFY(!store->managedState().value("hypr").toObject().contains("general:border_size"));
    }

    void backendReportsFailures() {
        QTemporaryDir dir;
        auto sys = std::make_shared<FakeSystem>();
        sys->answers.insert("powerprofilesctl set balanced", ProcResult{true, 1, {}, "No permission"});
        sys->answer("powerprofilesctl get", "performance");
        Backend backend(std::make_shared<Store>(pathsIn(dir), sys->exec()));
        backend.set("power.profile", "balanced");
        QTRY_COMPARE(backend.message(), QString("No permission"));
        QTRY_COMPARE(backend.values().value("power.profile").toString(), QString("performance"));
        QVERIFY(!backend.canUndo());
    }

    // ---- Theme and language -------------------------------------------------------------------
    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QVERIFY(theme.dark());
        QCOMPARE(theme.textSize(), 12);
    }

    void themeReadsPaletteModeAndTextSize() {
        QTemporaryDir dir;
        writeText(dir.path() + "/theme/colors.toml", "mode = \"light\"\naccent = \"#112233\"\nbackground = \"#eff1f5\"\nbogus = \"nope\"\n");
        writeText(dir.path() + "/user-shell.toml", "[bar]\nbase-size = 99\n[font]\nbase-size = 15 # px\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QVERIFY(!theme.dark());
        QCOMPARE(theme.colors().value("background").toString(), QString("#eff1f5"));
        QVERIFY(!theme.colors().contains("bogus"));
        QCOMPARE(theme.textSize(), 15);
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        writeText(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeText(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        writeText(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        writeText(dir.path() + "/language", "pt-BR\n");
        writeText(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        writeText(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        writeText(dir.path() + "/language", "ar\n");
        writeText(dir.path() + "/en.json", R"({"%n hours": {"one": "%n hour", "other": "%n hours"}})");
        writeText(dir.path() + "/ar.json", R"({"%n hours": {"zero": "z", "one": "o", "two": "t", "few": "f", "many": "m", "other": "x"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n hours", nullptr, 2), QString("t"));
        QCOMPARE(translator.translate("Main", "%n hours", nullptr, 11), QString("m"));
    }

    void shippedCatalogsAreValidJson() {
        for (const QString &file : QDir(QStringLiteral(SRCDIR) + "/../src/i18n").entryList({"*.json"})) {
            QJsonParseError error;
            QJsonDocument::fromJson(readText(QStringLiteral(SRCDIR) + "/../src/i18n/" + file).toUtf8(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(file + ": " + error.errorString()));
        }
    }
};

QTEST_MAIN(OmasettingsTests)
#include "omasettings_tests.moc"
