# -*- coding: utf-8 -*-
"""Sync wiki/ into the GitHub wiki repository.

wiki/ in this repo is the source: it is reviewable in a PR and cannot drift from the code the way a
hand-edited wiki does. The wiki repo is a separate git repository and cannot see this one's files, so
image paths (../docs/img/...) are rewritten to absolute raw URLs on the way out — the 22 diagrams stay
in one place rather than being duplicated.

    python tools/build-wiki.py            # write into a clone, report what changed
    python tools/build-wiki.py --clone    # clone the wiki repo first if it is not there

Then review and push from the clone. This script never pushes.
"""
import os, re, shutil, subprocess, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "wiki")
CLONE = os.path.join(ROOT, "..", "GenericMessagePlugin.wiki")
REMOTE = "git@github.com-wangjieest:wangjieest/GenericMessagePlugin.wiki.git"
RAW = "https://raw.githubusercontent.com/wangjieest/GenericMessagePlugin/main/docs/img/"


def rewrite(text):
    return text.replace("](../docs/img/", "](" + RAW)


def main():
    clone = os.path.abspath(CLONE)
    if "--clone" in sys.argv and not os.path.isdir(clone):
        subprocess.check_call(["git", "clone", REMOTE, clone])
    if not os.path.isdir(os.path.join(clone, ".git")):
        sys.exit(f"no wiki clone at {clone} — run with --clone, or clone {REMOTE} there yourself")

    for f in os.listdir(clone):
        if f.endswith(".md"):
            os.remove(os.path.join(clone, f))

    n = 0
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(".md"):
            continue
        body = open(os.path.join(SRC, f), encoding="utf-8").read()
        open(os.path.join(clone, f), "w", encoding="utf-8", newline="\n").write(rewrite(body))
        n += 1
    print(f"{n} pages -> {clone}")

    # a page is only reachable if some other page links it, so check before publishing
    pages = {f[:-3] for f in os.listdir(SRC) if f.endswith(".md")}
    broken = []
    for f in os.listdir(SRC):
        for link in re.findall(r"\[\[([^\]]+)\]\]", open(os.path.join(SRC, f), encoding="utf-8").read()):
            if link not in pages:
                broken.append((f, link))
    print("broken wiki links:", broken or "none")

    print(subprocess.run(["git", "-C", clone, "status", "--short"],
                         capture_output=True, text=True).stdout or "  (no changes)")
    print("review, then push from the clone")


if __name__ == "__main__":
    main()
