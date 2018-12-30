#pragma once

#include "stdafx.h"


// set -vf 
EXPORT_API int WINAPI ffplay_set_vf(const char * cmd);

//set -af
EXPORT_API int WINAPI ffplay_set_af(const char * cmd);

//play file in 'hwndParent' window
EXPORT_API int WINAPI ffplay_start(const char * name, HWND hwndParent);

//stop and release all file
EXPORT_API int WINAPI ffplay_stop();

EXPORT_API int WINAPI ffplay_resize(int w, int h);

//1.disable video render 0.enable
EXPORT_API void WINAPI ffplay_set_stop_show(int val);

EXPORT_API int WINAPI ffplay_step_to_next_frame();

//open file success
EXPORT_API void WINAPI ffplay_on_success(void(*func)());

//playing finished
EXPORT_API void WINAPI ffplay_on_complete(void(*on_complete)());

EXPORT_API void WINAPI ffplay_on_error(void(*on_e)(const char * err));

// 0.stop 1.playing -1.pause
EXPORT_API int WINAPI ffplay_get_state();

EXPORT_API int WINAPI ffplay_toggle_pause();

//millisecond
EXPORT_API long long WINAPI ffplay_get_duration();

//millisecond
EXPORT_API long long WINAPI ffplay_get_position();

//millisecond
EXPORT_API int WINAPI ffplay_set_position(long long position);
