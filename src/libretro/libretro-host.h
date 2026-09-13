/*
** libretro-host.h
**
** A minimal libretro frontend living inside mkxp-z. One core at a time is
** loaded from a shared library (SDL_LoadObject: dlopen / LoadLibrary), fed
** one joypad bitmask per frame, and its video is kept as an RGBA frame the
** Ruby side uploads into a Bitmap. Audio is queued into its own OpenAL source.
**
** Nothing here is NES-specific: any software-rendered libretro core (NES,
** Game Boy, SNES...) goes through the same interface.
*/

#ifndef LIBRETRO_HOST_H
#define LIBRETRO_HOST_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"

class LibretroHost
{
public:
	static LibretroHost &instance();

	/* Core lifecycle */
	bool loadCore(const std::string &path);
	void unloadCore();
	bool coreLoaded() const { return lib != 0; }

	/* Game lifecycle */
	bool loadGame(const std::string &path);
	void unloadGame();
	bool gameLoaded() const { return gameIsLoaded; }
	void reset();

	/* Runs exactly one emulated frame with the given joypad bitmask
	 * (bit n = RETRO_DEVICE_ID_JOYPAD_n) on port 0. */
	void runFrame(uint32_t buttons, uint32_t buttons2 = 0);

	/* Video: RGBA8888, width*height*4 bytes */
	const std::vector<uint8_t> &frame() const { return frameRGBA; }
	int frameWidth() const { return frameW; }
	int frameHeight() const { return frameH; }
	uint64_t frameCount() const { return frames; }
	uint64_t videoSerial() const { return videoSerialNo; }

	/* Timing */
	double fps() const { return avInfo.timing.fps; }
	double sampleRate() const { return avInfo.timing.sample_rate; }
	int baseWidth() const { return avInfo.geometry.base_width; }
	int baseHeight() const { return avInfo.geometry.base_height; }

	/* Audio */
	void setAudioEnabled(bool on);
	bool audioEnabled() const { return audioOn; }
	void setVolume(float v);
	float volume() const { return audioGain; }
	int audioQueuedBuffers();
	uint64_t audioFramesQueued() const { return audioFramesTotal; }
	uint64_t audioFramesDropped() const { return audioFramesDrop; }
	uint64_t audioUnderruns() const { return audioRestarts; }

	/* Memory, states */
	std::string readMemory(unsigned id, size_t offset, size_t len, bool whole);
	size_t memorySize(unsigned id);
	bool writeMemory(unsigned id, size_t offset, const std::string &data);
	bool saveState(std::string &out);
	bool loadState(const std::string &in);

	/* Info */
	const std::string &lastError() const { return error; }
	std::string coreName() const { return sysInfo.library_name ? sysInfo.library_name : ""; }
	std::string coreVersion() const { return sysInfo.library_version ? sysInfo.library_version : ""; }
	std::string coreExtensions() const { return sysInfo.valid_extensions ? sysInfo.valid_extensions : ""; }
	bool needFullpath() const { return sysInfo.need_fullpath; }

	/* Options / directories */
	std::map<std::string, std::string> options;        /* user-set values */
	std::map<std::string, std::string> optionDefaults; /* from SET_VARIABLES */
	bool optionsDirty = false;
	std::string systemDir = ".";
	std::string saveDir = ".";

	/* Callbacks (public so the static trampolines can reach them) */
	bool environment(unsigned cmd, void *data);
	void videoRefresh(const void *data, unsigned width, unsigned height, size_t pitch);
	size_t audioBatch(const int16_t *data, size_t frames);
	int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id);

private:
	LibretroHost() {}
	LibretroHost(const LibretroHost &);

	void fail(const std::string &msg) { error = msg; }
	void audioOpen();
	void audioClose();
	void audioFlush();

	void *lib = 0;
	bool gameIsLoaded = false;
	bool coreInited = false;
	std::string error;

	struct {
		void (*init)(void);
		void (*deinit)(void);
		unsigned (*api_version)(void);
		void (*get_system_info)(struct retro_system_info *);
		void (*get_system_av_info)(struct retro_system_av_info *);
		void (*set_environment)(retro_environment_t);
		void (*set_video_refresh)(retro_video_refresh_t);
		void (*set_audio_sample)(retro_audio_sample_t);
		void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
		void (*set_input_poll)(retro_input_poll_t);
		void (*set_input_state)(retro_input_state_t);
		void (*set_controller_port_device)(unsigned, unsigned);
		void (*reset)(void);
		void (*run)(void);
		size_t (*serialize_size)(void);
		bool (*serialize)(void *, size_t);
		bool (*unserialize)(const void *, size_t);
		bool (*load_game)(const struct retro_game_info *);
		void (*unload_game)(void);
		void *(*get_memory_data)(unsigned);
		size_t (*get_memory_size)(unsigned);
	} fn;

	struct retro_system_info sysInfo = {};
	struct retro_system_av_info avInfo = {};
	enum retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;

	std::string gamePath;
	std::vector<uint8_t> gameData;

	uint32_t pad[2] = {0, 0};

	std::vector<uint8_t> frameRGBA;
	int frameW = 0, frameH = 0;
	uint64_t frames = 0;
	uint64_t videoSerialNo = 0;

	/* audio */
	bool audioOn = true;
	float audioGain = 1.0f;
	bool alReady = false;
	unsigned alSource = 0;
	std::vector<unsigned> alFree;
	std::vector<unsigned> alAll;
	std::vector<int16_t> pending;
	uint64_t audioFramesTotal = 0;
	uint64_t audioFramesDrop = 0;
	uint64_t audioRestarts = 0;
	bool audioStarted = false;
};

#endif // LIBRETRO_HOST_H
