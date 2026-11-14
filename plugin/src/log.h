#pragma once
namespace cm::log
{
    enum Level { Off = 0, Info = 1, Verbose = 2 };
    void Open(const wchar_t* path, int level);
    void Close();
    int  LevelNow();
    void Write(int level, const char* fmt, ...);
}
#define LOGI(...) ::cm::log::Write(1, __VA_ARGS__)
#define LOGV(...) ::cm::log::Write(2, __VA_ARGS__)
