// omasettings: System Settings for Omarchy. One window that changes what you
// would otherwise change by hand in config files.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <memory>

#include "backend.h"
#include "cli.h"
#include "sidebuttons.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

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
    app.setApplicationName("omasettings");
    app.setApplicationDisplayName("Settings");
    app.setApplicationVersion("1.0.3");

    // Matches the window to omasettings.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("omasettings");
    app.setWindowIcon(QIcon::fromTheme("omasettings"));

    QQuickStyle::setStyle("Material");

    // The Omarchy shell draws everything in the desktop's monospace font, so
    // the window does too, down to text that sets no font of its own.
    app.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    auto store = std::make_shared<Store>();
    // The app icon follows the theme, like Omarchy's own menu entries: draw
    // it now, and make sure theme switches redraw it while we are closed.
    store->writeThemedIcon();
    store->ensureIconHook();
    Backend backend(store);

    // `omasettings open keyboard` opens on a pane.
    QString pane = args.value(1) == "open" ? args.value(2) : QString();

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("initialPane", pane);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().first()))
        new SideButtons(window);

    const int code = app.exec();
    backend.finish();
    return code;
}
