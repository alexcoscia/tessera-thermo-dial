#include "thermo_dial.h"
#include "thermo_logic.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome::thermo_dial {

static const char *const TAG = "thermo_dial";
using tessera::Font;
namespace ui = tessera::ui;

constexpr uint32_t DEBOUNCE_MS = 600;   // after the last tap on - or +
constexpr uint32_t CONFIRM_MS = 5000;   // how long Home Assistant has to report the target we sent
constexpr int MAX_MODES = 8;
constexpr int MODE_LEN = 16;
constexpr int NO_VALUE = INT_MIN;
// Tessera's icon set, by codepoint (components/smart_display/tile_icon_names.h): fire, snowflake, power.
constexpr uint32_t ICON_FIRE = 0xF0238, ICON_SNOWFLAKE = 0xF0717, ICON_POWER = 0xF0425;

class DialTile : public tessera::Tile {
 public:
  explicit DialTile(ThermoDial *plugin) : plugin_(plugin) {}
  ~DialTile() override { plugin_->detach(this); }   // LVGL's objects go with the card; nothing else is ours

  void create(const tessera::TileContext &c) override {
    std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    w_ = c.width;
    h_ = c.height;
    const char *separator = plugin_->text("decimal");
    decimal_ = *separator ? *separator : '.';
    // The ring stays this far from the tile's edge; the knob reaches a little beyond the ring.
    margin_ = ui::mm(3) + ui::px(6);
    const int key_h = ui::mm(9), gap = ui::px(4);
    layout_ = layout(w_, h_, c.columns, c.rows, key_h, margin_);
    // The keys only when the ring stays large beside them: the state and the temperature now stand in its middle.
    if (layout_.dial && layout_.keys && layout_.d < ui::mm(34))
      layout_ = layout(w_, h_, c.columns, c.rows, key_h, margin_, false);
    // A ring too small to read is no ring: the tile then shows the number, as a smaller tile does.
    if (layout_.dial && layout_.d < ui::mm(24)) layout_ = layout(w_, h_, c.columns, 1, key_h, margin_);
    // The name, the number and the keys must fit one over the other, or the keys stay out.
    if (!layout_.dial && layout_.keys &&
        ui::line_height(Font::BODY) + ui::line_height(Font::VALUE) + 3 * gap + key_h > h_) {
      layout_.keys = false;
      layout_.key_h = 0;
    }

    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    ui::set_text(name_, c.name);
    if (layout_.dial) {
      make_dial(c);
    } else {
      center_ = ui::label(c.parent, Font::VALUE, theme::INK);
      lv_obj_set_width(center_, w_);
      lv_obj_set_pos(center_, 0, ui::line_height(Font::BODY) + ui::px(4));
      lv_obj_set_width(name_, w_);
      lv_obj_set_pos(name_, 0, 0);
    }
    if (layout_.keys) make_keys(c);
    attached_ = plugin_->attach(this);
    apply();
    // DIAGNOSTICS (to remove): the input events LVGL sends to this tile's card (1 pressed, 3 press lost, 4 short click,
    // 8 long press, 10 clicked, 11 released, 12 scroll begin, 14 scroll end, 16 gesture).
    lv_obj_add_event_cb(lv_obj_get_parent(c.parent), &DialTile::debug_event, LV_EVENT_ALL, this);
    // Where this tile and its keys stand on the glass, to read beside the touch lines of the log.
    lv_obj_update_layout(c.parent);
    lv_area_t area, first, last;
    lv_obj_get_coords(c.parent, &area);
    ESP_LOGI(TAG, "geometry tile=(%d,%d)-(%d,%d) %ux%u cells dial=%d d=%d keys=%d", (int) area.x1, (int) area.y1,
             (int) area.x2, (int) area.y2, (unsigned) c.columns, (unsigned) c.rows, (int) layout_.dial, layout_.d,
             (int) layout_.keys);
    if (layout_.keys) {
      lv_obj_get_coords(keys_[0], &first);
      lv_obj_get_coords(keys_[2], &last);
      ESP_LOGI(TAG, "geometry key- =(%d,%d)-(%d,%d) key+ =(%d,%d)-(%d,%d)", (int) first.x1, (int) first.y1,
               (int) first.x2, (int) first.y2, (int) last.x1, (int) last.y1, (int) last.x2, (int) last.y2);
    }
  }

  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    JsonObjectConst a = data["attributes"];
    range_ = make_range(a["min_temp"] | 7.0, a["max_temp"] | 35.0, a["target_temp_step"] | 0.5);
    reported_ = number(a["temperature"]);
    current_ = number(a["current_temperature"]);
    mood_ = mood_of(state, a["hvac_action"] | "");
    std::snprintf(state_word_, sizeof state_word_, "%s", state.c_str());
    modes_n_ = 0;
    for (JsonVariantConst m : a["hvac_modes"].as<JsonArrayConst>()) {
      const char *mode = m.as<const char *>();
      if (modes_n_ >= MAX_MODES || !mode) continue;
      std::snprintf(modes_[modes_n_++], MODE_LEN, "%s", mode);
    }
    if (pending_active_ && reported_ == pending_) settle();   // Home Assistant brought the target we chose
    if (!dragging_ && !pending_active_) shown_ = reported_;
    if (!dragging_) apply();                                   // under a finger the dial does not move
  }

  void on_theme() override { apply(); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    const int count = modes_n_ >= 2 ? 3 : 2;
    const int key = key_at(p.x, p.y, w_, layout_, count);
    ESP_LOGI(TAG, "tap at=%d,%d tile=%dx%d keys=%d keys_y=%d key_h=%d count=%d key=%d mood=%d shown=%d reported=%d",
             p.x, p.y, w_, h_, (int) layout_.keys, layout_.keys_y, layout_.key_h, count, key, (int) mood_, shown_, reported_);
    if (mood_ == Mood::UNAVAILABLE) return;
    if (key < 0) return;
    if (count == 3 && key == 1) return change_mode();
    bump(key == 0 ? -1 : 1);
  }

  // Called by the plugin every 250 ms: the waits of this tile.
  void tick(uint32_t now) {
    if (debounce_on_ && expired(now, debounce_since_, DEBOUNCE_MS)) {
      debounce_on_ = false;
      send(shown_);
    }
    if (confirm_on_ && expired(now, confirm_since_, CONFIRM_MS)) {
      settle();                          // Home Assistant did not follow: back to what it says
      shown_ = reported_;
      if (!dragging_) apply();
    }
  }

 private:
  static int number(JsonVariantConst v) {
    return v.is<float>() || v.is<int>() ? tenths(v.as<double>()) : NO_VALUE;
  }

  void make_dial(const tessera::TileContext &c) {
    // The ring's width: thick, but never more than a sixth of the dial.
    const int ring = std::min(std::max(ui::mm(5), layout_.d / 9), layout_.d / 6);
    ring_ = ring;
    arc_ = lv_arc_create(c.parent);
    lv_obj_remove_style_all(arc_);
    lv_obj_set_size(arc_, layout_.d, layout_.d);
    lv_obj_set_pos(arc_, layout_.x, layout_.y);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_arc_set_range(arc_, 0, 56);
    for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
      lv_obj_set_style_arc_width(arc_, ring, part);
      lv_obj_set_style_arc_rounded(arc_, true, part);
    }
    lv_obj_set_style_bg_opa(arc_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(arc_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, ui::px(2), LV_PART_KNOB);
    lv_obj_add_event_cb(arc_, &DialTile::arc_event, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(arc_, &DialTile::arc_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(arc_, &DialTile::arc_event, LV_EVENT_PRESS_LOST, this);

    // What stands in the middle of the ring, from the top: the target, the temperature now, the state with its icon.
    // The first of these that fits inside the ring (each line within the chord of the inner circle where it stands):
    // everything with the large digits, then smaller faces, then without the state, then without the temperature now.
    struct Pass { Font face, now_face; bool now, state; };
    static const Pass passes[] = {
        {Font::DIGITS, Font::BODY_LARGE, true, true}, {Font::VALUE, Font::BODY_LARGE, true, true},
        {Font::VALUE, Font::BODY, true, true},        {Font::VALUE, Font::BODY_LARGE, true, false},
        {Font::VALUE, Font::BODY, true, false},       {Font::VALUE, Font::BODY, false, false}};
    const int gap = ui::px(4);
    const int hs = std::max(ui::line_height(Font::BODY), ui::line_height(Font::ICON_SMALL));
    const int cy = layout_.y + layout_.d / 2;
    int widest = 0;   // the widest state word, with its icon
    for (const char *key : {"heating", "cooling", "idle", "off", "unavailable"})
      widest = std::max(widest, ui::text_width(plugin_->text(key), Font::BODY));
    widest += ui::px(6) + ui::text_width(ui::icon(ICON_FIRE), Font::ICON_SMALL);
    const std::string sample = tessera::fill(plugin_->text("now"), "n", "88.8\xC2\xB0");
    for (const Pass &pass : passes) {
      const int hd = ui::line_height(pass.face), hn = ui::line_height(pass.now_face);
      const int total = hd + (pass.now ? gap + hn : 0) + (pass.state ? gap + hs : 0);
      int y = cy - total / 2;
      bool fits = inside(y, hd, ui::text_width("88.8\xC2\xB0", pass.face));
      y += hd + gap;
      if (pass.now) {
        fits = fits && inside(y, hn, ui::text_width(sample, pass.now_face));
        y += hn + gap;
      }
      if (pass.state) fits = fits && inside(y, hs, widest);
      if (fits || &pass == &passes[5]) {
        face_ = pass.face;
        now_face_ = pass.now_face;
        show_now_ = pass.now;
        show_state_ = pass.state;
        break;
      }
    }
    const int hd = ui::line_height(face_), hn = ui::line_height(now_face_);
    const int total = hd + (show_now_ ? gap + hn : 0) + (show_state_ ? gap + hs : 0);
    int y = cy - total / 2;
    center_ = ui::label(c.parent, face_, theme::INK);
    centre(center_, y);
    y += hd + gap;
    if (show_now_) {
      now_ = ui::label(c.parent, now_face_, theme::MUTED);
      centre(now_, y);
      y += hn + gap;
    }
    if (show_state_) {
      row_y_ = y;
      row_h_ = hs;
      glyph_ = ui::label(c.parent, Font::ICON_SMALL, theme::INK);
      state_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    }
    // The tile's name in the gap at the foot of the ring, when it fits there.
    const int name_y = layout_.y + layout_.d - ui::line_height(Font::BODY) - ring / 4;
    if (y - gap < name_y) {
      lv_obj_set_width(name_, layout_.d * 55 / 100);
      lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(name_, LV_LABEL_LONG_DOT);
      lv_obj_set_pos(name_, layout_.x + (layout_.d - layout_.d * 55 / 100) / 2, name_y);
    } else {
      lv_obj_add_flag(name_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Whether a line of `width` pixels, from y `top` and `h` high, stays inside the ring's inner circle: its widest chord
  // is at the line's edge farthest from the centre.
  bool inside(int top, int h, int width) const {
    const int cy = layout_.y + layout_.d / 2;
    const int radius = layout_.d / 2 - ring_ - ui::px(3);
    const int far = std::max(std::abs(top - cy), std::abs(top + h - cy));
    if (far >= radius) return false;
    const int half = (int) std::sqrt((double) radius * radius - (double) far * far);
    return width <= 2 * half;
  }

  // A label across the ring's width, its text centred.
  void centre(lv_obj_t *label, int y) {
    lv_obj_set_width(label, layout_.d);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, layout_.x, y);
  }

  void make_keys(const tessera::TileContext &c) {
    for (int i = 0; i < 3; ++i) {
      keys_[i] = ui::block(c.parent, theme::TRACK);
      key_labels_[i] = ui::label(keys_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_center(key_labels_[i]);
    }
    ui::set_text(key_labels_[0], "-");
    ui::set_text(key_labels_[2], "+");
  }

  // The row of keys: three with a mode key, two without, equal widths.
  void place_keys() {
    const int count = modes_n_ >= 2 ? 3 : 2;
    const int gap = ui::px(4);
    const int kw = (w_ - gap * (count - 1)) / count;
    int slot = 0;
    for (int i = 0; i < 3; ++i) {
      if (i == 1 && count == 2) {
        lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(keys_[i], kw, layout_.key_h);
      lv_obj_set_pos(keys_[i], slot++ * (kw + gap), layout_.keys_y);
    }
  }

  // Everything that shows, from the state as it is now: the value shown, colours, words, what can be touched.
  void apply() {
    const bool offline = mood_ == Mood::UNAVAILABLE;
    const bool has_value = shown_ != NO_VALUE && !offline;
    ui::set_text(center_, has_value ? format(shown_, decimal_) + "\xC2\xB0" : std::string("\xE2\x80\x94"));
    ui::set_color(center_, offline ? theme::MUTED : theme::INK);
    ui::set_color(name_, theme::MUTED);
    if (now_) {
      ui::set_text(now_, current_ != NO_VALUE && !offline
                             ? tessera::fill(plugin_->text("now"), "n", format(current_, decimal_) + "\xC2\xB0") : "");
      ui::set_color(now_, theme::MUTED);
    }
    if (state_) place_state();
    if (arc_) {
      lv_arc_set_range(arc_, 0, steps(range_));
      lv_arc_set_value(arc_, has_value ? to_step(shown_, range_) : 0);
      const bool quiet = offline || mood_ == Mood::OFF;
      lv_obj_set_style_arc_color(arc_, ui::color(theme::TRACK), LV_PART_MAIN);
      lv_obj_set_style_arc_color(arc_, quiet ? ui::color(theme::TRACK) : ui::tone_color(mood_tone(), theme::MUTED),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(arc_, quiet ? ui::color(theme::MUTED) : ui::color(theme::INK), LV_PART_KNOB);
      lv_obj_set_style_bg_opa(arc_, offline ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_KNOB);
      if (offline) {
        lv_obj_add_state(arc_, LV_STATE_DISABLED);
        lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
      } else {
        lv_obj_remove_state(arc_, LV_STATE_DISABLED);
        lv_obj_add_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
      }
    }
    if (layout_.keys) {
      place_keys();
      for (int i = 0; i < 3; ++i) ui::set_color(key_labels_[i], offline ? theme::MUTED : theme::INK);
      ui::set_text(key_labels_[1], mode_text(state_word_));
    }
  }

  // The state's icon and word as one row, centred in the ring.
  void place_state() {
    const std::string word = mood_word();
    const Glyph glyph = glyph_of(mood_);
    const uint32_t code = glyph == Glyph::FIRE ? ICON_FIRE : glyph == Glyph::SNOWFLAKE ? ICON_SNOWFLAKE : ICON_POWER;
    const std::string icon = glyph == Glyph::NONE ? "" : ui::icon(code);
    ui::set_text(glyph_, icon);
    ui::set_text(state_, word);
    ui::set_color(glyph_, theme::INK);
    ui::set_color(state_, theme::MUTED);
    const int gap = icon.empty() ? 0 : ui::px(6);
    const int iw = icon.empty() ? 0 : ui::text_width(icon, Font::ICON_SMALL);
    const int tw = ui::text_width(word, Font::BODY);
    const int x = layout_.x + std::max(0, (layout_.d - (iw + gap + tw)) / 2);
    lv_obj_set_width(glyph_, std::max(1, iw));
    lv_obj_set_pos(glyph_, x, row_y_ + (row_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
    lv_obj_set_width(state_, tw + ui::px(2));
    lv_obj_set_pos(state_, x + iw + gap, row_y_ + (row_h_ - ui::line_height(Font::BODY)) / 2);
  }

  tessera::Tone mood_tone() const {
    switch (mood_) {
      case Mood::HEATING: return tessera::Tone::BUSY;
      case Mood::COOLING: return tessera::Tone::ACCENT;
      default: return tessera::Tone::NORMAL;
    }
  }

  std::string mood_word() const {
    switch (mood_) {
      case Mood::HEATING: return plugin_->text("heating");
      case Mood::COOLING: return plugin_->text("cooling");
      case Mood::OFF: return plugin_->text("off");
      case Mood::UNAVAILABLE: return plugin_->text("unavailable");
      default: return plugin_->text("idle");
    }
  }

  std::string mode_text(const std::string &mode) const {
    const std::string key = "m_" + mode;
    const char *word = plugin_->text(key.c_str());
    return *word ? word : mode;
  }

  void change_mode() {
    ESP_LOGI(TAG, "mode from=%s of %d", state_word_, modes_n_);
    std::vector<std::string> modes(modes_, modes_ + modes_n_);
    const std::string next = next_mode(modes, state_word_);
    if (next.empty()) return;
    tessera::action("climate.set_hvac_mode", entity_, "hvac_mode", next);
  }

  // - or +: the shown value moves one step; one command follows DEBOUNCE_MS after the last tap (at once when this tile
  // has no tick to wait on).
  void bump(int direction) {
    const int base = shown_ != NO_VALUE ? shown_ : reported_;
    ESP_LOGI(TAG, "bump dir=%d base=%d attached=%d", direction, base, (int) attached_);
    if (base == NO_VALUE) return;                     // a thermostat without a target: nothing to move
    shown_ = from_step(to_step(base, range_) + direction, range_);
    pending_ = shown_;
    pending_active_ = true;
    apply();
    if (attached_) {
      debounce_on_ = true;
      debounce_since_ = millis();
    } else {
      send(shown_);
    }
  }

  void send(int value) {
    ESP_LOGI(TAG, "send value=%d reported=%d", value, reported_);
    if (value == NO_VALUE || value == reported_) {   // already there: nothing to tell Home Assistant
      settle();
      apply();
      return;
    }
    pending_ = value;
    pending_active_ = true;
    tessera::action("climate.set_temperature", entity_, "temperature", format(value, '.'));
    confirm_on_ = attached_;
    confirm_since_ = millis();
  }

  void settle() {
    pending_active_ = false;
    confirm_on_ = false;
  }

  static void debug_event(lv_event_t *e) {
    const int code = (int) lv_event_get_code(e);
    if (code >= 1 && code <= 16 && code != 2)
      ESP_LOGI(TAG, "ev %d on the %s", code, lv_event_get_target(e) == lv_event_get_current_target(e) ? "tile" : "child");
  }

  static void arc_event(lv_event_t *e) {
    auto *self = static_cast<DialTile *>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
      self->dragging_ = true;
      self->debounce_on_ = false;
      self->shown_ = from_step(lv_arc_get_value(self->arc_), self->range_);
      ui::set_text(self->center_, format(self->shown_, self->decimal_) + "\xC2\xB0");
    } else if (self->dragging_) {             // RELEASED or PRESS_LOST: the finger left the ring
      self->dragging_ = false;
      self->send(self->shown_);
      self->apply();
    }
  }

  ThermoDial *plugin_;
  char entity_[96]{};
  char state_word_[MODE_LEN]{};
  char modes_[MAX_MODES][MODE_LEN]{};
  int modes_n_ = 0;
  int w_ = 0, h_ = 0, margin_ = 0, ring_ = 0, row_y_ = 0, row_h_ = 0;
  Layout layout_;
  Range range_;
  Mood mood_ = Mood::UNAVAILABLE;
  Font face_ = Font::VALUE, now_face_ = Font::BODY_LARGE;
  bool show_now_ = false, show_state_ = false;
  char decimal_ = '.';
  int reported_ = NO_VALUE, current_ = NO_VALUE, shown_ = NO_VALUE, pending_ = NO_VALUE;
  bool pending_active_ = false, dragging_ = false, attached_ = false;
  bool debounce_on_ = false, confirm_on_ = false;
  uint32_t debounce_since_ = 0, confirm_since_ = 0;
  lv_obj_t *arc_{}, *name_{}, *center_{}, *now_{}, *state_{}, *glyph_{};
  lv_obj_t *keys_[3]{}, *key_labels_[3]{};
};

void ThermoDial::setup() { add_tile("dial", [this]() { return new DialTile(this); }); }

void ThermoDial::on_press() { ESP_LOGI(TAG, "on_press"); }
void ThermoDial::on_touch() { ESP_LOGI(TAG, "on_touch"); }

void ThermoDial::on_interval(uint32_t now_ms) {
  for (DialTile *tile : tiles_)
    if (tile) tile->tick(now_ms);
}

bool ThermoDial::attach(DialTile *tile) {
  for (DialTile *&slot : tiles_) {
    if (!slot) {
      slot = tile;
      return true;
    }
  }
  ESP_LOGW(TAG, "more than %d dials on this screen: the others send at once", MAX_TILES);
  return false;
}

void ThermoDial::detach(DialTile *tile) {
  for (DialTile *&slot : tiles_)
    if (slot == tile) slot = nullptr;
}

}  // namespace esphome::thermo_dial
