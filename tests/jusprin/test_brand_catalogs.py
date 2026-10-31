#!/usr/bin/env python3
"""The product name in the catalogs the app loads (Brand/brand_catalogs.py).

  python3 tests/jusprin/test_brand_catalogs.py
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "src/slic3r/GUI/JusPrin/Brand"))
import brand_catalogs


def translated(msgid, msgstr):
    entries = [{"msgid": msgid, "msgstr": {0: msgstr}}]
    messages, _ = brand_catalogs.catalog_messages(entries, "JusPrin", english=False)
    return messages[msgid.encode()].decode()


class BareName(unittest.TestCase):
    def test_a_warning_says_the_product(self):
        self.assertEqual(translated("Orca will automatically cap the jerk.", "Orca will automatically cap the jerk."),
                         "JusPrin will automatically cap the jerk.")

    def test_a_translation_with_no_space_after_the_name(self):
        self.assertEqual(translated("Orca will cap it.", "Orca将自动限制"), "JusPrin将自动限制")

    def test_the_long_names_still_change(self):
        self.assertEqual(translated("Orca Slicer and OrcaSlicer", "Orca Slicer und OrcaSlicer"), "JusPrin und JusPrin")

    def test_things_named_orca_keep_their_name(self):
        for msgid, msgstr in (("your Orca Cloud profile", "ihr Orca Cloud Profil"), ("Orca Cube", "Куб Orca"),
                              ("Orca String Hell", "«Струнный ад» Orca"), ("Orca Tolerance Test", "Тест точности Orca")):
            self.assertEqual(translated(msgid, msgstr), msgstr)

    def test_words_that_only_start_with_orca_keep_their_name(self):
        self.assertEqual(translated("Pull from OrcaCloud", "Von OrcaCloud laden"), "Von OrcaCloud laden")

    def test_attribution_keeps_the_upstream_name(self):
        msgid = "Orca Slicer is based on PrusaSlicer and BambuStudio"
        self.assertEqual(translated(msgid, msgid), msgid)

    def test_an_untranslated_string_naming_the_product_falls_back_rebranded(self):
        entries = [{"msgid": "Orca can preheat it.", "msgstr": {0: ""}}, {"msgid": "Orca Cube", "msgstr": {0: ""}}]
        messages, _ = brand_catalogs.catalog_messages(entries, "JusPrin", english=False)
        self.assertEqual(messages, {b"Orca can preheat it.": b"JusPrin can preheat it."})


if __name__ == "__main__":
    unittest.main()
