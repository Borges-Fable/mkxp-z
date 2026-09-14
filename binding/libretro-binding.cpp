/*
** libretro-binding.cpp
**
** Ruby module `Libretro`: load a libretro core (NES, Game Boy, SNES...) from
** a shared library, load a ROM, step it one frame at a time and show its
** video through an ordinary Bitmap.
**
**   Libretro.load_core("Cores/quicknes_libretro.so")  -> true / false
**   Libretro.load_game("ROMs/croom.nes")               -> true / false
**   Libretro.last_error                                -> String or nil
**   Libretro.run_frame(buttons [, buttons_p2])         -> frame number
**   Libretro.frame_bitmap                              -> Bitmap (engine-owned, updated on call)
**   Libretro.blit(bitmap)                              -> copies the frame into a same-sized Bitmap
**   Libretro.frame_size                                -> [w, h]
**   Libretro.unload_game / Libretro.unload
**   plus: core_info, av_info, reset, audio_enabled(=), volume(=), audio_stats,
**         memory(id [, offset, length]), memory_size(id), write_memory(id, offset, str),
**         save_state, load_state(str), set_option(key, value), options,
**         system_directory=, save_directory=
**   Button bit numbers: Libretro::B, Y, SELECT, START, UP, DOWN, LEFT, RIGHT, A, X, L, R, L2, R2, L3, R3
**   Memory ids: Libretro::MEMORY_SAVE_RAM, MEMORY_RTC, MEMORY_SYSTEM_RAM, MEMORY_VIDEO_RAM
*/

#include "binding-util.h"
#include "bitmap.h"
#include "sharedstate.h"
#include "graphics.h"
#include "exception.h"

#include "libretro/libretro-host.h"

/* RB_METHOD_GUARD arrived in mkxp-z after 2.4.2/c9378cf. Every C++ call in the
 * guarded bodies below already goes through GFX_GUARD_EXC, so on this base the
 * guard is only the method wrapper. */
#ifndef RB_METHOD_GUARD
#define RB_METHOD_GUARD(name) RB_METHOD(name) {
#define RB_METHOD_GUARD_END return Qnil; }
#endif


DECL_TYPE(Bitmap);
void bitmapInitProps(Bitmap *b, VALUE self);

static VALUE frameBitmapObj = Qnil;
static uint64_t frameBitmapSerial = (uint64_t)-1;

static LibretroHost &host()
{
	return LibretroHost::instance();
}

static std::string strArg(VALUE v)
{
	SafeStringValue(v);
	return std::string(RSTRING_PTR(v), RSTRING_LEN(v));
}

RB_METHOD(libretroLoadCore)
{
	RB_UNUSED_PARAM;
	VALUE path;
	rb_scan_args(argc, argv, "1", &path);
	return host().loadCore(strArg(path)) ? Qtrue : Qfalse;
}

RB_METHOD(libretroLoadGame)
{
	RB_UNUSED_PARAM;
	VALUE path;
	rb_scan_args(argc, argv, "1", &path);
	return host().loadGame(strArg(path)) ? Qtrue : Qfalse;
}

RB_METHOD(libretroLastError)
{
	RB_UNUSED_PARAM;
	const std::string &e = host().lastError();
	return e.empty() ? Qnil : rb_utf8_str_new(e.data(), e.size());
}

RB_METHOD(libretroCoreLoaded)
{
	RB_UNUSED_PARAM;
	return host().coreLoaded() ? Qtrue : Qfalse;
}

RB_METHOD(libretroGameLoaded)
{
	RB_UNUSED_PARAM;
	return host().gameLoaded() ? Qtrue : Qfalse;
}

RB_METHOD(libretroCoreInfo)
{
	RB_UNUSED_PARAM;
	if (!host().coreLoaded())
		return Qnil;
	VALUE h = rb_hash_new();
	std::string s;
	s = host().coreName();       rb_hash_aset(h, ID2SYM(rb_intern("name")), rb_utf8_str_new(s.data(), s.size()));
	s = host().coreVersion();    rb_hash_aset(h, ID2SYM(rb_intern("version")), rb_utf8_str_new(s.data(), s.size()));
	s = host().coreExtensions(); rb_hash_aset(h, ID2SYM(rb_intern("extensions")), rb_utf8_str_new(s.data(), s.size()));
	rb_hash_aset(h, ID2SYM(rb_intern("need_fullpath")), host().needFullpath() ? Qtrue : Qfalse);
	return h;
}

RB_METHOD(libretroAvInfo)
{
	RB_UNUSED_PARAM;
	if (!host().gameLoaded())
		return Qnil;
	VALUE h = rb_hash_new();
	rb_hash_aset(h, ID2SYM(rb_intern("fps")), rb_float_new(host().fps()));
	rb_hash_aset(h, ID2SYM(rb_intern("sample_rate")), rb_float_new(host().sampleRate()));
	rb_hash_aset(h, ID2SYM(rb_intern("base_width")), INT2NUM(host().baseWidth()));
	rb_hash_aset(h, ID2SYM(rb_intern("base_height")), INT2NUM(host().baseHeight()));
	return h;
}

RB_METHOD(libretroRunFrame)
{
	RB_UNUSED_PARAM;
	VALUE b1, b2;
	rb_scan_args(argc, argv, "02", &b1, &b2);
	uint32_t p1 = NIL_P(b1) ? 0 : NUM2UINT(b1);
	uint32_t p2 = NIL_P(b2) ? 0 : NUM2UINT(b2);
	host().runFrame(p1, p2);
	return ULL2NUM(host().frameCount());
}

RB_METHOD(libretroFrameCount)
{
	RB_UNUSED_PARAM;
	return ULL2NUM(host().frameCount());
}

RB_METHOD(libretroFrameSize)
{
	RB_UNUSED_PARAM;
	return rb_ary_new3(2, INT2NUM(host().frameWidth()), INT2NUM(host().frameHeight()));
}

static void uploadFrame(Bitmap *b)
{
	const std::vector<uint8_t> &f = host().frame();
	GFX_GUARD_EXC( b->replaceRaw((void *)f.data(), (int)f.size()); );
}

RB_METHOD_GUARD(libretroBlit)
{
	RB_UNUSED_PARAM;
	VALUE obj;
	rb_scan_args(argc, argv, "1", &obj);
	Bitmap *b = getPrivateDataCheck<Bitmap>(obj, BitmapType);
	if (!host().gameLoaded() || host().frame().empty())
		return Qfalse;
	if (b->width() != host().frameWidth() || b->height() != host().frameHeight())
		rb_raise(rb_eArgError, "Libretro.blit: bitmap is %dx%d, frame is %dx%d",
		         b->width(), b->height(), host().frameWidth(), host().frameHeight());
	uploadFrame(b);
	return Qtrue;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(libretroFrameBitmap)
{
	RB_UNUSED_PARAM;
	if (!host().gameLoaded() || host().frame().empty())
		return Qnil;

	int w = host().frameWidth(), h = host().frameHeight();
	bool fresh = false;

	if (NIL_P(frameBitmapObj) || RTEST(rb_funcall(frameBitmapObj, rb_intern("disposed?"), 0))) {
		fresh = true;
	} else {
		Bitmap *b = getPrivateData<Bitmap>(frameBitmapObj);
		if (b->width() != w || b->height() != h) {
			rb_funcall(frameBitmapObj, rb_intern("dispose"), 0);
			fresh = true;
		}
	}

	if (fresh) {
		VALUE klass = rb_const_get(rb_cObject, rb_intern("Bitmap"));
		VALUE args[2] = {INT2NUM(w), INT2NUM(h)};
		frameBitmapObj = rb_class_new_instance(2, args, klass);
		frameBitmapSerial = (uint64_t)-1;
	}

	if (frameBitmapSerial != host().videoSerial()) {
		uploadFrame(getPrivateData<Bitmap>(frameBitmapObj));
		frameBitmapSerial = host().videoSerial();
	}
	return frameBitmapObj;
}
RB_METHOD_GUARD_END

RB_METHOD(libretroReset)
{
	RB_UNUSED_PARAM;
	host().reset();
	return Qnil;
}

RB_METHOD(libretroUnloadGame)
{
	RB_UNUSED_PARAM;
	host().unloadGame();
	return Qnil;
}

RB_METHOD(libretroUnload)
{
	RB_UNUSED_PARAM;
	host().unloadCore();
	return Qnil;
}

RB_METHOD(libretroSetAudioEnabled)
{
	RB_UNUSED_PARAM;
	VALUE v;
	rb_scan_args(argc, argv, "1", &v);
	host().setAudioEnabled(RTEST(v));
	return v;
}

RB_METHOD(libretroAudioEnabled)
{
	RB_UNUSED_PARAM;
	return host().audioEnabled() ? Qtrue : Qfalse;
}

RB_METHOD(libretroSetVolume)
{
	RB_UNUSED_PARAM;
	VALUE v;
	rb_scan_args(argc, argv, "1", &v);
	host().setVolume((float)NUM2DBL(v));
	return v;
}

RB_METHOD(libretroVolume)
{
	RB_UNUSED_PARAM;
	return rb_float_new(host().volume());
}

RB_METHOD(libretroAudioStats)
{
	RB_UNUSED_PARAM;
	VALUE h = rb_hash_new();
	rb_hash_aset(h, ID2SYM(rb_intern("queued_buffers")), INT2NUM(host().audioQueuedBuffers()));
	rb_hash_aset(h, ID2SYM(rb_intern("frames_queued")), ULL2NUM(host().audioFramesQueued()));
	rb_hash_aset(h, ID2SYM(rb_intern("frames_dropped")), ULL2NUM(host().audioFramesDropped()));
	rb_hash_aset(h, ID2SYM(rb_intern("underruns")), ULL2NUM(host().audioUnderruns()));
	return h;
}

RB_METHOD(libretroMemory)
{
	RB_UNUSED_PARAM;
	VALUE id, off, len;
	rb_scan_args(argc, argv, "12", &id, &off, &len);
	std::string s = host().readMemory(NUM2UINT(id), NIL_P(off) ? 0 : NUM2SIZET(off),
	                                  NIL_P(len) ? 0 : NUM2SIZET(len), NIL_P(len));
	return rb_str_new(s.data(), s.size());
}

RB_METHOD(libretroMemorySize)
{
	RB_UNUSED_PARAM;
	VALUE id;
	rb_scan_args(argc, argv, "1", &id);
	return SIZET2NUM(host().memorySize(NUM2UINT(id)));
}

RB_METHOD(libretroWriteMemory)
{
	RB_UNUSED_PARAM;
	VALUE id, off, data;
	rb_scan_args(argc, argv, "3", &id, &off, &data);
	return host().writeMemory(NUM2UINT(id), NUM2SIZET(off), strArg(data)) ? Qtrue : Qfalse;
}

RB_METHOD(libretroSaveState)
{
	RB_UNUSED_PARAM;
	std::string s;
	if (!host().saveState(s))
		return Qnil;
	return rb_str_new(s.data(), s.size());
}

RB_METHOD(libretroLoadState)
{
	RB_UNUSED_PARAM;
	VALUE data;
	rb_scan_args(argc, argv, "1", &data);
	return host().loadState(strArg(data)) ? Qtrue : Qfalse;
}

RB_METHOD(libretroSetOption)
{
	RB_UNUSED_PARAM;
	VALUE k, v;
	rb_scan_args(argc, argv, "2", &k, &v);
	host().options[strArg(k)] = strArg(v);
	host().optionsDirty = true;
	return v;
}

RB_METHOD(libretroOptions)
{
	RB_UNUSED_PARAM;
	VALUE h = rb_hash_new();
	for (const auto &kv : host().optionDefaults)
		rb_hash_aset(h, rb_utf8_str_new_cstr(kv.first.c_str()), rb_utf8_str_new_cstr(kv.second.c_str()));
	for (const auto &kv : host().options)
		rb_hash_aset(h, rb_utf8_str_new_cstr(kv.first.c_str()), rb_utf8_str_new_cstr(kv.second.c_str()));
	return h;
}

RB_METHOD(libretroSetSystemDir)
{
	RB_UNUSED_PARAM;
	VALUE v;
	rb_scan_args(argc, argv, "1", &v);
	host().systemDir = strArg(v);
	return v;
}

RB_METHOD(libretroSetSaveDir)
{
	RB_UNUSED_PARAM;
	VALUE v;
	rb_scan_args(argc, argv, "1", &v);
	host().saveDir = strArg(v);
	return v;
}

void libretroBindingInit()
{
	VALUE m = rb_define_module("Libretro");
	rb_gc_register_address(&frameBitmapObj);

	_rb_define_module_function(m, "load_core", libretroLoadCore);
	_rb_define_module_function(m, "load_game", libretroLoadGame);
	_rb_define_module_function(m, "last_error", libretroLastError);
	_rb_define_module_function(m, "core_loaded?", libretroCoreLoaded);
	_rb_define_module_function(m, "game_loaded?", libretroGameLoaded);
	_rb_define_module_function(m, "core_info", libretroCoreInfo);
	_rb_define_module_function(m, "av_info", libretroAvInfo);
	_rb_define_module_function(m, "run_frame", libretroRunFrame);
	_rb_define_module_function(m, "frame_count", libretroFrameCount);
	_rb_define_module_function(m, "frame_size", libretroFrameSize);
	_rb_define_module_function(m, "frame_bitmap", libretroFrameBitmap);
	_rb_define_module_function(m, "blit", libretroBlit);
	_rb_define_module_function(m, "reset", libretroReset);
	_rb_define_module_function(m, "unload_game", libretroUnloadGame);
	_rb_define_module_function(m, "unload", libretroUnload);
	_rb_define_module_function(m, "audio_enabled=", libretroSetAudioEnabled);
	_rb_define_module_function(m, "audio_enabled", libretroAudioEnabled);
	_rb_define_module_function(m, "volume=", libretroSetVolume);
	_rb_define_module_function(m, "volume", libretroVolume);
	_rb_define_module_function(m, "audio_stats", libretroAudioStats);
	_rb_define_module_function(m, "memory", libretroMemory);
	_rb_define_module_function(m, "memory_size", libretroMemorySize);
	_rb_define_module_function(m, "write_memory", libretroWriteMemory);
	_rb_define_module_function(m, "save_state", libretroSaveState);
	_rb_define_module_function(m, "load_state", libretroLoadState);
	_rb_define_module_function(m, "set_option", libretroSetOption);
	_rb_define_module_function(m, "options", libretroOptions);
	_rb_define_module_function(m, "system_directory=", libretroSetSystemDir);
	_rb_define_module_function(m, "save_directory=", libretroSetSaveDir);

	static const struct { const char *name; int id; } buttons[] = {
		{"B", RETRO_DEVICE_ID_JOYPAD_B}, {"Y", RETRO_DEVICE_ID_JOYPAD_Y},
		{"SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT}, {"START", RETRO_DEVICE_ID_JOYPAD_START},
		{"UP", RETRO_DEVICE_ID_JOYPAD_UP}, {"DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN},
		{"LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT}, {"RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT},
		{"A", RETRO_DEVICE_ID_JOYPAD_A}, {"X", RETRO_DEVICE_ID_JOYPAD_X},
		{"L", RETRO_DEVICE_ID_JOYPAD_L}, {"R", RETRO_DEVICE_ID_JOYPAD_R},
		{"L2", RETRO_DEVICE_ID_JOYPAD_L2}, {"R2", RETRO_DEVICE_ID_JOYPAD_R2},
		{"L3", RETRO_DEVICE_ID_JOYPAD_L3}, {"R3", RETRO_DEVICE_ID_JOYPAD_R3},
	};
	for (const auto &b : buttons)
		rb_define_const(m, b.name, INT2NUM(b.id));

	rb_define_const(m, "MEMORY_SAVE_RAM", INT2NUM(RETRO_MEMORY_SAVE_RAM));
	rb_define_const(m, "MEMORY_RTC", INT2NUM(RETRO_MEMORY_RTC));
	rb_define_const(m, "MEMORY_SYSTEM_RAM", INT2NUM(RETRO_MEMORY_SYSTEM_RAM));
	rb_define_const(m, "MEMORY_VIDEO_RAM", INT2NUM(RETRO_MEMORY_VIDEO_RAM));
	rb_define_const(m, "API_VERSION", INT2NUM(RETRO_API_VERSION));
}
