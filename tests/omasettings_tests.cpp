#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

    // ---- Shortcuts, login items, window rules, Night Shift, and the rest -------
    void keysAreWrittenOneWay() {
        QCOMPARE(Store::normalizeKeys("shift + super + r"), QString("SUPER + SHIFT + R"));
        QCOMPARE(Store::normalizeKeys("ALT+CTRL+comma"), QString("SUPER + CTRL + ALT + COMMA").mid(8));
        QCOMPARE(Store::normalizeKeys("SUPER + code:20"), QString("SUPER + code:20"));
        QVERIFY(Hypr::validKeys("SUPER + code:20"));
        QVERIFY(!Hypr::validKeys("SUPER + R\"); os.execute(\"x"));
    }

    void shortcutsLoginItemsAndRulesRender() {
        QJsonObject state{
            {"bindings", QJsonArray{QJsonObject{{"keys", "SUPER + E"}, {"description", "Files"}, {"kind", "launch"}, {"value", "org.gnome.Nautilus.desktop"}},
                                    QJsonObject{{"keys", "SUPER + R"}, {"description", "Say \"hi\""}, {"kind", "command"}, {"value", "notify-send hi"}},
                                    QJsonObject{{"keys", "bad keys!"}, {"kind", "command"}, {"value", "x"}},
                                    QJsonObject{{"keys", "SUPER + Z"}, {"kind", "lua"}, {"value", "os.exit()"}}}},
            {"unbinds", QJsonArray{"SUPER + F", "rm -rf"}},
            {"autostart", QJsonArray{QJsonObject{{"command", "syncthing"}}, QJsonObject{{"command", "a\nb"}}}},
            {"windowRules", QJsonArray{QJsonObject{{"class", "^(mpv)$"}, {"float", true}, {"workspace", "3"}, {"width", 800}, {"height", 600}, {"opacity", 0.9}},
                                       QJsonObject{{"class", "x"}, {"workspace", "3; os.exit()"}}}},
            {"nightShift", QJsonObject{{"schedule", true}}}};
        const QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(hl.unbind("SUPER + F"))"));
        QVERIFY(!lua.contains("rm -rf"));
        QVERIFY(lua.contains(R"(o.rebind("SUPER + E", "Files", { launch = "org.gnome.Nautilus.desktop" }))"));
        QVERIFY(lua.contains("o.rebind(\"SUPER + R\", \"Say \\\"hi\\\"\", \"notify-send hi\")"));
        QVERIFY(!lua.contains("bad keys") && !lua.contains("os.exit"));
        QVERIFY(lua.contains(R"(o.launch_on_start("syncthing"))"));
        QVERIFY(lua.contains(R"(o.launch_on_start("hyprsunset"))"));
        QVERIFY(!lua.contains("a\\nb"));
        QVERIFY(lua.contains(R"(o.window({ class = "^(mpv)$" }, { float = true, workspace = "3", opacity = "0.90", size = { 800, 600 } }))"));
        QCOMPARE(lua.count("o.window("), 1);
    }

    void shortcutActionsKeepOneEntryPerKeys() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("shortcut.disable", {"super + e"}), QString());
        QCOMPARE(store.act("shortcut.add", {"SUPER + E", "Files", "launch", "nautilus.desktop"}), QString());
        QCOMPARE(store.act("shortcut.add", {"super+e", "Editor", "command", "zeditor"}), QString());
        const QJsonObject state = store.managedState();
        QCOMPARE(state.value("bindings").toArray().size(), 1);
        QCOMPARE(state.value("bindings").toArray()[0].toObject().value("description").toString(), QString("Editor"));
        // Adding a shortcut on keys that were turned off turns them back on.
        QCOMPARE(state.value("unbinds").toArray().size(), 0);
        QVERIFY(store.act("shortcut.add", {"E", "Typing breaker", "command", "x"}).contains("stops typing"));
        QVERIFY(store.act("shortcut.add", {"SHIFT + code:20", "Typing breaker", "command", "x"}).contains("stops typing"));
        QCOMPARE(store.act("shortcut.add", {"F9", "Function key", "command", "x"}), QString());
        QCOMPARE(store.act("shortcut.remove", {"F9"}), QString());
        QVERIFY(store.act("shortcut.add", {"SUPER + E", "", "lua", "x"}).contains("Choose what"));
        QCOMPARE(store.act("shortcut.remove", {"SUPER + E"}), QString());
        QCOMPARE(store.managedState().value("bindings").toArray().size(), 0);
    }

    void loginItemsAndRulesAreManaged() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("autostart.add", {"syncthing", "Syncthing"}), QString());
        QCOMPARE(store.act("autostart.add", {"syncthing", "Again"}), QString());
        QCOMPARE(store.managedState().value("autostart").toArray().size(), 1);
        QVERIFY(store.act("autostart.add", {"  ", ""}).contains("Enter a command"));
        QCOMPARE(store.act("rule.add", {QVariantMap{{"class", "^(mpv)$"}, {"float", true}}}), QString());
        QVERIFY(store.act("rule.add", {QVariantMap{{"float", true}}}).contains("Choose an app"));
        QVERIFY(store.act("rule.remove", {5}).contains("gone"));
        QCOMPARE(store.act("rule.remove", {0}), QString());
        QCOMPARE(store.act("autostart.remove", {"syncthing"}), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("syncthing"));
    }

    void nightShiftScheduleOwnsHyprsunsetConf() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/config/hypr/hyprsunset.conf", "# mine\nprofile {\n    time = 07:00\n    identity = true\n}\n");
        writeText(dir.path() + "/omarchy/config/hypr/hyprsunset.conf", "# omarchy default\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("display.nightStart", "21:30"), QString());
        // Changing a time while the schedule is off leaves the file alone.
        QVERIFY(readText(dir.path() + "/config/hypr/hyprsunset.conf").startsWith("# mine"));
        QCOMPARE(store.write("display.nightSchedule", true), QString());
        const QString conf = readText(dir.path() + "/config/hypr/hyprsunset.conf");
        QVERIFY(conf.contains("time = 21:30\n    temperature = 4000"));
        QVERIFY(conf.contains("time = 07:00\n    identity = true"));
        QCOMPARE(QDir(dir.path() + "/config/hypr").entryList({"hyprsunset.conf.bak.*"}).size(), 1);
        QVERIFY(sys.calls.contains("omarchy-restart-hyprsunset"));
        QVERIFY(readText(store.managedLuaPath()).contains(R"(o.launch_on_start("hyprsunset"))"));
        QCOMPARE(store.write("display.nightTemperature", 3000), QString());
        QVERIFY(readText(dir.path() + "/config/hypr/hyprsunset.conf").contains("temperature = 3000"));
        QCOMPARE(store.write("display.nightSchedule", false), QString());
        QCOMPARE(readText(dir.path() + "/config/hypr/hyprsunset.conf"), QString("# omarchy default\n"));
        QVERIFY(!readText(store.managedLuaPath()).contains("hyprsunset"));
    }

    void menuEntryKeepsTheRestOfTheFile() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{\n  // my entries\n  \"mine\": {\"label\":\"Mine\"}\n}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.read("integration.menuEntry"), QVariant(false));
        QCOMPARE(store.write("integration.menuEntry", true), QString());
        QCOMPARE(store.read("integration.menuEntry"), QVariant(true));
        QVERIFY(readText(menu).contains("// my entries"));
        QCOMPARE(store.write("integration.menuEntry", true), QString());
        QCOMPARE(readText(menu).count("setup.omasettings"), 1);
        QCOMPARE(store.write("integration.menuEntry", false), QString());
        QCOMPARE(readText(menu), QString("{\n  // my entries\n  \"mine\": {\"label\":\"Mine\"}\n}\n"));
    }

    void privilegedActionsAreChecked() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("firewall.allow", {"22", "tcp"}), QString());
        QVERIFY(sys.calls.contains("pkexec ufw allow 22/tcp"));
        QCOMPARE(store.act("firewall.delete", {"6000:6010", "udp", "allow"}), QString());
        QVERIFY(sys.calls.contains("pkexec ufw delete allow 6000:6010/udp"));
        QVERIFY(store.act("firewall.allow", {"22; reboot", "tcp"}).contains("port"));
        QVERIFY(store.act("firewall.delete", {"22", "tcp", "--force"}).contains("Unknown rule"));
        QVERIFY(store.act("printer.remove", {"-x everything"}).contains("Unknown printer"));
        QCOMPARE(store.act("printer.default", {"Office_HP"}), QString());
        QVERIFY(sys.calls.contains("lpoptions -d Office_HP"));
        QCOMPARE(store.act("mime.set", {"pdf", "org.gnome.Evince"}), QString());
        QVERIFY(sys.calls.contains("xdg-mime default org.gnome.Evince.desktop application/pdf"));
        QVERIFY(store.act("mime.set", {"pdf", "../evil"}).contains("Unknown app"));
        QVERIFY(store.act("theme.remove", {"tokyo-night"}).contains("Only themes you added"));
        QVERIFY(store.act("webapp.add", {"Mail", "javascript:alert(1)", ""}).contains("https://"));
        QVERIFY(store.act("launch", {"channel:dev"}).contains("Unknown channel"));
    }

    void dnsCustomOpensATerminalAndPresetsRunInline() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("network.dns", "Google"), QString());
        QVERIFY(sys.calls.contains("omarchy-dns Google"));
        QVERIFY(store.write("network.dns", "8.8.8.8").contains("not one of"));
    }

    void exportLeavesTheMachineBehindAndImportRoundTrips() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hostnamectl hostname", "my-laptop");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("shortcut.add", {"SUPER + E", "Files", "launch", "nautilus.desktop"}), QString());
        const QString file = dir.path() + "/export.json";
        QCOMPARE(store.act("settings.export", {file}), QString());
        const QJsonObject doc = QJsonDocument::fromJson(readText(file).toUtf8()).object();
        QCOMPARE(doc.value("app").toString(), QString("omasettings"));
        QVERIFY(!doc.value("settings").toObject().contains("general.hostname"));
        QCOMPARE(doc.value("managed").toObject().value("bindings").toArray().size(), 1);

        QTemporaryDir other;
        FakeSystem sys2;
        Store fresh(pathsIn(other), sys2.exec());
        QCOMPARE(fresh.act("settings.import", {file}), QString());
        QCOMPARE(fresh.managedState().value("bindings").toArray().size(), 1);
        writeText(other.path() + "/not-ours.json", "{\"app\":\"other\"}");
        QVERIFY(fresh.act("settings.import", {other.path() + "/not-ours.json"}).contains("not a Settings export"));
    }

    void managedChangesCanBeListedAndReset() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("desktop.borderSize", 4), QString());
        QCOMPARE(store.act("autostart.add", {"syncthing", "Syncthing"}), QString());
        const QVariantList changes = store.list("managed").toList();
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].toMap().value("label").toString(), QString("Window border"));
        QCOMPARE(store.act("managed.reset", {"hypr", "general:border_size"}), QString());
        QCOMPARE(store.list("managed").toList().size(), 1);
        QCOMPARE(store.act("managed.resetAll", {}), QString());
        QCOMPARE(store.list("managed").toList().size(), 0);
    }

    // ---- Branding, tools, security setup, troubleshooting ----------------------
    void terminalCommandsOnlyTakeKnownArguments() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("omarchy-kernel list", "linux\nlinux-omarchy\n");
        sys.answer("omarchy-plymouth-list", "nord\ntokyo-night\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.terminalCommand("kernel:linux"), (QStringList{"omarchy-kernel", "set", "linux"}));
        QVERIFY(store.terminalCommand("kernel:linux; reboot").isEmpty());
        QCOMPARE(store.terminalCommand("bootScreen:nord"), (QStringList{"omarchy-plymouth-set-by-theme", "nord"}));
        QVERIFY(store.terminalCommand("bootScreen:../../etc").isEmpty());
        QCOMPARE(store.terminalCommand("sshdRemove"), (QStringList{"omarchy-remove-security-sshd"}));
        QCOMPARE(store.terminalCommand("sudoPasswordlessOff"), (QStringList{"omarchy-sudo-passwordless", "--disable"}));
        QVERIFY(store.terminalCommand("rm").isEmpty());
        QVERIFY(store.act("launch", {"kernel:evil"}).contains("Unknown command"));
    }

    void gitIdentityGoesThroughOmarchy() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.write("security.gitName", "Ada Lovelace"), QString());
        QVERIFY(sys.calls.contains("omarchy-setup-git --name Ada Lovelace"));
        QCOMPARE(store.write("security.gitEmail", "ada@example.com"), QString());
        QVERIFY(sys.calls.contains("omarchy-setup-git --email ada@example.com"));
        QVERIFY(store.write("security.gitEmail", "not an email").contains("email"));
        QVERIFY(store.write("security.gitName", "  ").contains("Enter"));
    }

    void brandingHapticsAndAgentsAreChecked() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("branding.reset", {"screensaver"}), QString());
        QVERIFY(sys.calls.contains("omarchy-branding-screensaver reset"));
        QVERIFY(store.act("branding.reset", {"../etc"}).contains("Unknown branding"));
        QVERIFY(store.act("haptics.set", {"max"}).contains("Unknown strength"));
        QCOMPARE(store.write("general.agent", "codex"), QString());
        QVERIFY(sys.calls.contains("omarchy-default-agent codex"));
        QVERIFY(store.write("general.agent", "rm -rf").contains("not one of"));
    }

    void securityStatusIsReadBack() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("systemctl is-enabled sshd", "enabled");
        sys.answers.insert("omarchy-pkg-present fprintd", ProcResult{true, 1, {}, {}});
        sys.answers.insert("omarchy-sudo-docker --configured", ProcResult{true, 1, {}, {}});
        sys.answers.insert("omarchy-sudo-passwordless --active", ProcResult{true, 1, {}, {}});
        sys.answer("omarchy-boot-direct --number", "0003");
        Store store(pathsIn(dir), sys.exec());
        const QVariantMap sec = store.list("security").toMap();
        QVERIFY(sec.value("sshd").toBool());
        QVERIFY(!sec.value("fingerprint").toBool());
        QVERIFY(sec.value("fido2").toBool());
        QVERIFY(sec.value("dockerSudoless").toBool());
        QVERIFY(!sec.value("passwordlessSudo").toBool());
        QVERIFY(sec.value("directBoot").toBool());
    }

    // ---- The Omarchy menu --------------------------------------------------------
    void menuItemsHideAndShowWithoutLosingAnything() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n  \"learn\": {\"label\":\"Learn\"},\n  \"learn.bash\": {\"label\":\"Bash\",\"action\":\"x\"},\n"
                  "  \"about\": {\"label\":\"About\",\"action\":\"y\"}\n}\n");
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        const QByteArray original =
            "{\n  // mine\n  \"about\": {\"label\":\"Me\",\"when\":\"[[ -f ~/x ]]\"},\n"
            "  \"mine\": {\n    \"label\": \"Mine\",\n    \"action\": \"z\"\n  },\n  \"learn\":{\"when\":\"false\"}\n}\n";
        writeText(menu, original);
        Store store(pathsIn(dir), sys.exec());

        const QVariantList items = store.list("menu").toList();
        QCOMPARE(items.size(), 4);
        auto find = [&](const QString &id) {
            for (const QVariant &i : store.list("menu").toList())
                if (i.toMap().value("id") == id) return i.toMap();
            return QVariantMap();
        };
        QVERIFY(find("learn").value("hidden").toBool());
        QCOMPARE(find("about").value("label").toString(), QString("Me"));
        QVERIFY(!find("mine").value("editable").toBool());
        QVERIFY(find("mine").value("custom").toBool());
        QCOMPARE(find("learn.bash").value("depth").toInt(), 1);

        // Hiding a new item adds one line; showing it takes it away again.
        QCOMPARE(store.act("menu.hide", {"learn.bash"}), QString());
        QVERIFY(readText(menu).contains("\"learn.bash\": {\"when\": \"false\"},"));
        QCOMPARE(store.act("menu.show", {"learn.bash"}), QString());
        QCOMPARE(readText(menu).toUtf8(), original);

        // Hiding an item the user customized keeps their label, and their own
        // condition comes back when it is shown.
        QCOMPARE(store.act("menu.hide", {"about"}), QString());
        QVERIFY(find("about").value("hidden").toBool());
        QCOMPARE(find("about").value("label").toString(), QString("Me"));
        QCOMPARE(store.act("menu.show", {"about"}), QString());
        QVERIFY(readText(menu).contains("[[ -f ~/x ]]"));
        QVERIFY(!find("about").value("hidden").toBool());

        // An entry over several lines is the user's to edit.
        QVERIFY(store.act("menu.hide", {"mine"}).contains("several lines"));
        QVERIFY(store.act("menu.hide", {"bad id!"}).contains("Unknown menu item"));
        QVERIFY(readText(menu).contains("// mine"));
        QVERIFY(QFileInfo::exists(menu + ".bak.omasettings-" + QDateTime::currentDateTime().toString("yyyyMMdd")));
    }

    void hidingASectionHidesWhatIsInsideItToo() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n  \"learn\": {\"label\":\"Learn\"},\n  \"learn.bash\": {\"label\":\"Bash\"},\n"
                  "  \"learn.arch\": {\"label\":\"Arch\"},\n  \"learn.arch.wiki\": {\"label\":\"Wiki\"}\n}\n");
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        const QByteArray original = "{\n  \"learn.arch\": {\"when\": \"false\"}\n}\n";
        writeText(menu, original);
        Store store(pathsIn(dir), sys.exec());
        auto hidden = [&](const QString &id) {
            for (const QVariant &i : store.list("menu").toList())
                if (i.toMap().value("id") == id) return i.toMap().value("hidden").toBool();
            return false;
        };
        // Arch was hidden by hand, but its wiki item still shows in search.
        QVERIFY(!hidden("learn.arch.wiki"));
        QCOMPARE(store.act("menu.hideInside", {""}), QString());
        QVERIFY(hidden("learn.arch.wiki"));

        // Hiding Learn hides everything in it.
        QCOMPARE(store.act("menu.hide", {"learn"}), QString());
        QVERIFY(hidden("learn") && hidden("learn.bash") && hidden("learn.arch") && hidden("learn.arch.wiki"));
        // Showing it brings back what it hid, and leaves Arch, which was
        // hidden by hand, hidden.
        QCOMPARE(store.act("menu.show", {"learn"}), QString());
        QVERIFY(!hidden("learn") && !hidden("learn.bash"));
        QVERIFY(hidden("learn.arch") && hidden("learn.arch.wiki"));
        // And showing Arch brings back its wiki item; the file is as it was
        // before any of this, less the line for Arch itself.
        QCOMPARE(store.act("menu.show", {"learn.arch"}), QString());
        QVERIFY(!hidden("learn.arch.wiki"));
        QVERIFY(!readText(menu).contains("when"));
    }

    void theMenuCanGoBackToDefault() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n  \"learn\": {\"label\":\"Learn\"},\n  \"learn.bash\": {\"label\":\"Bash\"},\n  \"about\": {\"label\":\"About\"}\n}\n");
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{\n  \"mine\": {\n    \"label\": \"Mine\",\n    \"action\": \"z\"\n  },\n"
                        "  \"about\": {\"label\":\"Me\",\"when\":\"[[ -f ~/x ]]\"}\n}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("menu.hide", {"learn"}), QString());
        QCOMPARE(store.act("menu.hide", {"about"}), QString());
        QCOMPARE(store.act("menu.showAll", {""}), QString());
        for (const QVariant &i : store.list("menu").toList())
            QVERIFY2(!i.toMap().value("hidden").toBool(), qPrintable(i.toMap().value("id").toString()));
        const QString text = readText(menu);
        QVERIFY(text.contains("\"action\": \"z\""));      // their own entry stays
        QVERIFY(text.contains("[[ -f ~/x ]]"));              // and their condition comes back
        QVERIFY(!text.contains("\"false\""));
        QVERIFY(!QDir(dir.path() + "/config/omarchy/extensions").entryList({"omarchy-menu.jsonc.bak.omasettings-*-*"}).isEmpty());
    }

    void yourOwnMenuItemsCanBeAddedChangedAndRemoved() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n  \"learn\": {\"label\":\"Learn\"},\n  \"learn.bash\": {\"label\":\"Bash\"}\n}\n");
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{\n  // mine\n  \"tool\": {\n    \"label\": \"Tool\",\n    \"action\": \"run-it --fast\",\n"
                        "    \"when\": \"true\"\n  }\n}\n");
        Store store(pathsIn(dir), sys.exec());
        auto custom = [&](const QString &id) {
            for (const QVariant &i : store.list("menuCustom").toList())
                if (i.toMap().value("id") == id) return i.toMap();
            return QVariantMap();
        };
        QCOMPARE(store.list("menuCustom").toList().size(), 1);
        QCOMPARE(custom("tool").value("action").toString(), QString("run-it --fast"));

        // A new item goes under its section, with an id made from its name
        // that doesn't clash with one already there.
        QVariantMap add{{"parent", "learn"}, {"label", "Bash"}, {"action", "man bash"}, {"icon", "X"}};
        QCOMPARE(store.act("menu.addItem", {add}), QString());
        QCOMPARE(custom("learn.bash-2").value("label").toString(), QString("Bash"));
        QCOMPARE(custom("learn.bash-2").value("parent").toString(), QString("learn"));
        QVERIFY(store.act("menu.addItem", {QVariantMap{{"label", "  "}}}).contains("name"));
        QVERIFY(store.act("menu.addItem", {QVariantMap{{"label", "A"}, {"action", "x\ny"}}}).contains("one line"));

        // Editing an entry over several lines keeps what the window doesn't
        // show, and the user's comment.
        QCOMPARE(store.act("menu.editItem", {QVariantMap{{"id", "tool"}, {"label", "My Tool"}, {"action", "run-it --fast"}}}), QString());
        QCOMPARE(custom("tool").value("label").toString(), QString("My Tool"));
        QVERIFY(readText(menu).contains("\"when\":\"true\""));
        QVERIFY(readText(menu).contains("// mine"));

        // Removing a submenu takes what is inside it too.
        QCOMPARE(store.act("menu.addItem", {QVariantMap{{"label", "Work"}}}), QString());
        QCOMPARE(store.act("menu.addItem", {QVariantMap{{"parent", "work"}, {"label", "Notes"}, {"action", "n"}}}), QString());
        QVERIFY(!custom("work.notes").isEmpty());
        QCOMPARE(store.act("menu.removeItem", {QVariantMap{{"id", "work"}}}), QString());
        QVERIFY(custom("work").isEmpty() && custom("work.notes").isEmpty());
        QVERIFY(!custom("tool").isEmpty());
        QVERIFY(!store.act("menu.removeItem", {QVariantMap{{"id", "nope"}}}).isEmpty());
    }

    void settingsCanSitAtTheTopOfTheMenu() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{\n  // mine\n  \"setup\": {\"when\": \"false\"},\n  \"setup.omasettings\": {\"label\":\"Settings app\",\"when\":\"false\"}\n}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.read("integration.menuPlace"), QVariant("setup"));
        QCOMPARE(store.write("integration.menuPlace", "top"), QString());
        QCOMPARE(store.read("integration.menuPlace"), QVariant("top"));
        QCOMPARE(store.read("integration.menuEntry"), QVariant(true));
        QVERIFY(!readText(menu).contains("setup.omasettings"));
        QVERIFY(readText(menu).contains("\"setup\": {\"when\": \"false\"}"));
        QVERIFY(readText(menu).contains("// mine"));
        // Turning the old switch on leaves it where it is.
        QCOMPARE(store.write("integration.menuEntry", true), QString());
        QCOMPARE(store.read("integration.menuPlace"), QVariant("top"));
        QCOMPARE(store.write("integration.menuPlace", "off"), QString());
        QVERIFY(!readText(menu).contains("omasettings"));
        QVERIFY(!store.write("integration.menuPlace", "sideways").isEmpty());
    }

    void anItemCanBeAddedToAnEmptyMenuFile() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("menu.addItem", {QVariantMap{{"label", "Hi There"}, {"action", "echo hi"}}}), QString());
        QVERIFY(readText(menu).contains("\"hi-there\": {"));
    }

    void aBrokenMenuFileIsLeftAlone() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "[1, 2]\n");
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(!store.act("menu.hide", {"learn"}).isEmpty());
        QCOMPARE(readText(menu), QString("[1, 2]\n"));
    }

    // ---- The last menu items: clock, laptop screen, fonts, reminders, ... --------------
    void menuLeftoversBehaveLikeTheMenu() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        // Toggles that only flip run only when the state differs.
        QCOMPARE(store.write("display.laptopScreen", true), QString());
        QVERIFY(!sys.calls.join("|").contains("omarchy-hyprland-monitor-internal toggle"));
        QCOMPARE(store.write("display.laptopScreen", false), QString());
        QVERIFY(sys.calls.contains("omarchy-hyprland-monitor-internal toggle"));
        QCOMPARE(store.write("display.mirror", true), QString());
        QVERIFY(sys.calls.contains("omarchy-hyprland-monitor-internal-mirror toggle"));
        sys.answer("omarchy-bar get omarchy.power showPercentage", "true");
        sys.calls.clear();
        QCOMPARE(store.write("bar.batteryPercentage", true), QString());
        QVERIFY(!sys.calls.join("|").contains("togglePercentage"));
        QCOMPARE(store.write("desktop.oneWindowRatio", true), QString());
        QVERIFY(sys.calls.contains("omarchy-hyprland-toggle single-window-aspect-ratio on"));
        // Every clock style the menu offers.
        QCOMPARE(store.write("general.clockFormat", "yyyy-MM-dd HH:mm"), QString());
        QCOMPARE(store.write("general.clockVerticalFormat", "HH\nmm"), QString());
        QVERIFY(sys.calls.contains("omarchy-bar set omarchy.clock verticalFormat HH\nmm"));
        // Fonts and terminals only from the menu's own list.
        QCOMPARE(store.terminalCommand("font:iosevka"),
                 (QStringList{"omarchy-install-font", "Iosevka", "ttf-iosevka-nerd", "Iosevka Nerd Font Mono"}));
        QVERIFY(store.terminalCommand("font:../evil").isEmpty());
        QCOMPARE(store.terminalCommand("hybridGpu"), (QStringList{"omarchy-toggle-hybrid-gpu"}));
        // Reminders, panels, and plugin copies are checked.
        QCOMPARE(store.act("reminder.set", {30, "Tea"}), QString());
        QVERIFY(sys.calls.contains("omarchy-reminder 30 Tea"));
        QVERIFY(store.act("reminder.set", {5000, ""}).contains("24 hours"));
        QVERIFY(store.act("summon", {"omarchy.anything"}).contains("Unknown panel"));
        QCOMPARE(store.act("summon", {"omarchy.wifiqr"}), QString());
        QVERIFY(sys.calls.contains("omarchy-shell shell summon omarchy.wifiqr"));
        QVERIFY(store.act("plugin.clone", {"someone.widget"}).contains("Omarchy's own"));
        QCOMPARE(store.act("plugin.clone", {"omarchy.clock"}), QString());
        QVERIFY(sys.calls.contains("omarchy-plugin-clone omarchy.clock"));
    }

    // ---- Restore Defaults ------------------------------------------------------------
    void panesGoBackToWhatOmarchyShips() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/config/omarchy/shell.json", R"({"idle":{"screensaver":150,"lock":300},"dock":{"pinned":["foot"]}})");
        writeText(dir.path() + "/config/omarchy/shell.json", R"({"idle":{"screensaver":600,"lock":900}})");
        writeText(dir.path() + "/omarchy/default/hypr/input.lua", "local non_latin_layouts =\n  \" ara ru \"\n");
        sys.answer("hyprctl -j getoption input:kb_layout", R"({"str":"us,ara"})");
        sys.answer("hyprctl -j getoption input:kb_options", R"({"str":"compose:ralt"})");
        sys.answer("omarchy-bar get omarchy.clock format", "h:mm AP");
        Store store(pathsIn(dir), sys.exec());

        // What would change is listed first, with the value it goes back to.
        const QVariantList lock = store.defaultsFor("lock");
        QCOMPARE(lock.size(), 2);
        QCOMPARE(lock[0].toMap().value("default").toDouble(), 150.0);
        QCOMPARE(store.resetPane("lock"), QString());
        QCOMPARE(store.read("lock.screensaverAfter").toInt(), 150);
        QCOMPARE(store.read("lock.lockAfter").toInt(), 300);

        // Omarchy's keyboard options: Compose on Shift+Caps, and both Alts
        // to switch since Arabic can't type Latin.
        QCOMPARE(store.resetPane("keyboard"), QString());
        QCOMPARE(store.managedState().value("hypr").toObject().value("input:kb_options").toString(),
                 QString("omarchy:shift_caps_compose,grp:alts_toggle"));

        // The bar goes back through Omarchy's own reset; the clock too.
        QCOMPARE(store.resetPane("bar"), QString());
        QVERIFY(sys.calls.contains("omarchy-bar defaults"));
        QCOMPARE(store.resetPane("general"), QString());
        QVERIFY(sys.calls.contains("omarchy-bar set omarchy.clock format dddd HH:mm"));
        // Personal settings are never part of a reset.
        QVERIFY(!sys.calls.join("|").contains("hostnamectl set-hostname"));
        QVERIFY(store.managedState().value("hypr").toObject().value("input:kb_layout").isUndefined());
    }

    // ---- The app icon ----------------------------------------------------------------
    void iconFollowsTheThemeAndHooksThemeChanges() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j getoption decoration:rounding", R"({"int":6})");
        writeText(dir.path() + "/state/omarchy/current/theme/colors.toml", "foreground = \"#112233\"\n");
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        Store store(paths, sys.exec());
        QCOMPARE(store.writeThemedIcon(), QString());
        const QString svg = readText(dir.path() + "/data/icons/hicolor/scalable/apps/omasettings.svg");
        QVERIFY(svg.contains(R"(fill="#112233" fill-opacity="0.07")"));
        QVERIFY(svg.contains(R"(fill="#112233" fill-opacity="0.8")"));
        QVERIFY(svg.contains(R"(rx="64.0")"));
        // The hook goes in through Omarchy's own installer, once.
        QCOMPARE(store.ensureIconHook(), QString());
        QVERIFY(sys.calls.join("|").contains("omarchy-hook-install theme-set "));
        writeText(dir.path() + "/config/omarchy/hooks/theme-set.d/omasettings-icon", "x");
        sys.calls.clear();
        QCOMPARE(store.ensureIconHook(), QString());
        QVERIFY(!sys.calls.join("|").contains("omarchy-hook-install"));
        // A foreground that isn't a plain hex color never reaches the file:
        // the default color is drawn instead, or nothing at all.
        writeText(dir.path() + "/state/omarchy/current/theme/colors.toml", "foreground = \"red\\\" onload\"\n");
        store.writeThemedIcon();
        QVERIFY(!readText(dir.path() + "/data/icons/hicolor/scalable/apps/omasettings.svg").contains("onload"));
        writeText(dir.path() + "/state/omarchy/current/theme/colors.toml", "foreground = \"red\"\n");
        QVERIFY(store.writeThemedIcon().contains("isn't a color"));
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
