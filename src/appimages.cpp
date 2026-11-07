// AppImages: the ones in the app launcher, the ones in ~/Applications, and
// the ones lying in Downloads. Settings adds them to the launcher with their
// own name and icon, and updates them from the update information their
// makers embed in them (the .upd_info ELF section), checked against the
// .zsync file published with each release, without needing zsync itself.

#include "store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEndian>

static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

namespace {

QString shellQuote(const QString &s) {
    return "'" + QString(s).replace("'", "'\\''") + "'";
}

// The [Desktop Entry] group of a .desktop file.
QHash<QString, QString> desktopEntry(const QString &path) {
    QHash<QString, QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    bool inEntry = false;
    for (const QString &raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[')) {
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        const int eq = line.indexOf('=');
        if (inEntry && eq > 0 && !line.startsWith('#'))
            out.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return out;
}

// The program an Exec line runs: its first word, quoted or not.
QString execProgram(const QString &exec) {
    if (exec.startsWith('"')) {
        const int end = exec.indexOf('"', 1);
        return end > 0 ? exec.mid(1, end - 1) : QString();
    }
    return exec.section(' ', 0, 0);
}

QString execQuote(const QString &path) {
    QString escaped = path;
    for (const char *c : {"\\", "\"", "`", "$"})
        escaped.replace(QString::fromLatin1(c), "\\" + QString::fromLatin1(c));
    return "\"" + escaped + "\"";
}

// A name for files Settings writes about an AppImage, stable across updates
// since an update keeps the file's name.
QString appImageId(const QString &path) {
    QString base = QFileInfo(path).fileName();
    base.remove(QRegularExpression("\\.appimage$", QRegularExpression::CaseInsensitiveOption));
    base = base.toLower().replace(QRegularExpression("[^a-z0-9._-]+"), "-");
    return base.isEmpty() ? QStringLiteral("app") : base;
}

QString baseName(const QString &path) {
    QString base = QFileInfo(path).fileName();
    return base.remove(QRegularExpression("\\.appimage$", QRegularExpression::CaseInsensitiveOption));
}

// Who publishes an AppImage, by its update information: the GitHub project,
// or the folder its .zsync file is in. Two copies of one app agree on it.
QString publisher(const QString &info) {
    const QStringList parts = info.split('|');
    if (parts.value(0) == "gh-releases-zsync" && parts.size() >= 3)
        return ("github:" + parts[1] + "/" + parts[2]).toLower();
    if (parts.value(0) == "zsync" && parts.size() >= 2) {
        const QString url = parts.mid(1).join('|');
        return "zsync:" + url.left(url.lastIndexOf('/'));
    }
    return {};
}

// A version in a file name: 0.5.1, v2.0.3-beta; a bare number is not one.
const QRegularExpression &versionToken() {
    static const QRegularExpression re(R"(^v?\d+(\.\d+)+([.+~-]?[a-z0-9]+)*$)", QRegularExpression::CaseInsensitiveOption);
    return re;
}

QStringList nameTokens(const QString &path) {
    QString base = baseName(path).toLower();
    base.replace(QRegularExpression("x86[_-]64|amd64|aarch64|arm64|armhf|i[36]86"), " ");
    return base.split(QRegularExpression("[-_ ]+"), Qt::SkipEmptyParts);
}

// A file name without its version and architecture, so that
// artcraft-launcher-0.5.1-linux-x86_64 and -0.15.0-linux-x86_64 agree.
QString fileStem(const QString &path) {
    QStringList kept;
    for (const QString &token : nameTokens(path))
        if (!versionToken().match(token).hasMatch() && !QStringList{"linux", "anylinux", "x64", "appimage"}.contains(token))
            kept << token;
    return kept.join('-');
}

QString fileVersion(const QString &path) {
    for (const QString &token : nameTokens(path))
        if (versionToken().match(token).hasMatch())
            return token.startsWith('v') ? token.mid(1) : token;
    return {};
}

// -1, 0, or 1, comparing numbers as numbers: 0.15.0 is after 0.5.1.
int compareVersions(const QString &a, const QString &b) {
    static const QRegularExpression separators("[.+~-]");
    const QStringList x = a.split(separators), y = b.split(separators);
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const QString p = x.value(i, "0"), q = y.value(i, "0");
        bool pn = false, qn = false;
        const qlonglong pv = p.toLongLong(&pn), qv = q.toLongLong(&qn);
        const int c = pn && qn ? (pv < qv ? -1 : pv > qv ? 1 : 0) : p.compare(q, Qt::CaseInsensitive);
        if (c != 0)
            return c < 0 ? -1 : 1;
    }
    return 0;
}

QString sha1Of(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

bool Store::isAppImage(const QString &path) {
    QFile f(path);
    if (!QFileInfo(path).isFile() || !f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(11);
    return head.size() == 11 && head.startsWith("\x7f" "ELF") && head.mid(8, 2) == "AI"
        && (head[10] == 1 || head[10] == 2);
}

// The .upd_info section, read from the ELF headers without running anything.
QString Store::appImageUpdateInfo(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray ident = f.read(64);
    if (ident.size() < 52 || !ident.startsWith("\x7f" "ELF") || ident[5] != 1) // little-endian only
        return {};
    const bool is64 = ident[4] == 2;
    const uchar *h = reinterpret_cast<const uchar *>(ident.constData());
    const quint64 shoff = is64 ? qFromLittleEndian<quint64>(h + 0x28) : qFromLittleEndian<quint32>(h + 0x20);
    const quint16 shentsize = qFromLittleEndian<quint16>(h + (is64 ? 0x3A : 0x2E));
    const quint16 shnum = qFromLittleEndian<quint16>(h + (is64 ? 0x3C : 0x30));
    const quint16 shstrndx = qFromLittleEndian<quint16>(h + (is64 ? 0x3E : 0x32));
    if (shoff == 0 || shnum == 0 || shnum > 4096 || shstrndx >= shnum || shentsize < (is64 ? 64 : 40))
        return {};
    if (!f.seek(qint64(shoff)))
        return {};
    const QByteArray table = f.read(qint64(shentsize) * shnum);
    if (table.size() < qint64(shentsize) * shnum)
        return {};
    struct Section { quint32 name; quint64 offset, size; };
    auto section = [&](int i) {
        const uchar *s = reinterpret_cast<const uchar *>(table.constData()) + qint64(i) * shentsize;
        return is64 ? Section{qFromLittleEndian<quint32>(s), qFromLittleEndian<quint64>(s + 0x18), qFromLittleEndian<quint64>(s + 0x20)}
                    : Section{qFromLittleEndian<quint32>(s), qFromLittleEndian<quint32>(s + 0x10), qFromLittleEndian<quint32>(s + 0x14)};
    };
    auto readAt = [&](quint64 offset, quint64 size) {
        if (size > 65536 || !f.seek(qint64(offset)))
            return QByteArray();
        return f.read(qint64(size));
    };
    const Section names = section(shstrndx);
    const QByteArray strings = readAt(names.offset, names.size);
    for (int i = 0; i < shnum; ++i) {
        const Section s = section(i);
        if (s.name >= quint32(strings.size()))
            continue;
        if (QByteArray(strings.constData() + s.name) != ".upd_info")
            continue;
        QByteArray data = readAt(s.offset, s.size);
        const int nul = data.indexOf('\0');
        if (nul >= 0)
            data.truncate(nul);
        return QString::fromUtf8(data).trimmed();
    }
    return {};
}

// An icon by name, as the app launcher would find it, or a path as it is.
QString Store::resolveIcon(const QString &icon) const {
    if (icon.isEmpty())
        return {};
    if (icon.startsWith('/'))
        return QFileInfo::exists(icon) ? icon : QString();
    static const QStringList sizes{"scalable", "512x512", "256x256", "192x192", "128x128", "96x96", "64x64", "48x48", "32x32"};
    for (const QString &root : {m_paths.data + "/icons/hicolor", QStringLiteral("/usr/share/icons/hicolor")})
        for (const QString &size : sizes)
            for (const char *ext : {"svg", "png"}) {
                const QString path = QStringLiteral("%1/%2/apps/%3.%4").arg(root, size, icon, ext);
                if (QFileInfo::exists(path))
                    return path;
            }
    for (const QString &dir : {m_paths.data + "/icons", QStringLiteral("/usr/share/pixmaps")})
        for (const char *ext : {"svg", "png"})
            if (QFileInfo::exists(dir + "/" + icon + "." + ext))
                return dir + "/" + icon + "." + ext;
    return {};
}

QString Store::appImagesDir() const { return m_paths.home + "/Applications"; }

QList<QVariantMap> Store::appImageItems() const {
    QList<QVariantMap> items;
    QSet<QString> seen;
    auto kind = [](const QString &info) {
        return info.startsWith("gh-releases-zsync|") ? QStringLiteral("github")
             : info.startsWith("zsync|") ? QStringLiteral("zsync") : QString();
    };
    auto location = [this](const QString &path) {
        return QFileInfo(path).absolutePath() == appImagesDir() ? QStringLiteral("applications") : QStringLiteral("elsewhere");
    };

    // In the app launcher: entries that run an AppImage. An app with a
    // hidden helper entry too (a URL handler) is listed by its shown one.
    const QDir apps(m_paths.data + "/applications");
    QList<QPair<QString, QHash<QString, QString>>> entries;
    for (const QString &name : apps.entryList({"*.desktop"}, QDir::Files, QDir::Name))
        entries.append({apps.filePath(name), desktopEntry(apps.filePath(name))});
    std::stable_sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.second.value("NoDisplay") != "true" && b.second.value("NoDisplay") == "true";
    });
    for (const auto &[file, entry] : entries) {
        const QString program = execProgram(entry.value("Exec"));
        if (program.isEmpty() || seen.contains(program) || !isAppImage(program))
            continue;
        seen.insert(program);
        const QString info = appImageUpdateInfo(program);
        items << QVariantMap{{"path", program}, {"name", entry.value("Name", baseName(program))},
                             {"version", entry.value("X-AppImage-Version", fileVersion(program))}, {"icon", resolveIcon(entry.value("Icon"))},
                             {"publisher", publisher(info)},
                             {"desktop", file}, {"ours", entry.value("X-Omasettings-AppImage") == "true"},
                             {"updateKind", kind(info)}, {"integrated", true}, {"location", location(program)},
                             {"modified", QFileInfo(program).lastModified().toMSecsSinceEpoch()}};
    }

    // Not in the launcher yet: in ~/Applications, and lying in Downloads.
    auto loose = [&](const QString &dir, const QStringList &filter, const QString &where) {
        for (const QFileInfo &fi : QDir(dir).entryInfoList(filter, QDir::Files, QDir::Name)) {
            const QString path = fi.absoluteFilePath();
            if (seen.contains(path) || !isAppImage(path))
                continue;
            seen.insert(path);
            const QString info = appImageUpdateInfo(path);
            items << QVariantMap{{"path", path}, {"name", baseName(path)}, {"version", fileVersion(path)}, {"icon", QString()},
                                 {"publisher", publisher(info)}, {"desktop", QString()}, {"ours", false}, {"updateKind", kind(info)},
                                 {"integrated", false}, {"location", where}, {"modified", fi.lastModified().toMSecsSinceEpoch()}};
        }
    };
    loose(appImagesDir(), {}, "applications");
    loose(m_paths.downloads, {"*.AppImage", "*.appimage"}, "downloads");

    // A download that is another version of an app already added: the same
    // publisher when both say who that is, or else the same file name once
    // the version and architecture are taken out.
    for (QVariantMap &download : items) {
        if (download.value("location") != "downloads")
            continue;
        const QString dp = download.value("publisher").toString(), ds = fileStem(download.value("path").toString());
        for (const QVariantMap &added : items) {
            if (added.value("location") == "downloads")
                continue;
            const QString ap = added.value("publisher").toString();
            const bool match = !dp.isEmpty() && !ap.isEmpty() ? dp == ap
                             : !ds.isEmpty() && ds == fileStem(added.value("path").toString());
            if (!match)
                continue;
            const QString from = added.value("version").toString(), to = download.value("version").toString();
            download["replaces"] = added.value("path");
            download["replacesName"] = added.value("name");
            download["replacesVersion"] = from;
            download["compare"] = from.isEmpty() || to.isEmpty() ? QStringLiteral("unknown")
                                : QStringList{"older", "same", "newer"}.value(compareVersions(to, from) + 1);
            break;
        }
    }
    return items;
}

QVariant Store::listAppImages() {
    QVariantList out;
    for (const QVariantMap &item : appImageItems())
        out << item;
    return out;
}

// Whether an update is out, by the SHA-1 the release's .zsync file gives for
// the new file. Only the start of the .zsync is fetched: its header.
QVariant Store::listAppImageUpdates() {
    QVariantMap out;
    QHash<QString, QVariantMap> found;
    auto fetch = [this](const QStringList &args) { return run("curl", QStringList{"-fsSL", "--max-time", "20"} + args, 25000); };
    for (const QVariantMap &item : appImageItems()) {
        const QString path = item.value("path").toString();
        const QStringList info = appImageUpdateInfo(path).split('|');
        QString zsyncUrl;
        if (info.value(0) == "gh-releases-zsync" && info.size() >= 5) {
            const QString repo = info[1] + "/" + info[2], tag = info[3];
            const QString api = "https://api.github.com/repos/" + repo
                + (tag == "latest" ? QStringLiteral("/releases/latest")
                   : tag == "latest-pre" || tag == "latest-all" ? QStringLiteral("/releases")
                   : "/releases/tags/" + tag);
            const ProcResult r = fetch({"-H", "Accept: application/vnd.github+json", api});
            if (!r.ok()) {
                out[path] = QVariantMap{{"status", "unreachable"}};
                continue;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(r.out);
            QJsonObject release = doc.object();
            if (doc.isArray())
                for (const QJsonValue &v : doc.array())
                    if (tag == "latest-all" || v.toObject().value("prerelease").toBool()) {
                        release = v.toObject();
                        break;
                    }
            const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(info[4]));
            for (const QJsonValue &asset : release.value("assets").toArray())
                if (pattern.match(asset.toObject().value("name").toString()).hasMatch()) {
                    zsyncUrl = asset.toObject().value("browser_download_url").toString();
                    break;
                }
        } else if (info.value(0) == "zsync" && info.size() >= 2) {
            zsyncUrl = info.mid(1).join('|');
        } else {
            continue;
        }
        if (!zsyncUrl.startsWith("https://")) {
            out[path] = QVariantMap{{"status", "unreachable"}};
            continue;
        }
        const ProcResult head = fetch({"-r", "0-4095", zsyncUrl});
        QString sha1, url, file;
        for (const QString &line : QString::fromUtf8(head.out).split('\n')) {
            if (line.trimmed().isEmpty())
                break;
            const QString key = line.section(':', 0, 0).trimmed(), value = line.section(':', 1).trimmed();
            if (key == "SHA-1") sha1 = value.toLower();
            else if (key == "URL") url = QUrl(zsyncUrl).resolved(QUrl(value)).toString();
            else if (key == "Filename") file = value;
        }
        if (!head.ok() || sha1.isEmpty() || !url.startsWith("https://")) {
            out[path] = QVariantMap{{"status", "unreachable"}};
            continue;
        }
        // The file's time, so a result can be told apart from a file updated since.
        const QVariantMap result{{"status", sha1 == sha1Of(path) ? "current" : "available"},
                                 {"file", file}, {"url", url}, {"sha1", sha1}, {"modified", item.value("modified")}};
        out[path] = result;
        found.insert(path, result);
    }
    {
        QMutexLocker lock(&m_appImageLock);
        m_appImageUpdates = found;
    }
    out["checkedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return out;
}

// Adds an AppImage to the app launcher, with the name and icon it carries.
// Reading them means running the AppImage's own extractor, as the app
// launcher's AppImage tools do: the same trust as opening the app.
QString Store::integrateAppImage(const QString &path) {
    if (!isAppImage(path))
        return tr("%1 is not an AppImage").arg(path);
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner | QFile::ExeUser);
    const QString applications = m_paths.data + "/applications";
    const QString id = appImageId(path);
    const QString ownEntry = applications + "/appimage-" + id + ".desktop";
    // Someone else's entry for it (AppManager's, an installer's) stays theirs.
    for (const QString &name : QDir(applications).entryList({"*.desktop"}, QDir::Files)) {
        const QString file = applications + "/" + name;
        const auto entry = desktopEntry(file);
        if (file != ownEntry && execProgram(entry.value("Exec")) == path && entry.value("X-Omasettings-AppImage") != "true")
            return {};
    }

    QTemporaryDir tmp;
    const QString root = tmp.path() + "/squashfs-root";
    auto extract = [&](const QString &pattern) {
        run("bash", {"-c", "cd -- \"$1\" && \"$2\" --appimage-extract \"$3\" >/dev/null 2>&1", "omasettings",
                     tmp.path(), path, pattern}, 60000);
    };
    QHash<QString, QString> embedded;
    if (tmp.isValid()) {
        extract("*.desktop");
        const QStringList found = QDir(root).entryList({"*.desktop"}, QDir::Files);
        if (!found.isEmpty())
            embedded = desktopEntry(root + "/" + found.first());
    }

    // The icon: .DirIcon, which is often a link to the real one inside.
    QString icon;
    if (tmp.isValid()) {
        extract(".DirIcon");
        QFileInfo dirIcon(root + "/.DirIcon");
        // Only links that stay inside the AppImage are followed.
        for (int hops = 0; hops < 4 && dirIcon.isSymLink(); ++hops) {
            const QString target = QDir::cleanPath(dirIcon.symLinkTarget());
            if (!target.startsWith(root + "/"))
                break;
            extract(target.mid(root.size() + 1));
            dirIcon = QFileInfo(target);
        }
        if (!dirIcon.isSymLink() && dirIcon.isFile() && dirIcon.size() > 0 && dirIcon.size() < 8 * 1024 * 1024) {
            QFile f(dirIcon.absoluteFilePath());
            const bool svg = f.open(QIODevice::ReadOnly) && f.read(512).contains("<svg");
            const QString iconDir = m_paths.data + "/icons/omasettings-appimages";
            QDir().mkpath(iconDir);
            for (const char *ext : {"png", "svg"})
                QFile::remove(iconDir + "/" + id + "." + ext);
            icon = iconDir + "/" + id + (svg ? ".svg" : ".png");
            if (!QFile::copy(dirIcon.absoluteFilePath(), icon))
                icon.clear();
        }
    }

    QStringList lines{"[Desktop Entry]", "Type=Application",
                      "Name=" + embedded.value("Name", baseName(path)),
                      "Exec=" + execQuote(path) + " %U", "TryExec=" + path,
                      "Icon=" + (icon.isEmpty() ? QStringLiteral("application-x-executable") : icon), "Terminal=false"};
    for (const char *key : {"GenericName", "Comment", "Categories", "Keywords", "StartupWMClass", "X-AppImage-Version"})
        if (!embedded.value(key).isEmpty())
            lines << QString::fromLatin1(key) + "=" + embedded.value(key);
    lines << "X-Omasettings-AppImage=true";
    QDir().mkpath(applications);
    QFile out(ownEntry);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return tr("Could not write %1").arg(ownEntry);
    out.write((lines.join('\n') + "\n").toUtf8());
    return {};
}

QString Store::appImageUpdateCommand(const QString &path, QStringList &command) {
    auto found = [this, &path] {
        QMutexLocker lock(&m_appImageLock);
        return m_appImageUpdates.contains(path) ? m_appImageUpdates.value(path) : QVariantMap();
    };
    QVariantMap update = found();
    // Not checked in this run (from the command line): check now.
    if (update.isEmpty()) {
        listAppImageUpdates();
        update = found();
    }
    if (update.value("status") != "available")
        return tr("No update for %1 was found; check for updates first").arg(QFileInfo(path).fileName());
    const QString part = QFileInfo(path).absolutePath() + "/." + QFileInfo(path).fileName() + ".part";
    const QString self = QCoreApplication::applicationFilePath();
    // The new file replaces the old one only once it's whole and matches
    // the published checksum; a running copy keeps the old one.
    const QString script = QStringLiteral(
        "( set -e; echo %1; curl -fL --progress-bar -o %2 %3; echo %4 | sha1sum -c --quiet; "
        "chmod +x %2; mv -f %2 %5; %6 act appimage.integrate %5 || true; echo %7 ) || { rm -f %2; false; }")
        .arg(shellQuote(tr("Downloading %1").arg(update.value("file").toString())), shellQuote(part),
             shellQuote(update.value("url").toString()), shellQuote(update.value("sha1").toString() + "  " + part),
             shellQuote(path), shellQuote(self), shellQuote(tr("Updated.")));
    command = {"omarchy-launch-floating-terminal-with-presentation", script};
    return {};
}

// Puts a downloaded version in place of the one already added. The old file
// goes to the trash only once the new one is beside it, ready to take its
// place. One Settings added takes the new file's name and a fresh launcher
// entry; one another tool added keeps its path, so that tool's entry still works.
QString Store::replaceAppImage(const QString &download) {
    const QList<QVariantMap> items = appImageItems();
    QVariantMap fresh, old;
    for (const QVariantMap &item : items)
        if (item.value("path") == download && item.value("location") == "downloads")
            fresh = item;
    const QString oldPath = fresh.value("replaces").toString();
    for (const QVariantMap &item : items)
        if (!oldPath.isEmpty() && item.value("path") == oldPath)
            old = item;
    if (old.isEmpty())
        return tr("%1 isn't another version of an AppImage you've added").arg(QFileInfo(download).fileName());

    const bool ours = old.value("ours").toBool() || !old.value("integrated").toBool();
    const QString dir = QFileInfo(oldPath).absolutePath();
    const QString target = ours ? dir + "/" + QFileInfo(download).fileName() : oldPath;
    if (target != oldPath && QFileInfo::exists(target))
        return tr("%1 is already in ~/Applications").arg(QFileInfo(target).fileName());
    const QString staged = dir + "/." + QFileInfo(download).fileName() + ".new";
    QFile::remove(staged);
    if (!QFile::rename(download, staged)) {
        if (!QFile::copy(download, staged))
            return tr("Could not move %1 to ~/Applications").arg(QFileInfo(download).fileName());
        QFile::remove(download);
    }
    QFile::setPermissions(staged, QFile::permissions(staged) | QFile::ExeOwner | QFile::ExeUser);
    if (const QString error = runOk("gio", {"trash", oldPath}, 15000); !error.isEmpty()) {
        // The old one stays; the download goes back where it was.
        if (!QFile::rename(staged, download))
            QFile::remove(staged);
        return error;
    }
    if (!QFile::rename(staged, target))
        return tr("Could not move %1 to ~/Applications").arg(QFileInfo(download).fileName());
    if (!old.value("ours").toBool())
        return {};
    // Settings' own entry for the old file makes way for the new one's.
    if (target != oldPath) {
        QFile::remove(old.value("desktop").toString());
        for (const char *ext : {"png", "svg"})
            QFile::remove(m_paths.data + "/icons/omasettings-appimages/" + appImageId(oldPath) + "." + ext);
    }
    return integrateAppImage(target);
}

QString Store::actAppImage(const QString &action, const QVariantList &args) {
    const QString path = args.value(0).toString();
    auto listed = [this](const QString &p) {
        for (const QVariantMap &item : appImageItems())
            if (item.value("path") == p)
                return true;
        return false;
    };

    if (action == "appimage.add") {
        QStringList paths{path};
        if (path.isEmpty()) {
            const ProcResult picked = run("omarchy-file-select", {"--title", tr("Choose an AppImage"), "--extensions", "AppImage appimage"}, 600000);
            if (!picked.ok())
                return {};
            paths = picked.text().split('\n', Qt::SkipEmptyParts);
        }
        for (const QString &from : paths) {
            if (!isAppImage(from))
                return tr("%1 is not an AppImage").arg(QFileInfo(from).fileName());
            QString to = from;
            if (QFileInfo(from).absolutePath() != appImagesDir()) {
                to = appImagesDir() + "/" + QFileInfo(from).fileName();
                if (QFileInfo::exists(to))
                    return tr("%1 is already in ~/Applications").arg(QFileInfo(from).fileName());
                QDir().mkpath(appImagesDir());
                // A move, or a copy and delete when Downloads is on another disk.
                if (!QFile::rename(from, to)) {
                    if (!QFile::copy(from, to))
                        return tr("Could not move %1 to ~/Applications").arg(QFileInfo(from).fileName());
                    QFile::remove(from);
                }
            }
            if (const QString error = integrateAppImage(to); !error.isEmpty())
                return error;
        }
        return {};
    }
    if (action == "appimage.integrate")
        return integrateAppImage(path);
    if (action == "appimage.replace")
        return replaceAppImage(path);
    if (!listed(path))
        return tr("Unknown AppImage %1").arg(path);
    if (action == "appimage.remove") {
        if (const QString error = runOk("gio", {"trash", path}, 15000); !error.isEmpty())
            return error;
        const QString applications = m_paths.data + "/applications";
        for (const QString &name : QDir(applications).entryList({"*.desktop"}, QDir::Files))
            if (execProgram(desktopEntry(applications + "/" + name).value("Exec")) == path)
                QFile::remove(applications + "/" + name);
        for (const char *ext : {"png", "svg"})
            QFile::remove(m_paths.data + "/icons/omasettings-appimages/" + appImageId(path) + "." + ext);
        return {};
    }
    if (action == "appimage.update") {
        QStringList command;
        if (const QString error = appImageUpdateCommand(path, command); !error.isEmpty())
            return error;
        return launch(command.first(), command.mid(1));
    }
    return tr("Unknown action %1").arg(action);
}
