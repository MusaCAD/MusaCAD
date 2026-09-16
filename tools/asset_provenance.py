#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
"""Report the content credential (C2PA) embedded in an SVG or PNG, if any: who made the
file and whether an AI step is recorded. Run it on a logo or screenshot before a Flathub
submission -- reviewers read the same record.

    tools/asset_provenance.py assets/branding/musacad_logo.svg
"""
import base64
import re
import sys


def blob(path: str) -> bytes:
    data = open(path, "rb").read()
    if path.lower().endswith(".svg"):
        m = re.search(rb"<c2pa:manifest[^>]*>([^<]+)</c2pa:manifest>", data)
        if not m:
            return b""
        b64 = m.group(1).strip()
        return base64.b64decode(b64 + b"=" * (-len(b64) % 4))
    return data  # PNG / JPEG: the JUMBF box is inside the file as-is


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    raw = blob(sys.argv[1])
    if b"c2pa" not in raw:
        print("no content credential (C2PA) found")
        return 0
    text = raw.decode("latin-1")
    found = False

    def cbor_text_after(key: str) -> list[str]:
        # A CBOR text string carries its length in the byte before it (0x60 + n, n < 24).
        out = []
        for m in re.finditer(re.escape(key), text):
            i = m.end()
            if i < len(text) and 0x60 <= ord(text[i]) <= 0x77:
                n = ord(text[i]) - 0x60
                out.append(text[i + 1:i + 1 + n])
        return sorted(set(out))

    for label, hits in (
        ("software agent", cbor_text_after("softwareAgent")),
        ("digital source type", sorted(set(re.findall(r"digitalsourcetype/([A-Za-z]+)", text)))),
        ("action", sorted(set(re.findall(r"(c2pa\.(?:created|edited|placed|converted|opened|published))", text)))),
        ("claim generator", cbor_text_after("name")),
        ("signer", sorted({"".join(g for g in m if g)
                           for m in re.findall(r"CN=([A-Za-z0-9 ._-]{2,40})|\x13\x05(Canva)|\x0c\x05(Adobe)", text)})),
    ):
        if hits:
            found = True
            print(f"{label:20s} {', '.join(hits)}")
    if not found:
        print("a content credential is present but its fields were not recognised")
    if re.search(r"TrainedAlgorithmicMedia|trainedAlgorithmicMedia", text):
        print("=> the credential records an AI (trained algorithmic media) step")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
