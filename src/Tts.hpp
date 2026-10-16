#pragma once
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "Doc.hpp"

enum Engine { ESPEAK, FLITE, NUM_ENGINES };

struct Language {
	std::string code, name;
};

/** Immutable lists of what the engines offer; built once from the plugin's res/ folder. */
struct Catalog {
	std::string resDir;
	std::vector<std::string> variants; // eSpeak voice variants
	std::vector<int> variantGenders;   // 0 unknown, 1 male, 2 female
	std::vector<Language> languages;   // eSpeak languages
	std::vector<std::string> fliteVoices;

	static Catalog& get();
	void load(const std::string& resDir);
	int languageIndex(const std::string& code) const;
	const std::vector<std::string>& voices(int engine) const { return engine == FLITE ? fliteVoices : variants; }
	std::string voiceName(int engine, int index) const;
	int voiceGender(int engine, int index) const;
};

/** Everything one module shares with the synthesis worker. Owned jointly, so either side may go first.
A "unit" is what one trigger speaks, addressed as (line, word): word < 0 is the whole line; line < 0 makes word an
index into all words of the text; otherwise it is the word-th word of that line. Word indices wrap. */
struct Channel {
	static constexpr int MAX_LINES = 1024; // lines whose word counts the audio thread can see
	struct Request {
		int line, word;
		bool nearest; // on a cache miss, answer at once with the closest cached pitch/rate and render the exact one later
		uint64_t params;
		uint32_t seq;
	};
	struct Result {
		uint32_t seq;
		ClipPtr clip;
		uint64_t params;         // what was actually rendered (may differ from the request with `nearest`)
		int line, charOffset;    // where the unit sits in the text
		int wordBase, wordTotal; // a single word: its position among its line's words
	};
	struct Unit {
		std::string text;
		int line = -1, charOffset = 0, wordBase = 0, wordTotal = 0;
	};

	std::mutex mutex; // guards doc and tokens
	Doc doc;
	std::vector<Token> tokens;
	std::atomic<uint32_t> version{0};
	std::atomic<int> lineCount{1}, tokenCount{0}, cursorRow{0}, cursorToken{-1};
	std::array<std::atomic<uint16_t>, MAX_LINES> lineTokens{};
	std::atomic<uint64_t> liveParams{SynthParams().pack()};
	std::atomic<bool> wordMode{false}; // pre-render single words rather than lines

	Spsc<Request, 16> requests;  // audio -> worker
	Spsc<Result, 16> results;    // worker -> audio
	Spsc<ClipPtr, 64> graveyard; // audio -> worker, so clips are freed off the audio thread

	// Playback state for the display, written by the audio thread.
	std::atomic<int> playingLine{-1}, wordStart{-1}, wordLen{0}, nextLine{-1}, nextWord{-1};
	std::atomic<bool> waiting{false};

	/** Runs f(doc) under the lock and republishes the derived counters. */
	template <typename F>
	auto edit(F&& f) {
		std::lock_guard<std::mutex> lock(mutex);
		struct Publish {
			Channel* c;
			~Publish() {
				c->doc.clamp();
				c->tokens = c->doc.tokens();
				int lines = c->doc.count();
				std::vector<uint16_t> counts(MAX_LINES, 0);
				for (const Token& t : c->tokens)
					if (t.line < MAX_LINES)
						counts[t.line]++;
				for (int i = 0; i < MAX_LINES; i++)
					c->lineTokens[i] = counts[i];
				c->lineCount = lines;
				c->tokenCount = int(c->tokens.size());
				c->cursorRow = c->doc.row;
				c->cursorToken = Doc::tokenAt(c->tokens, c->doc.row, c->doc.col);
				c->version++;
			}
		} publish{this};
		return f(doc);
	}
	/** Global index of the word a (line, word) address names, or -1. Caller holds the lock. */
	int tokenOf(int line, int word) const {
		if (word < 0 || tokens.empty())
			return -1;
		if (line < 0)
			return word % int(tokens.size());
		int first = -1, n = 0;
		for (int i = 0; i < int(tokens.size()); i++)
			if (tokens[i].line == line)
				first = first < 0 ? i : first, n++;
		return n ? first + word % n : -1;
	}
	Unit unit(int line, int word) {
		std::lock_guard<std::mutex> lock(mutex);
		Unit u;
		if (word < 0) {
			if (line >= 0 && line < doc.count())
				u.text = toUtf8(doc.lines[line]), u.line = line;
			return u;
		}
		int i = tokenOf(line, word);
		if (i < 0)
			return u;
		const Token& t = tokens[i];
		u.text = toUtf8(doc.lines[t.line].substr(t.start, t.len));
		u.line = t.line, u.charOffset = t.start;
		for (const Token& o : tokens)
			u.wordBase += o.line == t.line && o.start < t.start, u.wordTotal += o.line == t.line;
		return u;
	}
};

/** Measured engine curves: engine pitch units <-> semitones from the default (50), rate <-> tempo relative to 175 wpm. */
namespace tune {
float pitchSemis(int engine, int pitch);
int pitchParam(int engine, float semis);
float pitchMin(int engine);
float pitchMax(int engine);
float tempo(int engine, int rate);
int rateParam(int engine, float tempo);
} // namespace tune

/** One background thread renders speech for every module: urgent requests first, then it
pre-renders each module's lines with its current settings so triggers usually hit the cache. */
class Service {
	struct Impl;
	std::unique_ptr<Impl> impl;
	Service();

public:
	~Service();
	static Service& get();
	void attach(const std::shared_ptr<Channel>& ch);
	/** Nudges the worker; safe to call from the audio thread. */
	void wake();
	/** Renders synchronously on the calling thread (for tests and tools). Not thread-safe with the worker. */
	ClipPtr renderNow(const std::string& text, SynthParams p);
	/** Drops every cached clip (tests). */
	void clearCache();
};
