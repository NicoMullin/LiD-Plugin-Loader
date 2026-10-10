/*
 * dinput8.dll for Let It Die - loads the game's plugin DLLs.
 *
 * The game loads dinput8.dll from its own folder first. This file passes every
 * DirectInput call to the real dinput8.dll in the Windows system folder, and
 * when the game starts it loads every plugin in the lid-plugins folder next to
 * itself. Each plugin is an ordinary DLL that makes its own changes; this file
 * changes nothing in the game.
 *
 *   Binaries\Win64\dinput8.dll                 this loader
 *   Binaries\Win64\lid-plugins\lid-fighter.dll     a plugin
 *   Binaries\Win64\lid-plugins\lid-deathmetals.dll another plugin
 *
 * Rules:
 *   - Plugins load only inside BrgGame-Steam.exe. Loaded by any other program,
 *     this file only passes DirectInput through.
 *   - Only files ending in .dll, directly in lid-plugins, in name order. Rename
 *     a plugin (for example to lid-fighter.dll.off) to switch it off.
 *   - A plugin that fails to load is named in the log and skipped; the rest
 *     still load.
 *   - No network, no registry. The only file it writes is lid_loader.log next
 *     to itself.
 *   - Delete dinput8.dll and the lid-plugins folder and the game is exactly
 *     stock again.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "version.h"

#define PLUGIN_FOLDER L"lid-plugins"
#define MAX_PLUGINS   64

/* ------------------------------------------------------------------ log */

static wchar_t g_log_path[MAX_PATH];
static int g_log_started;

static void log_line(const char *fmt, ...)
{
    char msg[768];
    SYSTEMTIME t;
    va_list ap;
    int n;
    HANDLE f;
    DWORD written;

    if (!g_log_path[0])
        return;
    GetLocalTime(&t);
    n = snprintf(msg, sizeof msg, "%02u:%02u:%02u  ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    n += vsnprintf(msg + n, sizeof msg - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof msg - 3)
        n = (int)sizeof msg - 3;
    msg[n++] = '\r';
    msg[n++] = '\n';

    /* a fresh log each time the game starts */
    f = CreateFileW(g_log_path, g_log_started ? FILE_APPEND_DATA : GENERIC_WRITE,
                    FILE_SHARE_READ, NULL, g_log_started ? OPEN_ALWAYS : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    g_log_started = 1;
    WriteFile(f, msg, (DWORD)n, &written, NULL);
    CloseHandle(f);
}

/* ------------------------------------------------- the real dinput8.dll */

static HMODULE g_real;

static FARPROC real_proc(const char *name)
{
    if (!g_real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 16)
            return NULL;
        wcscat(path, L"\\dinput8.dll");
        g_real = LoadLibraryW(path);
        if (!g_real) {
            log_line("could not load the system dinput8.dll (error %lu)", GetLastError());
            return NULL;
        }
    }
    return GetProcAddress(g_real, name);
}

typedef HRESULT (WINAPI *create_fn)(HINSTANCE, DWORD, const GUID *, void **, void *);
typedef HRESULT (WINAPI *classobj_fn)(const GUID *, const GUID *, void **);
typedef HRESULT (WINAPI *void_fn)(void);

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, const GUID *riid,
                                        void **out, void *outer)
{
    create_fn f = (create_fn)real_proc("DirectInput8Create");
    return f ? f(inst, version, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllGetClassObject(const GUID *clsid, const GUID *riid, void **out)
{
    classobj_fn f = (classobj_fn)real_proc("DllGetClassObject");
    return f ? f(clsid, riid, out) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow(void)
{
    void_fn f = (void_fn)real_proc("DllCanUnloadNow");
    return f ? f() : S_FALSE;
}

HRESULT WINAPI Proxy_DllRegisterServer(void)
{
    void_fn f = (void_fn)real_proc("DllRegisterServer");
    return f ? f() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer(void)
{
    void_fn f = (void_fn)real_proc("DllUnregisterServer");
    return f ? f() : E_FAIL;
}

/* -------------------------------------------------------------- plugins */

static int ends_with_dll(const wchar_t *name)
{
    size_t n = wcslen(name);
    return n > 4 && _wcsicmp(name + n - 4, L".dll") == 0;
}

static int by_name(const void *a, const void *b)
{
    return _wcsicmp((const wchar_t *)a, (const wchar_t *)b);
}

static void load_plugins(const wchar_t *folder)
{
    static wchar_t names[MAX_PLUGINS][MAX_PATH];
    wchar_t pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    int count = 0, loaded = 0, skipped = 0, i;

    if (_snwprintf(pattern, MAX_PATH, L"%ls\\*", folder) < 0) {
        log_line("the plugin folder's path is too long - no plugins loaded");
        return;
    }
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        log_line("no %ls folder next to dinput8.dll - no plugins to load", PLUGIN_FOLDER);
        return;
    }
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!ends_with_dll(found.cFileName))
            continue;
        if (_wcsicmp(found.cFileName, L"dinput8.dll") == 0) {
            log_line("skipped %ls: a copy of the loader, not a plugin", found.cFileName);
            skipped++;
            continue;
        }
        if (count == MAX_PLUGINS) {
            log_line("skipped %ls: more than %d plugins", found.cFileName, MAX_PLUGINS);
            skipped++;
            continue;
        }
        wcscpy(names[count++], found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);

    qsort(names, (size_t)count, sizeof names[0], by_name);
    for (i = 0; i < count; i++) {
        HMODULE m;
        if (_snwprintf(path, MAX_PATH, L"%ls\\%ls", folder, names[i]) < 0) {
            log_line("skipped %ls: path too long", names[i]);
            skipped++;
            continue;
        }
        /* the plugin's own folder is searched for anything it needs */
        m = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m) {
            log_line("loaded %ls", names[i]);
            loaded++;
        } else {
            log_line("could not load %ls (error %lu) - skipped", names[i], GetLastError());
            skipped++;
        }
    }
    log_line("%d plugin(s) loaded, %d skipped", loaded, skipped);
}

/* ---------------------------------------------------------------- start */

static int running_in_the_game(void)
{
    wchar_t exe[MAX_PATH];
    const wchar_t *name;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    return _wcsicmp(name, L"BrgGame-Steam.exe") == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t folder[MAX_PATH], *slash;
        DWORD n;

        DisableThreadLibraryCalls(inst);
        n = GetModuleFileNameW(inst, folder, MAX_PATH);
        slash = (n && n < MAX_PATH - 32) ? wcsrchr(folder, L'\\') : NULL;
        if (!slash)
            return TRUE;        /* no usable path: pass-through only, no log */
        *slash = 0;
        _snwprintf(g_log_path, MAX_PATH, L"%ls\\lid_loader.log", folder);

        if (!running_in_the_game())
            return TRUE;        /* some other program - pass-through only */
        log_line("LiD Plugin Loader " LID_LOADER_VERSION " loaded");
        wcscat(folder, L"\\" PLUGIN_FOLDER);
        load_plugins(folder);
    } else if (reason == DLL_PROCESS_DETACH && reserved == NULL && g_real) {
        FreeLibrary(g_real);
    }
    return TRUE;
}
