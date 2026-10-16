#include "Speak.hpp"
#include "layout.hpp"

/** Expander: the line, word and phoneme gates and indices, end-of-unit trigger and speech envelope. */
struct SpeakOuts : Module {
	enum OutputId { LINE_OUTPUT, LINE_INDEX_OUTPUT, WORD_OUTPUT, WORD_INDEX_OUTPUT, PHONE_OUTPUT, PHONE_INDEX_OUTPUT, EOL_OUTPUT, ENV_OUTPUT, NUM_OUTPUTS };
	enum LightId { LINK_LIGHT, NUM_LIGHTS };
	enum Scale { SCALE_TENTH, SCALE_VOLT, SCALE_SEMITONE, SCALE_SPAN, NUM_SCALES };

	SpeakLink messages[2];
	int scale = SCALE_TENTH;
	dsp::PulseGenerator lineGap, wordGap, phoneGap, eol;
	uint32_t start = 0, end = 0, word = 0, phone = 0;

	SpeakOuts() {
		config(0, 0, NUM_OUTPUTS, NUM_LIGHTS);
		configOutput(LINE_OUTPUT, "Line gate (high while speaking)");
		configOutput(LINE_INDEX_OUTPUT, "Line index");
		configOutput(WORD_OUTPUT, "Word gate");
		configOutput(WORD_INDEX_OUTPUT, "Word index within the line");
		configOutput(PHONE_OUTPUT, "Phoneme gate (poly: 1 any, 2 vowel)");
		configOutput(PHONE_INDEX_OUTPUT, "Phoneme index (poly: 1 position, 2 identity at 0.1 V/id)");
		configOutput(EOL_OUTPUT, "End of line (or word) trigger");
		configOutput(ENV_OUTPUT, "Speech envelope (0-10 V)");
		configLight(LINK_LIGHT, "Linked to vcvspeak on the left");
		leftExpander.producerMessage = &messages[0];
		leftExpander.consumerMessage = &messages[1];
	}

	float index(int i, int n) const {
		if (i < 0)
			return 0.f;
		switch (scale) {
			case SCALE_VOLT: return std::min(10.f, float(i));
			case SCALE_SEMITONE: return i / 12.f;
			case SCALE_SPAN: return n > 0 ? 10.f * (i + 0.5f) / n : 0.f;
			default: return std::min(10.f, i * 0.1f);
		}
	}

	/** When `serial` moves on while the gate is already high, a 1 ms dip gives back-to-back units, words and
	phonemes their own edges; a gate that was low just rises. */
	static void edge(uint32_t now, uint32_t& seen, dsp::PulseGenerator& p, bool high) {
		if (now != seen && high)
			p.trigger(1e-3f);
		seen = now;
	}

	void process(const ProcessArgs& args) override {
		bool link;
		const SpeakLink& m = receiveLeft(this, link);
		auto high = [&](int o) { return outputs[o].getVoltage() > 5.f; };
		edge(m.startSerial, start, lineGap, high(LINE_OUTPUT));
		edge(m.wordSerial, word, wordGap, high(WORD_OUTPUT));
		edge(m.phoneSerial, phone, phoneGap, high(PHONE_OUTPUT));
		if (m.endSerial != end)
			end = m.endSerial, eol.trigger(1e-3f);
		float dt = args.sampleTime;
		bool lineOpen = !lineGap.process(dt), wordOpen = !wordGap.process(dt), phoneOpen = !phoneGap.process(dt);
		bool speaking = link && m.speaking, phoned = speaking && m.phone >= 0;
		auto gate = [](bool b) { return b ? 10.f : 0.f; };

		outputs[LINE_OUTPUT].setVoltage(gate(speaking && lineOpen));
		if (speaking)
			outputs[LINE_INDEX_OUTPUT].setVoltage(index(m.line, m.lineCount));
		outputs[WORD_OUTPUT].setVoltage(gate(speaking && m.wordIndex >= 0 && wordOpen));
		if (speaking && m.wordIndex >= 0)
			outputs[WORD_INDEX_OUTPUT].setVoltage(index(m.wordIndex, m.wordCount));
		outputs[PHONE_OUTPUT].setChannels(2);
		outputs[PHONE_OUTPUT].setVoltage(gate(phoned && phoneOpen), 0);
		outputs[PHONE_OUTPUT].setVoltage(gate(phoned && phoneOpen && m.vowel), 1);
		outputs[PHONE_INDEX_OUTPUT].setChannels(2);
		if (phoned) {
			outputs[PHONE_INDEX_OUTPUT].setVoltage(index(m.phoneIndex, m.phoneCount), 0);
			outputs[PHONE_INDEX_OUTPUT].setVoltage(m.phone * 0.1f, 1);
		}
		outputs[EOL_OUTPUT].setVoltage(gate(link && eol.process(dt)));
		outputs[ENV_OUTPUT].setVoltage(link ? std::min(10.f, m.level * 11.f) : 0.f);
		lights[LINK_LIGHT].setBrightness(link);

		SpeakLink out = m;
		out.valid = link;
		sendRight(this, out);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "indexScale", json_integer(scale));
		return root;
	}
	void dataFromJson(json_t* root) override {
		scale = clamp(int(json_integer_value(json_object_get(root, "indexScale"))), 0, NUM_SCALES - 1);
	}
	void onReset() override { scale = SCALE_TENTH; }
};

struct SpeakOutsWidget : ModuleWidget {
	SpeakOutsWidget(SpeakOuts* module) {
		const layout::OutsLayout& L = layout::OUTS;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SpeakOuts.svg"), asset::plugin(pluginInstance, "res/SpeakOuts-dark.svg")));
		for (float y : {0.f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH})
			addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, y)));
		for (int i = 0; i < SpeakOuts::NUM_OUTPUTS; i++)
			addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(Vec(L.jacks[i][0], L.jacks[i][1])), module, i));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(L.link[0], L.link[1])), module, SpeakOuts::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		SpeakOuts* m = getModule<SpeakOuts>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Index output scale",
			{"0.1 V per step", "1 V per step", "1/12 V per step (semitones)", "0-10 V spans the count"}, &m->scale));
	}
};

Model* modelSpeakOuts = createModel<SpeakOuts, SpeakOutsWidget>("SpeakOuts");
