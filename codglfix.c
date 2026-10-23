// codglfix.dll — lets Call of Duty (2003) start missions on modern GPUs.
//
// The game copies the driver's GL_EXTENSIONS string into a fixed-size buffer; modern
// drivers report thousands of characters and the game crashes as a mission loads.
// This DLL, loaded into the game by the PermaCorpse launcher, hot-patches
// opengl32!glGetString so GL_EXTENSIONS returns only the extensions the game knows
// how to use (and the driver really has). Every other call goes to the real function.
//
// Optional: CoDGLFix.ini next to this file with "borderless=1" under [fix] makes a windowed game
// borderless (no title bar, covers the monitor). Off by default.
//
// Build (32-bit): zig cc -target x86-windows-gnu -shared -O2 -o codglfix.dll codglfix.c

#include <windows.h>
#include <string.h>

#define GL_EXTENSIONS 0x1F03
typedef const unsigned char *(WINAPI *GetStringFn)(unsigned int);

static GetStringFn real_glGetString;   // continues after the patched "mov edi,edi"
static char trimmed[2048];
static volatile LONG built;

// Extensions an id Tech 3 / Call of Duty renderer looks for, in the order it checks them.
static const char *wanted[] = {
    "GL_ARB_multitexture", "GL_EXT_texture_env_add", "GL_ARB_texture_env_add",
    "GL_ARB_texture_compression", "GL_EXT_texture_compression_s3tc", "GL_S3_s3tc",
    "GL_EXT_compiled_vertex_array", "GL_EXT_draw_range_elements",
    "GL_ARB_texture_cube_map", "GL_EXT_texture_cube_map",
    "GL_ARB_texture_env_combine", "GL_EXT_texture_env_combine", "GL_NV_texture_env_combine4",
    "GL_ARB_texture_env_dot3", "GL_EXT_texture_env_dot3",
    "GL_EXT_texture_filter_anisotropic", "GL_EXT_texture_lod_bias",
    "GL_ARB_texture_border_clamp", "GL_EXT_texture_edge_clamp", "GL_SGIS_texture_edge_clamp",
    "GL_NV_fog_distance", "GL_EXT_bgra", "GL_EXT_abgr", "GL_EXT_packed_pixels",
    "GL_EXT_stencil_wrap", "GL_EXT_separate_specular_color", "GL_EXT_secondary_color",
    "GL_EXT_point_parameters", "GL_ARB_point_parameters", "GL_SGIS_generate_mipmap",
    "GL_EXT_rescale_normal", "GL_ARB_vertex_buffer_object", "GL_NV_register_combiners",
    "GL_NV_vertex_array_range", "GL_ARB_vertex_program", "GL_ARB_fragment_program",
    0
};

static int has_ext(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    while ((p = strstr(p, name)) != 0) {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return 1;
        p += n;
    }
    return 0;
}

static const unsigned char *WINAPI hooked_glGetString(unsigned int name)
{
    const unsigned char *s = real_glGetString(name);
    if (name != GL_EXTENSIONS || s == 0)
        return s;
    if (!built) {
        size_t len = 0;
        trimmed[0] = 0;
        for (int i = 0; wanted[i]; i++) {
            size_t n = strlen(wanted[i]);
            if (has_ext((const char *)s, wanted[i]) && len + n + 2 < sizeof(trimmed)) {
                memcpy(trimmed + len, wanted[i], n);
                len += n;
                trimmed[len++] = ' ';
                trimmed[len] = 0;
            }
        }
        built = 1;
    }
    return (const unsigned char *)trimmed;
}

// Hot-patch: the function starts with "mov edi,edi" (8B FF) and has 5 padding bytes
// before it. Put "jmp hook" in the padding and "jmp $-5" over the mov.
static int patch(void)
{
    HMODULE gl = LoadLibraryA("opengl32.dll");   // the same system DLL the game loads
    if (!gl) return 0;
    unsigned char *f = (unsigned char *)GetProcAddress(gl, "glGetString");
    if (!f || f[0] != 0x8B || f[1] != 0xFF) return 0;
    for (int i = 1; i <= 5; i++)
        if (f[-i] != 0xCC && f[-i] != 0x90) return 0;

    DWORD old;
    if (!VirtualProtect(f - 5, 7, PAGE_EXECUTE_READWRITE, &old)) return 0;
    real_glGetString = (GetStringFn)(f + 2);
    f[-5] = 0xE9;
    *(DWORD *)(f - 4) = (DWORD)((unsigned char *)hooked_glGetString - f);
    *(volatile WORD *)f = 0xF9EB;                // jmp short -7 (to f-5), written last
    VirtualProtect(f - 5, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f - 5, 7);
    return 1;
}

// Optional borderless windowed (CoDGLFix.ini): whenever the game's (windowed) main
// window has a title bar or border, strip them and cover the monitor it is on. The
// renderer recreates its window on every level load, so keep watching.
static BOOL CALLBACK find_game_window(HWND w, LPARAM out)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w) && GetWindow(w, GW_OWNER) == 0) {
        *(HWND *)out = w;
        return FALSE;
    }
    return TRUE;
}

static void make_borderless(void)
{
    HWND w = 0;
    EnumWindows(find_game_window, (LPARAM)&w);
    if (!w) return;
    LONG style = GetWindowLongA(w, GWL_STYLE);
    if (!(style & (WS_CAPTION | WS_THICKFRAME)))
        return;   // already borderless (or a real fullscreen window)
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    SetWindowLongA(w, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) | WS_POPUP);
    SetWindowPos(w, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static HINSTANCE this_module;

static int ini_borderless(void)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(this_module, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    char *slash = strrchr(path, '\\');
    if (!slash || (size_t)(slash - path) + 16 >= MAX_PATH) return 0;
    strcpy(slash + 1, "CoDGLFix.ini");
    return GetPrivateProfileIntA("fix", "borderless", 0, path) != 0;
}

static DWORD WINAPI patch_thread(LPVOID unused)
{
    (void)unused;
    patch();
    if (!ini_borderless())
        return 0;
    for (;;) {
        make_borderless();
        Sleep(500);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        this_module = inst;
        HANDLE t = CreateThread(0, 0, patch_thread, 0, 0, 0);   // no LoadLibrary under the loader lock
        if (t) CloseHandle(t);
    }
    return TRUE;
}

// Lets the launcher confirm the patch went in (exported for GetProcAddress checks).
__declspec(dllexport) int codglfix_patched(void)
{
    return real_glGetString != 0;
}
