// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/dwg_converter.hpp"

#include "musacad/ui/dwg_installer.hpp"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>

namespace musacad::ui {

namespace {
constexpr int kStartTimeoutMs = 10'000;
constexpr int kRunTimeoutMs = 120'000; // big DWGs can be slow; generous ceiling

DwgConverter::Kind kind_from_basename(const QString& path) {
    const QString base = QFileInfo(path).fileName().toLower();
    if (base.contains(QStringLiteral("odafileconverter"))) {
        return DwgConverter::Kind::Oda;
    }
    if (base.contains(QStringLiteral("dwg2dxf")) || base.contains(QStringLiteral("dxf2dwg"))) {
        return DwgConverter::Kind::LibreDwg;
    }
    return DwgConverter::Kind::Generic; // a user-supplied wrapper: `<prog> <in> <out>`
}

// Run a process synchronously; true iff it started, finished normally, exit code 0.
// On the host (Flatpak opt-in) the process is flatpak-spawn --host program args...
bool run_sync(const QString& program, const QStringList& args, QString& err, bool host = false) {
    QProcess proc;
    if (host) {
        const auto [prog, argv] = DwgConverter::host_command(program, args);
        proc.start(prog, argv);
    } else {
        proc.start(program, args);
    }
    if (!proc.waitForStarted(kStartTimeoutMs)) {
        err = QStringLiteral("Could not start converter '%1': %2").arg(program, proc.errorString());
        return false;
    }
    if (!proc.waitForFinished(kRunTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(2'000);
        err = QStringLiteral("Converter timed out after %1s.").arg(kRunTimeoutMs / 1000);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString tail = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        err = QStringLiteral("Converter exited with code %1. %2").arg(proc.exitCode()).arg(tail);
        if (host) {
            err += QStringLiteral(
                "\n\nThe converter runs outside the sandbox; if that was refused, allow it once "
                "with:\n    flatpak override --user --talk-name=org.freedesktop.Flatpak org.musacad.MusaCAD");
        }
        return false;
    }
    return true;
}
// The host's answer to `command -v name` (empty when not found or not allowed).
QString host_which(const QString& name) {
    QProcess proc;
    const auto [prog, argv] = DwgConverter::host_command(
        QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("command -v %1").arg(name)});
    proc.start(prog, argv);
    if (!proc.waitForStarted(kStartTimeoutMs) || !proc.waitForFinished(kStartTimeoutMs) ||
        proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

bool host_exists(const QString& path) {
    QProcess proc;
    const auto [prog, argv] = DwgConverter::host_command(QStringLiteral("test"), {QStringLiteral("-e"), path});
    proc.start(prog, argv);
    return proc.waitForStarted(kStartTimeoutMs) && proc.waitForFinished(kStartTimeoutMs) &&
           proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

// A scratch directory the converter can reach: inside the sandbox /tmp is private, so
// host mode works under the app's cache directory (a real path on the host too).
QString scratch_base(bool host) {
    if (!host) {
        return QDir::tempPath();
    }
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    return cache;
}
} // namespace

bool DwgConverter::in_flatpak() {
    return QFileInfo::exists(QStringLiteral("/.flatpak-info")) ||
           !qEnvironmentVariable("FLATPAK_ID").isEmpty();
}

bool DwgConverter::host_mode() {
    return in_flatpak() && QSettings().value(QStringLiteral("io/dwg_host_converter"), false).toBool();
}

void DwgConverter::set_host_mode(bool on) {
    QSettings().setValue(QStringLiteral("io/dwg_host_converter"), on);
}

std::pair<QString, QStringList> DwgConverter::host_command(const QString& program,
                                                           const QStringList& args) {
    QStringList argv{QStringLiteral("--host"), program};
    argv += args;
    return {QStringLiteral("flatpak-spawn"), argv};
}

DwgConverter DwgConverter::discover_on_path() {
    if (host_mode()) {
        for (const QString& name : {QStringLiteral("ODAFileConverter"), QStringLiteral("dwg2dxf")}) {
            const QString found = host_which(name);
            if (!found.isEmpty()) {
                return DwgConverter{kind_from_basename(found), found, true};
            }
        }
        return DwgConverter{}; // None
    }
    // ODA File Converter on PATH (a few known executable names).
    for (const QString& name : {QStringLiteral("ODAFileConverter"),
                                QStringLiteral("ODAFileConverter.exe")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return DwgConverter{Kind::Oda, found};
        }
    }
#ifdef Q_OS_WIN
    // The Windows ODA installer puts the program under "Program Files\ODA\ODAFileConverter
    // <version>\" and adds nothing to PATH, so auto-detect would never find it there.
    // Newest version first (numeric-aware, so 26.12 outranks 26.5), so an upgrade wins
    // over a stale side-by-side install.
    QCollator by_version;
    by_version.setNumericMode(true);
    for (const char* root : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString base = qEnvironmentVariable(root);
        if (base.isEmpty()) {
            continue;
        }
        const QDir oda(base + QStringLiteral("/ODA"));
        QStringList versions = oda.entryList({QStringLiteral("ODAFileConverter*")},
                                             QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(versions.begin(), versions.end(),
                  [&](const QString& a, const QString& b) { return by_version.compare(a, b) > 0; });
        for (const QString& v : versions) {
            const QString exe = oda.filePath(v + QStringLiteral("/ODAFileConverter.exe"));
            if (QFileInfo::exists(exe)) {
                return DwgConverter{Kind::Oda, exe};
            }
        }
    }
#endif
    // LibreDWG dwg2dxf on PATH.
    const QString libredwg = QStandardPaths::findExecutable(QStringLiteral("dwg2dxf"));
    if (!libredwg.isEmpty()) {
        return DwgConverter{Kind::LibreDwg, libredwg};
    }
    return DwgConverter{}; // None
}

DwgConverter DwgConverter::discover_managed() {
    const QString program = OdaInstaller::installed_program();
    if (program.isEmpty()) {
        return DwgConverter{};
    }
    // The ODA converter needs an X11 display. Inside the Flatpak under Wayland the
    // sandbox has none (fallback-x11), so it runs on the host, where the session has one.
    const bool host = in_flatpak() && (host_mode() || qEnvironmentVariable("DISPLAY").isEmpty());
    return DwgConverter{Kind::Oda, program, host};
}

DwgConverter DwgConverter::discover_default() {
    const DwgConverter managed = discover_managed();
    return managed.available() ? managed : discover_on_path();
}

DwgConverter DwgConverter::from_program(const QString& path) {
    if (path.isEmpty()) {
        return DwgConverter{}; // None
    }
    if (host_mode()) {
        return host_exists(path) ? DwgConverter{kind_from_basename(path), path, true} : DwgConverter{};
    }
    if (!QFileInfo::exists(path)) {
        return DwgConverter{}; // None
    }
    return DwgConverter{kind_from_basename(path), path};
}

DwgConverter DwgConverter::discover() {
    // The explicitly configured path wins; otherwise search PATH.
    const QString configured =
        QSettings().value(QStringLiteral("io/dwg_converter_path")).toString();
    if (!configured.isEmpty()) {
        if (configured == OdaInstaller::installed_program()) {
            return discover_managed(); // the downloaded one, with its host/sandbox choice
        }
        const DwgConverter c = from_program(configured);
        if (c.available()) {
            return c;
        }
    }
    return discover_default();
}

QString DwgConverter::kind_name(Kind k) {
    switch (k) {
    case Kind::Generic:
        return QStringLiteral("custom wrapper");
    case Kind::LibreDwg:
        return QStringLiteral("LibreDWG");
    case Kind::Oda:
        return QStringLiteral("ODA File Converter");
    case Kind::None:
        break;
    }
    return QStringLiteral("none");
}

QString DwgConverter::install_hint() {
    QString hint = QStringLiteral(
        "No DWG converter found. DWG import and export run through a separate converter "
        "program (Musa CAD never bundles one -- it stays LGPL-clean). \"Download ODA File "
        "Converter\" fetches the free converter from the Open Design Alliance and sets it up; "
        "or install ODA File Converter (opendesign.com) or LibreDWG (dwg2dxf) yourself and "
        "Browse to it in DWG Setup.");
    if (in_flatpak()) {
        hint += QStringLiteral(
            "\n\nThis is the Flatpak: the sandbox cannot see programs installed on your "
            "system. To use one, allow Musa CAD to run it on the host once --\n"
            "    flatpak override --user --talk-name=org.freedesktop.Flatpak org.musacad.MusaCAD\n"
            "-- then turn on \"Use a converter installed on the host\" in DWG Setup. "
            "(Built-in DXF import and export work without any of this.)");
    }
    return hint;
}

bool DwgConverter::to_dxf(const QString& dwg_in, const QString& dxf_out, QString& err) const {
    if (kind_ == Kind::None) {
        err = install_hint();
        return false;
    }
    if (!QFileInfo::exists(dwg_in)) {
        err = QStringLiteral("Input file does not exist: %1").arg(dwg_in);
        return false;
    }
    switch (kind_) {
    case Kind::Generic:
        if (!run_sync(program_, {dwg_in, dxf_out}, err, host_)) {
            return false;
        }
        break;
    case Kind::LibreDwg:
        if (!run_sync(program_, {QStringLiteral("-y"), QStringLiteral("-o"), dxf_out, dwg_in},
                      err, host_)) {
            return false;
        }
        break;
    case Kind::Oda: {
        // ODA converts every matching file in an input DIR to an output DIR.
        QTemporaryDir in_dir(scratch_base(host_) + QStringLiteral("/dwg-in-XXXXXX"));
        QTemporaryDir out_dir(scratch_base(host_) + QStringLiteral("/dwg-out-XXXXXX"));
        if (!in_dir.isValid() || !out_dir.isValid()) {
            err = QStringLiteral("Could not create a temporary workspace.");
            return false;
        }
        const QString staged = in_dir.filePath(QStringLiteral("input.dwg"));
        if (!QFile::copy(dwg_in, staged)) {
            err = QStringLiteral("Could not stage the DWG for conversion.");
            return false;
        }
        // <inDir> <outDir> <outVer> <outType> <recurse> <audit> [filter]
        if (!run_sync(program_,
                      {in_dir.path(), out_dir.path(), QStringLiteral("ACAD2018"),
                       QStringLiteral("DXF"), QStringLiteral("0"), QStringLiteral("1"),
                       QStringLiteral("*.DWG")},
                      err, host_)) {
            return false;
        }
        const QString produced = out_dir.filePath(QStringLiteral("input.dxf"));
        if (!QFileInfo::exists(produced)) {
            err = QStringLiteral("ODA converter produced no DXF output.");
            return false;
        }
        QFile::remove(dxf_out);
        if (!QFile::copy(produced, dxf_out)) {
            err = QStringLiteral("Could not read the converted DXF.");
            return false;
        }
        break;
    }
    case Kind::None:
        return false;
    }
    if (!QFileInfo::exists(dxf_out)) {
        err = QStringLiteral("Conversion reported success but produced no DXF.");
        return false;
    }
    return true;
}

bool DwgConverter::to_dwg(const QString& dxf_in, const QString& dwg_out, const QString& version,
                          QString& err) const {
    if (kind_ == Kind::None) {
        err = install_hint();
        return false;
    }
    if (!QFileInfo::exists(dxf_in)) {
        err = QStringLiteral("Input DXF does not exist: %1").arg(dxf_in);
        return false;
    }
    switch (kind_) {
    case Kind::Generic:
        if (!run_sync(program_, {dxf_in, dwg_out}, err, host_)) {
            return false;
        }
        break;
    case Kind::LibreDwg: {
        // LibreDWG writes DWG from DXF via dxf2dwg (a sibling of dwg2dxf).
        QString prog = program_;
        prog.replace(QStringLiteral("dwg2dxf"), QStringLiteral("dxf2dwg"));
        if (!run_sync(prog, {QStringLiteral("-y"), QStringLiteral("-o"), dwg_out, dxf_in}, err, host_)) {
            return false;
        }
        break;
    }
    case Kind::Oda: {
        QTemporaryDir in_dir(scratch_base(host_) + QStringLiteral("/dwg-in-XXXXXX"));
        QTemporaryDir out_dir(scratch_base(host_) + QStringLiteral("/dwg-out-XXXXXX"));
        if (!in_dir.isValid() || !out_dir.isValid()) {
            err = QStringLiteral("Could not create a temporary workspace.");
            return false;
        }
        const QString staged = in_dir.filePath(QStringLiteral("input.dxf"));
        if (!QFile::copy(dxf_in, staged)) {
            err = QStringLiteral("Could not stage the DXF for conversion.");
            return false;
        }
        if (!run_sync(program_,
                      {in_dir.path(), out_dir.path(), version, QStringLiteral("DWG"),
                       QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("*.DXF")},
                      err, host_)) {
            return false;
        }
        const QString produced = out_dir.filePath(QStringLiteral("input.dwg"));
        if (!QFileInfo::exists(produced)) {
            err = QStringLiteral("ODA converter produced no DWG output.");
            return false;
        }
        QFile::remove(dwg_out);
        if (!QFile::copy(produced, dwg_out)) {
            err = QStringLiteral("Could not read the converted DWG.");
            return false;
        }
        break;
    }
    case Kind::None:
        return false;
    }
    if (!QFileInfo::exists(dwg_out)) {
        err = QStringLiteral("Conversion reported success but produced no DWG.");
        return false;
    }
    return true;
}

} // namespace musacad::ui
