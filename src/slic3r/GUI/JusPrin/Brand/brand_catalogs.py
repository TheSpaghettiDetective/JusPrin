#!/usr/bin/env python3
"""Build JusPrin translation catalogs from OrcaSlicer's .po files.

OrcaSlicer names itself in about 85 user-facing strings and in every
translation of them. Rather than editing those source strings (and the
translation macros) in upstream files, this script produces the catalogs the
application loads, with the product name substituted:

  * one <lang>/<domain>.mo per upstream .po, msgstr rewritten; entries whose
    msgstr is empty but whose msgid names the product get the substituted
    English text so the fallback also reads correctly;
  * an English catalog for the source strings in the .pot that name the
    product, so the untranslated UI switches too.

Attribution strings that must keep naming OrcaSlicer are excluded.
The .mo writer is self-contained so the build does not need gettext.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

PRODUCT_PATTERN = re.compile(r"Orca ?Slicer")

# msgids that are attribution and must keep the upstream name.
KEEP_UPSTREAM_NAME = (
    "Open-source slicing stands on a tradition",
    "OrcaSlicer began in that same spirit",
    "Today, OrcaSlicer is the most widely used",
    "Orca Slicer is based on PrusaSlicer",
)


def unescape(s):
    return (s.replace(r"\n", "\n").replace(r"\t", "\t").replace(r"\"", '"').replace(r"\\", "\\"))


def parse_po(path):
    """Yield dicts with msgctxt, msgid, msgid_plural, msgstr (list), fuzzy."""
    entries, cur, field, fuzzy = [], {}, None, False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("#~"):
            continue  # obsolete
        if line.startswith("#"):
            if line.startswith("#,") and "fuzzy" in line:
                fuzzy = True
            continue
        if not line:
            if cur:
                cur["fuzzy"] = fuzzy
                entries.append(cur)
            cur, field, fuzzy = {}, None, False
            continue
        m = re.match(r'^(msgctxt|msgid_plural|msgid|msgstr(?:\[(\d+)\])?)\s+"(.*)"$', line)
        if m:
            key, idx, text = m.group(1), m.group(2), unescape(m.group(3))
            if key.startswith("msgstr"):
                cur.setdefault("msgstr", {})[int(idx or 0)] = text
                field = ("msgstr", int(idx or 0))
            else:
                cur[key] = text
                field = key
            continue
        m = re.match(r'^"(.*)"$', line)
        if m and field:
            text = unescape(m.group(1))
            if isinstance(field, tuple):
                cur["msgstr"][field[1]] += text
            else:
                cur[field] += text
    if cur:
        cur["fuzzy"] = fuzzy
        entries.append(cur)
    return entries


def substitute(text, product):
    return PRODUCT_PATTERN.sub(product, text)


def keep_upstream(msgid):
    return any(msgid.startswith(prefix) for prefix in KEEP_UPSTREAM_NAME)


def write_mo(path, messages, header):
    """messages: {key bytes: value bytes}. Standard GNU .mo layout, no hash table."""
    items = sorted(messages.items())
    items.insert(0, (b"", header.encode("utf-8")))
    n = len(items)
    ids = b"".join(k + b"\0" for k, _ in items)
    strs = b"".join(v + b"\0" for _, v in items)
    keystart = 7 * 4 + 16 * n
    valuestart = keystart + len(ids)
    koffsets, voffsets, ko, vo = [], [], 0, 0
    for k, v in items:
        koffsets += [len(k), keystart + ko]; ko += len(k) + 1
        voffsets += [len(v), valuestart + vo]; vo += len(v) + 1
    out = struct.pack("Iiiiiii", 0x950412DE, 0, n, 7 * 4, 7 * 4 + n * 8, 0, 0)
    out += struct.pack("%di" % len(koffsets), *koffsets)
    out += struct.pack("%di" % len(voffsets), *voffsets)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out + ids + strs)


def catalog_messages(entries, product, english):
    messages, header = {}, "Content-Type: text/plain; charset=UTF-8\n"
    for e in entries:
        msgid = e.get("msgid")
        if msgid is None or e.get("fuzzy"):
            continue
        if msgid == "":
            header = e.get("msgstr", {}).get(0, header)
            if "charset=" not in header:
                header += "Content-Type: text/plain; charset=UTF-8\n"
            continue
        names_product = bool(PRODUCT_PATTERN.search(msgid) or PRODUCT_PATTERN.search(e.get("msgid_plural", "")))
        msgstr = dict(e.get("msgstr", {}))
        if not any(msgstr.values()):
            if not (english or names_product):
                continue  # untranslated and nothing to substitute
            msgstr = {0: msgid}
            if "msgid_plural" in e:
                msgstr[1] = e["msgid_plural"]
        if not keep_upstream(msgid):
            msgstr = {i: substitute(t, product) for i, t in msgstr.items()}
        elif english:
            continue
        key = msgid
        if "msgctxt" in e:
            key = e["msgctxt"] + "\x04" + key
        if "msgid_plural" in e:
            key += "\0" + e["msgid_plural"]
            value = "\0".join(msgstr[i] for i in sorted(msgstr))
        else:
            value = msgstr.get(0, "")
        if value:
            messages[key.encode("utf-8")] = value.encode("utf-8")
    return messages, header


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pot", required=True, type=Path)
    ap.add_argument("--po-dir", required=True, type=Path, help="localization/i18n")
    ap.add_argument("--out-dir", required=True, type=Path, help="resources/i18n")
    ap.add_argument("--domain", required=True, help="catalog file stem, the SLIC3R_APP_KEY")
    ap.add_argument("--product", required=True)
    ap.add_argument("--stamp", type=Path)
    args = ap.parse_args()

    written = 0
    pot_entries = [e for e in parse_po(args.pot) if e.get("msgid")]
    for po in sorted(args.po_dir.glob("*/OrcaSlicer_*.po")):
        lang = po.parent.name
        entries = parse_po(po)
        messages, header = catalog_messages(entries, args.product, english=False)
        if lang == "en":
            en_messages, _ = catalog_messages(pot_entries, args.product, english=True)
            en_messages.update(messages)
            messages = en_messages
        write_mo(args.out_dir / lang / f"{args.domain}.mo", messages, header)
        written += 1
    if not (args.out_dir / "en" / f"{args.domain}.mo").exists():
        messages, header = catalog_messages(pot_entries, args.product, english=True)
        write_mo(args.out_dir / "en" / f"{args.domain}.mo", messages, header)
        written += 1
    if args.stamp:
        args.stamp.parent.mkdir(parents=True, exist_ok=True)
        args.stamp.write_text(f"{written} catalogs\n")
    print(f"brand_catalogs: wrote {written} catalogs as {args.domain}.mo")
    return 0


if __name__ == "__main__":
    sys.exit(main())
