QT += core gui qml quick quickcontrols2 concurrent testlib
CONFIG += c++17 testcase
TARGET = omasettings_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/store.h ../src/hyprconf.h ../src/proc.h ../src/theme.h ../src/translator.h
SOURCES += omasettings_tests.cpp ../src/backend.cpp ../src/store.cpp ../src/extras.cpp ../src/features.cpp ../src/system.cpp ../src/software.cpp ../src/appimages.cpp ../src/hyprconf.cpp ../src/proc.cpp \
    ../src/theme.cpp ../src/translator.cpp
DEFINES += SRCDIR=\\\"$$PWD\\\"
