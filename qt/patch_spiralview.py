"""Build the small compatibility layer required by current game archives.

KnightLauncher's shaded SpiralView jar contains a locally modified SounderConfig
outer class but retains nested classes from its Clyde dependency.  The JVM rejects
that mixture.  This script restores the complete Clyde 1.2.1 SounderConfig family
and installs TRS's null-tolerant ConfigGroup class.
"""

from __future__ import annotations

import argparse
import zipfile
from pathlib import Path


SOUNDER_PREFIX = "com/threerings/openal/config/SounderConfig"
CONFIG_GROUP = "com/threerings/config/ConfigGroup.class"
CONFIG_GROUP_INNER_PREFIX = "com/threerings/config/ConfigGroup$"


def compiler_classpath(source: Path, destination: Path) -> None:
    """Make a javac-21-readable copy of a Java-25 dependency jar."""
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(destination, "w") as zout:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename.endswith(".class") and data[:4] == b"\xca\xfe\xba\xbe":
                data = data[:6] + (65).to_bytes(2, "big") + data[8:]
            zout.writestr(item, data)


def build(viewer: Path, clyde: Path, class_root: Path, destination: Path) -> None:
    with (
        zipfile.ZipFile(viewer) as source,
        zipfile.ZipFile(clyde) as dependency,
        zipfile.ZipFile(destination, "w") as output,
    ):
        for item in source.infolist():
            if (item.filename == CONFIG_GROUP or item.filename.startswith(CONFIG_GROUP_INNER_PREFIX) or
                    item.filename.startswith(SOUNDER_PREFIX)):
                continue
            output.writestr(item, source.read(item.filename))
        for item in dependency.infolist():
            if item.filename.startswith(SOUNDER_PREFIX):
                output.writestr(item, dependency.read(item.filename))
        for compiled in class_root.rglob("*.class"):
            output.writestr(compiled.relative_to(class_root).as_posix(), compiled.read_bytes())


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare = subparsers.add_parser("prepare-classpath")
    prepare.add_argument("source", type=Path)
    prepare.add_argument("destination", type=Path)
    package = subparsers.add_parser("build")
    package.add_argument("viewer", type=Path)
    package.add_argument("clyde", type=Path)
    package.add_argument("class_root", type=Path)
    package.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.command == "prepare-classpath":
        compiler_classpath(args.source, args.destination)
    else:
        build(args.viewer, args.clyde, args.class_root, args.destination)


if __name__ == "__main__":
    main()
