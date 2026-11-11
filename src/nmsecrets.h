#pragma once

#include <QString>
#include <functional>

// Saves one secret on a saved NetworkManager connection and returns an error
// message, or nothing when it worked. `setting` and `key` name it the way
// NetworkManager does ("802-11-wireless-security" and "psk"); for the "vpn"
// setting, `key` is an entry of its secrets ("password"). Tests hand the
// store a fake one.
using SecretWriter = std::function<QString(const QString &uuid, const QString &setting, const QString &key,
                                           const QString &value)>;

// The real one: straight to NetworkManager over the system bus. A secret
// given to nmcli is readable by other local accounts, in its arguments while
// it runs or in the history file its editor keeps (~/.cache/nmcli-history);
// a D-Bus message is neither.
QString writeNmSecret(const QString &uuid, const QString &setting, const QString &key, const QString &value);

// Settings 1.4.0 to 1.6.0 set secrets through nmcli's editor, which kept
// them in its history file. Takes them out again and makes the file private.
// Returns whether the file was changed.
bool scrubNmcliHistory(const QString &path);
