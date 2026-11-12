#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "snow.h"

#define MAX_DISPLAYS 16
#define TRAY_MESSAGE (WM_APP + 1)
enum { CMD_PAUSE = 101, CMD_BALL, CMD_CLEAR, CMD_QUIT, CMD_AUSTRALIA, CMD_RATE = 110, CMD_WIND = 120 };

static const WCHAR control_class[] = L"SnowOverlay.Control";
static const WCHAR overlay_class[] = L"SnowOverlay.Display";

typedef struct {
    HWND window;
    HMONITOR monitor;
    HDC buffer;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    uint32_t *pixels;
    RECT bounds;
    RECT work;
    SnowScene *scene;
    bool fullscreen;
} Display;

static struct {
    HINSTANCE instance;
    HWND control;
    HICON icon;
    UINT taskbar_created;
    Display displays[MAX_DISPLAYS];
    size_t display_count;
    int intensity, wind;
    bool paused, interactive, low_power, rescan, australia, moving_window;
    ULONGLONG last_frame, last_geometry, last_power;
} app;

static bool covers(RECT rect, RECT screen) {
    return rect.left <= screen.left + 2 && rect.top <= screen.top + 2 &&
           rect.right >= screen.right - 2 && rect.bottom >= screen.bottom - 2;
}

static SnowRect snow_rect(RECT rect) {
    return (SnowRect){(float)rect.left, (float)rect.top,
                      (float)(rect.right - rect.left), (float)(rect.bottom - rect.top)};
}

static bool window_bounds(HWND window, RECT *rect) {
    DWORD cloaked = 0;
    if (!IsWindowVisible(window) || IsIconic(window)) return false;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return false;
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, rect, sizeof *rect)) &&
        !GetWindowRect(window, rect)) return false;
    return rect->right - rect->left >= 22 && rect->bottom - rect->top >= 18;
}

static float corner_inset(HWND window, RECT rect) {
    UINT thickness;
    if (IsZoomed(window) || FAILED(DwmGetWindowAttribute(window, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS,
                                                         &thickness, sizeof thickness))) return 0;
    HMONITOR display = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    if (GetMonitorInfoW(display, &monitor)) {
        RECT work = monitor.rcWork;
        bool full_height = labs(rect.top - work.top) <= 2 && labs(rect.bottom - work.bottom) <= 2;
        bool full_width = labs(rect.left - work.left) <= 2 && labs(rect.right - work.right) <= 2;
        if (full_height || full_width) return 0;
    }
    UINT dpi = GetDpiForWindow(window);
    for (size_t i = 0; i < app.display_count; i++) {
        if (app.displays[i].monitor == display) {
            dpi = GetDpiForWindow(app.displays[i].window);
            break;
        }
    }
    return 8.0f * (dpi ? dpi : 96) / 96;
}

typedef struct {
    SnowSurface surfaces[SNOW_MAX_SURFACES - 1];
    size_t count;
} Geometry;

static BOOL CALLBACK collect_window(HWND window, LPARAM state) {
    Geometry *geometry = (Geometry *)state;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() || window == GetShellWindow() ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) return TRUE;
    RECT rect;
    if (window_bounds(window, &rect)) {
        geometry->surfaces[geometry->count++] = (SnowSurface){
            .id = (uintptr_t)window, .rect = snow_rect(rect), .catches = true,
            .corner_radius = corner_inset(window, rect),
        };
    }
    return geometry->count < SNOW_MAX_SURFACES - 1;
}

static void scan_geometry(void) {
    Geometry geometry = {0};
    APPBARDATA bar = {0};
    bar.cbSize = sizeof bar;
    if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar) &&
        bar.rc.right > bar.rc.left && bar.rc.bottom > bar.rc.top) {
        geometry.surfaces[geometry.count++] = (SnowSurface){
            .id = UINT64_MAX, .rect = snow_rect(bar.rc), .catches = true,
        };
    }
    EnumWindows(collect_window, (LPARAM)&geometry);
    HWND foreground = GetForegroundWindow();
    RECT active = {0};
    DWORD process = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &process);
    bool active_bounds = foreground && foreground != GetShellWindow() &&
        process != GetCurrentProcessId() && window_bounds(foreground, &active);
    for (size_t i = 0; i < app.display_count; i++) {
        Display *display = &app.displays[i];
        display->fullscreen = active_bounds && covers(active, display->bounds);
        if (display->fullscreen) {
            ShowWindow(display->window, SW_HIDE);
            display->scene->pointer_known = false;
            display->scene->holding = false;
        }
        snow_surfaces(display->scene, geometry.surfaces, geometry.count);
    }
}

static void blend(uint32_t *pixel, unsigned alpha, unsigned red, unsigned green, unsigned blue) {
    uint32_t old = *pixel;
    unsigned keep = 255 - alpha;
    unsigned a = alpha + ((old >> 24) * keep + 127) / 255;
    unsigned r = (red * alpha + ((old >> 16) & 255) * keep + 127) / 255;
    unsigned g = (green * alpha + ((old >> 8) & 255) * keep + 127) / 255;
    unsigned b = (blue * alpha + (old & 255) * keep + 127) / 255;
    *pixel = a << 24 | r << 16 | g << 8 | b;
}

static void circle(Display *display, float x, float y, float radius,
                   unsigned alpha, unsigned red, unsigned green, unsigned blue) {
    SnowRect screen = display->scene->screen;
    int width = (int)screen.width, height = (int)screen.height;
    x -= screen.x;
    y = snow_screen_y(display->scene, y) - screen.y;
    int left = (int)fmaxf(0, floorf(x - radius - 1));
    int right = (int)fminf((float)width, ceilf(x + radius + 1));
    int top = (int)fmaxf(0, floorf(y - radius - 1));
    int bottom = (int)fminf((float)height, ceilf(y + radius + 1));
    for (int row = top; row < bottom; row++) {
        for (int col = left; col < right; col++) {
            float dx = (float)col + .5f - x, dy = (float)row + .5f - y;
            float coverage = radius + .55f - sqrtf(dx * dx + dy * dy);
            if (coverage > 0) {
                blend(&display->pixels[(size_t)row * (size_t)width + (size_t)col],
                      (unsigned)((float)alpha * fminf(1, coverage)), red, green, blue);
            }
        }
    }
}

static void draw_ball(Display *display, float x, float y, float radius) {
    circle(display, x, y, radius, 253, 187, 215, 243);
    circle(display, x - .13f * radius, y - .19f * radius, .78f * radius, 255, 247, 252, 255);
}

static void draw_drifts(Display *display) {
    SnowScene *scene = display->scene;
    SnowRect screen = scene->screen;
    int width = (int)screen.width;
    int height = (int)screen.height;
    for (size_t i = 0; i < scene->drift_count; i++) {
        const SnowDrift *drift = &scene->drifts[i];
        SnowRect rect = drift->surface.rect;
        int base = (int)fminf(screen.height, rect.y - screen.y);
        if (base < 1 || !drift->span_count) continue;
        bool has_snow = false;
        for (size_t bin = 0; bin < SNOW_BINS && !has_snow; bin++) has_snow = drift->height[bin] > .08f;
        if (!has_snow) continue;
        for (size_t j = 0; j < drift->span_count; j++) {
            SnowSpan span = drift->spans[j];
            int first = (int)fmaxf(0, ceilf(span.left - screen.x));
            int end = (int)fminf((float)width, ceilf(span.right - screen.x));
            for (int col = first; col < end; col++) {
                float depth = snow_depth(drift, span, (float)col + screen.x + .5f);
                if (depth < .08f) continue;
                float snow_top = rect.y - screen.y - depth;
                int row = (int)fmaxf(0, floorf(snow_top));
                if (row >= base) continue;
                size_t out = (size_t)(scene->inverted ? height - 1 - row : row);
                blend(&display->pixels[out * (size_t)width + (size_t)col],
                      (unsigned)(238 * fminf(1, (float)row + 1 - snow_top)), 237, 247, 255);
                for (row++; row < base; row++) {
                    out = (size_t)(scene->inverted ? height - 1 - row : row);
                    blend(&display->pixels[out * (size_t)width + (size_t)col], 238, 237, 247, 255);
                }
            }
        }
    }
}

static bool draw(Display *display) {
    SnowScene *scene = display->scene;
    size_t pixel_count = (size_t)scene->screen.width * (size_t)scene->screen.height;
    memset(display->pixels, 0, pixel_count * sizeof(uint32_t));
    if (app.interactive) {
        /* Alpha zero in a layered window passes mouse events through empty pixels. */
        for (size_t i = 0; i < pixel_count; i++) display->pixels[i] = 0x01000000;
    }
    for (size_t i = 0; i < scene->flake_count; i++) {
        SnowFlake flake = scene->flakes[i];
        circle(display, flake.x, flake.y, flake.radius, (unsigned)fminf(245, 99 + flake.radius * 33), 239, 250, 255);
    }
    draw_drifts(display);
    for (size_t i = 0; i < scene->ball_count; i++) {
        SnowBall *ball = &scene->balls[i];
        const SnowDrift *drift = snow_ball_surface(scene, ball);
        if (!drift) continue;
        float x = drift->surface.rect.x + ball->offset * drift->surface.rect.width;
        float y = snow_ball_height(scene, ball, x);
        size_t surface = (size_t)(drift - scene->drifts);
        if (snow_exposed(scene, surface, x) && snow_point_visible(scene, surface, x, y)) {
            draw_ball(display, x, y, ball->radius);
        }
    }
    if (scene->holding) draw_ball(display, scene->ball_x, scene->ball_y, scene->ball_radius);
    if (app.interactive) {
        /* Keep the taskbar reachable while the canvas accepts input. */
        size_t width = (size_t)scene->screen.width;
        size_t left = (size_t)(display->work.left - display->bounds.left);
        size_t right = (size_t)(display->work.right - display->bounds.left);
        for (LONG y = display->bounds.top; y < display->bounds.bottom; y++) {
            uint32_t *row = display->pixels + (size_t)(y - display->bounds.top) * width;
            if (y < display->work.top || y >= display->work.bottom) {
                memset(row, 0, width * sizeof *row);
            } else {
                memset(row, 0, left * sizeof *row);
                memset(row + right, 0, (width - right) * sizeof *row);
            }
        }
    }
    POINT position = {display->bounds.left, display->bounds.top}, source = {0, 0};
    SIZE size = {(LONG)scene->screen.width, (LONG)scene->screen.height};
    BLENDFUNCTION mode = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(display->window, NULL, &position, &size, display->buffer,
                               &source, 0, &mode, ULW_ALPHA) != FALSE;
}

static void toggle_interaction(void) {
    if (app.paused) return;
    app.interactive = !app.interactive;
    POINT pointer = {0, 0};
    GetCursorPos(&pointer);
    HWND focus = NULL;
    for (size_t i = 0; i < app.display_count; i++) {
        Display *display = &app.displays[i];
        LONG_PTR style = GetWindowLongPtrW(display->window, GWL_EXSTYLE);
        if (app.interactive) {
            style &= ~(LONG_PTR)(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
            if (IsWindowVisible(display->window) && PtInRect(&display->bounds, pointer)) focus = display->window;
        } else {
            style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
            if (GetCapture() == display->window) ReleaseCapture();
        }
        SetWindowLongPtrW(display->window, GWL_EXSTYLE, style);
        SetWindowPos(display->window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        display->scene->holding = false;
        display->scene->pointer_known = false;
    }
    if (focus) SetForegroundWindow(focus);
}

static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
    }
    Display *display = (Display *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (!display || !app.interactive) return DefWindowProcW(window, message, wparam, lparam);
    SnowScene *scene = display->scene;
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        toggle_interaction();
        return 0;
    }
    if (message == WM_CAPTURECHANGED) {
        scene->holding = false;
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE || message == WM_LBUTTONUP) {
        float x = (float)(display->bounds.left + GET_X_LPARAM(lparam));
        float y = snow_screen_y(scene, (float)(display->bounds.top + GET_Y_LPARAM(lparam)));
        if (message == WM_LBUTTONDOWN) {
            SetCapture(window);
            snow_begin_ball(scene, x, y);
        } else if (message == WM_LBUTTONUP) {
            snow_drop_ball(scene, x, y);
            if (GetCapture() == window) ReleaseCapture();
        } else {
            snow_move_ball(scene, x, y);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void free_display(Display *display) {
    if (display->window) DestroyWindow(display->window);
    if (display->old_bitmap) SelectObject(display->buffer, display->old_bitmap);
    if (display->bitmap) DeleteObject(display->bitmap);
    if (display->buffer) DeleteDC(display->buffer);
    free(display->scene);
    *display = (Display){0};
}

static BOOL CALLBACK create_display(HMONITOR monitor, HDC dc, LPRECT bounds, LPARAM param) {
    (void)dc;
    (void)param;
    if (app.display_count == MAX_DISPLAYS) return FALSE;
    Display *display = &app.displays[app.display_count];
    display->bounds = *bounds;
    display->monitor = monitor;
    MONITORINFO monitor_info = {0};
    monitor_info.cbSize = sizeof monitor_info;
    if (!GetMonitorInfoW(monitor, &monitor_info) ||
        !IntersectRect(&display->work, bounds, &monitor_info.rcWork)) display->work = *bounds;
    SnowRect rect = snow_rect(*bounds);
    display->scene = calloc(1, sizeof *display->scene);
    if (!display->scene) return TRUE;
    snow_init(display->scene, rect, (uint32_t)GetTickCount64() + (uint32_t)app.display_count);
    snow_set_inverted(display->scene, app.australia);
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = (LONG)rect.width;
    info.bmiHeader.biHeight = -(LONG)rect.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    display->buffer = CreateCompatibleDC(NULL);
    if (display->buffer) {
        display->bitmap = CreateDIBSection(display->buffer, &info, DIB_RGB_COLORS,
                                           (void **)&display->pixels, NULL, 0);
    }
    if (!display->bitmap || !display->pixels) {
        free_display(display);
        return TRUE;
    }
    display->old_bitmap = SelectObject(display->buffer, display->bitmap);
    display->window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, overlay_class, L"", WS_POPUP,
                                    bounds->left, bounds->top, (int)rect.width, (int)rect.height,
                                    NULL, NULL, app.instance, display);
    if (display->window) app.display_count++;
    else free_display(display);
    return TRUE;
}

static void setup_displays(void) {
    if (app.interactive) toggle_interaction();
    for (size_t i = 0; i < app.display_count; i++) free_display(&app.displays[i]);
    app.display_count = 0;
    EnumDisplayMonitors(NULL, NULL, create_display, 0);
    app.last_geometry = 0;
    app.rescan = false;
}

static void tick(void) {
    ULONGLONG now = GetTickCount64();
    if (app.rescan) setup_displays();
    if (app.paused) return;
    if (now - app.last_power >= 1000) {
        SYSTEM_POWER_STATUS power;
        app.low_power = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
        app.last_power = now;
    }
    if (app.low_power && now - app.last_frame < 60) return;
    GUITHREADINFO thread = {.cbSize = sizeof thread};
    bool moving = GetGUIThreadInfo(0, &thread) && (thread.flags & GUI_INMOVESIZE) != 0;
    if (moving || app.moving_window || now - app.last_geometry >= (app.low_power ? 220 : 100)) {
        scan_geometry();
        app.last_geometry = now;
    }
    app.moving_window = moving;
    float dt = app.last_frame ? (float)(now - app.last_frame) / 1000 : .033f;
    app.last_frame = now;
    POINT pointer = {0, 0};
    BOOL pointer_available = GetCursorPos(&pointer);
    for (size_t i = 0; i < app.display_count; i++) {
        Display *display = &app.displays[i];
        if (display->fullscreen) continue;
        if (!app.interactive && pointer_available && PtInRect(&display->bounds, pointer)) {
            snow_sweep(display->scene, (float)pointer.x, snow_screen_y(display->scene, (float)pointer.y));
        } else {
            display->scene->pointer_known = false;
        }
        snow_step(display->scene, dt, app.low_power && app.intensity == 2 ? 1 : app.intensity, (float)app.wind);
        if (draw(display) && !IsWindowVisible(display->window)) ShowWindow(display->window, SW_SHOWNOACTIVATE);
    }
}

static NOTIFYICONDATAW tray_data(void) {
    NOTIFYICONDATAW data = {0};
    data.cbSize = sizeof data;
    data.hWnd = app.control;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = TRAY_MESSAGE;
    data.hIcon = app.icon;
    lstrcpynW(data.szTip, L"Снегопад", ARRAYSIZE(data.szTip));
    return data;
}

static bool add_tray(void) {
    NOTIFYICONDATAW data = tray_data();
    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

static void show_menu(void) {
    HMENU menu = CreatePopupMenu();
    HMENU rates = CreatePopupMenu();
    HMENU winds = CreatePopupMenu();
    if (!menu || !rates || !winds) {
        if (menu) DestroyMenu(menu);
        if (rates) DestroyMenu(rates);
        if (winds) DestroyMenu(winds);
        return;
    }
    AppendMenuW(menu, MF_STRING, CMD_PAUSE, app.paused ? L"Продолжить" : L"Пауза");
    AppendMenuW(menu, MF_STRING | (app.paused ? MF_GRAYED : 0), CMD_BALL,
                app.interactive ? L"Выйти из режима снежков (Esc)" : L"Режим снежков");
    AppendMenuW(menu, MF_STRING, CMD_CLEAR, L"Очистить снег");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(rates, MF_STRING, CMD_RATE, L"Лёгкий");
    AppendMenuW(rates, MF_STRING, CMD_RATE + 1, L"Ровный");
    AppendMenuW(rates, MF_STRING, CMD_RATE + 2, L"Метель");
    CheckMenuRadioItem(rates, CMD_RATE, CMD_RATE + 2, CMD_RATE + (UINT)app.intensity, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)rates, L"Снегопад");
    AppendMenuW(winds, MF_STRING, CMD_WIND, L"Влево");
    AppendMenuW(winds, MF_STRING, CMD_WIND + 1, L"Штиль");
    AppendMenuW(winds, MF_STRING, CMD_WIND + 2, L"Вправо");
    CheckMenuRadioItem(winds, CMD_WIND, CMD_WIND + 2, (UINT)(CMD_WIND + app.wind + 1), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)winds, L"Ветер");
    AppendMenuW(menu, MF_STRING | (app.australia ? MF_CHECKED : 0), CMD_AUSTRALIA, L"Австралия");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_QUIT, L"Выход");
    POINT point = {0, 0};
    GetCursorPos(&point);
    SetForegroundWindow(app.control);
    UINT command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, app.control, NULL);
    DestroyMenu(menu);
    PostMessageW(app.control, WM_NULL, 0, 0);
    if (command == CMD_PAUSE) {
        if (app.interactive) toggle_interaction();
        app.paused = !app.paused;
        app.last_frame = 0;
        app.last_geometry = 0;
        for (size_t i = 0; i < app.display_count; i++) {
            app.displays[i].scene->pointer_known = false;
            ShowWindow(app.displays[i].window, SW_HIDE);
        }
    } else if (command == CMD_BALL) {
        toggle_interaction();
    } else if (command == CMD_CLEAR) {
        for (size_t i = 0; i < app.display_count; i++) snow_clear(app.displays[i].scene);
    } else if (command == CMD_AUSTRALIA) {
        app.australia = !app.australia;
        app.last_geometry = 0;
        for (size_t i = 0; i < app.display_count; i++) snow_set_inverted(app.displays[i].scene, app.australia);
    } else if (command == CMD_QUIT) {
        DestroyWindow(app.control);
    } else if (command >= CMD_RATE && command <= CMD_RATE + 2) {
        app.intensity = (int)(command - CMD_RATE);
    } else if (command >= CMD_WIND && command <= CMD_WIND + 2) {
        app.wind = (int)(command - CMD_WIND) - 1;
    }
}

static LRESULT CALLBACK control_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER) {
        tick();
        return 0;
    }
    if (message == TRAY_MESSAGE) {
        UINT event = LOWORD(lparam);
        if (event == WM_CONTEXTMENU || event == NIN_SELECT || event == NIN_KEYSELECT) show_menu();
        return 0;
    }
    if (message == WM_DISPLAYCHANGE || (message == WM_SETTINGCHANGE && wparam == SPI_SETWORKAREA)) {
        app.rescan = true;
    }
    if (message == app.taskbar_created && message) add_tray();
    if (message == WM_DESTROY) {
        KillTimer(window, 1);
        NOTIFYICONDATAW data = tray_data();
        Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    (void)command_line;
    (void)show;
    HANDLE single_instance = CreateMutexW(NULL, FALSE, L"Local\\SnowOverlay.Desktop");
    if (single_instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(single_instance);
        return 0;
    }
    app.instance = instance;
    app.intensity = 1;
    app.icon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    if (!app.icon) app.icon = LoadIconW(NULL, IDI_APPLICATION);
    app.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW overlay = {0}, control = {0};
    overlay.hInstance = control.hInstance = instance;
    overlay.lpfnWndProc = overlay_proc;
    overlay.lpszClassName = overlay_class;
    overlay.hCursor = LoadCursorW(NULL, IDC_ARROW);
    control.lpfnWndProc = control_proc;
    control.lpszClassName = control_class;
    if (RegisterClassW(&overlay) && RegisterClassW(&control)) {
        app.control = CreateWindowExW(WS_EX_TOOLWINDOW, control_class, L"Снегопад", WS_OVERLAPPED,
                                     0, 0, 0, 0, NULL, NULL, instance, NULL);
    }
    int exit_code = 1;
    if (app.control && add_tray() && SetTimer(app.control, 1, 42, NULL)) {
        setup_displays();
        if (app.display_count) {
            MSG message;
            BOOL result;
            while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            exit_code = result == 0 ? 0 : 1;
        }
    }
    if (exit_code) MessageBoxW(NULL, L"Не удалось запустить снегопад.", L"Снегопад", MB_ICONERROR);
    if (app.control && IsWindow(app.control)) DestroyWindow(app.control);
    for (size_t i = 0; i < app.display_count; i++) free_display(&app.displays[i]);
    if (single_instance) CloseHandle(single_instance);
    return exit_code;
}
