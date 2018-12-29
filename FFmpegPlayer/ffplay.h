#pragma once

#include "stdafx.h"

EXPORT_API int WINAPI ffplay_set_vf(const char * cmd);
EXPORT_API int WINAPI ffplay_set_af(const char * cmd);


EXPORT_API int WINAPI ffplay_start(int argc, const char * name, HWND hwndParent);

EXPORT_API int WINAPI ffplay_stop();

EXPORT_API int WINAPI ffplay_resize(int w, int h);

EXPORT_API int WINAPI ffplay_set_stop_show(int w);

EXPORT_API int WINAPI ffplay_step_to_next_frame();

EXPORT_API int WINAPI ffplay_on_complete(void(*on_complete)());

// 0.stop 1.playing -1.pause
EXPORT_API int WINAPI ffplay_get_state();

EXPORT_API int WINAPI ffplay_toggle_pause();

EXPORT_API long long WINAPI ffplay_get_duration();

EXPORT_API long long WINAPI ffplay_get_position();

EXPORT_API int WINAPI ffplay_set_position(long long position);