# Security policy

## Supported versions

Security fixes target `main` and the [latest stable release](https://github.com/MusaCAD/MusaCAD/releases/latest).
Older versions may not receive the fix. Experimental and preview builds
are still under development; reports about them are welcome, but they should not be
assumed to have stable-release support.

## Report a vulnerability

Use [Report a vulnerability](https://github.com/MusaCAD/MusaCAD/security/advisories/new)
to send a private report, or email [pranay@weberq.in](mailto:pranay@weberq.in).
Please do not publish exploit details or malicious drawing files in public issues or
pull requests before a fix and public announcement can be agreed.

Include as much of the following as is available:

- Version/build, operating system, architecture, and installation source.
- Reproduction steps and a minimal sample or proof of concept.
- The affected file format, command, converter, or update/package operation.
- The possible impact and relevant logs, with private information removed.

Reports may concern opening drawings, scripting, file handling, running converters,
updates, or installation packages. DWG conversion uses separate programs;
include the converter name and version if it is involved. Third-party vulnerabilities
may also need to be reported to the relevant upstream project.

Share only files you have permission to share. Use a small example drawing instead
of a private customer file where possible. Test with copies and keep original
project files backed up.

## What happens next

The maintainer reviews reports and asks for any details needed to reproduce the
problem. Progress and plans for a public announcement are discussed privately with
the reporter.
There is no fixed response or repair deadline. Timing depends on the problem, the
information available, and the time available to work on it. Reporters are credited
with their permission when a fix or advisory is published.

For ordinary defects, use the [issue tracker](https://github.com/MusaCAD/MusaCAD/issues).
For the website, wiki, or fabrication tools, the same private reporting link and email
can be used; identify the affected repository or URL.
