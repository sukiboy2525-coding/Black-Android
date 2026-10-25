#include <chrono>
#include <cmath>
#include <cstring>

#include "common/options.h"
#include "game/layout.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;
using namespace layout;

namespace {

constexpr uint32_t kCharKind     = 0xC4;
constexpr uint32_t kCharPosition = 0xA0;
constexpr uint32_t kCharDying    = 0x384;
constexpr uint32_t kEnemyKind    = 1;

constexpr uint32_t kControllerYaw   = 0x18;
constexpr uint32_t kControllerPitch = 0x1C;
constexpr uint32_t kPlayerZoom      = 0x2A4;

constexpr float kHeight   = 1.8f;
constexpr float kBoxWidth = 0.45f;
constexpr float kNear     = 0.08f;
constexpr float kChest    = 1.2f;
constexpr float kCone     = 30.0f;
constexpr float kPull     = 1.0f;
constexpr float kDegrees  = 180.0f / 3.14159265f;

// skeleton joint offsets from feet position (Y up)
constexpr float kJoints[][3] = {
    {0.0f, 0.10f, 0.0f},  // left foot
    {0.0f, 0.10f, 0.0f},  // right foot
    {0.0f, 0.55f, 0.0f},  // left knee
    {0.0f, 0.55f, 0.0f},  // right knee
    {0.0f, 0.90f, 0.0f},  // hips
    {0.0f, 1.20f, 0.0f},  // chest
    {-0.30f, 1.20f, 0.0f},// left shoulder
    { 0.30f, 1.20f, 0.0f},// right shoulder
    {-0.30f, 0.85f, 0.0f},// left elbow
    { 0.30f, 0.85f, 0.0f},// right elbow
    {-0.30f, 0.50f, 0.0f},// left hand
    { 0.30f, 0.50f, 0.0f},// right hand
    {0.0f, 1.55f, 0.0f},  // neck
    {0.0f, 1.80f, 0.0f},  // head top
};
constexpr size_t kJointCount = sizeof(kJoints) / sizeof(kJoints[0]);

// skeleton bone connections: pairs of joint indices
constexpr int kBones[][2] = {
    {0, 2},  // left foot -> left knee
    {2, 4},  // left knee -> hips
    {1, 3},  // right foot -> right knee
    {3, 4},  // right knee -> hips
    {4, 5},  // hips -> chest
    {5, 6},  // chest -> left shoulder
    {5, 7},  // chest -> right shoulder
    {6, 8},  // left shoulder -> left elbow
    {7, 9},  // right shoulder -> right elbow
    {8, 10}, // left elbow -> left hand
    {9, 11}, // right elbow -> right hand
    {5, 12}, // chest -> neck
    {12, 13},// neck -> head
};
constexpr size_t kBoneCount = sizeof(kBones) / sizeof(kBones[0]);

constexpr auto    kSeen       = std::chrono::milliseconds(250);
constexpr size_t  kMaxEnemies = 64;

struct Seen {
    uint32_t who;
    std::chrono::steady_clock::time_point at;
};
Seen   g_seen[kMaxEnemies];
size_t g_count;

float g_view[16], g_projection[16];
bool  g_camera;
uint32_t g_camera_frame;

constexpr uint32_t kTriangles = 0xD5A00;
struct Vertex { float x, y; };

// segments: line(1) + box(4) + skeleton bones per enemy
constexpr size_t kSegmentsPerEnemy = 1 + 4 + kBoneCount;
constexpr size_t kMaxVertices      = kMaxEnemies * kSegmentsPerEnemy * 6;

void Remember(uint32_t who) {
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < g_count; i++)
        if (g_seen[i].who == who) { g_seen[i].at = now; return; }
    if (g_count < kMaxEnemies) { g_seen[g_count++] = {who, now}; return; }
    Seen* oldest = g_seen;
    for (Seen& s : g_seen)
        if (s.at < oldest->at) oldest = &s;
    *oldest = {who, now};
}

bool Clip(const float p[3], float clip[4]) {
    float eye[4];
    for (int c = 0; c < 4; c++)
        eye[c] = p[0]*g_view[c] + p[1]*g_view[4+c] + p[2]*g_view[8+c] + g_view[12+c];
    for (int c = 0; c < 4; c++)
        clip[c] = eye[0]*g_projection[c] + eye[1]*g_projection[4+c] +
                  eye[2]*g_projection[8+c] + eye[3]*g_projection[12+c];
    return clip[3] > kNear;
}

bool Project(const float p[3], float vx, float vy, float vw, float vh, float out[2]) {
    float clip[4];
    if (!Clip(p, clip)) return false;
    out[0] = vx + (clip[0]/clip[3] + 1) * 0.5f * vw;
    out[1] = vy + (1 - clip[1]/clip[3]) * 0.5f * vh;
    return true;
}

// distance from camera to world position using view matrix
float DistanceTo(const float p[3]) {
    // camera position is inverse of view translation
    float dx = p[0] - (-g_view[12]*g_view[0] - g_view[13]*g_view[1] - g_view[14]*g_view[2]);
    float dy = p[1] - (-g_view[12]*g_view[4] - g_view[13]*g_view[5] - g_view[14]*g_view[6]);
    float dz = p[2] - (-g_view[12]*g_view[8] - g_view[13]*g_view[9] - g_view[14]*g_view[10]);
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

Vertex* Segment(Vertex* v, float x0, float y0, float x1, float y1, float width) {
    float dx = x1-x0, dy = y1-y0;
    float length = std::sqrt(dx*dx + dy*dy);
    if (!(length > 0)) return v;
    float nx = -dy/length*width*0.5f, ny = dx/length*width*0.5f;
    const float corner[4][2] = {{x0+nx,y0+ny},{x0-nx,y0-ny},{x1+nx,y1+ny},{x1-nx,y1-ny}};
    for (int i : {0,1,2,2,1,3}) *v++ = {corner[i][0], corner[i][1]};
    return v;
}

// fill colour buffer for a range of vertices with rgba
void FillColour(uint32_t colours, size_t from, size_t count,
                float r, float g, float b, float a) {
    float c[4] = {r, g, b, a};
    for (size_t i = from; i < from + count; i++)
        std::memcpy(HostPtr(colours + i * sizeof(c)), c, sizeof(c));
}

}  // namespace

namespace game {

void EspCamera() {
    uint32_t transforms = SYM(D3D_Device) + XDK_CDevice_Transforms;
    std::memcpy(g_view,       HostPtr(transforms),      sizeof(g_view));
    std::memcpy(g_projection, HostPtr(transforms + 64), sizeof(g_projection));
    g_camera = true;
    g_camera_frame++;
}

void Aimbot(X86Context& ctx) {
    static uint32_t turned;
    uint32_t c  = ctx.esi;
    uint32_t me = R32(kWorld) ? R32(kWorld) + kWorldPlayer : 0;
    if (!Opt().aimbot || !g_camera || !me || turned == g_camera_frame ||
        !(G<float>(me + kPlayerZoom) > 1.01f)) return;
    turned = g_camera_frame;
    auto  now = std::chrono::steady_clock::now();
    float best = kCone, yaw = 0, pitch = 0;
    for (size_t i = 0; i < g_count; i++) {
        uint32_t who = g_seen[i].who;
        if (now - g_seen[i].at > kSeen ||
            R32(who + kCharKind) != kEnemyKind ||
            R32(who + kCharDying)) continue;
        const float* at = &G<float>(who + kCharPosition);
        float chest[3] = {at[0], at[1]+kChest, at[2]}, clip[4];
        if (!Clip(chest, clip)) continue;
        float y   = -std::atan(clip[0]/clip[3]/g_projection[0]) * kDegrees;
        float p   = -std::atan(clip[1]/clip[3]/g_projection[5]) * kDegrees;
        float off = std::sqrt(y*y + p*p);
        if (off < best) { best = off; yaw = y; pitch = p; }
    }
    G<float>(c + kControllerYaw)   += yaw   * kPull;
    G<float>(c + kControllerPitch) += pitch * kPull;
}

void DrawEsp(X86Context& ctx, float unit) {
    if (!Opt().esp || !g_camera || !R32(kWorld)) return;

    uint32_t viewport = SYM(D3D_Device) + XDK_CDevice_Viewport;
    float vx = float(R32(viewport)),      vy = float(R32(viewport + 4));
    float vw = float(R32(viewport + 8)),  vh = float(R32(viewport + 12));

    static const uint32_t buffer  = mem::AllocSystem(kMaxVertices * sizeof(Vertex), 16);
    static const uint32_t offset  = mem::AllocSystem(8, 16);
    static uint32_t        colours = mem::AllocSystem(kMaxVertices * sizeof(float[4]), 16);

    auto& opt = Opt();
    float er = opt.esp_color_r, eg = opt.esp_color_g,
          eb = opt.esp_color_b, ea = opt.esp_color_a;
    // skeleton slightly dimmer so box stands out
    float sr = er*0.75f, sg = eg*0.75f, sb = eb*0.75f;

    float width = opt.esp_line_thickness * unit;

    Vertex* first = static_cast<Vertex*>(HostPtr(buffer));
    Vertex* v     = first;
    auto    now   = std::chrono::steady_clock::now();
    size_t  kept  = 0;

    for (size_t i = 0; i < g_count; i++) {
        uint32_t who = g_seen[i].who;
        if (now - g_seen[i].at > kSeen ||
            R32(who + kCharKind) != kEnemyKind ||
            R32(who + kCharDying)) continue;
        g_seen[kept++] = g_seen[i];

        const float* at = &G<float>(who + kCharPosition);

        // distance check
        if (opt.esp_max_distance > 0.0f) {
            float chest[3] = {at[0], at[1]+kChest, at[2]};
            if (DistanceTo(chest) > opt.esp_max_distance) continue;
        }

        float feet[3] = {at[0], at[1],          at[2]};
        float head[3] = {at[0], at[1]+kHeight,  at[2]};
        float f[2], h[2];
        if (!Project(feet, vx, vy, vw, vh, f) ||
            !Project(head, vx, vy, vw, vh, h)) continue;

        float half  = (f[1]-h[1]) * kBoxWidth * 0.5f;
        float left  = f[0] - half, right = f[0] + half;
        size_t vbase = size_t(v - first);

        // --- line from bottom of screen ---
        if (opt.esp_line) {
            size_t before = size_t(v - first);
            v = Segment(v, vx+vw*0.5f, vy+vh, f[0], f[1], width);
            FillColour(colours, before, size_t(v-first)-before, er, eg, eb, ea);
        }

        // --- box ---
        if (opt.esp_box) {
            size_t before = size_t(v - first);
            v = Segment(v, left,  h[1], right, h[1], width); // top
            v = Segment(v, left,  f[1], right, f[1], width); // bottom
            v = Segment(v, left,  h[1], left,  f[1], width); // left
            v = Segment(v, right, h[1], right, f[1], width); // right
            FillColour(colours, before, size_t(v-first)-before, er, eg, eb, ea);
        }

        // --- skeleton ---
        if (opt.esp_skeleton) {
            // project all joints
            float js[kJointCount][2];
            bool  jv[kJointCount];
            for (size_t j = 0; j < kJointCount; j++) {
                float wp[3] = {
                    at[0] + kJoints[j][0],
                    at[1] + kJoints[j][1],
                    at[2] + kJoints[j][2]
                };
                jv[j] = Project(wp, vx, vy, vw, vh, js[j]);
            }
            size_t before = size_t(v - first);
            for (size_t b = 0; b < kBoneCount; b++) {
                int j0 = kBones[b][0], j1 = kBones[b][1];
                if (!jv[j0] || !jv[j1]) continue;
                v = Segment(v, js[j0][0], js[j0][1], js[j1][0], js[j1][1], width*0.8f);
            }
            FillColour(colours, before, size_t(v-first)-before, sr, sg, sb, ea);
        }

        (void)vbase;
    }

    g_count = kept;
    if (v == first) return;

    W32(offset, 0); W32(offset+4, 0);
    uint32_t eax = ctx.eax, ecx = ctx.ecx;
    ctx.eax = uint32_t(v - first) / 3;
    ctx.ecx = buffer;
    CallGuest(ctx, kTriangles, {offset, colours});
    ctx.eax = eax;
    ctx.ecx = ecx;
}

}  // namespace game

X86_EXTERN_FUNC(__imp__sub_001DFD90);
X86_FUNC(sub_001DFD90) {
    uint32_t who = ctx.ecx;
    if ((game::Opt().esp || game::Opt().aimbot) &&
        who && R32(who + kCharKind) == kEnemyKind && !R32(who + kCharDying))
        Remember(who);
    __imp__sub_001DFD90(ctx, base);
}
