"""Splits a bundle of source files ("=== relative/path" header lines) into files.

Usage: unbundle.py <bundle> <root>
"""
import os
import sys


def main():
    bundle, root = sys.argv[1], sys.argv[2]
    path, lines = None, []

    def flush():
        if path is None:
            return
        target = os.path.join(root, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join(lines).strip("\n") + "\n")
        print("wrote", path)

    with open(bundle, encoding="utf-8") as f:
        for line in f:
            if line.startswith("=== "):
                flush()
                path, lines = line[4:].strip(), []
            else:
                lines.append(line)
    flush()


if __name__ == "__main__":
    main()
