#!/usr/bin/env python3
"""Single source of truth for the panels: writes res/<Model>.svg, res/<Model>-dark.svg and src/layout.hpp.

Labels are converted to outlines (Rack's nanosvg can't render <text>). Pass the path of a TTF
(default: Nunito-Bold.ttf shipped with VCV Rack) as argv[1] or via $PANEL_FONT.
"""
import math, os, sys, pathlib
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = pathlib.Path(__file__).resolve().parent.parent
HP, H = 5.08, 128.5
KNOB_LABELS = ["ENGINE", "VOICE", "PITCH", "RANGE", "RATE", "SHIFT", "FORMANT", "SPEED"]

THEMES = {
    "": dict(bg="#ecebe8", ink="#2b2b2b", rule="#b3b1ab", plate="#2d2e31", plate_ink="#f0f0f0"),
    "-dark": dict(bg="#232427", ink="#d6d6d6", rule="#505156", plate="#121315", plate_ink="#d6d6d6"),
}


NONE = (-1.0, -1.0)


BLACK, TEAL, CYAN, AMBER, PURPLE, PINK = range(6)  # knob caps; BLACK is Rack's own knob
KNOB_CAPS = {TEAL: "#8ad5c7", CYAN: "#93dff2", AMBER: "#f7c97f", PURPLE: "#bea8ec", PINK: "#f5aacb"}
KNOB_NAMES = {TEAL: "teal", CYAN: "cyan", AMBER: "amber", PURPLE: "purple", PINK: "pink"}


def above(text, jack, size=1.9):
    return (text, jack[0], jack[1] - 6.0, size, "middle")


def seq_input(name, jack, name_size, light_size, gap=6.2, spread=3.5):
    """A LINE/WORD jack's trigger button (left of it, joined by a short rule) and its name, vertically centred on
    the jack, between its TRIG. INCR. (above) and INDEX (below) lights."""
    x, y = jack
    tx = x + gap
    lights = [(tx + 0.9, y - spread), (tx + 0.9, y + spread)]
    labels = [(name, tx, y + name_size * 0.35, name_size, "start")]
    labels += [(t, lx + 1.6, ly + light_size * 0.36, light_size, "start") for t, (lx, ly) in zip(("TRIG. INCR.", "INDEX"), lights)]
    button = (x - 9.5, y)
    return dict(button=button, lights=lights, labels=labels, rule=(button[0] + 2.6, x - 4.3, y))


def full_speak():
    w = 26 * HP
    cols = [9.5 + 16.15 * i for i in range(8)]
    kb_px, io = 9.6, 115.0
    line, word, reset, out = (17.0, io), (56.0, io), (cols[5], io), (cols[7], io)
    li, wi = seq_input("LINE", line, 2.4, 1.5), seq_input("WORD", word, 2.4, 1.5)
    return dict(
        title="speak-full", W=w, display=(6.0, 11.0, 98.0, 28.0), rows=4, values=False, encRow=(117.5, 18.0),
        encCol=(117.5, 33.0), ops=(6.0, 45.5, 18.0, 6.2, 2.0), keyboard=True,
        kb=((w - 12 * kb_px) / 2 + kb_px / 2, 52.9, kb_px, 6.6, 8.8, 5.8),
        knob=[(x, 88.5) for x in cols], cv=[(x, 99.5) for x in cols], knobColor=[BLACK] * 8,
        line=line, word=word, reset=reset, out=out, lineIn=li, wordIn=wi, smallLights=False, engineLabelsLeft=False,
        labels=[above("RESET", reset)] + li["labels"] + wi["labels"],
        groups=[("LATCHED ON TRIGGER", cols[1] - 5, cols[4] + 5, 79.4), ("LIVE", cols[5] - 5, cols[7] + 5, 79.4)],
        plates=[(cols[7] - 6.4, 104.6, w - 2.4, 120.0)], knob_labels=KNOB_LABELS, label_rise=6.0)


def compact_speak():
    w = 16 * HP
    x = [8.0 + (w - 16.0) / 4 * i for i in range(5)]  # PITCH SPEED VOICE RANGE FORMANT
    k, r2, r3, right = 64.0, 95.0, 113.0, 68.5
    knob = [(55.0, r2), (x[2], k), (x[0], k), (x[3], k), NONE, NONE, (x[4], k), (x[1], k)]
    cv = [NONE] + [(px, k + 12.5) if px > 0 else NONE for px, _ in knob[1:]]
    line, word, reset, out = (14.5, r2), (14.5, r3), (right, r2), (right, r3)
    li, wi = seq_input("LINE", line, 3.0, 1.9), seq_input("WORD", word, 3.0, 1.9)
    return dict(
        title="speak-lite", W=w, display=(3.5, 11.0, w - 7.0, 38.5), rows=6, values=True, encRow=NONE, encCol=NONE,
        ops=(-1.0, 0, 0, 0, 0), keyboard=False, kb=(0, 0, 0, 0, 0, 0),
        knob=knob, cv=cv, knobColor=[BLACK, AMBER, TEAL, PURPLE, BLACK, BLACK, PINK, CYAN],
        line=line, word=word, reset=reset, out=out, lineIn=li, wordIn=wi, smallLights=True, engineLabelsLeft=True,
        labels=[("INPUTS", 3.2, r2 - 7.4, 2.2, "start"), above("RESET", reset, 2.2)] + li["labels"] + wi["labels"],
        groups=[], plates=[(right - 8.3, r3 - 10.1, w - 2.4, r3 + 5.0)],
        knob_labels=["ENGINE", "VOICE", "PITCH", "RANGE", "", "", "FORMANT", "SPEED"], label_rise=7.4)


def face():
    w = 8 * HP
    return dict(W=w, display=(3.3, 12.0, w - 6.6, 40.5), link=(w / 2, 62.0), grille=(w / 2, 94.0, 15.5))


OUT_LABELS = ["LINE", "L.IDX", "WORD", "W.IDX", "PHON", "P.IDX", "EOL", "ENV"]


def outs():
    w = 6 * HP
    return dict(W=w, jacks=[(x, 32.0 + 22.0 * r) for r in range(4) for x in (8.6, 21.9)], link=(w / 2, 114.0))


class Typesetter:
    def __init__(self, path):
        self.font = TTFont(path)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upem = self.font["head"].unitsPerEm
        self.hmtx = self.font["hmtx"]

    def path(self, s, x, y, size, anchor="middle"):
        k = size / self.upem
        names = [self.cmap[ord(c)] for c in s]
        width = sum(self.hmtx[n][0] for n in names) * k
        x -= {"start": 0, "middle": width / 2, "end": width}[anchor]
        pen = SVGPathPen(self.glyphs)
        for n in names:
            self.glyphs[n].draw(TransformPen(pen, (k, 0, 0, -k, x, y)))
            x += self.hmtx[n][0] * k
        return pen.getCommands()


class Canvas:
    def __init__(self, t, c, w):
        self.t, self.c, self.w, self.out = t, c, w, [f'<rect width="{w:.2f}" height="{H}" fill="{c["bg"]}"/>']

    def text(self, s, x, y, size=1.9, fill=None, anchor="middle"):
        self.out.append(f'<path fill="{fill or self.c["ink"]}" d="{self.t.path(s, x, y, size, anchor)}"/>')

    def line(self, x1, y1, x2, y2):
        self.out.append(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{self.c["rule"]}" '
                        f'stroke-width="0.25" stroke-linecap="round"/>')

    def rect(self, x0, y0, x1, y1, fill, rx=1.6):
        self.out.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" rx="{rx}" fill="{fill}"/>')

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w:.2f}mm" height="{H}mm" '
                f'viewBox="0 0 {self.w:.2f} {H}">\n' + "\n".join(self.out) + "\n</svg>\n")


def draw_speak(cv, L):
    c = cv.c
    cv.text(L["title"], L["W"] / 2, 7.4, 4.6 if L["keyboard"] else 4.0)
    cv.text("un1tz3r0", L["W"] / 2, 126.2, 2.0, fill=c["rule"])
    if L["encRow"][0] > 0:
        cv.text("LINE", L["encRow"][0], L["encRow"][1] - 6.9)
        cv.text("CURSOR", L["encCol"][0], L["encCol"][1] - 6.9)
        for cx, cy in (L["encRow"], L["encCol"]):  # detent dots: an endless encoder, not a knob
            for k in range(20):
                a = 2 * math.pi * k / 20
                cv.out.append(f'<circle cx="{cx + 5.6 * math.sin(a):.2f}" cy="{cy - 5.6 * math.cos(a):.2f}" r="0.22" fill="{c["rule"]}"/>')
    for label, x0, x1, y in L["groups"]:
        cv.line(x0, y, x1, y)
        cv.text(label, (x0 + x1) / 2, y - 0.8, 1.6, fill=c["rule"])
    for i, ((x, y), s) in enumerate(zip(L["knob"], L["knob_labels"])):
        if x > 0:
            cv.text(s, x, y - (6.0 if i == 0 else L["label_rise"]))
            if L["values"] and L["knobColor"][i]:  # guide up to the knob's readout on the screen, broken by its name
                top, ly = L["display"][1] + L["display"][3], y - L["label_rise"]
                cv.line(x, top + 0.6, x, ly - 2.2)
                cv.line(x, ly + 0.9, x, y - 5.8)
    ex, ey = L["knob"][0]
    side, anchor = (-3.2, "end") if L["engineLabelsLeft"] else (3.2, "start")
    cv.text("FLITE", ex + side, ey - 2.0, 1.5, anchor=anchor)
    cv.text("ESPEAK", ex + side, ey + 3.0, 1.5, anchor=anchor)
    for (x, y), (x2, y2) in zip(L["knob"], L["cv"]):
        if x > 0 and x2 > 0:
            cv.line(x, y + L["label_rise"] - 1.2, x, y2 - 4.3)
    for x0, y0, x1, y1 in L["plates"]:
        cv.rect(x0, y0, x1, y1, c["plate"])
    for s, x, y, size, anchor in L["labels"]:
        cv.text(s, x, y, size, anchor=anchor)
    for key in ("lineIn", "wordIn"):
        x0, x1, y = L[key]["rule"]
        cv.line(x0, y, x1, y)
    cv.text("OUT", L["out"][0], L["out"][1] - 6.0, fill=c["plate_ink"])


def draw_face(cv, L):
    c = cv.c
    cv.text("speak-pix", L["W"] / 2, 7.4, 4.0)
    cv.text("un1tz3r0", L["W"] / 2, 126.2, 2.0, fill=c["rule"])
    cv.text("LINK", L["link"][0], L["link"][1] - 3.0)
    gx, gy, radius = L["grille"]  # speaker grille: hex-packed perforations in a disc
    pitch, hole = 1.75, 0.5
    rows = int(radius / (pitch * 0.866)) + 1
    for j in range(-rows, rows + 1):
        y = gy + j * pitch * 0.866
        shift = (j % 2) * pitch / 2
        for i in range(-rows - 1, rows + 2):
            x = gx + i * pitch + shift
            if math.hypot(x - gx, y - gy) <= radius - hole:
                cv.out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{hole}" fill="{c["plate"]}"/>')
    cv.out.append(f'<circle cx="{gx:.2f}" cy="{gy:.2f}" r="{radius + 1.0:.2f}" fill="none" stroke="{c["rule"]}" stroke-width="0.3"/>')


def knob_svgs():
    """Pastel knob caps (rotating) and their shared dark body, drawn here so they carry no third-party art."""
    d, r = 10.6, 5.3
    head = f'<svg xmlns="http://www.w3.org/2000/svg" width="{d}mm" height="{d}mm" viewBox="0 0 {d} {d}">'
    yield "knob-bg.svg", (head + f'<circle cx="{r}" cy="{r}" r="{r}" fill="#1c1d20"/>'
                          f'<circle cx="{r}" cy="{r}" r="{r - 0.35}" fill="#2b2c30"/></svg>\n')
    for color, fill in KNOB_CAPS.items():
        yield f"knob-{KNOB_NAMES[color]}.svg", (
            head + f'<circle cx="{r}" cy="{r}" r="4.2" fill="{fill}"/>'
            f'<circle cx="{r - 0.6}" cy="{r - 0.8}" r="3.0" fill="#ffffff" fill-opacity="0.22"/>'
            f'<circle cx="{r}" cy="{r}" r="4.05" fill="none" stroke="#000000" stroke-opacity="0.18" stroke-width="0.3"/>'
            f'<line x1="{r}" y1="1.5" x2="{r}" y2="3.7" stroke="#2b2b2b" stroke-width="0.75" stroke-linecap="round"/></svg>\n')


def draw_outs(cv, L):
    c = cv.c
    cv.text("speak-out", L["W"] / 2, 12.8, 3.6)
    cv.text("un1tz3r0", L["W"] / 2, 126.2, 1.8, fill=c["rule"])
    ys = [y for _, y in L["jacks"]]
    cv.rect(2.0, min(ys) - 10.2, L["W"] - 2.0, max(ys) + 5.4, c["plate"])
    for (x, y), s in zip(L["jacks"], OUT_LABELS):
        cv.text(s, x, y - 6.0, 1.8, fill=c["plate_ink"])
    cv.text("LINK", L["link"][0], L["link"][1] - 3.0)


def cpp_layout():
    f = lambda v: f"{v:.3f}f"
    arr = lambda vs: "{" + ", ".join(map(f, vs)) + "}"
    pairs = lambda ps: "{" + ", ".join(arr(p) for p in ps) + "}"

    def speak(name, L):
        io = ", ".join(arr(L[k]) for k in ("line", "word", "reset", "out"))
        io += f", {arr(L['lineIn']['button'])}, {arr(L['wordIn']['button'])}"
        return (f"constexpr SpeakLayout {name} = {{{f(L['W'])}, {arr(L['display'])}, {L['rows']}, "
                f"{'true' if L['values'] else 'false'}, {arr(L['encRow'])}, "
                f"{arr(L['encCol'])}, {arr(L['ops'])}, {'true' if L['keyboard'] else 'false'}, "
                f"{arr(L['kb'])},\n\t{pairs(L['knob'])},\n\t{pairs(L['cv'])},\n\t"
                f"{{{', '.join(map(str, L['knobColor']))}}},\n\t{io},\n\t"
                f"{pairs(L['lineIn']['lights'])}, {pairs(L['wordIn']['lights'])}, {'true' if L['smallLights'] else 'false'}}};")
    F, O = face(), outs()
    return f"""#pragma once
#include <cstdint>
// Generated by tools/panel.py; edit the layout there. All values in millimetres; x < 0 means "not on this panel".
namespace layout {{
struct SpeakLayout {{
	float W, display[4];
	int rows;    // visible text lines
	bool values; // a row of coloured knob readouts along the bottom of the screen
	float encRow[2], encCol[2];
	float ops[5]; // x0, centre y, key width, key height, gap
	bool keyboard;
	float kb[6]; // first key centre x, y, pitch x, pitch y, key width, key height
	float knob[8][2], cv[8][2]; // by param / input id
	int knobColor[8];           // 0 black, 1 teal, 2 cyan, 3 amber, 4 purple, 5 pink
	float line[2], word[2], reset[2], out[2], lineButton[2], wordButton[2];
	float lineLights[2][2], wordLights[2][2]; // trigger, index
	bool smallLights;
}};
constexpr uint32_t KNOB_COLORS[6] = {{0x000000, {", ".join("0x" + KNOB_CAPS[i][1:] for i in range(1, 6))}}};
struct FaceLayout {{
	float W, display[4], link[2];
}};
struct OutsLayout {{
	float W, jacks[8][2], link[2];
}};
{speak("SPEAK", full_speak())}
{speak("SPEAK_COMPACT", compact_speak())}
constexpr FaceLayout FACE = {{{f(F['W'])}, {arr(F['display'])}, {arr(F['link'])}}};
constexpr OutsLayout OUTS = {{{f(O['W'])}, {pairs(O['jacks'])}, {arr(O['link'])}}};
}}
"""


if __name__ == "__main__":
    font = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(
        "PANEL_FONT", str(ROOT.parent / "deps/Rack2Free/res/fonts/Nunito-Bold.ttf"))
    t = Typesetter(font)
    for model, L, draw in (("Speak", full_speak(), draw_speak), ("SpeakCompact", compact_speak(), draw_speak), ("SpeakFace", face(), draw_face), ("SpeakOuts", outs(), draw_outs)):
        for suffix, colors in THEMES.items():
            cv = Canvas(t, colors, L["W"])
            draw(cv, L)
            (ROOT / "res" / f"{model}{suffix}.svg").write_text(cv.svg())
    for name, svg in knob_svgs():
        (ROOT / "res" / name).write_text(svg)
    (ROOT / "src" / "layout.hpp").write_text(cpp_layout())
