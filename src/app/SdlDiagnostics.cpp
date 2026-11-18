// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// SDL API floor: only functions available in SDL 2.0.18 (see platform/AudioHost.h).

#include "app/SdlDiagnostics.h"

#include <SDL.h>

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace audreybrick
{
namespace
{
// Appends with snprintf semantics; never overruns, keeps `at` at the NUL.
void append(char* out, std::size_t size, std::size_t& at, const char* fmt, ...) noexcept AB_PRINTF_FORMAT(4, 5);
void append(char* out, std::size_t size, std::size_t& at, const char* fmt, ...) noexcept
{
    if(out == nullptr || size == 0 || at + 1 >= size)
        return;
    std::va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(out + at, size - at, fmt, args);
    va_end(args);
    if(n > 0)
        at += static_cast<std::size_t>(n) < size - at ? static_cast<std::size_t>(n) : size - at - 1;
}

const char* orNone(const char* s) noexcept
{
    return s != nullptr ? s : "(none)";
}

const char* controllerButtonName(int b) noexcept
{
    const char* n = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(b));
    return n != nullptr ? n : "?";
}

const char* controllerAxisName(int a) noexcept
{
    const char* n = SDL_GameControllerGetStringForAxis(static_cast<SDL_GameControllerAxis>(a));
    return n != nullptr ? n : "?";
}

const char* windowEventName(Uint8 e) noexcept
{
    switch(e)
    {
        case SDL_WINDOWEVENT_SHOWN: return "shown";
        case SDL_WINDOWEVENT_HIDDEN: return "hidden";
        case SDL_WINDOWEVENT_EXPOSED: return "exposed";
        case SDL_WINDOWEVENT_MOVED: return "moved";
        case SDL_WINDOWEVENT_RESIZED: return "resized";
        case SDL_WINDOWEVENT_SIZE_CHANGED: return "size changed";
        case SDL_WINDOWEVENT_MINIMIZED: return "minimized";
        case SDL_WINDOWEVENT_MAXIMIZED: return "maximized";
        case SDL_WINDOWEVENT_RESTORED: return "restored";
        case SDL_WINDOWEVENT_ENTER: return "mouse entered";
        case SDL_WINDOWEVENT_LEAVE: return "mouse left";
        case SDL_WINDOWEVENT_FOCUS_GAINED: return "focus gained";
        case SDL_WINDOWEVENT_FOCUS_LOST: return "focus lost";
        case SDL_WINDOWEVENT_CLOSE: return "close requested";
        case SDL_WINDOWEVENT_TAKE_FOCUS: return "take focus";
        case SDL_WINDOWEVENT_HIT_TEST: return "hit test";
        case SDL_WINDOWEVENT_ICCPROF_CHANGED: return "ICC profile changed";
        case SDL_WINDOWEVENT_DISPLAY_CHANGED: return "display changed";
        default: return "unknown";
    }
}

void appendRendererFlags(char* out, std::size_t size, std::size_t& at, Uint32 flags) noexcept
{
    append(out, size, at, "0x%x (", static_cast<unsigned>(flags));
    bool any = false;
    auto f   = [&](Uint32 bit, const char* name) {
        if((flags & bit) != 0)
        {
            append(out, size, at, "%s%s", any ? " " : "", name);
            any = true;
        }
    };
    f(SDL_RENDERER_SOFTWARE, "software");
    f(SDL_RENDERER_ACCELERATED, "accelerated");
    f(SDL_RENDERER_PRESENTVSYNC, "vsync");
    f(SDL_RENDERER_TARGETTEXTURE, "target-texture");
    append(out, size, at, ")");
}

void appendWindowFlags(char* out, std::size_t size, std::size_t& at, Uint32 flags) noexcept
{
    append(out, size, at, "0x%x (", static_cast<unsigned>(flags));
    bool any = false;
    auto f   = [&](Uint32 bit, const char* name) {
        if((flags & bit) == bit)
        {
            append(out, size, at, "%s%s", any ? " " : "", name);
            any = true;
        }
    };
    if((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP)
    {
        append(out, size, at, "fullscreen-desktop");
        any = true;
    }
    else
    {
        f(SDL_WINDOW_FULLSCREEN, "fullscreen");
    }
    f(SDL_WINDOW_OPENGL, "opengl");
    f(SDL_WINDOW_SHOWN, "shown");
    f(SDL_WINDOW_HIDDEN, "hidden");
    f(SDL_WINDOW_BORDERLESS, "borderless");
    f(SDL_WINDOW_RESIZABLE, "resizable");
    f(SDL_WINDOW_INPUT_FOCUS, "input-focus");
    f(SDL_WINDOW_MOUSE_FOCUS, "mouse-focus");
    f(SDL_WINDOW_ALLOW_HIGHDPI, "highdpi");
    f(SDL_WINDOW_METAL, "metal");
    f(SDL_WINDOW_VULKAN, "vulkan");
    append(out, size, at, ")");
}
} // namespace

const char* eventTypeName(Uint32 type) noexcept
{
    switch(type)
    {
        case SDL_QUIT: return "SDL_QUIT";
        case SDL_APP_TERMINATING: return "SDL_APP_TERMINATING";
        case SDL_APP_LOWMEMORY: return "SDL_APP_LOWMEMORY";
        case SDL_APP_WILLENTERBACKGROUND: return "SDL_APP_WILLENTERBACKGROUND";
        case SDL_APP_DIDENTERBACKGROUND: return "SDL_APP_DIDENTERBACKGROUND";
        case SDL_APP_WILLENTERFOREGROUND: return "SDL_APP_WILLENTERFOREGROUND";
        case SDL_APP_DIDENTERFOREGROUND: return "SDL_APP_DIDENTERFOREGROUND";
        case SDL_LOCALECHANGED: return "SDL_LOCALECHANGED";
        case SDL_DISPLAYEVENT: return "SDL_DISPLAYEVENT";
        case SDL_WINDOWEVENT: return "SDL_WINDOWEVENT";
        case SDL_SYSWMEVENT: return "SDL_SYSWMEVENT";
        case SDL_KEYDOWN: return "SDL_KEYDOWN";
        case SDL_KEYUP: return "SDL_KEYUP";
        case SDL_TEXTEDITING: return "SDL_TEXTEDITING";
        case SDL_TEXTINPUT: return "SDL_TEXTINPUT";
        case SDL_KEYMAPCHANGED: return "SDL_KEYMAPCHANGED";
        case SDL_MOUSEMOTION: return "SDL_MOUSEMOTION";
        case SDL_MOUSEBUTTONDOWN: return "SDL_MOUSEBUTTONDOWN";
        case SDL_MOUSEBUTTONUP: return "SDL_MOUSEBUTTONUP";
        case SDL_MOUSEWHEEL: return "SDL_MOUSEWHEEL";
        case SDL_JOYAXISMOTION: return "SDL_JOYAXISMOTION";
        case SDL_JOYBALLMOTION: return "SDL_JOYBALLMOTION";
        case SDL_JOYHATMOTION: return "SDL_JOYHATMOTION";
        case SDL_JOYBUTTONDOWN: return "SDL_JOYBUTTONDOWN";
        case SDL_JOYBUTTONUP: return "SDL_JOYBUTTONUP";
        case SDL_JOYDEVICEADDED: return "SDL_JOYDEVICEADDED";
        case SDL_JOYDEVICEREMOVED: return "SDL_JOYDEVICEREMOVED";
        case SDL_CONTROLLERAXISMOTION: return "SDL_CONTROLLERAXISMOTION";
        case SDL_CONTROLLERBUTTONDOWN: return "SDL_CONTROLLERBUTTONDOWN";
        case SDL_CONTROLLERBUTTONUP: return "SDL_CONTROLLERBUTTONUP";
        case SDL_CONTROLLERDEVICEADDED: return "SDL_CONTROLLERDEVICEADDED";
        case SDL_CONTROLLERDEVICEREMOVED: return "SDL_CONTROLLERDEVICEREMOVED";
        case SDL_CONTROLLERDEVICEREMAPPED: return "SDL_CONTROLLERDEVICEREMAPPED";
        case SDL_CONTROLLERTOUCHPADDOWN: return "SDL_CONTROLLERTOUCHPADDOWN";
        case SDL_CONTROLLERTOUCHPADMOTION: return "SDL_CONTROLLERTOUCHPADMOTION";
        case SDL_CONTROLLERTOUCHPADUP: return "SDL_CONTROLLERTOUCHPADUP";
        case SDL_CONTROLLERSENSORUPDATE: return "SDL_CONTROLLERSENSORUPDATE";
        case SDL_FINGERDOWN: return "SDL_FINGERDOWN";
        case SDL_FINGERUP: return "SDL_FINGERUP";
        case SDL_FINGERMOTION: return "SDL_FINGERMOTION";
        case SDL_DOLLARGESTURE: return "SDL_DOLLARGESTURE";
        case SDL_DOLLARRECORD: return "SDL_DOLLARRECORD";
        case SDL_MULTIGESTURE: return "SDL_MULTIGESTURE";
        case SDL_CLIPBOARDUPDATE: return "SDL_CLIPBOARDUPDATE";
        case SDL_DROPFILE: return "SDL_DROPFILE";
        case SDL_DROPTEXT: return "SDL_DROPTEXT";
        case SDL_DROPBEGIN: return "SDL_DROPBEGIN";
        case SDL_DROPCOMPLETE: return "SDL_DROPCOMPLETE";
        case SDL_AUDIODEVICEADDED: return "SDL_AUDIODEVICEADDED";
        case SDL_AUDIODEVICEREMOVED: return "SDL_AUDIODEVICEREMOVED";
        case SDL_SENSORUPDATE: return "SDL_SENSORUPDATE";
        case SDL_RENDER_TARGETS_RESET: return "SDL_RENDER_TARGETS_RESET";
        case SDL_RENDER_DEVICE_RESET: return "SDL_RENDER_DEVICE_RESET";
        default: return nullptr;
    }
}

std::size_t describeEvent(char* out, std::size_t size, const SDL_Event& e) noexcept
{
    if(out == nullptr || size == 0)
        return 0;
    out[0]         = '\0';
    std::size_t at = 0;
    switch(e.type)
    {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            const SDL_Scancode sc   = e.key.keysym.scancode;
            const char*        name = SDL_GetScancodeName(sc);
            append(out, size, at, "key scancode %d '%s' %s, keycode 0x%x, mod 0x%04x%s", static_cast<int>(sc),
                   (name != nullptr && name[0] != '\0') ? name : "?", e.type == SDL_KEYDOWN ? "down" : "up",
                   static_cast<unsigned>(e.key.keysym.sym), static_cast<unsigned>(e.key.keysym.mod),
                   e.key.repeat != 0 ? " (repeat)" : "");
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            append(out, size, at, "controller button '%s' (%d) %s, instance %d", controllerButtonName(e.cbutton.button),
                   static_cast<int>(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up",
                   static_cast<int>(e.cbutton.which));
            break;
        case SDL_CONTROLLERAXISMOTION:
            append(out, size, at, "controller axis '%s' (%d) = %d, instance %d", controllerAxisName(e.caxis.axis),
                   static_cast<int>(e.caxis.axis), static_cast<int>(e.caxis.value), static_cast<int>(e.caxis.which));
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            append(out, size, at, "joystick button %d %s, instance %d", static_cast<int>(e.jbutton.button),
                   e.type == SDL_JOYBUTTONDOWN ? "down" : "up", static_cast<int>(e.jbutton.which));
            break;
        case SDL_JOYAXISMOTION:
            append(out, size, at, "joystick axis %d = %d, instance %d", static_cast<int>(e.jaxis.axis),
                   static_cast<int>(e.jaxis.value), static_cast<int>(e.jaxis.which));
            break;
        case SDL_JOYHATMOTION:
            append(out, size, at, "joystick hat %d = 0x%x, instance %d", static_cast<int>(e.jhat.hat),
                   static_cast<unsigned>(e.jhat.value), static_cast<int>(e.jhat.which));
            break;
        case SDL_WINDOWEVENT:
            append(out, size, at, "window event %s (%d), data %d %d", windowEventName(e.window.event),
                   static_cast<int>(e.window.event), static_cast<int>(e.window.data1), static_cast<int>(e.window.data2));
            break;
        default:
        {
            const char* n = eventTypeName(e.type);
            if(n != nullptr)
                append(out, size, at, "%s", n);
            else
                append(out, size, at, "event type 0x%x", static_cast<unsigned>(e.type));
            break;
        }
    }
    return at;
}

const char* quitReason(const SDL_Event& e) noexcept
{
    switch(e.type)
    {
        case SDL_QUIT: return "SDL_QUIT (a system request: SIGTERM, SIGINT or the last window closed)";
        case SDL_WINDOWEVENT: return "window closed";
        case SDL_KEYDOWN:
            if(e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                return "Escape key";
            if(e.key.keysym.scancode == SDL_SCANCODE_F4)
                return "Alt+F4 (the KNULLI exit hotkey for shell ports)";
            return "a quit key";
        default: return "a quit event";
    }
}

void logSdlInfo(DiagLog& log, double t)
{
    SDL_version compiled;
    SDL_version linked;
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linked);
    log.line(t, "sdl: compiled against %d.%d.%d, linked %d.%d.%d, revision %s", compiled.major, compiled.minor,
             compiled.patch, linked.major, linked.minor, linked.patch, orNone(SDL_GetRevision()));
    log.line(t, "system: platform %s, %d CPU(s), %d MB RAM", orNone(SDL_GetPlatform()), SDL_GetCPUCount(),
             SDL_GetSystemRAM());
    char        buf[512];
    std::size_t at = 0;
    for(int i = 0; i < SDL_GetNumVideoDrivers(); ++i)
        append(buf, sizeof buf, at, "%s%s", i == 0 ? "" : " ", orNone(SDL_GetVideoDriver(i)));
    log.line(t, "sdl video drivers built in: %s", at == 0 ? "(none)" : buf);
    at = 0;
    buf[0] = '\0';
    for(int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
        append(buf, sizeof buf, at, "%s%s", i == 0 ? "" : " ", orNone(SDL_GetAudioDriver(i)));
    log.line(t, "sdl audio drivers built in: %s", at == 0 ? "(none)" : buf);
    const char* vars[] = {"SDL_VIDEODRIVER", "SDL_AUDIODRIVER", "SDL_RENDER_DRIVER", "SDL_RENDER_VSYNC",
                          "SDL_AUDIO_SAMPLES", "AUDREY_BUFFER", "LD_LIBRARY_PATH", "XDG_RUNTIME_DIR",
                          "PIPEWIRE_RUNTIME_DIR", "HOME"};
    for(const char* v : vars)
    {
        const char* val = SDL_getenv(v);
        log.line(t, "env %s=%s", v, val != nullptr ? val : "(not set)");
    }
}

namespace
{
// Writes a line to the log and, if `echo` is given, the same text to that stream
// ("audreybrick: <text>"), so that video facts also reach stdout.log.
struct Tee
{
    DiagLog&   log;
    std::FILE* echo;

    void line(double t, const char* fmt, ...) noexcept AB_PRINTF_FORMAT(3, 4)
    {
        char         buf[kLogMaxText + 1];
        std::va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);
        log.line(t, "%s", buf);
        if(echo != nullptr)
        {
            std::fprintf(echo, "audreybrick: %s\n", buf);
            std::fflush(echo);
        }
    }
};
} // namespace

void logVideoEnvironment(DiagLog& log, double t, std::FILE* echo)
{
    Tee         out{log, echo};
    const char* drv = SDL_getenv("SDL_VIDEODRIVER");
    out.line(t, "video environment: SDL_VIDEODRIVER=%s", drv != nullptr ? drv : "(not set)");
    char        buf[512];
    std::size_t at = 0;
    buf[0]         = '\0';
    for(int i = 0; i < SDL_GetNumVideoDrivers(); ++i)
        append(buf, sizeof buf, at, "%s%s", i == 0 ? "" : " ", orNone(SDL_GetVideoDriver(i)));
    out.line(t, "video environment: video drivers compiled into the linked SDL: %s", at == 0 ? "(none)" : buf);
    // The Mali framebuffer driver opens /dev/fb0 and reads /sys/class/graphics/fb0/modes.
    struct stat st;
    if(::stat("/dev/fb0", &st) != 0)
        out.line(t, "video environment: /dev/fb0 does not exist (%s)", std::strerror(errno));
    else
        out.line(t, "video environment: /dev/fb0 exists, %s, mode %o, owner %u:%u, readable %s, writable %s",
                 S_ISCHR(st.st_mode) ? "character device" : "NOT a character device",
                 static_cast<unsigned>(st.st_mode & 07777), static_cast<unsigned>(st.st_uid),
                 static_cast<unsigned>(st.st_gid), ::access("/dev/fb0", R_OK) == 0 ? "yes" : "NO",
                 ::access("/dev/fb0", W_OK) == 0 ? "yes" : "NO");
    std::FILE* m = std::fopen("/sys/class/graphics/fb0/modes", "r");
    if(m == nullptr)
    {
        out.line(t, "video environment: /sys/class/graphics/fb0/modes not readable (%s)", std::strerror(errno));
    }
    else
    {
        char line[160];
        int  n = 0;
        while(n < 8 && std::fgets(line, sizeof line, m) != nullptr)
        {
            line[std::strcspn(line, "\r\n")] = '\0';
            out.line(t, "video environment: fb0 mode: %s", line);
            ++n;
        }
        if(n == 0)
            out.line(t, "video environment: /sys/class/graphics/fb0/modes is empty");
        std::fclose(m);
    }
}

void logVideoDisplays(DiagLog& log, double t, std::FILE* echo)
{
    Tee out{log, echo};
    if(SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        out.line(t, "video: not initialised, no display information");
        return;
    }
    const int displays = SDL_GetNumVideoDisplays();
    out.line(t, "video: driver %s, %d display(s)%s%s", orNone(SDL_GetCurrentVideoDriver()), displays,
             displays < 0 ? ": " : "", displays < 0 ? SDL_GetError() : "");
    for(int d = 0; d < displays && d < 4; ++d)
    {
        SDL_DisplayMode mode;
        SDL_zero(mode);
        if(SDL_GetCurrentDisplayMode(d, &mode) == 0)
            out.line(t, "display %d '%s': current mode %dx%d @ %d Hz, %s", d, orNone(SDL_GetDisplayName(d)), mode.w,
                     mode.h, mode.refresh_rate, orNone(SDL_GetPixelFormatName(mode.format)));
        else
            out.line(t, "display %d: current mode unknown (%s)", d, SDL_GetError());
        SDL_zero(mode);
        if(SDL_GetDesktopDisplayMode(d, &mode) == 0)
            out.line(t, "display %d: desktop mode %dx%d @ %d Hz", d, mode.w, mode.h, mode.refresh_rate);
        SDL_Rect bounds;
        SDL_zero(bounds);
        if(SDL_GetDisplayBounds(d, &bounds) == 0)
            out.line(t, "display %d: bounds %d,%d %dx%d", d, bounds.x, bounds.y, bounds.w, bounds.h);
        const int modes = SDL_GetNumDisplayModes(d);
        for(int i = 0; i < modes && i < 16; ++i)
        {
            SDL_zero(mode);
            if(SDL_GetDisplayMode(d, i, &mode) == 0)
                out.line(t, "display %d: mode %d: %dx%d @ %d Hz, %s", d, i, mode.w, mode.h, mode.refresh_rate,
                         orNone(SDL_GetPixelFormatName(mode.format)));
        }
    }
}

void logVideoInfo(DiagLog& log, double t, SDL_Window* window, SDL_Renderer* renderer, std::FILE* echo)
{
    Tee out{log, echo};
    logVideoDisplays(log, t, echo);

    char        buf[512];
    std::size_t at = 0;
    if(window != nullptr)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        appendWindowFlags(buf, sizeof buf, at, SDL_GetWindowFlags(window));
        out.line(t, "window: %dx%d, flags %s, on display %d", w, h, buf, SDL_GetWindowDisplayIndex(window));
    }
    if(renderer != nullptr)
    {
        SDL_RendererInfo info;
        SDL_zero(info);
        if(SDL_GetRendererInfo(renderer, &info) == 0)
        {
            at = 0;
            appendRendererFlags(buf, sizeof buf, at, info.flags);
            out.line(t, "renderer: %s, flags %s, max texture %dx%d", orNone(info.name), buf, info.max_texture_width,
                     info.max_texture_height);
            out.line(t, "renderer: vsync %s", (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0
                                                  ? "reported on (whether presenting really waits shows in the fps of the stats lines)"
                                                  : "off (the frame rate is capped at 60)");
        }
        int ow = 0;
        int oh = 0;
        if(SDL_GetRendererOutputSize(renderer, &ow, &oh) == 0)
            out.line(t, "renderer: output size %dx%d", ow, oh);
        int lw = 0;
        int lh = 0;
        SDL_RenderGetLogicalSize(renderer, &lw, &lh);
        out.line(t, "renderer: logical size %dx%d, integer scaling %s", lw, lh,
                 SDL_RenderGetIntegerScale(renderer) == SDL_TRUE ? "on" : "off");
    }
    at     = 0;
    buf[0] = '\0';
    for(int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
    {
        SDL_RendererInfo info;
        SDL_zero(info);
        if(SDL_GetRenderDriverInfo(i, &info) == 0)
            append(buf, sizeof buf, at, "%s%s", i == 0 ? "" : " ", orNone(info.name));
    }
    out.line(t, "render drivers available: %s", at == 0 ? "(none)" : buf);
}

void logAudioDevices(DiagLog& log, double t)
{
    char        buf[512];
    std::size_t at = 0;
    buf[0]         = '\0';
    for(int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
        append(buf, sizeof buf, at, "%s%s", i == 0 ? "" : " ", orNone(SDL_GetAudioDriver(i)));
    log.line(t, "audio drivers built in: %s; in use: %s", at == 0 ? "(none)" : buf,
             orNone(SDL_GetCurrentAudioDriver()));
    for(int capture = 0; capture < 2; ++capture)
    {
        const char* kind = capture != 0 ? "capture" : "output";
        const int   n    = SDL_GetNumAudioDevices(capture);
        if(n < 0)
        {
            log.line(t, "audio %s devices: the driver cannot list them", kind);
            continue;
        }
        log.line(t, "audio %s devices: %d", kind, n);
        for(int d = 0; d < n && d < 16; ++d)
            log.line(t, "audio %s device %d: %s", kind, d, orNone(SDL_GetAudioDeviceName(d, capture)));
    }
}

void logJoysticks(DiagLog& log, double t)
{
    const char* cfg = SDL_getenv("SDL_GAMECONTROLLERCONFIG");
    if(cfg == nullptr)
        log.line(t, "SDL_GAMECONTROLLERCONFIG: not set");
    else
        log.line(t, "SDL_GAMECONTROLLERCONFIG (%zu characters): %s", std::strlen(cfg), cfg);
    const char* cfgFile = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE");
    if(cfgFile != nullptr)
        log.line(t, "SDL_GAMECONTROLLERCONFIG_FILE: %s", cfgFile);
    const int n = SDL_NumJoysticks();
    log.line(t, "joysticks: %d", n < 0 ? 0 : n);
    for(int i = 0; i < n; ++i)
    {
        char guid[64] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
        const bool gc = SDL_IsGameController(i) == SDL_TRUE;
        log.line(t, "joystick %d: '%s', GUID %s, vendor 0x%04x, product 0x%04x, instance %d, game controller: %s", i,
                 orNone(SDL_JoystickNameForIndex(i)), guid, static_cast<unsigned>(SDL_JoystickGetDeviceVendor(i)),
                 static_cast<unsigned>(SDL_JoystickGetDeviceProduct(i)), static_cast<int>(SDL_JoystickGetDeviceInstanceID(i)),
                 gc ? "yes" : "NO (SDL has no mapping for it; its buttons do not reach the app)");
        if(gc)
        {
            char* m = SDL_GameControllerMappingForDeviceIndex(i);
            log.line(t, "joystick %d mapping: %s", i, m != nullptr ? m : "(none)");
            if(m != nullptr)
                SDL_free(m);
        }
    }
}

void EventLogger::noteDevice(SDL_JoystickID id, const char* name) noexcept
{
    for(Device& d : devices_)
        if(d.id == id)
        {
            std::snprintf(d.name, sizeof d.name, "%s", orNone(name));
            return;
        }
    Device& d = devices_[nextDevice_++ % (sizeof devices_ / sizeof devices_[0])];
    d.id      = id;
    std::snprintf(d.name, sizeof d.name, "%s", orNone(name));
}

const char* EventLogger::deviceName(SDL_JoystickID id) const noexcept
{
    for(const Device& d : devices_)
        if(d.id == id)
            return d.name;
    return "(unknown)";
}

void EventLogger::onEvent(const SDL_Event& e, InputMap::Action action, const ButtonEvent& be, const InputMap& input,
                          double t) noexcept
{
    char desc[256];
    describeEvent(desc, sizeof desc, e);

    // Quit-path inputs, the first time each is pressed.
    {
        int which = -1;
        if(e.type == SDL_KEYDOWN && e.key.repeat == 0)
        {
            switch(e.key.keysym.scancode)
            {
                case SDL_SCANCODE_ESCAPE: which = 0; break;
                case SDL_SCANCODE_LALT: which = 1; break;
                case SDL_SCANCODE_RALT: which = 2; break;
                case SDL_SCANCODE_F4: which = 3; break;
                default: break;
            }
        }
        if(which < 0 && action == InputMap::Action::Button && be.pressed)
        {
            which = be.button == Button::Start ? 4 : be.button == Button::Select ? 5 : -1;
        }
        if(which >= 0 && quitInputs_.first(static_cast<std::size_t>(which)))
            log_.line(t, "quit-path input first seen: %s%s", desc,
                      action == InputMap::Action::Quit ? " (quits)" : "");
    }

    // Every Button, the first time it is pressed, with the SDL event behind it.
    if(action == InputMap::Action::Button && be.pressed && buttons_.first(static_cast<std::size_t>(be.button)))
        log_.line(t, "button first seen: %s <- %s", buttonName(be.button), desc);

    switch(e.type)
    {
        case SDL_CONTROLLERBUTTONDOWN:
            if(controllerButtons_.first(e.cbutton.button))
                log_.line(t, "controller button first pressed: '%s' (%d) -> %s", controllerButtonName(e.cbutton.button),
                          static_cast<int>(e.cbutton.button),
                          action == InputMap::Action::Button ? buttonName(be.button) : "not mapped");
            break;
        case SDL_CONTROLLERAXISMOTION:
        {
            const int v = e.caxis.value;
            if((v >= 8192 || v <= -8192) && controllerAxes_.first(e.caxis.axis))
            {
                const bool trigger = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                                     e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
                log_.line(t, "controller axis first moved: '%s' (%d) = %d -> %s", controllerAxisName(e.caxis.axis),
                          static_cast<int>(e.caxis.axis), v,
                          trigger ? (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "L2 (trigger axis)" : "R2 (trigger axis)")
                                  : "not mapped (sticks are not used)");
            }
            break;
        }
        case SDL_JOYBUTTONDOWN:
            if(joyButtons_.first(e.jbutton.button))
                log_.line(t, "joystick button first pressed (raw, diagnostics only): %s", desc);
            break;
        case SDL_JOYAXISMOTION:
        {
            const int v = e.jaxis.value;
            if((v >= 16384 || v <= -16384) && joyAxes_.first(e.jaxis.axis))
                log_.line(t, "joystick axis first moved (raw, diagnostics only): %s", desc);
            break;
        }
        case SDL_JOYHATMOTION:
            if(e.jhat.value != SDL_HAT_CENTERED && e.jhat.hat < 4 &&
               joyHats_.first(static_cast<std::size_t>(e.jhat.hat) * 16u + (e.jhat.value & 0x0Fu)))
                log_.line(t, "joystick hat first moved (raw, diagnostics only): %s", desc);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if(e.key.repeat == 0 && action == InputMap::Action::None && !InputMap::mapsScancode(e.key.keysym.scancode))
                if(keys_.allow(t))
                    log_.line(t, "unmapped key: %s", desc);
            break;
        case SDL_CONTROLLERDEVICEADDED:
        {
            const int idx = e.cdevice.which;
            char      guid[64] = {};
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(idx), guid, sizeof guid);
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(idx);
            noteDevice(id, SDL_GameControllerNameForIndex(idx));
            log_.line(t, "controller added: device index %d, instance %d, '%s', GUID %s", idx, static_cast<int>(id),
                      orNone(SDL_GameControllerNameForIndex(idx)), guid);
            char* m = SDL_GameControllerMappingForDeviceIndex(idx);
            log_.line(t, "controller mapping: %s", m != nullptr ? m : "(none)");
            if(m != nullptr)
                SDL_free(m);
            log_.line(t, "active controller: %s", input.controllerConnected() ? input.controllerName().c_str() : "(none)");
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
            log_.line(t, "controller removed: instance %d, '%s'; active controller now: %s",
                      static_cast<int>(e.cdevice.which), deviceName(e.cdevice.which),
                      input.controllerConnected() ? input.controllerName().c_str() : "(none)");
            break;
        case SDL_CONTROLLERDEVICEREMAPPED:
            log_.line(t, "controller remapped: instance %d", static_cast<int>(e.cdevice.which));
            break;
        case SDL_JOYDEVICEADDED:
        {
            const int idx = e.jdevice.which;
            char      guid[64] = {};
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(idx), guid, sizeof guid);
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(idx);
            noteDevice(id, SDL_JoystickNameForIndex(idx));
            log_.line(t, "joystick added: device index %d, instance %d, '%s', GUID %s, game controller: %s", idx,
                      static_cast<int>(id), orNone(SDL_JoystickNameForIndex(idx)), guid,
                      SDL_IsGameController(idx) == SDL_TRUE ? "yes" : "NO (no mapping: its buttons do not reach the app)");
            break;
        }
        case SDL_JOYDEVICEREMOVED:
            log_.line(t, "joystick removed: instance %d, '%s'", static_cast<int>(e.jdevice.which),
                      deviceName(e.jdevice.which));
            break;
        case SDL_WINDOWEVENT:
            if(e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                log_.line(t, "focus %s", e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? "gained" : "lost");
            else if(e.window.event != SDL_WINDOWEVENT_EXPOSED && e.window.event != SDL_WINDOWEVENT_MOVED &&
                    e.window.event != SDL_WINDOWEVENT_ENTER && e.window.event != SDL_WINDOWEVENT_LEAVE &&
                    window_.allow(t))
                log_.line(t, "%s", desc);
            break;
        case SDL_RENDER_TARGETS_RESET:
            log_.line(t, "render targets reset: the glyph atlas is rebuilt");
            break;
        case SDL_RENDER_DEVICE_RESET:
            log_.line(t, "render device reset: the glyph atlas is rebuilt");
            break;
        case SDL_AUDIODEVICEADDED:
        case SDL_AUDIODEVICEREMOVED:
            log_.line(t, "audio device %s: index/id %u, %s", e.type == SDL_AUDIODEVICEADDED ? "added" : "removed",
                      static_cast<unsigned>(e.adevice.which), e.adevice.iscapture != 0 ? "capture" : "output");
            break;
        case SDL_CONTROLLERBUTTONUP:
        case SDL_QUIT:
            break; // releases are seen through their presses; quit is logged by the app with its reason
        default:
            otherEvent(e, t);
            break;
    }
}

void EventLogger::otherEvent(const SDL_Event& e, double t) noexcept
{
    for(std::size_t i = 0; i < otherCount_; ++i)
        if(otherTypes_[i] == e.type)
            return;
    if(otherCount_ < sizeof otherTypes_ / sizeof otherTypes_[0])
        otherTypes_[otherCount_++] = e.type;
    const char* n = eventTypeName(e.type);
    if(n != nullptr)
        log_.line(t, "event type first seen: %s (0x%x)", n, static_cast<unsigned>(e.type));
    else
        log_.line(t, "event type first seen: 0x%x", static_cast<unsigned>(e.type));
}

} // namespace audreybrick
