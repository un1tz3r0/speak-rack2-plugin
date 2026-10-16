#pragma once
#include <algorithm>
#include <cmath>
#include "Common.hpp"

/** Finds pitch marks with a YIN f0 track, then places marks on the waveform's glottal peaks.
Unvoiced stretches get short, slightly jittered pseudo-periods so noise doesn't turn into a buzz. */
inline void analyzePitch(Clip& c) {
	const std::vector<float>& x = c.samples;
	const int n = int(x.size()), hop = std::max(1, int(c.rate * 0.005f));
	const int win = int(c.rate * 0.02f), tauMin = int(c.rate / 450.f), tauMax = int(c.rate / 55.f);
	const int frames = n / hop + 1;
	std::vector<float> period(frames, 0.f), d(tauMax + 1);

	float hi = 0.f, lo = 0.f;
	for (float v : x)
		hi = std::max(hi, v), lo = std::min(lo, v);
	const float sign = hi >= -lo ? 1.f : -1.f, floorRms = 0.02f * std::max(hi, -lo);

	for (int f = 0; f < frames && n >= win + tauMax; f++) {
		int s = std::clamp(f * hop - (win + tauMax) / 2, 0, n - win - tauMax);
		const float* p = &x[s];
		float e = 0.f;
		for (int j = 0; j < win; j++)
			e += p[j] * p[j];
		if (std::sqrt(e / win) < floorRms)
			continue;
		double running = 0.0;
		for (int tau = 1; tau <= tauMax; tau++) {
			float acc = 0.f;
			for (int j = 0; j < win; j++) {
				float q = p[j] - p[j + tau];
				acc += q * q;
			}
			running += acc;
			d[tau] = running > 0.0 ? float(acc * tau / running) : 1.f;
		}
		int best = -1;
		for (int tau = tauMin; tau <= tauMax; tau++) {
			if (d[tau] < 0.15f) {
				while (tau < tauMax && d[tau + 1] < d[tau])
					tau++;
				best = tau;
				break;
			}
		}
		if (best < 0) {
			best = int(std::min_element(d.begin() + tauMin, d.end()) - d.begin());
			if (d[best] > 0.35f)
				continue;
		}
		float delta = 0.f;
		if (best > tauMin && best < tauMax) {
			float a = d[best - 1], b = d[best], cc = d[best + 1], den = a - 2 * b + cc;
			if (den > 0.f)
				delta = 0.5f * (a - cc) / den;
		}
		period[f] = best + delta;
	}

	// Median of 5 over voiced neighbours; drop isolated voiced frames.
	std::vector<float> smooth(frames, 0.f);
	for (int f = 0; f < frames; f++) {
		if (period[f] <= 0.f)
			continue;
		float w[5];
		int k = 0;
		for (int j = std::max(0, f - 2); j <= std::min(frames - 1, f + 2); j++)
			if (period[j] > 0.f)
				w[k++] = period[j];
		if (k < 2)
			continue;
		std::nth_element(w, w + k / 2, w + k);
		smooth[f] = w[k / 2];
	}

	c.marks.clear();
	uint32_t lcg = 0x9E3779B9u;
	double pos = 0.0, last = -1.0;
	while (pos < n) {
		float T = smooth[std::min(frames - 1, int(pos / hop + 0.5))];
		if (T > 0.f) {
			int a = int(last >= 0.0 ? last + 0.7 * T : pos), b = std::min(n - 1, int(last >= 0.0 ? last + 1.3 * T : pos + T));
			if (a >= n - 1)
				break;
			int m = a;
			for (int j = a; j <= b; j++)
				if (sign * x[j] > sign * x[m])
					m = j;
			c.marks.push_back({float(m), T, true});
			last = pos = std::max(double(m), pos + 1.0);
		}
		else {
			lcg = lcg * 1664525u + 1013904223u;
			float u = c.rate * 0.005f * (0.8f + 0.4f * (lcg >> 8) / float(1 << 24));
			pos += u;
			c.marks.push_back({float(pos), u, false});
			last = -1.0;
		}
	}
}

/** TD-PSOLA playback of a clip with live pitch (formant-preserving), formant and speed control.
Pitch sets the grain spacing, formant sets the grain read rate, speed sets how fast the analysis
position walks through the clip. All three ratios may change every sample. */
class Player {
	struct Grain {
		double start, phase;
		float len, step;
	};
	static constexpr int MAX_GRAINS = 32;
	ClipPtr clip;
	Grain grains[MAX_GRAINS];
	int active = 0;
	double src = 0.0, untilGrain = 0.0;
	size_t mark = 0, word = 0, phone = 0;

	float at(double p) const {
		const std::vector<float>& s = clip->samples;
		long i = long(std::floor(p));
		float t = float(p - i);
		auto x = [&](long k) { return k >= 0 && k < long(s.size()) ? s[k] : 0.f; };
		float y0 = x(i - 1), y1 = x(i), y2 = x(i + 1), y3 = x(i + 2);
		float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
		return ((c3 * t + c2) * t + c1) * t + y1;
	}

	void spawn(float pitch, float formant, float r) {
		const std::vector<Mark>& m = clip->marks;
		if (m.empty()) {
			untilGrain += 0.005 * clip->rate / r;
			return;
		}
		while (mark + 1 < m.size() && m[mark + 1].pos <= src)
			mark++;
		size_t k = (mark + 1 < m.size() && m[mark + 1].pos - src < src - m[mark].pos) ? mark + 1 : mark;
		if (active < MAX_GRAINS)
			grains[active++] = {m[k].pos - m[k].period, 0.0, 2.f * m[k].period, formant * r};
		untilGrain += (m[k].voiced ? m[k].period / pitch : m[k].period) / r;
	}

	static const Span* track(const std::vector<Span>& spans, size_t& i, double pos) {
		while (i < spans.size() && spans[i].end <= pos)
			i++;
		return i < spans.size() && spans[i].start <= pos ? &spans[i] : nullptr;
	}

public:
	void start(ClipPtr c) {
		clip = std::move(c);
		active = 0;
		src = untilGrain = 0.0;
		mark = word = phone = 0;
	}
	/** Hands the clip back (so the caller can dispose of it off the audio thread). */
	ClipPtr release() {
		active = 0;
		return std::move(clip);
	}
	bool loaded() const { return bool(clip); }
	const Clip* current() const { return clip.get(); }
	bool playing() const { return clip && src < clip->samples.size(); }
	bool sounding() const { return clip && (playing() || active > 0); }
	const Span* currentWord() { return clip ? track(clip->words, word, src) : nullptr; }
	const Span* currentPhone() { return clip ? track(clip->phones, phone, src) : nullptr; }

	float process(float sampleRate, float pitch, float formant, float speed) {
		if (!clip)
			return 0.f;
		const float r = clip->rate / sampleRate;
		if (playing()) {
			while (untilGrain <= 0.0)
				spawn(pitch, formant, r);
			untilGrain -= 1.0;
			src += speed * r;
		}
		float sum = 0.f, wsum = 0.f;
		for (int i = 0; i < active; i++) {
			Grain& g = grains[i];
			float w = 0.5f - 0.5f * std::cos(float(2.0 * M_PI * g.phase / g.len));
			sum += w * at(g.start + g.phase);
			wsum += w;
			g.phase += g.step;
			if (g.phase >= g.len)
				grains[i--] = grains[--active];
		}
		return sum / std::max(wsum, 1.f);
	}
};
