#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>

namespace game {

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
    // ESP
    bool esp = false;
    bool esp_box = true;
    bool esp_line = true;
    bool esp_skeleton = false;
    uint32_t esp_max_distance = 0;   // 0 = unlimited, in metres
    uint32_t esp_color_r = 100;      // 0-100 percent
    uint32_t esp_color_g = 12;
    uint32_t esp_color_b = 12;
    uint32_t esp_color_a = 100;
    uint32_t esp_thickness = 15;     // 10-50, divided by 10 = 1.0-5.0
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
