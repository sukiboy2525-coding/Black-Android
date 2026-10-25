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

// skeleton joints: offset from feet (Y up)
constexpr float kJoints[][3] = {
    {-0.15f, 0.10f, 0.0f},  // 0 left foot
    { 0.15f, 0.10f, 0.0f},  // 1 right foot
    {-0.12f, 0.55f, 0.0f},  // 2 left knee
    { 0.12f, 0.55f, 0.0f},  // 3 right knee
    { 0.00f, 0.90f, 0.0f},  // 4 hips
    { 0.00f, 1.20f, 0.0f},  // 5 chest
    {-0.30f, 1.20f, 0.0f},  // 6 left shoulder
    { 0.30f, 1.20f, 0.0f},  // 7 right shoulder
    {-0.35f, 0.90f, 0.0f},  // 8 left elbow
    { 0.35f, 0.90f, 0.0f},  // 9 right elbow
    {-0.35f, 0.60f, 0.0f},  // 10 left hand
    { 0.35f, 0.60f, 0.0f},  // 11 right hand
    { 0.00f, 1.55f, 0.0f},  // 12 neck
    { 0.00f, 1.75f, 0.0f},  // 13 head
};
constexpr size_t kJointCount = sizeof(kJoints) / sizeof(kJoints[0]);

constexpr int kBones[][2] = {
    {0, 2}, {2, 4},   // left leg
    {1, 3}, {3, 4},   // right leg
    {4, 5},           // spine
    {5, 6}, {6, 8}, {8, 10},   // left arm
    {5, 7}, {7, 9}, {9, 11},   // right arm
    {5, 12}, {12, 13},         // neck + head
};
constexpr size_t kBoneCount = sizeof(kBones) / sizeof(kBones[0]);

constexpr auto   kSeen       = std::chrono::milliseconds(250);
constexpr size_t kMaxEnemies = 64;

struct Seen {
    uint32_t who;
    std::chrono::steady_clock::time_point at;
};
Seen   g_seen[kMaxEnemies];
size_t g_count;

float    g_view[16], g_projection[16];
bool     g_camera;
uint32_t g_camera_frame;

constexpr uint32_t kTriangles = 0xD5A00;
struct Vertex { float x, y; };

constexpr size_t kSegsPerEnemy  = 1 + 4 + kBoneCount;
constexpr size_t kMaxVertices   = kMaxEnemies * kSegsPerEnemy * 6;

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

float DistanceTo(const float p[3]) {
    float cx = -(g_view[0]*g_view[12] + g_view[1]*g_view[13] + g_view[2]*g_view[14]);
    float cy = -(g_view[4]*g_view[12] + g_view[5]*g_view[13] + g_view[6]*g_view[14]);
    float cz = -(g_view[8]*g_view[12] + g_view[9]*g_view[13] + g_view[10]*g_view[14]);
    float dx = p[0]-cx, dy = p[1]-cy, dz = p[2]-cz;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

Vertex* Segment(Vertex* v, float x0, float y0, float x1, float y1, float width) {
    float dx = x1-x0, dy = y1-y0;
    float len = std::sqrt(dx*dx + dy*dy);
    if (!(len > 0)) return v;
    float nx = -dy/len*width*0.5f, ny = dx/len*width*0.5f;
    const float c[4][2] = {{x0+nx,y0+ny},{x0-nx,y0-ny},{x1+nx,y1+ny},{x1-nx,y1-ny}};
    for (int i : {0,1,2,2,1,3}) *v++ = {c[i][0], c[i][1]};
    return v;
}

void FillColor(uint32_t buf, size_t from, size_t count, float r, float g, float b, float a) {
    float c[4] = {r, g, b, a};
    for (size_t i = from; i < from+count; i++)
        std::memcpy(HostPtr(buf + i*sizeof(c)), c, sizeof(c));
}

}  // namespace

namespace game {

void EspCamera() {
    uint32_t t = SYM(D3D_Device) + XDK_CDevice_Transforms;
    std::memcpy(g_view,       HostPtr(t),      sizeof(g_view));
    std::memcpy(g_projection, HostPtr(t + 64), sizeof(g_projection));
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
    float vx = float(R32(viewport)),     vy = float(R32(viewport + 4));
    float vw = float(R32(viewport + 8)), vh = float(R32(viewport + 12));

    static const uint32_t buffer  = mem::AllocSystem(kMaxVertices * sizeof(Vertex), 16);
    static const uint32_t offset  = mem::AllocSystem(8, 16);
    static const uint32_t colours = mem::AllocSystem(kMaxVertices * sizeof(float[4]), 16);

    auto& opt = Opt();
    // convert 0-100 uint to 0.0-1.0 float
    float er = opt.esp_color_r / 100.0f;
    float eg = opt.esp_color_g / 100.0f;
    float eb = opt.esp_color_b / 100.0f;
    float ea = opt.esp_color_a / 100.0f;
    float sr = er * 0.75f, sg = eg * 0.75f, sb = eb * 0.75f;
    float width = (opt.esp_thickness / 10.0f) * unit;

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

        // distance filter
        if (opt.esp_max_distance > 0) {
            float chest[3] = {at[0], at[1]+kChest, at[2]};
            if (DistanceTo(chest) > float(opt.esp_max_distance)) continue;
        }

        float feet[3] = {at[0], at[1],         at[2]};
        float head[3] = {at[0], at[1]+kHeight,  at[2]};
        float f[2], h[2];
        if (!Project(feet, vx, vy, vw, vh, f) ||
            !Project(head, vx, vy, vw, vh, h)) continue;

        float half  = (f[1]-h[1]) * kBoxWidth * 0.5f;
        float left  = f[0] - half, right = f[0] + half;

        // line
        if (opt.esp_line) {
            size_t b = size_t(v-first);
            v = Segment(v, vx+vw*0.5f, vy+vh, f[0], f[1], width);
            FillColor(colours, b, size_t(v-first)-b, er, eg, eb, ea);
        }

        // box
        if (opt.esp_box) {
            size_t b = size_t(v-first);
            v = Segment(v, left,  h[1], right, h[1], width);
            v = Segment(v, left,  f[1], right, f[1], width);
            v = Segment(v, left,  h[1], left,  f[1], width);
            v = Segment(v, right, h[1], right, f[1], width);
            FillColor(colours, b, size_t(v-first)-b, er, eg, eb, ea);
        }

        // skeleton
        if (opt.esp_skeleton) {
            float js[kJointCount][2];
            bool  jv[kJointCount];
            for (size_t j = 0; j < kJointCount; j++) {
                float wp[3] = {at[0]+kJoints[j][0], at[1]+kJoints[j][1], at[2]+kJoints[j][2]};
                jv[j] = Project(wp, vx, vy, vw, vh, js[j]);
            }
            size_t b = size_t(v-first);
            for (size_t bn = 0; bn < kBoneCount; bn++) {
                int j0 = kBones[bn][0], j1 = kBones[bn][1];
                if (!jv[j0] || !jv[j1]) continue;
                v = Segment(v, js[j0][0], js[j0][1], js[j1][0], js[j1][1], width*0.7f);
            }
            FillColor(colours, b, size_t(v-first)-b, sr, sg, sb, ea);
        }
    }

    g_count = kept;
    if (v == first) return;

    W32(offset, 0); W32(offset+4, 0);
    uint32_t eax = ctx.eax, ecx = ctx.ecx;
    ctx.eax = uint32_t(v-first) / 3;
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
