#include "Tts.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <deque>
#include <list>
#include <sstream>
#include <unordered_map>
#include <rack.hpp>
#include <espeak-ng/speak_lib.h>
#include <flite.h>
extern "C" {
#include <cst_cg.h>
cst_voice* register_cmu_us_kal(const char*);
cst_voice* register_cmu_us_kal16(const char*);
cst_voice* register_cmu_us_awb(const char*);
cst_voice* register_cmu_us_rms(const char*);
cst_voice* register_cmu_us_slt(const char*);
}
#include "Psola.hpp"

// ---------------------------------------------------------------------------------------------
// Catalog

Catalog& Catalog::get() {
	static Catalog catalog;
	return catalog;
}

void Catalog::load(const std::string& dir) {
	resDir = dir;
	std::string data = rack::system::join(dir, "espeak-ng-data");

	variants.clear();
	for (const std::string& path : rack::system::getEntries(rack::system::join(data, "voices", "!v"))) {
		std::string name = rack::system::getFilename(path);
		if (name.find(' ') == std::string::npos)
			variants.push_back(name);
	}
	auto rank = [](const std::string& v) {
		bool classic = v.size() == 2 && (v[0] == 'm' || v[0] == 'f') && std::isdigit((unsigned char)v[1]);
		return std::make_tuple(!classic, classic ? v[0] == 'f' : 0, rack::string::lowercase(v));
	};
	std::sort(variants.begin(), variants.end(), [&](auto& a, auto& b) { return rank(a) < rank(b); });
	variantGenders.clear();
	for (const std::string& v : variants) {
		std::ifstream in(rack::system::join(data, "voices", "!v", v));
		int gender = 0;
		for (std::string ln; std::getline(in, ln);)
			if (ln.rfind("gender", 0) == 0)
				gender = ln.find("female") != std::string::npos ? 2 : ln.find("male") != std::string::npos ? 1 : 0;
		variantGenders.push_back(gender);
	}

	languages.clear();
	std::function<void(const std::string&)> walk = [&](const std::string& path) {
		if (rack::system::isDirectory(path)) {
			for (const std::string& e : rack::system::getEntries(path))
				walk(e);
			return;
		}
		std::ifstream in(path);
		Language l;
		for (std::string ln; std::getline(in, ln);) {
			std::istringstream ss(ln);
			std::string key;
			ss >> key;
			if (key == "name")
				std::getline(ss >> std::ws, l.name);
			else if (key == "language" && l.code.empty())
				ss >> l.code;
		}
		if (!l.code.empty())
			languages.push_back({l.code, l.name.empty() ? l.code : l.name});
	};
	walk(rack::system::join(data, "lang"));
	std::sort(languages.begin(), languages.end(), [](auto& a, auto& b) { return a.name < b.name; });

	fliteVoices = {"kal (m, diphone 8k)", "kal16 (m, diphone)", "awb (m, Scottish)", "rms (m)", "slt (f)"};
}

std::string Catalog::voiceName(int engine, int index) const {
	const auto& v = voices(engine);
	return v.empty() ? "" : v[index % v.size()];
}

int Catalog::voiceGender(int engine, int index) const {
	if (engine == FLITE)
		return index % fliteVoices.size() == 4 ? 2 : 1;
	return variantGenders.empty() ? 0 : variantGenders[index % variantGenders.size()];
}

int Catalog::languageIndex(const std::string& code) const {
	for (size_t i = 0; i < languages.size(); i++)
		if (languages[i].code == code)
			return int(i);
	return -1;
}

// ---------------------------------------------------------------------------------------------
// Phoneme identities: ARPAbet ids shared by both engines, eSpeak mnemonics mapped onto them.

static const char* const ARPABET[] = {"pau", "aa", "ae", "ah", "ao", "aw", "ax", "ay", "b", "ch", "d", "dh", "eh", "er",
	"ey", "f", "g", "hh", "ih", "iy", "jh", "k", "l", "m", "n", "ng", "ow", "oy", "p", "r", "s", "sh", "t", "th", "uh",
	"uw", "v", "w", "y", "z", "zh"};
static constexpr int NUM_ARPABET = sizeof(ARPABET) / sizeof(*ARPABET);

static int arpabetId(const std::string& name) {
	for (int i = 0; i < NUM_ARPABET; i++)
		if (name == ARPABET[i])
			return i;
	return NUM_ARPABET + int(std::hash<std::string>()(name) % (64 - NUM_ARPABET));
}

static bool arpabetVowel(int id) {
	static const bool vowel[NUM_ARPABET] = {0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0,
		0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};
	return id < NUM_ARPABET && vowel[id];
}

static int espeakPhoneId(const std::string& mnemonic, bool& vowel) {
	static const std::unordered_map<std::string, const char*> map = {{"p", "p"}, {"b", "b"}, {"t", "t"}, {"d", "d"},
		{"k", "k"}, {"g", "g"}, {"f", "f"}, {"v", "v"}, {"T", "th"}, {"D", "dh"}, {"s", "s"}, {"z", "z"}, {"S", "sh"},
		{"Z", "zh"}, {"h", "hh"}, {"tS", "ch"}, {"dZ", "jh"}, {"m", "m"}, {"n", "n"}, {"N", "ng"}, {"l", "l"},
		{"r", "r"}, {"w", "w"}, {"j", "y"}, {"a", "ae"}, {"A", "aa"}, {"aI", "ay"}, {"aU", "aw"}, {"e", "eh"},
		{"E", "eh"}, {"eI", "ey"}, {"@", "ax"}, {"3", "er"}, {"i", "iy"}, {"I", "ih"}, {"O", "ao"}, {"OI", "oy"},
		{"oU", "ow"}, {"o", "ow"}, {"U", "uh"}, {"u", "uw"}, {"V", "ah"}, {"0", "aa"}};
	std::string base;
	for (char c : mnemonic)
		if (!std::strchr(":;#/-!2'", c))
			base += c;
	for (size_t len = std::min<size_t>(base.size(), 2); len > 0; len--) {
		auto it = map.find(base.substr(0, len));
		if (it != map.end()) {
			int id = arpabetId(it->second);
			vowel = arpabetVowel(id);
			return id;
		}
	}
	vowel = !base.empty() && std::strchr("aeiouAEIOUVY03@&", base[0]);
	return arpabetId(mnemonic);
}

/** Codepoint index of a byte offset in UTF-8 text. */
static int codepointIndex(const std::string& text, size_t byte) {
	int n = 0;
	for (size_t i = 0; i < byte && i < text.size(); i++)
		n += (text[i] & 0xC0) != 0x80;
	return n;
}

// ---------------------------------------------------------------------------------------------
// Post-processing shared by both engines: trim silence, normalise, find pitch marks.

static void finish(Clip& c) {
	float peak = 0.f;
	for (float v : c.samples)
		peak = std::max(peak, std::fabs(v));
	if (peak < 1e-5f) {
		c.samples.clear(), c.words.clear(), c.phones.clear();
		return;
	}
	auto loud = [&](float v) { return std::fabs(v) > peak * 0.003f; };
	long first = std::find_if(c.samples.begin(), c.samples.end(), loud) - c.samples.begin();
	long last = c.samples.rend() - std::find_if(c.samples.rbegin(), c.samples.rend(), loud);
	first = std::max(0L, first - long(c.rate * 0.003f));
	last = std::min(long(c.samples.size()), last + long(c.rate * 0.02f));
	c.samples = std::vector<float>(c.samples.begin() + first, c.samples.begin() + last);
	for (float& v : c.samples)
		v *= 0.9f / peak;
	for (auto* spans : {&c.words, &c.phones}) {
		for (Span& s : *spans) {
			s.start = uint32_t(std::clamp(long(s.start) - first, 0L, long(c.samples.size())));
			s.end = uint32_t(std::clamp(long(s.end) - first, long(s.start), long(c.samples.size())));
		}
		spans->erase(std::remove_if(spans->begin(), spans->end(), [](const Span& s) { return s.end <= s.start; }), spans->end());
		for (size_t i = 0; i < spans->size(); i++)
			(*spans)[i].index = int16_t(i);
	}
	analyzePitch(c);
}

// ---------------------------------------------------------------------------------------------
// eSpeak NG

struct Espeak {
	struct Sink {
		std::vector<short> pcm;
		std::vector<espeak_EVENT> events;
	};
	static thread_local Sink* sink;
	int rate = 0;
	std::string voice;

	static int callback(short* wav, int n, espeak_EVENT* ev) {
		if (!sink)
			return 1;
		if (wav)
			sink->pcm.insert(sink->pcm.end(), wav, wav + n);
		for (; ev && ev->type != espeakEVENT_LIST_TERMINATED; ev++)
			sink->events.push_back(*ev);
		return 0;
	}

	bool init(const std::string& resDir) {
		rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 200, resDir.c_str(),
			espeakINITIALIZE_PHONEME_EVENTS | espeakINITIALIZE_DONT_EXIT);
		if (rate <= 0) {
			WARN("vcvspeak: eSpeak NG failed to initialise from %s", resDir.c_str());
			return false;
		}
		espeak_SetSynthCallback(callback);
		return true;
	}

	std::shared_ptr<Clip> render(const std::string& text, const SynthParams& p) {
		const Catalog& cat = Catalog::get();
		std::string name = p.lang < cat.languages.size() ? cat.languages[p.lang].code : "en-us";
		if (!cat.variants.empty())
			name += "+" + cat.variants[p.voice % cat.variants.size()];
		if (name != voice) {
			if (espeak_SetVoiceByName(name.c_str()) != EE_OK)
				espeak_SetVoiceByName("en");
			voice = name;
		}
		espeak_SetParameter(espeakRATE, p.rate, 0);
		espeak_SetParameter(espeakPITCH, p.pitch, 0);
		espeak_SetParameter(espeakRANGE, p.range, 0);

		Sink s;
		sink = &s;
		espeak_Synth(text.c_str(), text.size() + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, nullptr, nullptr);
		sink = nullptr;

		auto clip = std::make_shared<Clip>();
		clip->rate = float(rate);
		clip->samples.reserve(s.pcm.size());
		for (short v : s.pcm)
			clip->samples.push_back(v / 32768.f);
		auto at = [&](const espeak_EVENT& e) { return uint32_t(std::min<size_t>(size_t(e.audio_position) * rate / 1000, s.pcm.size())); };

		// Words run from their event to the next boundary (next word, pause phoneme or end of text).
		int16_t w = 0, ph = 0;
		for (const espeak_EVENT& e : s.events) {
			uint32_t t = at(e);
			bool boundary = e.type == espeakEVENT_WORD || e.type == espeakEVENT_END || e.type == espeakEVENT_MSG_TERMINATED ||
			                (e.type == espeakEVENT_PHONEME && e.id.string[0] == '_');
			if (e.type == espeakEVENT_PHONEME || boundary) {
				if (!clip->phones.empty() && clip->phones.back().end == UINT32_MAX)
					clip->phones.back().end = t;
			}
			if (boundary && !clip->words.empty() && clip->words.back().end == UINT32_MAX)
				clip->words.back().end = t;
			if (e.type == espeakEVENT_WORD)
				clip->words.push_back({t, UINT32_MAX, w++, 0, int16_t(e.text_position - 1), int16_t(e.length), false});
			else if (e.type == espeakEVENT_PHONEME && e.id.string[0] != '_' && e.id.string[0]) {
				bool vowel;
				int id = espeakPhoneId(std::string(e.id.string, strnlen(e.id.string, sizeof e.id.string)), vowel);
				clip->phones.push_back({t, UINT32_MAX, ph++, int16_t(id), -1, 0, vowel});
			}
		}
		for (auto* spans : {&clip->words, &clip->phones})
			for (Span& sp : *spans)
				sp.end = std::min<uint32_t>(sp.end, uint32_t(s.pcm.size()));
		return clip;
	}
};
thread_local Espeak::Sink* Espeak::sink = nullptr;

// ---------------------------------------------------------------------------------------------
// Flite

struct Flite {
	struct Voice {
		cst_voice* v;
		float stddev;
	};
	std::vector<Voice> voices;

	bool init() {
		flite_init();
		for (auto reg : {register_cmu_us_kal, register_cmu_us_kal16, register_cmu_us_awb, register_cmu_us_rms, register_cmu_us_slt}) {
			cst_voice* v = reg(nullptr);
			float sd = 11.f;
			if (feat_present(v->features, "int_f0_target_stddev"))
				sd = feat_float(v->features, "int_f0_target_stddev");
			else if (feat_present(v->features, "cg_db"))
				sd = val_cg_db(feat_val(v->features, "cg_db"))->f0_stddev;
			voices.push_back({v, sd});
		}
		return true;
	}

	std::shared_ptr<Clip> render(const std::string& text, const SynthParams& p) {
		Voice& v = voices[p.voice % voices.size()];
		feat_set_float(v.v->features, "f0_shift", std::pow(2.f, (p.pitch - 50) / 50.f));
		feat_set_float(v.v->features, "int_f0_target_stddev", v.stddev * p.range / 50.f);
		feat_set_float(v.v->features, "duration_stretch", 175.f / p.rate);

		cst_utterance* u = flite_synth_text(text.c_str(), v.v);
		auto clip = std::make_shared<Clip>();
		if (!u)
			return clip;
		cst_wave* wav = utt_wave(u);
		clip->rate = float(wav->sample_rate);
		clip->samples.reserve(wav->num_samples);
		for (int i = 0; i < wav->num_samples; i++)
			clip->samples.push_back(wav->samples[i] / 32768.f);
		auto sample = [&](float sec) { return uint32_t(std::clamp(sec * clip->rate, 0.f, float(wav->num_samples))); };
		auto segEnd = [&](const cst_item* seg) { return seg ? item_feat_float(seg, "end") : 0.f; };

		int16_t ph = 0;
		for (cst_item* s = relation_head(utt_relation(u, "Segment")); s; s = item_next(s)) {
			std::string name = item_feat_string(s, "name");
			if (name == "pau")
				continue;
			int id = arpabetId(name);
			clip->phones.push_back({sample(segEnd(item_prev(s))), sample(segEnd(s)), ph++, int16_t(id), -1, 0, arpabetVowel(id)});
		}
		int16_t w = 0;
		size_t search = 0;
		for (cst_item* word = relation_head(utt_relation(u, "Word")); word; word = item_next(word)) {
			cst_item* syl = item_as(word, "SylStructure");
			cst_item* first = syl ? item_daughter(item_daughter(syl)) : nullptr;
			cst_item* last = syl ? item_last_daughter(item_last_daughter(syl)) : nullptr;
			if (!first || !last)
				continue;
			Span sp{sample(segEnd(item_prev(item_as(first, "Segment")))), sample(segEnd(last)), w++, 0, -1, 0, false};
			if (cst_item* tok = item_parent(item_as(word, "Token"))) {
				std::string name = item_feat_string(tok, "name");
				size_t at = name.empty() ? std::string::npos : text.find(name, search);
				if (at != std::string::npos) {
					sp.charStart = int16_t(codepointIndex(text, at));
					sp.charLen = int16_t(codepointIndex(text, at + name.size()) - sp.charStart);
					if (!item_next(item_as(word, "Token")))
						search = at + name.size();
				}
			}
			clip->words.push_back(sp);
		}
		delete_utterance(u);
		return clip;
	}
};

// ---------------------------------------------------------------------------------------------
// LRU clip cache

class Cache {
	size_t capacity, used = 0;
	std::list<std::pair<std::string, ClipPtr>> order;
	std::unordered_map<std::string, decltype(order)::iterator> index;

public:
	explicit Cache(size_t capacity) : capacity(capacity) {}
	bool find(const std::string& key, ClipPtr& out) {
		auto it = index.find(key);
		if (it == index.end())
			return false;
		order.splice(order.begin(), order, it->second);
		out = it->second->second;
		return true;
	}
	void put(const std::string& key, ClipPtr clip) {
		used += clip ? clip->bytes() : 0;
		order.emplace_front(key, std::move(clip));
		index[key] = order.begin();
		while (used > capacity && order.size() > 1) {
			auto& victim = order.back();
			used -= victim.second ? victim.second->bytes() : 0;
			index.erase(victim.first);
			order.pop_back();
		}
	}
	template <typename F>
	void each(F&& f) const {
		for (const auto& e : order)
			f(e.first, e.second);
	}
	void clear() {
		order.clear(), index.clear(), used = 0;
	}
};

// ---------------------------------------------------------------------------------------------
// Calibration, measured with voices m1, f1, m5 (eSpeak) and kal, slt (Flite) on a pangram.

namespace tune {
static const float ESPEAK_SEMIS[11] = {-7.2f, -5.9f, -4.9f, -3.2f, -1.75f, 0.f, 1.77f, 3.6f, 5.47f, 7.42f, 9.14f};
static const int RATES[11] = {80, 100, 125, 150, 175, 210, 250, 300, 350, 400, 450};
static const float TEMPO[NUM_ENGINES][11] = {
	{0.49f, 0.578f, 0.718f, 0.857f, 1.f, 1.2f, 1.42f, 1.73f, 2.04f, 2.34f, 2.66f},
	{0.44f, 0.562f, 0.708f, 0.847f, 1.f, 1.195f, 1.4f, 1.64f, 1.965f, 2.235f, 2.49f}};

float pitchSemis(int engine, int pitch) {
	float x = std::clamp(pitch, 0, 100) / 10.f;
	if (engine == FLITE)
		return (x - 5.f) * 2.4f; // f0_shift = 2^((pitch - 50) / 50)
	int i = std::min(9, int(x));
	return ESPEAK_SEMIS[i] + (ESPEAK_SEMIS[i + 1] - ESPEAK_SEMIS[i]) * (x - i);
}
float pitchMin(int engine) { return pitchSemis(engine, 0); }
float pitchMax(int engine) { return pitchSemis(engine, 100); }
int pitchParam(int engine, float semis) {
	int best = 50;
	for (int p = 0; p <= 100; p++)
		if (std::fabs(pitchSemis(engine, p) - semis) < std::fabs(pitchSemis(engine, best) - semis))
			best = p;
	return best;
}
float tempo(int engine, int rate) {
	const float* t = TEMPO[engine == FLITE];
	int i = 0;
	while (i < 9 && rate > RATES[i + 1])
		i++;
	float f = std::clamp(float(rate - RATES[i]) / (RATES[i + 1] - RATES[i]), 0.f, 1.f);
	return t[i] + (t[i + 1] - t[i]) * f;
}
int rateParam(int engine, float target) {
	int best = 175;
	for (int r = 80; r <= 450; r += 5)
		if (std::fabs(std::log2(tempo(engine, r) / target)) < std::fabs(std::log2(tempo(engine, best) / target)))
			best = r;
	return best;
}
} // namespace tune

// ---------------------------------------------------------------------------------------------
// Service

struct Service::Impl {
	std::mutex m;
	std::condition_variable cv;
	std::atomic<bool> poke{false};
	bool quit = false;
	std::vector<std::weak_ptr<Channel>> channels;
	std::thread thread;

	std::mutex renderMutex;
	bool ready[NUM_ENGINES] = {}, tried = false;
	Espeak espeak;
	Flite flite;
	Cache cache{128u << 20};
	std::deque<std::pair<std::string, SynthParams>> todo; // exact renders owed after a nearest-match answer

	struct Watch {
		uint64_t params = 0;
		uint32_t version = ~0u;
		bool word = false;
		double since = 0.0;
		int next = 0;
		bool done = false;
	};
	std::unordered_map<const Channel*, Watch> watch;

	static SynthParams normalise(SynthParams p) {
		size_t nv = Catalog::get().voices(p.engine).size();
		p.voice = uint8_t(nv ? p.voice % nv : 0);
		if (p.engine == FLITE)
			p.lang = 0;
		return p;
	}
	static std::string key(const std::string& text, SynthParams p) {
		uint64_t packed = normalise(p).pack();
		return std::string(reinterpret_cast<const char*>(&packed), sizeof packed) + text;
	}

	/** Closest cached rendering of the same text and voice, differing only in pitch and rate. */
	bool nearest(const std::string& text, SynthParams want, ClipPtr& out, SynthParams& used) {
		want = normalise(want);
		float best = 1e9f;
		cache.each([&](const std::string& k, const ClipPtr& clip) {
			if (!clip || k.size() != text.size() + 8 || k.compare(8, std::string::npos, text) != 0)
				return;
			uint64_t packed;
			std::memcpy(&packed, k.data(), 8);
			SynthParams p = SynthParams::unpack(packed);
			if (p.engine != want.engine || p.voice != want.voice || p.range != want.range || p.lang != want.lang)
				return;
			float ds = std::fabs(tune::pitchSemis(p.engine, p.pitch) - tune::pitchSemis(want.engine, want.pitch));
			float dt = std::fabs(std::log2(tune::tempo(p.engine, p.rate) / tune::tempo(want.engine, want.rate)));
			float d = ds / 2.f + dt * 4.f;
			if (ds <= 7.f && dt <= 0.75f && d < best)
				best = d, out = clip, used = p;
		});
		return best < 1e9f;
	}

	ClipPtr render(const std::string& text, SynthParams p, bool* cached = nullptr, bool allowNearest = false, SynthParams* used = nullptr) {
		std::lock_guard<std::mutex> lock(renderMutex);
		if (!tried) {
			tried = true;
			ready[ESPEAK] = espeak.init(Catalog::get().resDir);
			ready[FLITE] = flite.init();
		}
		if (used)
			*used = p;
		if (text.find_first_not_of(" \t") == std::string::npos || p.engine >= NUM_ENGINES || !ready[p.engine])
			return nullptr;
		std::string k = key(text, p);
		ClipPtr clip;
		if (cache.find(k, clip)) {
			if (cached)
				*cached = true;
			return clip;
		}
		SynthParams near;
		if (allowNearest && nearest(text, p, clip, near)) {
			if (used)
				*used = near;
			if (todo.size() < 32)
				todo.push_back({text, p});
			return clip;
		}
		std::shared_ptr<Clip> c = p.engine == FLITE ? flite.render(text, p) : espeak.render(text, p);
		c->text = toUtf32(text);
		finish(*c);
		clip = c->samples.empty() ? nullptr : ClipPtr(c);
		cache.put(k, clip);
		return clip;
	}

	/** Renders one not-yet-cached unit: first any exact renders owed, then the units of a module whose
	settings have been still for a moment. */
	bool prerender(const std::vector<std::shared_ptr<Channel>>& live) {
		while (!todo.empty()) {
			auto job = todo.front();
			todo.pop_front();
			bool cached = false;
			render(job.first, job.second, &cached);
			if (!cached)
				return true;
		}
		double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		for (const auto& ch : live) {
			Watch& w = watch[ch.get()];
			uint64_t params = ch->liveParams;
			uint32_t version = ch->version;
			bool word = ch->wordMode;
			if (params != w.params || version != w.version || word != w.word) {
				if (params != w.params)
					w.since = now;
				w.params = params, w.version = version, w.word = word, w.next = 0, w.done = false;
			}
			if (w.done || now - w.since < 0.25)
				continue;
			int n = std::min(word ? ch->tokenCount.load() : ch->lineCount.load(), 512);
			while (w.next < n) {
				bool cached = false;
				int i = w.next++;
				render((word ? ch->unit(-1, i) : ch->unit(i, -1)).text, SynthParams::unpack(params), &cached);
				if (!cached)
					return true;
			}
			w.done = true;
		}
		return false;
	}

	void loop() {
		std::vector<std::shared_ptr<Channel>> live;
		while (true) {
			{
				std::lock_guard<std::mutex> lock(m);
				if (quit)
					return;
				channels.erase(std::remove_if(channels.begin(), channels.end(), [&](auto& w) {
					auto ch = w.lock();
					if (ch)
						live.push_back(ch);
					return !ch;
				}), channels.end());
			}
			bool worked = false;
			for (const auto& ch : live) {
				ClipPtr dead;
				while (ch->graveyard.pop(dead))
					dead.reset();
				Channel::Request req{}, latest{};
				bool any = false;
				while (ch->requests.pop(req))
					latest = req, any = true;
				if (any) {
					Channel::Unit u = ch->unit(latest.line, latest.word);
					SynthParams used;
					ClipPtr clip = render(u.text, SynthParams::unpack(latest.params), nullptr, latest.nearest, &used);
					ch->results.push({latest.seq, std::move(clip), used.pack(), u.line, u.charOffset, u.wordBase, u.wordTotal});
					worked = true;
				}
			}
			if (!worked)
				worked = prerender(live);
			if (live.size() < watch.size()) {
				for (auto it = watch.begin(); it != watch.end();)
					it = std::any_of(live.begin(), live.end(), [&](auto& c) { return c.get() == it->first; }) ? std::next(it) : watch.erase(it);
			}
			bool idle = live.empty();
			live.clear();
			if (!worked) {
				std::unique_lock<std::mutex> lock(m);
				cv.wait_for(lock, std::chrono::milliseconds(idle ? 200 : 5), [&] { return poke.load() || quit; });
				poke = false;
			}
		}
	}
};

Service::Service() : impl(new Impl) {
	impl->thread = std::thread([this] { impl->loop(); });
}

Service::~Service() {
	{
		std::lock_guard<std::mutex> lock(impl->m);
		impl->quit = true;
	}
	impl->cv.notify_all();
	if (impl->thread.joinable())
		impl->thread.join();
}

Service& Service::get() {
	static Service service;
	return service;
}

void Service::attach(const std::shared_ptr<Channel>& ch) {
	std::lock_guard<std::mutex> lock(impl->m);
	impl->channels.push_back(ch);
}

void Service::wake() {
	impl->poke = true;
	impl->cv.notify_one();
}

ClipPtr Service::renderNow(const std::string& text, SynthParams p) {
	return impl->render(text, p);
}

void Service::clearCache() {
	std::lock_guard<std::mutex> lock(impl->renderMutex);
	impl->cache.clear();
}
