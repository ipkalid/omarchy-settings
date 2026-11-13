// Sync: this computer's settings, apps, and the files that make the desktop
// yours, kept in a private git repository (GitHub, through the gh login), so
// another computer can be brought to match it with one click. Hardware stays
// behind: displays, sound devices, networks, power, and the like.
//
// The repository holds a snapshot as plain files, so its history on GitHub
// reads like a changelog:
//
//   sync.json            who uploaded it, when, from which Omarchy
//   settings.json        the same document Export writes, without its date
//   desktop/             the Omarchy menu file, the dock and bar, file types, branding
//   hypr/                ~/.config/hypr: keybindings, input, look and feel, ... not monitors
//   apps/                Omarchy's apps, pacman, AUR, Flatpak, mise, web apps, AppImages
//   themes.json          themes added by git URL
//   wallpapers/          wallpapers added to themes
//   plugins.json         shell plugins by git URL, and whether they're on
//   git.json             the name and email on git commits
//   dotfiles/            files and folders the person chose, from their home
//
// Uploading overwrites the snapshot with this computer's, so it never runs
// while the repository has something newer this computer hasn't applied.
// Applying only adds: missing apps are installed, nothing is removed.

#include "store.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

static QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

namespace {

QString readText(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QByteArray readBytes(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString saveBytes(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return tr("Could not write %1").arg(path);
    return {};
}

// Whether a path in the snapshot is a plain file that is really inside it:
// not a link, and not reached through a linked folder. git checks out links
// the repository holds (Settings asks it not to, see Store::git), and one
// would let the repository read any file of this account.
bool insideSnapshot(const QString &root, const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink())
        return false;
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    return !canonicalRoot.isEmpty() && info.canonicalFilePath().startsWith(canonicalRoot + "/");
}

// Takes out any link in the local copy. A link the repository holds would
// make Sync read or write, or empty, whatever it points at; checked out as
// the plain file core.symlinks=false makes of it, it's harmless, and copies
// made by an earlier version of Settings are cleaned the same way.
void dropLinks(const QString &root) {
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList links;
    while (it.hasNext()) {
        const QString path = it.next();
        if (it.fileInfo().isSymLink())
            links << path;
    }
    for (const QString &link : links)
        QFile::remove(link);
}

// Puts a file from the snapshot in place. It can be a key: a file that
// isn't here yet is made for this account only before anything is written
// to it, since it would otherwise take the umask, often readable by
// everyone. One that is here keeps the permissions it has.
QString restoreFile(const QString &path, const QString &from, const QString &root) {
    if (!insideSnapshot(root, from))
        return tr("%1 isn't a file in the repository").arg(QFileInfo(from).fileName());
    const QByteArray data = readBytes(from);
    const bool isNew = !QFileInfo::exists(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return tr("Could not write %1").arg(path);
    if (isNew) {
        QFileDevice::Permissions mode = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
        if (QFileInfo(from).isExecutable())
            mode |= QFileDevice::ExeOwner;
        if (!file.setPermissions(mode))
            return tr("Could not write %1").arg(path);
    }
    if (file.write(data) != data.size() || !file.commit())
        return tr("Could not write %1").arg(path);
    return {};
}

QString saveJson(const QString &path, const QJsonValue &value) {
    const QJsonDocument doc = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return saveBytes(path, doc.toJson(QJsonDocument::Indented));
}

QJsonValue loadJson(const QString &path) {
    const QJsonDocument doc = QJsonDocument::fromJson(readBytes(path));
    return doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
}

QString saveLines(const QString &path, QStringList lines) {
    lines.removeDuplicates();
    lines.sort();
    return saveBytes(path, lines.isEmpty() ? QByteArray() : (lines.join('\n') + "\n").toUtf8());
}

QStringList loadLines(const QString &path) {
    QStringList out;
    for (const QString &line : readText(path).split('\n')) {
        const QString t = line.section('#', 0, 0).trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

QString shellQuote(const QString &s) {
    return "'" + QString(s).replace("'", "'\\''") + "'";
}

QString shellWords(const QStringList &words) {
    QStringList out;
    for (const QString &w : words)
        out << shellQuote(w);
    return out.join(' ');
}

// Files larger than this stay out of the repository: it's for settings,
// not for media or caches.
constexpr qint64 maxFileSize = 20 * 1024 * 1024;

// Copies a file or a folder's files, leaving out git metadata, sockets, and
// anything too large. Returns how many files were copied.
int copyTree(const QString &from, const QString &to) {
    const QFileInfo info(from);
    if (info.isFile()) {
        if (info.size() > maxFileSize)
            return 0;
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::remove(to);
        return QFile::copy(from, to) ? 1 : 0;
    }
    if (!info.isDir())
        return 0;
    int copied = 0;
    QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(from).relativeFilePath(path);
        if (rel.startsWith(".git/") || rel.contains("/.git/") || !it.fileInfo().isFile())
            continue;
        copied += copyTree(path, to + "/" + rel);
    }
    return copied;
}

// Every file under a folder, relative to it.
QStringList filesUnder(const QString &root) {
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!it.fileInfo().isSymLink())
            out << QDir(root).relativeFilePath(path);
    }
    out.sort();
    return out;
}

bool sameFile(const QString &a, const QString &b) {
    const QFileInfo fa(a), fb(b);
    return fa.isFile() && fb.isFile() && fa.size() == fb.size() && readBytes(a) == readBytes(b);
}

// A copy of a file about to be overwritten, the way Settings backs up the
// menu file: once a day, beside it.
void backUp(const QString &path) {
    if (!QFileInfo(path).isFile())
        return;
    const QString copy = path + ".bak.omasettings-" + QDate::currentDate().toString("yyyy-MM-dd");
    if (!QFileInfo::exists(copy))
        QFile::copy(path, copy);
}

// What the snapshot holds, by the part of it each machine can leave out.
const QList<QPair<QString, QStringList>> &categories() {
    static const QList<QPair<QString, QStringList>> list{
        {"settings", {"settings.json"}},
        {"desktop", {"desktop"}},
        {"hypr", {"hypr"}},
        {"apps", {"apps"}},
        {"themes", {"themes.json", "wallpapers"}},
        {"plugins", {"plugins.json"}},
        {"git", {"git.json"}},
        {"dotfiles", {"dotfiles", "dotfiles.json"}},
    };
    return list;
}

// Files in ~/.config/hypr that stay behind: the displays, what Settings
// writes from settings that sync on their own (omasettings.lua, Night
// Shift's hyprsunset.conf), backups, and lock files.
bool hyprLocal(const QString &rel) {
    static const QRegularExpression skip(R"((^|/)(monitors\.lua|omasettings\.lua|hyprsunset\.conf|\.lock)$|\.bak($|[.-])|\.omasettings\.bak$|~$)");
    return skip.match(rel).hasMatch();
}

// ~/.config/hypr's files that sync, relative to it.
QStringList hyprFiles(const QString &root) {
    QStringList out;
    for (const QString &rel : filesUnder(root))
        if (!hyprLocal(rel) && QFileInfo(root + "/" + rel).size() <= maxFileSize)
            out << rel;
    return out;
}

// The parts of shell.json that are taste rather than hardware.
const QList<QPair<QString, QStringList>> &shellKeys() {
    static const QList<QPair<QString, QStringList>> keys{
        {"bar", {"layout", "position", "background"}},
        {"dock", {"pinned", "position", "autohide"}},
    };
    return keys;
}

QJsonObject shellSubset(const QJsonObject &shell) {
    QJsonObject out;
    for (const auto &[group, keys] : shellKeys()) {
        const QJsonObject from = shell.value(group).toObject();
        QJsonObject part;
        for (const QString &k : keys)
            if (from.contains(k))
                part.insert(k, from.value(k));
        if (!part.isEmpty())
            out.insert(group, part);
    }
    return out;
}

// Packages that come with the machine rather than the person: the base
// system, kernels, firmware, microcode, the boot loader, graphics drivers
// and GPU compute, laptop power and graphics switching, and Omarchy itself.
const QRegularExpression &systemPackage() {
    static const QRegularExpression re(
        R"(^(base|base-devel|linux|linux-.+|.+-firmware|.+-firmware-.+|.+-ucode|limine.*|grub|efibootmgr|mkinitcpio.*|sudo|)"
        R"(pipewire.*|wireplumber|omarchy.*|omasettings.*|nvidia.*|lib32-nvidia.*|mesa.*|lib32-mesa.*|vulkan-.+|)"
        R"(lib32-vulkan-.+|xf86-.+|intel-media-driver|libva-.+|broadcom-wl.*|.+-dkms|.+-headers|)"
        R"(rocm.*|hip-runtime.*|cuda.*|opencl-.+|amdgpu.*|supergfxctl|asusctl|tlp.*|thermald|nvidia-.+)$)");
    return re;
}

// A tool line from mise's [tools] table: `node = "22"`, `"npm:x" = "latest"`.
QPair<QString, QString> miseLine(const QString &line) {
    static const QRegularExpression re(R"re(^\s*("([^"]+)"|'([^']+)'|([A-Za-z0-9_.:@/+-]+))\s*=\s*(.+?)\s*$)re");
    const auto m = re.match(line);
    if (!m.hasMatch())
        return {};
    const QString name = !m.captured(2).isEmpty() ? m.captured(2) : !m.captured(3).isEmpty() ? m.captured(3) : m.captured(4);
    QString value = m.captured(5);
    static const QRegularExpression quoted(R"re(^["']([^"']*)["']$)re");
    const auto q = quoted.match(value);
    value = q.hasMatch() ? q.captured(1) : QStringLiteral("latest");
    return {name, value.isEmpty() ? QStringLiteral("latest") : value};
}

QStringList packageNames(const QString &text) {
    QStringList out;
    for (const QString &line : text.split('\n')) {
        const QString t = line.section('#', 0, 0).trimmed();
        if (!t.isEmpty())
            out << t.section(' ', 0, 0);
    }
    return out;
}

QString hostName() {
    const QString host = QSysInfo::machineHostName();
    return host.isEmpty() ? QStringLiteral("this computer") : host;
}

} // namespace

// --- Pieces tests check --------------------------------------------------------------

QStringList Store::syncPackages(const QStringList &explicitPackages, const QStringList &excluded) {
    const QSet<QString> skip(excluded.begin(), excluded.end());
    QStringList out;
    for (const QString &p : explicitPackages) {
        const QString name = p.trimmed();
        if (name.isEmpty() || skip.contains(name) || systemPackage().match(name).hasMatch())
            continue;
        out << name;
    }
    out.removeDuplicates();
    out.sort();
    return out;
}

QStringList Store::miseTools(const QString &toml) {
    QStringList out;
    bool inTools = false;
    for (const QString &raw : toml.split('\n')) {
        const QString line = raw.section('#', 0, 0).trimmed();
        if (line.startsWith('[')) {
            inTools = line == "[tools]";
            continue;
        }
        if (!inTools || line.isEmpty())
            continue;
        const auto [name, version] = miseLine(line);
        if (!name.isEmpty())
            out << name + "@" + version;
    }
    return out;
}

// --- Where it lives ------------------------------------------------------------------

QString Store::syncDir() const { return m_paths.data + "/omasettings/sync"; }
QString Store::syncStatePath() const { return m_paths.state + "/omasettings/sync.json"; }

// The local copy holds the files Sync carries, which can include keys, so
// only this account may enter it. git checks files out with the umask, often
// readable by everyone, so the folder around them is what keeps them private.
void Store::makeSyncDirPrivate() const {
    if (QFileInfo(syncDir()).isDir())
        QFile::setPermissions(syncDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

// Why a path, relative to the home folder, can't sync, or nothing when it
// can. Every path is checked here, wherever it comes from: picked on this
// computer, or listed by the repository, which may have been written by
// someone else. Keys and passwords never go to a repository, in a folder
// of their own or inside one that holds them.
QString Store::dotfileError(const QString &rel) const {
    static const QStringList secrets{".ssh", ".gnupg", ".password-store", ".local/share/keyrings", ".pki", ".aws", ".kube",
                                     ".docker", ".config/gh", ".git-credentials", ".netrc"};
    const QString clean = QDir::cleanPath(rel);
    if (rel.isEmpty() || clean == "." || clean == ".." || clean.startsWith("../") || QDir::isAbsolutePath(rel)
        || rel.split('/').contains("..") || clean != rel)
        return tr("Choose something inside your home folder");
    auto within = [](const QString &path, const QString &folder) { return path == folder || path.startsWith(folder + "/"); };
    for (const QString &secret : secrets)
        if (within(clean, secret) || within(secret, clean))
            return tr("%1 holds keys or passwords; it stays on this computer").arg(clean);
    const QString absolute = m_paths.home + "/" + clean;
    for (const QString &own : {m_paths.data + "/omasettings", m_paths.state + "/omasettings", m_paths.home + "/.cache"})
        if (within(absolute, own) || within(own, absolute))
            return tr("%1 can't be synced").arg(clean);
    return {};
}

QJsonObject Store::syncState() const {
    return QJsonDocument::fromJson(readBytes(syncStatePath())).object();
}

QString Store::updateSyncState(const std::function<void(QJsonObject &)> &change) {
    QMutexLocker lock(&m_syncStateLock);
    QJsonObject state = syncState();
    change(state);
    return saveJson(syncStatePath(), state);
}

bool Store::syncAuto() const {
    const QJsonObject state = syncState();
    return !state.value("url").toString().isEmpty() && state.value("auto").toBool(true);
}

QStringList Store::syncSkipped() const {
    QStringList out;
    for (const QJsonValue &v : syncState().value("skip").toArray())
        out << v.toString();
    return out;
}

// git options that sign in to GitHub with the gh login, and nowhere else.
// Helpers from the user's own git config are cleared first, and gh answers
// only for https://github.com: given another host, gh would hand over a
// GitHub Enterprise token from the environment (GH_ENTERPRISE_TOKEN), so a
// non-GitHub server picked for Sync would receive it.
static QStringList ghCredentialArgs() {
    const QString gh = QStandardPaths::findExecutable("gh");
    if (gh.isEmpty())
        return {};
    return {"-c", "credential.helper=", "-c", "credential.https://github.com.helper=!" + shellQuote(gh) + " auth git-credential"};
}

// git in the local copy. Commands that reach the server sign in with the gh
// login, since nothing here can answer a password prompt.
ProcResult Store::git(const QStringList &args, int timeoutMs, bool network) const {
    // Links in the repository arrive as plain files holding the link's text.
    QStringList full{"-C", syncDir(), "-c", "core.symlinks=false"};
    if (network)
        full << ghCredentialArgs();
    return run("git", full + args, timeoutMs);
}

QString Store::syncBranch() const {
    const QString branch = syncState().value("branch").toString();
    return branch.isEmpty() ? QStringLiteral("main") : branch;
}

QString Store::syncRemoteHead() const {
    const ProcResult r = git({"rev-parse", "--verify", "--quiet", "refs/remotes/origin/" + syncBranch()});
    return r.ok() ? r.text().trimmed() : QString();
}

// The repository's owner/name, for a GitHub URL; empty for other hosts.
static QString githubRepo(const QString &url) {
    static const QRegularExpression re(R"(^(?:https://github\.com/|git@github\.com:)([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$)");
    const auto m = re.match(url.trimmed());
    return m.hasMatch() ? m.captured(1) : QString();
}

// --- The snapshot --------------------------------------------------------------------

QString Store::writeSyncSnapshot(const QString &dir, const QStringList &skip, const QStringList &dotfiles) {
    QStringList failed;
    auto note = [&](const QString &error) {
        if (!error.isEmpty())
            failed << error;
    };
    auto wanted = [&](const QString &category) { return !skip.contains(category); };
    // Each part is rewritten whole, so what was removed here goes from the
    // repository too; parts this machine leaves out are kept as they are.
    for (const auto &[category, paths] : categories()) {
        if (!wanted(category))
            continue;
        for (const QString &p : paths) {
            // A link is removed itself, never what it points at.
            QFileInfo info(dir + "/" + p);
            if (info.isSymLink())
                QFile::remove(info.filePath());
            else if (info.isDir())
                QDir(info.filePath()).removeRecursively();
            else if (info.exists())
                QFile::remove(info.filePath());
        }
    }

    if (wanted("settings")) {
        QJsonObject doc = exportSettings();
        doc.remove("exported"); // a date would make every upload a change
        note(saveJson(dir + "/settings.json", doc));
    }

    if (wanted("desktop")) {
        copyTree(menuFile(), dir + "/desktop/menu.jsonc");
        const QJsonObject shell = shellSubset(loadJson(m_paths.config + "/omarchy/shell.json").toObject());
        if (!shell.isEmpty())
            note(saveJson(dir + "/desktop/shell.json", shell));
        if (QFileInfo(m_paths.config + "/mimeapps.list").isFile())
            copyTree(m_paths.config + "/mimeapps.list", dir + "/desktop/mimeapps.list");
        for (const char *name : {"about.txt", "screensaver.txt"})
            copyTree(m_paths.config + "/omarchy/branding/" + name, dir + "/desktop/branding/" + name);
    }

    if (wanted("hypr")) {
        const QString root = m_paths.config + "/hypr";
        for (const QString &rel : hyprFiles(root))
            copyTree(root + "/" + rel, dir + "/hypr/" + rel);
    }

    if (wanted("apps")) {
        QStringList omarchy, catalogPackages;
        for (const QVariantMap &item : softwareItems(true)) {
            const QString package = item.value("package").toString();
            if (!package.isEmpty())
                catalogPackages << package;
            if (item.value("installed").toBool() && item.value("category") != "tools")
                omarchy << item.value("key").toString();
        }
        note(saveLines(dir + "/apps/omarchy.txt", omarchy));

        QStringList excluded = catalogPackages;
        excluded << packageNames(output("omarchy-pkg-defaults", {}, 15000));
        excluded << packageNames(readText(m_paths.omarchy + "/install/omarchy-other.packages"));
        excluded << packageNames(output("pactree", {"-u", "omarchy"}, 15000));
        const QStringList chosen = syncPackages(output("pacman", {"-Qqe"}, 15000).split('\n'), excluded);
        const QStringList foreignList = output("pacman", {"-Qqm"}, 15000).split('\n', Qt::SkipEmptyParts);
        const QSet<QString> foreign(foreignList.begin(), foreignList.end());
        QStringList repo, aur;
        for (const QString &p : chosen)
            (foreign.contains(p) ? aur : repo) << p;
        note(saveLines(dir + "/apps/pacman.txt", repo));
        note(saveLines(dir + "/apps/aur.txt", aur));

        if (!QStandardPaths::findExecutable("flatpak").isEmpty())
            note(saveLines(dir + "/apps/flatpak.txt", output("flatpak", {"list", "--app", "--columns=application"}, 15000).split('\n', Qt::SkipEmptyParts)));

        QStringList mise;
        for (const char *file : {"config.toml", "mise.toml"})
            mise << miseTools(readText(m_paths.config + "/mise/" + file));
        note(saveLines(dir + "/apps/mise.txt", mise));

        QJsonArray webapps;
        for (const QVariant &v : list("webapps").toList()) {
            const QVariantMap w = v.toMap();
            const QString name = w.value("name").toString(), url = w.value("url").toString();
            if (name.isEmpty() || url.isEmpty())
                continue;
            QString icon = w.value("icon").toString();
            QString iconFile = icon.startsWith('/') ? icon : m_paths.data + "/icons/hicolor/256x256/apps/" + icon + ".png";
            QString kept;
            if (QFileInfo(iconFile).isFile()) {
                kept = "webapp-icons/" + name + "." + QFileInfo(iconFile).suffix();
                copyTree(iconFile, dir + "/apps/" + kept);
            }
            webapps.append(QJsonObject{{"name", name}, {"title", w.value("title").toString()}, {"url", url}, {"icon", kept}});
        }
        note(saveJson(dir + "/apps/webapps.json", webapps));

        QJsonArray images;
        for (const QVariantMap &item : appImageItems()) {
            const QString path = item.value("path").toString();
            const QString info = appImageUpdateInfo(path);
            if (item.value("location") == "downloads" || info.isEmpty() || item.value("publisher").toString().isEmpty())
                continue;
            images.append(QJsonObject{{"name", item.value("name").toString()}, {"file", QFileInfo(path).fileName()},
                                      {"publisher", item.value("publisher").toString()}, {"update", info}});
        }
        note(saveJson(dir + "/apps/appimages.json", images));
    }

    if (wanted("themes")) {
        QJsonArray themes;
        const QString root = m_paths.config + "/omarchy/themes";
        for (const QString &slug : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const ProcResult r = run("git", {"-C", root + "/" + slug, "remote", "get-url", "origin"});
            if (r.ok() && !r.text().trimmed().isEmpty())
                themes.append(QJsonObject{{"slug", slug}, {"url", r.text().trimmed()}});
        }
        note(saveJson(dir + "/themes.json", themes));
        copyTree(m_paths.config + "/omarchy/backgrounds", dir + "/wallpapers");
    }

    if (wanted("plugins")) {
        QJsonArray plugins;
        for (const QVariant &v : listPlugins().toList()) {
            const QVariantMap p = v.toMap();
            if (!p.value("git").toBool())
                continue;
            const QString id = p.value("id").toString();
            const ProcResult r = run("git", {"-C", pluginDir(id), "remote", "get-url", "origin"});
            if (r.ok() && !r.text().trimmed().isEmpty())
                plugins.append(QJsonObject{{"id", id}, {"url", r.text().trimmed()}, {"enabled", p.value("enabled").toBool()}});
        }
        note(saveJson(dir + "/plugins.json", plugins));
    }

    if (wanted("git")) {
        const QString name = read("security.gitName").toString(), email = read("security.gitEmail").toString();
        if (!name.isEmpty() || !email.isEmpty())
            note(saveJson(dir + "/git.json", QJsonObject{{"name", name}, {"email", email}}));
    }

    if (wanted("dotfiles")) {
        QJsonArray listed;
        for (const QString &rel : dotfiles) {
            if (!dotfileError(rel).isEmpty())
                continue;
            if (copyTree(m_paths.home + "/" + rel, dir + "/dotfiles/" + rel) > 0)
                listed.append(rel);
        }
        note(saveJson(dir + "/dotfiles.json", listed));
    }

    return failed.isEmpty() ? QString() : failed.join("; ");
}

// --- Connecting ----------------------------------------------------------------------

QString Store::syncConnect(const QString &rawUrl) {
    const QString url = rawUrl.trimmed();
    static const QRegularExpression gitUrl(R"(^(https://[^\s'"]+|git@[A-Za-z0-9.-]+:[^\s'"]+)$)");
    if (!gitUrl.match(url).hasMatch())
        return tr("Enter a git URL, like https://github.com/you/omarchy-sync");

    // Dotfiles and a git identity don't belong in a public repository.
    const QString repo = githubRepo(url);
    if (!repo.isEmpty() && !QStandardPaths::findExecutable("gh").isEmpty()) {
        const ProcResult r = run("gh", {"repo", "view", repo, "--json", "visibility", "--jq", ".visibility"}, 20000);
        if (!r.ok())
            return tr("Can't reach %1 on GitHub: sign in to GitHub, and check the repository exists and is yours").arg(repo);
        if (r.text().trimmed().toUpper() == "PUBLIC")
            return tr("%1 is public; Sync only uses a private repository, since it holds your files").arg(repo);
    }

    QMutexLocker lock(&m_syncLock);
    QDir(syncDir()).removeRecursively();
    // git clones into an empty folder that is there, so it is made private first.
    QDir().mkpath(syncDir());
    makeSyncDirPrivate();
    QStringList clone{"clone", "--quiet", url, syncDir()};
    if (const ProcResult r = run("git", QStringList{"-c", "core.symlinks=false"} + ghCredentialArgs() + clone, 180000); !r.ok())
        return tr("Could not get %1: %2").arg(url, r.failure("git clone"));
    git({"config", "core.symlinks", "false"});
    dropLinks(syncDir());

    // Its default branch, or for a new, empty repository the one git starts.
    QString branch = git({"symbolic-ref", "--short", "refs/remotes/origin/HEAD"}).text().trimmed().section('/', 1);
    if (branch.isEmpty())
        branch = git({"symbolic-ref", "--short", "HEAD"}).text().trimmed();
    if (branch.isEmpty())
        branch = "main";
    updateSyncState([&](QJsonObject &s) {
        s = QJsonObject{{"url", url}, {"branch", branch}, {"auto", true}, {"dotfiles", s.value("dotfiles")},
                        {"skip", s.value("skip")}};
    });

    // An empty repository gets this computer's settings; one with settings
    // in it waits to be reviewed and applied.
    if (syncRemoteHead().isEmpty())
        return syncUpload(false, false);
    return syncCheck();
}

QString Store::syncCreate(const QString &rawName) {
    const QString name = rawName.trimmed().isEmpty() ? QStringLiteral("omarchy-sync") : rawName.trimmed();
    static const QRegularExpression valid("^[A-Za-z0-9_.-]{1,100}$");
    if (!valid.match(name).hasMatch())
        return tr("A repository name has letters, numbers, dots, dashes, and underscores");
    if (QStandardPaths::findExecutable("gh").isEmpty())
        return tr("The GitHub command (gh) isn't installed");
    const ProcResult r = run("gh", {"repo", "create", name, "--private", "--description", "Omarchy settings, kept by Settings' Sync"}, 60000);
    if (!r.ok())
        return tr("Could not create the repository: %1").arg(r.failure("gh repo create"));
    static const QRegularExpression link(R"((https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+))");
    const auto m = link.match(r.text());
    if (!m.hasMatch())
        return tr("Could not create the repository: %1").arg(r.text());
    return syncConnect(m.captured(1));
}

// --- Checking, uploading -------------------------------------------------------------

QString Store::syncCheck() {
    QMutexLocker lock(&m_syncLock);
    if (syncState().value("url").toString().isEmpty())
        return tr("Sync isn't connected to a repository");
    const ProcResult fetched = git({"fetch", "--quiet", "origin"}, 60000, true);
    if (!fetched.ok()) {
        const QString error = tr("Could not reach the repository: %1").arg(fetched.failure("git fetch"));
        updateSyncState([&](QJsonObject &s) { s.insert("lastError", error); });
        return error;
    }
    const QString head = syncRemoteHead();
    const QJsonObject remote = QJsonDocument::fromJson(git({"show", head + ":sync.json"}).out).object();
    updateSyncState([&](QJsonObject &s) {
        s.insert("checked", QDateTime::currentDateTime().toString(Qt::ISODate));
        s.insert("pending", !head.isEmpty() && head != s.value("lastCommit").toString());
        s.insert("remoteHost", remote.value("host"));
        s.insert("remoteUpdated", remote.value("updated"));
        s.remove("lastError");
    });
    return {};
}

QString Store::syncUpload(bool force, bool automatic) {
    QMutexLocker lock(&m_syncLock);
    const QJsonObject state = syncState();
    if (state.value("url").toString().isEmpty())
        return tr("Sync isn't connected to a repository");
    if (!QFileInfo(syncDir() + "/.git").isDir())
        return tr("The local copy of the repository is missing; connect again");
    auto failWith = [&](const QString &error) {
        updateSyncState([&](QJsonObject &s) { s.insert("lastError", error); });
        return error;
    };

    if (const ProcResult r = git({"fetch", "--quiet", "origin"}, 60000, true); !r.ok())
        return failWith(tr("Could not reach the repository: %1").arg(r.failure("git fetch")));
    const QString branch = syncBranch();
    const QString head = syncRemoteHead();

    // Something newer is there: uploading would overwrite it before this
    // computer has it. It waits until it's applied, unless asked to.
    if (!head.isEmpty() && head != state.value("lastCommit").toString() && !force) {
        const QJsonObject remote = QJsonDocument::fromJson(git({"show", head + ":sync.json"}).out).object();
        updateSyncState([&](QJsonObject &s) {
            s.insert("pending", true);
            s.insert("remoteHost", remote.value("host"));
            s.insert("remoteUpdated", remote.value("updated"));
        });
        return automatic ? QString() : tr("The repository has newer settings from %1; apply them first, or upload anyway")
                                           .arg(remote.value("host").toString(tr("another computer")));
    }

    // Start from what's there, so history continues on top of it.
    if (!head.isEmpty()) {
        if (const ProcResult r = git({"checkout", "--quiet", "-B", branch, head}); !r.ok())
            return failWith(r.failure("git checkout"));
        git({"reset", "--quiet", "--hard", head});
    }
    git({"clean", "-fdq"});
    dropLinks(syncDir());

    // Apps being installed from the repository aren't here yet; until
    // they are, the repository's list of them is kept as it is.
    QStringList skip = syncSkipped();
    const QDateTime installing = QDateTime::fromString(state.value("installing").toString(), Qt::ISODate);
    if (installing.isValid() && installing.secsTo(QDateTime::currentDateTime()) < 2 * 3600)
        skip << "apps";
    QStringList dotfiles;
    for (const QJsonValue &v : state.value("dotfiles").toArray())
        dotfiles << v.toString();
    const QString snapError = writeSyncSnapshot(syncDir(), skip, dotfiles);

    git({"add", "-A"});
    const QStringList changed = git({"status", "--porcelain"}).text().split('\n', Qt::SkipEmptyParts);
    if (changed.isEmpty()) {
        updateSyncState([&](QJsonObject &s) {
            s.insert("pending", false);
            if (!head.isEmpty())
                s.insert("lastCommit", head);
            s.insert("checked", QDateTime::currentDateTime().toString(Qt::ISODate));
            s.remove("lastError");
        });
        return snapError;
    }

    // Which parts changed, for the commit's subject: "desktop, apps".
    QStringList parts;
    for (const QString &line : changed) {
        const QString path = line.mid(3).section(" -> ", -1).remove('"');
        for (const auto &[category, paths] : categories())
            for (const QString &p : paths)
                if ((path == p || path.startsWith(p + "/")) && !parts.contains(category))
                    parts << category;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    saveJson(syncDir() + "/sync.json", QJsonObject{{"app", "omasettings-sync"}, {"version", 1}, {"host", hostName()},
                                                   {"updated", now}, {"omarchy", output("omarchy-version", {})},
                                                   {"settings", QCoreApplication::applicationVersion()}});
    saveBytes(syncDir() + "/README.md",
              QStringLiteral("# Omarchy Sync\n\n"
                             "Settings, apps, and desktop files kept by [Settings](https://github.com/ipkalid/omarchy-settings) "
                             "for Omarchy. Open General › Sync on another computer and connect this repository to bring it to match.\n\n"
                             "Last uploaded from **%1** on %2.\n\n"
                             "| Path | What it is |\n| --- | --- |\n"
                             "| settings.json | Settings, shortcuts, login items, window rules |\n"
                             "| desktop/ | Omarchy menu, dock and bar, file types, branding |\n"
                             "| hypr/ | ~/.config/hypr: keybindings, input, look and feel, autostart (not monitors) |\n"
                             "| apps/ | Omarchy apps, pacman, AUR, Flatpak, mise, web apps, AppImages |\n"
                             "| themes.json, wallpapers/ | Themes added by URL, wallpapers you added |\n"
                             "| plugins.json | Shell plugins and whether they're on |\n"
                             "| git.json | Name and email on git commits |\n"
                             "| dotfiles/ | Files and folders you chose to sync |\n\n"
                             "Displays, sound devices, networks, Bluetooth, power, printers, and the firewall stay with each computer.\n")
                  .arg(hostName(), now.left(10))
                  .toUtf8());
    git({"add", "-A"});

    QStringList commit{"commit", "--quiet", "-m", hostName() + ": " + (parts.isEmpty() ? QStringLiteral("update") : parts.join(", "))};
    if (git({"config", "user.email"}).text().trimmed().isEmpty())
        commit = QStringList{"-c", "user.name=Settings on " + hostName(), "-c", "user.email=omasettings@localhost"} + commit;
    if (const ProcResult r = git(commit); !r.ok())
        return failWith(r.failure("git commit"));
    if (const ProcResult r = git({"push", "--quiet", "origin", "HEAD:" + branch}, 120000, true); !r.ok()) {
        // Someone else uploaded in the meantime: theirs is the newer one.
        git({"reset", "--quiet", "--hard", head.isEmpty() ? QStringLiteral("HEAD~0") : head});
        return failWith(tr("Could not upload: %1").arg(r.failure("git push")));
    }
    const QString pushed = git({"rev-parse", "HEAD"}).text().trimmed();
    updateSyncState([&](QJsonObject &s) {
        s.insert("lastCommit", pushed);
        s.insert("lastUpload", now);
        s.insert("checked", now);
        s.insert("pending", false);
        s.insert("remoteHost", hostName());
        s.insert("remoteUpdated", now);
        s.remove("lastError");
    });
    return snapError;
}

// --- What applying would do ----------------------------------------------------------

static QString shown(const QVariant &v) {
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    if (v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QStringList)
        return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
    return v.toString();
}

static bool sameValue(const QVariant &current, const QJsonValue &wanted) {
    if (QJsonValue::fromVariant(current) == wanted)
        return true;
    if (wanted.isDouble() && current.canConvert<double>())
        return qFuzzyCompare(1 + current.toDouble(), 1 + wanted.toDouble());
    return shown(current) == shown(wanted.toVariant());
}

QVariantMap Store::syncPlan() {
    const QString dir = syncDir();
    const QStringList skip = syncSkipped();
    auto wanted = [&](const QString &category) { return !skip.contains(category); };
    QVariantMap plan;

    if (wanted("settings")) {
        const QJsonObject doc = loadJson(dir + "/settings.json").toObject();
        QVariantList settings;
        const QJsonObject values = doc.value("settings").toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const Setting *s = setting(it.key());
            if (!s || isMachineSpecific(it.key()) || it.key().startsWith("display.night"))
                continue;
            const QVariant current = s->read ? s->read() : QVariant();
            if (sameValue(current, it.value()))
                continue;
            settings << QVariantMap{{"id", it.key()}, {"label", s->label}, {"from", shown(current)}, {"to", shown(it.value().toVariant())}};
        }
        plan["settings"] = settings;
        QStringList managed;
        const QJsonObject state = managedState(), theirs = doc.value("managed").toObject();
        for (auto it = theirs.begin(); it != theirs.end(); ++it)
            if (importableManaged().contains(it.key()) && state.value(it.key()) != it.value())
                managed << it.key();
        plan["managed"] = managed;
        // Commands that would be new here: each login item, and each
        // shortcut that runs one.
        QVariantList commands;
        auto commandsIn = [](const QJsonArray &list, const QString &field) {
            QStringList out;
            for (const QJsonValue &v : list) {
                const QJsonObject o = v.toObject();
                if (field == "command" ? !o.value("command").toString().isEmpty() : o.value("kind").toString() == "command")
                    out << o.value(field == "command" ? "command" : "value").toString();
            }
            return out;
        };
        const QStringList mineStart = commandsIn(state.value("autostart").toArray(), "command");
        for (const QString &c : commandsIn(theirs.value("autostart").toArray(), "command"))
            if (!mineStart.contains(c))
                commands << QVariantMap{{"label", tr("Login items")}, {"command", c}};
        const QStringList mineKeys = commandsIn(state.value("bindings").toArray(), "value");
        for (const QString &c : commandsIn(theirs.value("bindings").toArray(), "value"))
            if (!mineKeys.contains(c))
                commands << QVariantMap{{"label", tr("Keyboard shortcuts")}, {"command", c}};
        plan["commands"] = commands;
    }

    if (wanted("desktop")) {
        QStringList desktop;
        if (QFileInfo(dir + "/desktop/menu.jsonc").isFile() && !sameFile(dir + "/desktop/menu.jsonc", menuFile()))
            desktop << "menu";
        const QJsonObject theirs = loadJson(dir + "/desktop/shell.json").toObject();
        if (!theirs.isEmpty() && theirs != shellSubset(loadJson(m_paths.config + "/omarchy/shell.json").toObject()))
            desktop << "shell";
        if (QFileInfo(dir + "/desktop/mimeapps.list").isFile() && !sameFile(dir + "/desktop/mimeapps.list", m_paths.config + "/mimeapps.list"))
            desktop << "mime";
        for (const char *name : {"about.txt", "screensaver.txt"})
            if (QFileInfo(dir + "/desktop/branding/" + name).isFile()
                && !sameFile(dir + "/desktop/branding/" + name, m_paths.config + "/omarchy/branding/" + name)) {
                desktop << "branding";
                break;
            }
        plan["desktop"] = desktop;
    }

    if (wanted("hypr")) {
        QStringList hypr;
        for (const QString &rel : filesUnder(dir + "/hypr"))
            if (!hyprLocal(rel) && !sameFile(dir + "/hypr/" + rel, m_paths.config + "/hypr/" + rel))
                hypr << rel;
        plan["hypr"] = hypr;
    }

    if (wanted("apps")) {
        const QStringList theirOmarchy = loadLines(dir + "/apps/omarchy.txt");
        QVariantList omarchy;
        if (!theirOmarchy.isEmpty()) {
            for (const QVariantMap &item : softwareItems(true)) {
                if (!theirOmarchy.contains(item.value("key").toString()) || item.value("installed").toBool()
                    || !item.value("available").toBool())
                    continue;
                omarchy << QVariantMap{{"key", item.value("key")}, {"label", item.value("label")},
                                       {"installId", item.value("installId")}};
            }
        }
        plan["omarchy"] = omarchy;

        const QStringList installedList = output("pacman", {"-Qq"}, 15000).split('\n', Qt::SkipEmptyParts);
        const QSet<QString> installed(installedList.begin(), installedList.end());
        // Names go to installers as arguments; one that could be read as
        // an option, or isn't a name at all, is left out.
        static const QRegularExpression packageName(R"(^[a-z0-9@_+][a-z0-9@._+-]*$)");
        for (const char *kind : {"pacman", "aur"}) {
            QStringList missing;
            for (const QString &p : loadLines(dir + "/apps/" + kind + ".txt"))
                if (packageName.match(p).hasMatch() && !installed.contains(p))
                    missing << p;
            plan[kind] = missing;
        }

        const QStringList theirFlatpaks = loadLines(dir + "/apps/flatpak.txt");
        QStringList flatpaks;
        if (!theirFlatpaks.isEmpty()) {
            const QStringList mine = output("flatpak", {"list", "--app", "--columns=application"}, 15000).split('\n', Qt::SkipEmptyParts);
            static const QRegularExpression appId(R"(^[A-Za-z0-9_][A-Za-z0-9_.-]*$)");
            for (const QString &id : theirFlatpaks)
                if (appId.match(id).hasMatch() && !mine.contains(id))
                    flatpaks << id;
        }
        plan["flatpak"] = flatpaks;

        QSet<QString> mineTools;
        for (const char *file : {"config.toml", "mise.toml"})
            for (const QString &t : miseTools(readText(m_paths.config + "/mise/" + file)))
                mineTools.insert(t.section('@', 0, 0));
        QStringList mise;
        static const QRegularExpression tool(R"(^[A-Za-z0-9_@][A-Za-z0-9_@.:/+-]*$)");
        for (const QString &t : loadLines(dir + "/apps/mise.txt"))
            if (tool.match(t).hasMatch() && !mineTools.contains(t.section('@', 0, 0)))
                mise << t;
        plan["mise"] = mise;

        QSet<QString> mineWebapps;
        for (const QVariant &v : list("webapps").toList())
            mineWebapps.insert(v.toMap().value("name").toString());
        QVariantList webapps;
        for (const QJsonValue &v : loadJson(dir + "/apps/webapps.json").toArray())
            if (!mineWebapps.contains(v.toObject().value("name").toString()))
                webapps << v.toObject().toVariantMap();
        plan["webapps"] = webapps;

        QSet<QString> minePublishers;
        for (const QVariantMap &item : appImageItems())
            if (item.value("location") != "downloads")
                minePublishers.insert(item.value("publisher").toString());
        QVariantList images;
        for (const QJsonValue &v : loadJson(dir + "/apps/appimages.json").toArray())
            if (!minePublishers.contains(v.toObject().value("publisher").toString()))
                images << v.toObject().toVariantMap();
        plan["appimages"] = images;
    }

    if (wanted("themes")) {
        QVariantList themes;
        for (const QJsonValue &v : loadJson(dir + "/themes.json").toArray())
            if (!QFileInfo(m_paths.config + "/omarchy/themes/" + v.toObject().value("slug").toString()).isDir())
                themes << v.toObject().toVariantMap();
        plan["themes"] = themes;
        QStringList wallpapers;
        for (const QString &rel : filesUnder(dir + "/wallpapers"))
            if (!QFileInfo::exists(m_paths.config + "/omarchy/backgrounds/" + rel))
                wallpapers << rel;
        plan["wallpapers"] = wallpapers;
    }

    if (wanted("plugins")) {
        QHash<QString, bool> mine;
        for (const QVariant &v : listPlugins().toList())
            mine.insert(v.toMap().value("id").toString(), v.toMap().value("enabled").toBool());
        QVariantList plugins;
        for (const QJsonValue &v : loadJson(dir + "/plugins.json").toArray()) {
            const QJsonObject p = v.toObject();
            const QString id = p.value("id").toString();
            if (!mine.contains(id) || mine.value(id) != p.value("enabled").toBool())
                plugins << QVariantMap{{"id", id}, {"url", p.value("url").toString()}, {"enabled", p.value("enabled").toBool()},
                                       {"installed", mine.contains(id)}};
        }
        plan["plugins"] = plugins;
    }

    if (wanted("git")) {
        const QJsonObject g = loadJson(dir + "/git.json").toObject();
        QVariantMap git;
        if (!g.value("name").toString().isEmpty() && g.value("name").toString() != read("security.gitName").toString())
            git["name"] = g.value("name").toString();
        if (!g.value("email").toString().isEmpty() && g.value("email").toString() != read("security.gitEmail").toString())
            git["email"] = g.value("email").toString();
        plan["git"] = git;
    }

    if (wanted("dotfiles")) {
        QStringList dotfiles;
        for (const QString &rel : filesUnder(dir + "/dotfiles"))
            if (dotfileError(rel).isEmpty() && !sameFile(dir + "/dotfiles/" + rel, m_paths.home + "/" + rel))
                dotfiles << rel;
        plan["dotfiles"] = dotfiles;
        // Paths the repository lists that this computer doesn't sync yet:
        // once applied, they are uploaded from here too, so they are shown.
        const QJsonArray mine = syncState().value("dotfiles").toArray();
        QStringList adopted;
        for (const QJsonValue &v : loadJson(dir + "/dotfiles.json").toArray())
            if (dotfileError(v.toString()).isEmpty() && !mine.contains(v) && !adopted.contains(v.toString()))
                adopted << v.toString();
        plan["dotfilesAdded"] = adopted;
    }
    return plan;
}

// The plan as the confirmation lists it: one group per part, each change a
// row with what it is and, for a setting, what it goes from and to.
static QVariantList planGroups(const QVariantMap &plan) {
    QVariantList groups;
    auto group = [&](const QString &key, const QVariantList &items) {
        if (!items.isEmpty())
            groups << QVariantMap{{"key", key}, {"items", items}};
    };
    auto rows = [](const QStringList &names, const QString &detail) {
        QVariantList out;
        for (const QString &n : names)
            out << QVariantMap{{"label", n}, {"detail", detail}};
        return out;
    };

    QVariantList settings;
    for (const QVariant &v : plan.value("settings").toList()) {
        const QVariantMap s = v.toMap();
        settings << QVariantMap{{"label", s.value("label")}, {"setting", true},
                                {"detail", s.value("from").toString() + " → " + s.value("to").toString()}};
    }
    auto managedName = [](const QString &key) {
        return key == "bindings" ? tr("Keyboard shortcuts") : key == "unbinds" ? tr("Turned-off shortcuts")
             : key == "autostart" ? tr("Login items") : key == "windowRules" ? tr("Window rules")
             : key == "gestures" ? tr("Gestures") : key == "nightShift" ? tr("Night Shift schedule") : key;
    };
    for (const QString &key : plan.value("managed").toStringList())
        settings << QVariantMap{{"label", managedName(key)}};
    // What login items and shortcuts would run, word for word.
    for (const QVariant &v : plan.value("commands").toList())
        settings << QVariantMap{{"label", v.toMap().value("label")}, {"detail", v.toMap().value("command")}};
    group("settings", settings);

    auto desktopName = [](const QString &key) {
        return key == "menu" ? tr("Omarchy menu") : key == "shell" ? tr("Dock and bar")
             : key == "mime" ? tr("Apps that open each file type") : key == "branding" ? tr("Branding text") : key;
    };
    QVariantList desktop;
    for (const QString &key : plan.value("desktop").toStringList())
        desktop << QVariantMap{{"label", desktopName(key)}};
    group("desktop", desktop);
    group("hypr", rows(plan.value("hypr").toStringList(), tr("Replaced; the old one is kept beside it")));

    QVariantList apps;
    for (const QVariant &v : plan.value("omarchy").toList())
        apps << QVariantMap{{"label", v.toMap().value("label")}, {"detail", "Omarchy"}};
    apps += rows(plan.value("pacman").toStringList(), "pacman");
    apps += rows(plan.value("aur").toStringList(), "AUR");
    apps += rows(plan.value("flatpak").toStringList(), "Flatpak");
    apps += rows(plan.value("mise").toStringList(), "mise");
    for (const QVariant &v : plan.value("webapps").toList())
        apps << QVariantMap{{"label", v.toMap().value("title").toString().isEmpty() ? v.toMap().value("name") : v.toMap().value("title")},
                            {"detail", tr("Web app")}};
    for (const QVariant &v : plan.value("appimages").toList())
        apps << QVariantMap{{"label", v.toMap().value("name")}, {"detail", "AppImage · " + v.toMap().value("update").toString()}};
    group("apps", apps);

    QVariantList themes;
    for (const QVariant &v : plan.value("themes").toList())
        themes << QVariantMap{{"label", v.toMap().value("slug")}, {"detail", tr("Theme") + " · " + v.toMap().value("url").toString()}};
    themes += rows(plan.value("wallpapers").toStringList(), tr("Wallpaper"));
    group("themes", themes);

    QVariantList plugins;
    for (const QVariant &v : plan.value("plugins").toList()) {
        const QVariantMap p = v.toMap();
        plugins << QVariantMap{{"label", p.value("id")},
                               {"detail", !p.value("installed").toBool() ? tr("Add") + " · " + p.value("url").toString()
                                          : p.value("enabled").toBool() ? tr("Turn on") : tr("Turn off")}};
    }
    group("plugins", plugins);

    QVariantList git;
    const QVariantMap g = plan.value("git").toMap();
    if (g.contains("name"))
        git << QVariantMap{{"label", tr("Name on your git commits")}, {"detail", g.value("name")}};
    if (g.contains("email"))
        git << QVariantMap{{"label", tr("Email on your git commits")}, {"detail", g.value("email")}};
    group("git", git);

    QVariantList dotfiles = rows(plan.value("dotfiles").toStringList(), tr("Replaced; the old one is kept beside it"));
    dotfiles += rows(plan.value("dotfilesAdded").toStringList(), tr("Uploaded from this computer from now on"));
    group("dotfiles", dotfiles);
    return groups;
}

QVariant Store::listSyncPreview() {
    QMutexLocker lock(&m_syncLock);
    if (syncState().value("url").toString().isEmpty())
        return QVariantMap{{"error", tr("Sync isn't connected to a repository")}};
    if (const ProcResult r = git({"fetch", "--quiet", "origin"}, 60000, true); !r.ok())
        return QVariantMap{{"error", tr("Could not reach the repository: %1").arg(r.failure("git fetch"))}};
    const QString head = syncRemoteHead();
    if (head.isEmpty())
        return QVariantMap{{"groups", QVariantList()}, {"commit", QString()}};
    git({"checkout", "--quiet", "-B", syncBranch(), head});
    git({"reset", "--quiet", "--hard", head});
    git({"clean", "-fdq"});
    dropLinks(syncDir());
    const QJsonObject remote = loadJson(syncDir() + "/sync.json").toObject();
    const QVariantList groups = planGroups(syncPlan());
    int total = 0;
    for (const QVariant &g : groups)
        total += g.toMap().value("items").toList().size();
    return QVariantMap{{"groups", groups}, {"total", total}, {"commit", head},
                       {"host", remote.value("host").toString()}, {"updated", remote.value("updated").toString()}};
}

// --- Applying ------------------------------------------------------------------------

// Where an AppImage's newest file is, from the update information it
// carries: a GitHub release's asset, or what a .zsync file names.
QVariantMap Store::appImageSource(const QString &updateInfo) const {
    // https only, redirects included, so no one on the network can answer.
    auto fetch = [this](const QStringList &args) {
        return run("curl", QStringList{"-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "20"} + args, 25000);
    };
    // The file is named by the publisher; a name that is only a file name.
    static const QRegularExpression plainName(R"(^[A-Za-z0-9_][A-Za-z0-9_.+ -]*\.AppImage$)", QRegularExpression::CaseInsensitiveOption);
    const QStringList info = updateInfo.split('|');
    QString zsyncUrl;
    if (info.value(0) == "gh-releases-zsync" && info.size() >= 5) {
        const QString tag = info[3];
        const QString api = "https://api.github.com/repos/" + info[1] + "/" + info[2]
            + (tag == "latest" ? QStringLiteral("/releases/latest")
               : tag == "latest-pre" || tag == "latest-all" ? QStringLiteral("/releases") : "/releases/tags/" + tag);
        const QJsonDocument doc = QJsonDocument::fromJson(fetch({"-H", "Accept: application/vnd.github+json", api}).out);
        QJsonObject release = doc.object();
        if (doc.isArray() && !doc.array().isEmpty())
            release = doc.array().first().toObject();
        QString pattern = info[4];
        if (pattern.endsWith(".zsync"))
            pattern.chop(6);
        const QRegularExpression match(QRegularExpression::wildcardToRegularExpression(pattern));
        for (const QJsonValue &asset : release.value("assets").toArray()) {
            const QString name = asset.toObject().value("name").toString();
            const QString url = asset.toObject().value("browser_download_url").toString();
            if (!match.match(name).hasMatch() || !plainName.match(name).hasMatch() || !url.startsWith("https://"))
                continue;
            // GitHub publishes each asset's SHA-256.
            const QString digest = asset.toObject().value("digest").toString();
            return {{"url", url}, {"file", name}, {"sha256", digest.startsWith("sha256:") ? digest.mid(7) : QString()}};
        }
        return {};
    }
    if (info.value(0) == "zsync" && info.size() >= 2)
        zsyncUrl = info.mid(1).join('|');
    if (!zsyncUrl.startsWith("https://"))
        return {};
    QString url, file, sha1;
    for (const QString &line : QString::fromUtf8(fetch({"-r", "0-4095", zsyncUrl}).out).split('\n')) {
        if (line.trimmed().isEmpty())
            break;
        const QString key = line.section(':', 0, 0).trimmed(), value = line.section(':', 1).trimmed();
        if (key == "URL")
            url = QUrl(zsyncUrl).resolved(QUrl(value)).toString();
        else if (key == "Filename")
            file = value;
        else if (key == "SHA-1")
            sha1 = value.toLower();
    }
    if (file.isEmpty())
        file = QFileInfo(QUrl(url).path()).fileName();
    if (!url.startsWith("https://") || !plainName.match(file).hasMatch())
        return {};
    return {{"url", url}, {"file", file}, {"sha1", sha1}};
}

// One terminal installs everything missing that needs a password or a
// download, in order, and carries on past what fails; Settings is told how
// it went at the end.
QString Store::syncInstallScript(const QVariantMap &plan) {
    QStringList body;
    QStringList labels;
    auto quoteAll = [](const QStringList &l) { return shellWords(l); };

    const QStringList pacman = plan.value("pacman").toStringList();
    const QStringList aur = plan.value("aur").toStringList();
    const QVariantList omarchy = plan.value("omarchy").toList();
    const QStringList flatpaks = plan.value("flatpak").toStringList();
    const QStringList mise = plan.value("mise").toStringList();
    const QVariantList images = plan.value("appimages").toList();
    if (pacman.isEmpty() && aur.isEmpty() && omarchy.isEmpty() && flatpaks.isEmpty() && mise.isEmpty() && images.isEmpty())
        return {};

    if (!pacman.isEmpty() || !aur.isEmpty() || !omarchy.isEmpty() || !flatpaks.isEmpty())
        body << "source omarchy-sudo-keepalive";
    if (!pacman.isEmpty()) {
        body << "step " + shellQuote(tr("Packages"));
        body << QStringLiteral("omarchy-pkg-add %1 || for p in %1; do omarchy-pkg-add \"$p\" || failed+=(\"$p\"); done").arg(quoteAll(pacman));
    }
    if (!aur.isEmpty()) {
        body << "step " + shellQuote(tr("Packages from the AUR"));
        body << QStringLiteral("for p in %1; do omarchy-pkg-aur-add \"$p\" || failed+=(\"$p\"); done").arg(quoteAll(aur));
    }
    if (!omarchy.isEmpty()) {
        body << "step " + shellQuote(tr("Omarchy apps"));
        const QJsonObject menu = defaultMenuItems();
        for (const QVariant &v : omarchy) {
            const QVariantMap item = v.toMap();
            const QString action = menu.value(item.value("installId").toString()).toObject().value("action").toString();
            if (action.isEmpty())
                continue;
            body << QStringLiteral("echo %1; ( eval %2 ) || failed+=(%3)")
                        .arg(shellQuote("· " + item.value("label").toString()), shellQuote(action), shellQuote(item.value("label").toString()));
        }
    }
    if (!flatpaks.isEmpty()) {
        body << "step Flatpak";
        body << QStringLiteral("for a in %1; do flatpak install -y --noninteractive flathub -- \"$a\" || failed+=(\"$a\"); done").arg(quoteAll(flatpaks));
    }
    if (!mise.isEmpty()) {
        body << "step mise";
        body << QStringLiteral("for t in %1; do mise use --global -- \"$t\" || failed+=(\"$t\"); done").arg(quoteAll(mise));
    }
    const QString self = QCoreApplication::applicationFilePath();
    if (!images.isEmpty()) {
        body << "step AppImages";
        for (const QVariant &v : images) {
            const QVariantMap image = v.toMap();
            const QVariantMap source = appImageSource(image.value("update").toString());
            const QString name = image.value("name").toString();
            if (source.value("url").toString().isEmpty() || source.value("file").toString().isEmpty()) {
                body << QStringLiteral("echo %1; failed+=(%2)").arg(shellQuote(tr("Could not find a download for %1").arg(name)), shellQuote(name));
                continue;
            }
            const QString dest = appImagesDir() + "/" + QFileInfo(source.value("file").toString()).fileName();
            // The download is checked against the hash its publisher lists,
            // when there is one, before it is kept.
            static const QRegularExpression hex40("^[0-9a-f]{40}$"), hex64("^[0-9a-f]{64}$");
            const QString sha1 = source.value("sha1").toString(), sha256 = source.value("sha256").toString();
            const QString check = hex64.match(sha256).hasMatch()
                ? QStringLiteral("echo %1 | sha256sum -c --quiet -; ").arg(shellQuote(sha256 + "  " + dest + ".part"))
                : hex40.match(sha1).hasMatch() ? QStringLiteral("echo %1 | sha1sum -c --quiet -; ").arg(shellQuote(sha1 + "  " + dest + ".part"))
                : QString();
            body << QStringLiteral("( set -e; echo %1; mkdir -p %2; curl -fL --proto =https --proto-redir =https --progress-bar -o %3.part %4; "
                                   "%7chmod +x %3.part; mv -f %3.part %3; %5 act appimage.integrate %3 ) || { rm -f %3.part; failed+=(%6); }")
                        .arg(shellQuote("· " + name), shellQuote(appImagesDir()), shellQuote(dest),
                             shellQuote(source.value("url").toString()), shellQuote(self), shellQuote(name), check);
        }
    }

    // Omarchy's installers open their own terminal; here they run in this
    // one, and the menu's install-and-open helpers only install.
    const QString script = QStringLiteral(
        "#!/bin/bash\n"
        "# Written by Settings' Sync: installs what the synced repository lists and this computer lacks.\n"
        "shim=$(mktemp -d)\n"
        "printf '#!/bin/bash\\nbash -c \"$*\"\\n' > \"$shim/omarchy-launch-floating-terminal-with-presentation\"\n"
        "chmod +x \"$shim/omarchy-launch-floating-terminal-with-presentation\"\n"
        "export PATH=\"$shim:$PATH\"\n"
        "omarchy-install-and-launch() { omarchy-pkg-add $2; }\n"
        "omarchy-install-app() { omarchy-pkg-add $2; }\n"
        "failed=()\n"
        "step() { echo; echo \"== $1\"; }\n"
        "%1\n"
        "rm -rf \"$shim\"\n"
        "echo\n"
        "if (( ${#failed[@]} )); then echo %2 \"${failed[*]}\"; else echo %3; fi\n"
        "%4 act sync.installed \"${#failed[@]}\" \"${failed[*]}\" >/dev/null 2>&1\n"
        "(( ${#failed[@]} == 0 ))\n")
        .arg(body.join('\n'), shellQuote(tr("Not installed:")), shellQuote(tr("Everything is installed.")), shellQuote(self));
    const QString path = m_paths.state + "/omasettings/sync-install.sh";
    if (const QString error = saveBytes(path, script.toUtf8()); !error.isEmpty())
        return error;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (const QString error = launch("omarchy-launch-floating-terminal-with-presentation", {"bash " + shellQuote(path)}); !error.isEmpty())
        return error;
    updateSyncState([](QJsonObject &s) {
        s.insert("installing", QDateTime::currentDateTime().toString(Qt::ISODate));
        s.remove("installResult");
    });
    return {};
}

// `reviewed` is the commit the confirmation showed. When given, that commit
// is what is applied: something pushed since then hasn't been looked at.
QString Store::syncApply(const QString &reviewed) {
    QMutexLocker lock(&m_syncLock);
    if (syncState().value("url").toString().isEmpty())
        return tr("Sync isn't connected to a repository");
    if (const ProcResult r = git({"fetch", "--quiet", "origin"}, 60000, true); !r.ok())
        return tr("Could not reach the repository: %1").arg(r.failure("git fetch"));
    const QString head = syncRemoteHead();
    if (head.isEmpty())
        return tr("The repository is empty; upload from this computer first");
    if (!reviewed.isEmpty() && reviewed != head)
        return tr("The repository changed since you looked; open it again to see what's new");
    git({"checkout", "--quiet", "-B", syncBranch(), head});
    git({"reset", "--quiet", "--hard", head});
    git({"clean", "-fdq"});
    dropLinks(syncDir());

    const QString dir = syncDir();
    const QVariantMap plan = syncPlan();
    QStringList failed;
    auto note = [&](const QString &what, const QString &error) {
        if (!error.isEmpty())
            failed << what + ": " + error;
    };

    // Plugins and themes first: the bar and the theme setting refer to them.
    for (const QVariant &v : plan.value("plugins").toList()) {
        const QVariantMap p = v.toMap();
        const QString id = p.value("id").toString();
        if (!p.value("installed").toBool())
            note(id, actPlugin("plugin.add", {p.value("url")}));
        note(id, actPlugin(p.value("enabled").toBool() ? "plugin.enable" : "plugin.disable", {id}));
    }
    for (const QVariant &v : plan.value("themes").toList())
        note(v.toMap().value("slug").toString(), runOk("omarchy-theme-install", {v.toMap().value("url").toString()}, 300000));
    for (const QString &rel : plan.value("wallpapers").toStringList())
        if (insideSnapshot(dir, dir + "/wallpapers/" + rel))
            copyTree(dir + "/wallpapers/" + rel, m_paths.config + "/omarchy/backgrounds/" + rel);
    for (const QVariant &v : plan.value("webapps").toList()) {
        const QVariantMap w = v.toMap();
        // The icon is one Sync kept in apps/webapp-icons, and nothing else.
        static const QRegularExpression keptIcon(R"(^webapp-icons/[^/]+$)");
        const QString kept = w.value("icon").toString();
        const QString icon = keptIcon.match(kept).hasMatch() && insideSnapshot(dir, dir + "/apps/" + kept)
            ? dir + "/apps/" + kept : QStringLiteral("applications-internet");
        note(w.value("name").toString(), runOk("omarchy-webapp-install", {w.value("name").toString(), w.value("url").toString(), icon}, 60000));
    }

    const QStringList desktop = plan.value("desktop").toStringList();
    if (desktop.contains("menu") && insideSnapshot(dir, dir + "/desktop/menu.jsonc")) {
        backUp(menuFile());
        note(tr("Omarchy menu"), saveBytes(menuFile(), readBytes(dir + "/desktop/menu.jsonc")));
    }
    if (desktop.contains("shell")) {
        const QJsonObject theirs = loadJson(dir + "/desktop/shell.json").toObject();
        for (auto it = theirs.begin(); it != theirs.end(); ++it) {
            const QJsonObject part = it.value().toObject();
            for (auto key = part.begin(); key != part.end(); ++key)
                note(tr("Dock and bar"), shellSet(it.key() + "." + key.key(), key.value()));
        }
    }
    if (desktop.contains("mime")) {
        backUp(m_paths.config + "/mimeapps.list");
        note(tr("Apps that open each file type"), restoreFile(m_paths.config + "/mimeapps.list", dir + "/desktop/mimeapps.list", dir));
    }
    if (desktop.contains("branding"))
        for (const char *name : {"about.txt", "screensaver.txt"})
            if (insideSnapshot(dir, dir + "/desktop/branding/" + name))
                copyTree(dir + "/desktop/branding/" + name, m_paths.config + "/omarchy/branding/" + name);

    const QVariantMap g = plan.value("git").toMap();
    if (g.contains("name"))
        note(tr("Name on your git commits"), write("security.gitName", g.value("name")));
    if (g.contains("email"))
        note(tr("Email on your git commits"), write("security.gitEmail", g.value("email")));

    for (const QString &rel : plan.value("dotfiles").toStringList()) {
        if (!dotfileError(rel).isEmpty())
            continue;
        const QString target = m_paths.home + "/" + rel;
        backUp(target);
        note(rel, restoreFile(target, dir + "/dotfiles/" + rel, dir));
    }
    if (!syncSkipped().contains("dotfiles")) {
        const QStringList theirs = plan.value("dotfilesAdded").toStringList();
        updateSyncState([&](QJsonObject &s) {
            QJsonArray mine = s.value("dotfiles").toArray();
            for (const QString &rel : theirs)
                if (!mine.contains(rel))
                    mine.append(rel);
            s.insert("dotfiles", mine);
        });
    }

    // Hyprland's own files, then one reload (settings below reload it again
    // only if they change something).
    const QStringList hypr = plan.value("hypr").toStringList();
    for (const QString &rel : hypr) {
        const QString target = m_paths.config + "/hypr/" + rel;
        backUp(target);
        note(rel, restoreFile(target, dir + "/hypr/" + rel, dir));
    }
    if (!hypr.isEmpty() && m_paths.hyprland)
        run("hyprctl", {"reload"}, 10000);

    // Settings last, once the theme, plugins, and fonts they name are here.
    if (!syncSkipped().contains("settings") && (!plan.value("settings").toList().isEmpty() || !plan.value("managed").toStringList().isEmpty()))
        note(tr("Settings"), importSettings(loadJson(dir + "/settings.json").toObject(), true));

    note(tr("Apps"), syncInstallScript(plan));

    updateSyncState([&](QJsonObject &s) {
        s.insert("lastCommit", head);
        s.insert("lastApply", QDateTime::currentDateTime().toString(Qt::ISODate));
        s.insert("pending", false);
        s.remove("lastError");
    });
    return failed.isEmpty() ? QString() : tr("Some things were not applied: %1").arg(failed.join("; "));
}

// --- Actions and the page's status ---------------------------------------------------

QVariant Store::listSync() {
    const QJsonObject state = syncState();
    QVariantMap out = state.toVariantMap();
    out["connected"] = !state.value("url").toString().isEmpty() && QFileInfo(syncDir() + "/.git").isDir();
    out["auto"] = state.value("auto").toBool(true);
    out["host"] = hostName();
    out["repo"] = githubRepo(state.value("url").toString());
    if (!out.value("connected").toBool()) {
        out["gh"] = !QStandardPaths::findExecutable("gh").isEmpty();
        out["signedIn"] = run("gh", {"auth", "status"}, 15000).ok();
        return out;
    }
    // What the repository holds, part by part, as the page counts it.
    const QString dir = syncDir();
    int apps = 0;
    for (const char *f : {"omarchy.txt", "pacman.txt", "aur.txt", "flatpak.txt", "mise.txt"})
        apps += loadLines(dir + "/apps/" + f).size();
    apps += loadJson(dir + "/apps/webapps.json").toArray().size() + loadJson(dir + "/apps/appimages.json").toArray().size();
    out["counts"] = QVariantMap{
        {"settings", loadJson(dir + "/settings.json").toObject().value("settings").toObject().size()},
        {"desktop", QDir(dir + "/desktop").entryList(QDir::Files | QDir::NoDotAndDotDot).size()
                        + (QFileInfo(dir + "/desktop/branding").isDir() ? 1 : 0)},
        {"hypr", filesUnder(dir + "/hypr").size()},
        {"apps", apps},
        {"themes", loadJson(dir + "/themes.json").toArray().size() + filesUnder(dir + "/wallpapers").size()},
        {"plugins", loadJson(dir + "/plugins.json").toArray().size()},
        {"git", QFileInfo(dir + "/git.json").isFile() ? 1 : 0},
        {"dotfiles", state.value("dotfiles").toArray().size()},
    };
    return out;
}

QString Store::actSync(const QString &action, const QVariantList &args) {
    const QString a0 = args.value(0).toString();
    if (action == "sync.connect")
        return syncConnect(a0);
    if (action == "sync.create")
        return syncCreate(a0);
    if (action == "sync.check")
        return syncCheck();
    if (action == "sync.upload")
        return syncUpload(a0 == "force", a0 == "auto");
    if (action == "sync.apply")
        return syncApply(a0);
    if (action == "sync.disconnect") {
        QMutexLocker lock(&m_syncLock);
        QDir(syncDir()).removeRecursively();
        return updateSyncState([](QJsonObject &s) {
            s = QJsonObject{{"dotfiles", s.value("dotfiles")}, {"skip", s.value("skip")}};
        });
    }
    if (action == "sync.auto") {
        const bool on = args.value(0).toBool() || a0 == "true" || a0 == "on";
        return updateSyncState([on](QJsonObject &s) { s.insert("auto", on); });
    }
    if (action == "sync.skip") {
        // args: part, whether it syncs
        bool known = false;
        for (const auto &[category, paths] : categories())
            known = known || category == a0;
        if (!known)
            return tr("Unknown part %1").arg(a0);
        const QVariant on = args.value(1);
        const bool syncs = on.typeId() == QMetaType::Bool ? on.toBool() : on.toString() == "true" || on.toString() == "on";
        return updateSyncState([&](QJsonObject &s) {
            QJsonArray skip = s.value("skip").toArray();
            for (int i = skip.size() - 1; i >= 0; --i)
                if (skip.at(i).toString() == a0)
                    skip.removeAt(i);
            if (!syncs)
                skip.append(a0);
            s.insert("skip", skip);
        });
    }
    if (action == "sync.dotfile.add") {
        QStringList paths{a0};
        if (a0.isEmpty()) {
            const ProcResult picked = run("omarchy-file-select", {"--title", tr("Choose a file or folder to sync"), "--multiple"}, 600000);
            if (!picked.ok())
                return {};
            paths = picked.text().split('\n', Qt::SkipEmptyParts);
        }
        const QDir home(m_paths.home);
        QStringList added;
        for (const QString &p : paths) {
            const QString absolute = QFileInfo(p).isAbsolute() ? QFileInfo(p).absoluteFilePath() : home.absoluteFilePath(p);
            const QString rel = home.relativeFilePath(absolute);
            if (const QString error = dotfileError(rel); !error.isEmpty())
                return error;
            if (!QFileInfo::exists(absolute))
                return tr("%1 doesn't exist").arg(rel);
            added << rel;
        }
        return updateSyncState([&](QJsonObject &s) {
            QJsonArray list = s.value("dotfiles").toArray();
            for (const QString &rel : added)
                if (!list.contains(rel))
                    list.append(rel);
            s.insert("dotfiles", list);
        });
    }
    if (action == "sync.dotfile.remove") {
        return updateSyncState([&](QJsonObject &s) {
            QJsonArray list = s.value("dotfiles").toArray();
            for (int i = list.size() - 1; i >= 0; --i)
                if (list.at(i).toString() == a0)
                    list.removeAt(i);
            s.insert("dotfiles", list);
        });
    }
    if (action == "sync.installed") {
        // From the install terminal: how many failed, and which.
        const int failedCount = args.value(0).toInt();
        updateSyncState([&](QJsonObject &s) {
            s.remove("installing");
            s.insert("installResult", QJsonObject{{"failed", failedCount}, {"names", args.value(1).toString()},
                                                  {"at", QDateTime::currentDateTime().toString(Qt::ISODate)}});
        });
        // Settings that needed what was just installed (a font, the
        // default agent) get another try.
        if (!syncSkipped().contains("settings") && QFileInfo(syncDir() + "/settings.json").isFile())
            importSettings(loadJson(syncDir() + "/settings.json").toObject(), true);
        return {};
    }
    return tr("Unknown action %1").arg(action);
}
