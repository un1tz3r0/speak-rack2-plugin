#include "Speak.hpp"
#include "Widgets.hpp"
#include "layout.hpp"

static const char* const DEFAULT_TEXT = "hello world\nthis is v c v speak\none two three four\nthe quick brown fox\njumps over the lazy dog";

bool isSpeakModel(Model* m) {
	return m && (m == modelSpeak || m == modelSpeakCompact);
}
bool isSpeakExpander(Model* m) {
	return m && (m == modelSpeakFace || m == modelSpeakOuts);
}

/** UTF-8 of text[start, start + len) into a fixed buffer, whole codepoints only. */
static void copyWord(char (&out)[32], const std::u32string& text, int start, int len) {
	out[0] = 0;
	if (start < 0 || start >= int(text.size()))
		return;
	std::string s = toUtf8(text.substr(start, len));
	size_t n = std::min(s.size(), sizeof out - 1);
	while (n > 0 && n < s.size() && (s[n] & 0xC0) == 0x80)
		n--;
	std::memcpy(out, s.data(), n);
	out[n] = 0;
}

struct VoiceQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		Speak* m = dynamic_cast<Speak*>(module);
		if (!m)
			return ParamQuantity::getDisplayValueString();
		SynthParams p = m->synthParams();
		return Catalog::get().voiceName(p.engine, p.voice);
	}
};

Speak::Speak(bool compact) : compact(compact) {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	configSwitch(ENGINE_PARAM, 0.f, 1.f, 0.f, "Engine", {"eSpeak NG", "Flite"});
	configParam<VoiceQuantity>(VOICE_PARAM, 0.f, 1.f, 0.f, "Voice");
	configParam(RANGE_PARAM, 0.f, 100.f, 50.f, "Intonation range");
	configParam(FORMANT_PARAM, -12.f, 12.f, 0.f, "Formant shift", " semitones");
	if (compact) {
		configParam(PITCH_PARAM, -24.f, 24.f, 0.f, "Pitch", " semitones");
		configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed", "x", 2.f);
		for (int id : {RATE_PARAM, SHIFT_PARAM})
			configParam(id, 0.f, 1.f, 0.f, "Unused")->randomizeEnabled = false;
	}
	else {
		configParam(PITCH_PARAM, 0.f, 100.f, 50.f, "Engine pitch");
		configParam(RATE_PARAM, 80.f, 450.f, 175.f, "Rate", " wpm");
		configParam(SHIFT_PARAM, -24.f, 24.f, 0.f, "Pitch shift", " semitones");
		configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed", "x", 2.f);
	}
	configButton(LINE_BUTTON_PARAM, "Trigger LINE");
	configButton(WORD_BUTTON_PARAM, "Trigger WORD");

	configInput(ENGINE_INPUT, "Engine gate (high flips the switch)");
	configInput(VOICE_INPUT, compact ? "Voice CV: 0-10 V spans every voice of both engines (overrides ENGINE and VOICE)"
	                                 : "Voice CV (10 V spans the engine's voices)");
	configInput(RANGE_INPUT, "Intonation range CV (1 V = 10)");
	configInput(FORMANT_INPUT, "Formant shift 1V/oct");
	configInput(SPEED_INPUT, "Speed 1V/oct");
	if (compact)
		configInput(PITCH_INPUT, "Pitch 1V/oct");
	else {
		configInput(PITCH_INPUT, "Engine pitch CV (1 V = 10)");
		configInput(RATE_INPUT, "Rate CV (1 V = 37 wpm)");
		configInput(SHIFT_INPUT, "Pitch shift 1V/oct");
	}
	configInput(LINE_INPUT, "Line: triggers step to the next line, a steady CV selects one");
	configInput(WORD_INPUT, "Word: triggers step to the next word, a steady CV selects one (within LINE's line if patched)");
	configInput(RESET_INPUT, "Reset: back to the first line and word");
	configOutput(AUDIO_OUTPUT, "Speech");
	configLight(LINE_TRIG_LIGHT, "LINE reads triggers");
	configLight(LINE_INDEX_LIGHT, "LINE reads an index CV");
	configLight(WORD_TRIG_LIGHT, "WORD reads triggers");
	configLight(WORD_INDEX_LIGHT, "WORD reads an index CV");

	paramDivider.setDivision(64);
	setLanguage(language);
	ch->edit([](Doc& d) { d.setText(DEFAULT_TEXT); });
	Service::get().attach(ch);
}

void Speak::onReset() {
	Module::onReset();
	ch->edit([](Doc& d) { d.setText(DEFAULT_TEXT); });
	setLanguage("en-us");
	indexRange = SPAN_0_10;
	lineMode = wordMode = InputClassifier::AUTO;
	lineStep = wordStep = line = 0;
}

void Speak::setLanguage(const std::string& code) {
	int i = Catalog::get().languageIndex(code);
	if (i < 0)
		return;
	language = code;
	languageIndex = i;
}

float Speak::knob(int id, float cvScale, float lo, float hi) {
	return clamp(params[id].getValue() + inputs[id].getVoltage() * cvScale, lo, hi);
}

float Speak::targetSemis() {
	return knob(PITCH_PARAM, 12.f, -36.f, 36.f);
}

float Speak::targetOctaves() {
	return knob(SPEED_PARAM, 1.f, -4.f, 3.f);
}

int Speak::voiceIndex(int engine) {
	int n = int(Catalog::get().voices(engine).size());
	float v = compact ? params[VOICE_PARAM].getValue() : knob(VOICE_PARAM, 0.1f, 0.f, 1.f);
	return n ? clamp(int(std::round(v * (n - 1))), 0, n - 1) : 0;
}

SynthParams Speak::synthParams() {
	SynthParams p;
	const Catalog& cat = Catalog::get();
	if (compact && inputs[VOICE_INPUT].isConnected()) { // one CV over eSpeak's voices then Flite's, overriding both controls
		int ne = int(cat.voices(ESPEAK).size()), nf = int(cat.voices(FLITE).size());
		int i = clamp(int(std::round(clamp(inputs[VOICE_INPUT].getVoltage() / 10.f, 0.f, 1.f) * (ne + nf - 1))), 0, ne + nf - 1);
		p.engine = uint8_t(i < ne ? ESPEAK : FLITE);
		p.voice = uint8_t(i < ne ? i : i - ne);
	}
	else {
		p.engine = uint8_t((params[ENGINE_PARAM].getValue() > 0.5f) != (inputs[ENGINE_INPUT].getVoltage() > 1.f));
		p.voice = uint8_t(voiceIndex(p.engine));
	}
	p.range = uint8_t(std::round(knob(RANGE_PARAM, 10.f, 0.f, 100.f)));
	p.lang = uint16_t(languageIndex);
	if (compact) {
		float semis = clamp(std::round(targetSemis() / 2.f) * 2.f, tune::pitchMin(p.engine), tune::pitchMax(p.engine));
		p.pitch = uint8_t(tune::pitchParam(p.engine, semis));
		p.rate = uint16_t(tune::rateParam(p.engine, std::exp2(std::round(targetOctaves() * 4.f) / 4.f)));
	}
	else {
		p.pitch = uint8_t(std::round(knob(PITCH_PARAM, 10.f, 0.f, 100.f)));
		p.rate = uint16_t(std::round(knob(RATE_PARAM, 37.f, 80.f, 450.f) / 5.f) * 5);
	}
	return p;
}

std::string Speak::readout(int param) {
	switch (param) {
		case PITCH_PARAM: return string::f("%+.1f", targetSemis());
		case SPEED_PARAM: return string::f("%.2fx", std::exp2(targetOctaves()));
		case RANGE_PARAM: return string::f("%d", int(std::round(knob(RANGE_PARAM, 10.f, 0.f, 100.f))));
		case FORMANT_PARAM: return string::f("%+.1f", clamp(params[FORMANT_PARAM].getValue() + inputs[FORMANT_INPUT].getVoltage() * 12.f, -24.f, 24.f));
		case VOICE_PARAM: {
			SynthParams p = synthParams();
			std::string name = Catalog::get().voiceName(p.engine, p.voice);
			return name.substr(0, name.find(' '));
		}
		default: return "";
	}
}

/** LINE and WORD jacks to requests. Steps run from the start (RESET) and wrap; selections speak when they change. */
void Speak::sequence(float dt) {
	bool lineOn = inputs[LINE_INPUT].isConnected(), wordOn = inputs[WORD_INPUT].isConnected();
	int lines = std::max(1, ch->lineCount.load());
	if (reset.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
		lineStep = wordStep = line = 0;

	int index = 0;
	SeqInput::Event e = lineIn.process(inputs[LINE_INPUT], dt, lineMode, indexRange, lines, index);
	if (lineButton.process(params[LINE_BUTTON_PARAM].getValue() > 0.f))
		e = SeqInput::STEP; // the button is a trigger into the jack
	if (e != SeqInput::NONE) {
		line = e == SeqInput::STEP ? lineStep % lines : index;
		if (e == SeqInput::STEP)
			lineStep = (line + 1) % lines;
		wordStep = 0;
		if (!wordOn)
			fire(line, -1);
	}
	line = std::min(line, lines - 1);

	int n = !lineOn ? ch->tokenCount.load() : line < Channel::MAX_LINES ? int(ch->lineTokens[line]) : 0;
	e = wordIn.process(inputs[WORD_INPUT], dt, wordMode, indexRange, n, index);
	if (wordButton.process(params[WORD_BUTTON_PARAM].getValue() > 0.f))
		e = SeqInput::STEP;
	if (e == SeqInput::STEP)
		wordDelay = lineOn ? 0.002f : 0.f; // a LINE edge on the same beat lands first
	else if (e == SeqInput::SELECT)
		fire(lineOn ? line : -1, index);
	if (wordDelay >= 0.f && (wordDelay -= dt) < 0.f && n > 0) {
		int k = wordStep % n;
		wordStep = (k + 1) % n;
		fire(lineOn ? line : -1, k);
	}

	int nextLine = -1, nextWord = -1;
	if (wordOn && wordIn.trig() && n > 0)
		nextLine = lineOn ? line : -1, nextWord = wordStep % n;
	else if (lineOn && lineIn.trig())
		nextLine = lineStep % lines;
	ch->nextLine = nextLine, ch->nextWord = nextWord;
	ch->wordMode = wordOn;

	if (playRequest.exchange(false)) { // Ctrl+Enter on the display
		if (wordOn)
			fire(-1, ch->cursorToken);
		else
			fire(ch->cursorRow, -1);
	}
	lights[LINE_TRIG_LIGHT].setBrightness(lineOn && lineIn.trig());
	lights[LINE_INDEX_LIGHT].setBrightness(lineOn && lineIn.level());
	lights[WORD_TRIG_LIGHT].setBrightness(wordOn && wordIn.trig());
	lights[WORD_INDEX_LIGHT].setBrightness(wordOn && wordIn.level());
}

void Speak::fire(int l, int w) {
	if (l < 0 && w < 0)
		return;
	waiting = ch->waiting = true;
	ch->requests.push({l, w, compact, synthParams().pack(), ++seq});
	Service::get().wake();
}

void Speak::dispose(ClipPtr clip) {
	if (clip)
		ch->graveyard.push(std::move(clip));
}

void Speak::begin(Channel::Result& r) {
	if (voice.loaded()) {
		dispose(fading.release());
		std::swap(voice, fading);
		fadeGain = 1.f;
	}
	lastWord = lastPhone = -1;
	playing = {SynthParams::unpack(r.params), r.line, r.charOffset, r.wordBase, r.wordTotal};
	ch->playingLine = r.clip ? r.line : -1;
	ch->wordStart = -1;
	link.word[0] = 0;
	if (r.clip) {
		link.startSerial++;
		voice.start(std::move(r.clip));
	}
}

/** Follows words and phonemes of the playing clip for the display and the expander chain. */
void Speak::track(bool speaking) {
	const Clip* c = voice.current();
	const Span* w = speaking ? voice.currentWord() : nullptr;
	const Span* ph = speaking ? voice.currentPhone() : nullptr;
	if (w && w->index != lastWord) {
		lastWord = w->index;
		link.wordSerial++;
		ch->wordStart = w->charStart >= 0 ? playing.charOffset + w->charStart : -1;
		ch->wordLen = w->charLen;
		copyWord(link.word, c->text, w->charStart, w->charLen);
	}
	if (!w)
		ch->wordStart = -1;
	if (ph && ph->index != lastPhone) {
		lastPhone = ph->index;
		link.phoneSerial++;
	}
	if (wasSpeaking && !speaking)
		link.endSerial++;
	wasSpeaking = speaking;

	SynthParams p = speaking ? playing.params : SynthParams::unpack(ch->liveParams.load());
	bool words = playing.wordTotal > 0;
	link.valid = true, link.speaking = speaking;
	link.phone = ph ? ph->id : -1, link.vowel = ph && ph->vowel;
	link.phoneIndex = ph ? ph->index : -1, link.phoneCount = int16_t(c ? c->phones.size() : 0);
	link.wordIndex = int16_t(!speaking ? -1 : words ? playing.wordBase : w ? w->index : -1);
	link.wordCount = int16_t(words ? playing.wordTotal : c ? c->words.size() : 0);
	link.line = int16_t(speaking ? playing.line : -1), link.lineCount = int16_t(ch->lineCount.load());
	link.engine = p.engine, link.voice = p.voice, link.level = envelope;
	if (!speaking)
		link.word[0] = 0;
	sendRight(this, link);
}

void Speak::process(const ProcessArgs& args) {
	if (paramDivider.process())
		ch->liveParams = synthParams().pack();
	sequence(args.sampleTime);

	Channel::Result result;
	while (ch->results.pop(result)) {
		if (waiting && result.seq == seq) {
			waiting = ch->waiting = false;
			begin(result);
		}
		else
			dispose(std::move(result.clip));
	}

	float formant = std::exp2(clamp(params[FORMANT_PARAM].getValue() / 12.f + inputs[FORMANT_INPUT].getVoltage(), -2.f, 2.f));
	float pitch, speed;
	if (compact) { // render at the snapped value, PSOLA the rest of the way to the knob
		const SynthParams& p = playing.params;
		pitch = std::exp2(clamp((targetSemis() - tune::pitchSemis(p.engine, p.pitch)) / 12.f, -3.f, 3.f));
		speed = std::exp2(targetOctaves()) / tune::tempo(p.engine, p.rate);
	}
	else {
		pitch = std::exp2(clamp(params[SHIFT_PARAM].getValue() / 12.f + inputs[SHIFT_INPUT].getVoltage(), -3.f, 3.f));
		speed = std::exp2(clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage(), -4.f, 3.f));
	}

	float y = voice.process(args.sampleRate, pitch, formant, speed);
	if (fading.loaded()) {
		y += fadeGain * fading.process(args.sampleRate, pitch, formant, speed);
		if ((fadeGain -= args.sampleTime / 0.008f) <= 0.f)
			dispose(fading.release());
	}
	outputs[AUDIO_OUTPUT].setVoltage(5.f * y);
	float mag = std::fabs(y);
	envelope += (mag - envelope) * (1.f - std::exp(-args.sampleTime / (mag > envelope ? 0.003f : 0.08f)));

	bool speaking = voice.playing();
	track(speaking);
	if (voice.loaded() && !voice.sounding()) {
		dispose(voice.release());
		ch->playingLine = -1;
	}
}

json_t* Speak::dataToJson() {
	json_t* root = json_object();
	json_t* lines = json_array();
	ch->edit([&](Doc& d) {
		for (const auto& l : d.lines)
			json_array_append_new(lines, json_string(toUtf8(l).c_str()));
		json_object_set_new(root, "row", json_integer(d.row));
		json_object_set_new(root, "col", json_integer(d.col));
	});
	json_object_set_new(root, "lines", lines);
	json_object_set_new(root, "language", json_string(language.c_str()));
	json_object_set_new(root, "indexRange", json_integer(indexRange));
	json_object_set_new(root, "lineMode", json_integer(lineMode));
	json_object_set_new(root, "wordMode", json_integer(wordMode));
	return root;
}

void Speak::dataFromJson(json_t* root) {
	if (json_t* lines = json_object_get(root, "lines")) {
		ch->edit([&](Doc& d) {
			d.lines.clear();
			size_t i;
			json_t* l;
			json_array_foreach(lines, i, l) d.lines.push_back(toUtf32(json_string_value(l) ? json_string_value(l) : ""));
			if (d.lines.empty())
				d.lines.push_back(U"");
			d.row = int(json_integer_value(json_object_get(root, "row")));
			d.col = int(json_integer_value(json_object_get(root, "col")));
		});
	}
	if (json_t* l = json_object_get(root, "language"))
		setLanguage(json_string_value(l));
	if (json_t* r = json_object_get(root, "indexRange"))
		indexRange = clamp(int(json_integer_value(r)), 0, NUM_INDEX_RANGES - 1);
	for (auto kv : {std::make_pair("lineMode", &lineMode), std::make_pair("wordMode", &wordMode)})
		if (json_t* m = json_object_get(root, kv.first))
			*kv.second = clamp(int(json_integer_value(m)), 0, InputClassifier::NUM_MODES - 1);
}

// ---------------------------------------------------------------------------------------------
// Panel

/** Pastel-capped knob: a fixed dark body (res/knob-bg.svg) under a rotating cap (res/knob-<colour>.svg). */
template <int COLOR>
struct PastelKnob : app::SvgKnob {
	PastelKnob() {
		static const char* const NAMES[] = {"", "teal", "cyan", "amber", "purple", "pink"};
		minAngle = -0.83f * float(M_PI);
		maxAngle = 0.83f * float(M_PI);
		auto* bg = new widget::SvgWidget;
		bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/knob-bg.svg")));
		fb->addChildBelow(bg, tw);
		setSvg(Svg::load(asset::plugin(pluginInstance, std::string("res/knob-") + NAMES[COLOR] + ".svg")));
	}
};

struct KeyDef {
	const char *lo, *hi;
	Icon icon = Icon::NONE;
	int span = 1;
};

static const std::vector<std::vector<KeyDef>> KEYBOARD = {
	{{"1", "!"}, {"2", "@"}, {"3", "#"}, {"4", "$"}, {"5", "%"}, {"6", "^"}, {"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"}, {"'", "\""}, {"", "", Icon::BACKSPACE}},
	{{"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"}, {"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"}, {"o", "O"}, {"p", "P"}, {"-", "_"}, {"?", "/"}},
	{{"a", "A"}, {"s", "S"}, {"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"}, {"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"}, {"", "", Icon::ENTER, 2}},
	{{"", "", Icon::SHIFT}, {"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"}, {"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"}, {".", ">"}, {"", "", Icon::SPACE, 2}},
};

struct SpeakWidget : ModuleWidget {
	OledDisplay* display;
	int shift = 0; // 0 off, 1 next key, 2 locked

	KeyButton* key(Rect r, std::function<void()> action) {
		auto* k = new KeyButton;
		k->box = r;
		k->onPress = std::move(action);
		k->focus = display;
		addChild(k);
		return k;
	}

	static ParamWidget* knob(int color, Vec pos, Module* m, int id) {
		switch (color) {
			case 1: return createParamCentered<PastelKnob<1>>(pos, m, id);
			case 2: return createParamCentered<PastelKnob<2>>(pos, m, id);
			case 3: return createParamCentered<PastelKnob<3>>(pos, m, id);
			case 4: return createParamCentered<PastelKnob<4>>(pos, m, id);
			case 5: return createParamCentered<PastelKnob<5>>(pos, m, id);
			default: return createParamCentered<RoundBlackKnob>(pos, m, id);
		}
	}

	SpeakWidget(Speak* module, const layout::SpeakLayout& L, const std::string& slug) {
		auto mm = [](const float (&p)[2]) { return mm2px(Vec(p[0], p[1])); };
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/" + slug + ".svg"), asset::plugin(pluginInstance, "res/" + slug + "-dark.svg")));
		for (float x : {RACK_GRID_WIDTH, box.size.x - 2 * RACK_GRID_WIDTH})
			for (float y : {0.f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH})
				addChild(createWidget<ThemedScrew>(Vec(x, y)));

		display = new OledDisplay;
		display->module = module;
		display->typeHint = !L.keyboard;
		display->rows = L.rows;
		display->layout = L.values ? &L : nullptr;
		display->box = Rect(mm2px(Vec(L.display[0], L.display[1])), mm2px(Vec(L.display[2], L.display[3])));
		addChild(display);

		auto edit = [this](auto f) { return [this, f] { display->edit(f); }; };
		for (int which = 0; which < 2 && L.encRow[0] > 0; which++) {
			auto* enc = new Encoder;
			enc->box.pos = mm(which ? L.encCol : L.encRow).minus(enc->box.size.div(2));
			enc->focus = display;
			enc->onStep = [this, which](int n) { display->edit([&](Doc& d) { which ? d.moveCol(n) : d.moveRow(n); }); };
			addChild(enc);
		}

		struct Op {
			const char* label;
			Icon icon;
			std::function<void()> action;
		} ops[] = {
			{"INS", Icon::NONE, edit([](Doc& d) { d.insertLine(); })},
			{"DEL", Icon::NONE, edit([](Doc& d) { d.deleteLine(); })},
			{"DUP", Icon::NONE, edit([](Doc& d) { d.duplicateLine(); })},
			{"", Icon::UP, edit([](Doc& d) { d.moveLine(-1); })},
			{"", Icon::DOWN, edit([](Doc& d) { d.moveLine(1); })},
		};
		const float *o = L.ops, *kb = L.kb;
		for (int i = 0; i < 5 && o[0] >= 0; i++) {
			KeyButton* k = key(Rect(mm2px(Vec(o[0] + i * (o[2] + o[4]), o[1] - o[3] / 2)), mm2px(Vec(o[2], o[3]))), ops[i].action);
			k->label = ops[i].label;
			k->icon = ops[i].icon;
		}

		for (int row = 0; L.keyboard && row < int(KEYBOARD.size()); row++) {
			int unit = 0;
			for (const KeyDef& def : KEYBOARD[row]) {
				float w = kb[4] + (def.span - 1) * kb[2];
				Vec pos = mm2px(Vec(kb[0] + unit * kb[2] - kb[4] / 2, kb[1] + row * kb[3] - kb[5] / 2));
				unit += def.span;
				std::function<void()> action;
				switch (def.icon) {
					case Icon::BACKSPACE: action = edit([](Doc& d) { d.backspace(); }); break;
					case Icon::ENTER: action = edit([](Doc& d) { d.newline(); }); break;
					case Icon::SPACE: action = edit([](Doc& d) { d.insert(U" "); }); break;
					case Icon::SHIFT: action = [this] { shift = (shift + 1) % 3; }; break;
					default:
						action = [this, def] {
							display->edit([&](Doc& d) { d.insert(toUtf32(shift ? def.hi : def.lo)); });
							if (shift == 1)
								shift = 0;
						};
				}
				KeyButton* k = key(Rect(pos, mm2px(Vec(w, kb[5]))), action);
				k->icon = def.icon;
				if (def.icon == Icon::SHIFT)
					k->lit = [this] { return shift > 0; };
				else if (def.icon == Icon::NONE)
					k->dynamicLabel = [this, def] { return std::string(shift ? def.hi : def.lo); };
			}
		}

		addParam(createParamCentered<CKSS>(mm(L.knob[0]), module, Speak::ENGINE_PARAM));
		for (int i = 0; i < 8; i++) {
			if (i && L.knob[i][0] > 0)
				addParam(knob(L.knobColor[i], mm(L.knob[i]), module, i));
			if (L.cv[i][0] > 0)
				addInput(createInputCentered<ThemedPJ301MPort>(mm(L.cv[i]), module, i));
		}
		addParam(createParamCentered<TL1105>(mm(L.lineButton), module, Speak::LINE_BUTTON_PARAM));
		addParam(createParamCentered<TL1105>(mm(L.wordButton), module, Speak::WORD_BUTTON_PARAM));
		addInput(createInputCentered<ThemedPJ301MPort>(mm(L.line), module, Speak::LINE_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm(L.word), module, Speak::WORD_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm(L.reset), module, Speak::RESET_INPUT));
		for (int k = 0; k < 2; k++) {
			for (auto lw : {std::make_pair(L.lineLights[k], Speak::LINE_TRIG_LIGHT), std::make_pair(L.wordLights[k], Speak::WORD_TRIG_LIGHT)}) {
				Vec pos = mm2px(Vec(lw.first[0], lw.first[1]));
				if (L.smallLights)
					addChild(createLightCentered<SmallLight<WhiteLight>>(pos, module, lw.second + k));
				else
					addChild(createLightCentered<TinyLight<WhiteLight>>(pos, module, lw.second + k));
			}
		}
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm(L.out), module, Speak::AUDIO_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Speak* m = getModule<Speak>();
		const Catalog& cat = Catalog::get();
		menu->addChild(new MenuSeparator);
		int li = cat.languageIndex(m->language);
		menu->addChild(createSubmenuItem("eSpeak language", li >= 0 ? cat.languages[li].name : m->language, [=](Menu* menu) {
			std::map<char, std::vector<const Language*>> groups;
			for (const Language& l : cat.languages)
				groups[std::toupper((unsigned char)l.name[0])].push_back(&l);
			for (auto& g : groups) {
				std::vector<const Language*> langs = g.second;
				menu->addChild(createSubmenuItem(std::string(1, g.first), "", [=](Menu* menu) {
					for (const Language* l : langs)
						menu->addChild(createCheckMenuItem(l->name, l->code, [=] { return m->language == l->code; }, [=] { m->setLanguage(l->code); }));
				}));
			}
		}));
		menu->addChild(createIndexPtrSubmenuItem("LINE / WORD index range",
			std::vector<std::string>(INDEX_RANGE_NAMES, INDEX_RANGE_NAMES + NUM_INDEX_RANGES), &m->indexRange));
		std::vector<std::string> reads = {"Auto: short pulses step, steady CV selects", "Triggers only", "Index CV only"};
		menu->addChild(createIndexPtrSubmenuItem("LINE jack reads", reads, &m->lineMode));
		menu->addChild(createIndexPtrSubmenuItem("WORD jack reads", reads, &m->wordMode));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Copy all lines", "", [=] {
			std::string text;
			m->ch->edit([&](Doc& d) {
				for (size_t i = 0; i < d.lines.size(); i++)
					text += (i ? "\n" : "") + toUtf8(d.lines[i]);
			});
			glfwSetClipboardString(APP->window->win, text.c_str());
		}));
		menu->addChild(createMenuItem("Replace all lines from clipboard", "", [=] {
			if (const char* text = glfwGetClipboardString(APP->window->win)) {
				std::string s = text;
				s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
				m->ch->edit([&](Doc& d) { d.setText(s); });
			}
		}));
		menu->addChild(createMenuItem("Clear all lines", "", [=] { m->ch->edit([](Doc& d) { d.setText(""); }); }));
		menu->addChild(createMenuLabel("Keys: Ctrl+Enter play, Alt+Up/Down move line, Ctrl+D duplicate, Shift+Del delete line"));
	}
};

struct SpeakFullWidget : SpeakWidget {
	SpeakFullWidget(Speak* m) : SpeakWidget(m, layout::SPEAK, "Speak") {}
};
struct SpeakCompactWidget : SpeakWidget {
	SpeakCompactWidget(Speak* m) : SpeakWidget(m, layout::SPEAK_COMPACT, "SpeakCompact") {}
};

Model* modelSpeak = createModel<SpeakFull, SpeakFullWidget>("Speak");
Model* modelSpeakCompact = createModel<SpeakCompact, SpeakCompactWidget>("SpeakCompact");
