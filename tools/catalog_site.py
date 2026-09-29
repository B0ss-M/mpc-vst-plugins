#!/usr/bin/env python3
"""Generate the static catalog site from catalog.json (tools/catalog_build.py).

  tools/catalog_site.py [--catalog catalog/dist/catalog.json] [--out catalog/dist/site]

Writes feed.xml (Atom), index.html (one self-contained page: the catalog is embedded, filtering and sorting run in the browser),
catalog.json (for installers and other tools) and .nojekyll. Deploy the folder with GitHub Pages.
Standard library only. Template: tools/catalog_site/index.template.html. See docs/CATALOG.md.
"""
import argparse
import json
import os
import shutil
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))


def render(catalog):
    """The page HTML for a catalog dict. The JSON is embedded in a <script type=application/json>, so '<' is escaped."""
    data = json.dumps(catalog, separators=(",", ":"), ensure_ascii=False).replace("<", "\\u003c").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    tpl = open(os.path.join(HERE, "catalog_site", "index.template.html"), encoding="utf-8").read()
    marker = "/*CATALOG_JSON*/"
    if tpl.count(marker) != 1:
        raise SystemExit("template must contain the marker exactly once")
    return tpl.replace(marker, data)


def atom(catalog, base=""):
    """Atom feed of the 50 newest non-yanked releases. `base` is the site URL (feed ids fall back to tag: URIs)."""
    items = []
    for p in catalog["plugins"]:
        for v in p["versions"]:
            if not v["yanked"] and v.get("date"):
                items.append((v["date"], p, v))
    items.sort(key=lambda t: (t[0], t[1]["name"].lower()), reverse=True)
    out = ['<?xml version="1.0" encoding="utf-8"?>', '<feed xmlns="http://www.w3.org/2005/Atom">',
           "<title>MPC OS Plugin Catalog: new releases</title>", "<id>tag:mpc-vst-catalog,2026:releases</id>",
           "<updated>%s</updated>" % (items[0][0] + "T00:00:00Z" if items else catalog.get("generated", "1970-01-01T00:00:00Z"))]
    if base:
        out.append('<link rel="self" href="%s"/>' % escape(base.rstrip("/") + "/feed.xml", {'"': "&quot;"}))
    for date, p, v in items[:50]:
        beta = " (beta)" if v["channel"] == "beta" else ""
        out.append("<entry><title>%s %s%s</title><id>tag:mpc-vst-catalog,2026:%s@%s</id><updated>%sT00:00:00Z</updated>"
                   '<link href="%s"/><author><name>%s</name></author><summary>%s</summary></entry>' % (
                       escape(p["name"]), escape(v["version"]), beta, escape(p["id"]), escape(v["version"]), date,
                       escape(v["url"], {'"': "&quot;"}), escape(p["author"]), escape(p["summary"])))
    out.append("</feed>")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", default="catalog/dist/catalog.json")
    ap.add_argument("--out", default="catalog/dist/site")
    ap.add_argument("--base-url", default="", help="public site URL, for the feed's self link")
    a = ap.parse_args()
    catalog = json.load(open(a.catalog, encoding="utf-8"))
    if catalog.get("schema") != 1:
        raise SystemExit("unsupported catalog schema %r" % catalog.get("schema"))
    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, "index.html"), "w", encoding="utf-8").write(render(catalog))
    open(os.path.join(a.out, "feed.xml"), "w", encoding="utf-8").write(atom(catalog, a.base_url))
    shutil.copy(a.catalog, os.path.join(a.out, "catalog.json"))
    open(os.path.join(a.out, ".nojekyll"), "w").close()
    print("%s (%d plugins)" % (os.path.join(a.out, "index.html"), len(catalog["plugins"])))


if __name__ == "__main__":
    main()
