#include <windows.h>
#include "climbdiag.h"
static HMODULE g_module = nullptr;
static DWORD WINAPI Boot(LPVOID) { cm::Start(g_module); return 0; }
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE h = CreateThread(nullptr, 0, Boot, nullptr, 0, nullptr)) CloseHandle(h); // off the loader lock
    }
    else if (reason == DLL_PROCESS_DETACH)
        cm::Stop(reserved != nullptr);
    return TRUE;
}
