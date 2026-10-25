#!/usr/bin/env python3
"""Validate and apply the target Android package name for a local build.

The package name is accepted as data rather than interpolated into a shell
command or a sed replacement expression.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

_PACKAGE_NAME_RE = re.compile(
    r"^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+$"
)
_DESCRIPTION_PLACEHOLDER = (
    'moduleDescription = "Il2CppDumper Zygisk version."'
)
_PACKAGE_PLACEHOLDER = (
    '#define GamePackageName "com.game.packagename"'
)


def validate_package_name(package_name: str) -> None:
    """Raise ValueError unless *package_name* is a plausible Android app ID."""
    if len(package_name) > 255 or not _PACKAGE_NAME_RE.fullmatch(package_name):
        raise ValueError(
            "package name must contain at least two dot-separated identifiers; "
            "each identifier must start with an ASCII letter and contain only "
            "ASCII letters, digits, or underscores (maximum 255 characters)"
        )


def configure_package(root: Path, package_name: str) -> None:
    """Replace both build placeholders after validating all expected inputs."""
    validate_package_name(package_name)

    gradle_path = root / "module.gradle"
    header_path = root / "module/src/main/cpp/game.h"
    gradle_content = gradle_path.read_text(encoding="utf-8")
    header_content = header_path.read_text(encoding="utf-8")

    # Validate every placeholder before writing either file, avoiding a
    # half-configured tree when the repository layout changes unexpectedly.
    if gradle_content.count(_DESCRIPTION_PLACEHOLDER) != 1:
        raise ValueError(f"expected exactly one description placeholder in {gradle_path}")
    if header_content.count(_PACKAGE_PLACEHOLDER) != 1:
        raise ValueError(f"expected exactly one package placeholder in {header_path}")

    new_gradle_content = gradle_content.replace(
        _DESCRIPTION_PLACEHOLDER,
        f'moduleDescription = "({package_name}) Il2CppDumper Zygisk version."',
        1,
    )
    new_header_content = header_content.replace(
        _PACKAGE_PLACEHOLDER,
        f'#define GamePackageName "{package_name}"',
        1,
    )

    gradle_path.write_text(new_gradle_content, encoding="utf-8")
    header_path.write_text(new_header_content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package-name",
        required=True,
        help="Android application ID to configure for this build",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="project root (defaults to the current directory)",
    )
    args = parser.parse_args()

    try:
        configure_package(args.root.resolve(), args.package_name)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print(f"Configured target package: {args.package_name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
