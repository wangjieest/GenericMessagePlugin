# -*- coding: utf-8 -*-
"""Generate the README / docs diagrams as lossless webp, animated and static alike.

Everything is drawn on #191919 so the figures sit flush on a dark page, and
rendered at 2x then downscaled so the edges stay clean. GIF is used rather than
animated SVG because GitHub strips SMIL and slide tools do not take it.

Requires Pillow. Font paths below are Windows; point them at any CJK-capable
and monospace font elsewhere.

Run: python tools/gen-diagrams.py
"""
import os, sys, math
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs", "img")
FONT = r"C:\Windows\Fonts\msyh.ttc"
MONO = r"C:\Windows\Fonts\consola.ttf"
S = 4                      # supersampling factor
EXPORT = 2                 # output scale: layout stays 1x, pixels ship at 2x for HiDPI
BG  = (25, 25, 25)
ACC = (79, 195, 247)
GRN = (129, 199, 132)
WRN = (255, 183, 77)
RED = (229, 115, 115)
DIM = (110, 118, 126)
TXT = (221, 221, 221)
SUB = (141, 148, 156)
BOXF= (37, 41, 46)

_fc = {}
def F(size, mono=False):
    k = (size, mono)
    if k not in _fc:
        _fc[k] = ImageFont.truetype(MONO if mono else FONT, size * S, index=0)
    return _fc[k]

class C:
    """Canvas: callers use 1x logical coordinates, scaling by S happens inside."""
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new("RGB", (w * S, h * S), BG)
        self.d = ImageDraw.Draw(self.im)
    def text(self, x, y, s, size=15, fill=TXT, anchor="lm", mono=False):
        self.d.text((x * S, y * S), s, font=F(size, mono), fill=fill, anchor=anchor)
    def box(self, x, y, w, h, stroke=ACC, fill=BOXF, r=7, dash=False, width=1.6):
        xy = [x * S, y * S, (x + w) * S, (y + h) * S]
        if dash:
            self.d.rounded_rectangle(xy, radius=r * S, fill=fill, outline=DIM, width=int(1.4 * S))
        else:
            self.d.rounded_rectangle(xy, radius=r * S, fill=fill, outline=stroke, width=int(width * S))
    def line(self, p0, p1, fill=ACC, width=2, dash=False):
        if not dash:
            self.d.line([p0[0]*S, p0[1]*S, p1[0]*S, p1[1]*S], fill=fill, width=int(width*S))
        else:
            n = max(2, int(math.dist(p0, p1) / 9))
            for i in range(n):
                if i % 2: continue
                a = (p0[0]+(p1[0]-p0[0])*i/n, p0[1]+(p1[1]-p0[1])*i/n)
                b = (p0[0]+(p1[0]-p0[0])*(i+1)/n, p0[1]+(p1[1]-p0[1])*(i+1)/n)
                self.d.line([a[0]*S,a[1]*S,b[0]*S,b[1]*S], fill=fill, width=int(width*S))
    def head(self, p, ang, fill=ACC, sz=7):
        a, b = ang + 2.6, ang - 2.6
        pts = [(p[0], p[1]),
               (p[0] + sz*math.cos(a), p[1] + sz*math.sin(a)),
               (p[0] + sz*math.cos(b), p[1] + sz*math.sin(b))]
        self.d.polygon([(x*S, y*S) for x, y in pts], fill=fill)
    def arrow(self, p0, p1, fill=ACC, width=2, dash=False):
        self.line(p0, p1, fill, width, dash)
        self.head(p1, math.atan2(p1[1]-p0[1], p1[0]-p0[0]), fill)
    def curve(self, pts, fill=ACC, width=2, arrow=True):
        pl = bez(pts, 40)
        for i in range(len(pl)-1):
            self.line(pl[i], pl[i+1], fill, width)
        if arrow:
            self.head(pl[-1], math.atan2(pl[-1][1]-pl[-3][1], pl[-1][0]-pl[-3][0]), fill)
    def dot(self, p, fill=ACC, r=5):
        self.d.ellipse([(p[0]-r)*S, (p[1]-r)*S, (p[0]+r)*S, (p[1]+r)*S], fill=fill)
    def out(self):
        return self.im.resize((self.w * EXPORT, self.h * EXPORT), Image.LANCZOS)

def bez(p, n=40):
    """Sample a cubic or quadratic bezier."""
    out = []
    for i in range(n+1):
        t = i/n
        if len(p) == 4:
            a,b,c,d = p
            x = (1-t)**3*a[0] + 3*(1-t)**2*t*b[0] + 3*(1-t)*t*t*c[0] + t**3*d[0]
            y = (1-t)**3*a[1] + 3*(1-t)**2*t*b[1] + 3*(1-t)*t*t*c[1] + t**3*d[1]
        else:
            a,b,c = p
            x = (1-t)**2*a[0] + 2*(1-t)*t*b[0] + t*t*c[0]
            y = (1-t)**2*a[1] + 2*(1-t)*t*b[1] + t*t*c[1]
        out.append((x,y))
    return out

def at(pts, t):
    pl = bez(pts, 60)
    i = min(int(t*60), 60)
    return pl[i]

def save_gif(name, frames, ms=70):
    # Animated lossless webp, not gif: same frames for roughly half the bytes, and no 256-colour ceiling.
    p = os.path.join(OUT, os.path.splitext(name)[0] + ".webp")
    fr = [f.convert("RGB").quantize(colors=64, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
          for f in frames]
    fr[0].save(p, save_all=True, append_images=fr[1:], loop=0,
               duration=ms, lossless=True, quality=100, method=4)
    tot = (sum(ms) if isinstance(ms, list) else ms*len(fr))/1000.0
    print(f"  {os.path.basename(p):32s} {os.path.getsize(p)/1024:7.1f} KB  {len(frames)} frames  {tot:.1f}s")

# 08 is a discrete-state figure: hold each state long enough to read, do not tween
def save_steps(name, fn, steps):
    save_gif(name, [fn(t) for t, _ in steps], [ms for _, ms in steps])

def save_png(name, im):
    # Quantise, then encode lossless webp: same pixels as the 256-colour png this replaces, about a third
    # smaller. Quantising first matters -- lossless webp over an anti-aliased original comes out larger.
    p = os.path.join(OUT, os.path.splitext(name)[0] + ".webp")
    q = im.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE)
    q.convert("RGB").save(p, lossless=True, quality=100, method=6)
    print(f"  {os.path.basename(p):32s} {os.path.getsize(p)/1024:7.1f} KB")

W, H = 900, 320
def ease(t):  # ease in/out
    return t*t*(3-2*t)

# ============================================================ 01 dispatch layers (animated)
def f01(t):
    c = C(W, 300)
    c.box(30, 118, 190, 62); c.text(125, 142, "Send from Actor", 16, TXT, "mm")
    c.text(125, 162, "NotifyObjectMessage", 12, SUB, "mm")
    tg = [(18, "Listens on this Actor", "ListenObjectMessage"),
          (106, "Listens on its World", "ListenWorldMessage"),
          (194, "Listens globally", "ListenMessage")]
    paths = []
    for i, (y, lb, sb) in enumerate(tg):
        pts = [(226,149), (392,149), (412,y+31), (552,y+31)]
        paths.append(pts)
        c.curve(pts, DIM if t < 0.15+i*0.06 else ACC, 2)
    for i, (y, lb, sb) in enumerate(tg):
        lit = t > 0.45 + i*0.10
        c.box(560, y, 310, 62, ACC if lit else DIM, (40,52,60) if lit else BOXF)
        c.text(715, y+26, lb, 16, TXT if lit else SUB, "mm")
        c.text(715, y+46, sb, 12, SUB, "mm")
    for i, pts in enumerate(paths):
        tt = min(1.0, max(0.0, (t - i*0.08) / 0.55))
        if 0 < tt < 1.0:
            c.dot(at(pts, ease(tt)), ACC, 5)
    c.text(250, 216, "one send, all three levels receive", 13, SUB, "mm")
    c.line((560, 282), (210, 282), DIM, 1.6, dash=True)
    c.head((210, 282), math.pi, DIM)
    c.text(570, 282, "World-level send", 12, SUB, "lm")
    c.text(216, 268, "x  won't reach Object-level listeners  (one-way: narrow -> broad)", 12, RED, "lm")
    return c.out()

# ============================================================ 02 times and order
def f02():
    c = C(W, 320)
    c.text(30, 28, "Order: lower value runs first", 16, ACC, "lm")
    c.text(30, 50, "default is 0, so equal orders keep registration order", 11, SUB, "lm")
    c.arrow((356, 96), (356, 248), GRN, 2)      # draw the connector first so the circles sit on top
    rows = [("Order = -10", ""), ("Order = 0", "registered 1st"),
            ("Order = 0", "registered 2nd"), ("Order = +5", "")]
    for i, (o, note) in enumerate(rows):
        y = 66 + i*54
        same = bool(note)
        c.box(30, y, 300, 44, GRN if same else ACC)
        c.text(52, y+22, o, 14, TXT, "lm", mono=True)
        if note:
            c.text(308, y+22, note, 11, GRN, "rm")
        c.d.ellipse([(341)*S, (y+7)*S, (371)*S, (y+37)*S], fill=BG, outline=GRN, width=int(1.6*S))
        c.text(356, y+22, str(i+1), 15, GRN, "mm")
    c.line((394, 128), (394, 218), DIM, 1.2)
    c.line((394, 128), (388, 128), DIM, 1.2)
    c.line((394, 218), (388, 218), DIM, 1.2)
    c.text(402, 165, "tie", 11, DIM, "lm")
    c.text(402, 181, "-> FIFO", 11, DIM, "lm")
    c.line((466, 26), (466, 292), (60,66,72), 1.4, dash=True)
    c.text(504, 28, "Times: auto-unlisten when used up", 16, WRN, "lm")
    c.box(504, 56, 260, 50, WRN); c.text(634, 81, "Times = 3", 16, TXT, "mm")
    for i, (n, y) in enumerate([("3 -> 2", 130), ("2 -> 1", 176), ("1 -> 0", 222)]):
        c.text(516, y+14, f"after call {i+1}", 13, SUB, "lm")
        c.text(714, y+14, n, 14, TXT, "rm")
    c.text(634, 282, "hits 0  ->  auto Unlisten", 14, WRN, "mm")
    return c.out()

# ============================================================ 03 request / response (animated)
def f03(t):
    c = C(W, 310)
    c.box(60, 18, 220, 46); c.text(170, 41, "Requester", 16, TXT, "mm")
    c.box(620, 18, 220, 46); c.text(730, 41, "Responder", 16, TXT, "mm")
    c.line((170, 68), (170, 286), (70,76,84), 1.4, dash=True)
    c.line((730, 68), (730, 286), (70,76,84), 1.4, dash=True)
    c.arrow((172, 120), (722, 120), ACC, 2)
    c.text(446, 104, "Seq = 7   + args", 14, (207,233,247), "mm", mono=True)
    if t < 0.40:
        c.dot((172 + 550*ease(t/0.40), 120), ACC, 5)
    c.text(446, 168, "... async: frames or seconds later ...", 13, SUB, "mm")
    back = t > 0.55
    c.arrow((728, 225), (178, 225), WRN if back else (70,60,45), 2)
    _c = (255,217,160) if back else SUB
    c.text(378, 209, "Rsp.Response(...)", 14, _c, "mm", mono=True)   # monospace is for code only
    c.text(536, 209, "matched by Seq=7", 13, _c, "mm")                   # CJK needs the UI font; the mono face has no CJK glyphs
    if 0.55 < t < 0.95:
        c.dot((728 - 550*ease((t-0.55)/0.40), 225), WRN, 5)
    gone = t > 0.95
    c.box(45, 250, 250, 44, dash=True)
    c.text(170, 265, "the callback", 15, SUB if gone else TXT, "mm")
    c.text(170, 283, "fired once, then destroyed" if gone else "waiting for the response", 12, SUB, "mm")
    return c.out()

# ============================================================ 04 sticky messages (animated)
def f04(t):
    c = C(W, 300)
    c.arrow((60, 180), (860, 180), (100,108,116), 1.6)
    for x, lb in [(220, "t1"), (600, "t2")]:
        c.line((x, 168), (x, 192), (130,138,146), 1.4)
        c.text(x, 206, lb, 13, SUB, "mm")
    c.box(120, 92, 200, 52); c.text(220, 110, "Send", 16, TXT, "mm")
    c.text(220, 130, "no listener at this moment", 12, SUB, "mm")
    stored = t > 0.35
    c.box(470, 70, 220, 52, ACC if stored else DIM, (40,52,60) if stored else BOXF)
    c.text(580, 88, "latest value kept", 16, TXT if stored else SUB, "mm")
    c.text(580, 108, "StoreObjectMessage", 12, SUB, "mm")
    pts1 = [(322,112),(378,104),(414,96),(466,96)]   # arc from the broadcast box to the store box without dipping through the timeline
    c.curve(pts1, ACC if t > 0.05 else DIM, 2)
    if 0.05 < t < 0.40: c.dot(at(pts1, ease((t-0.05)/0.35)), ACC, 5)
    listen = t > 0.55
    c.box(510, 228, 220, 52, ACC if listen else DIM, (40,52,60) if listen else BOXF)
    c.text(620, 254, "Listen registers here", 16, TXT if listen else SUB, "mm")
    if t > 0.62:
        c.arrow((580, 126), (604, 222), WRN, 2)
        c.text(618, 178, "replayed at once", 13, WRN, "lm")
        if t < 0.92: c.dot((580 + 24*ease((t-0.62)/0.30), 126 + 96*ease((t-0.62)/0.30)), WRN, 5)
    c.box(60, 228, 300, 52, dash=True)
    c.text(210, 246, "a plain message instead", 15, SUB, "mm")
    c.text(210, 266, "nothing arrives at t2", 12, SUB, "mm")
    return c.out()

# ============================================================ 05 transparent script rewrite (animated)
def f05(t):
    c = C(W, 250)
    c.text(30, 26, "what the script author writes", 13, SUB, "lm")
    c.box(30, 40, 380, 66)
    c.text(48, 66, "NotifyObjectMessage(self,", 14, (207,233,247), "lm", mono=True)
    hi = 0.10 < t < 0.55
    c.text(48, 88, '  "Player.Hurt", dmg, causer)', 14, WRN if hi else (207,233,247), "lm", mono=True)
    c.arrow((420, 73), (520, 73), ACC, 2)
    c.text(470, 26, "load / compile time", 13, SUB, "mm")   # above the box: this label is too wide to sit inside
    if 0.2 < t < 0.75: c.dot((420 + 100*ease((t-0.2)/0.55), 73), ACC, 5)
    on = t > 0.60
    c.box(530, 40, 340, 66, GRN if on else DIM, (36,48,40) if on else BOXF)
    c.text(548, 66, "Notify_Player_Hurt(", 14, (200,230,201) if on else SUB, "lm", mono=True)
    c.text(548, 88, "  self, dmg, causer)", 14, (200,230,201) if on else SUB, "lm", mono=True)
    c.text(700, 124, "key-baked, strongly typed", 13, SUB if not on else GRN, "mm")
    chips = [("UnLua / slua","text rewrite at load"), ("AngelScript","pre-compile preprocessor"),
             ("Puerts","tsc AST transform"), ("C#","generics, no rewrite needed")]
    for i,(n,how) in enumerate(chips):
        x = 30 + i*215
        c.box(x, 172, 200, 58, dash=True)
        c.text(x+100, 192, n, 15, TXT, "mm"); c.text(x+100, 213, how, 12, SUB, "mm")
    return c.out()

# ============================================================ 06 RefEvent (animated)
def f06(t):
    c = C(W, 225)
    c.box(30, 36, 300, 100)
    c.text(46, 58, "C++", 13, SUB, "lm")
    c.text(46, 86, "int32 out = -1;", 14, (207,233,247), "lm", mono=True)
    c.text(46, 112, "FastInvoke(Obj, Func, 21, out);", 14, (207,233,247), "lm", mono=True)
    c.arrow((340, 70), (560, 70), ACC, 2)
    c.text(450, 20, "compile-time signature, no ProcessEvent", 13, SUB, "mm")   # same as 05: moved above the box
    if t < 0.35: c.dot((340 + 220*ease(t/0.35), 70), ACC, 5)
    on = t > 0.32
    c.box(570, 36, 300, 100, GRN if on else DIM, (36,48,40) if on else BOXF)
    c.text(586, 58, "Blueprint CustomEvent (void)", 13, SUB, "lm")
    c.text(586, 90, "out ← 21 * 2", 14, (200,230,201) if on else SUB, "lm", mono=True)
    c.text(586, 114, "writes to the out pin", 12, SUB, "lm")
    pts = [(566,122),(460,188),(300,188),(184,142)]
    back = t > 0.48
    c.curve(pts, WRN if back else (70,60,45), 2)
    if 0.48 < t < 0.92: c.dot(at(pts, ease((t-0.48)/0.44)), WRN, 5)
    c.text(380, 202, "the out param writes straight back into the C++ stack variable", 13, WRN if back else SUB, "mm")
    if t > 0.90:
        c.text(46, 160, "// out == 42", 14, (255,217,160), "lm", mono=True)
    return c.out()

# ============================================================ 07 lookup vs baked key
def f07():
    c = C(W, 250)
    c.line((330, 18), (330, 246), (70,76,84), 1.4, dash=True)
    c.text(318, 32, "compile time", 13, SUB, "rm"); c.text(344, 32, "runtime", 13, SUB, "lm")
    c.text(30, 84, "naive", 15, SUB, "lm")
    seq = [(346, 150, '"Common.Action"'), (516, 110, "FName"), (646, 120, "TMap lookup"), (786, 90, "store")]
    for i,(x,w,lb) in enumerate(seq):
        c.box(x, 60, w, 46, dash=True); c.text(x+w/2, 83, lb, 14, SUB, "mm")
        if i:
            px, pw, _ = seq[i-1][0], seq[i-1][1], 0
            c.arrow((px+pw+4, 83), (x-6, 83), DIM, 1.6, dash=True)
    c.text(30, 184, "key baked", 15, ACC, "lm")
    c.box(120, 160, 200, 46); c.text(220, 183, "C_STRING_TYPE", 14, (207,233,247), "mm", mono=True)
    c.arrow((324, 183), (470, 183), ACC, 2)
    c.box(480, 160, 220, 46, GRN, (36,48,40)); c.text(590, 183, "static store pointer", 15, TXT, "mm")
    c.text(590, 226, "no string, no lookup at runtime", 13, SUB, "mm")
    return c.out()

# ============================================================ 08 dispatch stack depth (measured)
# Measured by the TSTK case in GMPTests.cpp (real FPlatformStackWalk capture), modular build:
#   DebugGame    by-name 9 / by-store 7
#   Development  by-name 4 / by-store 3        (listener callback frame excluded)
def f08():
    H = 560
    PUR    = (186, 148, 255)
    DEADB  = (54, 58, 64)     # eliminated frames: low contrast so they do not compete
    DEADT  = (98, 105, 113)
    DEADF  = (30, 32, 35)
    STRIKE = (112, 80, 80)
    c = C(W, H)
    left = [("SendObjectMessageWrapperEx", 1), ("NotifyMessageImpl", 1), ("FireMsgBodyAdapt", 1),
            ("GMPFireWithSigSourceDirectRaw", 0), ("FireWithSigSourceCore", 0),
            ("FireWithSigSourceRaw lambda", 2), ("FlexBackendThunk::FlexThunk", 2),
            ("TGMPFunction::operator()", 2), ("RawUnpackThunk", 2)]
    Y0, ST, BW = 46, 34, 330
    c.text(40, 26, "unoptimized  -  by-name", 14, ACC, "lm")
    c.text(214, 27, "(DebugGame)", 11, SUB, "lm")
    c.text(370, 27, "9 GMP frames", 11, SUB, "rm")
    for i, (nm, kind) in enumerate(left):
        y = Y0 + i*ST
        dead = kind != 0
        c.box(40, y, BW, 28, DEADB if dead else ACC, DEADF if dead else BOXF)
        c.text(56, y+15, nm, 12, DEADT if dead else TXT, "lm", mono=True)
        if dead:
            c.line((52, y+14), (BW+28, y+14), STRIKE, 1.0)
    yb = Y0 + 9*ST
    c.box(40, yb, BW, 28, PUR, (44, 38, 58))
    c.text(56, yb+15, "your listener  [](int32 V){ ... }", 12, TXT, "lm", mono=True)
    # two groups of removed frames, each labelled with why it went
    for i0, i1, l1, l2 in [(0, 2, "collapsed by", "key-baking"),
                           (5, 8, "inlined by", "the optimizer")]:
        yt, yq = Y0 + i0*ST + 4, Y0 + i1*ST + 24
        c.line((BW+46, yt), (BW+46, yq), DEADB, 1.2)
        c.line((BW+46, yt), (BW+40, yt), DEADB, 1.2)
        c.line((BW+46, yq), (BW+40, yq), DEADB, 1.2)
        ym = (yt + yq) // 2
        c.text(BW+56, ym-8, l1, 11, DIM, "lm")
        c.text(BW+56, ym+8, l2, 11, DIM, "lm")
    # ---- right column: optimized + baked key ----
    X = 512
    c.text(X, 26, "optimized  -  key-baked", 14, GRN, "lm")
    c.text(X+186, 27, "(Development)", 11, SUB, "lm")
    c.text(X+358, 27, "3 GMP frames", 11, GRN, "rm")
    for i, (nm, hot) in enumerate([("NotifyMessageDirectRaw", 0), ("GMPFireWithSigSourceDirectRaw", 1),
                                   ("FireWithSigSourceCore", 1)]):
        y = Y0 + i*ST
        c.box(X, y, 358, 28, WRN if hot else GRN, (46, 41, 31) if hot else (34, 46, 38))
        c.text(X+16, y+15, nm, 12, TXT, "lm", mono=True)
    y3 = Y0 + 3*ST
    c.box(X, y3, 358, 28, PUR, (44, 38, 58))
    c.text(X+16, y3+15, "your listener  [](int32 V){ ... }", 12, TXT, "lm", mono=True)
    c.arrow((388, 60), (X-10, 60), GRN, 2)
    c.text(X+4, y3+56, "unpack, thunk and operator() are gone -", 12, SUB, "lm")
    c.text(X+4, y3+76, "the dispatch loop calls your callback directly.", 12, GRN, "lm")
    c.text(X+4, y3+116, "monolithic + INLINE_FIRE removes the two", 12, WRN, "lm")
    c.text(X+4, y3+136, "highlighted above: the cross-module wrapper", 11, SUB, "lm")
    c.text(X+4, y3+154, "goes away and the loop inlines into the caller.", 11, SUB, "lm")
    # The stack is shallow because the boundary is one 3-arg fn-ptr plus a Self address
    ay = 402
    c.box(40, ay, 820, 88, (62, 78, 92), (28, 34, 40))
    c.text(56, ay+20, "one stable C ABI at the callback boundary", 11, ACC, "lm")
    c.text(56, ay+45, "void (*)(void* Self, const FGMPTypedAddr* Params, const FGMPExtra* Extra)", 13, TXT, "lm", mono=True)
    c.text(56, ay+70, "Self is the callable's own address, not a vtable   -   C++ / Blueprint / UnLua / slua / Puerts / AngelScript / C# all land here", 10, SUB, "lm")
    c.text(40, H-52, "measured with FPlatformStackWalk inside the listener  (modular build)", 12, SUB, "lm")
    c.text(40, H-28, "GMP frames:   unoptimized  by-name 9 / by-store 7        optimized  by-name 4 / by-store 3", 12, SUB, "lm")
    return c.out()

# ============================================================ 09 C ABI hub
def f09():
    c = C(W, 336)
    c.box(40, 114, 190, 62); c.text(135, 138, "GMP core", 16, TXT, "mm")
    c.text(135, 158, "fire", 12, SUB, "mm")
    c.arrow((238, 145), (322, 145), ACC, 2)
    c.box(330, 84, 268, 120, WRN, (48,42,30), r=9)
    c.text(464, 106, "FGMPRawSig  erased to", 13, (255,217,160), "mm")
    for k, ln in enumerate(["void (*)(void* Self,", "         const FGMPTypedAddr* Params,",
                            "         const FGMPExtra* Extra)"]):
        c.text(344, 132 + k*20, ln, 10, (207,233,247), "lm", mono=True)
    c.text(464, 222, "one stable C boundary", 13, SUB, "mm")
    c.text(464, 242, "Self is the callable's address, no vtable", 10, DIM, "mm")
    c.text(464, 268, "Params[i] = one erased address; its FName rides inside", 9.5, DIM, "mm")
    c.text(464, 284, "only under GMP_WITH_TYPENAME (Editor / Development)", 9.5, DIM, "mm")
    c.text(464, 304, "Extra = Size, MsgKey, Seq, static TypeNames table", 9.5, DIM, "mm")
    langs = [("UnLua","push onto lua stack"), ("slua","cached pusher"), ("Puerts","to v8 value"),
             ("AngelScript","AS args"), ("C#","raw fn-ptr, zero marshal")]
    for i,(n,how) in enumerate(langs):
        y = 14 + i*58
        c.curve([(602,145),(640,145),(646,y+24),(672,y+24)], ACC, 2)
        c.box(680, y, 210, 48)
        c.text(785, y+18, n, 15, TXT, "mm"); c.text(785, y+36, how, 12, SUB, "mm")
    return c.out()


# ============================================================ blueprint node drawing (shared by 10 and 11)
PEXEC = (238, 238, 238); PFLT = (150, 236, 120); PBOOL = (206, 74, 74)
POBJ  = (72, 148, 244);  PNAME = (198, 132, 232)

def _pin(c, x, y, col, exec_=False):
    if exec_:
        c.d.polygon([((x-5)*S, (y-6)*S), ((x+5)*S, y*S), ((x-5)*S, (y+6)*S)], fill=col)
    else:
        c.d.ellipse([(x-5)*S, (y-5)*S, (x+5)*S, (y+5)*S], fill=col)

def _node(c, x, y, w, title, tag, ins, outs, tcol=ACC):
    top = 46 + (26 if tag else 0)
    h = top + max(len(ins), len(outs)) * 26 + 12
    c.box(x, y, w, h, tcol, (33, 36, 41), r=8)
    c.d.rounded_rectangle([x*S, y*S, (x+w)*S, (y+32)*S], radius=8*S, fill=(46, 52, 60))
    c.d.rectangle([x*S, (y+24)*S, (x+w)*S, (y+32)*S], fill=(46, 52, 60))
    c.text(x + w//2, y + 16, title, 13, TXT, "mm")
    if tag:
        c.box(x + 12, y + 40, w - 24, 24, WRN, (48, 42, 30), r=5, width=1.2)
        c.text(x + 22, y + 52, "Tag", 10, SUB, "lm")
        c.text(x + 52, y + 52, tag, 12, (255, 217, 160), "lm", mono=True)
    for i, (lb, col, ex) in enumerate(ins):
        yy = y + top + 13 + i*26
        _pin(c, x + 14, yy, col, ex)
        c.text(x + 28, yy, lb, 11, TXT if ex else SUB, "lm")
    for i, (lb, col, ex) in enumerate(outs):
        yy = y + top + 13 + i*26
        _pin(c, x + w - 14, yy, col, ex)
        c.text(x + w - 28, yy, lb, 11, TXT if ex else SUB, "rm")
    return h

# ============================================================ 10 self-describing, self-validating
def f10():
    c = C(W, 330)
    c.text(40, 26, "self-describing, self-validating", 15, ACC, "lm")
    c.text(40, 48, "pins are a projection of the signature table - pick a tag and they are there, nothing to configure by hand", 11, SUB, "lm")
    _node(c, 40, 74, 300, "Notify Message", "Player.Hurt",
          [("", PEXEC, True), ("Target", POBJ, False), ("Damage", PFLT, False), ("Causer", POBJ, False)],
          [("", PEXEC, True)])
    _node(c, 560, 74, 300, "Notify Message", "Game.Ready",
          [("", PEXEC, True), ("bReady", PBOOL, False)],
          [("", PEXEC, True)])
    c.arrow((360, 150), (540, 150), GRN, 2)
    c.text(450, 132, "switch the tag", 12, GRN, "mm")
    c.text(450, 174, "pins rebuild", 11, SUB, "mm")
    c.text(450, 192, "wires already connected", 11, SUB, "mm")
    c.text(450, 210, "keep their identity", 11, SUB, "mm")
    c.text(40, 282, "connect the wrong type and the Blueprint compile rejects it - the check does not wait for runtime", 11, SUB, "lm")
    c.text(40, 306, "pin identity is a PersistentGuid, so rebuilding the node does not drop the links you already made", 11, DIM, "lm")
    return c.out()

# ============================================================ 11 NeuronAction - the HTTP node GMP ships
# Taken from UGMPJsonHttpUtils::HttpPostRequestWild in GMPJsonUtils.h - the class opts in with meta=(NeuronAction)
def f11():
    c = C(W, 596)
    c.text(40, 26, "NeuronAction: one async action is one node", 15, ACC, "lm")
    c.text(40, 48, "GMP ships one - UGMPJsonHttpUtils. The class opts in, the delegate parameter becomes an output exec pin.", 11, SUB, "lm")
    code = ['UCLASS(meta = (NeuronAction))',
            'class UGMPJsonHttpUtils : public UBlueprintFunctionLibrary',
            '',
            'DECLARE_DYNAMIC_DELEGATE_TwoParams(FGMPJsonResponseDelegate, bool, bSucc, int32, RspCode);',
            '',
            'UFUNCTION(BlueprintCallable, CustomThunk, BlueprintInternalUseOnly, meta = (',
            '    DisplayName = "GMPHttpPostRequest", NeuronAction, WorldContext = InCtx,',
            '    CustomStructureParam = "RequestStruct,ResponseStruct", AdvancedDisplay = "Headers,TimeoutSecs"))',
            'static void HttpPostRequestWild(const UObject* InCtx, const FString& Url,',
            '        const TMap<FString, FString>& Headers, float TimeoutSecs,',
            '        const FGMPJsonResponseDelegate& OnHttpResponse, int32 ConvertFlags,',
            '        const int32& RequestStruct, int32& ResponseStruct);']
    c.box(40, 68, 820, 232, (62, 78, 92), (28, 32, 38))
    for i, ln in enumerate(code):
        col = WRN if ('NeuronAction' in ln or 'DECLARE_DYNAMIC' in ln) else TXT
        c.text(58, 90 + i*18, ln, 10, col, "lm", mono=True)
    _node(c, 220, 322, 460, "GMP Http Post Request", "",
          [("", PEXEC, True), ("Url", PSTR, False), ("Request Struct", PWILD, False),
           ("Convert Flags", PINT, False), ("Headers", PSTR, False), ("Timeout Secs", PFLT, False)],
          [("", PEXEC, True), ("On Http Response", PEXEC, True), ("bSucc", PBOOL, False),
           ("RspCode", PINT, False), ("Response Struct", PWILD, False)], tcol=GRN)
    c.text(212, 417, "wildcard - typed by", 10.5, DIM, "rm")
    c.text(212, 433, "whatever you plug in", 10.5, DIM, "rm")
    c.text(688, 407, "- the delegate parameter", 10.5, DIM, "lm")
    c.text(688, 477, "- wildcard again: the response JSON", 10.5, DIM, "lm")
    c.text(688, 493, "  lands here before this fires", 10.5, DIM, "lm")
    c.text(40, 556, "the response JSON is deserialized straight into the struct you plug in - no manual parsing, no proxy object to hold", 11, SUB, "lm")
    c.text(40, 578, "same node family: GMPHttpGetRequest, and any UFUNCTION you tag with NeuronAction", 11, DIM, "lm")
    return c.out()

# ============================================================ 12 jump tracing
def f12():
    c = C(W, 330)
    c.text(40, 26, "the message is decoupled - the call site is not lost", 15, ACC, "lm")
    steps = [("script calls", ["NotifyObjectMessage(...)", "at player.lua:42"], ACC, True),
             ("engine debug API", ["already maintained", "by each language"], WRN, False),
             ("GMP records", ["MsgKey  ->", '"player.lua:42"'], GRN, True),
             ("panel click", ["IDE opens that file", "on that line"], GRN, False)]
    for i, (t, lines, col, mono) in enumerate(steps):
        x = 40 + i*212
        c.box(x, 64, 190, 88, col)
        c.text(x + 95, 86, t, 13, TXT, "mm")
        for k, ln in enumerate(lines):
            c.text(x + 95, 110 + k*20, ln, 10.5, SUB, "mm", mono=mono)
        if i < 3:
            c.arrow((x + 194, 108), (x + 210, 108), DIM, 1.8)
    for k, ln in enumerate(["lua        debug library", "Puerts     v8 StackTrace",
                            "AS         active context", "C#         CallerFilePath"]):
        c.text(254, 184 + k*20, ln, 10.5, DIM, "lm", mono=True)
    c.text(40, 296, "GMP only reads what those engines already track - none of them is patched", 11, DIM, "lm")
    return c.out()

# ============================================================ 14 IntelliSense
def f14():
    c = C(W, 340)
    c.text(40, 26, "one signature table feeds every language's hints", 15, ACC, "lm")
    c.box(40, 58, 280, 76, WRN, (48, 42, 30))
    c.text(58, 82, "Player.Hurt", 12, (255, 217, 160), "lm", mono=True)
    c.text(58, 108, "(float Damage, AActor* Causer)", 10.5, SUB, "lm", mono=True)
    c.text(180, 150, "collected in the editor", 10.5, DIM, "mm")
    c.arrow((330, 96), (382, 96), GRN, 2)
    c.text(356, 78, "codegen", 10.5, GRN, "mm")
    for i, (n, w) in enumerate([("UnLua / slua", "---@param annotations"), ("Puerts", "gmp_messages.d.ts"),
                                ("AngelScript", "declaration stubs"), ("C#", "generic MsgTag<T...>")]):
        y = 54 + i*42
        c.box(396, y, 464, 34, ACC)
        c.text(414, y + 17, n, 11, TXT, "lm")
        c.text(842, y + 17, w, 10.5, SUB, "rm")
    c.box(40, 238, 820, 76, RED, (44, 32, 32))
    c.text(58, 264, 'Notify_Player_Hurt("10", Causer)', 13, TXT, "lm", mono=True)
    x0 = 58 + 19*7.16; x1 = 58 + 23*7.16
    k = 0
    while x0 + k*4 < x1:
        xa = x0 + k*4
        c.line((xa, 274 + (0 if k % 2 else 3)), (xa + 4, 274 + (3 if k % 2 else 0)), RED, 1.4)
        k += 1
    c.text(58, 296, "expected number, got string", 10.5, RED, "lm")
    c.text(842, 264, "the wrong type is flagged where you type it,", 11, SUB, "rm")
    c.text(842, 286, "not at runtime inside someone else's module", 11, SUB, "rm")
    return c.out()

PSTR = (245, 92, 200); PINT = (44, 200, 178); PWILD = (152, 158, 166)

# ============================================================ 15 coupling through the build graph
def f15():
    c = C(W, 340)
    c.text(40, 26, "static delegates couple you through the build graph", 15, ACC, "lm")
    c.box(330, 56, 240, 54, RED, (44, 32, 32))
    c.text(450, 74, "Shared.h", 14, TXT, "mm", mono=True)
    c.text(450, 94, "the delegate signature", 10.5, SUB, "mm")
    for i, m in enumerate(["Gameplay", "UI", "Audio", "Net", "Plugin"]):
        x = 40 + i*172
        c.box(x, 186, 152, 44, RED, (40, 30, 30))
        c.text(x + 76, 202, m, 12.5, TXT, "mm")
        c.text(x + 76, 219, "rebuild", 10, RED, "mm")
        c.line((450, 112), (x + 76, 184), RED, 1.3, dash=True)
    c.text(40, 264, "change one type in that header and every translation unit including it rebuilds", 11.5, SUB, "lm")
    c.text(40, 288, "the coupling lives in the build graph, so deleting a module is a compile error, not a decision", 11.5, SUB, "lm")
    c.text(40, 316, "and Blueprint is a second system on the side: Interface + Dispatcher, wired by hand", 11, DIM, "lm")
    return c.out()

# ============================================================ 16 one key, no shared header
def f16():
    c = C(W, 340)
    c.text(40, 26, "sender and listener share a string, not a header", 15, ACC, "lm")
    c.box(40, 58, 330, 86, ACC)
    c.text(58, 80, "NotifyMessage(", 11, SUB, "lm", mono=True)
    c.text(58, 100, '  MSGKEY("Common.Action"), P1, P2)', 11, TXT, "lm", mono=True)
    c.text(58, 128, "sender module", 10.5, DIM, "lm")
    c.box(530, 58, 330, 86, GRN)
    c.text(548, 80, "ListenMessage(", 11, SUB, "lm", mono=True)
    c.text(548, 100, '  MSGKEY("Common.Action"), this, cb)', 11, TXT, "lm", mono=True)
    c.text(548, 128, "listener module", 10.5, DIM, "lm")
    c.box(392, 84, 116, 34, WRN, (48, 42, 30), r=17)
    c.text(450, 101, "Common.Action", 10.5, (255, 217, 160), "mm", mono=True)
    c.line((374, 101), (388, 101), DIM, 1.4, dash=True)
    c.line((512, 101), (526, 101), DIM, 1.4, dash=True)
    for i, n in enumerate(["C++", "Blueprint", "Script x5"]):
        x = 300 + i*112
        c.box(x, 206, 100, 34, ACC)
        c.text(x + 50, 223, n, 11.5, TXT, "mm")
        c.curve([(x + 50, 204), (x + 50, 180), (450, 168), (450, 122)], DIM, 1.4)
    c.text(450, 264, "the same key, from every side of the project", 11.5, SUB, "mm")
    c.text(40, 300, "no shared type header between the two modules, and no compile-time link either", 11.5, SUB, "lm")
    c.text(40, 322, "delete one side and the other still compiles - that is the whole point", 11, DIM, "lm")
    return c.out()

# ============================================================ 17 capability map
def f17():
    c = C(W, 380)
    c.text(40, 26, "what it does today", 15, ACC, "lm")
    cards = [("Dispatch", ["object / world / global", "one-way, narrow to broad"], ACC),
             ("Listen", ["times limit, auto-unlisten", "order, ties keep FIFO"], ACC),
             ("Interact", ["request / response", "sticky and once-only"], ACC),
             ("Infer", ["signature learned", "from first use"], ACC),
             ("Blueprint", ["self-describing pins", "self-validating signatures"], GRN),
             ("Script", ["UnLua  slua  Puerts", "AngelScript  C#"], GRN),
             ("Interop", ["call BP events from C++", "inline hook"], GRN),
             ("Tooling", ["jump back to source", "IntelliSense per language"], GRN)]
    for i, (t, lines, col) in enumerate(cards):
        x = 30 + (i % 4)*215
        y = 60 + (i // 4)*140
        c.box(x, y, 195, 118, col)
        c.text(x + 97, y + 30, t, 14, TXT, "mm")
        for k, ln in enumerate(lines):
            c.text(x + 97, y + 62 + k*22, ln, 10.5, SUB, "mm")
    c.text(40, 356, "the first half of this talk is what these do; the second half is why none of it costs you", 11, DIM, "lm")
    return c.out()

# ============================================================ 18 key baking
def f18():
    c = C(W, 340)
    c.text(40, 26, "the string is gone before the program runs", 15, ACC, "lm")
    steps = [('C_STRING_TYPE("Common.Action")', ["a compile-time type,", "not a runtime string"], WRN),
             ("GetKeySlot<KeyT>()", ["one static slot", "per process"], ACC),
             ("store pointer", ["address settled", "at compile time"], GRN)]
    for i, (t, sub, col) in enumerate(steps):
        x = 40 + i*286
        c.box(x, 62, 266, 84, col)
        c.text(x + 133, 86, t, 11.5, TXT, "mm", mono=True)
        for k, ln in enumerate(sub):
            c.text(x + 133, 108 + k*18, ln, 10.5, SUB, "mm")
        if i < 2:
            c.arrow((x + 270, 104), (x + 282, 104), DIM, 1.8)
    c.box(40, 182, 400, 78, GRN, (34, 46, 38))
    c.text(60, 206, "monolithic", 12, GRN, "lm")
    c.text(60, 232, "one field read - nothing else on the hot path", 11, SUB, "lm")
    c.box(460, 182, 400, 78, WRN, (48, 42, 30))
    c.text(480, 206, "modular", 12, WRN, "lm")
    c.text(480, 232, "resolved once on first call, then cached in the slot", 11, SUB, "lm")
    c.text(40, 296, "either way the hash lookup happens zero times per send", 11.5, SUB, "lm")
    c.text(40, 320, "the slot is a Meyers singleton, so vague linkage cannot hand out two of them", 11, DIM, "lm")
    return c.out()

# ============================================================ 19 handy bits
def f19():
    c = C(W, 340)
    c.text(40, 26, "the small things you end up using every day", 15, ACC, "lm")
    items = [("FSigHandle", "RAII - unlisten on destruction, safe for non-UObject owners"),
             ("CreateWeakLambda", "this plus a lambda, in one line, without lifetime worries"),
             ("LocalSharedStorage", "named shared data scoped to a World, type safe"),
             ("RpcMessageUtils", "a MSGKEY is the RPC interface, over UE's own serialization"),
             ("Archive / Json / Protobuf", "three serializers, all bridged to UStruct reflection"),
             ("signature checking", "on in Editor to catch mistakes, compiled out in Shipping")]
    for i, (t, d) in enumerate(items):
        y = 58 + i*46
        c.box(40, y, 820, 38, ACC if i < 3 else GRN)
        c.text(58, y + 19, t, 12, TXT, "lm", mono=True)
        c.text(842, y + 19, d, 11, SUB, "rm")
    return c.out()

# ============================================================ 20 the last hop
def f20():
    c = C(W, 360)
    c.text(40, 26, "the last hop: one indirect jump", 15, ACC, "lm")
    c.text(40, 62, "virtual dispatch", 12, DIM, "lm")
    for i, (t, sub) in enumerate([("object", "load vptr"), ("vtable", "load slot"), ("target", "call")]):
        x = 40 + i*136
        c.box(x, 82, 116, 48, (58, 62, 68), (30, 32, 35))
        c.text(x + 58, 98, t, 11.5, (120, 127, 135), "mm")
        c.text(x + 58, 116, sub, 10, (98, 105, 113), "mm")
        if i < 2:
            c.arrow((x + 120, 106), (x + 132, 106), (72, 78, 84), 1.6)
    c.text(452, 106, "three dependent loads before you know where you are going", 11, DIM, "lm")
    c.text(40, 168, "GMP", 12, GRN, "lm")
    c.box(40, 188, 300, 62, GRN, (34, 46, 38))
    c.text(58, 210, "thunk fn-ptr", 11.5, TXT, "lm", mono=True)
    c.text(58, 232, "object address", 11.5, TXT, "lm", mono=True)
    c.text(322, 221, "16B slot", 10, DIM, "rm")
    c.arrow((348, 219), (392, 219), GRN, 2)
    c.box(400, 188, 460, 62, GRN, (34, 46, 38))
    c.text(418, 210, "reinterpret_cast<R(*)(void*,Args...)>(...)(Obj, Args...)", 10, TXT, "lm", mono=True)
    c.text(418, 232, "in tail position - clang emits jmp, no new frame", 10.5, GRN, "lm")
    c.text(40, 288, "the pointer is already in hand: nothing to load, nothing to guess", 11.5, SUB, "lm")
    c.text(40, 312, "small lambdas live inline in that 16-byte slot, so the callable is not on the heap either", 11, DIM, "lm")
    c.text(40, 338, "one runtime-decided jump is the floor - the sender is not supposed to know who listens", 11, DIM, "lm")
    return c.out()

# ============================================================ 21 what remains of one message
def f21():
    c = C(W, 330)
    c.text(40, 26, "what is left of one message", 15, ACC, "lm")
    rows = [("key to store", "a pointer settled at compile time"),
            ("dispatch", "inlined into the caller"),
            ("the last hop", "one jmp, no new frame"),
            ("argument packing", "expanded at compile time"),
            ("signature check", "not in the Shipping binary at all"),
            ("script call", "codegen + baked key + direct C ABI")]
    for i, (a, b) in enumerate(rows):
        y = 58 + i*42
        c.box(40, y, 300, 34, DIM, (32, 34, 38))
        c.text(58, y + 17, a, 11.5, SUB, "lm")
        c.arrow((348, y + 17), (378, y + 17), GRN, 1.8)
        c.box(386, y, 474, 34, GRN, (34, 46, 38))
        c.text(404, y + 17, b, 11.5, TXT, "lm")
    return c.out()

# ============================================================ 22 two halves of the same trade
def f22():
    c = C(W, 340)
    c.text(40, 26, "two halves of the same trade", 15, ACC, "lm")
    c.box(40, 60, 400, 176, ACC)
    c.text(240, 86, "written to be easy", 13, ACC, "mm")
    for k, ln in enumerate(["three dispatch layers", "times and order", "request / response",
                            "sticky messages", "signature inference", "self-describing nodes"]):
        c.text(72, 116 + k*20, ln, 11, SUB, "lm")
    c.box(460, 60, 400, 176, GRN)
    c.text(660, 86, "paid for at compile time", 13, GRN, "mm")
    for k, ln in enumerate(["baked keys", "inlined dispatch", "tail call on the last hop",
                            "one C ABI for five languages", "codegen per tag", "checks compiled out"]):
        c.text(492, 116 + k*20, ln, 11, SUB, "lm")
    c.box(40, 264, 820, 48, WRN, (48, 42, 30))
    c.text(450, 288, "if it can be settled at compile time, do not leave it for runtime", 13, (255, 217, 160), "mm")
    return c.out()

# ==================================================== 13 signature inference (animated)
def f13(t):
    c = C(W, 340)
    # ---- top: the signature table ----
    c.text(40, 34, "signature table", 15, ACC, "lm")
    c.text(40, 56, '"Player.Hurt"', 13, SUB, "lm", mono=True)
    reg = t > 0.16
    if not reg:
        c.box(250, 26, 300, 46, dash=True)
        c.text(400, 49, "not registered yet", 14, SUB, "mm")
    chips = [("int32", 250), ("float", 372), ("UObject*", 494)]
    grown = t > 0.62
    for i, (ty, x) in enumerate(chips):
        if i == 2 and not grown: continue
        if not reg: break
        fresh = (i == 2 and t < 0.74) or (i < 2 and t < 0.30)
        pre = 0.36 < t < 0.56 and i == 0            # highlight the first entry while the listener takes only a prefix
        col = WRN if fresh else (GRN if pre else ACC)
        c.box(x, 26, 112, 46, col, (48,42,30) if fresh else ((36,48,40) if pre else BOXF))
        c.text(x+56, 49, ty, 15, TXT, "mm", mono=True)
    if grown:
        c.text(614, 49, "<- grew at the tail", 13, WRN if t < 0.80 else SUB, "lm")

    # ---- bottom: the call sequence ----
    rows = [
        (0.16, "Notify(\"Player.Hurt\", 42, 0.5f)",           "first use -> signature registered", ACC),
        (0.36, "Listen(\"Player.Hurt\", [](int32 dmg){})",     "fewer params: prefix matched",       GRN),
        (0.62, "Notify(\"Player.Hurt\", 42, 0.5f, Causer)",    "extra trailing arg -> table grows",  WRN),
        (0.84, "the Listen above keeps working",               "old listeners untouched",            GRN),
    ]
    for i, (t0, code, note, col) in enumerate(rows):
        y = 104 + i*50
        on = t > t0
        c.box(40, y, 470, 42, col if on else DIM, BOXF)
        c.text(58, y+21, code, 13, TXT if on else SUB, "lm", mono=(i < 3))
        if on:
            c.text(530, y+21, note, 13, col, "lm")
    c.text(40, 322, "rule:  listener params <= sender params,  and the shared prefix types must match", 13, SUB, "lm")
    return c.out()

# ---------------------------------------------------------------- run
# ============================================================ 23 Class2Name / TClass2Prop
def f23():
    c = C(W, 330)
    c.text(40, 26, "one C++ type, three representations", 15, ACC, "lm")
    c.text(40, 48, "every recorded signature, every script declaration and every serialiser goes through these", 11, SUB, "lm")
    cols = [("C++ type", ["FVector", "int32", "AActor*"], ACC),
            ("FName", ["\"Vector\"", "\"int\"", "\"Actor\""], WRN),
            ("FProperty*", ["FStructProperty", "FIntProperty", "FObjectProperty"], GRN)]
    for i, (t, rows, col) in enumerate(cols):
        x = 40 + i*284
        c.text(x + 120, 82, t, 13, col, "mm")
        for k, r in enumerate(rows):
            y = 104 + k*54
            c.box(x, y, 240, 42, col)
            c.text(x + 120, y + 21, r, 12, TXT, "mm", mono=True)
            if i < 2:
                c.arrow((x + 246, y + 21), (x + 278, y + 21), DIM, 1.6)
    c.text(202, 288, "Class2Name", 11, WRN, "mm")
    c.text(486, 288, "TClass2Prop::GetProperty()", 11, GRN, "mm")
    c.text(40, 314, "only reflected types map; a plain C++ struct has no name and no property, so it stays inside C++", 11, DIM, "lm")
    return c.out()

# ============================================================ 24 GMPArchive
def f24():
    c = C(W, 320)
    c.text(40, 26, "why not a plain FMemoryWriter", 15, ACC, "lm")
    c.box(40, 58, 400, 96, RED, (44, 32, 32))
    c.text(60, 82, "FMemoryWriter", 13, RED, "lm")
    c.text(60, 108, "Ar << Object;", 11.5, TXT, "lm", mono=True)
    c.text(60, 130, "writes the raw pointer - meaningless elsewhere", 10.5, SUB, "lm")
    c.box(460, 58, 400, 96, GRN, (34, 46, 38))
    c.text(480, 82, "FGMPMemoryArchive", 13, GRN, "lm")
    c.text(480, 108, "virtual FArchive& operator<<(UObject*&)", 10.5, TXT, "lm", mono=True)
    c.text(480, 130, "resolves the reference on the far side", 10.5, SUB, "lm")
    c.text(450, 106, "vs", 12, DIM, "mm")
    for i, (n, d, col) in enumerate([("FGMPMemoryWriter", "sized buffer", ACC), ("FGMPMemoryReader", "sized buffer", ACC),
                                     ("FGMPNetBitWriter", "UPackageMap aware", WRN), ("FGMPNetBitReader", "UPackageMap aware", WRN)]):
        x = 40 + (i % 2)*424; y = 186 + (i // 2)*56
        c.box(x, y, 396, 44, col)
        c.text(x + 16, y + 22, n, 12, TXT, "lm", mono=True)
        c.text(x + 380, y + 22, d, 10.5, SUB, "rm")
    c.text(40, 302, "the net pair rides UE's own replication path, so object references survive the wire", 11, DIM, "lm")
    return c.out()

# ============================================================ 25 FRpcMessageUtils
def f25():
    c = C(W, 320)
    c.text(40, 26, "a MSGKEY used as the RPC interface", 15, ACC, "lm")
    steps = [("Z_PostRPC", ["args -> FProperty*", "via TClass2Prop"], ACC),
             ("net archive", ["FGMPNetBitWriter", "UPackageMap"], ACC),
             ("PostRPCMsg", ["reliable or not", "bounded by GetMaxBytes"], WRN),
             ("Z_VerifyRPC", ["key + property list", "checked before decode"], GRN)]
    for i, (t, lines, col) in enumerate(steps):
        x = 40 + i*212
        c.box(x, 70, 190, 92, col)
        c.text(x + 95, 94, t, 13, TXT, "mm", mono=True)
        for k, ln in enumerate(lines):
            c.text(x + 95, 120 + k*20, ln, 10.5, SUB, "mm")
        if i < 3:
            c.arrow((x + 194, 116), (x + 210, 116), DIM, 1.8)
    c.text(135, 186, "sender", 11, DIM, "mm")
    c.text(771, 186, "receiver", 11, GRN, "mm")
    c.box(40, 214, 820, 62, WRN, (48, 42, 30))
    c.text(58, 238, "an RPC surface addressed by string is reachable with any string a client can build.", 12, (255, 217, 160), "lm")
    c.text(58, 260, "Z_VerifyRPC is part of the contract, not a debug aid.", 12, TXT, "lm")
    c.text(40, 302, "no UFUNCTION per remote call; the message key is the interface", 11, DIM, "lm")
    return c.out()


# ============================================================ 26 collection: three ways to take one table (static)
def f26():
    c = C(W, 348)
    c.text(40, 26, "one stored TArray<FItem>, three ways to take it", 15, ACC, "lm")

    rows = ["1001  Potion   3", "1002  Elixir   5", "1003  Ether    2", "1004  Ration   9"]
    c.box(40, 56, 250, 24 + len(rows)*26, ACC)
    c.text(60, 72, "StoreObjectMessage", 11, SUB, "lm")
    for i, r in enumerate(rows):
        y = 92 + i*26
        c.text(60, y, r, 12, TXT if i != 2 else WRN, "lm", mono=True)
        c.text(46, y, str(i), 10, DIM if i != 2 else WRN, "lm", mono=True)
    c.text(165, 198, "row 2 just changed", 11, WRN, "mm")

    lanes = [(64,  "const TArray<FItem>& All", "the list itself: count and structure", GRN, "wakes"),
             (152, "int32 Row, int32 Id, ...",  "every changed row, expanded",         ACC, "wakes for row 2"),
             (240, "int32 Id, const FString&", "slot 2 only",                          ACC, "wakes")]
    for y, sig, what, col, tag in lanes:
        c.box(370, y, 340, 62, col)
        c.text(388, y + 22, sig, 12, TXT, "lm", mono=True)
        c.text(388, y + 44, what, 11.5, SUB, "lm")
        c.arrow((292, 130), (366, y + 31), col, 1.8)
        c.text(760, y + 31, tag, 12, col, "lm")

    c.box(40, 232, 250, 76, dash=True)
    c.text(58, 254, "a trailing", 11, SUB, "lm")
    c.text(58, 274, "const FGMPStoreUpdate&", 11, TXT, "lm", mono=True)
    c.text(58, 294, "is what opts a lambda in", 11, SUB, "lm")
    c.text(370, 330, "a slot past the end stays quiet unless it subscribed with WithRemoval(n)",
           11, DIM, "lm")
    return c.out()

# ============================================================ 27 collection: what wakes a slot (animated)
def f27(t):
    c = C(W, 344)
    edit = t < 0.5                     # first half: edit row 2. second half: insert at row 2
    c.text(40, 26, "editing row 2" if edit else "inserting at row 2", 15, WRN if edit else GRN, "lm")
    c.text(40, 48, "content changed, count did not" if edit else "everything from 2 on shifted down",
           12, SUB, "lm")

    listeners = 5                      # slot listeners that exist, one per visible row before the insert
    for i in range(listeners if edit else listeners + 1):
        y = 84 + i*40
        moved = (not edit) and i >= 2
        touched = (edit and i == 2) or moved
        col = (WRN if edit else GRN) if touched else DIM
        c.box(300, y, 190, 30, col, (48,42,30) if (touched and edit) else ((36,48,40) if touched else BOXF))
        c.text(395, y + 15, "row %d" % i, 12, TXT if touched else SUB, "mm", mono=True)
        c.text(280, y + 15, str(i), 11, DIM, "rm", mono=True)
        if i < listeners:
            c.box(560, y, 230, 30, col if touched else DIM, BOXF)
            c.text(575, y + 15, "slot %d listener" % i, 12, TXT if touched else SUB, "lm")
            if touched:
                c.arrow((494, y + 15), (556, y + 15), col, 1.8)
                c.text(806, y + 15, "wakes", 11, col, "lm")
        else:
            c.box(560, y, 230, 30, dash=True)
            c.text(575, y + 15, "the list widget adds it", 11, SUB, "lm")

    c.box(40, 84, 220, 70, ACC)
    c.text(150, 106, "TotalCount", 12, SUB, "mm")
    c.text(150, 130, "5" if edit else "5 -> 6", 17, TXT if edit else GRN, "mm", mono=True)
    c.box(40, 172, 220, 82, dash=True)
    c.text(150, 194, "Ranges", 12, SUB, "mm")
    c.text(150, 216, "{2,1}" if edit else "{2,4}", 15, WRN if edit else GRN, "mm", mono=True)
    c.text(150, 240, "one row" if edit else "widened to the tail", 11, SUB, "mm")
    c.text(40, 326, "no add / change / remove enum: the count says whether it resized, the index says where",
           11, DIM, "lm")
    return c.out()

# ============================================================ 28 collection: a virtual list across modules (static)
def f28():
    c = C(W, 330)
    c.text(40, 26, "what it is for: a virtual list whose rows outlive their data", 15, ACC, "lm")

    c.box(40, 60, 250, 96, GRN)
    c.text(60, 82, "gameplay module", 11, SUB, "lm")
    c.text(60, 106, "TGMPStoredArray<FItem>", 12, (200,230,201), "lm", mono=True)
    c.text(60, 128, "Arr.GetMutable(5).Count -= 1", 11, TXT, "lm", mono=True)
    c.text(60, 146, "Arr.Add(...)", 11, TXT, "lm", mono=True)
    c.text(165, 174, "one fire when the scope ends", 11, SUB, "mm")

    c.box(340, 60, 220, 60, ACC)
    c.text(450, 80, "the stored table", 12, TXT, "mm")
    c.text(450, 102, "FItem lives here", 11, SUB, "mm")
    c.arrow((294, 100), (336, 90), GRN, 2)

    c.box(620, 44, 240, 54, ACC)
    c.text(640, 64, "list widget", 12, TXT, "lm")
    c.text(640, 84, "SetItemCount(TotalCount)", 11, SUB, "lm", mono=True)
    c.arrow((564, 84), (616, 71), ACC, 1.8)

    for i, y in enumerate((112, 168, 224)):
        hit = (i + 3) == 5                       # the row the gameplay side just edited
        c.box(620, y, 240, 44, ACC if hit else DIM)
        c.text(640, y + 22, "row widget  index %d" % (i + 3), 12, TXT if hit else SUB, "lm")
        c.arrow((564, 96 + i*8), (616, y + 22), ACC if hit else DIM, 1.8 if hit else 1.4)
    c.text(740, 286, "only the row whose content moved is called", 11, ACC, "mm")

    c.box(40, 200, 250, 96, WRN, (48,42,30))
    c.text(60, 222, "ui module", 11, (255,217,160), "lm")
    c.text(60, 246, "[](int32 Id,", 11, TXT, "lm", mono=True)
    c.text(60, 264, "  const FString& Name,", 11, TXT, "lm", mono=True)
    c.text(60, 282, "  int32 Count){ ... }", 11, TXT, "lm", mono=True)
    c.text(165, 314, "never includes FItem", 11, WRN, "mm")
    c.arrow((294, 248), (616, 246), WRN, 1.6, dash=True)
    return c.out()


# ============================================================ 29 collection: what a stored copy buys
def f29():
    c = C(W, 376)
    c.text(40, 26, "row dispatch needs no store; knowing which rows changed does", 15, ACC, "lm")

    # left: plain send -- the table is the caller's argument
    c.box(40, 58, 380, 74, WRN, (48, 42, 30))
    c.text(60, 80, "SendObjectMessage(Obj, K, MyItems)", 12, TXT, "lm", mono=True)
    c.text(60, 104, "the table is the caller's argument", 11, (255, 217, 160), "lm")
    c.text(60, 122, "gone when the call returns", 11, SUB, "lm")

    # right: store -- the kept copy is the previous table
    c.box(480, 58, 380, 74, GRN)
    c.text(500, 80, "StoreObjectMessage(Obj, K, MyItems)", 12, TXT, "lm", mono=True)
    c.text(500, 104, "the kept copy is the previous table", 11, (200, 230, 201), "lm")
    c.text(500, 122, "which is what a diff compares against", 11, SUB, "lm")

    # the three subscription shapes, one row each, answered on both sides
    shapes = [(172, "AllRows", "every row, expanded",      True,  "yes",      True),
              (238, "slot 5",  "only when row 5 differs",  False, "yes",      True),
              (304, "a late listener", "subscribed after the fire", False, "replayed", False)]
    for y, name, what, on_send, store_tag, is_code in shapes:
        c.text(60, y, name, 12, TXT, "lm", mono=is_code)
        c.text(60, y + 18, what, 11, SUB, "lm")
        c.text(360, y + 6, "yes" if on_send else "never", 12, GRN if on_send else RED, "mm")
        c.text(800, y + 6, store_tag, 12, GRN, "mm")
        c.line((40, y - 18), (860, y - 18), (52, 56, 62), 1)

    c.text(360, 152, "send", 11, WRN, "mm")
    c.text(800, 152, "store", 11, GRN, "mm")

    c.line((40, 340), (860, 340), (52, 56, 62), 1)
    c.text(40, 358, "held back on purpose: with no change set every row looks changed, so a slot would fire "
                    "on every send", 11, DIM, "lm")
    return c.out()


if __name__ == "__main__":
    print("animated:")
    for name, fn, n in [("01-dispatch-layers", f01, 26), ("03-request-response", f03, 28),
                        ("04-store-message", f04, 28), ("05-script-rewrite", f05, 24),
                        ("27-collection-wake", f27, 24),
                        ("06-refevent", f06, 26), ("13-signature-inference", f13, 34)]:
        save_gif(name + ".gif", [fn(i/(n-1)) for i in range(n)], ms=80)
    print("static:")
    for name, fn in [("02-times-order", f02), ("07-key-lookup-vs-baked", f07), ("08-inline-fire", f08),
                     ("09-c-abi-hub", f09), ("10-message-node", f10), ("11-neuron-action", f11),
                     ("12-jump-trace", f12), ("14-intellisense", f14),
                     ("15-coupling", f15), ("16-key-contract", f16), ("17-capability-map", f17),
                     ("18-key-baking", f18), ("19-handy-bits", f19), ("20-tail-call", f20),
                     ("21-what-remains", f21), ("22-two-sides", f22),
                     ("23-class2name", f23), ("24-archive", f24), ("25-rpc", f25),
                     ("26-collection-shapes", f26), ("28-collection-virtuallist", f28),
                     ("29-collection-send-vs-store", f29)]:
        save_png(name + ".png", fn())
    print("done ->", OUT)
