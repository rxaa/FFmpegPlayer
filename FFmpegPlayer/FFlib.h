#pragma once
#include <windows.h>

#ifdef _WIN64
#pragma comment(lib,"SDL2/lib/x64/SDL2.lib")

#pragma comment(lib,"ffmpeg-4.1/lib64/avcodec.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/avformat.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/avutil.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/swresample.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/avdevice.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/postproc.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/swscale.lib")
#pragma comment(lib,"ffmpeg-4.1/lib64/avfilter.lib")


#else

#pragma comment(lib,"SDL2/lib/x86/SDL2.lib")

#pragma comment(lib,"ffmpeg-4.1/lib32/avcodec.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/avformat.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/avutil.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/swresample.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/avdevice.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/postproc.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/swscale.lib")
#pragma comment(lib,"ffmpeg-4.1/lib32/avfilter.lib")
#endif // _WIN64