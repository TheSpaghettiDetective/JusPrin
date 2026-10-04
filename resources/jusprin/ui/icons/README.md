# Functional icons

The functional SVGs in this directory are copied byte-for-byte from
`lucide-static@1.47.0`. Their local filenames are stable UI contracts; aliases
such as `close.svg` → upstream `x.svg` are recorded in `lucide-map.json`.
The npm release has integrity
`sha512-yWIrkdXc688Feq5VjOktsKmV5Ikc7y5Nu3rrdtbr8nWjkJWk8QlnZfVtIak22Af+fNhZ7k4cTJpZo1zmj7X5sA==`.
The package's ISC and inherited Feather MIT license notices are in
`LUCIDE-LICENSE.txt`. `agent-bot.svg` is JusPrin artwork and is not Lucide.
The native shell's solid chip caret and filled/empty pane-toggle glyphs remain
custom because those shapes encode product-specific states.

To verify provenance after downloading the pinned npm package:

```sh
mkdir -p /tmp/jusprin-lucide-check
npm pack lucide-static@1.47.0 --pack-destination /tmp/jusprin-lucide-check
tar -xzf /tmp/jusprin-lucide-check/lucide-static-1.47.0.tgz -C /tmp/jusprin-lucide-check
python3 resources/jusprin/ui/icons/verify_lucide.py /tmp/jusprin-lucide-check/package
```

Render these `currentColor` SVGs with semantic design tokens. Do not edit their
paths locally; update the pinned package and mapping when changing the icon set.
