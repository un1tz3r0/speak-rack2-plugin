#pragma once
#include "Speak.hpp"
#include "layout.hpp"

static NVGcolor oledInk(float alpha = 1.f) {
	return nvgRGBAf(0.88f, 0.96f, 1.f, alpha);
}

/** Widgets that shouldn't steal keyboard focus from the display hand it straight back. */
struct FocusForwarder {
	widget::Widget* focus = nullptr;
	void forward() {
		if (focus)
			APP->event->setSelectedWidget(focus);
	}
};

/** Four-line OLED text editor: numbered lines, cursor, live word highlight, keyboard entry. */
struct OledDisplay : widget::OpaqueWidget {
	Speak* module = nullptr;
	int rows = 4, top = 0, hscroll = 0, scrolledRow = 0;
	bool focused = false, typeHint = false; // typeHint: no panel keyboard, so show that the display takes keys
	// Metrics from the last draw, used to map clicks to characters.
	float rowH = 1.f, cw = 1.f, textX = 0.f, pad = mm2px(1.6f);
	const layout::SpeakLayout* layout = nullptr; // set: draw the knob readouts along the bottom
	int cols = 1;

	struct View {
		std::vector<std::u32string> lines;
		int row = 0, col = 0, count = 1, playing = -1, wordStart = -1, wordLen = 0;
		int nextLine = -1, nextStart = -1, nextLen = 0; // what the next trigger will speak
		int follow = -1;                                // line the view tracks instead of the cursor
		bool waiting = false;
	};

	template <typename F>
	void edit(F&& f) {
		if (module)
			module->ch->edit(std::forward<F>(f));
	}

	View view() {
		View v;
		if (!module) {
			v.lines = {U"hello world", U"this is v c v speak", U"one two three four", U"the quick brown fox"};
			v.count = 5, v.playing = 1, v.wordStart = 8, v.wordLen = 1;
			return v;
		}
		Channel& ch = *module->ch;
		{
			std::lock_guard<std::mutex> lock(ch.mutex);
			v.row = ch.doc.row, v.col = ch.doc.col, v.count = ch.doc.count();
			int token = ch.tokenOf(ch.nextLine, ch.nextWord);
			if (token >= 0)
				v.nextLine = ch.tokens[token].line, v.nextStart = ch.tokens[token].start, v.nextLen = ch.tokens[token].len;
			else if (ch.nextLine >= 0 && ch.nextWord < 0)
				v.nextLine = ch.nextLine;
			// Without keyboard focus the view follows what is speaking, or else what is queued next.
			int playing = ch.playingLine;
			v.follow = focused ? -1 : playing >= 0 ? playing : v.nextLine;
			int anchor = v.follow >= 0 ? std::min(v.follow, v.count - 1) : v.row;
			int reach = v.follow >= 0 ? std::min(std::max(anchor + 1, v.nextLine), anchor + rows - 1) : anchor; // keep what's queued in view too
			top = clamp(clamp(top, std::min(reach, v.count - 1) - rows + 1, anchor), 0, std::max(0, v.count - rows));
			for (int r = top; r < std::min(v.count, top + rows); r++)
				v.lines.push_back(ch.doc.lines[r]);
		}
		v.playing = ch.playingLine, v.wordStart = ch.wordStart, v.wordLen = ch.wordLen, v.waiting = ch.waiting;
		return v;
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float r = mm2px(1.4f), in = mm2px(0.7f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, r);
		nvgFillColor(vg, nvgRGB(0x0d, 0x0e, 0x10));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, in, in, box.size.x - 2 * in, box.size.y - 2 * in, r * 0.6f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, in, 0, box.size.y, nvgRGB(0x06, 0x07, 0x09), nvgRGB(0x02, 0x03, 0x04)));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 14));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
	}

	void glyph(NVGcontext* vg, char32_t c, float x, float y) {
		std::string s = toUtf8(std::u32string(1, c));
		nvgText(vg, x, y, s.c_str(), nullptr);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		NVGcontext* vg = args.vg;
		View v = view();
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(vg, font->handle);
		float strip = layout ? mm2px(4.4f) : 0.f, textBottom = box.size.y - pad - strip;
		rowH = (textBottom - pad) / rows;
		nvgFontSize(vg, rowH * 0.86f);
		float bounds[4];
		cw = nvgTextBounds(vg, 0, 0, "0000000000", nullptr, bounds) / 10.f;
		int digits = std::max(2, int(std::to_string(v.count).size()));
		textX = pad + (digits + 1.5f) * cw;
		cols = std::max(1, int((box.size.x - textX - pad - mm2px(1.2f)) / cw));
		// One row scrolls sideways: the cursor's, or while following, the tracked line (keeping its word in view).
		int a = v.col, b = v.col;
		scrolledRow = v.follow >= 0 ? v.follow : v.row;
		if (v.follow >= 0) {
			a = b = 0;
			if (v.follow == v.playing && v.wordStart >= 0)
				a = v.wordStart, b = v.wordStart + v.wordLen;
			else if (v.follow == v.nextLine && v.nextStart >= 0)
				a = v.nextStart, b = v.nextStart + v.nextLen;
		}
		int len = scrolledRow - top >= 0 && scrolledRow - top < int(v.lines.size()) ? int(v.lines[scrolledRow - top].size()) : 0;
		hscroll = clamp(clamp(hscroll, b - cols + 1, a), 0, std::max(0, len - cols + 1));
		bool blink = std::fmod(system::getTime(), 1.0) < 0.6;
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		for (int i = 0; i < int(v.lines.size()); i++) {
			int row = top + i;
			const std::u32string& text = v.lines[i];
			float y = pad + (i + 0.5f) * rowH;
			bool current = row == v.row;
			int off = row == scrolledRow ? hscroll : 0;
			if (current) {
				nvgBeginPath(vg);
				nvgRect(vg, pad * 0.5f, y - rowH / 2, box.size.x - pad, rowH);
				nvgFillColor(vg, oledInk(0.07f));
				nvgFill(vg);
			}
			std::string num = std::to_string(row + 1);
			nvgFillColor(vg, oledInk(current ? 0.7f : 0.3f));
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgText(vg, pad + digits * cw, y, num.c_str(), nullptr);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			if (row == v.playing) {
				float x = pad + (digits + 0.4f) * cw, h = rowH * 0.2f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, y - h), nvgLineTo(vg, x + h * 1.3f, y), nvgLineTo(vg, x, y + h), nvgClosePath(vg);
				nvgFillColor(vg, oledInk());
				nvgFill(vg);
			}
			if (row == v.nextLine && row != v.playing) { // hollow marker: the next line auto-increment will speak
				float x = pad + (digits + 0.4f) * cw, h = rowH * 0.2f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, y - h), nvgLineTo(vg, x + h * 1.3f, y), nvgLineTo(vg, x, y + h), nvgClosePath(vg);
				nvgStrokeColor(vg, oledInk(0.6f));
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
			if (row == v.nextLine && v.nextStart >= 0) { // and in word mode, the next word
				int a = std::max(0, v.nextStart - off), b = std::min(cols, v.nextStart + v.nextLen - off);
				if (b > a) {
					nvgBeginPath(vg);
					nvgRect(vg, textX + a * cw, y + rowH * 0.34f, (b - a) * cw, rowH * 0.06f);
					nvgFillColor(vg, oledInk(0.5f));
					nvgFill(vg);
				}
			}
			int hiA = -1, hiB = -1;
			if (row == v.playing && v.wordStart >= 0)
				hiA = v.wordStart - off, hiB = hiA + v.wordLen;
			for (int k = 0; k < cols && off + k < int(text.size()); k++) {
				float x = textX + k * cw;
				bool inverted = (k >= hiA && k < hiB) || (current && focused && blink && off + k == v.col);
				if (inverted) {
					nvgBeginPath(vg);
					nvgRect(vg, x, y - rowH * 0.42f, cw, rowH * 0.84f);
					nvgFillColor(vg, oledInk(0.92f));
					nvgFill(vg);
				}
				nvgFillColor(vg, inverted ? nvgRGB(0, 0, 0) : oledInk(current ? 1.f : 0.62f));
				glyph(vg, text[off + k], x, y);
			}
			if (off + cols < int(text.size()) || off > 0) {
				nvgFillColor(vg, oledInk(0.5f));
				for (int side = 0; side < 2; side++) {
					if (side == 0 ? off == 0 : off + cols >= int(text.size()))
						continue;
					float x = side == 0 ? textX - cw * 0.35f : textX + cols * cw + cw * 0.15f, h = rowH * 0.15f;
					nvgBeginPath(vg);
					nvgMoveTo(vg, x + (side ? 0 : h), y - h), nvgLineTo(vg, x + (side ? h : 0), y), nvgLineTo(vg, x + (side ? 0 : h), y + h);
					nvgClosePath(vg);
					nvgFill(vg);
				}
			}
			if (current && (!focused || blink) && !(focused && v.col < int(text.size()))) {
				float x = textX + (v.col - off) * cw;
				nvgBeginPath(vg);
				if (focused)
					nvgRect(vg, x, y - rowH * 0.42f, cw, rowH * 0.84f);
				else
					nvgRect(vg, x, y + rowH * 0.3f, cw, rowH * 0.08f);
				nvgFillColor(vg, oledInk(focused ? 0.92f : 0.45f));
				nvgFill(vg);
			}
		}
		if (v.count > rows) {
			float h = textBottom - pad, x = box.size.x - pad - mm2px(0.5f);
			nvgBeginPath(vg);
			nvgRect(vg, x, pad + h * top / v.count, mm2px(0.5f), std::max(2.f, h * rows / v.count));
			nvgFillColor(vg, oledInk(0.45f));
			nvgFill(vg);
		}
		if (typeHint && !focused) { // tiny keyboard glyph: click here to type
			float w = mm2px(3.4f), h = mm2px(1.9f), x = box.size.x - pad - w - mm2px(1.2f), y = textBottom - h;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, y, w, h, 1.f);
			nvgStrokeColor(vg, oledInk(0.35f));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
			nvgBeginPath(vg);
			for (int r = 0; r < 2; r++)
				for (int k = 0; k < 5; k++)
					nvgRect(vg, x + w * (0.12f + 0.16f * k), y + h * (0.22f + 0.3f * r), w * 0.1f, h * 0.16f);
			nvgRect(vg, x + w * 0.3f, y + h * 0.78f, w * 0.4f, h * 0.1f);
			nvgFillColor(vg, oledInk(0.35f));
			nvgFill(vg);
		}
		if (layout)
			drawReadouts(vg, textBottom, strip);
		if (v.waiting && blink) {
			nvgBeginPath(vg);
			nvgCircle(vg, box.size.x - pad - mm2px(0.8f), pad + mm2px(0.4f), mm2px(0.45f));
			nvgFillColor(vg, oledInk(0.8f));
			nvgFill(vg);
		}
	}

	/** Each coloured knob's value, centred under the knob's guide line, in an OLED version of its cap colour. */
	void drawReadouts(NVGcontext* vg, float top, float strip) {
		nvgBeginPath(vg);
		nvgRect(vg, pad, top + strip * 0.12f, box.size.x - 2 * pad, 0.7f);
		nvgFillColor(vg, oledInk(0.18f));
		nvgFill(vg);
		nvgFontSize(vg, strip * 0.78f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		static const char* const PREVIEW[Speak::NUM_PARAMS] = {"", "m1", "+0.0", "50", "", "", "+0.0", "1.00x"};
		for (int i = 0; i < 8; i++) {
			int color = layout->knobColor[i];
			if (!color || layout->knob[i][0] < 0)
				continue;
			uint32_t c = layout::KNOB_COLORS[color];
			std::string text = module ? module->readout(i) : PREVIEW[i];
			nvgFillColor(vg, nvgRGB(c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF));
			nvgText(vg, mm2px(layout->knob[i][0] - layout->display[0]), top + strip * 0.6f, text.c_str(), nullptr);
		}
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		int r = top + int((e.pos.y - pad) / rowH);
		edit([&](Doc& d) {
			int off = r == scrolledRow ? hscroll : 0;
			d.row = clamp(r, 0, d.count() - 1);
			d.col = off + int(std::round((e.pos.x - textX) / cw));
		});
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		edit([&](Doc& d) { d.moveRow(e.scrollDelta.y > 0 ? -1 : 1); });
		e.consume(this);
	}
	void onSelect(const SelectEvent& e) override { focused = true; }
	void onDeselect(const DeselectEvent& e) override { focused = false; }
	void onSelectText(const SelectTextEvent& e) override {
		if (e.codepoint >= 0x20 && e.codepoint != 0x7F)
			edit([&](Doc& d) { d.insert(std::u32string(1, char32_t(e.codepoint))); });
		e.consume(this);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)
			return;
		int mods = e.mods & RACK_MOD_MASK;
		bool ctrl = mods & RACK_MOD_CTRL, alt = mods & GLFW_MOD_ALT, shift = mods & GLFW_MOD_SHIFT;
		GLFWwindow* win = APP->window->win;
		switch (e.key) {
			case GLFW_KEY_LEFT: edit([](Doc& d) { d.moveCol(-1); }); break;
			case GLFW_KEY_RIGHT: edit([](Doc& d) { d.moveCol(1); }); break;
			case GLFW_KEY_UP: edit([&](Doc& d) { alt ? d.moveLine(-1) : d.moveRow(-1); }); break;
			case GLFW_KEY_DOWN: edit([&](Doc& d) { alt ? d.moveLine(1) : d.moveRow(1); }); break;
			case GLFW_KEY_PAGE_UP: edit([&](Doc& d) { d.moveRow(-rows); }); break;
			case GLFW_KEY_PAGE_DOWN: edit([&](Doc& d) { d.moveRow(rows); }); break;
			case GLFW_KEY_HOME: edit([](Doc& d) { d.home(); }); break;
			case GLFW_KEY_END: edit([](Doc& d) { d.end(); }); break;
			case GLFW_KEY_BACKSPACE: edit([](Doc& d) { d.backspace(); }); break;
			case GLFW_KEY_DELETE: edit([&](Doc& d) { shift ? d.deleteLine() : d.del(); }); break;
			case GLFW_KEY_ENTER:
			case GLFW_KEY_KP_ENTER:
				if (ctrl) {
					if (module)
						module->playRequest = true;
				}
				else
					edit([&](Doc& d) { shift ? d.insertLine() : d.newline(); });
				break;
			case GLFW_KEY_ESCAPE: APP->event->setSelectedWidget(nullptr); break;
			case GLFW_KEY_D:
				if (!ctrl)
					return e.consume(this);
				edit([](Doc& d) { d.duplicateLine(); });
				break;
			case GLFW_KEY_C:
			case GLFW_KEY_X:
				if (!ctrl)
					return e.consume(this);
				edit([&](Doc& d) {
					glfwSetClipboardString(win, toUtf8(d.cur()).c_str());
					if (e.key == GLFW_KEY_X)
						d.deleteLine();
				});
				break;
			case GLFW_KEY_V:
				if (!ctrl)
					return e.consume(this);
				if (const char* text = glfwGetClipboardString(win)) {
					std::string s = text;
					s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
					edit([&](Doc& d) { d.insert(toUtf32(s)); });
				}
				break;
			default:
				if (ctrl || alt)
					return;
		}
		e.consume(this);
	}
};

enum class Icon { NONE, BACKSPACE, ENTER, SHIFT, SPACE, UP, DOWN };

/** A keycap drawn in the panel's theme; fires on press. */
struct KeyButton : widget::OpaqueWidget, FocusForwarder {
	std::string label;
	Icon icon = Icon::NONE;
	std::function<void()> onPress;
	std::function<std::string()> dynamicLabel;
	std::function<bool()> lit;
	bool pressed = false;

	void drawIcon(NVGcontext* vg, NVGcolor ink, bool on) {
		float cx = box.size.x / 2, cy = box.size.y / 2, s = box.size.y * 0.2f;
		nvgStrokeColor(vg, ink);
		nvgFillColor(vg, ink);
		nvgStrokeWidth(vg, std::max(1.f, s * 0.28f));
		nvgLineJoin(vg, NVG_ROUND);
		nvgLineCap(vg, NVG_ROUND);
		nvgBeginPath(vg);
		switch (icon) {
			case Icon::UP:
			case Icon::DOWN: {
				float d = icon == Icon::UP ? -1.f : 1.f;
				nvgMoveTo(vg, cx - s, cy - d * s * 0.5f), nvgLineTo(vg, cx + s, cy - d * s * 0.5f), nvgLineTo(vg, cx, cy + d * s * 0.7f);
				nvgClosePath(vg);
				nvgFill(vg);
				return;
			}
			case Icon::SHIFT:
				nvgMoveTo(vg, cx, cy - s * 1.1f), nvgLineTo(vg, cx + s, cy), nvgLineTo(vg, cx + s * 0.45f, cy);
				nvgLineTo(vg, cx + s * 0.45f, cy + s), nvgLineTo(vg, cx - s * 0.45f, cy + s), nvgLineTo(vg, cx - s * 0.45f, cy);
				nvgLineTo(vg, cx - s, cy), nvgClosePath(vg);
				on ? nvgFill(vg) : nvgStroke(vg);
				return;
			case Icon::BACKSPACE:
				nvgMoveTo(vg, cx + s * 1.2f, cy), nvgLineTo(vg, cx - s * 1.2f, cy);
				nvgMoveTo(vg, cx - s * 0.5f, cy - s * 0.7f), nvgLineTo(vg, cx - s * 1.2f, cy), nvgLineTo(vg, cx - s * 0.5f, cy + s * 0.7f);
				break;
			case Icon::ENTER:
				nvgMoveTo(vg, cx + s * 1.2f, cy - s), nvgLineTo(vg, cx + s * 1.2f, cy + s * 0.4f), nvgLineTo(vg, cx - s * 1.2f, cy + s * 0.4f);
				nvgMoveTo(vg, cx - s * 0.6f, cy - s * 0.2f), nvgLineTo(vg, cx - s * 1.2f, cy + s * 0.4f), nvgLineTo(vg, cx - s * 0.6f, cy + s);
				break;
			case Icon::SPACE:
				nvgMoveTo(vg, cx - s * 2.f, cy - s * 0.2f), nvgLineTo(vg, cx - s * 2.f, cy + s * 0.5f);
				nvgLineTo(vg, cx + s * 2.f, cy + s * 0.5f), nvgLineTo(vg, cx + s * 2.f, cy - s * 0.2f);
				break;
			default: return;
		}
		nvgStroke(vg);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		bool dark = settings::preferDarkPanels, on = lit && lit();
		float r = mm2px(0.9f), sink = pressed ? 0.6f : 0.f;
		nvgBeginPath(vg);
		nvgRect(vg, -4, -2, box.size.x + 8, box.size.y + 8);
		nvgFillPaint(vg, nvgBoxGradient(vg, 0, 1.2f - sink, box.size.x, box.size.y, r, pressed ? 1.5f : 3.f,
			nvgRGBAf(0, 0, 0, dark ? 0.5f : 0.22f), nvgRGBAf(0, 0, 0, 0)));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, sink, box.size.x, box.size.y - sink, r);
		NVGcolor a = dark ? nvgRGB(0x3c, 0x3e, 0x43) : nvgRGB(0xfb, 0xfb, 0xfa);
		NVGcolor b = dark ? nvgRGB(0x30, 0x31, 0x35) : nvgRGB(0xe6, 0xe5, 0xe1);
		if (pressed)
			std::swap(a, b);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, box.size.y, a, b));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, dark ? 0.55f : 0.16f));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		NVGcolor ink = dark ? nvgRGB(0xdc, 0xdc, 0xdc) : nvgRGB(0x2b, 0x2b, 0x2b);
		nvgSave(vg);
		nvgTranslate(vg, 0, sink);
		if (icon != Icon::NONE)
			drawIcon(vg, ink, on);
		else if (std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"))) {
			std::string text = dynamicLabel ? dynamicLabel() : label;
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, box.size.y * (text.size() > 1 ? 0.42f : 0.56f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, ink);
			nvgText(vg, box.size.x / 2, box.size.y / 2, text.c_str(), nullptr);
		}
		nvgRestore(vg);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		pressed = true;
		if (onPress)
			onPress();
	}
	void onDragEnd(const DragEndEvent& e) override { pressed = false; }
	void onSelect(const SelectEvent& e) override { forward(); }
};

/** Endless rotary encoder: reports detent steps, holds no parameter. */
struct Encoder : widget::OpaqueWidget, FocusForwarder {
	widget::FramebufferWidget* fb = new widget::FramebufferWidget;
	widget::TransformWidget* tw = new widget::TransformWidget;
	widget::SvgWidget* cap = new widget::SvgWidget;
	std::function<void(int)> onStep;
	float accum = 0.f, angle = 0.f;

	Encoder() {
		auto* bg = new widget::SvgWidget;
		bg->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundBlackKnob_bg.svg")));
		cap->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundBlackKnob.svg")));
		box.size = fb->box.size = tw->box.size = cap->box.size;
		auto* shadow = new app::CircularShadow;
		shadow->box.size = box.size;
		shadow->box.pos = Vec(0, box.size.y * 0.1f);
		addChild(fb);
		fb->addChild(shadow);
		fb->addChild(bg);
		fb->addChild(tw);
		tw->addChild(cap);
	}
	void step(int n) {
		if (onStep)
			onStep(n);
		angle += n * float(M_PI) / 10.f;
		Vec c = box.size.div(2);
		tw->identity();
		tw->translate(c);
		tw->rotate(angle);
		tw->translate(c.neg());
		fb->setDirty();
	}
	void onDragMove(const DragMoveEvent& e) override {
		for (accum -= e.mouseDelta.y; std::fabs(accum) >= 10.f; accum -= std::copysign(10.f, accum))
			step(accum > 0 ? 1 : -1);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		step(e.scrollDelta.y > 0 ? 1 : -1);
		e.consume(this);
	}
	void onSelect(const SelectEvent& e) override { forward(); }
};
