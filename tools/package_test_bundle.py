#!/usr/bin/env python3
"""Create a deterministic local AudioBridge PS5 test bundle."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import zipfile


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    name = f"AudioBridge-GUI-v{version}-test"
    output = root / "build" / name
    archive = root / "build" / f"{name}.zip"
    files = (
        "audiobridge-gui.elf",
        "audiobridge-uninstall.elf",
        "INSTALL-RU.txt",
        "README.md",
        "docs/PS5-NATIVE-TILE.md",
        "LICENSE",
    )

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    for relative in files:
        source = root / relative
        if not source.is_file() or source.stat().st_size == 0:
            raise SystemExit(f"required bundle file is missing or empty: {relative}")
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    checksums = "".join(
        f"{sha256(output / relative)}  {relative}\n"
        for relative in files
    )
    (output / "SHA256SUMS").write_text(checksums, encoding="ascii")

    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(
            (item for item in output.rglob("*") if item.is_file()),
            key=lambda item: item.relative_to(output).as_posix(),
        ):
            relative = path.relative_to(output).as_posix()
            info = zipfile.ZipInfo(f"{name}/{relative}", (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)

    print(archive)


if __name__ == "__main__":
    main()
