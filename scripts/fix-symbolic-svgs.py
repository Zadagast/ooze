#!/usr/bin/env python3
"""Normalize sheet-exported symbolic SVGs for GTK's symbolic renderer.

elementary's symbolic icons are exported from a large sheet: the drawing sits
at sheet coordinates and a root <g transform="translate(-X Y)"> shifts it into
the 16x16 canvas. GTK >= 4.20 renders simple symbolic SVGs with its own path
renderer, which ignores that group transform, so the drawing lands outside the
viewport and the icon shows up blank.

Rewriting the translate into an equivalent viewBox on the <svg> element keeps
the file valid everywhere and renders correctly in GTK's path renderer.

Usage: fix-symbolic-svgs.py <icon-theme-dir>
"""

import pathlib
import re
import sys

TRANSLATE_RE = re.compile(
    r'\s*transform=(["\'])translate\((-?[0-9.]+)[,\s]+(-?[0-9.]+)\)\1')
SVG_TAG_RE = re.compile(r"<svg\b[^>]*>", re.S)
WIDTH_RE = re.compile(r'\bwidth=(["\'])([0-9.]+)\1')
HEIGHT_RE = re.compile(r'\bheight=(["\'])([0-9.]+)\1')


def fix_file(path):
    text = path.read_text(encoding="utf-8")

    if "viewBox" in text or "matrix(" in text:
        return False

    transforms = TRANSLATE_RE.findall(text)
    if len(transforms) != 1:
        return False

    # The translate must be on an element that actually wraps the drawing;
    # some files carry it on a leftover empty <g ... /> while the artwork
    # already sits at canvas coordinates.
    m = TRANSLATE_RE.search(text)
    tag_end = text.find(">", m.end())
    if tag_end == -1 or text[tag_end - 1] == "/":
        return False

    svg_tag = SVG_TAG_RE.search(text)
    if not svg_tag:
        return False
    width_m = WIDTH_RE.search(svg_tag.group(0))
    height_m = HEIGHT_RE.search(svg_tag.group(0))
    if not width_m or not height_m:
        return False

    tx, ty = float(transforms[0][1]), float(transforms[0][2])
    width, height = width_m.group(2), height_m.group(2)

    new_text = TRANSLATE_RE.sub("", text, count=1)
    new_text = new_text.replace(
        "<svg",
        '<svg\n   viewBox="%g %g %s %s"' % (-tx, -ty, width, height),
        1)

    path.write_text(new_text, encoding="utf-8")
    return True


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1])
    fixed = 0
    for svg in root.rglob("*-symbolic.svg"):
        if svg.is_symlink():
            continue
        if fix_file(svg):
            fixed += 1
    print("fix-symbolic-svgs: rewrote %d file(s) under %s" % (fixed, root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
