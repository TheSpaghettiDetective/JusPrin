#!/usr/bin/env python3
"""Collect JusPrin's own interface text into its translation catalogs.

JusPrin keeps its translations apart from OrcaSlicer's, so taking a new
OrcaSlicer release never conflicts in a catalog:

  * native/JusPrin.pot and native/<lang>.po hold the text the native windows
    mark with _L, _u8L, L, _L_CONTEXT and _L_PLURAL under src/slic3r/GUI/JusPrin.
    Text OrcaSlicer's own template already has is left out: its translations
    come from upstream. Brand/brand_catalogs.py merges these into the catalog
    the app loads.
  * pages/<lang>.po hold the text of the AI and Home pages, marked with the
    Lingui macros (t, Trans, Plural). Lingui's own extractor writes them.

Run it after marking or changing text, then translate the new entries:

    python3 src/slic3r/GUI/JusPrin/Localization/update_catalogs.py

Needs GNU gettext (xgettext, msgmerge, msgcat, msgcomm) and the pages'
node_modules (npm ci in AgentUI).
"""

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
JUSPRIN = HERE.parent
ROOT = JUSPRIN.parents[3]
UPSTREAM_I18N = ROOT / "localization" / "i18n"
NATIVE = HERE / "native"
PAGES = HERE / "pages"

# The keywords upstream's scripts/run_gettext.sh passes, translated_plural
# from Support/Translate.hpp, plural_title from Agent/ActivityTitle.hpp and
# plural_words from Workspace/SettingDisplay.hpp.
XGETTEXT_KEYWORDS = [
    "--keyword=L", "--keyword=_L", "--keyword=_u8L",
    "--keyword=L_CONTEXT:1,2c", "--keyword=_L_CONTEXT:1,2c", "--keyword=_u8L_CONTEXT:1,2c",
    "--keyword=_L_PLURAL:1,2", "--keyword=translated_plural:1,2", "--keyword=plural_title:1,2", "--keyword=plural_words:1,2",
]
# Test doubles and the page projects are not native interface text.
SKIP_DIRS = {"Testing", "AgentUI", "HomeUI", "WebShared", "node_modules", "Localization"}

DATE = re.compile(r'^"POT-Creation-Date: .*\\n"\n', re.M)


def languages():
    data = json.loads((HERE / "languages.json").read_text(encoding="utf-8"))
    return [l["code"] for l in data["languages"] if l["code"] != data["source"]]


def run(*args, **kw):
    subprocess.run(list(args), check=True, **kw)


def keep_if_only_date_changed(path, before):
    """Leave a file as it was when only its creation date moved."""
    if before is not None and path.exists() and DATE.sub("", path.read_text(encoding="utf-8")) == DATE.sub("", before):
        path.write_text(before, encoding="utf-8")


def native_sources():
    files = []
    for f in sorted(JUSPRIN.rglob("*")):
        if f.suffix in (".cpp", ".hpp") and not SKIP_DIRS.intersection(f.relative_to(JUSPRIN).parts):
            files.append(str(f.relative_to(ROOT)))
    return files


def upstream_header_line(lang, name):
    po = UPSTREAM_I18N / lang / f"OrcaSlicer_{lang}.po"
    m = re.search(rf'^"{name}: (.*)\\n"$', po.read_text(encoding="utf-8"), re.M)
    return m.group(1) if m else None


def update_native():
    NATIVE.mkdir(exist_ok=True)
    pot = NATIVE / "JusPrin.pot"
    before = pot.read_text(encoding="utf-8") if pot.exists() else None
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        listing = tmp / "files.txt"
        listing.write_text("\n".join(native_sources()) + "\n", encoding="utf-8")
        everything = tmp / "all.pot"
        run("xgettext", *XGETTEXT_KEYWORDS, "--add-comments=TRN", "--from-code=UTF-8", "--no-location",
            "--boost", "--no-wrap", "--package-name=JusPrin", "-f", str(listing), "-o", str(everything), cwd=ROOT)
        # Entries OrcaSlicer's template also has keep upstream's translations.
        shared = tmp / "shared.pot"
        run("msgcomm", "--more-than=1", "--no-wrap", "-o", str(shared), str(everything),
            str(UPSTREAM_I18N / "OrcaSlicer.pot"))
        if shared.exists():
            run("msgcat", "--unique", "--no-wrap", "-o", str(pot), str(everything), str(shared))
        else:
            shutil.copy(everything, pot)
    text = pot.read_text(encoding="utf-8").replace("charset=CHARSET", "charset=UTF-8")
    pot.write_text(text, encoding="utf-8")
    keep_if_only_date_changed(pot, before)

    for lang in languages():
        po = NATIVE / f"{lang}.po"
        before = po.read_text(encoding="utf-8") if po.exists() else None
        if not po.exists():
            # A new language starts as the template's header alone, with
            # upstream's plural rule; msgmerge then adds every entry with as
            # many plural forms as that rule has.
            header = pot.read_text(encoding="utf-8").split("\n\n", 1)[0] + "\n"
            plural = upstream_header_line(lang, "Plural-Forms") or "nplurals=2; plural=(n != 1);"
            header = header.replace('"Language: \\n"', f'"Language: {lang}\\n"')
            header = re.sub(r'"Plural-Forms: .*\\n"', lambda _: f'"Plural-Forms: {plural}\\n"', header)
            header = header.replace("#, fuzzy\nmsgid \"\"", "msgid \"\"", 1)
            po.write_text(header, encoding="utf-8")
        run("msgmerge", "--quiet", "-N", "--no-wrap", "--previous", "-U", "--backup=none", str(po), str(pot))
        keep_if_only_date_changed(po, before)


def update_pages():
    before = {p: p.read_text(encoding="utf-8") for p in PAGES.glob("*.po")}
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if npx is None:
        sys.exit("update_catalogs: npx not found; the page catalogs need Node")
    run(npx, "lingui", "extract", "--clean", cwd=JUSPRIN / "AgentUI")
    for path, text in before.items():
        keep_if_only_date_changed(path, text)


def main():
    for tool in ("xgettext", "msgmerge", "msgcat", "msgcomm"):
        if shutil.which(tool) is None:
            sys.exit(f"update_catalogs: {tool} not found; install GNU gettext")
    update_native()
    update_pages()
    print("update_catalogs: native/JusPrin.pot, native/<lang>.po and pages/<lang>.po are up to date")


if __name__ == "__main__":
    main()
