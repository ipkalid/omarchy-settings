QT += core gui qml quick quickcontrols2 concurrent network dbus

CONFIG += c++17 release
TARGET = omasettings
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/hyprconf.h \
    src/monitorlayout.h \
    src/nmsecrets.h \
    src/version.h \
    src/proc.h \
    src/sidebuttons.h \
    src/store.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/appimages.cpp \
    src/cli.cpp \
    src/extras.cpp \
    src/features.cpp \
    src/system.cpp \
    src/software.cpp \
    src/sync.cpp \
    src/hyprconf.cpp \
    src/monitorlayout.cpp \
    src/nmsecrets.cpp \
    src/proc.cpp \
    src/store.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
