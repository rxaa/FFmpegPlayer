// FFmpegPlayer.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "ffplay.h"
#include "SDL2/include/SDL.h"

#ifdef _DEBUG
#undef main


//自定义消息循环相应函数
LRESULT CALLBACK myWndProc(HWND hWnd,
	UINT Msg,
	WPARAM wParam,
	LPARAM lParam)
{
	switch (Msg)
	{
	case WM_CLOSE:
		if (MessageBox(hWnd, "close window？", "info!", MB_OKCANCEL) == IDOK)
			DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_SIZE: //改变窗口消息
	{
		// extract size info
		int width = LOWORD(lParam);
		int height = HIWORD(lParam);
		ffplay_resize(width, height);
	}
	default:
		return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

int main(int argc, char *argv[]) {



	SetProcessDPIAware();

	HINSTANCE hInstance = 0;

	WNDCLASS wndcls;
	wndcls.cbClsExtra = 0;
	wndcls.cbWndExtra = 0;
	wndcls.hbrBackground = (HBRUSH)GetStockObject(WHITE_PEN);
	wndcls.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndcls.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndcls.hInstance = hInstance;
	wndcls.lpfnWndProc = myWndProc;
	wndcls.lpszClassName = "123";
	wndcls.lpszMenuName = NULL;
	wndcls.style = CS_HREDRAW | CS_VREDRAW;

	RegisterClass(&wndcls);

	HWND hWnd = CreateWindow("123", "title", WS_OVERLAPPEDWINDOW
		| WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);


	ffplay_set_vf("vflip");
	ffplay_start( "D:/video test/[MMD]Satisfaction.mp4", hWnd);


	//消息循环
	MSG Msg;
	while (GetMessage(&Msg, hWnd, NULL, NULL))
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}

	system("pause");
	return 0;
}

#else

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
#endif // DEBUG


