// Renders a contact sheet of face parts and visemes to a PPM: face_sheet out.ppm [scale]
#include <cstdio>
#include <cstdlib>
#include "../src/Face.hpp"

using namespace face;

int main(int argc, char** argv) {
	const int scale = argc > 2 ? std::atoi(argv[2]) : 4, cell = N + 2;
	std::vector<std::vector<std::pair<Spec, Anim>>> rows;
	auto spec = [](Kind k, int hair, int beard, int glasses, int skin, int hc) {
		Spec s;
		s.kind = k, s.hair = hair, s.beard = beard, s.glasses = glasses, s.skin = skin, s.hairColor = hc;
		s.shirt = (hair + beard) % 8, s.bg = (hair + skin) % 6, s.eyes = hair % 5;
		return s;
	};
	Anim idle;
	std::vector<std::pair<Spec, Anim>> row;
	for (int h = 0; h < count(MALE_HAIR); h++)
		row.push_back({spec(MALE, h, 0, 0, h % 6, h % 7), idle});
	rows.push_back(row), row.clear();
	for (int b = 0; b < count(BEARDS); b++)
		row.push_back({spec(MALE, b % 7, b, b % 4, (b + 2) % 6, b), idle});
	rows.push_back(row), row.clear();
	for (int h = 0; h < count(FEMALE_HAIR); h++)
		row.push_back({spec(FEMALE, h, 0, h % 4, (h + 1) % 6, (h * 3) % 10), idle});
	rows.push_back(row), row.clear();
	for (int v = 0; v < NUM_VISEMES; v++) {
		Anim a;
		a.mouth = Viseme(v);
		row.push_back({spec(FEMALE, 0, 0, 0, 1, 2), a});
	}
	rows.push_back(row), row.clear();
	for (int v = 0; v < NUM_VISEMES; v++) {
		Anim a;
		a.mouth = Viseme(v);
		row.push_back({spec(MALE, 1, 3, 2, 3, 0), a});
	}
	rows.push_back(row), row.clear();
	for (int r = 0; r < 6; r++) {
		Spec s = spec(ROBOT, r % 3, r % 2, 0, r % 4, r % 4);
		Anim a;
		a.mouth = Viseme((r * 2) % NUM_VISEMES), a.time = 0.3 * r;
		row.push_back({s, a});
	}
	Anim blink;
	blink.blink = true, blink.glance = 0;
	row.push_back({spec(MALE, 0, 1, 0, 2, 3), blink});
	Anim look;
	look.glance = -1, look.brow = true;
	row.push_back({spec(FEMALE, 2, 0, 0, 4, 4), look});
	rows.push_back(row), row.clear();
	for (const char* name : {"m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "f1", "f2", "f3", "f4", "f5"})
		row.push_back({specFor(std::string("0:") + name, name[0] == 'f' ? 2 : 1, false), idle});
	rows.push_back(row), row.clear();

	size_t cols = 0;
	for (auto& r : rows)
		cols = std::max(cols, r.size());
	int W = int(cols) * cell * scale, H = int(rows.size()) * cell * scale;
	std::vector<uint32_t> img(size_t(W) * H, 0x404040);
	for (size_t ry = 0; ry < rows.size(); ry++)
		for (size_t rx = 0; rx < rows[ry].size(); rx++) {
			Pixels px;
			render(rows[ry][rx].first, rows[ry][rx].second, px);
			Palette pal = palette(rows[ry][rx].first);
			for (int y = 0; y < N; y++)
				for (int x = 0; x < N; x++)
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++)
							img[size_t((ry * cell + 1 + y) * scale + sy) * W + (rx * cell + 1 + x) * scale + sx] = pal[px[y][x]];
		}
	FILE* f = std::fopen(argc > 1 ? argv[1] : "faces.ppm", "wb");
	std::fprintf(f, "P6 %d %d 255\n", W, H);
	for (uint32_t c : img) {
		unsigned char rgb[3] = {uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
		std::fwrite(rgb, 1, 3, f);
	}
	std::fclose(f);
}
