#pragma once
// Procedural 32x32 pixel-art talking faces. Rack-independent so tools can render contact sheets.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace face {

constexpr int N = 32;

enum Slot : uint8_t {
	BG, OUTLINE, SKIN, SKIN_SHADE, WHITE, IRIS, LIP, MOUTH, TONGUE, HAIR, HAIR_SHADE, FRAME, LENS,
	SHIRT, SHIRT_SHADE, METAL, METAL_SHADE, LED, LED_DIM, BLUSH, GLINT, NUM_SLOTS, CLEAR = 0xFF
};

enum Viseme : uint8_t { REST, MBP, AI, E, O, U, FV, L, CONS, NUM_VISEMES };
enum Kind : uint8_t { MALE, FEMALE, ROBOT };

struct Spec {
	Kind kind = MALE;
	uint8_t skin = 0, hairColor = 0, hair = 0, beard = 0, glasses = 0, frame = 0, shirt = 0, eyes = 0, bg = 0;
	bool operator==(const Spec& o) const { return std::memcmp(this, &o, sizeof *this) == 0; }
};

struct Anim {
	Viseme mouth = REST;
	bool blink = false, asleep = false, brow = false;
	int glance = 0;  // -1 left, 0 centre, 1 right
	float level = 0.f; // speech envelope 0..1
	double time = 0.0; // seconds, for idle robot lights
};

using Pixels = std::array<std::array<uint8_t, N>, N>;
using Palette = std::array<uint32_t, NUM_SLOTS>; // 0xRRGGBB

// ---------------------------------------------------------------------------------------------
// Art. Sprites are ASCII: each character names a palette slot, space is transparent.

inline uint8_t slotOf(char c) {
	switch (c) {
		case 'k': return OUTLINE; case 's': return SKIN; case 'd': return SKIN_SHADE; case 'w': return WHITE;
		case 'i': return IRIS; case 'l': return LIP; case 'r': return MOUTH; case 't': return TONGUE;
		case 'h': return HAIR; case 'H': return HAIR_SHADE; case 'g': return FRAME; case 'G': return LENS;
		case 'c': return SHIRT; case 'C': return SHIRT_SHADE; case 'm': return METAL; case 'M': return METAL_SHADE;
		case 'e': return LED; case 'E': return LED_DIM; case 'b': return BLUSH; case 'x': return GLINT;
		default: return CLEAR;
	}
}

struct Sprite {
	int x, y;
	const char* art; // rows separated by '\n'; a leading newline is ignored
};

inline void blit(Pixels& px, const Sprite& s, bool mirror = false) {
	int row = 0, col = 0;
	for (const char* p = s.art + (s.art[0] == '\n'); *p; p++) {
		if (*p == '\n') {
			row++, col = 0;
			continue;
		}
		uint8_t v = slotOf(*p);
		int x = mirror ? N - 1 - (s.x + col) : s.x + col, y = s.y + row;
		if (v != CLEAR && x >= 0 && x < N && y >= 0 && y < N)
			px[y][x] = v;
		col++;
	}
}

// Mouth shapes, 8 wide at (12, 19).
inline const char* const MOUTHS[NUM_VISEMES] = {
	"\n\n  llll",                                        // REST
	"\n  llll\n  llll",                                  // MBP
	"  llll\n lwwwwl\n lrrrrl\n lrttrl\n  llll",         // AI
	" llllll\n wwwwww\n lrrrrl\n  llll",                 // E
	"   ll\n  lrrl\n  lrrl\n   ll",                      // O
	"\n   ll\n  lrrl\n   ll",                            // U
	"  llll\n wwwwww\n  llll",                           // FV
	"  llll\n lwwwwl\n lrttrl\n  llll",                  // L
	"  llll\n lwwwwl\n  llll",                           // CONS
};

struct HairStyle {
	const char* name;
	Sprite back, front;
};

inline const HairStyle MALE_HAIR[] = {
	{"short", {0, 0, ""}, {8, 3, R"(
   hhhhhhhhhh
 hhhhhhhhhhhhhh
hhhhhhhhhhhhhhhh
hhhhHhhhhhhhHhhh
hhhHhhhhHhhhhhhh
hh  hH  hH  H hh
hh            hh
 h            h
 h            h)"}},
	{"side part", {0, 0, ""}, {8, 2, R"(
    hhhhhhhhh
  hhhhhhhhhhhhh
 hhhhhhhHhhhhhhh
hhhhhhhHhhhhhhhhh
hhhhhhHhhhhhhhhhh
hh  hHhhhhhhhhhhh
hh         hhhhhh
h            hhh
 h            h
 h            h)"}},
	{"buzz", {0, 0, ""}, {9, 4, R"(
  HHHHHHHHHH
 HHHHHHHHHHHH
HHHHHHHHHHHHHH
H H H H H H HH
H            H)"}},
	{"bald", {0, 0, ""}, {9, 10, R"(
H            H
H            H)"}},
	{"spiky", {0, 0, ""}, {8, 0, R"(
   h  h  h  h
  hh hh hhh hh
 hhhhhhhhhhhhhh
 hhhhhhhhhhhhhhh
hhhHhhhHhhhHhhhh
hhhhhhhhhhhhhhhh
hh hH hH hH hH hh
hh            hh
 h            h)"}},
	{"long", {6, 8, R"(
hh                hh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhhh            hhhh
hhhh            hhhh
 hhh            hhh
 hhh            hhh
  hh            hh)"}, {7, 2, R"(
    hhhhhhhhhh
  hhhhhhhhhhhhhh
 hhhhhhhhhhhhhhhh
hhhhHhhhhhhhhHhhhh
hhhHhhhhHhhhhhHhhh
hhhhhH  hH   Hhhhh
hhh            hhh
hh              hh)"}},
	{"afro", {0, 0, ""}, {5, 0, R"(
     hhhhhhhhhhhh
   hhhhhhhHhhhhhhhh
  hhhHhhhhhhhhhhHhhh
 hhhhhhhhhhhHhhhhhhhh
 hhhhhHhhhhhhhhhhhhhh
hhhhhhhhhhhhhhhHhhhhhh
hhhhHhhhhhhhhhhhhhhhhh
hhhhhhHhhhhhhhhhhhHhhh
hhhhhh hH hH hH hhhhhh
hhhhh            hhhhh
hhhh              hhhh
 hhh              hhh
 hhh              hhh
  hh              hh)"}},
};

inline const HairStyle FEMALE_HAIR[] = {
	{"long", {6, 7, R"(
hhh              hhh
hhh              hhh
hhhh            hhhh
hhhh            hhhh
hhhh            hhhh
hhhh            hhhh
hhhh            hhhh
hhhh            hhhh
hhhh            hhhh
hhhHh          hHhhh
hhhhh          hhhhh
hhhhhh        hhhhhh
hhhhhhh      hhhhhhh
 hhhhhh      hhhhhh
  hhhhh      hhhhh
   hhhh      hhhh)"}, {6, 2, R"(
     hhhhhhhhhh
   hhhhhhhhhhhhhh
  hhhhhhhhhhhhhhhh
 hhhhhHhhhhhhhhhhhh
 hhhhHhhhhhhhhhHhhh
hhhhHhhhhhhhhhhhHhhh
hhhhhhHhh  hhHhhhhhh
hhhh  hH    Hh  hhhh
hhh              hhh)"}},
	{"bob", {0, 0, ""}, {6, 2, R"(
     hhhhhhhhhh
   hhhhhhhhhhhhhh
  hhhhhhhhhhhhhhhh
 hhhhHhhhhhhhhhHhhh
 hhhHhhhhhhhhhhhHhh
hhhhhhhhhhhhhhhhhhhh
hhhhhhhhhhhhhhhhhhhh
hhhh            hhhh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhh              hhh
hhhh            hhhh
hhhh            hhhh
hhhhh          hhhhh
 hhhh          hhhh
  hhh          hhh)"}},
	{"bun", {0, 0, ""}, {8, 0, R"(
      hhhh
     hhHhhh
     hhhhhh
   hhhhhhhhhh
 hhhhhhhhhhhhhh
hhhhHhhhhhhhhhhh
hhhHhhhhhhhhHhhh
hhhhhhh hhhhhhhh
hhh  Hh  Hh   hh
hh            hh
hh            hh
h              h)"}},
	{"curly", {5, 8, R"(
hhh                hhh
hhhh              hhhh
hhHh              hHhh
hhhh              hhhh
 hhhh            hhhh
 hhHh            hHhh
 hhhh            hhhh
  hhhh          hhhh
  hHhh          hhHh
   hh            hh)"}, {5, 0, R"(
      hhhhhhhhhh
    hhhhHhhhhhhhhh
   hhHhhhhhhhhHhhhh
  hhhhhhhhHhhhhhhhhh
 hhhhHhhhhhhhhhhhHhhh
 hhhhhhhhhhhHhhhhhhhh
hhhhhhhHhhhhhhhhhhhhhh
hhHhhhhhhhhhhhhhhHhhhh
hhhhhhHhh hHh hhhhhhhh
hhhh              hhhh)"}},
	{"pixie", {0, 0, ""}, {8, 2, R"(
    hhhhhhhh
  hhhhhhhhhhhh
 hhhhhhhhhhhhhh
hhhhhhhhhhhhhhhh
hhhhhhhhhhhhhhHh
hhhhhhhhhhh hhhh
hhhhhhHh      hh
hh  hh         h
hh             h
 h)"}},
	{"side swept", {6, 8, R"(
hhh              hhh
hhh              hhh
hhhh             hhh
hhhh             hhh
hhhh             hhh
hhhh             hhh
hhhh             hhh
hhhhh           hhhh
hhhhh           hhhh
hhhhhh          hhhh
 hhhhh          hhh
  hhhh          hh)"}, {6, 2, R"(
     hhhhhhhhhh
   hhhhhhhhhhhhhh
  hhhhhhhhhhhhhhhh
 hhhhhhhhhhhhhhHhhh
 hhhhhhhhhhhhhHhhhh
hhhhhhhhhhhhhHhhhhhh
hhhhhhhhhhhHhh   hhh
hhhhhhhhhhh       hh
hhhh hhhh         hh
hhh               hh)"}},
};

inline const Sprite BEARDS[] = {
	{0, 0, ""},
	{12, 18, " hhhhhh\nhhH  Hhh"},                                                         // moustache
	{12, 18, " hhhhhh\n hH  Hh\n\n\n  hhhh\n  hHhh\n   hh"},                           // goatee
	{9, 15, "h            h\nh            h\nhh          hh\nhhh hhhhhh hhh\nhhhh      hhhh\n"
	        "hhhhh    hhhhh\nhhhhhhhhhhhhhh\n hhhhhHhhhhhh\n  hhhhhhhhhh\n   hhhhhhhh\n     hhhh"},  // full beard
	{10, 20, "H          H\n H H    H H\n  H HHHH H\n   H H H"},                            // stubble
	{9, 15, "h            h\nh            h\nh            h\nh            h\nhh          hh\n"
	        " hh        hh\n  hh      hh\n   hhhhhhhh"},                                   // chin strap
};
inline const char* const BEARD_NAMES[] = {"", "moustache", "goatee", "beard", "stubble", "chin strap"};

// Glasses over the eyes (rows 12..15). Index 0 is none.
inline const Sprite GLASSES[] = {
	{0, 0, ""},
	{9, 12, "  ggg    ggg\ngg   gggg   gg\n g   g  g   g\n  ggg    ggg"},
	{9, 12, " ggggg  ggggg\ngg   gggg   gg\n g   g  g   g\n ggggg  ggggg"},
	{9, 12, " ggggg  ggggg\nggxGGggggxGGgg\n gGGGg  gGGGg\n  ggg    ggg"},
};
inline const char* const GLASSES_NAMES[] = {"", "round", "square", "shades"};

// ---------------------------------------------------------------------------------------------
// Colours

inline constexpr uint32_t SKINS[][2] = {{0xf6d2b3, 0xd9a988}, {0xeab48c, 0xc98f6a}, {0xcf9a6b, 0xa9744b},
	{0xae7348, 0x8a5634}, {0x84502f, 0x663a20}, {0x5e3822, 0x472916}};
inline constexpr uint32_t HAIRS[][2] = {{0x2b2320, 0x1a1412}, {0x4c3121, 0x352115}, {0x7a4c2a, 0x5a361b},
	{0x9c3e20, 0x742c15}, {0xdcb461, 0xb38e43}, {0xcf6428, 0xa54b1a}, {0xbdbdbd, 0x8f8f8f}, {0x2aa198, 0x1c7670},
	{0x8a52d6, 0x6538a8}, {0xe0609e, 0xb2447a}};
inline constexpr uint32_t SHIRTS[][2] = {{0x3a78d8, 0x2a58a4}, {0xd64a4a, 0xa33434}, {0x42a86a, 0x2f7d4e},
	{0xe0a030, 0xb07820}, {0x8a5ad0, 0x6a42a4}, {0x2b2b30, 0x1c1c20}, {0xe8e8e8, 0xbdbdbd}, {0x1aa8b8, 0x137e8a}};
inline constexpr uint32_t IRISES[] = {0x5a3a22, 0x3b78c8, 0x3f8f45, 0x8a6a2a, 0x6f7f90};
inline constexpr uint32_t BGS[] = {0x121c2e, 0x1f1224, 0x0f2222, 0x1a1a1a, 0x24140f, 0x14240f};
inline constexpr uint32_t FRAMES[] = {0x141414, 0x6a3a1c, 0xc8a040, 0xb02a2a};
// Robots reuse the fields: skin = plating, hairColor = LED colour, hair = antenna, beard = eye style.
inline constexpr uint32_t PLATING[][2] = {{0xb8c0c8, 0x7e8790}, {0xc9a55a, 0x96763a}, {0x7890a8, 0x52667c}, {0xc07060, 0x8c4c40}};
inline constexpr uint32_t LEDS[][2] = {{0x40e8ff, 0x0e3a44}, {0x60ff70, 0x123c16}, {0xff4a4a, 0x401010}, {0xffb030, 0x40280a}};

template <typename T, size_t K>
constexpr int count(const T (&)[K]) {
	return int(K);
}

inline Palette palette(const Spec& s) {
	Palette p{};
	auto mix = [](uint32_t a, uint32_t b, float t) {
		uint32_t out = 0;
		for (int sh = 0; sh <= 16; sh += 8)
			out |= uint32_t(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t) << sh;
		return out;
	};
	p[BG] = BGS[s.bg % count(BGS)];
	p[OUTLINE] = mix(p[BG], 0x000000, 0.7f);
	p[WHITE] = 0xf4f4f0, p[MOUTH] = 0x3a1016, p[TONGUE] = 0xd05a6a, p[GLINT] = 0xffffff;
	p[IRIS] = IRISES[s.eyes % count(IRISES)];
	p[SHIRT] = SHIRTS[s.shirt % count(SHIRTS)][0], p[SHIRT_SHADE] = SHIRTS[s.shirt % count(SHIRTS)][1];
	p[FRAME] = FRAMES[s.frame % count(FRAMES)], p[LENS] = 0x202028;
	if (s.kind == ROBOT) {
		p[METAL] = PLATING[s.skin % count(PLATING)][0], p[METAL_SHADE] = PLATING[s.skin % count(PLATING)][1];
		p[LED] = LEDS[s.hairColor % count(LEDS)][0], p[LED_DIM] = LEDS[s.hairColor % count(LEDS)][1];
		return p;
	}
	p[SKIN] = SKINS[s.skin % count(SKINS)][0], p[SKIN_SHADE] = SKINS[s.skin % count(SKINS)][1];
	p[HAIR] = HAIRS[s.hairColor % count(HAIRS)][0], p[HAIR_SHADE] = HAIRS[s.hairColor % count(HAIRS)][1];
	p[LIP] = s.kind == FEMALE ? mix(p[SKIN_SHADE], 0xc8284a, 0.55f) : mix(p[SKIN_SHADE], 0x8a3030, 0.25f);
	p[BLUSH] = mix(p[SKIN], 0xff6070, 0.3f);
	return p;
}

// ---------------------------------------------------------------------------------------------
// Composition

inline Viseme visemeFor(int arpabet, bool vowel) {
	// Order matches the ARPAbet table in Tts.cpp: pau aa ae ah ao aw ax ay b ch d dh eh er ey f g hh ih iy jh
	// k l m n ng ow oy p r s sh t th uh uw v w y z zh
	static const Viseme table[] = {REST, AI, AI, AI, O, O, AI, AI, MBP, U, CONS, L, E, U, E, FV, CONS, CONS, E, E,
		U, CONS, L, MBP, CONS, CONS, O, O, MBP, U, CONS, U, CONS, L, U, U, FV, U, E, CONS, U};
	if (arpabet >= 0 && arpabet < int(sizeof table / sizeof *table))
		return table[arpabet];
	return vowel ? AI : CONS;
}

/** Fills rows [y0, y1] between per-row spans (inclusive) with a slot; shade the rightmost column. */
inline void fillSpans(Pixels& px, int y0, const std::vector<std::pair<int, int>>& spans, uint8_t fill, uint8_t shade) {
	for (size_t i = 0; i < spans.size(); i++)
		for (int x = spans[i].first; x <= spans[i].second; x++)
			px[y0 + i][x] = x >= spans[i].second - 1 && shade != CLEAR ? shade : fill;
}

inline void outline(Pixels& px) {
	Pixels src = px;
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			if (src[y][x] != CLEAR)
				continue;
			bool edge = (x > 0 && src[y][x - 1] != CLEAR) || (x < N - 1 && src[y][x + 1] != CLEAR) ||
			            (y > 0 && src[y - 1][x] != CLEAR) || (y < N - 1 && src[y + 1][x] != CLEAR);
			if (edge)
				px[y][x] = OUTLINE;
		}
	for (auto& row : px)
		for (auto& v : row)
			if (v == CLEAR)
				v = BG;
}

inline void body(Pixels& px, uint8_t fill, uint8_t shade) {
	fillSpans(px, 27, {{9, 22}, {6, 25}, {4, 27}, {3, 28}, {3, 28}}, fill, shade);
}

inline void robot(Pixels& px, const Spec& s, const Anim& a) {
	body(px, SHIRT, SHIRT_SHADE);
	fillSpans(px, 24, {{14, 17}, {14, 17}, {14, 17}}, METAL_SHADE, CLEAR);
	std::vector<std::pair<int, int>> head(19, {8, 23});
	fillSpans(px, 5, head, METAL, METAL_SHADE);
	for (int x = 9; x <= 22; x++)
		px[23][x] = METAL_SHADE;
	for (int y : {12, 13, 14, 15})
		px[y][7] = px[y][24] = METAL_SHADE;
	for (auto p : {std::make_pair(9, 6), std::make_pair(22, 6), std::make_pair(9, 22), std::make_pair(22, 22)})
		px[p.second][p.first] = METAL_SHADE;
	bool tip = std::fmod(a.time, 1.6) < 0.8 || a.level > 0.1f;
	switch (s.hair % 3) {
		case 0: // single antenna
			for (int y = 2; y <= 4; y++)
				px[y][15] = METAL_SHADE;
			px[1][15] = px[1][16] = px[2][16] = tip ? LED : LED_DIM;
			break;
		case 1: // twin antennae
			for (int y = 2; y <= 4; y++)
				px[y][10] = px[y][21] = METAL_SHADE;
			px[1][10] = tip ? LED : LED_DIM, px[1][21] = tip ? LED_DIM : LED;
			break;
		default: // dome
			fillSpans(px, 3, {{12, 19}, {10, 21}}, METAL_SHADE, CLEAR);
	}
	if (s.beard % 2 == 0) { // two eyes
		int dx = a.glance;
		for (int ex : {11, 18}) {
			for (int y = 11; y <= 13; y++)
				for (int x = ex; x < ex + 3; x++)
					px[y][x] = LED_DIM;
			if (!a.blink && !a.asleep)
				px[12][ex + 1 + dx] = px[11][ex + 1 + dx] = LED;
			else
				px[13][ex] = px[13][ex + 1] = px[13][ex + 2] = LED;
		}
	}
	else { // visor with a scanning light
		for (int y = 11; y <= 13; y++)
			for (int x = 10; x <= 21; x++)
				px[y][x] = LED_DIM;
		if (!a.asleep) {
			int sx = 10 + int(std::fabs(std::fmod(a.time * 1.2, 2.0) - 1.0) * 10.0);
			px[12][sx] = px[12][sx + 1] = LED;
			if (!a.blink)
				px[11][sx] = px[11][sx + 1] = px[13][sx] = px[13][sx + 1] = LED;
		}
	}
	// Grille mouth: lit cells sized by viseme.
	static const int8_t size[NUM_VISEMES][2] = {{0, 0}, {8, 1}, {10, 4}, {10, 2}, {4, 4}, {2, 2}, {8, 1}, {6, 3}, {8, 2}};
	for (int y = 18; y <= 21; y++)
		for (int x = 11; x <= 20; x++)
			px[y][x] = (x + y) % 2 ? METAL_SHADE : LED_DIM;
	int w = size[a.mouth][0], h = size[a.mouth][1];
	for (int y = 20 - (h + 1) / 2; y < 20 - (h + 1) / 2 + h; y++)
		for (int x = 16 - w / 2; x < 16 - w / 2 + w; x++)
			px[y][x] = LED;
}

inline void human(Pixels& px, const Spec& s, const Anim& a) {
	bool female = s.kind == FEMALE;
	const HairStyle& hair = female ? FEMALE_HAIR[s.hair % count(FEMALE_HAIR)] : MALE_HAIR[s.hair % count(MALE_HAIR)];
	if (hair.back.art[0])
		blit(px, hair.back);
	body(px, SHIRT, SHIRT_SHADE);
	fillSpans(px, 24, {{13, 18}, {13, 18}, {13, 18}, {13, 18}}, SKIN, CLEAR);
	for (int x = 13; x <= 18; x++)
		px[24][x] = px[25][x] = SKIN_SHADE;
	fillSpans(px, 28, {{14, 17}, {15, 16}}, SKIN_SHADE, CLEAR);
	std::vector<std::pair<int, int>> head = {{11, 20}, {10, 21}};
	for (int y = 7; y <= 19; y++)
		head.push_back({9, 22});
	if (female)
		head.insert(head.end(), {{10, 21}, {11, 20}, {12, 19}, {13, 18}, {14, 17}});
	else
		head.insert(head.end(), {{10, 21}, {10, 21}, {11, 20}, {12, 19}, {13, 18}});
	fillSpans(px, 5, head, SKIN, SKIN_SHADE);
	for (int y = 12; y <= 15; y++)
		px[y][8] = SKIN, px[y][23] = SKIN_SHADE;
	px[13][8] = px[14][8] = SKIN_SHADE;

	// Brows, eyes, nose, cheeks.
	int by = a.brow ? 9 : 10;
	for (int k = 0; k < 3; k++) {
		if (female && k == 0)
			continue;
		px[by][11 + k] = px[by][20 - k] = HAIR_SHADE;
	}
	for (int side = 0; side < 2; side++) {
		int x0 = side ? 18 : 11, outer = side ? x0 + 3 : x0 - 1;
		if (a.blink || a.asleep) {
			for (int k = 0; k < 3; k++)
				px[14][x0 + k] = OUTLINE;
			continue;
		}
		for (int k = 0; k < 3; k++)
			px[12][x0 + k] = OUTLINE, px[13][x0 + k] = px[14][x0 + k] = WHITE;
		px[13][x0 + 1 + a.glance] = px[14][x0 + 1 + a.glance] = IRIS;
		if (female)
			px[12][outer] = OUTLINE;
	}
	px[16][16] = px[17][15] = px[17][16] = SKIN_SHADE;
	if (female)
		px[18][10] = px[18][11] = px[18][20] = px[18][21] = BLUSH;

	if (!female && s.beard)
		blit(px, BEARDS[s.beard % count(BEARDS)]);
	blit(px, {12, 19, MOUTHS[a.asleep ? REST : a.mouth]});
	blit(px, hair.front);
	if (s.glasses)
		blit(px, GLASSES[s.glasses % count(GLASSES)]);
}

inline void render(const Spec& s, const Anim& a, Pixels& px) {
	for (auto& row : px)
		row.fill(CLEAR);
	if (s.kind == ROBOT)
		robot(px, s, a);
	else
		human(px, s, a);
	outline(px);
}

// ---------------------------------------------------------------------------------------------
// Choosing a face for a voice: deterministic, so a voice always wears the same face.

inline uint32_t fnv(const std::string& s) {
	uint32_t h = 2166136261u;
	for (unsigned char c : s)
		h = (h ^ c) * 16777619u;
	return h;
}

/** gender: 0 unknown, 1 male, 2 female. */
inline Spec specFor(const std::string& voice, int gender, bool robot) {
	uint32_t h = fnv(voice);
	auto pick = [&](int n) {
		uint32_t v = h % uint32_t(n);
		h = h / uint32_t(n) * 2654435761u + 0x9E3779B9u;
		return uint8_t(v);
	};
	Spec s;
	s.kind = robot ? ROBOT : gender == 2 ? FEMALE : gender == 1 ? MALE : Kind(pick(2));
	s.bg = pick(count(BGS));
	s.shirt = pick(count(SHIRTS));
	s.eyes = pick(count(IRISES));
	s.frame = pick(count(FRAMES));
	if (robot) {
		s.skin = pick(count(PLATING));
		s.hairColor = pick(count(LEDS));
		s.hair = pick(3);
		s.beard = pick(2);
		return s;
	}
	s.skin = pick(count(SKINS));
	uint8_t hc = pick(20); // mostly natural colours, occasionally dyed
	s.hairColor = hc < 16 ? hc % 6 : 7 + (hc - 16) % 3;
	s.hair = pick(s.kind == FEMALE ? count(FEMALE_HAIR) : count(MALE_HAIR));
	uint8_t g = pick(9);
	s.glasses = g < 5 ? 0 : g < 7 ? 1 + (g - 5) : g == 7 ? 1 : 3;
	s.beard = s.kind == MALE ? std::max(0, int(pick(8)) - 2) % count(BEARDS) : 0;
	if (voice.find("grand") != std::string::npos) // grandpa / grandma
		s.hairColor = 6, s.glasses = 1;
	return s;
}

inline Spec specForSeed(int seed) {
	return specFor("#" + std::to_string(seed), 1 + seed % 2, seed % 7 == 0);
}

} // namespace face
