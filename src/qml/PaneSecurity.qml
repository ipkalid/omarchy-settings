import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "security"
    title: qsTr("Login & Security")
    tagline: qsTr("Passwords and keys")
    glyph: "\u{F0237}"
    description: qsTr("Your password, faster ways to unlock, and keys for signing in to other machines.")

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
        ButtonRow {
            label: qsTr("Fingerprint")
            sublabel: qsTr("Unlock and approve with your finger")
            glyph: "\u{F0237}"
            text: qsTr("Set Up…")
            onClicked: backend.act("launch", ["fingerprint"])
        }
        ButtonRow {
            label: qsTr("Security key")
            sublabel: qsTr("Unlock and approve with a FIDO2 key, like a YubiKey")
            glyph: "\u{F0306}"
            text: qsTr("Set Up…")
            onClicked: backend.act("launch", ["fido2"])
        }
    }

    SettingsGroup {
        title: qsTr("SSH")
        ButtonRow {
            label: qsTr("SSH agent")
            sublabel: qsTr("Remember your SSH key passphrase for the session")
            glyph: "\u{F08C0}"
            text: qsTr("Set Up…")
            onClicked: backend.act("launch", ["sshAgent"])
        }
    }
}
