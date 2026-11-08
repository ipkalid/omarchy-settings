import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Passwords, ways to unlock, remote login, administrator access, git
// identity, and booting. Setup runs Omarchy's own commands in a terminal,
// as its menu does, and the status of each is read back here.
SettingsPage {
    id: page
    configFiles: ["gitconfig"]
    pane: "security"
    title: qsTr("Login & Security")
    tagline: qsTr("Passwords and keys")
    glyph: "\u{F0237}"
    description: qsTr("Your password, faster ways to unlock, and keys for signing in to other machines.")
    lists: ["security", "hardware"]
    // Setup happens in a terminal; check back for when it is done.
    pollMs: 5000

    readonly property var sec: backend.lists.security || ({})
    readonly property var hw: backend.lists.hardware || ({})
    function status(on) { return on ? qsTr("Set up") : qsTr("Not set up") }

    // A thing that is set up or not, with the command to do each.
    component SetupRow: SettingRow {
        id: srow
        property bool on: false
        property string setup
        property string remove
        sublabel: page.status(on)
        tint: on ? ui.accent : ui.fg
        OmaButton {
            text: srow.on ? qsTr("Remove…") : qsTr("Set Up…")
            bordered: !srow.on
            visible: srow.on ? srow.remove.length > 0 : srow.setup.length > 0
            onClicked: backend.act("launch", [srow.on ? srow.remove : srow.setup], ["security"])
        }
    }

    SettingsGroup {
        title: qsTr("Password")
        footer: qsTr("Your login password unlocks the screen and approves changes to the system. Each of these opens a terminal to walk you through it.")
        ButtonRow {
            label: qsTr("Login password")
            glyph: "\u{F0306}"
            text: qsTr("Change…")
            onClicked: backend.act("launch", ["password"])
        }
        ButtonRow {
            label: qsTr("Disk encryption password")
            sublabel: qsTr("Asked for when the computer starts")
            glyph: "\u{F02CA}"
            text: qsTr("Change…")
            onClicked: backend.act("launch", ["drivePassword"])
        }
    }

    SettingsGroup {
        title: qsTr("Unlocking")
        SetupRow {
            label: qsTr("Fingerprint")
            glyph: "\u{F0237}"
            on: page.sec.fingerprint === true
            setup: "fingerprint"
            remove: "fingerprintRemove"
        }
        SetupRow {
            label: qsTr("Security key")
            glyph: "\u{F0306}"
            on: page.sec.fido2 === true
            setup: "fido2"
            remove: "fido2Remove"
        }
    }

    SettingsGroup {
        title: qsTr("Remote Login")
        footer: qsTr("The SSH server lets you sign in to this computer from another one. Turn it on only on networks you trust.")
        SetupRow {
            label: qsTr("SSH server")
            sublabel: page.sec.sshd ? qsTr("On: other computers can sign in") : qsTr("Off")
            glyph: "\u{F08C0}"
            on: page.sec.sshd === true
            setup: "sshdSetup"
            remove: "sshdRemove"
        }
        SetupRow {
            label: qsTr("SSH agent")
            sublabel: page.hw.sshAgent ? qsTr("On: your SSH key passphrase is remembered for the session")
                                       : qsTr("Remember your SSH key passphrase for the session")
            glyph: "\u{F0306}"
            on: page.hw.sshAgent === true
            setup: "sshAgent"
            remove: "sshAgentRemove"
        }
    }

    SettingsGroup {
        title: qsTr("Administrator")
        footer: qsTr("Both trade some safety for convenience: anything you run can then act as administrator, or reach Docker, without asking.")
        SetupRow {
            label: qsTr("Sudo without a password")
            sublabel: page.sec.passwordlessSudo ? qsTr("On") : qsTr("Off: sudo asks for your password")
            glyph: "\u{F033E}"
            on: page.sec.passwordlessSudo === true
            setup: "sudoPasswordless"
            remove: "sudoPasswordlessOff"
        }
        SetupRow {
            visible: page.sec.docker === true
            label: qsTr("Docker without sudo")
            sublabel: page.sec.dockerSudoless ? qsTr("On: you're in the docker group") : qsTr("Off: docker needs sudo")
            glyph: "\u{F0868}"
            on: page.sec.dockerSudoless === true
            setup: "dockerSetup"
            remove: "dockerRemove"
        }
    }

    SettingsGroup {
        title: qsTr("Git")
        footer: qsTr("The name and email on your commits. Sign in to GitHub to push without typing a password.")
        SettingRow {
            label: qsTr("Name")
            busy: backend.busy.indexOf("security.gitName") >= 0
            OmaField {
                id: gitName
                width: Math.round(260 * ui.scale)
                placeholderText: qsTr("Your Name")
                text: backend.values["security.gitName"] || ""
                onAccepted: if (text.trim() !== backend.values["security.gitName"]) backend.set("security.gitName", text.trim())
                onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => backend.values["security.gitName"] || "")
                Accessible.name: qsTr("Name on your git commits")
            }
        }
        SettingRow {
            label: qsTr("Email")
            busy: backend.busy.indexOf("security.gitEmail") >= 0
            OmaField {
                id: gitEmail
                width: Math.round(260 * ui.scale)
                placeholderText: "you@example.com"
                text: backend.values["security.gitEmail"] || ""
                onAccepted: if (text.trim() !== backend.values["security.gitEmail"]) backend.set("security.gitEmail", text.trim())
                onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => backend.values["security.gitEmail"] || "")
                Accessible.name: qsTr("Email on your git commits")
            }
        }
        ButtonRow {
            label: qsTr("GitHub")
            text: qsTr("Sign In…")
            onClicked: backend.act("launch", ["gitSetup"])
        }
    }

    SettingsGroup {
        title: qsTr("Booting")
        footer: page.sec.directBootSupported === false ? page.sec.directBootProblem
                : qsTr("A direct boot entry starts Omarchy straight from the firmware, skipping the boot menu.")
        SetupRow {
            label: qsTr("Direct boot")
            sublabel: page.sec.directBoot ? qsTr("On") : qsTr("Off")
            glyph: "\u{F0901}"
            dimmed: page.sec.directBootSupported === false
            on: page.sec.directBoot === true
            setup: page.sec.directBootSupported ? "directBoot" : ""
            remove: "directBoot"
        }
    }
}
