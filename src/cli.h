#pragma once

#include <QStringList>

class Store;

// Commands that need no display, so agents and scripts can read and change
// settings: omasettings list|get|set|data|skill. Returns the exit code, or -1
// when the arguments ask for the window instead.
int runCli(const QStringList &args, Store &store);

// Whether these arguments are a CLI command rather than "open the window".
bool isCliCommand(const QStringList &args);
