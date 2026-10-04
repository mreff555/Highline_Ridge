#!/usr/bin/env python3
"""Pack resources/ into a Highline Ridge asset blob + C++ index.

Format (HLAP v1):
  magic[4] = b"HLAP"
  version u32le = 1
  entry_count u32le
  blob_size u32le
  entries[entry_count]:
    path_offset u32le   # into string table
    data_offset u32le   # into blob
    data_size u32le
    flags u32le         # bit0 = stored as .xz payload
  string_table: concatenated NUL-terminated logical paths
  blob: concatenated file bytes

Logical paths omit a trailing ".xz" even when the on-disk file is compressed;
flags mark xz so the runtime can decompress.

Usage:
  python3 tools/pack_assets.py --resources resources --out-dir build/generated
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


FLAG_XZ = 1


def collect_files(resources: Path) -> list[tuple[str, Path, bool]]:
    """Return (logical_path, file_path, is_xz). Prefer .xz when both exist."""
    entries: dict[str, tuple[Path, bool]] = {}
    for path in sorted(resources.rglob("*")):
        if not path.is_file():
            continue
        # Skip authoring scratch / secrets.
        rel = path.relative_to(resources).as_posix()
        if (
            rel.startswith(".authoring/")
            or rel.endswith("/xai_api_key")
            or rel == "xai_api_key"
            or rel == "editor_prefs.json"
        ):
            continue
        if path.name.startswith("."):
            continue

        is_xz = path.suffix == ".xz"
        logical = rel[:-3] if is_xz and rel.endswith(".xz") else rel
        logical_key = f"resources/{logical}"
        prev = entries.get(logical_key)
        if prev is None:
            entries[logical_key] = (path, is_xz)
        elif is_xz and not prev[1]:
            # Prefer compressed payload when both present.
            entries[logical_key] = (path, True)
        # else keep existing (already xz or uncompressed-only)
    out = [(k, v[0], v[1]) for k, v in sorted(entries.items())]
    return out


def write_pak(entries: list[tuple[str, Path, bool]], pak_path: Path) -> None:
    string_table = bytearray()
    path_offsets: list[int] = []
    for logical, _, _ in entries:
        path_offsets.append(len(string_table))
        string_table.extend(logical.encode("utf-8"))
        string_table.append(0)

    blob = bytearray()
    data_offsets: list[int] = []
    data_sizes: list[int] = []
    flags_list: list[int] = []
    for logical, file_path, is_xz in entries:
        data = file_path.read_bytes()
        data_offsets.append(len(blob))
        data_sizes.append(len(data))
        flags_list.append(FLAG_XZ if is_xz else 0)
        blob.extend(data)

    header = struct.pack(
        "<4sIII",
        b"HLAP",
        1,
        len(entries),
        len(blob),
    )
    index = bytearray()
    for i in range(len(entries)):
        index.extend(
            struct.pack(
                "<IIII",
                path_offsets[i],
                data_offsets[i],
                data_sizes[i],
                flags_list[i],
            )
        )

    pak_path.parent.mkdir(parents=True, exist_ok=True)
    with pak_path.open("wb") as f:
        f.write(header)
        f.write(index)
        f.write(string_table)
        # Align blob to 16 bytes for nicer mmap later (optional).
        pad = (16 - (f.tell() % 16)) % 16
        f.write(b"\0" * pad)
        f.write(blob)

    print(f"Wrote {pak_path} ({len(entries)} entries, blob {len(blob)} bytes)")


def write_embed_asm(pak_path: Path, asm_path: Path, apple: bool) -> None:
    """Emit gas/clang assembly that .incbin's the pak into .rodata."""
    pak_name = pak_path.name
    # Apple Mach-O: asm symbols need a leading underscore to match C names.
    sym = "_highline_pak_start" if apple else "highline_pak_start"
    sym_end = "_highline_pak_end" if apple else "highline_pak_end"
    if apple:
        body = f"""\
/* Auto-generated — embeds {pak_name} (Mach-O). */
\t.section __DATA,__const
\t.globl {sym}
\t.globl {sym_end}
\t.align 4
{sym}:
\t.incbin "{pak_name}"
{sym_end}:
"""
    else:
        body = f"""\
/* Auto-generated — embeds {pak_name} (ELF). */
\t.section .rodata,"a",@progbits
\t.global {sym}
\t.global {sym_end}
\t.align 16
{sym}:
\t.incbin "{pak_name}"
{sym_end}:
"""
    asm_path.write_text(body)
    print(f"Wrote {asm_path} (apple_syms={apple})")


def write_cpp_shim(hdr_path: Path, cpp_path: Path, pak_filename: str) -> None:
    hdr_path.parent.mkdir(parents=True, exist_ok=True)
    hdr_path.write_text(
        """\
#pragma once
#include <cstddef>
#include <cstdint>

namespace timberline_engine {

/** Linked via .incbin when HIGHLINE_EMBED_RESOURCES=ON. */
extern "C" {
extern const unsigned char highline_pak_start[];
extern const unsigned char highline_pak_end[];
}

inline const unsigned char* embeddedPakBytes() { return highline_pak_start; }
inline std::size_t embeddedPakSize() {
    return static_cast<std::size_t>(highline_pak_end - highline_pak_start);
}

/** Fallback filename if binary embed is unavailable. */
extern const char kEmbeddedAssetPakFile[];

} // namespace timberline_engine
"""
    )
    cpp_path.write_text(
        f"""\
#include "EmbeddedAssets.h"

namespace timberline_engine {{

const char kEmbeddedAssetPakFile[] = "{pak_filename}";

}} // namespace timberline_engine
"""
    )
    print(f"Wrote {hdr_path} and {cpp_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--resources",
        type=Path,
        default=Path("resources"),
        help="Game resources directory",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("build/generated"),
        help="Output directory for .pak and EmbeddedAssets.*",
    )
    args = parser.parse_args()

    resources = args.resources.resolve()
    if not resources.is_dir():
        raise SystemExit(f"resources not found: {resources}")

    entries = collect_files(resources)
    if not entries:
        raise SystemExit("no resource files found")

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    pak_path = out_dir / "highline_assets.pak"
    write_pak(entries, pak_path)
    import sys

    apple = sys.platform == "darwin"
    write_embed_asm(pak_path, out_dir / "highline_assets_pak.S", apple=apple)
    write_cpp_shim(
        out_dir / "EmbeddedAssets.h",
        out_dir / "EmbeddedAssets.cpp",
        pak_path.name,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
