#include "oled_face.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265f;

// Animation timing.
constexpr uint32_t kTickMs = 40;            // timer period (25 fps at most)
constexpr float kLidCloseMs = 110.0f;       // eyes close when the emotion changes
constexpr float kLidOpenMs = 140.0f;        // ... and re-open with the new face
constexpr float kBlinkMs = 150.0f;
constexpr float kEaseMs = 85.0f;            // time constant for smooth movement
constexpr uint32_t kAnimStepMs = 120;       // step of the extras animation (z, tears, dots)

// Layout in the 64x48 design space.
constexpr float kEyeX[2] = {18.0f, 46.0f};
constexpr float kEyeY = 17.0f;
constexpr float kMouthX = 32.0f;
constexpr float kMouthY = 37.0f;

// Extras that can be added to a face.
enum : uint16_t {
    kTears = 1 << 0,      // falling tears under both eyes
    kTearSlow = 1 << 1,   // one slow tear
    kSweat = 1 << 2,      // sweat drop at the top right
    kZzz = 1 << 3,        // floating z z z
    kQuestion = 1 << 4,   // question mark
    kDots = 1 << 5,       // "..." thinking dots
    kHeartFloat = 1 << 6, // small floating heart
    kBrowAngry = 1 << 7,
    kBrowSad = 1 << 8,
    kTongue = 1 << 9,     // tongue in an open (grin) mouth
    kHalfLid = 1 << 10,   // heavy eyelids (top of round eyes cut off)
};

// ---- drawing helpers -----------------------------------------------------------------------

struct Canvas {
    lv_layer_t* layer;
    float ox, oy, u;

    int X(float x) const { return static_cast<int>(lroundf(ox + x * u)); }
    int Y(float y) const { return static_cast<int>(lroundf(oy + y * u)); }
    int S(float s) const { return static_cast<int>(lroundf(s * u)); }
    int Stroke(float s) const { return std::max(2, S(s)); }
};

lv_color_t Ink(bool lit) { return lit ? lv_color_black() : lv_color_white(); }

// Filled rounded rectangle centred on (cx, cy). radius < 0 means fully rounded (pill/circle).
void FillRect(const Canvas& c, float cx, float cy, float w, float h, bool lit = true,
              float radius = -1) {
    int pw = std::max(2, c.S(w));
    int ph = std::max(2, c.S(h));
    lv_area_t a;
    a.x1 = c.X(cx) - pw / 2;
    a.y1 = c.Y(cy) - ph / 2;
    a.x2 = a.x1 + pw - 1;
    a.y2 = a.y1 + ph - 1;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = Ink(lit);
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius < 0 ? LV_RADIUS_CIRCLE : std::max(0, c.S(radius));
    lv_draw_rect(c.layer, &d, &a);
}

void FillCircle(const Canvas& c, float cx, float cy, float r, bool lit = true) {
    FillRect(c, cx, cy, 2 * r, 2 * r, lit);
}

// Hollow circle (ring) of outer radius r.
void Ring(const Canvas& c, float cx, float cy, float r, float stroke) {
    int pr = std::max(2, c.S(r));
    lv_area_t a;
    a.x1 = c.X(cx) - pr;
    a.y1 = c.Y(cy) - pr;
    a.x2 = a.x1 + 2 * pr - 1;
    a.y2 = a.y1 + 2 * pr - 1;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_color = lv_color_black();
    d.border_opa = LV_OPA_COVER;
    d.border_width = c.Stroke(stroke);
    d.border_side = LV_BORDER_SIDE_FULL;
    lv_draw_rect(c.layer, &d, &a);
}

void Line(const Canvas& c, float x1, float y1, float x2, float y2, float w, bool lit = true,
          int min_px = 2) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = c.X(x1);
    d.p1.y = c.Y(y1);
    d.p2.x = c.X(x2);
    d.p2.y = c.Y(y2);
    d.color = Ink(lit);
    d.width = std::max(min_px, c.S(w));
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;
    lv_draw_line(c.layer, &d);
}

void Triangle(const Canvas& c, float x1, float y1, float x2, float y2, float x3, float y3) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.p[0].x = c.X(x1);
    d.p[0].y = c.Y(y1);
    d.p[1].x = c.X(x2);
    d.p[1].y = c.Y(y2);
    d.p[2].x = c.X(x3);
    d.p[2].y = c.Y(y3);
    d.color = lv_color_black();
    d.opa = LV_OPA_COVER;
    lv_draw_triangle(c.layer, &d);
}

// Circular arc through two points 'chord' apart on the line y, bulging by 'depth'.
// smile=true bulges downwards (a "u"), smile=false upwards (a "n" / "^").
void ArcChord(const Canvas& c, float cx, float y, float chord, float depth, bool smile,
              float stroke) {
    depth = std::max(depth, 0.6f);
    float r = (chord * chord / 4.0f + depth * depth) / (2.0f * depth);
    float centre_y = smile ? (y + depth - r) : (y - depth + r);
    float theta = atan2f(r - depth, chord / 2.0f) * 180.0f / kPi;  // 0..90 degrees
    int w = c.Stroke(stroke);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = c.X(cx);
    d.center.y = c.Y(centre_y);
    d.radius = static_cast<uint16_t>(std::max(1, c.S(r) + w / 2));
    d.width = w;
    d.start_angle = static_cast<lv_value_precise_t>(smile ? theta : 180.0f + theta);
    d.end_angle = static_cast<lv_value_precise_t>(smile ? 180.0f - theta : 360.0f - theta);
    d.color = lv_color_black();
    d.opa = LV_OPA_COVER;
    d.rounded = 1;
    lv_draw_arc(c.layer, &d);
}

// Filled half disc (flat side up) = open, smiling mouth.
void HalfDisc(const Canvas& c, float cx, float top_y, float r) {
    int pr = std::max(2, c.S(r));
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = c.X(cx);
    d.center.y = c.Y(top_y);
    d.radius = static_cast<uint16_t>(pr);
    d.width = pr;
    d.start_angle = 0;
    d.end_angle = 180;
    d.color = lv_color_black();
    d.opa = LV_OPA_COVER;
    d.rounded = 0;
    lv_draw_arc(c.layer, &d);
}

void Heart(const Canvas& c, float cx, float cy, float s) {
    float r = s * 0.27f;
    FillCircle(c, cx - s * 0.25f, cy - s * 0.18f, r);
    FillCircle(c, cx + s * 0.25f, cy - s * 0.18f, r);
    Triangle(c, cx - s * 0.5f, cy - s * 0.02f, cx + s * 0.5f, cy - s * 0.02f, cx, cy + s * 0.5f);
}

void LetterZ(const Canvas& c, float x, float y, float s) {
    // Thin (1 px) strokes so a 4 px letter stays readable.
    Line(c, x, y, x + s, y, 1.2f, true, 1);
    Line(c, x + s, y, x, y + s, 1.2f, true, 1);
    Line(c, x, y + s, x + s, y + s, 1.2f, true, 1);
}

void QuestionMark(const Canvas& c, float x, float y) {
    int w = c.Stroke(2.2f);
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = c.X(x);
    d.center.y = c.Y(y + 3.8f);
    d.radius = static_cast<uint16_t>(std::max(2, c.S(3.8f) + w / 2));
    d.width = w;
    d.start_angle = 180;
    d.end_angle = 90;
    d.color = lv_color_black();
    d.opa = LV_OPA_COVER;
    d.rounded = 1;
    lv_draw_arc(c.layer, &d);
    Line(c, x, y + 7.6f, x, y + 9.4f, 2.2f);
    FillCircle(c, x, y + 13.0f, 1.2f);
}

}  // namespace

// ---- emotion table ---------------------------------------------------------------------------

struct OledFace::Pose {
    const char* name;
    Eye eye_l, eye_r;
    float h_l, h_r;   // eye height (design units)
    float w_l, w_r;   // eye width
    float look_x, look_y;  // -100..100
    Mouth mouth;
    float curve;      // -100 frown .. 100 smile
    float mouth_w;
    float mouth_dx;
    float ring_r;
    uint16_t extras;
};

using Eye = OledFace::Eye;
using Mouth = OledFace::Mouth;

// clang-format off
static const OledFace::Pose kPoses[] = {
    //  name         eye L          eye R          h_l   h_r   w_l   w_r  look_x look_y  mouth          curve  w    dx   ring  extras
    {"neutral",    Eye::Open,     Eye::Open,     22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   15.f, 16.f, 0.f, 0.f, 0},
    {"happy",      Eye::ArcUp,    Eye::ArcUp,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   85.f, 22.f, 0.f, 0.f, 0},
    {"laughing",   Eye::ArcUp,    Eye::ArcUp,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Grin,     0.f, 24.f, 0.f, 0.f, 0},
    {"funny",      Eye::Open,     Eye::Open,     20.f, 20.f, 13.f, 13.f,   0.f,   0.f, Mouth::Grin,     0.f, 22.f, 0.f, 0.f, kTongue},
    {"sad",        Eye::Open,     Eye::Open,     20.f, 20.f, 12.f, 12.f,   0.f,  10.f, Mouth::Curve,  -60.f, 14.f, 0.f, 0.f, kBrowSad | kTearSlow},
    {"angry",      Eye::Open,     Eye::Open,     15.f, 15.f, 14.f, 14.f,   0.f,   0.f, Mouth::Curve,  -45.f, 18.f, 0.f, 0.f, kBrowAngry},
    {"crying",     Eye::Squeeze,  Eye::Squeeze,  22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Wail,      0.f, 15.f, 0.f, 0.f, kTears},
    {"loving",     Eye::Heart,    Eye::Heart,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   85.f, 22.f, 0.f, 0.f, 0},
    {"embarrassed",Eye::ArcDown,  Eye::ArcDown,  22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Wavy,     0.f, 14.f, 0.f, 0.f, kSweat},
    {"surprised",  Eye::Open,     Eye::Open,     28.f, 28.f, 15.f, 15.f,   0.f,   0.f, Mouth::Ring,     0.f, 16.f, 0.f, 5.0f, 0},
    {"shocked",    Eye::Open,     Eye::Open,     32.f, 32.f, 17.f, 17.f,   0.f,   0.f, Mouth::Ring,     0.f, 16.f, 0.f, 7.0f, 0},
    {"thinking",   Eye::Open,     Eye::Open,     22.f, 22.f, 13.f, 13.f,  40.f, -40.f, Mouth::Curve,    0.f, 11.f, 6.f, 0.f, kDots},
    {"winking",    Eye::Open,     Eye::ArcUp,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   80.f, 20.f, 0.f, 0.f, 0},
    {"cool",       Eye::Shades,   Eye::Shades,   22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   45.f, 16.f, 0.f, 0.f, 0},
    {"relaxed",    Eye::ArcDown,  Eye::ArcDown,  22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Curve,   55.f, 18.f, 0.f, 0.f, 0},
    {"delicious",  Eye::ArcUp,    Eye::ArcUp,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Grin,     0.f, 20.f, 0.f, 0.f, kTongue},
    {"kissy",      Eye::ArcUp,    Eye::ArcUp,    22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Pucker,   0.f, 10.f, 2.f, 3.4f, kHeartFloat},
    {"confident",  Eye::Open,     Eye::Open,     18.f, 18.f, 14.f, 14.f,  35.f,   0.f, Mouth::Curve,   60.f, 18.f, 4.f, 0.f, kHalfLid},
    {"sleepy",     Eye::Line,     Eye::Line,     22.f, 22.f, 13.f, 13.f,   0.f,   0.f, Mouth::Ring,     0.f, 10.f, 0.f, 2.6f, kZzz},
    {"silly",      Eye::Open,     Eye::Open,     24.f, 14.f, 14.f,  9.f,   0.f,   0.f, Mouth::Grin,     0.f, 18.f, 0.f, 0.f, kTongue},
    {"confused",   Eye::Open,     Eye::Open,     22.f, 15.f, 13.f, 13.f, -40.f,   0.f, Mouth::Wavy,     0.f, 16.f, 0.f, 0.f, kQuestion},
};
// clang-format on

static const OledFace::Pose* FindPose(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (const auto& pose : kPoses) {
        if (strcmp(pose.name, name) == 0) {
            return &pose;
        }
    }
    return nullptr;
}

// ---- OledFace ----------------------------------------------------------------------------------

OledFace::OledFace(lv_obj_t* parent, int width, int height) : width_(width), height_(height) {
    obj_ = lv_obj_create(parent);
    lv_obj_set_size(obj_, width, height);
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj_, 0, 0);
    lv_obj_set_style_outline_width(obj_, 0, 0);
    lv_obj_set_style_shadow_width(obj_, 0, 0);
    lv_obj_set_style_pad_all(obj_, 0, 0);
    lv_obj_set_style_radius(obj_, 0, 0);
    lv_obj_add_event_cb(obj_, DrawCb, LV_EVENT_DRAW_MAIN, this);

    uint32_t now = lv_tick_get();
    rng_ ^= now * 2654435761u;
    if (rng_ == 0) {
        rng_ = 0x2545F491u;
    }
    last_tick_ = now;
    anim_ms_ = now;
    next_blink_ = now + Rand(1500, 3500);
    next_glance_ = now + Rand(2500, 6000);

    cur_.lid = 1.0f;
    cur_.blink = 1.0f;
    const Pose* neutral = FindPose("neutral");
    ApplyDiscrete(*neutral);
    target_ = neutral;

    timer_ = lv_timer_create(TimerCb, kTickMs, this);
}

OledFace::~OledFace() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (obj_ != nullptr) {
        lv_obj_delete(obj_);
        obj_ = nullptr;
    }
}

uint32_t OledFace::Rand(uint32_t lo, uint32_t hi) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + rng_ % (hi - lo + 1);
}

bool OledFace::SetEmotion(const char* name) {
    const Pose* pose = FindPose(name);
    if (pose == nullptr) {
        return false;
    }
    if (pose == target_ && pending_ == nullptr) {
        return true;  // already showing it
    }
    pending_ = pose;
    return true;
}

// Switch to the discrete parts of a pose (eye/mouth style, extras) and snap the shape values.
// Called while the eyes are closed, so the change is hidden by the blink.
void OledFace::ApplyDiscrete(const Pose& pose) {
    eye_style_[0] = pose.eye_l;
    eye_style_[1] = pose.eye_r;
    mouth_style_ = pose.mouth;
    extras_ = pose.extras;
    cur_.eye_h[0] = pose.h_l;
    cur_.eye_h[1] = pose.h_r;
    cur_.eye_w[0] = pose.w_l;
    cur_.eye_w[1] = pose.w_r;
    cur_.curve = pose.curve;
    cur_.mouth_w = pose.mouth_w;
    cur_.mouth_dx = pose.mouth_dx;
    cur_.ring_r = pose.ring_r;
}

void OledFace::TimerCb(lv_timer_t* timer) {
    static_cast<OledFace*>(lv_timer_get_user_data(timer))->OnTimer();
}

void OledFace::DrawCb(lv_event_t* e) {
    auto* self = static_cast<OledFace*>(lv_event_get_user_data(e));
    self->Draw(lv_event_get_layer(e));
}

void OledFace::OnTimer() {
    if (obj_ == nullptr) {
        return;
    }
    uint32_t now = lv_tick_get();
    uint32_t dt = now - last_tick_;
    if (dt == 0 || dt > 250) {
        dt = kTickMs;
    }
    last_tick_ = now;
    float k = 1.0f - expf(-static_cast<float>(dt) / kEaseMs);

    // Emotion change: close the eyes, swap the face, open the eyes again.
    if (pending_ != nullptr) {
        cur_.lid -= static_cast<float>(dt) / kLidCloseMs;
        if (cur_.lid <= 0.02f) {
            cur_.lid = 0.0f;
            ApplyDiscrete(*pending_);
            target_ = pending_;
            pending_ = nullptr;
        }
    } else if (cur_.lid < 1.0f) {
        cur_.lid = std::min(1.0f, cur_.lid + static_cast<float>(dt) / kLidOpenMs);
    }

    // Random blink.
    if (!blinking_ && now >= next_blink_) {
        blinking_ = true;
        blink_start_ = now;
    }
    if (blinking_) {
        float p = static_cast<float>(now - blink_start_) / kBlinkMs;
        if (p >= 1.0f) {
            blinking_ = false;
            cur_.blink = 1.0f;
            next_blink_ = now + Rand(2200, 5200);
        } else {
            cur_.blink = 1.0f - 0.9f * sinf(p * kPi);
        }
    }

    // Idle glances (only for round open eyes).
    if (now >= next_glance_) {
        if (glance_tx_ != 0.0f || glance_ty_ != 0.0f) {
            glance_tx_ = glance_ty_ = 0.0f;
            next_glance_ = now + Rand(2500, 6000);
        } else {
            glance_tx_ = static_cast<float>(static_cast<int>(Rand(0, 60)) - 30);
            glance_ty_ = static_cast<float>(static_cast<int>(Rand(0, 24)) - 12);
            next_glance_ = now + Rand(700, 1300);
        }
    }
    glance_x_ += (glance_tx_ - glance_x_) * k;
    glance_y_ += (glance_ty_ - glance_y_) * k;

    float look_gain = (eye_style_[0] == Eye::Open || eye_style_[1] == Eye::Open) ? 1.0f : 0.0f;
    float look_target_x = (target_ ? target_->look_x : 0.0f) + glance_x_ * look_gain;
    float look_target_y = (target_ ? target_->look_y : 0.0f) + glance_y_ * look_gain;
    cur_.look_x += (look_target_x - cur_.look_x) * k;
    cur_.look_y += (look_target_y - cur_.look_y) * k;

    // Talking mouth.
    float talk_target = talking_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    cur_.talk += (talk_target - cur_.talk) * k;
    if (cur_.talk < 0.02f) {
        cur_.talk = 0.0f;
        cur_.talk_open = 0.0f;
    } else {
        float t = static_cast<float>(now);
        float a = 0.5f + 0.5f * sinf(t * 0.024f);
        float b = 0.6f + 0.4f * sinf(t * 0.0083f + 1.3f);
        cur_.talk_open = a * b;
    }

    // The z / tears / dots animation advances in coarse steps so we do not redraw for nothing.
    anim_ms_ = (now / kAnimStepMs) * kAnimStepMs;

    // Redraw only when something visible changed.
    int16_t snapshot[kSnapshotSize] = {};
    ComputeSnapshot(snapshot, kSnapshotSize);
    if (!snapshot_valid_ || memcmp(snapshot, last_snapshot_, sizeof(snapshot)) != 0) {
        memcpy(last_snapshot_, snapshot, sizeof(snapshot));
        snapshot_valid_ = true;
        lv_obj_invalidate(obj_);
    }
}

void OledFace::ComputeSnapshot(int16_t* out, int count) const {
    auto q = [](float v, float scale) { return static_cast<int16_t>(lroundf(v * scale)); };
    float open = cur_.lid * cur_.blink;
    int i = 0;
    auto put = [&](int16_t v) {
        if (i < count) {
            out[i++] = v;
        }
    };
    put(q(cur_.eye_h[0] * open, 4));
    put(q(cur_.eye_h[1] * open, 4));
    put(q(cur_.eye_w[0], 4));
    put(q(cur_.eye_w[1], 4));
    put(q(cur_.look_x, 2));
    put(q(cur_.look_y, 2));
    put(q(cur_.curve, 1));
    put(q(cur_.mouth_w, 2));
    put(q(cur_.mouth_dx, 2));
    put(q(cur_.ring_r, 4));
    put(q(cur_.talk, 20));
    put(q(cur_.talk_open, 20));
    put(q(cur_.lid, 20));
    put(static_cast<int16_t>(eye_style_[0]));
    put(static_cast<int16_t>(eye_style_[1]));
    put(static_cast<int16_t>(mouth_style_));
    put(static_cast<int16_t>(extras_));
    // Extras and the breathing "o" mouth animate with time.
    bool animated = extras_ != 0;
    put(animated ? static_cast<int16_t>((anim_ms_ / kAnimStepMs) & 0x7FFF) : 0);
}

void OledFace::Draw(lv_layer_t* layer) {
    if (obj_ == nullptr) {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(obj_, &area);

    float u = std::min(static_cast<float>(width_) / 64.0f, static_cast<float>(height_) / 48.0f);
    Canvas c;
    c.layer = layer;
    c.u = u;
    c.ox = static_cast<float>(area.x1) + (static_cast<float>(width_) - 64.0f * u) / 2.0f;
    c.oy = static_cast<float>(area.y1) + (static_cast<float>(height_) - 48.0f * u) / 2.0f;

    const float open = cur_.lid * cur_.blink;
    const float phase = static_cast<float>(anim_ms_) / 1000.0f;
    const float dx = cur_.look_x * 0.06f;
    const float dy = cur_.look_y * 0.07f;

    // ---- eyes ----
    if (eye_style_[0] == Eye::Shades) {
        float lid_h = 12.0f * (0.35f + 0.65f * cur_.lid);
        for (int i = 0; i < 2; i++) {
            FillRect(c, kEyeX[i], kEyeY + 1.0f, 22.0f, lid_h, true, 5.0f);
        }
        Line(c, 28.0f, kEyeY - 2.0f, 36.0f, kEyeY - 2.0f, 2.5f);
    } else {
        for (int i = 0; i < 2; i++) {
            float x = kEyeX[i] + dx;
            float y = kEyeY + dy;
            float w = cur_.eye_w[i];
            switch (eye_style_[i]) {
                case Eye::Open: {
                    float h = std::max(2.5f, cur_.eye_h[i] * open);
                    FillRect(c, x, y, w, h);
                    break;
                }
                case Eye::ArcUp:
                    ArcChord(c, kEyeX[i], kEyeY + 3.0f, 13.0f, std::max(0.8f, 6.0f * cur_.lid),
                             false, 3.0f);
                    break;
                case Eye::ArcDown:
                    ArcChord(c, kEyeX[i], kEyeY - 2.0f, 13.0f, std::max(0.8f, 5.0f * cur_.lid),
                             true, 3.0f);
                    break;
                case Eye::Line:
                    ArcChord(c, kEyeX[i], kEyeY, 13.0f, 2.2f, true, 3.0f);
                    break;
                case Eye::Squeeze: {
                    // ">" for the left eye, "<" for the right eye (tightly shut eyes)
                    float d = (i == 0) ? 1.0f : -1.0f;
                    float half = std::max(0.8f, 5.0f * cur_.lid);
                    Line(c, kEyeX[i] - d * 4.5f, kEyeY - half, kEyeX[i] + d * 3.5f, kEyeY, 3.0f);
                    Line(c, kEyeX[i] + d * 3.5f, kEyeY, kEyeX[i] - d * 4.5f, kEyeY + half, 3.0f);
                    break;
                }
                case Eye::Heart:
                    Heart(c, kEyeX[i], kEyeY, 17.0f * (0.5f + 0.5f * cur_.lid));
                    break;
                case Eye::Shades:
                    break;
            }
        }
    }

    // ---- heavy eyelids: cut the top off round eyes ----
    if (extras_ & kHalfLid) {
        for (int i = 0; i < 2; i++) {
            if (eye_style_[i] != Eye::Open) {
                continue;
            }
            float h = std::max(2.5f, cur_.eye_h[i] * open);
            float top = kEyeY + dy - h / 2.0f - 1.0f;
            float cover = 0.45f * h + 1.0f;
            FillRect(c, kEyeX[i] + dx, top + cover / 2.0f, cur_.eye_w[i] + 4.0f, cover, false, 0.0f);
        }
    }

    // ---- eyebrows ----
    if (extras_ & kBrowAngry) {
        Line(c, 10.0f, 3.5f, 25.0f, 8.5f, 2.5f);
        Line(c, 54.0f, 3.5f, 39.0f, 8.5f, 2.5f);
    }
    if (extras_ & kBrowSad) {
        Line(c, 11.0f, 5.5f, 25.0f, 2.5f, 2.5f);
        Line(c, 53.0f, 5.5f, 39.0f, 2.5f, 2.5f);
    }

    // ---- mouth ----
    const float mx = kMouthX + cur_.mouth_dx;
    const float talk_open = cur_.talk * cur_.talk_open;
    const bool talking = cur_.talk > 0.5f;
    switch (mouth_style_) {
        case Mouth::Ring: {
            float r = cur_.ring_r;
            if (extras_ & kZzz) {
                r += 0.7f * sinf(phase * 2.6f);  // slow breathing
            }
            if (talking) {
                r = std::max(r, 3.0f) * (0.7f + 0.7f * talk_open);
            }
            if (r < 3.6f) {
                FillCircle(c, mx, 39.0f, r);
            } else {
                Ring(c, mx, 39.0f, r, r >= 6.0f ? 3.0f : 2.5f);
            }
            break;
        }
        case Mouth::Wail: {
            // wide open crying mouth that wobbles a little
            float h = talking ? 4.0f + 9.0f * talk_open : 8.0f + 1.5f * sinf(phase * 5.0f);
            FillRect(c, mx, 39.0f, cur_.mouth_w, h);
            break;
        }
        case Mouth::Pucker:
            if (talking) {
                FillRect(c, mx, 38.0f, 8.0f, 3.0f + 8.0f * talk_open);
            } else {
                Ring(c, mx + 1.0f, 38.0f, cur_.ring_r, 2.5f);
                Line(c, mx - 7.0f, 37.0f, mx - 3.5f, 38.0f, 2.5f);
            }
            break;
        case Mouth::Wavy:
            if (talking) {
                FillRect(c, mx, 38.0f, cur_.mouth_w * 0.7f, 3.0f + 9.0f * talk_open);
            } else {
                float x0 = mx - cur_.mouth_w / 2.0f;
                float seg = cur_.mouth_w / 4.0f;
                for (int i = 0; i < 4; i++) {
                    float ya = kMouthY + ((i % 2) ? 2.2f : -2.2f);
                    float yb = kMouthY + ((i % 2) ? -2.2f : 2.2f);
                    Line(c, x0 + seg * i, ya, x0 + seg * (i + 1), yb, 2.5f);
                }
            }
            break;
        case Mouth::Grin:
            if (talking) {
                FillRect(c, mx, 38.0f, cur_.mouth_w * 0.8f, 3.0f + 11.0f * talk_open);
            } else {
                float r = cur_.mouth_w / 2.0f;
                HalfDisc(c, mx, 33.0f, r);
                if (extras_ & kTongue) {
                    // tongue: a dark disc inside the open mouth
                    FillCircle(c, mx + r * 0.28f, 33.0f + r * 0.55f, 3.3f, false);
                }
            }
            break;
        case Mouth::Curve:
        default:
            if (talking) {
                FillRect(c, mx, 38.0f, cur_.mouth_w * 0.7f, 3.0f + 11.0f * talk_open);
            } else if (fabsf(cur_.curve) < 10.0f) {
                Line(c, mx - cur_.mouth_w / 2.0f, kMouthY, mx + cur_.mouth_w / 2.0f, kMouthY, 3.0f);
            } else if (cur_.curve > 0.0f) {
                ArcChord(c, mx, 35.0f, cur_.mouth_w, cur_.curve * 0.08f, true, 3.0f);
            } else {
                ArcChord(c, mx, 41.0f, cur_.mouth_w, -cur_.curve * 0.08f, false, 3.0f);
            }
            break;
    }

    // ---- extras ----
    if (extras_ & kTears) {
        for (int i = 0; i < 2; i++) {
            float fall = fmodf(phase * 16.0f + static_cast<float>(i) * 7.0f, 15.0f);
            FillCircle(c, kEyeX[i] + (i == 0 ? -2.0f : 2.0f), kEyeY + 7.0f + fall, 2.0f);
        }
    }
    if (extras_ & kTearSlow) {
        float fall = fmodf(phase * 5.0f, 16.0f);
        FillCircle(c, kEyeX[0] - 3.0f, kEyeY + 11.0f + fall, 1.8f);
    }
    if (extras_ & kSweat) {
        float bob = sinf(phase * 3.0f) * 0.8f;
        Triangle(c, 58.0f, 1.0f + bob, 55.6f, 6.5f + bob, 60.4f, 6.5f + bob);
        FillCircle(c, 58.0f, 8.0f + bob, 2.6f);
    }
    if (extras_ & kZzz) {
        int n = static_cast<int>(fmodf(phase, 2.4f) / 0.6f);  // 0..3 letters visible
        if (n >= 1) LetterZ(c, 54.0f, 8.0f, 3.0f);
        if (n >= 2) LetterZ(c, 57.0f, 4.0f, 3.5f);
        if (n >= 3) LetterZ(c, 60.0f, 0.5f, 3.5f);
    }
    if (extras_ & kQuestion) {
        float bob = sinf(phase * 4.0f) * 0.7f;
        QuestionMark(c, 57.5f, 1.5f + bob);
    }
    if (extras_ & kDots) {
        int lit = static_cast<int>(phase * 2.2f) % 4;  // 0..3 dots on
        for (int i = 0; i < 3; i++) {
            if (i < lit) {
                FillCircle(c, 56.5f + 3.0f * static_cast<float>(i), 3.0f, 1.2f);
            }
        }
    }
    if (extras_ & kHeartFloat) {
        float bob = sinf(phase * 4.0f) * 1.2f;
        Heart(c, 58.0f, 8.0f + bob, 7.0f);
    }
}
