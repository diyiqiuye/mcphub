"""把 dist-pack 的直链写回 catalog/*.json 的 sources[0]（name=pack）。

用法:
    python scripts/apply-pack-url.py https://pub-xxxx.r2.dev/mcphub

会在每个 catalog/<id>.json 的 install.sources 最前面插入一条:
    {"name": "pack", "url": "<base>/<file>", "sha256": "<packs.json 里的哈希>"}
其余官方源保留作为兜底。重复执行只会替换 name=pack 的那一条。
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main() -> int:
    if len(sys.argv) < 2 or not sys.argv[1].startswith(("http://", "https://")):
        print(__doc__)
        return 2
    base = sys.argv[1].rstrip("/")
    dry = "--dry-run" in sys.argv[2:]
    packs_file = os.path.join(ROOT, "dist-pack", "packs.json")
    with open(packs_file, encoding="utf-8") as f:
        packs = json.load(f)["packs"]

    for p in packs:
        cat = os.path.join(ROOT, "catalog", p["id"] + ".json")
        if not os.path.exists(cat):
            print("skip (no catalog):", p["id"])
            continue
        with open(cat, encoding="utf-8") as f:
            doc = json.load(f)
        install = doc.setdefault("install", {})
        sources = [s for s in install.get("sources", []) if s.get("name") != "pack"]
        sources.insert(0, {"name": "pack", "url": f"{base}/{p['file']}", "sha256": p["sha256"]})
        install["sources"] = sources
        install["packBytes"] = p["bytes"]
        if not dry:
            with open(cat, "w", encoding="utf-8") as f:
                json.dump(doc, f, ensure_ascii=False, indent=2)
                f.write("\n")
        print(("would update " if dry else "updated ") + f"{p['id']:8s} -> {sources[0]['url']}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
