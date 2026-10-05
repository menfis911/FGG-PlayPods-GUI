#!/usr/bin/env python3
"""Generate a C translation unit containing the Web GUI assets."""

from pathlib import Path
import sys


ASSETS = (
    ("/", "web/index.html", "text/html; charset=utf-8"),
    ("/index.html", "web/index.html", "text/html; charset=utf-8"),
    ("/styles.css", "web/styles.css", "text/css; charset=utf-8"),
    ("/app.js", "web/app.js", "application/javascript; charset=utf-8"),
)


def c_bytes(data: bytes) -> str:
    return ",".join(str(value) for value in data)


def main() -> int:
    root = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2])
    unique = {}
    lines = ['#include "web_assets.h"', "#include <string.h>", ""]
    for _, relative, _ in ASSETS:
        if relative in unique:
            continue
        symbol = "asset_" + Path(relative).name.replace(".", "_")
        unique[relative] = symbol
        data = (root / relative).read_bytes()
        lines.append(f"static const unsigned char {symbol}[] = {{{c_bytes(data)}}};")
    lines.extend(["", "int web_asset_find(const char *path, web_asset *asset)", "{"])
    for route, relative, content_type in ASSETS:
        symbol = unique[relative]
        lines.extend([
            f'    if (strcmp(path, "{route}") == 0) {{',
            f"        asset->data = (const char *){symbol};",
            f"        asset->size = sizeof {symbol};",
            f'        asset->content_type = "{content_type}";',
            "        return 1;",
            "    }",
        ])
    lines.extend(["    return 0;", "}", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
