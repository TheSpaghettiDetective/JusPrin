#!/usr/bin/env python3
"""Where each request first differs from the chat's previous one.

Reads a --request-log directory (tests/jusprin_support/RequestLog.hpp) and, for
consecutive bodies of one chat, prints the first differing byte and the JSON
path there. Title requests carry no "tools", so they are paired only with each
other. --ignore-cache-control drops Anthropic's breakpoint, which moves to the
newest user content on every request, before comparing.

Usage: python3 tests/project_prompt/prefix_check.py LOG_DIR [--ignore-cache-control]
"""

import json, pathlib, re, sys


def common_prefix(a, b):
    lo, hi = 0, min(len(a), len(b))  # by halving, as slices compare at C speed
    while lo < hi:
        mid = (lo + hi + 1) // 2
        lo, hi = (mid, hi) if a[:mid] == b[:mid] else (lo, mid - 1)
    return lo


def first_path(a, b, path="$"):
    """The path of the first value where `a` and `b` differ. JusPrin writes keys
    sorted, so of two different keys the smaller comes first."""
    if isinstance(a, dict) and isinstance(b, dict):
        for (ka, va), (kb, vb) in zip(a.items(), b.items()):
            found = path + "." + min(ka, kb) if ka != kb else first_path(va, vb, path + "." + ka)
            if found:
                return found
        return None if len(a) == len(b) else path
    if isinstance(a, list) and isinstance(b, list):
        for i, (x, y) in enumerate(zip(a, b)):
            if found := first_path(x, y, "%s[%d]" % (path, i)):
                return found
        return None if len(a) == len(b) else "%s[%d]" % (path, min(len(a), len(b)))
    return None if a == b else path


def check(directory, ignore_cache_control=False):
    previous, lines = {}, []
    for body_file in sorted(pathlib.Path(directory).glob("[0-9][0-9][0-9][0-9]-request.json")):
        body = body_file.read_bytes()
        if ignore_cache_control:
            body = re.sub(rb'"cache_control":\{[^{}]*\},?', b"", body)
        chat = (body_file.with_name(body_file.name[:4] + "-usage.txt").read_text("utf-8").splitlines() or [""])[0]
        key = (chat, b'"tools":' in body)
        if key in previous:
            name, earlier = previous[key]
            shared = common_prefix(earlier, body)
            where = "identical" if earlier == body else "first difference at byte %d, %s" % (
                shared, first_path(json.loads(earlier), json.loads(body)))
            lines.append("%s %s -> %s: %s" % (chat, name, body_file.name[:4], where))
        previous[key] = (body_file.name[:4], body)
    return lines


if __name__ == "__main__":
    print("\n".join(check(sys.argv[1], "--ignore-cache-control" in sys.argv[2:])))
