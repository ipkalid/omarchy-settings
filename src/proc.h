#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

// The result of running one command to completion.
struct ProcResult {
    bool started = false;
    int code = -1;
    QByteArray out;
    QByteArray err;

    bool ok() const { return started && code == 0; }
    QString text() const { return QString::fromUtf8(out).trimmed(); }
    // The most useful line to show when the command failed.
    QString failure(const QString &program) const;
};

// Every command the app runs goes through one of these, so tests can hand the
// store a fake system instead of the real one. `input` is written to the
// command's standard input.
using Exec = std::function<ProcResult(const QString &program, const QStringList &args, int timeoutMs,
                                      const QByteArray &input)>;

// Runs a program without a shell (arguments are never interpreted), waits up
// to timeoutMs, and kills it if it takes longer.
ProcResult runProcess(const QString &program, const QStringList &args, int timeoutMs = 5000,
                      const QByteArray &input = {});
