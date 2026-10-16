#pragma once
#include <algorithm>
#include <cmath>

/** How an index voltage maps onto N units. Spans quantise the range evenly; steps are a fixed size per unit. */
enum IndexRange { SPAN_0_10, SPAN_0_5, SPAN_0_1, SPAN_PM5, SPAN_PM10, STEP_VOLT, STEP_TENTH, STEP_SEMITONE, NUM_INDEX_RANGES };
inline const char* const INDEX_RANGE_NAMES[NUM_INDEX_RANGES] = {
	"0 to 10 V spans all", "0 to 5 V spans all", "0 to 1 V spans all", "-5 to 5 V spans all", "-10 to 10 V spans all",
	"1 V per step", "0.1 V per step", "1/12 V per step (semitones)"};

inline int mapIndex(float v, int n, int range) {
	if (n <= 0)
		return -1;
	static const float lo[] = {0.f, 0.f, 0.f, -5.f, -10.f}, hi[] = {10.f, 5.f, 1.f, 5.f, 10.f};
	int i;
	switch (range) {
		case STEP_VOLT: i = int(std::round(v)); break;
		case STEP_TENTH: i = int(std::round(v * 10.f)); break;
		case STEP_SEMITONE: i = int(std::round(v * 12.f)); break;
		default: i = int((v - lo[range]) / (hi[range] - lo[range]) * n);
	}
	return std::clamp(i, 0, n - 1);
}

/** Tells trigger streams from index CV on one jack. Pulses that fall back to zero within 20 ms are triggers;
anything that holds a level longer, or steps between non-zero levels, is an index. The verdict sticks until the
signal shows the other behaviour, so a trigger stream fires on its rising edges with no added latency. */
struct InputClassifier {
	enum Mode { AUTO, TRIGGERS, INDEX, NUM_MODES };
	enum Kind { UNKNOWN, TRIG, LEVEL };
	enum Event { NONE, FIRE };

	Kind kind = UNKNOWN;
	bool high = false, fired = false;
	float highTime = 0.f, plateau = 0.f;

	void reset() { kind = UNKNOWN, high = fired = false; }

	/** Returns FIRE when a trigger arrives. When `kind == LEVEL` the caller reads the voltage as an index. */
	Event process(float v, float dt, int mode) {
		if (mode == INDEX) {
			kind = LEVEL;
			return NONE;
		}
		if (!high && v >= 1.f) {
			high = true, highTime = 0.f, plateau = v;
			fired = kind == TRIG || mode == TRIGGERS;
			return fired ? FIRE : NONE;
		}
		if (!high)
			return NONE;
		highTime += dt;
		if (highTime < 0.002f)
			plateau = v;
		if (v < 0.1f) {
			high = false;
			if (highTime <= 0.02f && !fired && mode == AUTO) {
				kind = TRIG;
				return FIRE;
			}
			return NONE;
		}
		if (mode == AUTO && ((kind == UNKNOWN && highTime > 0.02f) || (kind == TRIG && std::fabs(v - plateau) > 0.5f)))
			kind = LEVEL;
		return NONE;
	}
};
