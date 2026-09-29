#!/usr/bin/env python3
"""Generate the static catalog site from catalog.json (tools/catalog_build.py).

  tools/catalog_site.py [--catalog catalog/dist/catalog.json] [--out catalog/dist/site]

Writes index.html (one self-contained page: the catalog is embedded, filtering and sorting run in the browser),
catalog.json (for installers and other tools) and .nojekyll. Deploy the folder with GitHub Pages.
Standard library only. Template: tools/catalog_site/index.template.html. See docs/CATALOG.md.
"""
import argparse
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))


def render(catalog):
    """The page HTML for a catalog dict. The JSON is embedded in a <script type=application/json>, so '<' is escaped."""
    data = json.dumps(catalog, separators=(",", ":"), ensure_ascii=False).replace("<", "\\u003c").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    tpl = open(os.path.join(HERE, "catalog_site", "index.template.html"), encoding="utf-8").read()
    marker = "/*CATALOG_JSON*/"
    if tpl.count(marker) != 1:
        raise SystemExit("template must contain the marker exactly once")
    return tpl.replace(marker, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", default="catalog/dist/catalog.json")
    ap.add_argument("--out", default="catalog/dist/site")
    a = ap.parse_args()
    catalog = json.load(open(a.catalog, encoding="utf-8"))
    if catalog.get("schema") != 1:
        raise SystemExit("unsupported catalog schema %r" % catalog.get("schema"))
    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, "index.html"), "w", encoding="utf-8").write(render(catalog))
    shutil.copy(a.catalog, os.path.join(a.out, "catalog.json"))
    open(os.path.join(a.out, ".nojekyll"), "w").close()
    print("%s (%d plugins)" % (os.path.join(a.out, "index.html"), len(catalog["plugins"])))


if __name__ == "__main__":
    main()
