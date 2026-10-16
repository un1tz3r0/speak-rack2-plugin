#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

/** Lock-free single-producer/single-consumer queue that moves values in and out. */
template <typename T, size_t N>
class Spsc {
	static_assert((N & (N - 1)) == 0, "N must be a power of two");
	std::array<T, N> slots;
	std::atomic<size_t> head{0}, tail{0};

public:
	bool push(T v) {
		size_t h = head.load(std::memory_order_relaxed);
		if (h - tail.load(std::memory_order_acquire) == N)
			return false;
		slots[h & (N - 1)] = std::move(v);
		head.store(h + 1, std::memory_order_release);
		return true;
	}
	bool pop(T& out) {
		size_t t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_acquire))
			return false;
		out = std::move(slots[t & (N - 1)]);
		tail.store(t + 1, std::memory_order_release);
		return true;
	}
};

/** Pitch mark for PSOLA: grain centre, local period (both in clip samples). */
struct Mark {
	float pos, period;
	bool voiced;
};

/** A timed segment of a clip: a word or a phoneme. */
struct Span {
	uint32_t start, end;       // clip samples
	int16_t index;             // ordinal within the line
	int16_t id;                // phoneme identity (phonemes only)
	int16_t charStart, charLen; // position in the line text (words only, -1 if unknown)
	bool vowel;
};

/** One synthesised line, immutable once published to the audio thread. */
struct Clip {
	float rate = 22050.f;
	std::u32string text; // what was spoken; word spans index into it
	std::vector<float> samples;
	std::vector<Mark> marks;
	std::vector<Span> words, phones;

	size_t bytes() const {
		return sizeof(Clip) + samples.size() * sizeof(float) + marks.size() * sizeof(Mark) +
		       (words.size() + phones.size()) * sizeof(Span);
	}
};
using ClipPtr = std::shared_ptr<const Clip>;

/** Engine parameters latched when a line is triggered. Exactly 8 bytes so it packs into an atomic. */
struct SynthParams {
	uint8_t engine = 0; // 0 = eSpeak NG, 1 = Flite
	uint8_t voice = 0;  // variant (eSpeak) or voice (Flite) index
	uint8_t pitch = 50; // 0..100
	uint8_t range = 50; // 0..100
	uint16_t rate = 175; // words per minute, 80..450
	uint16_t lang = 0;  // eSpeak language index

	uint64_t pack() const {
		uint64_t v;
		std::memcpy(&v, this, sizeof v);
		return v;
	}
	static SynthParams unpack(uint64_t v) {
		SynthParams p;
		std::memcpy(static_cast<void*>(&p), &v, sizeof p);
		return p;
	}
	bool operator==(const SynthParams& o) const { return pack() == o.pack(); }
	bool operator!=(const SynthParams& o) const { return pack() != o.pack(); }
};
static_assert(sizeof(SynthParams) == 8, "SynthParams must pack into 64 bits");

inline std::string toUtf8(const std::u32string& s) {
	std::string out;
	for (char32_t c : s) {
		if (c < 0x80)
			out += char(c);
		else if (c < 0x800)
			out += {char(0xC0 | c >> 6), char(0x80 | (c & 0x3F))};
		else if (c < 0x10000)
			out += {char(0xE0 | c >> 12), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F))};
		else
			out += {char(0xF0 | c >> 18), char(0x80 | (c >> 12 & 0x3F)), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F))};
	}
	return out;
}

inline std::u32string toUtf32(const std::string& s) {
	std::u32string out;
	for (size_t i = 0; i < s.size();) {
		unsigned char c = s[i];
		int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
		char32_t cp = n == 0 ? c : c & (0x3F >> n);
		for (int k = 1; k <= n && i + k < s.size(); k++)
			cp = cp << 6 | (s[i + k] & 0x3F);
		out += cp;
		i += n + 1;
	}
	return out;
}
