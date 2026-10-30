"""Unit test for prefix_check.py on hand-written request logs.

Run: python3 -m unittest tests/project_prompt/test_prefix_check.py
"""

import pathlib, sys, tempfile, unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import prefix_check  # noqa: E402


class PrefixCheckTest(unittest.TestCase):
    def check(self, *requests, **options):
        with tempfile.TemporaryDirectory() as directory:
            for n, (chat, body) in enumerate(requests, 1):
                pathlib.Path(directory, "%04d-request.json" % n).write_bytes(body)
                pathlib.Path(directory, "%04d-usage.txt" % n).write_text(chat + "\n{}\n")
            return prefix_check.check(directory, **options)

    def test_names_the_first_difference_per_chat(self):
        self.assertEqual(self.check(
            ("p c-1", b'{"input":["a"],"tools":[]}'),
            ("p c-2", b'{"input":["z"],"tools":[]}'),  # another chat: not paired with c-1
            ("p c-1", b'{"input":["a"],"title":"t"}'),  # a title request: no tools
            ("p c-1", b'{"input":["a","b"],"tools":[]}'),
            ("p c-1", b'{"input":["x","b"],"tools":[]}'),
            ("p c-1", b'{"input":["x","b"],"tools":[]}'),
        ), ["p c-1 0001 -> 0004: first difference at byte 13, $.input[1]",
            "p c-1 0004 -> 0005: first difference at byte 11, $.input[0]",
            "p c-1 0005 -> 0006: identical"])

    def test_ignores_the_moving_cache_breakpoint_when_asked(self):
        earlier = b'{"messages":[{"cache_control":{"type":"ephemeral"},"text":"a"}],"tools":[]}'
        later = b'{"messages":[{"text":"a"},{"cache_control":{"type":"ephemeral"},"text":"b"}],"tools":[]}'
        self.assertEqual(self.check(("p c", earlier), ("p c", later)),
                         ["p c 0001 -> 0002: first difference at byte 15, $.messages[0].cache_control"])
        self.assertEqual(self.check(("p c", earlier), ("p c", later), ignore_cache_control=True),
                         ["p c 0001 -> 0002: first difference at byte 25, $.messages[1]"])


if __name__ == "__main__":
    unittest.main()
