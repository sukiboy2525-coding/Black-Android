#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>

// the game's own settings, in the settings file through app::kGame.options
namespace game {

// the languages, saved by these codes: English, French, German, Italian, Spanish, Indonesian. Indonesian is the
// recomp's own text only: the game files have none, so the game itself stays in English
inline constexpr const char* kLanguages[] = {"int", "fra", "deu", "ita", "esp", "ind"};

struct Options {
    bool skip_intros = false;
    bool fps_counter = false;
    uint32_t fmv_scale = 0;
    uint32_t screen_filter = 0;
    bool reload_blur = true;
    bool bloom = true;
    bool fog = true;
    uint32_t anti_aliasing = 0;
    bool hdr = false;
    uint32_t draw_distance = 1;
    bool dynamic_resolution = false;
    uint32_t dynamic_target_fps = 60;
    uint32_t dynamic_min_percent = 40;
    uint32_t dynamic_max_percent = 100;
    uint32_t dynamic_hold = 5;
    float mouse_sensitivity = 1.0f;
    float touch_sensitivity = 1.0f;
    bool invert_mouse = false;
    bool hold_aim = true;
    uint32_t fov = 70;
    uint32_t viewmodel_fov = 70;
    uint32_t walk_speed = 50;
    uint32_t left_deadzone = 0;
    uint32_t right_deadzone = 0;
    float left_sensitivity = 1.0f;
    float right_sensitivity = 1.0f;
    uint32_t audio_output = 0;
    std::string language = "int";
    bool infinite_ammo = false;
    uint32_t rapid_fire = 1;
    bool no_recoil = false;
    uint32_t game_speed = 100;
    bool god_mode = false;
    bool one_hit_kill = false;
    bool infinite_grenades = false;
    bool unlock_levels = false;
    bool unlock_black_ops = false;

    // ESP settings
    bool esp = false;           // master toggle: ESP overlay on enemies
    bool esp_box = true;        // box around each enemy
    bool esp_line = true;       // line from bottom of screen to each enemy
    bool esp_skeleton = false;  // skeleton lines on each enemy
    float esp_max_distance = 0.0f;  // max world units to draw ESP, 0 = unlimited
    float esp_color_r = 1.0f;   // ESP colour red   (0.0 - 1.0)
    float esp_color_g = 0.125f; // ESP colour green (0.0 - 1.0)
    float esp_color_b = 0.125f; // ESP colour blue  (0.0 - 1.0)
    float esp_color_a = 1.0f;   // ESP opacity      (0.0 - 1.0)
    float esp_line_thickness = 1.5f;  // line thickness (0.5 - 5.0)

    bool aimbot = false;
    std::map<std::string, std::string> keys;
};
Options& Opt();
const std::string& BootLanguage();
bool HasLanguages();
bool LanguageAvailable(const std::string& code);
inline std::atomic<float> g_fps{0};
inline std::atomic<bool> g_hide_hud{false};
inline std::atomic<float> g_zoom{1.0f};
inline std::atomic<bool> g_focus_paused{false};
inline std::atomic<uint32_t> g_focus_pauses{0};
void WaitWhileUnfocused();
bool HudShown();

}  // namespace game
