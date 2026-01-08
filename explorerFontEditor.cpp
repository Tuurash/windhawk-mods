// ==WindhawkMod==
// @id              fix-w11-explorer-font
// @name            Fix w11 Explorer Font
// @description     Customize Windows 11 Explorer font colors and fix theme inconsistencies, especially in dark mode.
// @version         0.1
// @author          Turash- Mohaimanul Haque
// @github          https://github.com/tuurash
// @include         explorer.exe
// @compilerOptions -luxtheme -lgdi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Fix w11 Explorer Font


Customize Windows 11 Explorer font colors and fix theme inconsistencies,
especially in dark mode.  Many Windows 11 users experience difficulty reading
text in Explorer when using a dark system theme. This project aims to solve this
problem by allowing users to customize the font colors within Explorer, ensuring
optimal readability and a consistent visual experience regardless of the chosen
theme.

## Configuration:

- Font Face Name:
To not affect the Windows default-picked font face name, simply fill in the
setting textbox with the value `None`.

- Custom text color:
Toggles custom text color functionality. When enabled, all text will be rendered
with the RGB color values specified below.

- Text Color: Red (0-255):
The red value of the text color (0-255).

- Text Color: Green (0-255):
The green value of the text color (0-255).

- Text Color: Blue (0-255):
The blue value of the text color (0-255).

## System stability:

Explorer Font Changer is heavily tested against GDI handle leaks and there are
none (known!) at the time of writing this.

This can be checked by others by going to System Informer, looking for
`explorer.exe`, right-clicking the entry, going to "Miscellaneous", and then
"GDI handles". Then, you can look at the font handles. Normally, you should see
no handles present with your chosen font there!
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
# Here you can define settings, in YAML format, that the mod users will be able
# to configure. Metadata values such as $name and $description are optional.
# Check out the documentation for more information:
# https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod#settings
- font:
  - name: "None"
    $name: "Font Face Name"
  - customColor: true
    $name: "Custom text color"
  - textR: 255
    $name: "Text Color: Red (0-255)"
  - textG: 255
    $name: "Text Color: Green (0-255)"
  - textB: 255
    $name: "Text Color: Blue (0-255)"
  $name: Custom font
  $description: This font will be used for all font creation in Windows.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

using namespace std::string_view_literals;
using namespace WindhawkUtils;

namespace util {
StringSetting s_font_name;
bool s_custom_color = true;
int s_text_r = 255;
int s_text_g = 255;
int s_text_b = 255;

// Class to declare transparent RAII types.
template <class T, auto Fn>
class raii {
   public:
    raii() = delete;
    raii(T handle) : m_handle(handle) {}
    ~raii() { Fn(m_handle); }

    // we don't need operator= or raii(raii&&) yet...

    auto& get() { return m_handle; }

    auto get() const { return m_handle; }

   private:
    T m_handle;
};

// RAII `HFONT`.
using unique_hfont_t = raii<HFONT, DeleteObject>;

void change_font_in_struct(LOGFONTW* font) {
    auto font_name = std::wstring_view(s_font_name.get());

    // Check font configuration.
    if (font_name != L"None"sv) {
        if (font_name.size() <= 31) {
            // `face_name` points to a total of 32 WORDs.
            auto face_name = static_cast<WCHAR*>(font->lfFaceName);

            std::memset(face_name, 0, ARRAYSIZE(font->lfFaceName));
            std::memcpy(
                face_name, font_name.data(),
                font_name.size() * sizeof(decltype(font_name)::value_type));
        } else {
            Wh_Log(L"Trying to change font to \"%s\": size too long (%d)",
                   font_name.data(), font_name.size());
        }
    }
}

std::pair<unique_hfont_t, LOGFONTW> hdc_update_font(HDC hdc) {
    // Get current selected font.
    auto h_font = GetCurrentObject(hdc, OBJ_FONT);

    // Create struct for font.
    auto font = LOGFONTW{0};

    // Get `LOGFONTW` from font handle.
    GetObjectW(static_cast<HANDLE>(h_font), sizeof(font),
               static_cast<LPVOID>(&font));

    // Change font to the font specified in settings.
    change_font_in_struct(&font);

    // Create new font.
    auto h_new_font = CreateFontIndirectW(&font);

    // Select font.
    SelectObject(hdc, h_new_font);

    return {h_new_font, font};
}

void update_settings() {
    s_font_name = StringSetting::make(L"font.name");
    s_custom_color = Wh_GetIntSetting(L"font.customColor") == 1;
    s_text_r = Wh_GetIntSetting(L"font.textR");
    s_text_g = Wh_GetIntSetting(L"font.textG");
    s_text_b = Wh_GetIntSetting(L"font.textB");
}

bool is_custom_color_enabled() {
    return s_custom_color;
}

COLORREF get_custom_text_color() {
    // Get the RGB values
    auto r = static_cast<uint8_t>(s_text_r & 0xff);
    auto g = static_cast<uint8_t>(s_text_g & 0xff);
    auto b = static_cast<uint8_t>(s_text_b & 0xff);

    // Generate RGB COLORREF (format: 0x00BBGGRR)
    auto result = static_cast<COLORREF>(RGB(r, g, b));

    return result;
}

// Check if background is light (context menus, tooltips, etc.)
bool is_light_background(HDC hdc) {
    COLORREF bg_color = GetBkColor(hdc);

    // Extract RGB components
    int r = GetRValue(bg_color);
    int g = GetGValue(bg_color);
    int b = GetBValue(bg_color);

    // Calculate perceived brightness using luminance formula
    int brightness = (r * 299 + g * 587 + b * 114) / 1000;

    // If brightness > 128, it's a light background
    return brightness > 128;
}

// Check if we're drawing in the Explorer file/folder view
bool is_explorer_file_view(HDC hdc) {
    HWND hwnd = WindowFromDC(hdc);
    if (!hwnd) {
        return false;
    }

    // Get window class name
    WCHAR className[256] = {0};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));

    // Check if this is a DirectUIHWND window (contains file list view)
    // or SysListView32 (the actual list view control)
    std::wstring_view classNameView(className);

    if (classNameView == L"DirectUIHWND"sv ||
        classNameView == L"SysListView32"sv) {
        return true;
    }

    // Also check parent windows up to 3 levels for SHELLDLL_DefView
    HWND parent = hwnd;
    for (int i = 0; i < 3; i++) {
        parent = GetParent(parent);
        if (!parent)
            break;

        GetClassNameW(parent, className, ARRAYSIZE(className));
        std::wstring_view parentClassName(className);

        if (parentClassName == L"SHELLDLL_DefView"sv) {
            return true;
        }
    }

    return false;
}
}  // namespace util

using draw_textw_hook_t = decltype(&DrawTextW);
static draw_textw_hook_t draw_textw_original = nullptr;

using draw_text_exw_hook_t = decltype(&DrawTextExW);
static draw_text_exw_hook_t draw_text_exw_original = nullptr;

INT WINAPI draw_textw_hook(HDC hdc,
                           LPCWSTR lpchText,
                           INT cchText,
                           LPRECT lprc,
                           UINT format) {
    // Update font on HDC to settings font, from current HFONT.
    auto [h_new_font, _] = util::hdc_update_font(hdc);

    // Apply custom text color ONLY to file/folder names in Explorer view
    // Skip: light backgrounds (context menus), non-file-view windows (address
    // bar, ribbon, etc.)
    if (util::is_custom_color_enabled() && !util::is_light_background(hdc) &&
        util::is_explorer_file_view(hdc)) {
        SetTextColor(hdc, util::get_custom_text_color());
    }

    return draw_textw_original(hdc, lpchText, cchText, lprc, format);
}

INT WINAPI draw_text_exw_hook(HDC hdc,
                              LPWSTR lpchText,
                              INT cchText,
                              LPRECT lprc,
                              UINT format,
                              LPDRAWTEXTPARAMS lpdtp) {
    // Update font on HDC to settings font, from current HFONT.
    auto [h_new_font, _] = util::hdc_update_font(hdc);

    // Apply custom text color ONLY to file/folder names in Explorer view
    // Skip: light backgrounds (context menus), non-file-view windows (address
    // bar, ribbon, etc.)
    if (util::is_custom_color_enabled() && !util::is_light_background(hdc) &&
        util::is_explorer_file_view(hdc)) {
        SetTextColor(hdc, util::get_custom_text_color());
    }

    return draw_text_exw_original(hdc, lpchText, cchText, lprc, format, lpdtp);
}

BOOL Wh_ModInit() {
    // Get settings before applying hooks.
    util::update_settings();

    auto user32 = LoadLibraryW(L"user32.dll");

    auto draw_textw = reinterpret_cast<draw_textw_hook_t>(
        GetProcAddress(user32, "DrawTextW"));
    Wh_SetFunctionHook(reinterpret_cast<void*>(draw_textw),
                       reinterpret_cast<void*>(draw_textw_hook),
                       reinterpret_cast<void**>(&draw_textw_original));

    auto draw_text_exw = reinterpret_cast<draw_text_exw_hook_t>(
        GetProcAddress(user32, "DrawTextExW"));
    Wh_SetFunctionHook(reinterpret_cast<void*>(draw_text_exw),
                       reinterpret_cast<void*>(draw_text_exw_hook),
                       reinterpret_cast<void**>(&draw_text_exw_original));

    return TRUE;
}

void Wh_ModSettingsChanged() {
    util::update_settings();
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
}
