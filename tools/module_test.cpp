// Drives Speak / Speak Compact in real time against the live synthesis worker, with a Speak Outs expander chained
// on the right, and checks triggering, auto-increment, word mode, the unified IN jack and the merged pitch/speed knobs.
#include <chrono>
#include <cstdio>
#include <thread>
#include "../src/Speak.hpp"

enum { LINE, L_IDX, WORD, W_IDX, PHON, P_IDX, EOL, ENV };

struct Rig {
	Module* m;
	Module* outs;
	Module::ProcessArgs args;
	long frame = 0;
	int lineRises = 0, wordRises = 0, phoneRises = 0, eols = 0;
	long lineRiseFrame = -1;
	double sumSq = 0.0;
	bool last[8] = {};
	std::vector<float> lineIdx, wordIdx, audio;

	explicit Rig(Model* model) : m(model->createModule()), outs(modelSpeakOuts->createModule()) {
		m->rightExpander.module = outs;
		outs->leftExpander.module = m;
		args.sampleRate = 48000.f;
		args.sampleTime = 1.f / 48000.f;
	}
	Speak& speak() { return *static_cast<Speak*>(m); }
	void patch(int input, float v) {
		m->inputs[input].channels = 1;
		m->inputs[input].setVoltage(v);
	}
	void set(int param, float v) { m->params[param].setValue(v); }
	/** Runs `sec` seconds paced to wall-clock time so the worker thread sees realistic timing. */
	void run(float sec) {
		auto start = std::chrono::steady_clock::now();
		long n = long(sec * args.sampleRate);
		for (long i = 0; i < n; i++, frame++) {
			args.frame = frame;
			m->process(args);
			outs->process(args);
			if (outs->leftExpander.messageFlipRequested) {
				std::swap(outs->leftExpander.producerMessage, outs->leftExpander.consumerMessage);
				outs->leftExpander.messageFlipRequested = false;
			}
			auto hi = [&](int o) { return outs->outputs[o].getVoltage() > 5.f; };
			if (hi(LINE) && !last[LINE]) {
				lineRises++, lineRiseFrame = frame;
				lineIdx.push_back(outs->outputs[L_IDX].getVoltage());
			}
			if (hi(WORD) && !last[WORD]) {
				wordRises++;
				wordIdx.push_back(outs->outputs[W_IDX].getVoltage());
			}
			phoneRises += hi(PHON) && !last[PHON];
			eols += hi(EOL) && !last[EOL];
			for (int o : {LINE, WORD, PHON, EOL})
				last[o] = hi(o);
			float y = m->outputs[Speak::AUDIO_OUTPUT].getVoltage();
			if (!std::isfinite(y)) {
				std::printf("FAIL: non-finite output\n");
				std::exit(1);
			}
			sumSq += y * y;
			audio.push_back(y);
			if (i % 64 == 63)
				std::this_thread::sleep_until(start + std::chrono::microseconds(long((i + 1) / 48000.0 * 1e6)));
		}
	}
	void reset() {
		lineRises = wordRises = phoneRises = eols = 0;
		lineRiseFrame = -1;
		sumSq = 0.0;
		lineIdx.clear(), wordIdx.clear(), audio.clear();
	}
	/** One trigger pulse of `ms` milliseconds, then `after` seconds of running. */
	void pulse(int input, float after, float ms = 1.f) {
		patch(input, 10.f);
		run(ms / 1000.f);
		patch(input, 0.f);
		run(after);
	}
	float f0() {
		Clip c;
		c.rate = args.sampleRate;
		c.samples = audio;
		analyzePitch(c);
		std::vector<float> f;
		for (const Mark& mk : c.marks)
			if (mk.voiced)
				f.push_back(c.rate / mk.period);
		std::nth_element(f.begin(), f.begin() + f.size() / 2, f.end());
		return f.empty() ? 0.f : f[f.size() / 2];
	}
};

static int failures = 0;
static void check(bool ok, const char* what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}
static bool near(const std::vector<float>& v, std::initializer_list<float> want) {
	if (v.size() != want.size())
		return false;
	size_t i = 0;
	for (float w : want)
		if (std::fabs(v[i++] - w) > 1e-3f)
			return false;
	return true;
}

/** Both jacks high together for 1 ms: the same beat on LINE and WORD. */
static void pulseBoth(Rig& r, float after) {
	r.patch(Speak::LINE_INPUT, 10.f);
	r.patch(Speak::WORD_INPUT, 10.f);
	r.run(0.001f);
	r.patch(Speak::LINE_INPUT, 0.f);
	r.patch(Speak::WORD_INPUT, 0.f);
	r.run(after);
}
static void unplug(Rig& r, int input) {
	r.m->inputs[input].channels = 0;
	r.run(0.01f);
}
static float light(Rig& r, int id) {
	return r.m->lights[id].getBrightness();
}

int main() {
	Catalog::get().load("res");

	std::printf("-- Speak (full)\n");
	Rig r(modelSpeak);
	r.speak().playRequest = true;
	r.run(2.0f);
	check(r.lineRises == 1 && r.eols == 1, "play: one line gate and one end-of-line");
	check(r.wordRises == 2 && near(r.wordIdx, {0.f, 0.1f}), "play: two word gates, indices 0 and 0.1 V");
	check(r.phoneRises >= 6 && r.sumSq > 1.0, "play: phonemes and audible output");

	// LINE alone: triggers step through the lines, RESET returns to the first.
	r.reset();
	r.patch(Speak::LINE_INPUT, 0.f);
	r.run(0.01f);
	for (int i = 0; i < 3; i++)
		r.pulse(Speak::LINE_INPUT, 1.6f);
	check(light(r, Speak::LINE_TRIG_LIGHT) > 0.5f && light(r, Speak::LINE_INDEX_LIGHT) < 0.5f, "LINE: TRIG. INCR. light");
	r.pulse(Speak::RESET_INPUT, 0.01f);
	r.pulse(Speak::LINE_INPUT, 1.2f);
	check(near(r.lineIdx, {0.f, 0.1f, 0.2f, 0.f}), "LINE triggers: lines 1, 2, 3, then reset to 1");
	r.reset();
	long edge = r.frame;
	r.pulse(Speak::LINE_INPUT, 0.3f);
	std::printf("      trigger -> line gate: %.2f ms\n", (r.lineRiseFrame - edge) / 48.0);
	check(r.lineRiseFrame >= 0 && r.lineRiseFrame - edge < 48 * 4, "LINE: fires on the edge from the cache");
	r.run(1.5f);

	// LINE as index CV (after re-patching): speaks the selected line when it changes.
	unplug(r, Speak::LINE_INPUT);
	r.reset();
	for (float v : {7.f, 3.f}) { // 5 lines: 7 V -> 4th, 3 V -> 2nd
		r.patch(Speak::LINE_INPUT, v);
		r.run(2.2f);
	}
	check(near(r.lineIdx, {0.3f, 0.1f}), "LINE CV: selects and speaks on change");
	check(light(r, Speak::LINE_INDEX_LIGHT) > 0.5f && light(r, Speak::LINE_TRIG_LIGHT) < 0.5f, "LINE: INDEX light");
	unplug(r, Speak::LINE_INPUT);

	// WORD alone: triggers speak single words straight through the text.
	r.reset();
	r.patch(Speak::WORD_INPUT, 0.f);
	r.run(1.0f); // pre-render words
	for (int i = 0; i < 3; i++)
		r.pulse(Speak::WORD_INPUT, 0.7f);
	check(near(r.lineIdx, {0.f, 0.f, 0.1f}) && near(r.wordIdx, {0.f, 0.1f, 0.f}), "WORD triggers: hello, world, this");

	// Both: LINE picks the line silently, WORD speaks its words and wraps within it.
	r.pulse(Speak::RESET_INPUT, 0.01f);
	r.patch(Speak::LINE_INPUT, 0.f);
	r.run(0.01f);
	r.reset();
	pulseBoth(r, 0.7f);                // bar 1: line 1, "hello"
	r.pulse(Speak::WORD_INPUT, 0.7f);  // "world"
	r.pulse(Speak::WORD_INPUT, 0.7f);  // wraps: "hello"
	pulseBoth(r, 0.7f);                // bar 2: line 2, "this"
	r.pulse(Speak::LINE_INPUT, 0.7f);  // LINE alone moves to line 3 without speaking
	check(near(r.lineIdx, {0.f, 0.f, 0.f, 0.1f}) && near(r.wordIdx, {0.f, 0.1f, 0.f, 0.f}),
		"LINE + WORD: LINE edge lands first, WORD speaks and wraps within the line");
	r.pulse(Speak::WORD_INPUT, 0.7f);
	check(near(r.lineIdx, {0.f, 0.f, 0.f, 0.1f, 0.2f}), "LINE + WORD: a lone LINE step is silent, the next word comes from its line");
	unplug(r, Speak::LINE_INPUT);
	unplug(r, Speak::WORD_INPUT);

	std::printf("-- Speak Compact\n");
	Rig c(modelSpeakCompact);
	c.run(1.0f); // let pre-rendering fill the cache
	// Merged knobs: the engine renders on a 2-semitone grid and PSOLA supplies the rest.
	auto voiced = [&](float pitchKnob, float speedKnob, float& seconds) {
		c.set(Speak::PITCH_PARAM, pitchKnob);
		c.set(Speak::SPEED_PARAM, speedKnob);
		c.run(0.4f);
		c.reset();
		c.speak().playRequest = true;
		c.run(3.0f);
		size_t first = 0, last = 0;
		for (size_t i = 0; i < c.audio.size(); i++)
			if (std::fabs(c.audio[i]) > 0.05f)
				last = i, first = first ? first : i;
		seconds = (last - first) / 48000.f;
		return c.f0();
	};
	c.speak().ch->edit([](Doc& d) { d.row = 4; }); // "jumps over the lazy dog": long enough for a steady median f0
	float d0, d1, d2;
	float f0 = voiced(0.f, 0.f, d0), f3 = voiced(3.f, 0.f, d1), fs = voiced(0.f, 0.5f, d2);
	std::printf("      f0 %.1f Hz -> +3 st %.1f Hz (%.2f st); duration %.2f s -> speed x1.41 %.2f s (x%.2f, f0 %.1f Hz)\n",
		f0, f3, 12 * std::log2(f3 / f0), d0, d2, d0 / d2, fs);
	check(std::fabs(12 * std::log2(f3 / f0) - 3.f) < 0.6f, "PITCH: +3 semitones lands within 0.6 st");
	check(std::fabs(d0 / d2 - 1.414f) < 0.15f && std::fabs(12 * std::log2(fs / f0)) < 0.6f, "SPEED: +half octave is 1.41x faster, same pitch");

	// A pitch jump to an uncached grid point answers at once from the nearest cached render.
	c.set(Speak::PITCH_PARAM, 0.f);
	c.set(Speak::SPEED_PARAM, 0.f);
	c.run(1.0f);
	c.patch(Speak::LINE_INPUT, 0.f);
	c.run(0.01f);
	c.pulse(Speak::LINE_INPUT, 1.5f); // first pulse teaches the jack it carries triggers
	c.patch(Speak::PITCH_INPUT, 0.5f); // +6 semitones, never rendered
	c.run(0.01f);
	c.reset();
	edge = c.frame;
	c.pulse(Speak::LINE_INPUT, 1.5f);
	std::printf("      uncached pitch: trigger -> line gate %.2f ms\n", (c.lineRiseFrame - edge) / 48.0);
	check(c.lineRiseFrame >= 0 && c.lineRiseFrame - edge < 48 * 4, "nearest: no render wait on a new pitch");

	// The buttons beside LINE and WORD are triggers into those jacks.
	auto press = [&](int param, float after) {
		c.set(param, 1.f);
		c.run(0.01f);
		c.set(param, 0.f);
		c.run(after);
	};
	unplug(c, Speak::LINE_INPUT);
	c.set(Speak::PITCH_PARAM, 0.f);
	c.m->inputs[Speak::PITCH_INPUT].channels = 0;
	c.m->inputs[Speak::PITCH_INPUT].setVoltage(0.f);
	c.pulse(Speak::RESET_INPUT, 0.3f);
	c.m->inputs[Speak::RESET_INPUT].channels = 0;
	c.reset();
	press(Speak::LINE_BUTTON_PARAM, 1.6f);
	press(Speak::LINE_BUTTON_PARAM, 2.0f);
	press(Speak::WORD_BUTTON_PARAM, 0.8f);
	check(near(c.lineIdx, {0.f, 0.1f, 0.f}) && c.wordIdx.size() >= 3 && std::fabs(c.wordIdx.back()) < 1e-4f,
		"buttons: LINE steps lines 1, 2; WORD speaks the first word");
	check(c.speak().readout(Speak::SPEED_PARAM) == "1.00x" && c.speak().readout(Speak::PITCH_PARAM) == "+0.0", "readouts: SPEED 1.00x, PITCH +0.0");

	// One VOICE CV spans eSpeak's voices then Flite's, overriding the ENGINE switch.
	const Catalog& cat = Catalog::get();
	int ne = int(cat.voices(ESPEAK).size()), nf = int(cat.voices(FLITE).size());
	auto voiceAt = [&](float v) {
		c.patch(Speak::VOICE_INPUT, v);
		return c.speak().synthParams();
	};
	c.set(Speak::ENGINE_PARAM, 1.f);
	SynthParams lo = voiceAt(0.f), hi = voiceAt(10.f), first = voiceAt(10.f * ne / (ne + nf - 1));
	check(lo.engine == ESPEAK && lo.voice == 0 && hi.engine == FLITE && hi.voice == nf - 1 && first.engine == FLITE && first.voice == 0,
		"VOICE CV: 0 V = first eSpeak voice, 10 V = last Flite voice, switch overridden");

	std::printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
	return failures ? 1 : 0;
}
