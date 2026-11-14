#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
namespace cm::log
{
    static HANDLE g_file = INVALID_HANDLE_VALUE;
    static int g_level = 1;
    static CRITICAL_SECTION g_cs;
    static bool g_csInit = false;
    void Open(const wchar_t* path, int level)
    {
        if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
        g_level = level;
        if (level <= 0) return;
        g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    void Close()
    {
        if (!g_csInit) return;
        EnterCriticalSection(&g_cs);
        if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
        LeaveCriticalSection(&g_cs);
    }
    int LevelNow() { return g_level; }
    void Write(int level, const char* fmt, ...)
    {
        if (level > g_level || g_file == INVALID_HANDLE_VALUE || !g_csInit) return;
        char buf[1536];
        SYSTEMTIME st; GetLocalTime(&st);
        int n = snprintf(buf, sizeof buf, "%02u:%02u:%02u.%03u [%s] ", st.wHour, st.wMinute, st.wSecond,
                         st.wMilliseconds, level == 1 ? "info" : "verb");
        va_list ap; va_start(ap, fmt);
        int m = vsnprintf(buf + n, sizeof buf - n - 3, fmt, ap);
        va_end(ap);
        if (m < 0) m = 0;
        n += (m < (int)(sizeof buf - n - 3)) ? m : (int)(sizeof buf - n - 3);
        buf[n++] = '\r'; buf[n++] = '\n';
        EnterCriticalSection(&g_cs);
        DWORD w; WriteFile(g_file, buf, (DWORD)n, &w, nullptr);
        FlushFileBuffers(g_file);
        LeaveCriticalSection(&g_cs);
    }
}
