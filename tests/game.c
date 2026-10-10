/* Stand-in for BrgGame-Steam.exe (or, renamed, for some other program). It loads
 * dinput8.dll from its own folder the way the game does, then checks which
 * plugins loaded, what the log says and that DirectInput still works.
 *
 *   game.exe plugins     lid-plugins holds test-a.dll, test-b.dll, a broken
 *                        c-broken.dll and files that are not plugins
 *   game.exe no-folder   there is no lid-plugins folder
 *   game.exe other-exe   the exe is not BrgGame-Steam.exe: nothing may load */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static int read_log(char *buf, size_t size)
{
    FILE *f = fopen("lid_loader.log", "rb");
    size_t n;
    buf[0] = 0;
    if (!f) return 0;
    n = fread(buf, 1, size - 1, f);
    buf[n] = 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    typedef HRESULT (WINAPI *create_fn)(HINSTANCE, DWORD, const GUID *, void **, void *);
    static const GUID IID_IDirectInput8W =
        {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    const char *mode = argc > 1 ? argv[1] : "";
    char order[256] = "", log[8192];
    int have_log;
    void *di = NULL;
    create_fn create;
    HMODULE dll;
    HRESULT hr;

    DeleteFileW(L"lid_loader.log");
    dll = LoadLibraryW(L"dinput8.dll");
    if (!dll) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    GetEnvironmentVariableA("LID_TEST_ORDER", order, sizeof order);
    have_log = read_log(log, sizeof log);
    printf("%s", log);
    printf("plugins loaded, in order: [%s]\n\n", order);

    if (strcmp(mode, "plugins") == 0) {
        check("both test plugins loaded, in name order", strcmp(order, "a,b") == 0);
        check("the log names each loaded plugin",
              strstr(log, "loaded test-a.dll") && strstr(log, "loaded test-b.dll"));
        check("the broken plugin is named and skipped", strstr(log, "could not load c-broken.dll") != NULL);
        check("files not ending in .dll are ignored",
              !strstr(log, "readme.txt") && !strstr(log, "test-c.dll.off"));
        check("a dll in a subfolder is ignored", !strstr(log, "nested.dll"));
        check("a copy of the loader is not loaded as a plugin", strstr(log, "skipped dinput8.dll") != NULL);
        check("the summary counts them", strstr(log, "2 plugin(s) loaded, 2 skipped") != NULL);
    } else if (strcmp(mode, "no-folder") == 0) {
        check("nothing loaded", order[0] == 0);
        check("the log says there is no plugin folder", strstr(log, "no lid-plugins folder") != NULL);
    } else if (strcmp(mode, "other-exe") == 0) {
        check("nothing loaded in another program", order[0] == 0);
        check("no log written in another program", !have_log);
    } else {
        printf("unknown mode %s\n", mode);
        return 2;
    }

    create = (create_fn)GetProcAddress(dll, "DirectInput8Create");
    hr = create ? create(GetModuleHandleW(NULL), 0x0800, &IID_IDirectInput8W, &di, NULL) : E_FAIL;
    check("DirectInput8Create passes through (S_OK)", hr == S_OK);
    if (di) {
        typedef ULONG (WINAPI *release_fn)(void *);
        ((release_fn)(*(void ***)di)[2])(di);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
