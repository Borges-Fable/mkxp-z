/*
** libretro-host.cpp
**
** See libretro-host.h. Single-threaded: every call comes from the RGSS
** (Ruby) thread, which also owns the GL and OpenAL contexts.
*/

#include "libretro-host.h"

#include <SDL_loadso.h>
#include <al.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "util/debugwriter.h"

/* ---------------------------------------------------------------------- */
/* C trampolines                                                          */

static LibretroHost *active()
{
	return &LibretroHost::instance();
}

static bool cbEnvironment(unsigned cmd, void *data)
{
	return active()->environment(cmd, data);
}

static void cbVideo(const void *data, unsigned w, unsigned h, size_t pitch)
{
	active()->videoRefresh(data, w, h, pitch);
}

static void cbAudioSample(int16_t l, int16_t r)
{
	int16_t s[2] = {l, r};
	active()->audioBatch(s, 1);
}

static size_t cbAudioBatch(const int16_t *data, size_t frames)
{
	return active()->audioBatch(data, frames);
}

static void cbInputPoll(void) {}

static int16_t cbInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	return active()->inputState(port, device, index, id);
}

static void cbLog(enum retro_log_level level, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	size_t n = strlen(buf);
	while (n && (buf[n-1] == '\n' || buf[n-1] == '\r'))
		buf[--n] = 0;
	static const char *names[] = {"debug", "info", "warn", "error"};
	if (level >= RETRO_LOG_INFO)
		Debug() << "[libretro" << (level <= RETRO_LOG_ERROR ? names[level] : "?") << "]" << buf;
}

/* ---------------------------------------------------------------------- */

LibretroHost &LibretroHost::instance()
{
	static LibretroHost host;
	return host;
}

bool LibretroHost::loadCore(const std::string &path)
{
	if (lib)
		unloadCore();

	error.clear();

	void *h = SDL_LoadObject(path.c_str());
	if (!h) {
		fail(std::string("cannot open core '") + path + "': " + SDL_GetError());
		return false;
	}

	memset(&fn, 0, sizeof(fn));
	bool ok = true;
	#define SYM(field, name) \
		fn.field = (decltype(fn.field)) SDL_LoadFunction(h, name); \
		if (!fn.field) { ok = false; fail(std::string("core is missing symbol ") + name); }
	SYM(init, "retro_init");
	SYM(deinit, "retro_deinit");
	SYM(api_version, "retro_api_version");
	SYM(get_system_info, "retro_get_system_info");
	SYM(get_system_av_info, "retro_get_system_av_info");
	SYM(set_environment, "retro_set_environment");
	SYM(set_video_refresh, "retro_set_video_refresh");
	SYM(set_audio_sample, "retro_set_audio_sample");
	SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
	SYM(set_input_poll, "retro_set_input_poll");
	SYM(set_input_state, "retro_set_input_state");
	SYM(set_controller_port_device, "retro_set_controller_port_device");
	SYM(reset, "retro_reset");
	SYM(run, "retro_run");
	SYM(serialize_size, "retro_serialize_size");
	SYM(serialize, "retro_serialize");
	SYM(unserialize, "retro_unserialize");
	SYM(load_game, "retro_load_game");
	SYM(unload_game, "retro_unload_game");
	SYM(get_memory_data, "retro_get_memory_data");
	SYM(get_memory_size, "retro_get_memory_size");
	#undef SYM

	if (!ok) {
		SDL_UnloadObject(h);
		memset(&fn, 0, sizeof(fn));
		return false;
	}

	if (fn.api_version() != RETRO_API_VERSION) {
		fail("core has an incompatible libretro API version");
		SDL_UnloadObject(h);
		memset(&fn, 0, sizeof(fn));
		return false;
	}

	lib = h;
	pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
	optionDefaults.clear();

	/* Order mandated by the libretro spec: set_environment before init */
	fn.set_environment(cbEnvironment);
	fn.init();
	coreInited = true;

	fn.set_video_refresh(cbVideo);
	fn.set_audio_sample(cbAudioSample);
	fn.set_audio_sample_batch(cbAudioBatch);
	fn.set_input_poll(cbInputPoll);
	fn.set_input_state(cbInputState);

	memset(&sysInfo, 0, sizeof(sysInfo));
	fn.get_system_info(&sysInfo);

	Debug() << "libretro: loaded core" << coreName() << coreVersion() << "from" << path;
	return true;
}

void LibretroHost::unloadCore()
{
	if (!lib)
		return;
	unloadGame();
	if (coreInited)
		fn.deinit();
	coreInited = false;
	SDL_UnloadObject(lib);
	lib = 0;
	memset(&fn, 0, sizeof(fn));
	memset(&sysInfo, 0, sizeof(sysInfo));
}

bool LibretroHost::loadGame(const std::string &path)
{
	error.clear();
	if (!lib) {
		fail("no core loaded");
		return false;
	}
	unloadGame();

	struct retro_game_info info = {};
	gamePath = path;
	info.path = gamePath.c_str();

	if (!sysInfo.need_fullpath) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			fail(std::string("cannot read ROM '") + path + "'");
			return false;
		}
		gameData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		if (gameData.empty()) {
			fail(std::string("ROM '") + path + "' is empty");
			return false;
		}
		info.data = gameData.data();
		info.size = gameData.size();
	}

	if (!fn.load_game(&info)) {
		fail(std::string("core rejected ROM '") + path + "'");
		gameData.clear();
		return false;
	}

	/* Core expects port devices to be joypads by default, but be explicit */
	fn.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	fn.set_controller_port_device(1, RETRO_DEVICE_JOYPAD);

	memset(&avInfo, 0, sizeof(avInfo));
	fn.get_system_av_info(&avInfo);

	frameW = avInfo.geometry.base_width;
	frameH = avInfo.geometry.base_height;
	frameRGBA.assign((size_t)frameW * frameH * 4, 0);
	for (size_t i = 3; i < frameRGBA.size(); i += 4)
		frameRGBA[i] = 255;
	frames = 0;
	videoSerialNo++;
	gameIsLoaded = true;

	audioOpen();

	Debug() << "libretro: loaded" << path << "(" << frameW << "x" << frameH
	        << "@" << avInfo.timing.fps << "fps, audio" << avInfo.timing.sample_rate << "Hz)";
	return true;
}

void LibretroHost::unloadGame()
{
	if (!gameIsLoaded)
		return;
	audioClose();
	fn.unload_game();
	gameIsLoaded = false;
	gameData.clear();
	pad[0] = pad[1] = 0;
}

void LibretroHost::reset()
{
	if (gameIsLoaded)
		fn.reset();
}

void LibretroHost::runFrame(uint32_t buttons, uint32_t buttons2)
{
	if (!gameIsLoaded)
		return;
	pad[0] = buttons;
	pad[1] = buttons2;
	fn.run();
	frames++;
	audioFlush();
}

/* ---------------------------------------------------------------------- */
/* Callbacks                                                              */

bool LibretroHost::environment(unsigned cmd, void *data)
{
	const unsigned base = cmd & ~(RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE);
	#define BASE(x) ((x) & ~(RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE))

	switch (base) {
	case BASE(RETRO_ENVIRONMENT_GET_CAN_DUPE):
		if (data) *(bool *)data = true;
		return true;

	case BASE(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT): {
		enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
		if (f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_RGB565 &&
		    f != RETRO_PIXEL_FORMAT_XRGB8888)
			return false;
		pixelFormat = f;
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY):
		*(const char **)data = systemDir.c_str();
		return true;

	case BASE(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY):
		*(const char **)data = saveDir.c_str();
		return true;

	case BASE(RETRO_ENVIRONMENT_GET_LOG_INTERFACE):
		((struct retro_log_callback *)data)->log = cbLog;
		return true;

	case BASE(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS):
		return true;

	case BASE(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION):
		/* 0 = legacy SET_VARIABLES; every core supports that path */
		if (data) *(unsigned *)data = 0;
		return true;

	case BASE(RETRO_ENVIRONMENT_SET_VARIABLES): {
		const struct retro_variable *v = (const struct retro_variable *)data;
		for (; v && v->key; v++) {
			/* "Description; first|second|third" -> default is "first" */
			std::string val = v->value ? v->value : "";
			size_t semi = val.find("; ");
			if (semi != std::string::npos)
				val = val.substr(semi + 2);
			size_t bar = val.find('|');
			if (bar != std::string::npos)
				val = val.substr(0, bar);
			optionDefaults[v->key] = val;
		}
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_GET_VARIABLE): {
		struct retro_variable *v = (struct retro_variable *)data;
		if (!v || !v->key)
			return false;
		std::map<std::string, std::string>::const_iterator it = options.find(v->key);
		if (it == options.end()) {
			it = optionDefaults.find(v->key);
			if (it == optionDefaults.end())
				return false;
		}
		v->value = it->second.c_str();
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE):
		*(bool *)data = optionsDirty;
		optionsDirty = false;
		return true;

	case BASE(RETRO_ENVIRONMENT_SET_GEOMETRY): {
		const struct retro_game_geometry *g = (const struct retro_game_geometry *)data;
		avInfo.geometry = *g;
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO): {
		const struct retro_system_av_info *av = (const struct retro_system_av_info *)data;
		bool rateChanged = av->timing.sample_rate != avInfo.timing.sample_rate;
		avInfo = *av;
		if (rateChanged && gameIsLoaded) {
			audioClose();
			audioOpen();
		}
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_GET_LANGUAGE):
		*(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
		return true;

	case BASE(RETRO_ENVIRONMENT_SET_MESSAGE): {
		const struct retro_message *m = (const struct retro_message *)data;
		if (m && m->msg) Debug() << "[libretro msg]" << m->msg;
		return true;
	}

	case BASE(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS):
	case BASE(RETRO_ENVIRONMENT_SET_MEMORY_MAPS):
	case BASE(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME):
	case BASE(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL):
	case BASE(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO):
	case BASE(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO):
	case BASE(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS):
	case BASE(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS):
		return true;

	default:
		/* Everything else (hardware rendering, core options v1/v2,
		 * rumble, camera...) is unsupported; cores fall back. */
		return false;
	}
	#undef BASE
}

void LibretroHost::videoRefresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
	if (!data || data == RETRO_HW_FRAME_BUFFER_VALID)
		return; /* duped frame: keep the previous one */

	if ((int)width != frameW || (int)height != frameH) {
		frameW = width;
		frameH = height;
		frameRGBA.assign((size_t)width * height * 4, 0);
	}

	uint8_t *out = frameRGBA.data();
	for (unsigned y = 0; y < height; y++) {
		const uint8_t *row = (const uint8_t *)data + y * pitch;
		switch (pixelFormat) {
		case RETRO_PIXEL_FORMAT_RGB565: {
			const uint16_t *p = (const uint16_t *)row;
			for (unsigned x = 0; x < width; x++, out += 4) {
				uint16_t c = p[x];
				uint8_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
				out[0] = (r << 3) | (r >> 2);
				out[1] = (g << 2) | (g >> 4);
				out[2] = (b << 3) | (b >> 2);
				out[3] = 255;
			}
			break;
		}
		case RETRO_PIXEL_FORMAT_0RGB1555: {
			const uint16_t *p = (const uint16_t *)row;
			for (unsigned x = 0; x < width; x++, out += 4) {
				uint16_t c = p[x];
				uint8_t r = (c >> 10) & 0x1f, g = (c >> 5) & 0x1f, b = c & 0x1f;
				out[0] = (r << 3) | (r >> 2);
				out[1] = (g << 3) | (g >> 2);
				out[2] = (b << 3) | (b >> 2);
				out[3] = 255;
			}
			break;
		}
		default: { /* XRGB8888 */
			const uint32_t *p = (const uint32_t *)row;
			for (unsigned x = 0; x < width; x++, out += 4) {
				uint32_t c = p[x];
				out[0] = (c >> 16) & 0xff;
				out[1] = (c >> 8) & 0xff;
				out[2] = c & 0xff;
				out[3] = 255;
			}
			break;
		}
		}
	}
	videoSerialNo++;
}

int16_t LibretroHost::inputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)index;
	if (port > 1 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD)
		return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return (int16_t)(pad[port] & 0xffff);
	if (id > 15)
		return 0;
	return (pad[port] >> id) & 1;
}

/* ---------------------------------------------------------------------- */
/* Audio: a private OpenAL streaming source.                              */
/* Each emulated frame's samples become one AL buffer. If the queue grows */
/* beyond MAX_QUEUED (the core runs ~0.16% fast vs a 60 Hz game loop, or  */
/* the game loop hitches), the frame's audio is dropped; if it runs dry   */
/* the source restarts after PREROLL buffers.                             */

static const int AL_POOL = 24;
static const int MAX_QUEUED = 8;   /* ~133 ms at 60 fps */
static const int PREROLL = 3;      /* ~50 ms */

void LibretroHost::audioOpen()
{
	if (alReady)
		return;
	alGetError();
	alGenSources(1, &alSource);
	if (alGetError() != AL_NO_ERROR) {
		Debug() << "libretro: could not create OpenAL source; audio disabled";
		return;
	}
	alAll.resize(AL_POOL);
	alGenBuffers(AL_POOL, alAll.data());
	if (alGetError() != AL_NO_ERROR) {
		alDeleteSources(1, &alSource);
		alAll.clear();
		Debug() << "libretro: could not create OpenAL buffers; audio disabled";
		return;
	}
	alFree = alAll;
	alSourcef(alSource, AL_GAIN, audioGain);
	alSourcei(alSource, AL_SOURCE_RELATIVE, AL_TRUE);
	alSourcei(alSource, AL_LOOPING, AL_FALSE);
	pending.clear();
	audioStarted = false;
	alReady = true;
}

void LibretroHost::audioClose()
{
	if (!alReady)
		return;
	alSourceStop(alSource);
	alSourcei(alSource, AL_BUFFER, 0);
	alDeleteSources(1, &alSource);
	alDeleteBuffers((ALsizei)alAll.size(), alAll.data());
	alAll.clear();
	alFree.clear();
	pending.clear();
	alReady = false;
}

void LibretroHost::setAudioEnabled(bool on)
{
	audioOn = on;
	if (!on && alReady) {
		alSourceStop(alSource);
		pending.clear();
	}
}

void LibretroHost::setVolume(float v)
{
	audioGain = v < 0 ? 0 : v;
	if (alReady)
		alSourcef(alSource, AL_GAIN, audioGain);
}

int LibretroHost::audioQueuedBuffers()
{
	if (!alReady)
		return 0;
	ALint q = 0, p = 0;
	alGetSourcei(alSource, AL_BUFFERS_QUEUED, &q);
	alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &p);
	return q - p;
}

size_t LibretroHost::audioBatch(const int16_t *data, size_t count)
{
	if (audioOn && alReady)
		pending.insert(pending.end(), data, data + count * 2);
	return count;
}

void LibretroHost::audioFlush()
{
	if (!alReady || !audioOn || pending.empty()) {
		pending.clear();
		return;
	}

	/* Reclaim finished buffers */
	ALint processed = 0;
	alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
	while (processed-- > 0) {
		ALuint b = 0;
		alSourceUnqueueBuffers(alSource, 1, &b);
		alFree.push_back(b);
	}

	size_t nframes = pending.size() / 2;
	ALint queued = 0;
	alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);

	if (queued >= MAX_QUEUED || alFree.empty()) {
		audioFramesDrop += nframes;
		pending.clear();
		return;
	}

	ALuint b = alFree.back();
	alFree.pop_back();
	alBufferData(b, AL_FORMAT_STEREO16, pending.data(),
	             (ALsizei)(nframes * 2 * sizeof(int16_t)),
	             (ALsizei)(avInfo.timing.sample_rate > 0 ? avInfo.timing.sample_rate : 44100));
	alSourceQueueBuffers(alSource, 1, &b);
	audioFramesTotal += nframes;
	pending.clear();

	ALint state = 0;
	alGetSourcei(alSource, AL_SOURCE_STATE, &state);
	if (state != AL_PLAYING && queued + 1 >= PREROLL) {
		if (audioStarted)
			audioRestarts++;   /* the queue ran dry: an audible gap */
		audioStarted = true;
		alSourcePlay(alSource);
	}
}

/* ---------------------------------------------------------------------- */

size_t LibretroHost::memorySize(unsigned id)
{
	if (!gameIsLoaded)
		return 0;
	return fn.get_memory_size(id);
}

std::string LibretroHost::readMemory(unsigned id, size_t offset, size_t len, bool whole)
{
	if (!gameIsLoaded)
		return std::string();
	const uint8_t *p = (const uint8_t *)fn.get_memory_data(id);
	size_t size = fn.get_memory_size(id);
	if (!p || offset >= size)
		return std::string();
	if (whole || offset + len > size)
		len = size - offset;
	return std::string((const char *)p + offset, len);
}

bool LibretroHost::writeMemory(unsigned id, size_t offset, const std::string &data)
{
	if (!gameIsLoaded)
		return false;
	uint8_t *p = (uint8_t *)fn.get_memory_data(id);
	size_t size = fn.get_memory_size(id);
	if (!p || offset + data.size() > size)
		return false;
	memcpy(p + offset, data.data(), data.size());
	return true;
}

bool LibretroHost::saveState(std::string &out)
{
	if (!gameIsLoaded)
		return false;
	size_t n = fn.serialize_size();
	if (!n)
		return false;
	out.assign(n, '\0');
	return fn.serialize(&out[0], n);
}

bool LibretroHost::loadState(const std::string &in)
{
	if (!gameIsLoaded || in.empty())
		return false;
	return fn.unserialize(in.data(), in.size());
}
