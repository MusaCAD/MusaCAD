// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/dwg_converter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

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
bool run_sync(const QString& program, const QStringList& args, QString& err) {
    QProcess proc;
    proc.start(program, args);
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
        return false;
    }
    return true;
}
} // namespace

DwgConverter DwgConverter::discover_on_path() {
    // ODA File Converter on PATH (a few known executable names).
    for (const QString& name : {QStringLiteral("ODAFileConverter"),
                                QStringLiteral("ODAFileConverter.exe")}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return DwgConverter{Kind::Oda, found};
        }
    }
    // LibreDWG dwg2dxf on PATH.
    const QString libredwg = QStandardPaths::findExecutable(QStringLiteral("dwg2dxf"));
    if (!libredwg.isEmpty()) {
        return DwgConverter{Kind::LibreDwg, libredwg};
    }
    return DwgConverter{}; // None
}

DwgConverter DwgConverter::from_program(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return DwgConverter{}; // None
    }
    return DwgConverter{kind_from_basename(path), path};
}

DwgConverter DwgConverter::discover() {
    // The explicitly configured path wins; otherwise search PATH.
    const QString configured =
        QSettings().value(QStringLiteral("io/dwg_converter_path")).toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        return DwgConverter{kind_from_basename(configured), configured};
    }
    return discover_on_path();
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
    return QStringLiteral(
        "No DWG converter found. DWG support needs an external converter (Musa CAD never "
        "bundles one -- it stays LGPL-clean). Install the free ODA File Converter "
        "(opendesign.com) or LibreDWG (dwg2dxf), then use the \"DWG Setup\" button to "
        "Browse to it or auto-detect it on your PATH.");
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
        if (!run_sync(program_, {dwg_in, dxf_out}, err)) {
            return false;
        }
        break;
    case Kind::LibreDwg:
        if (!run_sync(program_, {QStringLiteral("-y"), QStringLiteral("-o"), dxf_out, dwg_in},
                      err)) {
            return false;
        }
        break;
    case Kind::Oda: {
        // ODA converts every matching file in an input DIR to an output DIR.
        QTemporaryDir in_dir;
        QTemporaryDir out_dir;
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
                      err)) {
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
        if (!run_sync(program_, {dxf_in, dwg_out}, err)) {
            return false;
        }
        break;
    case Kind::LibreDwg: {
        // LibreDWG writes DWG from DXF via dxf2dwg (a sibling of dwg2dxf).
        QString prog = program_;
        prog.replace(QStringLiteral("dwg2dxf"), QStringLiteral("dxf2dwg"));
        if (!run_sync(prog, {QStringLiteral("-y"), QStringLiteral("-o"), dwg_out, dxf_in}, err)) {
            return false;
        }
        break;
    }
    case Kind::Oda: {
        QTemporaryDir in_dir;
        QTemporaryDir out_dir;
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
                      err)) {
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
