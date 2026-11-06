// Branding and the boot screen, default tools, security setup, and
// troubleshooting: mostly Omarchy's own setup commands, with the state
// each one leaves behind read back so the window can show it.

#include "store.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
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
