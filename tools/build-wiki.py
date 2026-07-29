# -*- coding: utf-8 -*-
"""Render wiki/ into a checkout of the GitHub wiki repository.

wiki/ in this repo is the source: it is reviewable in a PR and cannot drift from the code the way a
hand-edited wiki does. The wiki repo is a separate git repository and cannot see this one's files, so
image paths (../docs/img/...) are rewritten to absolute raw URLs on the way out — the diagrams stay in
one place rather than being duplicated.

    python tools/build-wiki.py                    # write into ../GenericMessagePlugin.wiki
    python tools/build-wiki.py --clone            # clone it there first if missing
    python tools/build-wiki.py --target <dir>     # write into an existing checkout (CI)

Never pushes. Locally, review and push yourself; in CI that is the workflow's job.
"""
import argparse, os, re, subprocess, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "wiki")
DEFAULT_TARGET = os.path.join(ROOT, "..", "GenericMessagePlugin.wiki")
DEFAULT_REMOTE = "git@github.com-wangjieest:wangjieest/GenericMessagePlugin.wiki.git"
RAW = "https://raw.githubusercontent.com/wangjieest/GenericMessagePlugin/main/docs/img/"


def check_links():
    """A page is only reachable if something links it, so verify before publishing."""
    pages = {f[:-3] for f in os.listdir(SRC) if f.endswith(".md")}
    broken = []
    for f in os.listdir(SRC):
        body = open(os.path.join(SRC, f), encoding="utf-8").read()
        broken += [(f, l) for l in re.findall(r"\[\[([^\]]+)\]\]", body) if l not in pages]
    return broken


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default=DEFAULT_TARGET, help="wiki checkout to write into")
    ap.add_argument("--clone", action="store_true", help="clone the wiki repo if the target is missing")
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    args = ap.parse_args()

    target = os.path.abspath(args.target)
    if args.clone and not os.path.isdir(target):
        subprocess.check_call(["git", "clone", args.remote, target])
    if not os.path.isdir(target):
        sys.exit(f"no wiki checkout at {target} — pass --target, or use --clone")

    for f in os.listdir(target):
        if f.endswith(".md"):
            os.remove(os.path.join(target, f))

    n = 0
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(".md"):
            continue
        body = open(os.path.join(SRC, f), encoding="utf-8").read()
        open(os.path.join(target, f), "w", encoding="utf-8", newline="\n").write(
            body.replace("](../docs/img/", "](" + RAW))
        n += 1
    print(f"{n} pages -> {target}")

    broken = check_links()
    print("broken wiki links:", broken or "none")
    if broken:
        sys.exit(1)


if __name__ == "__main__":
    main()
