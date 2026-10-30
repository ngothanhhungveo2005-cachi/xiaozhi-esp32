#ifndef OLED_FACE_H
#define OLED_FACE_H

#include <lvgl.h>

#include <atomic>
#include <cstdint>

// A small animated cartoon face (eyes + mouth) for monochrome OLED screens.
//
// The face is drawn with LVGL primitives inside a single transparent object, so it costs no
// bitmaps or fonts. All shapes are laid out in a 64x48 design space and scaled to whatever size
// the object has (64x48 on a 128x64 screen, 32x32 on a 128x32 screen).
//
// Animation (all driven by one lv_timer, only redraws when something visibly changed):
//   - eyes blink at random intervals and glance around while idle
//   - every emotion change closes the eyes for a moment and re-opens them with the new face
//   - while SetTalking(true) the mouth opens and closes
//   - sleepy / crying / thinking / confused ... have small animated extras (z, tears, dots, ?)
//
// Colours: on these panels LVGL "black" is a lit pixel, so shapes are drawn in lv_color_black().
//
// Thread-safety: SetEmotion() and the constructor/destructor must be called with the LVGL lock
// held (OledDisplay does that). SetTalking() only flips an atomic flag and can be called anywhere.
class OledFace {
public:
    OledFace(lv_obj_t* parent, int width, int height);
    ~OledFace();

    OledFace(const OledFace&) = delete;
    OledFace& operator=(const OledFace&) = delete;

    // Select the face for an emotion name ("happy", "sad", ...). Returns false, and changes
    // nothing, when the name is not one of the supported emotions.
    bool SetEmotion(const char* name);

    // Mouth flaps while true (device is speaking).
    void SetTalking(bool talking) { talking_.store(talking, std::memory_order_relaxed); }

    lv_obj_t* obj() const { return obj_; }

    enum class Eye : uint8_t { Open, ArcUp, ArcDown, Line, Squeeze, Heart, Shades };
    enum class Mouth : uint8_t { Curve, Grin, Ring, Wavy, Pucker, Wail };

    struct Pose;  // one entry of the emotion table (defined in oled_face.cc)

private:

    // Everything the drawing code needs, in design-space units. The continuous values are
    // eased towards their targets on every tick; the discrete ones switch while the eyes are
    // closed.
    struct Shape {
        float eye_h[2];
        float eye_w[2];
        float look_x, look_y;
        float curve;      // mouth: -100 frown .. +100 smile
        float mouth_w;
        float mouth_dx;   // sideways shift of the mouth (smirk)
        float ring_r;     // radius of an "o" mouth
        float talk;       // 0..1, how strongly the talking animation is applied
        float talk_open;  // 0..1, current mouth opening while talking
        float lid;        // 1 open .. 0 closed (emotion-change transition)
        float blink;      // 1 open .. ~0.1 closed (random blink)
    };

    void OnTimer();
    void Draw(lv_layer_t* layer);
    void ApplyDiscrete(const Pose& pose);
    void ComputeSnapshot(int16_t* out, int count) const;

    static void TimerCb(lv_timer_t* timer);
    static void DrawCb(lv_event_t* e);

    lv_obj_t* obj_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    int width_;
    int height_;

    std::atomic<bool> talking_{false};

    // Current (drawn) state.
    Shape cur_{};
    Eye eye_style_[2] = {Eye::Open, Eye::Open};
    Mouth mouth_style_ = Mouth::Curve;
    uint16_t extras_ = 0;

    // Target state of the emotion that was asked for.
    const Pose* target_ = nullptr;   // pose whose continuous values we ease towards
    const Pose* pending_ = nullptr;  // pose waiting for the eyes to close before it is applied

    // Time bookkeeping (milliseconds of lv_tick).
    uint32_t last_tick_ = 0;
    uint32_t anim_ms_ = 0;  // time used by the extras animation, advances in coarse steps
    uint32_t next_blink_ = 0;
    uint32_t blink_start_ = 0;
    bool blinking_ = false;
    uint32_t next_glance_ = 0;
    float glance_x_ = 0, glance_y_ = 0;
    float glance_tx_ = 0, glance_ty_ = 0;
    uint32_t rng_ = 0x2545F491u;

    static constexpr int kSnapshotSize = 24;
    int16_t last_snapshot_[kSnapshotSize] = {};
    bool snapshot_valid_ = false;

    uint32_t Rand(uint32_t lo, uint32_t hi);
};

#endif  // OLED_FACE_H
