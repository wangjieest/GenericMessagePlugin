# -*- coding: utf-8 -*-
"""Build the GitHub Pages site under docs/ from the two READMEs.

Emits index.html (English), index-cn.html (Simplified Chinese) and
dispatch-stack-measured.html with inlined styles and no CDN. README image paths
(docs/img/...) are rewritten to img/... so they resolve from inside docs/, which
is the Pages root; cross-language links stay inside the site.

Run: python tools/build-docs.py
"""
import os, re, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass
import markdown
from markdown.extensions.toc import slugify_unicode

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
GH = "https://github.com/wangjieest/GenericMessagePlugin"
MARKET = "https://www.unrealengine.com/marketplace/en-US/product/genericmessageplugin-gmp"

CSS = """
:root{--bg:#191919;--fg:#ddd;--sub:#8d949c;--acc:#4fc3f7;--grn:#81c784;--wrn:#ffb74d;
      --box:#252a2e;--line:#2f353b}
*{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;background:var(--bg);color:var(--fg);
     font:16px/1.75 -apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,"Microsoft YaHei",sans-serif}
a{color:var(--acc);text-decoration:none}
a:hover{text-decoration:underline}
.wrap{max-width:960px;margin:0 auto;padding:0 24px 96px}
header{border-bottom:1px solid var(--line);background:linear-gradient(135deg,#0d1b2a,#1b263b)}
header .wrap{padding:64px 24px 56px}
header h1{margin:0;font-size:52px;letter-spacing:-1px}
header h1 span{color:var(--acc)}
header .tag{margin:14px 0 0;font-size:21px;color:#fff;font-weight:400}
header .lede{margin:18px 0 0;font-size:16px;color:#b9c2cb;max-width:720px}
header .links{margin:30px 0 0;display:flex;flex-wrap:wrap;gap:10px}
header .links a{border:1px solid #3d5570;border-radius:6px;padding:8px 16px;font-size:14px;color:#cfe9f7}
header .links a.primary{background:var(--acc);border-color:var(--acc);color:#06222f;font-weight:600}
h1.part{margin:84px 0 6px;padding:26px 0 0;border-top:2px solid var(--acc);
        font-size:34px;color:#fff;font-weight:600;letter-spacing:-.4px}
.partsub{margin:8px 0 0;color:var(--sub);font-size:15px;max-width:760px}
h2{margin:52px 0 14px;padding-bottom:9px;border-bottom:1px solid var(--line);
   font-size:26px;color:var(--acc);font-weight:600}
h3{margin:36px 0 10px;font-size:19px;color:#e6edf3;font-weight:600}
h3::before{content:"";display:inline-block;width:3px;height:15px;margin-right:10px;
           vertical-align:-2px;background:var(--grn);border-radius:2px}
.partsub + h2{margin-top:34px}
p{margin:14px 0}
strong{color:#fff}
img{display:block;max-width:100%;height:auto;margin:26px auto;border-radius:8px}
code{font-family:Consolas,"SF Mono",Menlo,monospace;font-size:.9em;
     background:#22272b;border:1px solid var(--line);border-radius:4px;padding:1px 5px;color:#cfe9f7}
pre{background:#1d2226;border:1px solid var(--line);border-left:3px solid var(--acc);
    border-radius:8px;padding:16px 18px;overflow:auto;margin:20px 0}
pre code{background:none;border:0;padding:0;font-size:13.5px;line-height:1.62;color:#dfe6ec}
table{border-collapse:collapse;width:100%;margin:22px 0;font-size:14.5px}
th,td{border:1px solid var(--line);padding:9px 13px;text-align:left;vertical-align:top}
th{background:#22272b;color:var(--acc);font-weight:600}
tbody tr:nth-child(even){background:#1d2124}
ul{padding-left:22px}
li{margin:7px 0}
blockquote{margin:22px 0;padding:12px 18px;border-left:3px solid var(--wrn);
           background:#241f16;border-radius:0 6px 6px 0;color:#cbb894}
blockquote p{margin:0}
hr{border:0;border-top:1px solid var(--line);margin:64px 0 0}
footer{border-top:1px solid var(--line);margin-top:72px;padding:28px 0 0;color:var(--sub);font-size:14px}
@media(max-width:640px){header h1{font-size:38px}h2{font-size:22px}.wrap{padding:0 16px 64px}}
"""

LANGS = [
 dict(
   src="README.md", out="index.html", lang="en",
   title="GMP &middot; GenericMessagePlugin",
   desc="One messaging system for Unreal C++, Blueprint and five scripting languages. "
        "No shared header between sender and listener.",
   tag="One messaging system for C++, Blueprint, and five scripting languages.",
   lede="Sender and listener share a string, not a header &mdash; so you can delete a module "
        "and nothing is waiting to break the build. Signatures are still collected, validated and "
        "turned into Blueprint pins and per-language IntelliSense; the checking just happens "
        "somewhere else.",
   nav=[("GitHub", GH, True), ("Marketplace", MARKET, False),
        ("Collection messages", "article-collection-messages.html", False),
        ("Inlined dispatch", "article-inline-fire.html", False),
        ("Measured dispatch stack", "dispatch-stack-measured.html", False),
        ("Archived README", GH + "/blob/main/README_old.md", False),
        ("简体中文", "index-cn.html", False)],
   foot=f'GenericMessagePlugin · built from <a href="{GH}/blob/main/README.md">README.md</a>'
        ' · diagrams regenerate from <code>tools/gen-diagrams.py</code>',
   swap=[("](README_CN.md)", "](index-cn.html)")],
 ),
 dict(
   src="README_CN.md", out="index-cn.html", lang="zh-CN",
   title="GMP &middot; GenericMessagePlugin",
   desc="一套消息，喂饱 Unreal 的 C++、蓝图和五种脚本。发和收之间没有共享的类型头。",
   tag="一套消息，喂饱 C++、蓝图和五种脚本。",
   lede="发和收之间没有共享的类型头，只有一个字符串约定 &mdash;&mdash; 所以你能直接把一个模块删掉，"
        "不会有编译错误在等你。签名照样被收集、被校验，照样长成蓝图引脚和各语言的智能提示，"
        "只是检查发生的时机换了地方。",
   nav=[("GitHub", GH, True), ("Marketplace", MARKET, False),
        ("集合型消息", "article-collection-messages-cn.html", False),
        ("内联派发", "article-inline-fire-cn.html", False),
        ("派发栈实测", "dispatch-stack-measured.html", False),
        ("旧版 README 存档", GH + "/blob/main/README_old.md", False),
        ("English", "index.html", False)],
   foot=f'GenericMessagePlugin · 由 <a href="{GH}/blob/main/README_CN.md">README_CN.md</a> 生成'
        ' · 示意图由 <code>tools/gen-diagrams.py</code> 重出',
   swap=[("](README.md)", "](index.html)")],
 ),
]


def render(md, extra_swaps=()):
    """README markdown -> page body html, with repo paths rewritten for the site."""
    body = md[md.index("\n---\n") + 5:]          # the page has its own hero
    for a, b in [("](docs/img/", "](img/"),
                 ("](docs/dispatch-stack-measured.md)", "](dispatch-stack-measured.html)"),
                 ("](LICENSE)", f"]({GH}/blob/main/LICENSE)"),
                 ("](README_old.md)", f"]({GH}/blob/main/README_old.md)")] + list(extra_swaps):
        body = body.replace(a, b)
    # slugify_unicode: the default drops non-ASCII, leaving the Chinese page with positional _1.._N heading ids
    html = markdown.markdown(body, extensions=["tables", "fenced_code", "toc", "attr_list"],
                             extension_configs={"toc": {"slugify": slugify_unicode}})
    # h1 is a part banner, h2 an area, h3 a topic; the part intro line is italic
    html = html.replace("<h1", '<h1 class="part"')
    return re.sub(r'(<h1 class="part"[^>]*>.*?</h1>)\s*<p><em>(.*?)</em></p>',
                  r'\1<p class="partsub">\2</p>', html, flags=re.S)


def page(cfg):
    md = open(os.path.join(ROOT, cfg["src"]), encoding="utf-8").read()
    nav = "\n  ".join(f'<a{" class=\"primary\"" if p else ""} href="{u}">{t}</a>'
                      for t, u, p in cfg["nav"])
    out = os.path.join(DOCS, cfg["out"])
    open(out, "w", encoding="utf-8").write(f"""<!DOCTYPE html>
<html lang="{cfg['lang']}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{cfg['title']}</title>
<meta name="description" content="{cfg['desc']}">
<style>{CSS}</style>
</head>
<body>
<header><div class="wrap">
<h1>G<span>MP</span></h1>
<p class="tag">{cfg['tag']}</p>
<p class="lede">{cfg['lede']}</p>
<div class="links">
  {nav}
</div></div></header>
<div class="wrap">
{render(md, cfg['swap'])}
<footer>{cfg['foot']}</footer>
</div>
</body>
</html>
""")
    print(f"docs/{cfg['out']}  {os.path.getsize(out)/1024:.0f} KB")


for c in LANGS:
    page(c)


# ---- Sub-pages: same stylesheet, hand-built HTML, no Jekyll anywhere ----
def subpage(name, title, blurb, lang="en"):
    md = open(os.path.join(DOCS, name + ".md"), encoding="utf-8").read()
    md = md.replace("](dispatch-stack-measured.md)", "](dispatch-stack-measured.html)")
    md = re.sub(r"^#\s+.*$", "", md, count=1, flags=re.M)
    b = markdown.markdown(md, extensions=["tables", "fenced_code", "attr_list"])
    b = b.replace("<h1", '<h1 class="part"')
    out = os.path.join(DOCS, name + ".html")
    open(out, "w", encoding="utf-8").write(f"""<!DOCTYPE html>
<html lang="{lang}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title} &middot; GMP</title><style>{CSS}</style></head>
<body><header><div class="wrap">
<h1 style="font-size:34px">{title}</h1>
<p class="lede">{blurb}</p>
<div class="links"><a href="index.html">&larr; GMP</a>
  <a href="index-cn.html">&larr; 中文</a>
  <a href="{GH}">GitHub</a></div>
</div></header><div class="wrap">{b}
<footer>GenericMessagePlugin · <a href="{GH}">GitHub</a></footer></div></body></html>""")
    print(f"docs/{name}.html  {os.path.getsize(out)/1024:.0f} KB")


subpage("dispatch-stack-measured", "Measured dispatch stack",
        "Raw symbolized frames captured inside the listener, and the exact commands to reproduce them.")

subpage("article-collection-messages", "Collection messages",
        "Teaching the message system to see the rows inside an array: row dispatch off a plain send, "
        "and what a stored copy buys on top of it.")
subpage("article-collection-messages-cn", "集合型消息",
        "让消息系统看见数组里的「行」：按行分发不依赖存储，增量依赖。", lang="zh-CN")
subpage("article-inline-fire", "Inlined dispatch",
        "Collapsing a send to four frames: compile-time store resolution, a reference-passing ABI, "
        "and what each of them removes.")
subpage("article-inline-fire-cn", "内联派发",
        "把一次消息发送压到 4 层栈：编译期解析 store、传引用的 ABI，各自削掉了什么。", lang="zh-CN")

open(os.path.join(DOCS, ".nojekyll"), "w").close()
print("docs/.nojekyll")
