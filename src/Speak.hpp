#pragma once
#include "plugin.hpp"
#include "Tts.hpp"
#include "Psola.hpp"
#include "Sequencer.hpp"

/** What a Speak module tells the expanders chained on its right, once per sample. Each expander passes it on. */
struct SpeakLink {
	bool valid = false, speaking = false, vowel = false;
	int16_t phone = -1, phoneIndex = -1, phoneCount = 0;
	int16_t wordIndex = -1, wordCount = 0, line = -1, lineCount = 0;
	uint8_t engine = 0, voice = 0;
	uint32_t startSerial = 0, endSerial = 0, wordSerial = 0, phoneSerial = 0;
	float level = 0.f;
	char word[32] = {};
};

bool isSpeakModel(Model* m);
bool isSpeakExpander(Model* m);

/** Copies `msg` into the expander on `from`'s right, if there is one of ours. */
inline void sendRight(Module* from, const SpeakLink& msg) {
	Module* r = from->rightExpander.module;
	if (!r || !isSpeakExpander(r->model))
		return;
	*static_cast<SpeakLink*>(r->leftExpander.producerMessage) = msg;
	r->leftExpander.requestMessageFlip();
}

/** Expander side: the latest message, and whether it comes from a live chain. */
inline const SpeakLink& receiveLeft(Module* self, bool& linked) {
	const SpeakLink& in = *static_cast<const SpeakLink*>(self->leftExpander.consumerMessage);
	Module* l = self->leftExpander.module;
	linked = l && (isSpeakModel(l->model) || isSpeakExpander(l->model)) && in.valid;
	return in;
}

/** One LINE or WORD jack: tells triggers from index CV, and reports what the patch asks for. */
struct SeqInput {
	enum Event { NONE, STEP, SELECT };
	InputClassifier classifier;
	int lastIndex = -1, fired = -1;
	float stable = 0.f;

	/** STEP on a trigger; SELECT with `index` set when a steady CV settles on a new index among `n`. */
	Event process(Input& in, float dt, int mode, int range, int n, int& index) {
		if (!in.isConnected()) {
			classifier.reset();
			lastIndex = fired = -1;
			return NONE;
		}
		float v = in.getVoltage();
		if (classifier.process(v, dt, mode) == InputClassifier::FIRE)
			return STEP;
		if (classifier.kind != InputClassifier::LEVEL || n <= 0)
			return NONE;
		int i = mapIndex(v, n, range);
		if (i != lastIndex)
			lastIndex = i, stable = 0.f;
		else if ((stable += dt) >= 0.005f && i != fired) {
			fired = index = i;
			return SELECT;
		}
		return NONE;
	}
	bool trig() const { return classifier.kind == InputClassifier::TRIG; }
	bool level() const { return classifier.kind == InputClassifier::LEVEL; }
};

/** Both Speak panels share this module. The compact one merges engine pitch and rate into its live PITCH and
SPEED knobs; the parameters it doesn't show stay unused.

Sequencing comes from two jacks that each take triggers (step to the next) or CV (select by index):
LINE alone speaks lines, WORD alone speaks words through the whole text, and with both patched LINE picks the line
(silently) while WORD speaks its words. */
struct Speak : Module {
	// Knob column i pairs PARAM i with INPUT i.
	enum ParamId { ENGINE_PARAM, VOICE_PARAM, PITCH_PARAM, RANGE_PARAM, RATE_PARAM, SHIFT_PARAM, FORMANT_PARAM, SPEED_PARAM,
		LINE_BUTTON_PARAM, WORD_BUTTON_PARAM, NUM_PARAMS };
	enum InputId { ENGINE_INPUT, VOICE_INPUT, PITCH_INPUT, RANGE_INPUT, RATE_INPUT, SHIFT_INPUT, FORMANT_INPUT, SPEED_INPUT,
		LINE_INPUT, WORD_INPUT, RESET_INPUT, NUM_INPUTS };
	enum OutputId { AUDIO_OUTPUT, NUM_OUTPUTS };
	enum LightId { LINE_TRIG_LIGHT, LINE_INDEX_LIGHT, WORD_TRIG_LIGHT, WORD_INDEX_LIGHT, NUM_LIGHTS };

	const bool compact;
	std::shared_ptr<Channel> ch = std::make_shared<Channel>();
	std::string language = "en-us";
	int languageIndex = 0;
	int indexRange = SPAN_0_10;
	int lineMode = InputClassifier::AUTO, wordMode = InputClassifier::AUTO;
	std::atomic<bool> playRequest{false};

	explicit Speak(bool compact);
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* root) override;
	void onReset() override;

	void setLanguage(const std::string& code);
	/** Engine parameters for a render now. Compact snaps pitch and rate to a coarse grid (PSOLA covers the rest). */
	SynthParams synthParams();
	int voiceIndex(int engine);
	float targetSemis();
	float targetOctaves();
	/** A knob's effective value (knob + CV) as the screen's readout shows it. */
	std::string readout(int param);

private:
	struct Playing {
		SynthParams params;
		int line = -1, charOffset = 0, wordBase = 0, wordTotal = 0;
	};
	dsp::SchmittTrigger reset;
	dsp::BooleanTrigger lineButton, wordButton;
	dsp::ClockDivider paramDivider;
	SeqInput lineIn, wordIn;
	float wordDelay = -1.f, envelope = 0.f, fadeGain = 0.f;
	int lineStep = 0, wordStep = 0, line = 0, lastWord = -1, lastPhone = -1;
	uint32_t seq = 0;
	bool waiting = false, wasSpeaking = false;
	Playing playing;
	SpeakLink link;
	Player voice, fading;

	float knob(int id, float cvScale, float lo, float hi);
	void sequence(float dt);
	void fire(int line, int word);
	void begin(Channel::Result& r);
	void dispose(ClipPtr clip);
	void track(bool speaking);
};

struct SpeakFull : Speak {
	SpeakFull() : Speak(false) {}
};
struct SpeakCompact : Speak {
	SpeakCompact() : Speak(true) {}
};
