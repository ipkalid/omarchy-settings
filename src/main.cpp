// omasettings: System Settings for Omarchy. One window that changes what you
// would otherwise change by hand in config files.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <memory>
#include <unistd.h>

#include "backend.h"
#include "cli.h"
#include "sidebuttons.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

// One Settings window per user. A second launch (from the menu, the
// launcher, or `omasettings open vpn`) hands what it was asked to show to
// the window that's open and brings that window forward. OMASETTINGS_INSTANCE
// names a separate one, for running a copy beside the real window.
static QString instanceSocket() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QString name = "omasettings" + (qEnvironmentVariableIsEmpty("OMASETTINGS_INSTANCE")
                                              ? QString() : "-" + qEnvironmentVariable("OMASETTINGS_INSTANCE"));
    return runtime.isEmpty() ? QStringLiteral("%1-%2").arg(name).arg(getuid()) : runtime + "/" + name + ".sock";
}

static bool handToOpenWindow(const QString &target) {
    QLocalSocket socket;
    socket.connectToServer(instanceSocket());
    if (!socket.waitForConnected(500))
        return false;
    socket.write((target + "\n").toUtf8());
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    // Hyprland doesn't let a window raise itself, so this launch does it,
    // by class: so not for a named copy, which the class can't tell apart.
    if (!qEnvironmentVariableIsEmpty("OMASETTINGS_INSTANCE"))
        return true;
    QProcess focus;
    focus.start("hyprctl", {"dispatch", R"(hl.dsp.focus({ window = "class:^(omasettings)$" }))"});
    if (focus.waitForFinished(2000) && focus.readAll().contains("error"))
        QProcess::execute("hyprctl", {"dispatch", "focuswindow", "class:^(omasettings)$"});
    return true;
}

int main(int argc, char *argv[]) {
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    // Commands for scripts and agents need no display at all.
    if (isCliCommand(args)) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("omasettings");
        Store store;
        const int code = runCli(args, store);
        return code < 0 ? 2 : code;
    }

    QGuiApplication app(argc, argv);

    // `omasettings open keyboard` opens on a pane, and
    // `omasettings open general/datetime` on a page inside one.
    const QString target = args.value(1) == "open" ? args.value(2) : QString();
    if (handToOpenWindow(target))
        return 0;

    app.setApplicationName("omasettings");
    app.setApplicationDisplayName("Settings");
    app.setApplicationVersion("1.4.2");

    // Matches the window to omasettings.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("omasettings");
    app.setWindowIcon(QIcon::fromTheme("omasettings"));

    QQuickStyle::setStyle("Material");

    // The Omarchy shell draws everything in the desktop's monospace font, so
    // the window does too, down to text that sets no font of its own.
    app.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes. OMASETTINGS_LANGUAGE_FILE
    // points it at another language file, to check a translation without
    // changing the desktop's language.
    Translator translator(qEnvironmentVariable("OMASETTINGS_LANGUAGE_FILE"));
    app.installTranslator(&translator);

    Theme theme;
    auto store = std::make_shared<Store>();
    // The app icon follows the theme, like Omarchy's own menu entries: draw
    // it now, and make sure theme switches redraw it while we are closed.
    store->writeThemedIcon();
    store->ensureIconHook();
    Backend backend(store);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialPane", target);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().first()))
        new SideButtons(window);

    // Later launches arrive here, one line each: what to show.
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(instanceSocket()); // nothing answered on it, so it's left over
    server.listen(instanceSocket());
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, &backend] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::readyRead, client, [client, &backend] {
                while (client->canReadLine())
                    emit backend.openRequested(QString::fromUtf8(client->readLine()).trimmed());
            });
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });

    const int code = app.exec();
    backend.finish();
    return code;
}
