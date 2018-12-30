// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <windows.h>

#include "FFlib.h"


// TODO: reference additional headers your program requires here
#define BUFFER_SIZE 8192

#define EXPORT_API __declspec(dllexport) 

#ifdef _MSC_VER
#	define THREAD_LOCAL_VAR __declspec(thread)
#else
#	define THREAD_LOCAL_VAR __thread
#endif