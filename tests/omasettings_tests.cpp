#include <functional>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "hyprconf.h"
#include "monitorlayout.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

// A pretend system: every command the store runs is recorded, and answered
// from a table keyed by "program arg arg...".
struct FakeSystem {
    QStringList calls;
    // What each call was given on standard input, by call.
    QHash<QString, QByteArray> inputs;
    QHash<QString, ProcResult> answers;
    QString configErrorsAfterReload;
    bool reloaded = false;
    // Side effects a pretend command has, such as the files unsquashfs writes.
    std::function<void(const QString &, const QStringList &)> onRun;

    Exec exec() {
        return [this](const QString &program, const QStringList &args, int, const QByteArray &input) {
            const QString call = (QStringList{QFileInfo(program).fileName()} + args).join(' ');
            calls << call;
            if (onRun)
                onRun(QFileInfo(program).fileName(), args);
            if (!input.isEmpty())
                inputs[call] += input;
            if (call == "hyprctl reload")
                reloaded = true;
            if (call == "hyprctl configerrors") {
                ProcResult r{true, 0, reloaded ? configErrorsAfterReload.toUtf8() : QByteArray(), {}};
                return r;
            }
            for (auto it = answers.begin(); it != answers.end(); ++it)
                if (call == it.key())
                    return it.value();
            for (auto it = prefixAnswers.begin(); it != prefixAnswers.end(); ++it)
                if (call.startsWith(it.key()))
                    return it.value();
            return ProcResult{true, 0, {}, {}};
        };
    }
    void answer(const QString &call, const QByteArray &out, int code = 0) { answers.insert(call, ProcResult{true, code, out, {}}); }
    // For calls whose arguments are generated, such as a script.
    QHash<QString, ProcResult> prefixAnswers;
    void answerPrefix(const QString &prefix, const QByteArray &out) { prefixAnswers.insert(prefix, ProcResult{true, 0, out, {}}); }

    // Secrets saved on NetworkManager connections, as "uuid setting key value".
    QStringList secrets;
    QString secretError;
    SecretWriter secretWriter() {
        return [this](const QString &uuid, const QString &setting, const QString &key, const QString &value) {
            secrets << QStringList{uuid, setting, key, value}.join(' ');
            return secretError;
        };
    }
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

// A minimal ELF64 file: an AppImage's magic and a .upd_info section, which
// is all Settings reads of one.
static QByteArray fakeElf(const QByteArray &updateInfo, bool appImage = true) {
    const QByteArray names = QByteArray("\0.shstrtab\0.upd_info\0", 21);
    QByteArray file(64, '\0');
    file.replace(0, 4, "\x7f" "ELF");
    file[4] = 2; // 64-bit
    file[5] = 1; // little-endian
    file[6] = 1;
    if (appImage)
        file.replace(8, 3, QByteArray("AI\x02", 3));
    const quint64 namesAt = file.size();
    file += names;
    const quint64 infoAt = file.size();
    file += updateInfo + QByteArray(1, '\0');
    while (file.size() % 8)
        file += '\0';
    const quint64 tableAt = file.size();
    auto put16 = [&](int at, quint16 v) { qToLittleEndian(v, reinterpret_cast<uchar *>(file.data()) + at); };
    put16(0x3A, 64);
    put16(0x3C, 3);
    put16(0x3E, 1);
    qToLittleEndian(tableAt, reinterpret_cast<uchar *>(file.data()) + 0x28);
    auto section = [&](quint32 name, quint64 offset, quint64 size) {
        QByteArray s(64, '\0');
        qToLittleEndian(name, reinterpret_cast<uchar *>(s.data()));
        qToLittleEndian(offset, reinterpret_cast<uchar *>(s.data()) + 0x18);
        qToLittleEndian(size, reinterpret_cast<uchar *>(s.data()) + 0x20);
        return s;
    };
    file += section(0, 0, 0) + section(1, namesAt, names.size()) + section(11, infoAt, updateInfo.size() + 1);
    return file;
}

class OmasettingsTests : public QObject {
    Q_OBJECT

    Paths pathsIn(const QTemporaryDir &dir) {
        Paths p;
        p.config = dir.path() + "/config";
        p.state = dir.path() + "/state";
        p.omarchy = dir.path() + "/omarchy";
        p.home = dir.path() + "/home";
        p.downloads = dir.path() + "/home/Downloads";
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
        sys.answer("hyprctl -j monitors all", R"([{"name":"DP-1"}])");
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.act("monitor.apply", {"DP-1", QVariantMap{{"disabled", true}}}).contains("only display"));
        QVERIFY(!readText(store.managedLuaPath()).contains("disabled"));
        sys.answer("hyprctl -j monitors all", R"([{"name":"DP-1"},{"name":"HDMI-A-1"}])");
        QCOMPARE(store.act("monitor.apply", {"DP-1", QVariantMap{{"disabled", true}}}), QString());
        QVERIFY(readText(store.managedLuaPath()).contains(R"(output = "DP-1", disabled = true)"));
        QCOMPARE(store.act("monitor.restore", {"DP-1", QVariant()}), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("DP-1"));
    }

    void luaWritesMonitorKeysMirrorsAndColor() {
        QJsonObject state{{"monitors", QJsonObject{
            {"desc:Dell Inc. U2723QE 1234", QJsonObject{{"mode", "3840x2160@60"}, {"position", "0x0"}, {"scale", 1.5},
                                                       {"vrr", 1}, {"bitdepth", 10}, {"cm", "hdr"},
                                                       {"sdrbrightness", 1.2}, {"sdrsaturation", 3}}},
            {"HDMI-A-1", QJsonObject{{"mode", "1920x1080@60"}, {"position", "2560x0"}, {"scale", 1},
                                     {"mirror", "desc:Dell Inc. U2723QE 1234"}, {"vrr", 7}, {"bitdepth", 12}, {"cm", "evil"}}},
            {"desc:Evil\"); os.execute(\"x", QJsonObject{{"mode", "preferred"}}},
            {"desc:", QJsonObject{{"mode", "preferred"}}}}}};
        const QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(hl.monitor({ output = "desc:Dell Inc. U2723QE 1234", mode = "3840x2160@60", position = "0x0", scale = 1.5, vrr = 1, bitdepth = 10, cm = "hdr", sdrbrightness = 1.2 }))"));
        // A mirror has no place of its own, and bad values are left out.
        QVERIFY(lua.contains(R"(hl.monitor({ output = "HDMI-A-1", mode = "1920x1080@60", position = "auto", scale = 1, mirror = "desc:Dell Inc. U2723QE 1234" }))"));
        QVERIFY(lua.contains(R"(output = "desc:Evil\"); os.execute(\"x")"));
        QVERIFY(!lua.contains(R"(output = "desc:")"));
        QVERIFY(!lua.contains("sdrsaturation"));
    }

    void luaWritesWorkspacesAndMainDisplay() {
        QJsonObject state{{"primary", "desc:Dell Inc. U2723QE 1234"},
                          {"workspaces", QJsonObject{{"7", "HDMI-A-1"}, {"6", "HDMI-A-1"}, {"11", "HDMI-A-1"}, {"2", "bad;name"}}}};
        QString lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(default_monitor = "desc:Dell Inc. U2723QE 1234",)"));
        QVERIFY(lua.contains(R"(hl.workspace_rule({ workspace = "1", monitor = "desc:Dell Inc. U2723QE 1234", default = true }))"));
        QVERIFY(lua.contains(R"(hl.workspace_rule({ workspace = "6", monitor = "HDMI-A-1", default = true }))"));
        QVERIFY(lua.contains(R"(hl.workspace_rule({ workspace = "7", monitor = "HDMI-A-1" }))"));
        QVERIFY(!lua.contains(R"(workspace = "11")"));
        QVERIFY(!lua.contains(R"(workspace = "2")"));
        // Workspace 1 put elsewhere wins over the main display.
        state.insert("workspaces", QJsonObject{{"1", "HDMI-A-1"}});
        lua = Hypr::renderLua(state);
        QVERIFY(lua.contains(R"(hl.workspace_rule({ workspace = "1", monitor = "HDMI-A-1", default = true }))"));
        QVERIFY(!lua.contains(R"(monitor = "desc:Dell)"));
    }

    void monitorKeysAreDescriptionsUnlessShared() {
        QVERIFY(Hypr::validMonitorKey("DP-2"));
        QVERIFY(Hypr::validMonitorKey("desc:Samsung Electric Company Odyssey G85SD H1AK500000"));
        QVERIFY(!Hypr::validMonitorKey("desc:"));
        QVERIFY(!Hypr::validMonitorKey("desc:a\nb"));
        QVERIFY(!Hypr::validMonitorKey("DP 2"));
        const QJsonArray live = QJsonDocument::fromJson(R"([
            {"name":"DP-1","description":"Dell U2723QE 1"},
            {"name":"DP-2","description":"Acme Screen"},{"name":"DP-3","description":"Acme Screen"},
            {"name":"eDP-1","description":""}])").array();
        const QHash<QString, QString> keys = Store::monitorKeys(live);
        QCOMPARE(keys.value("DP-1"), QString("desc:Dell U2723QE 1"));
        QCOMPARE(keys.value("DP-2"), QString("DP-2"));
        QCOMPARE(keys.value("DP-3"), QString("DP-3"));
        QCOMPARE(keys.value("eDP-1"), QString("eDP-1"));
    }

    void displaysApplyTogetherAndMoveToTheirDescription() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j monitors all", R"([{"name":"DP-1","description":"Dell U2723QE 1"},{"name":"HDMI-A-1","description":"LG TV"}])");
        Store store(pathsIn(dir), sys.exec());
        // A choice saved by connector before displays were matched by
        // description.
        QCOMPARE(store.act("monitor.apply", {"DP-1", QVariantMap{{"mode", "preferred"}, {"position", "0x0"}}}), QString());
        QVERIFY(readText(store.managedLuaPath()).contains(R"(output = "DP-1")"));
        QCOMPARE(store.act("monitors.primary", {"DP-1"}), QString());

        const int writes = sys.calls.count("hyprctl reload");
        QCOMPARE(store.act("monitors.apply", {QVariantMap{
                     {"desc:Dell U2723QE 1", QVariantMap{{"mode", "preferred"}, {"position", "1920x0"}}},
                     {"desc:LG TV", QVariantMap{{"mode", "1920x1080@60"}, {"position", "0x0"}}}}}), QString());
        QCOMPARE(sys.calls.count("hyprctl reload"), writes + 1);
        QString lua = readText(store.managedLuaPath());
        QVERIFY(lua.contains(R"(output = "desc:Dell U2723QE 1", mode = "preferred", position = "1920x0")"));
        QVERIFY(lua.contains(R"(output = "desc:LG TV")"));
        QVERIFY(!lua.contains(R"(output = "DP-1")"));
        QVERIFY(lua.contains(R"(default_monitor = "desc:Dell U2723QE 1")"));

        // Mirroring every display leaves none with a picture of its own.
        QVERIFY(store.act("monitors.apply", {QVariantMap{
                    {"desc:LG TV", QVariantMap{{"mode", "preferred"}, {"mirror", "desc:Dell U2723QE 1"}}},
                    {"desc:Dell U2723QE 1", QVariantMap{{"mode", "preferred"}, {"mirror", "desc:LG TV"}}}}}).contains("mirror"));
        QCOMPARE(store.act("monitors.apply", {QVariantMap{
                     {"desc:LG TV", QVariantMap{{"mode", "preferred"}, {"mirror", "desc:Dell U2723QE 1"}}}}}), QString());
        QVERIFY(readText(store.managedLuaPath()).contains(R"(mirror = "desc:Dell U2723QE 1")"));
        QVERIFY(store.act("monitors.apply", {QVariantMap{{"desc:LG TV", QVariantMap{{"mode", "preferred"}, {"mirror", "desc:LG TV"}}}}})
                    .contains("itself"));

        // Undoing a change with null forgets the display under every name.
        QCOMPARE(store.act("monitors.restore", {QVariantMap{{"desc:LG TV", QVariant()}}}), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("LG TV"));
    }

    void workspacesMoveToTheirDisplay() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("hyprctl -j monitors all", R"([{"name":"DP-1","description":"Dell U2723QE 1"},{"name":"HDMI-A-1","description":"LG TV"}])");
        sys.answer("hyprctl -j activeworkspace", R"({"id":2})");
        sys.answer("hyprctl -j workspaces", R"([{"id":2,"monitor":"DP-1"},{"id":6,"monitor":"DP-1"},{"id":7,"monitor":"HDMI-A-1"}])");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("monitors.workspaces", {QVariantMap{{"6", "desc:LG TV"}, {"7", "desc:LG TV"}}}), QString());
        QVERIFY(readText(store.managedLuaPath()).contains(R"(hl.workspace_rule({ workspace = "6", monitor = "desc:LG TV", default = true }))"));
        const QString calls = sys.calls.join('\n');
        QVERIFY(calls.contains(R"(hl.dsp.workspace.move({ workspace = "6", monitor = "desc:LG TV" }))"));
        QVERIFY(!calls.contains(R"(workspace = "7", monitor)"));
        QVERIFY(calls.contains(R"(hl.dsp.focus({ workspace = "2" }))"));
        QVERIFY(store.act("monitors.workspaces", {QVariantMap{{"11", "desc:LG TV"}}}).contains("1 to 10"));
        // Restore Defaults for Displays forgets layout, workspaces, and the
        // main display together.
        QCOMPARE(store.act("monitors.primary", {"desc:LG TV"}), QString());
        QCOMPARE(store.act("monitor.apply", {"desc:LG TV", QVariantMap{{"mode", "preferred"}, {"position", "0x0"}}}), QString());
        QCOMPARE(store.resetPane("displays"), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("hl.monitor"));
        QVERIFY(!readText(store.managedLuaPath()).contains("default_monitor"));
        QVERIFY(!readText(store.managedLuaPath()).contains("workspace_rule"));
        QCOMPARE(store.act("monitors.workspaces", {QVariantMap{{"6", "desc:LG TV"}, {"7", "desc:LG TV"}}}), QString());
        QCOMPARE(store.act("monitors.workspaces", {QVariantMap{{"6", QString()}, {"7", QString()}}}), QString());
        QVERIFY(!readText(store.managedLuaPath()).contains("workspace_rule"));
    }

    void identifyMovesEachNumberToItsDisplay() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.act("monitors.identify", {"DP-1\"); os.execute(\"x"}).contains("Unknown display"));
        QCOMPARE(store.act("monitors.identify", {"HDMI-A.1"}), QString());
        const QString calls = sys.calls.join('\n');
        // Hyprland matches the whole title, which Qt ends with " — Settings".
        QVERIFY(calls.contains(R"(hl.dsp.window.move({ monitor = "HDMI-A.1", window = "title:^omasettings-identify-HDMI-A\\.1( .*)?$" }))"));
        QVERIFY(calls.contains(R"(hl.dsp.window.fullscreen({ mode = "fullscreen", action = "set", window = "title:^omasettings-identify-HDMI-A\\.1( .*)?$" }))"));
        QVERIFY(!readText(store.managedLuaPath()).contains("HDMI"));
    }

    void draggedDisplaysSnapEdgeToEdge() {
        const QRect a(0, 0, 2560, 1440), b(2560, 0, 1920, 1080);
        // Dropped near the right edge: snaps to it, tops lined up.
        QCOMPARE(Layout::place({a, b}, 1, QPoint(2575, 12), 40), (QList<QPoint>{{0, 0}, {2560, 0}}));
        // Dropped on top of the other: pushed out to the nearest side.
        QCOMPARE(Layout::place({a, b}, 1, QPoint(2000, 100), 40), (QList<QPoint>{{0, 0}, {2560, 100}}));
        // Dropped far away: brought back against the nearest edge.
        QCOMPARE(Layout::place({a, b}, 1, QPoint(500, 3000), 40), (QList<QPoint>{{0, 0}, {500, 1440}}));
        // Dropped left of the first: everything shifts to start at 0,0.
        QCOMPARE(Layout::place({a, b}, 1, QPoint(-1930, 0), 40), (QList<QPoint>{{1920, 0}, {0, 0}}));
        // One display always sits at 0,0.
        QCOMPARE(Layout::place({a}, 0, QPoint(300, 300), 40), (QList<QPoint>{{0, 0}}));
    }

    void wifiAndVpnPasswordsNeverGoInArguments() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("nmcli -t -f SSID,SECURITY device wifi list --rescan no", "Cafe:WPA2\nHome\\:5G:WPA3\n");
        sys.answer("nmcli connection add type wifi con-name Cafe ssid Cafe wifi-sec.key-mgmt wpa-psk",
                   "Connection 'Cafe' (11111111-2222-3333-4444-555555555555) successfully added.\n");
        Store store(pathsIn(dir), sys.exec(), sys.secretWriter());
        const QString password = R"(  my "pass" \word  )";
        QCOMPARE(store.act("wifi.connect", {"Cafe", password}), QString());
        // Saved as typed, and not through nmcli at all: not in its
        // arguments, not on its standard input, not in its editor.
        QCOMPARE(sys.secrets, QStringList{"11111111-2222-3333-4444-555555555555 802-11-wireless-security psk " + password});
        QVERIFY(sys.calls.contains("nmcli --wait 30 connection up uuid 11111111-2222-3333-4444-555555555555"));
        for (const QString &call : sys.calls) {
            QVERIFY2(!call.contains("pass"), qPrintable(call));
            QVERIFY2(!call.contains("connection edit"), qPrintable(call));
        }
        QVERIFY(sys.inputs.isEmpty());

        // A network secured with WPA3 alone is joined with SAE; one that
        // fails is not left behind as a saved profile.
        sys.answer("nmcli connection add type wifi con-name Home:5G ssid Home:5G wifi-sec.key-mgmt sae",
                   "Connection 'Home:5G' (66666666-2222-3333-4444-555555555555) successfully added.\n");
        sys.answer("nmcli --wait 30 connection up uuid 66666666-2222-3333-4444-555555555555", "Error: Secrets were required", 4);
        QVERIFY(!store.act("wifi.connect", {"Home:5G", "wrong-password"}).isEmpty());
        QVERIFY(sys.calls.contains("nmcli connection delete uuid 66666666-2222-3333-4444-555555555555"));
        QVERIFY(store.act("wifi.connect", {"Cafe", "tab\there!"}).contains("line breaks"));

        // VPN: the user name in vpn.data, the password never near nmcli.
        const QString vpn = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
        QCOMPARE(store.act("vpn.credentials", {vpn, "me", "s3cret pass"}), QString());
        QCOMPARE(sys.secrets.last(), vpn + " vpn password s3cret pass");
        for (const QString &call : sys.calls) {
            QVERIFY2(!call.contains("s3cret"), qPrintable(call));
            QVERIFY2(!call.contains("connection edit"), qPrintable(call));
        }
        QVERIFY(sys.inputs.isEmpty());
    }

    void passwordsAnEarlierVersionLeftInNmcliHistoryAreRemoved() {
        QTemporaryDir dir;
        const QString path = dir.path() + "/nmcli-history";
        writeText(path, "[11111111-2222-3333-4444-555555555555]\n"
                        "0=set 802-11-wireless-security.psk\n1=wifi pass\n2=save persistent\n3=quit\n"
                        "4=set ipv4.method auto\n5=set wifi-sec.psk inline pass\n"
                        "\n[aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee]\n"
                        "0=set vpn.data password-flags=0\n1=set vpn.secrets\n2=password=vpn pass\n3=save persistent\n");
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        QVERIFY(scrubNmcliHistory(path));
        QCOMPARE(readText(path), QString("[11111111-2222-3333-4444-555555555555]\n"
                                         "2=save persistent\n3=quit\n"
                                         "4=set ipv4.method auto\n"
                                         "\n[aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee]\n"
                                         "0=set vpn.data password-flags=0\n3=save persistent\n"));
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
        // Nothing left to take out: the file is left alone.
        QVERIFY(!scrubNmcliHistory(path));
        QVERIFY(!scrubNmcliHistory(dir.path() + "/missing"));
    }

    void secretErrorsNeverShowThePassword() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.secretError = "failed to set 'password' property: 'hunter22' is invalid";
        Store store(pathsIn(dir), sys.exec(), sys.secretWriter());
        const QString error = store.act("vpn.credentials", {"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", "me", "hunter22"});
        QVERIFY(error.contains("is invalid"));
        QVERIFY(!error.contains("hunter22"));
    }

    void actionsRejectUnsafeArguments() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QVERIFY(store.act("bluetooth.connect", {"; reboot"}).contains("Unknown device"));
        QVERIFY(store.act("monitor.apply", {"DP-1;x", QVariantMap{}}).contains("Unknown display"));
        QVERIFY(store.act("launch", {"rm"}).contains("Unknown command"));
        QVERIFY(store.act("background.set", {"/etc/passwd"}).contains("not in the current theme"));
        QVERIFY(store.act("edit", {"/etc/passwd"}).contains("Unknown file"));
        QVERIFY(store.act("edit", {"../../etc/passwd"}).contains("Unknown file"));
    }

    void configFilesListWhereEachPagesFileIs() {
        QTemporaryDir dir;
        FakeSystem sys;
        const Paths paths = pathsIn(dir);
        writeText(paths.config + "/hypr/bindings.lua", "-- mine\n");
        writeText(paths.home + "/.gitconfig", "[user]\n");
        Store store(paths, sys.exec());
        QHash<QString, QVariantMap> byKey;
        for (const QVariant &v : store.list("configFiles").toList())
            byKey.insert(v.toMap().value("key").toString(), v.toMap());
        // Every key a page names is in the table.
        for (const QString &key : QStringList{"bindings", "input", "looknfeel", "monitors", "hyprsunset", "autostart", "hyprland",
                                   "managed", "shell", "shelltoml", "menu", "screensaver", "about", "mimeapps",
                                   "terminals", "fontconfig", "alacritty", "kitty", "ghostty", "foot", "gitconfig",
                                   "xcompose"})
            QVERIFY2(byKey.contains(key), qPrintable(key));
        QCOMPARE(byKey["bindings"].value("name").toString(), QString("bindings.lua"));
        QVERIFY(byKey["bindings"].value("exists").toBool());
        QVERIFY(!byKey["input"].value("exists").toBool());
        // Files under ~ are shown that way; ~/.config may live elsewhere.
        QCOMPARE(byKey["gitconfig"].value("path").toString(), QString("~/.gitconfig"));
        QVERIFY(byKey["gitconfig"].value("exists").toBool());
        QCOMPARE(byKey["menu"].value("path").toString(), paths.config + "/omarchy/extensions/omarchy-menu.jsonc");
        QCOMPARE(store.configFilePath("xcompose"), paths.home + "/.XCompose");
        // Git's XDG file counts only when there is no ~/.gitconfig.
        writeText(paths.config + "/git/config", "[user]\n");
        QCOMPARE(store.configFilePath("gitconfig"), paths.home + "/.gitconfig");
        QVERIFY(QFile::remove(paths.home + "/.gitconfig"));
        QCOMPARE(store.configFilePath("gitconfig"), paths.config + "/git/config");
        QVERIFY(store.configFilePath("nope").isEmpty());
    }

    // ---- Hidden Wi-Fi, VPN, plugins, the bar ----------------------------------------
    void hiddenNetworksAreJoinedByName() {
        QTemporaryDir dir;
        FakeSystem sys;
        sys.answer("nmcli connection add type wifi con-name Secret Lab ssid Secret Lab wifi-sec.key-mgmt wpa-psk 802-11-wireless.hidden yes",
                   "Connection 'Secret Lab' (77777777-2222-3333-4444-555555555555) successfully added.\n");
        Store store(pathsIn(dir), sys.exec(), sys.secretWriter());
        QCOMPARE(store.act("wifi.connectHidden", {"Secret Lab", "hunter22"}), QString());
        QCOMPARE(sys.secrets, QStringList{"77777777-2222-3333-4444-555555555555 802-11-wireless-security psk hunter22"});
        QVERIFY(sys.calls.contains("nmcli --wait 30 connection up uuid 77777777-2222-3333-4444-555555555555"));
        QVERIFY(!sys.calls.join('\n').contains("hunter22"));
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
        Store store(pathsIn(dir), sys.exec(), sys.secretWriter());
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
        QVERIFY(sys.calls.contains("nmcli connection modify uuid " + uuid + " +vpn.data password-flags=0"));
        QCOMPARE(sys.secrets, QStringList{uuid + " vpn password pw"});
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

    void panesAndPagesCanBeFoundFromTheMenuSearch() {
        QTemporaryDir dir;
        FakeSystem sys;
        const QString menu = dir.path() + "/config/omarchy/extensions/omarchy-menu.jsonc";
        writeText(menu, "{\n  \"mine\": {\"label\":\"Mine\",\"action\":\"true\"},\n}\n");
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("menu.searchEntries", {QVariantList{}, "Settings"}), QString());
        QVERIFY(!store.list("menuSearch").toMap().value("on").toBool());

        const QVariantList entries{
            QVariantMap{{"id", "vpn"}, {"target", "vpn"}, {"icon", "x"}, {"label", "VPN"},
                        {"searchTerms", QVariantList{"vpn wireguard", "  "}}},
            QVariantMap{{"id", "general.datetime"}, {"target", "general/datetime"}, {"label", "Datum \"& Uhrzeit\""},
                        {"searchTerms", QVariantList{"Date & Time"}}},
        };
        QCOMPARE(store.act("menu.searchEntries", {entries, "Einstellungen"}), QString());
        QCOMPARE(store.write("integration.menuPlace", "setup"), QString());
        const QString text = readText(menu);
        QVERIFY(store.list("menuSearch").toMap().value("on").toBool());
        // The rest of the file is as it was, and the file is still a menu.
        QVERIFY(text.contains("\"mine\": {\"label\":\"Mine\",\"action\":\"true\"}"));
        QVERIFY(text.contains("\"setup.omasettings\""));
        QVERIFY(text.contains("\"omasettings-search\": {\"icon\":"));
        QVERIFY(text.contains("\"label\":\"Einstellungen\",\"when\":\"false\""));
        QVERIFY(text.contains("\"omasettings-search.general.datetime\": {\"action\":\"omasettings open general/datetime\",\"icon\":\"\",\"label\":\"Datum \\\"& Uhrzeit\\\"\",\"searchTerms\":[\"Date & Time\"]}"));
        QVERIFY(text.contains("\"searchTerms\":[\"vpn wireguard\"]"));
        QJsonParseError error;
        QJsonDocument::fromJson(text.split('\n').filter(QRegularExpression("^(?!\\s*//)")).join('\n').replace(",\n}", "\n}").toUtf8(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);

        // The same entries again leave the file alone; new ones replace the block.
        const QDateTime written = QFileInfo(menu).lastModified();
        QTest::qWait(20);
        QCOMPARE(store.act("menu.searchEntries", {entries, "Einstellungen"}), QString());
        QCOMPARE(QFileInfo(menu).lastModified(), written);
        QCOMPARE(store.act("menu.searchEntries", {QVariantList{entries[0]}, "Settings"}), QString());
        QCOMPARE(readText(menu).count("omasettings:search begin"), 1);
        QVERIFY(!readText(menu).contains("general.datetime"));

        // Ids and targets go into a shell command, so only plain ones do.
        QVERIFY(!store.act("menu.searchEntries", {QVariantList{QVariantMap{{"id", "x"}, {"target", "vpn; rm -rf ~"}, {"label", "X"}}}, ""}).isEmpty());
        QVERIFY(!store.act("menu.searchEntries", {QVariantList{QVariantMap{{"id", "x\"y"}, {"target", "vpn"}, {"label", "X"}}}, ""}).isEmpty());

        // None takes the block out, from the app or the command line.
        QCOMPARE(store.act("menu.searchEntries", {"[]"}), QString());
        QVERIFY(!readText(menu).contains("omasettings-search"));
        QVERIFY(readText(menu).contains("\"setup.omasettings\""));
        QVERIFY(!store.list("menuSearch").toMap().value("on").toBool());
    }

    void aRuleCanBeChangedInPlace() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        QCOMPARE(store.act("rule.add", {QVariantMap{{"class", "^(mpv)$"}, {"float", true}}}), QString());
        QCOMPARE(store.act("rule.add", {QVariantMap{{"class", "^(imv)$"}, {"center", true}}}), QString());
        QCOMPARE(store.act("rule.update", {0, QVariantMap{{"class", "steam"}, {"title", "Steam"}, {"tile", true}}}), QString());
        const QJsonArray rules = store.managedState().value("windowRules").toArray();
        QCOMPARE(rules.size(), 2);
        QCOMPARE(rules[0].toObject().value("class").toString(), QString("steam"));
        QVERIFY(!rules[0].toObject().contains("float"));
        QCOMPARE(rules[1].toObject().value("class").toString(), QString("^(imv)$"));
        const QString lua = readText(store.managedLuaPath());
        QVERIFY(lua.contains(R"(o.window({ class = "steam", title = "Steam" }, { tile = true }))"));
        QVERIFY(!lua.contains("mpv"));
        QVERIFY(store.act("rule.update", {7, QVariantMap{{"class", "x"}, {"float", true}}}).contains("gone"));
        QVERIFY(store.act("rule.update", {0, QVariantMap{{"float", true}}}).contains("Choose an app"));
    }

    void aClassPatternNamesTheAppsItMatches() {
        const QVariantList installed{
            QVariantMap{{"id", "steam"}, {"name", "Steam"}, {"icon", "steam"}, {"wmClass", "steam"}},
            QVariantMap{{"id", "brave-browser"}, {"name", "Brave"}, {"wmClass", "brave-browser"}},
            QVariantMap{{"id", "chromium"}, {"name", "Chromium"}, {"wmClass", "chromium"}},
            QVariantMap{{"id", "hidden"}, {"name", "Hidden"}, {"wmClass", "steamwebhelper"}, {"hidden", true}},
        };
        QCOMPARE(Store::appForPattern(installed, "steam").value("appName").toString(), QString("Steam"));
        QCOMPARE(Store::appForPattern(installed, "^(steam)$").value("appIcon").toString(), QString("steam"));
        // Hyprland matches the whole class, so "steam" isn't steamwebhelper.
        QCOMPARE(Store::appForPattern(installed, "steam.*").value("appName").toString(), QString("Steam"));
        QCOMPARE(Store::appForPattern(installed, "(chromium|brave-browser)").value("appNames").toStringList(),
                 QStringList({"Brave", "Chromium"}));
        QVERIFY(Store::appForPattern(installed, "mpv").isEmpty());
        QVERIFY(Store::appForPattern(installed, "(").isEmpty());
    }

    void omarchysOwnRulesAreListed() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/hypr/apps/steam.lua", "o.window(\"steam\", { float = true })\n");
        writeText(dir.path() + "/config/hypr/input.lua", "-- mine\n");
        // What Hyprland's Lua hands back after running the files.
        sys.answerPrefix("hyprctl repl ", R"([
            {"file":"apps/steam.lua","source":"omarchy","match":{"class":"steam"},"rules":{"float":true}},
            {"file":"apps/steam.lua","source":"omarchy","match":{"class":"steam","title":"Steam"},"rules":{"center":true}},
            {"file":"apps/steam.lua","source":"omarchy","match":{"class":"steam","title":"Steam"},"rules":{"size":[1100,700]}},
            {"file":"windows.lua","source":"omarchy","match":{"tag":"floating-window"},"rules":{"float":true}},
            {"file":"windows.lua","source":"omarchy","match":{"class":".*"},"rules":{"suppress_event":"maximize"}},
            {"file":"apps/bitwarden.lua","source":"omarchy","match":{"class":"^([bB]itwarden)$"},"rules":{"tag":"+floating-window"}},
            {"file":"~/.config/hypr/input.lua","source":"config","match":{"class":"mpv"},"rules":{"workspace":"3"}}
        ])");
        Store store(pathsIn(dir), sys.exec());
        const QVariantList others = store.list("windowRules").toMap().value("others").toList();

        // The files ran in Hyprland: Omarchy's, then the user's, not Settings' own.
        const QString call = sys.calls.filter("hyprctl repl ").value(0);
        QVERIFY(call.contains(dir.path() + "/omarchy/default/hypr/apps/steam.lua"));
        QVERIFY(call.contains(dir.path() + "/config/hypr/input.lua"));
        QVERIFY(!call.contains("omasettings.lua"));

        // Rules for every window or a tag alone aren't an app's; the two
        // lines for Steam's main window read as one rule.
        QCOMPARE(others.size(), 4);
        const QVariantMap main = others[1].toMap();
        QCOMPARE(main.value("title").toString(), QString("Steam"));
        QVERIFY(main.value("rules").toMap().value("center").toBool());
        QCOMPARE(main.value("rules").toMap().value("size").toList().value(0).toInt(), 1100);
        // A rule that only adds a tag reads as what the tag does.
        const QVariantMap bitwarden = others[2].toMap();
        QVERIFY(bitwarden.value("rules").toMap().value("float").toBool());
        QVERIFY(!bitwarden.value("rules").toMap().contains("tag"));
        QCOMPARE(others[3].toMap().value("source").toString(), QString("config"));
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

    void importReloadsHyprlandOnce() {
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        const QString file = dir.path() + "/settings.json";
        writeText(file, R"({"app":"omasettings","version":1,"settings":{"desktop.borderSize":3,"keyboard.repeatRate":40,"desktop.gapsInner":7}})");
        QCOMPARE(store.act("settings.import", {file}), QString());
        QCOMPARE(sys.calls.count("hyprctl reload"), 1);
        QCOMPARE(store.managedState().value("hypr").toObject().size(), 3);
    }

    // ---- Sync -------------------------------------------------------------------
    void syncKeepsThePersonAndLeavesTheMachine() {
        QCOMPARE(Store::syncPackages({"linux-lts", "amd-ucode", "sof-firmware", "omarchy-keyring", "nvidia-open-dkms",
                                      "supergfxctl", "rocm-smi-lib", "htop", "btop", ""}, {"btop"}),
                 QStringList{"htop"});
        QCOMPARE(Store::miseTools("[env]\nA = \"1\"\n[tools]\nnode = \"22\" # lts\n'cargo:x' = ['1', '2']\n"
                                  "python = { version = \"3.12\" }\n"),
                 (QStringList{"node@22", "cargo:x@latest", "python@latest"}));
        for (const char *id : {"power.batteryProfile", "power.acProfile", "display.mirror", "display.laptopScreen",
                               "notifications.doNotDisturb", "sound.output", "wifi.enabled"})
            QVERIFY2(Store::isMachineSpecific(id), id);
        QVERIFY(!Store::isMachineSpecific("appearance.theme"));
    }

    void syncSnapshotHoldsPartsAndKeepsSkippedOnes() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths p = pathsIn(dir);
        p.data = dir.path() + "/data";
        sys.answer("pacman -Qqe", "base\nlinux\nnvidia-open-dkms\nripgrep\nslack-desktop\nchromium\nzoom\n");
        sys.answer("omarchy-pkg-defaults", "chromium\n");
        sys.answer("pacman -Qqm", "zoom\n");
        Store store(p, sys.exec());
        writeText(p.config + "/omarchy/shell.json", R"({"bar":{"position":"bottom"},"idle":{"lock":300},"dock":{"pinned":["a"]}})");
        writeText(p.config + "/omarchy/extensions/omarchy-menu.jsonc", "{\n  \"setup\": { \"when\": \"false\" }\n}\n");
        writeText(p.config + "/mise/config.toml", "[tools]\nnode = \"22\"\n\"npm:x\" = \"latest\"\n[settings]\nfoo = 1\n");
        for (const char *f : {"bindings.lua", "monitors.lua", "omasettings.lua", "hyprsunset.conf", "bindings.lua.bak.1788345603",
                              "hyprland.lua.omasettings.bak", "keybindings-editor/overrides.lua", "keybindings-editor/.lock"})
            writeText(p.config + "/hypr/" + f, QByteArray("-- ") + f);
        const QString out = dir.path() + "/snap";
        writeText(out + "/dotfiles/kept.txt", "from another computer");

        QCOMPARE(store.writeSyncSnapshot(out, {"dotfiles"}, {}), QString());
        const QJsonObject settings = QJsonDocument::fromJson(readText(out + "/settings.json").toUtf8()).object();
        QVERIFY(settings.contains("settings"));
        QVERIFY(!settings.contains("exported"));
        QVERIFY(!settings.value("settings").toObject().contains("general.hostname"));
        const QJsonObject shell = QJsonDocument::fromJson(readText(out + "/desktop/shell.json").toUtf8()).object();
        QCOMPARE(shell.value("bar").toObject().value("position").toString(), QString("bottom"));
        QVERIFY(!shell.contains("idle"));
        QVERIFY(readText(out + "/desktop/menu.jsonc").contains("\"setup\""));
        QCOMPARE(readText(out + "/apps/pacman.txt"), QString("ripgrep\nslack-desktop\n"));
        QCOMPARE(readText(out + "/apps/aur.txt"), QString("zoom\n"));
        QCOMPARE(readText(out + "/apps/mise.txt"), QString("node@22\nnpm:x@latest\n"));
        // Hyprland's files travel, but not the displays, what Settings
        // generates, backups, or locks.
        QCOMPARE(QDir(out + "/hypr").entryList(QDir::Files | QDir::Hidden), QStringList{"bindings.lua"});
        QCOMPARE(QDir(out + "/hypr/keybindings-editor").entryList(QDir::Files | QDir::Hidden), QStringList{"overrides.lua"});
        // Left out here, so what the repository has stays.
        QCOMPARE(readText(out + "/dotfiles/kept.txt"), QString("from another computer"));
    }

    void syncUploadWaitsForNewerSettings() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths p = pathsIn(dir);
        p.data = dir.path() + "/data";
        Store store(p, sys.exec());
        QDir().mkpath(store.syncDir() + "/.git");
        writeText(store.syncStatePath(), "{\"url\":\"https://github.com/me/omarchy-sync\",\"branch\":\"main\",\"lastCommit\":\"aaa\"}");
        sys.answer("git -C " + store.syncDir() + " rev-parse --verify --quiet refs/remotes/origin/main", "bbb\n");

        QCOMPARE(store.act("sync.upload", {"auto"}), QString());
        QVERIFY(store.act("sync.upload", {""}).contains("newer settings"));
        for (const QString &call : sys.calls)
            QVERIFY2(!call.contains(" push ") && !call.contains(" commit "), qPrintable(call));
        const QJsonObject state = QJsonDocument::fromJson(readText(store.syncStatePath()).toUtf8()).object();
        QVERIFY(state.value("pending").toBool());
        QVERIFY(store.syncAuto());
    }

    void syncCopyIsOnlyOpenToYou() {
        const QFileDevice::Permissions ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        const QFileDevice::Permissions all = ownerOnly | QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                             QFileDevice::ReadOther | QFileDevice::ExeOther;
        auto mode = [](const QString &path) {
            return QFileInfo(path).permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser);
        };
        QTemporaryDir dir;
        Paths p = pathsIn(dir);
        p.data = dir.path() + "/data";

        // Connecting clones into a folder that is already private.
        const QString syncDir = p.data + "/omasettings/sync";
        {
            FakeSystem sys;
            const Exec fake = sys.exec();
            QFileDevice::Permissions modeAtClone;
            Exec exec = [&](const QString &program, const QStringList &args, int t, const QByteArray &in) {
                if (args.contains("clone")) {
                    modeAtClone = mode(syncDir);
                    QDir().mkpath(syncDir + "/.git");
                }
                return fake(program, args, t, in);
            };
            Store store(p, exec);
            QCOMPARE(store.syncDir(), syncDir);
            QCOMPARE(store.act("sync.connect", {"https://github.com/me/omarchy-sync"}), QString());
            QCOMPARE(modeAtClone, ownerOnly);
            QCOMPARE(mode(syncDir), ownerOnly);
        }

        // A copy an earlier version left open is closed on start.
        QFile::setPermissions(syncDir, all);
        writeText(syncDir + "/.config/tool/api-key", "secret");
        QCOMPARE(mode(syncDir), all);
        FakeSystem sys;
        Store store(p, sys.exec());
        QCOMPARE(mode(store.syncDir()), ownerOnly);
    }

    void syncPlanOnlyAddsWhatIsMissing() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths p = pathsIn(dir);
        p.data = dir.path() + "/data";
        sys.answer("pacman -Qq", "ripgrep\nhtop\n");
        Store store(p, sys.exec());
        const QString repo = store.syncDir();
        writeText(repo + "/apps/pacman.txt", "ripgrep\nfd\n");
        writeText(repo + "/apps/mise.txt", "node@22\ngo@latest\n");
        writeText(p.config + "/mise/config.toml", "[tools]\nnode = \"20\"\n");
        writeText(repo + "/dotfiles/.zshrc", "new");
        writeText(p.home + "/.zshrc", "old");
        writeText(repo + "/desktop/mimeapps.list", "[Default Applications]\n");
        writeText(repo + "/hypr/bindings.lua", "-- theirs");
        writeText(p.config + "/hypr/bindings.lua", "-- mine");
        writeText(repo + "/hypr/input.lua", "-- same");
        writeText(p.config + "/hypr/input.lua", "-- same");

        const QVariantMap plan = store.syncPlan();
        QCOMPARE(plan.value("hypr").toStringList(), QStringList{"bindings.lua"});
        QCOMPARE(plan.value("pacman").toStringList(), QStringList{"fd"});
        QCOMPARE(plan.value("mise").toStringList(), QStringList{"go@latest"});
        QCOMPARE(plan.value("dotfiles").toStringList(), QStringList{".zshrc"});
        QVERIFY(plan.value("desktop").toStringList().contains("mime"));

        // A part this computer leaves out isn't planned at all.
        QCOMPARE(store.act("sync.skip", {"dotfiles", false}), QString());
        QVERIFY(!store.syncPlan().contains("dotfiles"));
        QVERIFY(store.act("sync.skip", {"displays", false}).contains("Unknown part"));
    }

    void syncDotfilesStayInHomeAndAwayFromKeys() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths p = pathsIn(dir);
        p.data = dir.path() + "/data";
        Store store(p, sys.exec());
        writeText(p.home + "/.ssh/id_ed25519", "secret");
        writeText(p.home + "/.config/nvim/init.lua", "-- mine");
        QVERIFY(store.act("sync.dotfile.add", {p.home + "/.ssh"}).contains("keys or passwords"));
        QVERIFY(store.act("sync.dotfile.add", {"/etc/passwd"}).contains("home folder"));
        QCOMPARE(store.act("sync.dotfile.add", {p.home + "/.config/nvim"}), QString());
        const QJsonObject state = QJsonDocument::fromJson(readText(store.syncStatePath()).toUtf8()).object();
        QCOMPARE(state.value("dotfiles").toArray(), QJsonArray{".config/nvim"});
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

    void theAppsPaneListsWhatTheMenuInstallsAndRemoves() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n"
                  "  // Omarchy's menu\n"
                  "  \"install.package\": {\"label\":\"Package\",\"action\":\"omarchy-pkg-install\"},\n"
                  "  \"install.browser\": {\"label\":\"Browser\"},\n"
                  "  \"install.browser.zen\": {\"label\":\"Zen\",\"icon\":\"Z\",\"action\":\"omarchy-install-browser zen\",\"disabled\":\"omarchy-pkg-present zen-browser-bin\"},\n"
                  "  \"remove.browser.zen\": {\"label\":\"Zen\",\"action\":\"omarchy-remove-browser zen\",\"when\":\"omarchy-pkg-present zen-browser-bin\"},\n"
                  "  \"install.editor.vscode\": {\"label\":\"VSCode\",\"action\":\"x\",\"disabled\":\"omarchy-pkg-present visual-studio-code-bin\"},\n"
                  "  \"install.editor.zed\": {\"label\":\"Zed\",\"action\":\"y\",\"disabled\":\"omarchy-pkg-present zed\"},\n"
                  "  \"install.gaming.heroic\": {\"label\":\"Heroic\",\"action\":\"h\",\"when\":\"omarchy-hw-x86\"},\n"
                  "  \"install.development.javascript\": {\"label\":\"JavaScript\"},\n"
                  "  \"install.development.javascript.node\": {\"label\":\"Node.js\",\"action\":\"n\"},\n"
                  "  \"remove.development.javascript.node\": {\"label\":\"Node.js\",\"action\":\"rn\"},\n"
                  "  \"setup.default.dictation.voxtype\": {\"label\":\"Voxtype\",\"action\":\"omarchy-default-dictation voxtype\"},\n"
                  "  \"remove.dictation.voxtype\": {\"label\":\"Voxtype\",\"action\":\"rv\",\"when\":\"omarchy-pkg-present voxtype-bin\"},\n"
                  "  \"remove.service.ssh-agent\": {\"label\":\"SSH Agent\",\"action\":\"s\"},\n"
                  "  \"setup.default.agent\": {\"label\":\"Agent\"},\n"
                  "  \"setup.default.agent.claude\": {\"label\":\"Claude\",\"action\":\"omarchy-default-agent claude\"},\n"
                  "  \"setup.default.agent.hermes\": {\"label\":\"Hermes\",\"action\":\"omarchy-default-agent hermes\"},\n"
                  "  \"install.ai.claude\": {\"label\":\"Claude Desktop\",\"action\":\"c\",\"disabled\":\"omarchy-pkg-present claude-desktop\"},\n"
                  "  \"remove.ai.claude\": {\"label\":\"Claude Desktop\",\"action\":\"rc\"},\n"
                  "  \"remove.ai.hermes\": {\"label\":\"Hermes Desktop\",\"action\":\"rh\"},\n"
                  "  \"install.style.theme\": {\"label\":\"Theme\",\"action\":\"t\"}\n"
                  "}\n");
        Store store(pathsIn(dir), sys.exec());
        auto items = [&]() {
            QHash<QString, QVariantMap> byKey;
            for (const QVariant &v : store.list("software").toList())
                byKey.insert(v.toMap().value("key").toString(), v.toMap());
            return byKey;
        };
        QHash<QString, QVariantMap> all = items();
        // Apps, tools, dictation, and coding agents; not the SSH agent
        // switch or themes.
        QCOMPARE(QSet<QString>(all.keyBegin(), all.keyEnd()),
                 (QSet<QString>{"package", "browser.zen", "editor.vscode", "editor.zed", "gaming.heroic",
                                "development.javascript.node", "dictation.voxtype", "agent.claude", "agent.hermes",
                                "ai.claude"}));
        QCOMPARE(all["agent.claude"].value("installId").toString(), QString("setup.default.agent.claude"));
        QCOMPARE(all["agent.claude"].value("category").toString(), QString("agent"));
        // The agent menu has no installed check; omarchy-default-agent's own is used.
        QVERIFY(all["agent.claude"].value("known").toBool());
        // Hermes comes with its desktop app, so removing it is the app's
        // removal; Claude Desktop's removal is not the Claude agent's.
        QCOMPARE(all["agent.hermes"].value("removeId").toString(), QString("remove.ai.hermes"));
        QVERIFY(!all["agent.hermes"].value("dropRemove").toBool());
        QCOMPARE(all["agent.claude"].value("removeId").toString(), QString());
        QVERIFY(all["agent.claude"].value("dropRemove").toBool());
        QCOMPARE(all["browser.zen"].value("removeId").toString(), QString("remove.browser.zen"));
        QCOMPARE(all["development.javascript.node"].value("label").toString(), QString("Node.js (JavaScript)"));
        QCOMPARE(all["dictation.voxtype"].value("installId").toString(), QString("setup.default.dictation.voxtype"));
        QCOMPARE(all["package"].value("category").toString(), QString("tools"));
        QVERIFY(!all["package"].value("known").toBool());
        // Without a removal in the menu, the package it checks for can go.
        QCOMPARE(all["editor.zed"].value("package").toString(), QString("zed"));
        QVERIFY(all["editor.zed"].value("dropRemove").toBool());
        QVERIFY(!all["browser.zen"].value("dropRemove").toBool());

        // The menu's conditions decide what's installed and available.
        sys.answerPrefix("bash -c", "");
        all = items();
        QVERIFY(!all["browser.zen"].value("installed").toBool());
        QVERIFY(!all["gaming.heroic"].value("available").toBool());
        sys.prefixAnswers.clear();
        sys.answerPrefix("bash -c", "i:browser.zen\na:gaming.heroic\ni:agent.claude\n");
        all = items();
        QVERIFY(all["browser.zen"].value("installed").toBool());
        QVERIFY(all["agent.claude"].value("installed").toBool());
        QVERIFY(all["gaming.heroic"].value("available").toBool());
        QVERIFY(!all["editor.zed"].value("installed").toBool());
    }

    void appImagesAreFoundByTheirHeaders() {
        QTemporaryDir dir;
        const QString image = dir.path() + "/a.AppImage", plain = dir.path() + "/plain";
        writeText(image, fakeElf("gh-releases-zsync|kem-a|AppManager|latest|*x86_64.AppImage.zsync"));
        writeText(plain, fakeElf("", false));
        QVERIFY(Store::isAppImage(image));
        QVERIFY(!Store::isAppImage(plain));
        QCOMPARE(Store::appImageUpdateInfo(image), QString("gh-releases-zsync|kem-a|AppManager|latest|*x86_64.AppImage.zsync"));
        QCOMPARE(Store::appImageUpdateInfo(plain), QString());
    }

    void appImagesAreListedFromTheLauncherApplicationsAndDownloads() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString launched = dir.path() + "/elsewhere/Tool.AppImage";
        writeText(launched, fakeElf("zsync|https://example.com/tool.zsync"));
        writeText(paths.data + "/applications/tool.desktop",
                  "[Desktop Entry]\nName=Tool\nExec=\"" + launched.toUtf8() + "\" %U\nIcon=tool\nX-AppImage-Version=1.2\n");
        writeText(paths.data + "/icons/hicolor/256x256/apps/tool.png", "png");
        writeText(paths.home + "/Applications/Loose.AppImage", fakeElf(""));
        writeText(paths.home + "/Applications/notes.txt", "not an app");
        writeText(paths.downloads + "/Fresh.AppImage", fakeElf(""));
        Store store(paths, sys.exec());
        QHash<QString, QVariantMap> byName;
        for (const QVariant &v : store.list("appimages").toList())
            byName.insert(v.toMap().value("name").toString(), v.toMap());
        QCOMPARE(QSet<QString>(byName.keyBegin(), byName.keyEnd()), (QSet<QString>{"Tool", "Loose", "Fresh"}));
        QCOMPARE(byName["Tool"].value("icon").toString(), paths.data + "/icons/hicolor/256x256/apps/tool.png");
        QCOMPARE(byName["Tool"].value("version").toString(), QString("1.2"));
        QCOMPARE(byName["Tool"].value("updateKind").toString(), QString("zsync"));
        QVERIFY(byName["Tool"].value("integrated").toBool());
        QCOMPARE(byName["Tool"].value("location").toString(), QString("elsewhere"));
        QVERIFY(!byName["Loose"].value("integrated").toBool());
        QCOMPARE(byName["Loose"].value("location").toString(), QString("applications"));
        QCOMPARE(byName["Fresh"].value("location").toString(), QString("downloads"));
    }

    void anAppImageWithoutANameIsNamedLikeAnApp() {
        QCOMPARE(Store::appImageName("/a/atomic-chat-2.3.5-linux-x86_64.AppImage"), QString("Atomic Chat"));
        QCOMPARE(Store::appImageName("/a/LM-Studio-0.3.5-x86_64.AppImage"), QString("LM Studio"));
        QCOMPARE(Store::appImageName("/a/lichii-linux-x86_64.appimage"), QString("Lichii"));
        QCOMPARE(Store::appImageName("/a/Fresh-v1.0.AppImage"), QString("Fresh"));
        QCOMPARE(Store::appImageName("/a/kdenlive_24.12_amd64.AppImage"), QString("Kdenlive"));
        QCOMPARE(Store::appImageName("/a/x86_64.AppImage"), QString("x86_64"));
    }

    void appImagesSettingsNamedByTheirFileGetTheirAppName() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString ours = paths.home + "/Applications/atomic-chat-2.3.5-linux-x86_64.AppImage";
        const QString theirs = paths.home + "/Applications/other-1.0.AppImage";
        writeText(ours, fakeElf(""));
        writeText(theirs, fakeElf(""));
        const QString ourEntry = paths.data + "/applications/appimage-atomic-chat.desktop";
        writeText(ourEntry, "[Desktop Entry]\nType=Application\nName=atomic-chat-2.3.5-linux-x86_64\nExec=\"" + ours.toUtf8()
                  + "\" %U\nX-Omasettings-AppImage=true\n");
        writeText(paths.data + "/applications/other.desktop", "[Desktop Entry]\nName=other-1.0\nExec=\"" + theirs.toUtf8() + "\"\n");
        writeText(paths.downloads + "/new-tool-2.0-x86_64.AppImage", fakeElf(""));
        Store store(paths, sys.exec());
        QStringList names;
        for (const QVariant &v : store.list("appimages").toList())
            names << v.toMap().value("name").toString();
        names.sort();
        QCOMPARE(names, (QStringList{"Atomic Chat", "New Tool", "other-1.0"}));
        // The launcher shows the same name: the entry Settings wrote is fixed,
        // and someone else's is left alone.
        QVERIFY(readText(ourEntry).contains("\nName=Atomic Chat\n"));
        QVERIFY(readText(paths.data + "/applications/other.desktop").contains("Name=other-1.0\n"));
    }

    void anAddedAppImageMovesToApplicationsAndTheLauncher() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString from = paths.downloads + "/Fresh-1.0.AppImage";
        writeText(from, fakeElf(""));
        writeText(paths.downloads + "/fake.AppImage", "not one");
        Store store(paths, sys.exec());
        QVERIFY(!store.act("appimage.add", {paths.downloads + "/fake.AppImage"}).isEmpty());
        QCOMPARE(store.act("appimage.add", {from}), QString());
        const QString to = paths.home + "/Applications/Fresh-1.0.AppImage";
        QVERIFY(!QFileInfo::exists(from));
        QVERIFY(QFileInfo(to).isExecutable());
        // Nothing could be extracted here, so the name comes from the file's.
        const QString entry = readText(paths.data + "/applications/appimage-fresh-1.0.desktop");
        QVERIFY(entry.contains("Name=Fresh\n"));
        QVERIFY(entry.contains("Exec=\"" + to + "\" %U"));
        QVERIFY(entry.contains("X-Omasettings-AppImage=true"));
        // Adding it again is refused rather than overwriting it.
        writeText(from, fakeElf(""));
        QVERIFY(!store.act("appimage.add", {from}).isEmpty());

        // Removing it trashes it and takes it out of the launcher.
        QCOMPARE(store.act("appimage.remove", {to}), QString());
        QVERIFY(sys.calls.contains("gio trash " + to));
        QVERIFY(!QFileInfo::exists(paths.data + "/applications/appimage-fresh-1.0.desktop"));
        QVERIFY(!store.act("appimage.remove", {dir.path() + "/nowhere.AppImage"}).isEmpty());
    }

    void appImagesArePickedWithoutAnExtensionFilter() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        // One named the usual way, one with no extension at all.
        writeText(paths.downloads + "/Tool-1.0.AppImage", fakeElf(""));
        writeText(paths.downloads + "/Bare", fakeElf(""));
        sys.answer("omarchy-file-select --title Choose AppImages --multiple",
                   (paths.downloads + "/Tool-1.0.AppImage\n" + paths.downloads + "/Bare\n").toUtf8());
        Store store(paths, sys.exec());
        QCOMPARE(store.act("appimage.add", {}), QString());
        QVERIFY(QFileInfo::exists(paths.home + "/Applications/Tool-1.0.AppImage"));
        QVERIFY(QFileInfo::exists(paths.home + "/Applications/Bare"));
        QVERIFY(!sys.calls.filter("omarchy-file-select").first().contains("--extensions"));

        // Whatever else is picked is refused.
        writeText(paths.downloads + "/notes.txt", "hello");
        sys.answer("omarchy-file-select --title Choose AppImages --multiple", (paths.downloads + "/notes.txt\n").toUtf8());
        QVERIFY(store.act("appimage.add", {}).contains("not an AppImage"));
        QVERIFY(QFileInfo::exists(paths.downloads + "/notes.txt"));
    }

    void aDownloadedVersionIsMatchedToTheOneAddedAndReplacesIt() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString tool = paths.home + "/Applications/tool-1.0-x86_64.AppImage";
        writeText(tool, fakeElf("gh-releases-zsync|Me|tool|latest|tool-*.zsync"));
        writeText(paths.data + "/applications/appimage-tool-1.0-x86_64.desktop",
                  "[Desktop Entry]\nName=Tool\nExec=\"" + tool.toUtf8() + "\" %U\nX-AppImage-Version=1.0\nX-Omasettings-AppImage=true\n");
        writeText(paths.home + "/Applications/Other-1.0.AppImage", fakeElf(""));
        // By who publishes it, whatever the file is called.
        writeText(paths.downloads + "/Tool_v1.2_amd64.AppImage", fakeElf("gh-releases-zsync|me|tool|latest|tool-*.zsync"));
        // The same name, but someone else's project: not a version of it.
        writeText(paths.downloads + "/tool-2.0-x86_64.AppImage", fakeElf("gh-releases-zsync|fork|tool|latest|tool-*.zsync"));
        // No update information: by the name without version and architecture.
        writeText(paths.downloads + "/Other-0.9-linux-x86_64.AppImage", fakeElf(""));
        Store store(paths, sys.exec());
        QHash<QString, QVariantMap> byFile;
        for (const QVariant &v : store.list("appimages").toList())
            byFile.insert(QFileInfo(v.toMap().value("path").toString()).fileName(), v.toMap());
        QCOMPARE(byFile["Tool_v1.2_amd64.AppImage"].value("replaces").toString(), tool);
        QCOMPARE(byFile["Tool_v1.2_amd64.AppImage"].value("compare").toString(), QString("newer"));
        QCOMPARE(byFile["Tool_v1.2_amd64.AppImage"].value("replacesVersion").toString(), QString("1.0"));
        QVERIFY(!byFile["tool-2.0-x86_64.AppImage"].contains("replaces"));
        QCOMPARE(byFile["Other-0.9-linux-x86_64.AppImage"].value("replaces").toString(), paths.home + "/Applications/Other-1.0.AppImage");
        QCOMPARE(byFile["Other-0.9-linux-x86_64.AppImage"].value("compare").toString(), QString("older"));

        // Nothing checked where the download came from, and the list says so.
        QCOMPARE(byFile["Tool_v1.2_amd64.AppImage"].value("verified"), QVariant(false));

        QVERIFY(!store.act("appimage.replace", {paths.downloads + "/tool-2.0-x86_64.AppImage", "unverified"}).isEmpty());
        // Replacing needs saying so; without it nothing moves.
        QVERIFY(!store.act("appimage.replace", {paths.downloads + "/Tool_v1.2_amd64.AppImage"}).isEmpty());
        QVERIFY(QFileInfo::exists(paths.downloads + "/Tool_v1.2_amd64.AppImage"));
        QVERIFY(!sys.calls.contains("gio trash " + tool));
        QCOMPARE(store.act("appimage.replace", {paths.downloads + "/Tool_v1.2_amd64.AppImage", "unverified"}), QString());
        // The old one went to the trash; the new one has its name and a launcher entry.
        QVERIFY(sys.calls.contains("gio trash " + tool));
        const QString fresh = paths.home + "/Applications/Tool_v1.2_amd64.AppImage";
        QVERIFY(QFileInfo(fresh).isExecutable());
        QVERIFY(!QFileInfo::exists(paths.downloads + "/Tool_v1.2_amd64.AppImage"));
        QVERIFY(!QFileInfo::exists(paths.data + "/applications/appimage-tool-1.0-x86_64.desktop"));
        QVERIFY(readText(paths.data + "/applications/appimage-tool_v1.2_amd64.desktop").contains("Exec=\"" + fresh + "\" %U"));
    }

    // The name and icon come out of the squashfs after the runtime, read by
    // unsquashfs: the AppImage itself never runs.
    void anAppImageIsReadWithoutRunningIt() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QByteArray runtime = fakeElf("");
        const QString image = paths.home + "/Applications/Tool-1.0.AppImage";
        writeText(image, runtime + "hsqs" + QByteArray(60, '\0'));
        writeText(dir.path() + "/secret.desktop", "[Desktop Entry]\nName=Secret\n");
        writeText(dir.path() + "/outside/icon.png", "not theirs");
        // What unsquashfs would write: a .desktop that is a link out, the real
        // one, and a .DirIcon that is a link to the icon inside.
        sys.onRun = [&](const QString &program, const QStringList &args) {
            if (program != "unsquashfs")
                return;
            const QString root = args.value(args.indexOf("-d") + 1);
            QDir().mkpath(root);
            const QStringList patterns = args.mid(args.indexOf(image) + 1);
            if (patterns.contains("*.desktop")) {
                QFile::link(dir.path() + "/secret.desktop", root + "/a-link.desktop");
                writeText(root + "/tool.desktop", "[Desktop Entry]\nName=Real Tool\nComment=Bad\\nKey=1\n");
                QFile::link("usr/share/icons/tool.png", root + "/.DirIcon");
            } else if (patterns == QStringList{"usr/share/icons/tool.png"}) {
                writeText(root + "/usr/share/icons/tool.png", "png");
            }
        };
        Store store(paths, sys.exec());
        QCOMPARE(store.act("appimage.integrate", {image}), QString());
        QVERIFY(sys.calls.filter("--appimage-extract").isEmpty());
        const QStringList unsquashfs = sys.calls.filter(QRegularExpression("^unsquashfs "));
        QCOMPARE(unsquashfs.size(), 2);
        QVERIFY2(unsquashfs[0].startsWith(QStringLiteral("unsquashfs -no-xattrs -o %1 -d ").arg(runtime.size())), qPrintable(unsquashfs[0]));
        QVERIFY(unsquashfs[0].endsWith(" " + image + " *.desktop .DirIcon"));
        QVERIFY(unsquashfs[1].endsWith(" " + image + " usr/share/icons/tool.png"));
        const QString entry = readText(paths.data + "/applications/appimage-tool-1.0.desktop");
        QVERIFY(entry.contains("\nName=Real Tool\n"));
        QVERIFY(!entry.contains("Secret"));
        // An escaped line break in a value is left out rather than passed on.
        QVERIFY(!entry.contains("Comment="));
        const QString icon = paths.data + "/icons/omasettings-appimages/tool-1.0.png";
        QVERIFY(entry.contains("Icon=" + icon + "\n"));
        QCOMPARE(readText(icon), QString("png"));

        // An icon behind a folder that links out of the AppImage is not taken.
        sys.calls.clear();
        sys.onRun = [&](const QString &program, const QStringList &args) {
            const QString root = args.value(args.indexOf("-d") + 1);
            QDir().mkpath(root);
            const QStringList patterns = args.mid(args.indexOf(image) + 1);
            if (program == "unsquashfs" && patterns.contains(".DirIcon"))
                QFile::link("share/icon.png", root + "/.DirIcon");
            else if (program == "unsquashfs")
                QFile::link(dir.path() + "/outside", root + "/share");
        };
        QFile::remove(icon);
        QCOMPARE(store.act("appimage.integrate", {image}), QString());
        QCOMPARE(sys.calls.filter(QRegularExpression("^unsquashfs ")).size(), 2);
        QVERIFY(readText(paths.data + "/applications/appimage-tool-1.0.desktop").contains("Icon=application-x-executable\n"));
        QVERIFY(!QFileInfo::exists(icon));

        // Without unsquashfs, it says so.
        sys.onRun = nullptr;
        sys.prefixAnswers.insert("unsquashfs", ProcResult{false, -1, {}, {}});
        QVERIFY(store.act("appimage.integrate", {image}).contains("unsquashfs"));
    }

    // A file name can't add lines to the launcher entry, and a % in it is
    // not a field code.
    void anAppImagesNameCannotRewriteItsLauncherEntry() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString bad = paths.home + "/Applications/Tool\nExec=evil\n.AppImage";
        writeText(bad, fakeElf(""));
        Store store(paths, sys.exec());
        QVERIFY(!store.act("appimage.integrate", {bad}).isEmpty());
        QVERIFY(QDir(paths.data + "/applications").entryList({"*.desktop"}, QDir::Files).isEmpty());
        const QString downloaded = paths.downloads + "/Other\r1.0.AppImage";
        writeText(downloaded, fakeElf(""));
        QVERIFY(!store.act("appimage.add", {downloaded}).isEmpty());
        QVERIFY(QFileInfo::exists(downloaded));

        const QString odd = paths.home + "/Applications/100%-tool.AppImage";
        writeText(odd, fakeElf(""));
        QCOMPARE(store.act("appimage.integrate", {odd}), QString());
        const QString entry = readText(paths.data + "/applications/appimage-100--tool.desktop");
        QVERIFY2(entry.contains("\nExec=\"" + paths.home + "/Applications/100%%-tool.AppImage\" %U\n"), qPrintable(entry));
        QVERIFY(entry.contains("\nTryExec=" + odd + "\n"));
        // And it is still recognized as the entry for that file.
        bool integrated = false;
        for (const QVariant &v : store.list("appimages").toList())
            if (v.toMap().value("path") == odd)
                integrated = v.toMap().value("integrated").toBool();
        QVERIFY(integrated);
    }

    void appImageUpdatesAreFoundByTheReleasesChecksum() {
        QTemporaryDir dir;
        FakeSystem sys;
        Paths paths = pathsIn(dir);
        paths.data = dir.path() + "/data";
        const QString image = paths.home + "/Applications/Tool.AppImage";
        const QByteArray bytes = fakeElf("gh-releases-zsync|me|tool|latest|Tool-*-x86_64.AppImage.zsync");
        writeText(image, bytes);
        const QString sha1 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        sys.answer("curl -fsSL --proto =https --proto-redir =https --max-time 20 -H Accept: application/vnd.github+json https://api.github.com/repos/me/tool/releases/latest",
                   "{\"assets\": [{\"name\": \"Tool-2.0-x86_64.AppImage\", \"browser_download_url\": \"https://github.com/me/tool/releases/download/v2/Tool-2.0-x86_64.AppImage\"},"
                   " {\"name\": \"Tool-2.0-x86_64.AppImage.zsync\", \"browser_download_url\": \"https://github.com/me/tool/releases/download/v2/Tool-2.0-x86_64.AppImage.zsync\"}]}");
        const QString zsync = "curl -fsSL --proto =https --proto-redir =https --max-time 20 -r 0-4095 https://github.com/me/tool/releases/download/v2/Tool-2.0-x86_64.AppImage.zsync";
        sys.answer(zsync, "zsync: 0.6.2\nFilename: Tool-2.0-x86_64.AppImage\nURL: Tool-2.0-x86_64.AppImage\nSHA-1: 0123abcd\n\n" "\x01\x02" "binary");
        Store store(paths, sys.exec());
        QStringList command;
        // Without a check yet (from the command line), it checks first.
        QCOMPARE(store.appImageUpdateCommand(image, command), QString());
        command.clear();

        QVariantMap found = store.list("appimageUpdates").toMap().value(image).toMap();
        QCOMPARE(found.value("status").toString(), QString("available"));
        QCOMPARE(found.value("url").toString(), QString("https://github.com/me/tool/releases/download/v2/Tool-2.0-x86_64.AppImage"));
        QCOMPARE(store.appImageUpdateCommand(image, command), QString());
        QCOMPARE(command.first(), QString("omarchy-launch-floating-terminal-with-presentation"));
        QVERIFY(command.last().contains("sha1sum -c"));
        // The download can't be sent to plain http by a redirect either.
        QVERIFY(command.last().contains("curl -fL --proto =https --proto-redir =https "));
        for (const QString &call : sys.calls.filter(QRegularExpression("^curl ")))
            QVERIFY2(call.startsWith("curl -fsSL --proto =https --proto-redir =https "), qPrintable(call));
        QVERIFY(command.last().contains("'0123abcd  " + paths.home + "/Applications/.Tool.AppImage.part'"));

        // The same checksum as the file here: up to date.
        sys.answer(zsync, "SHA-1: " + sha1.toUtf8() + "\nURL: Tool-2.0-x86_64.AppImage\n\n");
        found = store.list("appimageUpdates").toMap().value(image).toMap();
        QCOMPARE(found.value("status").toString(), QString("current"));
        QVERIFY(!store.appImageUpdateCommand(image, command).isEmpty());

        // GitHub out of reach.
        sys.answer("curl -fsSL --proto =https --proto-redir =https --max-time 20 -H Accept: application/vnd.github+json https://api.github.com/repos/me/tool/releases/latest", "", 22);
        QCOMPARE(store.list("appimageUpdates").toMap().value(image).toMap().value("status").toString(), QString("unreachable"));
    }

    void defaultAppsOfferWhatIsInstalled() {
        QTemporaryDir dir;
        FakeSystem sys;
        // Installed is the command Omarchy's default-app scripts look for.
        for (const char *command : {"kitty", "subl", "zeditor", "firefox"}) {
            const QString path = dir.path() + "/bin/" + QString::fromUtf8(command);
            writeText(path, "#!/bin/sh\n");
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", (dir.path() + "/bin").toUtf8());
        Store store(pathsIn(dir), sys.exec());
        const QVariantMap apps = store.list("defaultApps").toMap();
        qputenv("PATH", path);
        QCOMPARE(apps.value("terminal").toStringList(), QStringList{"kitty"});
        QCOMPARE(apps.value("editor").toStringList(), (QStringList{"zed", "sublime_text"}));
        QCOMPARE(apps.value("browser").toStringList(), QStringList{"firefox"});
    }

    void appsAreInstalledAndRemovedOnlyTheMenusWay() {
        QTemporaryDir dir;
        FakeSystem sys;
        writeText(dir.path() + "/omarchy/default/omarchy/omarchy-menu.jsonc",
                  "{\n"
                  "  \"install.browser.zen\": {\"label\":\"Zen\",\"action\":\"omarchy-install-browser zen\",\"disabled\":\"omarchy-pkg-present zen-browser-bin\"},\n"
                  "  \"remove.browser.zen\": {\"label\":\"Zen\",\"action\":\"omarchy-remove-browser zen\"},\n"
                  "  \"setup.default.agent.codex\": {\"label\":\"Codex\",\"action\":\"omarchy-default-agent codex\"},\n"
                  "  \"install.terminal.kitty\": {\"label\":\"Kitty\",\"action\":\"k\",\"disabled\":\"omarchy-pkg-present kitty\"},\n"
                  "  \"install.terminal.foot\": {\"label\":\"Foot\",\"action\":\"f\",\"disabled\":\"omarchy-pkg-present foot\"},\n"
                  "  \"install.browser\": {\"label\":\"Browser\"},\n"
                  "  \"learn.arch\": {\"label\":\"Arch\",\"action\":\"xdg-open https://wiki.archlinux.org\"}\n"
                  "}\n");
        // Kitty is the terminal in use.
        sys.answer("omarchy-default-terminal", "kitty\n");
        Store store(pathsIn(dir), sys.exec());
        QStringList command;
        QCOMPARE(store.softwareCommand("software.run", "install.browser.zen", command), QString());
        QCOMPARE(command, (QStringList{"bash", "-c", "omarchy-install-browser zen"}));
        command.clear();
        QCOMPARE(store.softwareCommand("software.run", "remove.browser.zen", command), QString());
        QCOMPARE(command.last(), QString("omarchy-remove-browser zen"));
        command.clear();
        QCOMPARE(store.softwareCommand("software.run", "setup.default.agent.codex", command), QString());
        QCOMPARE(command.last(), QString("omarchy-default-agent codex"));
        command.clear();
        QCOMPARE(store.softwareCommand("software.drop", "agent.codex", command), QString());
        // An agent mise installed comes out of mise, in a terminal.
        QCOMPARE(command.first(), QString("omarchy-launch-floating-terminal-with-presentation"));
        QVERIFY(command.last().contains("mise uninstall --all \"$agent_tool\""));
        QVERIFY(command.last().contains("set -- 'codex'"));
        // Only install and remove items, and only ones that do something.
        QVERIFY(!store.softwareCommand("software.run", "learn.arch", command).isEmpty());
        QVERIFY(!store.softwareCommand("software.run", "install.browser", command).isEmpty());
        QVERIFY(!store.softwareCommand("software.run", "install.nothing", command).isEmpty());

        command.clear();
        QCOMPARE(store.softwareCommand("software.drop", "terminal.foot", command), QString());
        QCOMPARE(command, (QStringList{"omarchy-launch-floating-terminal-with-presentation", "omarchy-pkg-drop foot"}));
        // Not one the menu can remove itself, and never the terminal in use.
        QVERIFY(!store.softwareCommand("software.drop", "browser.zen", command).isEmpty());
        QVERIFY(!store.softwareCommand("software.drop", "nope", command).isEmpty());
        QVERIFY(store.softwareCommand("software.drop", "terminal.kitty", command).contains("default terminal"));
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

    void translatorTellsAWordsUsesApart() {
        QTemporaryDir dir;
        writeText(dir.path() + "/language", "ar\n");
        writeText(dir.path() + "/ar.json", R"({"Installed": "heading", "Installed|status": "status", "Open": "open"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Installed", nullptr, -1), QString("heading"));
        QCOMPARE(translator.translate("Main", "Installed", "status", -1), QString("status"));
        QCOMPARE(translator.translate("Main", "Open", "verb", -1), QString("open"));
    }

    void shippedCatalogsAreValidJson() {
        for (const QString &file : QDir(QStringLiteral(SRCDIR) + "/../src/i18n").entryList({"*.json"})) {
            QJsonParseError error;
            QJsonDocument::fromJson(readText(QStringLiteral(SRCDIR) + "/../src/i18n/" + file).toUtf8(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(file + ": " + error.errorString()));
        }
    }

    // ---- Catalogs are complete --------------------------------------------------
    // ar.json holds every string Settings shows; every other catalog but
    // en.json (English plural forms only) has exactly its keys, keeps each
    // key's placeholders, and gives counted strings the forms its language
    // uses. tools/i18n.py checks the same and lists what a catalog lacks.

    static QJsonObject catalog(const QString &code) {
        return QJsonDocument::fromJson(readText(QStringLiteral(SRCDIR) + "/../src/i18n/" + code + ".json").toUtf8()).object();
    }

    static QStringList placeholders(const QString &text, bool countSpelledOut) {
        static const QRegularExpression re("%L?\\d|%n");
        QStringList out;
        for (auto it = re.globalMatch(text); it.hasNext();) {
            const QString p = it.next().captured();
            if (!(countSpelledOut && p == "%n"))
                out << p;
        }
        out.sort();
        return out;
    }

    static QString unescapeLiteral(QString s) {
        static const QRegularExpression codepoint("\\\\u\\{([0-9A-Fa-f]+)\\}");
        for (auto m = codepoint.match(s); m.hasMatch(); m = codepoint.match(s))
            s.replace(m.capturedStart(), m.capturedLength(), QString::fromUcs4(std::u32string(1, char32_t(m.captured(1).toUInt(nullptr, 16))).data(), 1));
        return s.replace("\\n", "\n").replace("\\\"", "\"").replace("\\'", "'").replace("\\\\", "\\");
    }

    void everySourceStringIsInTheReferenceCatalog() {
        const QJsonObject reference = catalog("ar");
        // A quoted literal: "((?:[^"\\]|\\.)*)"
        const QString lit = "\"((?:[^\"\\\\]|\\\\.)*)\"";
        const QRegularExpression qsTrCall("\\bqsTr\\(\\s*" + lit + "(?:\\s*,\\s*" + lit + ")?");
        const QRegularExpression qsTranslateCall("\\b(?:qsTranslate|QT_TRANSLATE_NOOP)\\(\\s*" + lit + "\\s*,\\s*" + lit);
        const QRegularExpression trCall("(?<![\\w.])tr\\(\\s*" + lit);
        static const QRegularExpression comment("(?m)^\\s*//.*$");
        QStringList missing;
        QDirIterator files(QStringLiteral(SRCDIR) + "/../src", {"*.qml", "*.cpp", "*.h"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString text = readText(files.next()).remove(comment);
            for (auto it = qsTrCall.globalMatch(text); it.hasNext();) {
                const auto m = it.next();
                const QString key = unescapeLiteral(m.captured(1)) + (m.captured(2).isEmpty() ? "" : "|" + unescapeLiteral(m.captured(2)));
                if (!reference.contains(key)) missing << key;
            }
            for (auto it = qsTranslateCall.globalMatch(text); it.hasNext();)
                if (const QString key = unescapeLiteral(it.next().captured(2)); !reference.contains(key)) missing << key;
            for (auto it = trCall.globalMatch(text); it.hasNext();)
                if (const QString key = unescapeLiteral(it.next().captured(1)); !reference.contains(key)) missing << key;
        }
        // Setting labels are translated by qsTranslate("Setting", label).
        QTemporaryDir dir;
        FakeSystem sys;
        Store store(pathsIn(dir), sys.exec());
        for (const Setting &s : store.settings())
            if (!s.label.isEmpty() && !reference.contains(s.label)) missing << s.label;
        missing.removeDuplicates();
        QVERIFY2(missing.isEmpty(), qPrintable("Not in ar.json: " + missing.join(" | ")));
    }

    void shippedCatalogsAreComplete() {
        const QJsonObject reference = catalog("ar");
        const QStringList referenceKeys = reference.keys();
        for (const QString &file : QDir(QStringLiteral(SRCDIR) + "/../src/i18n").entryList({"*.json"})) {
            const QString code = file.chopped(5);
            if (code == "en")
                continue;
            const QJsonObject cat = catalog(code);
            QStringList problems;
            const QStringList catKeys = cat.keys();
            const QSet<QString> have(catKeys.begin(), catKeys.end()), want(referenceKeys.begin(), referenceKeys.end());
            for (const QString &k : want - have) problems << "missing " + k;
            for (const QString &k : have - want) problems << "extra " + k;

            // The forms this language's plural rule uses, and "other" as the fallback.
            QSet<QString> forms{"other"};
            for (long long n = 0; n <= 200; ++n) forms << Translator::pluralCategory(code, n);
            forms << Translator::pluralCategory(code, 1000000);

            for (const QString &key : referenceKeys) {
                if (!cat.contains(key)) continue;
                const QString source = key.section('|', 0, 0);
                const QJsonValue value = cat.value(key);
                if (reference.value(key).isObject()) {
                    const QJsonObject given = value.toObject();
                    const QStringList givenForms = given.keys();
                    if (!value.isObject() || QSet<QString>(givenForms.begin(), givenForms.end()) != forms) {
                        problems << key + ": wrong plural forms";
                        continue;
                    }
                    for (const QString &form : givenForms) {
                        // zero, one, and two may spell the count out ("a day").
                        const bool spelled = form == "zero" || form == "one" || form == "two";
                        if (given.value(form).toString().trimmed().isEmpty()
                            || placeholders(given.value(form).toString(), spelled) != placeholders(source, spelled))
                            problems << key + "[" + form + "]: empty or placeholders differ";
                    }
                } else if (!value.isString() || value.toString().trimmed().isEmpty()
                           || placeholders(value.toString(), false) != placeholders(source, false)) {
                    problems << key + ": empty or placeholders differ";
                }
            }
            QVERIFY2(problems.isEmpty(), qPrintable(file + ": " + QString::number(problems.size()) + " problems, e.g. "
                                                    + problems.mid(0, 5).join(" | ")));
        }
    }

    void everyCatalogIsBundled() {
        const QString qrc = readText(QStringLiteral(SRCDIR) + "/../src/resources.qrc");
        for (const QString &file : QDir(QStringLiteral(SRCDIR) + "/../src/i18n").entryList({"*.json"}))
            QVERIFY2(qrc.contains("<file alias=\"i18n/" + file + "\">i18n/" + file + "</file>"),
                     qPrintable(file + " is not in resources.qrc"));
    }
};

QTEST_MAIN(OmasettingsTests)
#include "omasettings_tests.moc"
