// Offline check of the engine layer and PSOLA player: renders lines, prints timing, writes WAVs,
// and measures that pitch/speed controls do what they claim.
#include <cstdio>
#include <rack.hpp>
#include "../src/Tts.hpp"
#include "../src/Psola.hpp"

static void writeWav(const std::string& path, const std::vector<float>& x, int rate) {
	FILE* f = std::fopen(path.c_str(), "wb");
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f), u32(36 + x.size() * 2), std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16), u16(1), u16(1), u32(rate), u32(rate * 2), u16(2), u16(16), std::fwrite("data", 1, 4, f), u32(x.size() * 2);
	for (float v : x) {
		int16_t s = int16_t(std::clamp(v, -1.f, 1.f) * 32767);
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
}

static float medianF0(const std::vector<float>& x, float rate) {
	Clip c;
	c.rate = rate;
	c.samples = x;
	analyzePitch(c);
	std::vector<float> f;
	for (const Mark& m : c.marks)
		if (m.voiced)
			f.push_back(rate / m.period);
	if (f.empty())
		return 0.f;
	std::nth_element(f.begin(), f.begin() + f.size() / 2, f.end());
	return f[f.size() / 2];
}

static std::vector<float> play(ClipPtr clip, float fs, float pitch, float formant, float speed) {
	Player p;
	p.start(clip);
	std::vector<float> out;
	while (p.sounding() && out.size() < fs * 30)
		out.push_back(p.process(fs, pitch, formant, speed));
	return out;
}

int main(int argc, char** argv) {
	Catalog::get().load(argc > 1 ? argv[1] : "res");
	const Catalog& cat = Catalog::get();
	std::printf("variants %zu (first: %s %s ...), languages %zu, flite voices %zu\n", cat.variants.size(),
		cat.variants[0].c_str(), cat.variants[1].c_str(), cat.languages.size(), cat.fliteVoices.size());
	const std::string text = "Hello there, 42 robots are singing.";
	const float fs = 48000.f;
	for (int engine : {ESPEAK, FLITE}) {
		SynthParams p;
		p.engine = engine;
		p.voice = engine == FLITE ? 4 : 0;
		p.lang = cat.languageIndex("en-us");
		double t0 = rack::system::getTime();
		ClipPtr clip = Service::get().renderNow(text, p);
		double t1 = rack::system::getTime();
		if (!clip) {
			std::printf("engine %d: no clip\n", engine);
			return 1;
		}
		int voiced = 0;
		for (const Mark& m : clip->marks)
			voiced += m.voiced;
		std::printf("\nengine %d: %.0f Hz, %.2f s, rendered in %.1f ms, marks %zu (%d voiced)\n", engine, clip->rate,
			clip->samples.size() / clip->rate, (t1 - t0) * 1e3, clip->marks.size(), voiced);
		std::u32string u = toUtf32(text);
		for (const Span& w : clip->words)
			std::printf("  word %d %.3f-%.3f '%s'\n", w.index, w.start / clip->rate, w.end / clip->rate,
				w.charStart >= 0 ? toUtf8(u.substr(w.charStart, w.charLen)).c_str() : "?");
		std::printf("  phones:");
		for (const Span& s : clip->phones)
			std::printf(" %d%s", s.id, s.vowel ? "*" : "");
		std::printf("\n");

		std::string tag = engine == FLITE ? "flite" : "espeak";
		auto dry = play(clip, fs, 1.f, 1.f, 1.f);
		float f0 = medianF0(dry, fs);
		writeWav("out_" + tag + ".wav", dry, int(fs));
		struct { const char* name; float pitch, formant, speed; } cases[] = {
			{"up5th", 1.5f, 1.f, 1.f}, {"down8va", 0.5f, 1.f, 1.f}, {"formant_up", 1.f, 1.4f, 1.f}, {"slow", 1.f, 1.f, 0.5f}, {"fast", 1.f, 1.f, 2.f}};
		std::printf("  dry: %.2f s, median f0 %.1f Hz\n", dry.size() / fs, f0);
		for (auto& c : cases) {
			auto y = play(clip, fs, c.pitch, c.formant, c.speed);
			float peak = 0.f;
			for (float v : y)
				peak = std::max(peak, std::fabs(v));
			std::printf("  %-10s %.2f s (x%.2f), f0 %.1f Hz (x%.2f), peak %.2f\n", c.name, y.size() / fs,
				y.size() / double(dry.size()), medianF0(y, fs), medianF0(y, fs) / f0, peak);
			writeWav("out_" + tag + "_" + c.name + ".wav", y, int(fs));
		}
	}
	SynthParams p;
	p.lang = cat.languageIndex("de");
	ClipPtr de = Service::get().renderNow("Guten Morgen, schöne Grüße.", p);
	std::printf("\ngerman: %s, %zu words\n", de ? "ok" : "FAILED", de ? de->words.size() : 0);
	return 0;
}
