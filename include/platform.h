#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
/* -- if any of these macros are defined, they can cause naming conflicts with our code, so we undefine them here.
* This is a common practice when including Windows headers in C++ projects to avoid pollution of the global namespace with Windows-specific macros.
* if you have problems with windows macros, undefine them here i.e.
#ifdef DrawText
#undef DrawText;
#endif
*/
using affinity_mask_t = DWORD_PTR;
using native_handle_t = HANDLE;
#else
using affinity_mask_t = unsigned long long;
using native_handle_t = void*;
#endif