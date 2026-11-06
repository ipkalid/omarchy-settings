import QtQuick
import Quickshell.Io
import qs.Commons as Commons

// Puts the Settings app on this computer once the plugin is enabled, and
// again whenever the plugin is updated. The build runs in the background
// with this plugin's own install script and installs for the current user
// only (~/.local), so nothing needs a password. If build tools are missing,
// a notification offers to add them in a terminal, where pacman can ask
// for the password.
Item {
  id: root

  property var manifest: null

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  function localPath(name) {
    return decodeURIComponent(Qt.resolvedUrl(name).toString().replace(/^file:\/\//, ""))
  }
  readonly property string installer: localPath("install")
  readonly property string manifestPath: localPath("../manifest.json")

  function check() {
    if (!statusProbe.running && !build.running)
      statusProbe.running = true
  }

  function notify(args) {
    const n = notifier.createObject(root, { command: ["notify-send", "--app-name=Settings", "--icon=omasettings"].concat(args) })
    n.running = true
  }

  Component.onCompleted: check()

  // `omarchy plugin update` replaces the manifest; look again then.
  FileView {
    path: root.manifestPath
    watchChanges: true
    printErrors: false
    onFileChanged: root.check()
  }

  Process {
    id: statusProbe
    command: [root.installer, "--status"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        const status = text.trim()
        if (status === "build") {
          build.running = true
        } else if (status === "needs-tools") {
          toolsPrompt.running = true
        }
      }
    }
  }

  Process {
    id: build
    command: [root.installer, "--quiet"]
    stderr: StdioCollector { id: buildErrors; waitForEnd: true }
    onRunningChanged: if (running) root.notify([strings.t("installing.title"), strings.t("installing.body")])
    onExited: function(exitCode) {
      if (exitCode === 0)
        root.notify([strings.t("ready.title"), strings.t("ready.body")])
      else if (exitCode === 3)
        toolsPrompt.running = true
      else
        root.notify(["--urgency=critical", strings.t("failed.title"),
                     buildErrors.text.trim().split("\n").slice(-3).join("\n")])
    }
  }

  // Adding packages needs a password, so it happens in a terminal, and only
  // when asked.
  Process {
    id: toolsPrompt
    command: ["notify-send", "--app-name=Settings", "--icon=omasettings", "--wait",
              "--action=install=" + strings.t("tools.action"),
              strings.t("tools.title"), strings.t("tools.body")]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        if (text.trim() === "install") {
          terminal.command = ["omarchy-launch-floating-terminal-with-presentation",
                              "'" + root.installer.replace(/'/g, "'\\''") + "'"]
          terminal.running = true
        }
      }
    }
  }

  Process { id: terminal }

  Component {
    id: notifier
    Process {
      onExited: destroy()
    }
  }
}
