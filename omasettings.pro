QT += core gui qml quick quickcontrols2 concurrent

CONFIG += c++17 release
TARGET = omasettings
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/hyprconf.h \
    src/proc.h \
    src/store.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/hyprconf.cpp \
    src/proc.cpp \
    src/store.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
