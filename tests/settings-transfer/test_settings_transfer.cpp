/*
 * Unit tests for KisSettingsTransferCore (patch 08), built against the real
 * patched Krita source by .github/workflows/tests.yml.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisSettingsTransferCore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <KConfig>
#include <KConfigGroup>

#include <cstdio>

namespace Core = KisSettingsTransferCore;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        ++g_checks;                                                                 \
        const auto _va = (a);                                                       \
        const auto _vb = (b);                                                       \
        if (!(_va == _vb)) {                                                        \
            ++g_failures;                                                           \
            QString _sa, _sb;                                                       \
            QDebug(&_sa) << _va;                                                    \
            QDebug(&_sb) << _vb;                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n   got %s\n   expected %s\n", \
                         __FILE__, __LINE__, #a, #b, qPrintable(_sa), qPrintable(_sb)); \
        }                                                                           \
    } while (0)

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

static QString readKey(const QString &kritarc, const QString &group, const QString &key)
{
    KConfig cfg(kritarc, KConfig::SimpleConfig);
    return KConfigGroup(&cfg, group).readEntry(key, QStringLiteral("<missing>"));
}

static bool hasGroup(const QString &kritarc, const QString &group)
{
    KConfig cfg(kritarc, KConfig::SimpleConfig);
    return cfg.hasGroup(group);
}

/// Simulates an export: entries + file contents, as the zip would carry them
static QHash<QString, QByteArray> pack(const QVector<Core::Entry> &entries,
                                       const QString &configDir, const QString &resourceDir)
{
    QHash<QString, QByteArray> files;
    for (const Core::Entry &e : entries) {
        files.insert(Core::archivePath(e), readFile(Core::localPath(e, configDir, resourceDir)));
    }
    return files;
}

static void testAllowedEntries()
{
    using E = Core::Entry;
    CHECK(Core::isAllowedEntry(E{"config", "kritarc"}));
    CHECK(Core::isAllowedEntry(E{"config", "kritashortcutsrc"}));
    CHECK(Core::isAllowedEntry(E{"config", "kritadisplayrc"}));
    CHECK(Core::isAllowedEntry(E{"config", "klanguageoverridesrc"}));
    CHECK(Core::isAllowedEntry(E{"resources", "krita5.xmlgui"}));
    CHECK(Core::isAllowedEntry(E{"resources", "input/kritadefault.profile"}));
    CHECK(Core::isAllowedEntry(E{"resources", "input/my-touch_profile.2.profile"}));

    CHECK(!Core::isAllowedEntry(E{"config", "evilrc"}));
    CHECK(!Core::isAllowedEntry(E{"config", "../kritarc"}));
    CHECK(!Core::isAllowedEntry(E{"config", "/etc/passwd"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "input/../../kritarc.profile"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "input/sub/x.profile"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "input/x.sh"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "brushes/x.kpp"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "C:/x.profile"}));
    CHECK(!Core::isAllowedEntry(E{"resources", "input\\x.profile"}));
    CHECK(!Core::isAllowedEntry(E{"elsewhere", "kritarc"}));
    CHECK(!Core::isAllowedEntry(E{"config", ""}));
}

static void testManifest()
{
    const QVector<Core::Entry> entries{{"config", "kritarc"}, {"resources", "input/a.profile"}};
    const QByteArray json = Core::createManifest(entries, "5.4.0-test", "android");

    QVector<Core::Entry> parsed;
    QJsonObject info;
    QString error;
    CHECK(Core::parseManifest(json, &parsed, &info, &error));
    CHECK_EQ(parsed.size(), 2);
    CHECK(parsed == entries);
    CHECK_EQ(info.value("platform").toString(), QString("android"));
    CHECK_EQ(info.value("kritaVersion").toString(), QString("5.4.0-test"));
    CHECK(!info.value("created").toString().isEmpty());

    // wrong format id
    QJsonObject o = QJsonDocument::fromJson(json).object();
    o["format"] = "something-else";
    CHECK(!Core::parseManifest(QJsonDocument(o).toJson(), &parsed, &info, &error));

    // future version
    o = QJsonDocument::fromJson(json).object();
    o["version"] = Core::FORMAT_VERSION + 1;
    CHECK(!Core::parseManifest(QJsonDocument(o).toJson(), &parsed, &info, &error));

    // path traversal hidden in the manifest
    o = QJsonDocument::fromJson(json).object();
    QJsonArray files = o["files"].toArray();
    files.append(QJsonObject{{"location", "config"}, {"path", "../../.bashrc"}});
    o["files"] = files;
    CHECK(!Core::parseManifest(QJsonDocument(o).toJson(), &parsed, &info, &error));
    CHECK(error.contains(".bashrc"));

    CHECK(!Core::parseManifest("not json", &parsed, &info, &error));
}

static void testRoundTrip()
{
    QTemporaryDir root;
    CHECK(root.isValid());

    // ---- device A (the one exporting) ----
    const QString cfgA = root.filePath("A/config");
    const QString appA = root.filePath("A/appdata");
    const QString resA = root.filePath("A/custom-resources");
    writeFile(cfgA + "/kritarc",
              "ResourceDirectory=" + resA.toUtf8() + "\n"
              "\n[General]\nfavoriteSetting=from-A\ncanvasColor=dark\n"
              "\n[RecentFiles]\nFile1=/home/a/drawing.kra\n"
              "\n[File Dialogs]\nOpenDocument=/home/a\n");
    writeFile(cfgA + "/kritashortcutsrc", "[Shortcuts]\nerase_action=E\n");
    writeFile(resA + "/krita5.xmlgui", "<gui name=\"Krita\" version=\"546\"/>\n");
    writeFile(resA + "/input/touch.profile", "[General]\nname=Touch\n");
    writeFile(resA + "/input/ignored.txt", "not a profile");

    CHECK_EQ(Core::resolveResourceDir(cfgA, appA), QDir::cleanPath(resA));

    const QVector<Core::Entry> exported = Core::collectEntries(cfgA, Core::resolveResourceDir(cfgA, appA));
    CHECK_EQ(exported.size(), 4); // kritarc, kritashortcutsrc, krita5.xmlgui, input/touch.profile
    CHECK(exported.contains(Core::Entry{"config", "kritarc"}));
    CHECK(exported.contains(Core::Entry{"config", "kritashortcutsrc"}));
    CHECK(exported.contains(Core::Entry{"resources", "krita5.xmlgui"}));
    CHECK(exported.contains(Core::Entry{"resources", "input/touch.profile"}));
    const QHash<QString, QByteArray> files = pack(exported, cfgA, Core::resolveResourceDir(cfgA, appA));

    // ---- device B (has its own settings and a default resource folder) ----
    const QString cfgB = root.filePath("B/config");
    const QString appB = root.filePath("B/appdata");
    writeFile(cfgB + "/kritarc",
              "\n[General]\nfavoriteSetting=from-B\nonlyOnB=1\n"
              "\n[RecentFiles]\nFile1=/sdcard/b.kra\n");
    writeFile(cfgB + "/kritadisplayrc", "[General]\nOpenGLRenderer=auto\n");
    const QString resB = Core::resolveResourceDir(cfgB, appB);
#ifndef Q_OS_ANDROID
    CHECK_EQ(resB, QDir::cleanPath(appB + "/krita"));
#endif
    writeFile(resB + "/input/touch.profile", "[General]\nname=Old touch\n");
    writeFile(resB + "/input/local-only.profile", "[General]\nname=Local\n");

    const QString backup = root.filePath("B/backup");
    QStringList log;
    QString error;
    CHECK(Core::applyFiles(exported, files, cfgB, appB, backup, &log, &error));
    CHECK(error.isEmpty());

    const QString kritarcB = cfgB + "/kritarc";
    CHECK_EQ(readKey(kritarcB, "General", "favoriteSetting"), QString("from-A"));   // imported
    CHECK_EQ(readKey(kritarcB, "General", "canvasColor"), QString("dark"));          // imported
    CHECK_EQ(readKey(kritarcB, "General", "onlyOnB"), QString("<missing>"));         // kritarc replaced
    CHECK_EQ(readKey(kritarcB, QString(), "ResourceDirectory"), QString("<missing>")); // B had none: kept none
    CHECK_EQ(readKey(kritarcB, "RecentFiles", "File1"), QString("/sdcard/b.kra"));   // B's recent files kept
    CHECK(!hasGroup(kritarcB, "File Dialogs"));                                       // A's paths not imported

    CHECK_EQ(readFile(cfgB + "/kritashortcutsrc"), files.value("config/kritashortcutsrc"));
    CHECK_EQ(readFile(cfgB + "/kritadisplayrc"), QByteArray("[General]\nOpenGLRenderer=auto\n")); // not in archive: untouched
    CHECK_EQ(readFile(resB + "/krita5.xmlgui"), files.value("resources/krita5.xmlgui"));
    CHECK_EQ(readFile(resB + "/input/touch.profile"), files.value("resources/input/touch.profile"));
    CHECK_EQ(readFile(resB + "/input/local-only.profile"), QByteArray("[General]\nname=Local\n")); // kept

    // backup holds B's previous files
    CHECK(readFile(backup + "/config/kritarc").contains("from-B"));
    CHECK(readFile(backup + "/config/kritadisplayrc").contains("OpenGLRenderer"));
    CHECK_EQ(readFile(backup + "/resources/input/touch.profile"), QByteArray("[General]\nname=Old touch\n"));
    CHECK(!QFile::exists(cfgB + "/kritarc.import-tmp"));

    // ---- device C: fresh install, keeps no foreign device-specific entries ----
    const QString cfgC = root.filePath("C/config");
    const QString appC = root.filePath("C/appdata");
    CHECK(Core::applyFiles(exported, files, cfgC, appC, root.filePath("C/backup"), &log, &error));
    const QString kritarcC = cfgC + "/kritarc";
    CHECK_EQ(readKey(kritarcC, "General", "favoriteSetting"), QString("from-A"));
    CHECK_EQ(readKey(kritarcC, QString(), "ResourceDirectory"), QString("<missing>")); // A's path dropped
    CHECK(!hasGroup(kritarcC, "RecentFiles"));
    CHECK(QFile::exists(Core::resolveResourceDir(cfgC, appC) + "/input/touch.profile"));

    // ---- device D: has its own custom resource folder, which must be kept ----
    const QString cfgD = root.filePath("D/config");
    const QString resD = root.filePath("D/my-resources");
    writeFile(cfgD + "/kritarc", "ResourceDirectory=" + resD.toUtf8() + "\n\n[General]\nx=1\n");
    CHECK(Core::applyFiles(exported, files, cfgD, root.filePath("D/appdata"), root.filePath("D/backup"), &log, &error));
    CHECK_EQ(readKey(cfgD + "/kritarc", QString(), "ResourceDirectory"), resD);
    CHECK(QFile::exists(resD + "/krita5.xmlgui"));

    // ---- incomplete archive: nothing is written ----
    QHash<QString, QByteArray> incomplete = files;
    incomplete.remove("resources/krita5.xmlgui");
    const QString cfgE = root.filePath("E/config");
    writeFile(cfgE + "/kritarc", "[General]\nuntouched=yes\n");
    CHECK(!Core::applyFiles(exported, incomplete, cfgE, root.filePath("E/appdata"), root.filePath("E/backup"), &log, &error));
    CHECK(error.contains("krita5.xmlgui"));
    CHECK_EQ(readKey(cfgE + "/kritarc", "General", "untouched"), QString("yes"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testAllowedEntries();
    testManifest();
    testRoundTrip();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
