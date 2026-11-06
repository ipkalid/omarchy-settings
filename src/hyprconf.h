#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

// Pure helpers for the one Hyprland file Settings owns,
// ~/.config/hypr/omasettings.lua. It is generated in full from the saved
// choices every time, and Hyprland executes it, so everything written into it
// is validated or escaped here.
namespace Hypr {

// The generated file for a saved state: {"hypr": {"input:repeat_rate": 40},
// "monitors": {"DP-2": {...}}, "gestures": {"workspaceSwipe": true}}.
QString renderLua(const QJsonObject &state);

// A Lua string literal that reads back as exactly `value`.
QString luaString(const QString &value);

// Whether a name is safe to write as an option path or monitor connector.
bool validOption(const QString &option);
bool validMonitorName(const QString &name);
bool validMode(const QString &mode);
bool validPosition(const QString &position);

// The value of `hyprctl -j getoption` output as a QVariant (int, double,
// bool, or string). Gaps come back as "css" with the four sides run together
// ("2222", "10101010"); equal sides read as that one number.
QVariant parseOption(const QByteArray &json);

// Replaces the kb_options entries that start with any of `prefixes` with
// `token` (or drops them when token is empty), keeping every other option.
QString replaceKbOption(const QString &options, const QStringList &prefixes, const QString &token);
// The first kb_options entry starting with one of `prefixes`, or "".
QString findKbOption(const QString &options, const QStringList &prefixes);

}
