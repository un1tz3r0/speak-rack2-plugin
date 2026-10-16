#include "Speak.hpp"
#include "Face.hpp"
#include "layout.hpp"

static constexpr int NUM_FIXED_FACES = 64;

/** Expander: sits in the chain right of Speak / Speak Compact and animates a face for the current voice. */
struct SpeakFace : Module {
	enum LightId { LINK_LIGHT, NUM_LIGHTS };

	SpeakLink messages[2];
	std::atomic<int> fixedFace{0}; // 0: the face that belongs to the voice
	// Published for the display (UI thread).
	std::atomic<bool> linked{false}, speaking{false};
	std::atomic<int> mouth{face::REST}, voiceKey{-1};
	std::atomic<uint32_t> wordSerial{0};
	std::atomic<float> level{0.f};
	std::mutex captionMutex;
	std::string caption;

	face::Viseme held = face::REST;
	float holdLeft = 0.f;
	uint32_t captionSerial = ~0u;

	SpeakFace() {
		config(0, 0, 0, NUM_LIGHTS);
		configLight(LINK_LIGHT, "Linked to vcvspeak on the left");
		leftExpander.producerMessage = &messages[0];
		leftExpander.consumerMessage = &messages[1];
	}

	void process(const ProcessArgs& args) override {
		bool link;
		const SpeakLink& msg = receiveLeft(this, link);
		face::Viseme v = link && msg.speaking && msg.phone >= 0 ? face::visemeFor(msg.phone, msg.vowel) : face::REST;
		// Hold each mouth shape for 50 ms so short phonemes survive the display's frame rate.
		if ((holdLeft -= args.sampleTime) <= 0.f && v != held)
			held = v, holdLeft = 0.05f;
		linked = link;
		speaking = link && msg.speaking;
		mouth = held;
		voiceKey = link ? msg.engine << 8 | msg.voice : -1;
		wordSerial = msg.wordSerial;
		level = link ? msg.level : 0.f;
		if (msg.wordSerial != captionSerial || !msg.speaking) { // rare: once per word
			captionSerial = msg.wordSerial;
			std::unique_lock<std::mutex> lock(captionMutex, std::try_to_lock);
			if (lock)
				caption = link && msg.speaking ? msg.word : "";
		}
		lights[LINK_LIGHT].setBrightness(link);
		SpeakLink out = msg;
		out.valid = link;
		sendRight(this, out);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "fixedFace", json_integer(fixedFace));
		return root;
	}
	void dataFromJson(json_t* root) override {
		fixedFace = clamp(int(json_integer_value(json_object_get(root, "fixedFace"))), 0, NUM_FIXED_FACES);
	}
	void onReset() override { fixedFace = 0; }
};

struct FaceDisplay : widget::OpaqueWidget {
	SpeakFace* module = nullptr;
	face::Pixels pixels;
	face::Spec spec;
	std::string name;
	int cachedKey = -2;
	double blinkAt = 2.0, blinkUntil = 0.0, glanceAt = 1.0, glanceUntil = 0.0, browUntil = 0.0;
	int glance = 0;
	uint32_t lastWord = 0;

	/** Picks the face: a fixed one from the knob, or the one belonging to the voice being spoken. */
	void choose(int key) {
		if (key == cachedKey)
			return;
		cachedKey = key;
		if (key >= 0x10000) {
			spec = face::specForSeed(key - 0x10000);
			name = "#" + std::to_string(key - 0x10000);
			return;
		}
		int engine = key < 0 ? ESPEAK : key >> 8, voice = key < 0 ? 2 : key & 0xFF;
		const Catalog& cat = Catalog::get();
		std::string v = cat.voiceName(engine, voice);
		std::string lower = string::lowercase(v);
		bool robot = lower.find("robo") != std::string::npos;
		spec = face::specFor(std::to_string(engine) + ":" + v, cat.voiceGender(engine, voice), robot);
		name = key < 0 ? "" : v.substr(0, v.find(' '));
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, mm2px(1.4f));
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x10));
		nvgFill(vg);
		float in = mm2px(0.7f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, in, in, box.size.x - 2 * in, box.size.y - 2 * in, mm2px(0.8f));
		nvgFillColor(vg, nvgRGB(0x03, 0x04, 0x05));
		nvgFill(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		double t = system::getTime();
		bool linked = !module || module->linked;
		int fixed = module ? module->fixedFace.load() : 0;
		choose(fixed ? 0x10000 + fixed : module ? (linked ? module->voiceKey.load() : -1) : 2);

		face::Anim a;
		a.time = t;
		a.asleep = module && !linked && !fixed;
		bool talking = module ? module->speaking.load() : true;
		if (module)
			a.mouth = face::Viseme(module->mouth.load()), a.level = module->level;
		else // library preview: babble
			a.mouth = face::Viseme(int(t * 7.0) * 5 % face::NUM_VISEMES);
		if (t >= blinkAt)
			blinkUntil = t + 0.13, blinkAt = t + 2.0 + random::uniform() * 4.0;
		a.blink = t < blinkUntil;
		if (t >= glanceAt) {
			glance = talking ? 0 : int(random::u32() % 3) - 1;
			glanceUntil = t + 0.5 + random::uniform(), glanceAt = glanceUntil + 1.0 + random::uniform() * 3.0;
		}
		a.glance = t < glanceUntil && !talking ? glance : 0;
		uint32_t word = module ? module->wordSerial.load() : 0;
		if (word != lastWord) {
			lastWord = word;
			if (random::u32() % 3)
				browUntil = t + 0.18;
		}
		a.brow = t < browUntil;

		face::render(spec, a, pixels);
		face::Palette pal = face::palette(spec);
		float pad = mm2px(1.2f), side = box.size.x - 2 * pad, cell = side / face::N;
		for (int s = 0; s < face::NUM_SLOTS; s++) {
			nvgBeginPath(vg);
			bool any = false;
			for (int y = 0; y < face::N; y++)
				for (int x = 0; x < face::N; x++)
					if (pixels[y][x] == s) {
						nvgRect(vg, pad + x * cell + cell * 0.06f, pad + y * cell + cell * 0.06f, cell * 0.88f, cell * 0.88f);
						any = true;
					}
			if (!any)
				continue;
			uint32_t c = pal[s];
			nvgFillColor(vg, nvgRGB(c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF));
			nvgFill(vg);
		}

		std::string label = a.asleep ? "zzz" : name;
		if (module && talking) {
			std::lock_guard<std::mutex> lock(module->captionMutex);
			if (!module->caption.empty())
				label = module->caption;
		}
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && !label.empty()) {
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, mm2px(3.4f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBAf(0.88f, 0.96f, 1.f, 0.75f));
			nvgText(vg, box.size.x / 2, (pad + side + box.size.y) / 2, label.c_str(), nullptr);
		}
	}
};

struct SpeakFaceWidget : ModuleWidget {
	SpeakFaceWidget(SpeakFace* module) {
		const layout::FaceLayout& L = layout::FACE;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SpeakFace.svg"), asset::plugin(pluginInstance, "res/SpeakFace-dark.svg")));
		for (float y : {0.f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH})
			addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, y)));
		auto* display = new FaceDisplay;
		display->module = module;
		display->box = Rect(mm2px(Vec(L.display[0], L.display[1])), mm2px(Vec(L.display[2], L.display[3])));
		addChild(display);
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(L.link[0], L.link[1])), module, SpeakFace::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		SpeakFace* m = getModule<SpeakFace>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Face follows the voice", "", [=] { return m->fixedFace == 0; }, [=] { m->fixedFace = 0; }));
		menu->addChild(createSubmenuItem("Fixed face", m->fixedFace ? "#" + std::to_string(m->fixedFace) : "", [=](Menu* menu) {
			for (int group = 0; group < NUM_FIXED_FACES; group += 16)
				menu->addChild(createSubmenuItem(string::f("#%d to #%d", group + 1, group + 16), "", [=](Menu* menu) {
					for (int i = group + 1; i <= group + 16; i++) {
						face::Spec s = face::specForSeed(i);
						std::string kind = s.kind == face::ROBOT ? "robot" : s.kind == face::FEMALE ? "woman" : "man";
						menu->addChild(createCheckMenuItem("#" + std::to_string(i), kind, [=] { return m->fixedFace == i; }, [=] { m->fixedFace = i; }));
					}
				}));
		}));
	}
};

Model* modelSpeakFace = createModel<SpeakFace, SpeakFaceWidget>("SpeakFace");
