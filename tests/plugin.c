/* A test plugin. Built twice, as test-a.dll and test-b.dll: when loaded it adds
 * its name to the LID_TEST_ORDER environment variable, so the stand-in game can
 * see which plugins loaded and in what order. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef PLUGIN_NAME
#error PLUGIN_NAME must be set
#endif

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t order[256] = L"";
        GetEnvironmentVariableW(L"LID_TEST_ORDER", order, 200);
        if (order[0])
            lstrcatW(order, L",");
        lstrcatW(order, PLUGIN_NAME);
        SetEnvironmentVariableW(L"LID_TEST_ORDER", order);
    }
    return TRUE;
}
