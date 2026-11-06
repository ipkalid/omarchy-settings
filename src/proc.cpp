#include "proc.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>

// Messages people see go through the app's translator.
static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

QString ProcResult::failure(const QString &program) const {
    if (!started)
        return tr("%1 is not installed").arg(program);
    const QString message = QString::fromUtf8(err).trimmed();
    const QString output = QString::fromUtf8(out).trimmed();
    const QString detail = !message.isEmpty() ? message : output;
    if (detail.isEmpty())
        return tr("%1 failed (exit %2)").arg(program).arg(code);
    // Long tool output is noise in a settings window; the last line is
    // usually the reason.
    return detail.section('\n', -1).trimmed();
}

ProcResult runProcess(const QString &program, const QStringList &args, int timeoutMs) {
    ProcResult result;
    QProcess process;
    // Omarchy's commands send desktop notifications and translate their output
    // themselves, so they get the session environment unchanged, except that
    // tools print plain English for us to parse.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C.UTF-8");
    env.insert("NO_COLOR", "1");
    // Git (plugin updates) must never stop to ask for a password: there is
    // no terminal to answer it.
    env.insert("GIT_TERMINAL_PROMPT", "0");
    if (!env.contains("GIT_SSH_COMMAND"))
        env.insert("GIT_SSH_COMMAND", "ssh -oBatchMode=yes");
    process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, args);
    if (!process.waitForStarted(2000))
        return result;
    result.started = true;
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.code = -1;
        result.err = QByteArray("timed out");
        return result;
    }
    result.code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    return result;
}
