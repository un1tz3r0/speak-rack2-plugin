#include "plugin.hpp"
#include "Tts.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	Catalog::get().load(asset::plugin(p, "res"));
	p->addModel(modelSpeak);
	p->addModel(modelSpeakCompact);
	p->addModel(modelSpeakFace);
	p->addModel(modelSpeakOuts);
}
