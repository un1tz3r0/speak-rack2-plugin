RACK_DIR ?= ../..

FLAGS += -Ivendor/espeak-ng/src/include -Ivendor/flite/include
EXTRA_CXXFLAGS += -std=c++17
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res LICENSE.txt VENDOR.txt

include $(RACK_DIR)/arch.mk

# --- eSpeak NG: static library for the target, data compiled by a native host build ---
ESPEAK_SRC := vendor/espeak-ng
ESPEAK_BUILD := dep/espeak-ng
ESPEAK_HOST := dep/espeak-ng-host
ESPEAK_LIBS := $(ESPEAK_BUILD)/src/libespeak-ng/libespeak-ng.a \
	$(ESPEAK_BUILD)/src/speechPlayer/libspeechPlayer.a \
	$(ESPEAK_BUILD)/src/ucd-tools/libucd.a
ESPEAK_STAMP := $(ESPEAK_BUILD)/.built
ESPEAK_DATA := res/espeak-ng-data/phontab
ESPEAK_OPTS := -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DUSE_MBROLA=OFF -DUSE_LIBSONIC=OFF -DUSE_LIBPCAUDIO=OFF -DUSE_ASYNC=OFF \
	-DENABLE_TESTS=OFF -DESPEAK_COMPAT=OFF -DCMAKE_C_VISIBILITY_PRESET=hidden -DCMAKE_CXX_VISIBILITY_PRESET=hidden

# --- Flite: autotools build in a scratch copy, so vendor/ stays pristine ---
FLITE_BUILD := dep/flite
FLITE_STAMP := $(FLITE_BUILD)/.built
FLITE_LIBS := cmu_us_kal cmu_us_kal16 cmu_us_awb cmu_us_rms cmu_us_slt usenglish cmulex flite
ifdef CROSS_COMPILE
FLITE_HOST := --host=$(MACHINE)
endif

DEPS += $(ESPEAK_STAMP) $(FLITE_STAMP) $(ESPEAK_DATA)
OBJECTS += $(ESPEAK_LIBS)
FLITE_LDFLAGS := -L$(FLITE_BUILD)/lib $(addprefix -lflite_,$(filter-out flite,$(FLITE_LIBS))) -lflite
LDFLAGS += $(FLITE_LDFLAGS)
ifdef ARCH_LIN
LDFLAGS += -Wl,--exclude-libs,ALL
endif
ifdef ARCH_WIN
LDFLAGS += -lwinmm
endif

$(ESPEAK_LIBS): $(ESPEAK_STAMP)

$(ESPEAK_STAMP):
	$(CMAKE) -S $(ESPEAK_SRC) -B $(ESPEAK_BUILD) $(ESPEAK_OPTS) -DCOMPILE_INTONATIONS=OFF
	cmake --build $(ESPEAK_BUILD) --target espeak-ng -j4
	touch $@

$(ESPEAK_DATA):
	env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS cmake -S $(ESPEAK_SRC) -B $(ESPEAK_HOST) $(ESPEAK_OPTS)
	env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS cmake --build $(ESPEAK_HOST) --target data -j4
	rm -rf res/espeak-ng-data
	cp -R $(ESPEAK_HOST)/espeak-ng-data res/espeak-ng-data

$(FLITE_STAMP):
	rm -rf $(FLITE_BUILD) && mkdir -p dep && cp -R vendor/flite $(FLITE_BUILD)
	cd $(FLITE_BUILD) && CFLAGS="$(DEP_CFLAGS) -fvisibility=hidden" ./configure $(FLITE_HOST) --with-audio=none --disable-shared --with-pic
	$(MAKE) -C $(FLITE_BUILD)/include
	$(MAKE) -C $(FLITE_BUILD)/src
	$(MAKE) -C $(FLITE_BUILD)/lang
	mkdir -p $(FLITE_BUILD)/lib && cp $(FLITE_BUILD)/build/*/lib/*.a $(FLITE_BUILD)/lib/
	touch $@

include $(RACK_DIR)/plugin.mk

$(TARGET): | $(FLITE_STAMP) $(ESPEAK_DATA)

# Offline checks (Linux): module_test drives process() against the live worker; render_test writes WAVs to build/.
TEST_LINK = $(ESPEAK_LIBS) $(FLITE_LDFLAGS) -L$(RACK_DIR) -lRack -Wl,-rpath,$(abspath $(RACK_DIR)) -lpthread
test: $(TARGET)
	$(CXX) $(CXXFLAGS) -o build/module_test tools/module_test.cpp $(filter build/src/%,$(OBJECTS)) $(TEST_LINK)
	$(CXX) $(CXXFLAGS) -o build/render_test tools/render_test.cpp build/src/Tts.cpp.o $(TEST_LINK)
	build/module_test
	cd build && ./render_test ../res

.PHONY: test

# Contact sheet of every hairstyle, beard, pair of glasses, robot and mouth shape: build/faces.ppm
faces:
	mkdir -p build && $(CXX) -std=c++17 -O1 -o build/face_sheet tools/face_sheet.cpp && build/face_sheet build/faces.ppm 5

.PHONY: faces
