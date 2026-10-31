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
// Tessera's icon set, by codepoint (components/smart_display/tile_icon_names.h): fire, snowflake, power, sleep (zzz).
constexpr uint32_t ICON_FIRE = 0xF0238, ICON_SNOWFLAKE = 0xF0717, ICON_POWER = 0xF0425, ICON_SLEEP = 0xF04B2;

// The icon of a glyph; 0 for none.
static uint32_t glyph_code(Glyph glyph) {
  switch (glyph) {
    case Glyph::FIRE: return ICON_FIRE;
    case Glyph::SNOWFLAKE: return ICON_SNOWFLAKE;
    case Glyph::POWER: return ICON_POWER;
    case Glyph::SLEEP: return ICON_SLEEP;
    default: return 0;
  }
}

// What the climate entity says, as a tile and its card both read it.
struct Reading {
  Range range;
  Mood mood = Mood::UNAVAILABLE;
  int reported = NO_VALUE, current = NO_VALUE;
  char state_word[MODE_LEN]{};
  char modes[MAX_MODES][MODE_LEN]{};
  int modes_n = 0;

  static int number(JsonVariantConst v) { return v.is<float>() || v.is<int>() ? tenths(v.as<double>()) : NO_VALUE; }

  void read(JsonObjectConst data) {
    const std::string state = data["state"] | "";
    JsonObjectConst a = data["attributes"];
    range = make_range(a["min_temp"] | 7.0, a["max_temp"] | 35.0, a["target_temp_step"] | 0.5);
    reported = number(a["temperature"]);
    current = number(a["current_temperature"]);
    mood = mood_of(state, a["hvac_action"] | "");
    std::snprintf(state_word, sizeof state_word, "%s", state.c_str());
    modes_n = 0;
    for (JsonVariantConst m : a["hvac_modes"].as<JsonArrayConst>()) {
      const char *mode = m.as<const char *>();
      if (modes_n >= MAX_MODES || !mode) continue;
      std::snprintf(modes[modes_n++], MODE_LEN, "%s", mode);
    }
  }
};

static std::string mood_word(const tessera::Plugin *plugin, Mood mood) {
  switch (mood) {
    case Mood::HEATING: return plugin->text("heating");
    case Mood::COOLING: return plugin->text("cooling");
    case Mood::OFF: return plugin->text("off");
    case Mood::UNAVAILABLE: return plugin->text("unavailable");
    default: return plugin->text("idle");
  }
}

static std::string mode_text(const tessera::Plugin *plugin, const std::string &mode) {
  const std::string key = "m_" + mode;
  const char *word = plugin->text(key.c_str());
  return *word ? word : mode;
}

static tessera::Tone mood_tone(Mood mood) {
  switch (mood) {
    case Mood::HEATING: return tessera::Tone::BUSY;
    case Mood::COOLING: return tessera::Tone::ACCENT;
    default: return tessera::Tone::NORMAL;
  }
}

static char decimal_of(const tessera::Plugin *plugin) {
  const char *separator = plugin->text("decimal");
  return *separator ? *separator : '.';
}

// ---- The tile: the target, the temperature now and the keys; a tap anywhere else opens the card with the dial. Two cells
// wide and one high, - and + stand at the sides and the number between them ----
class DialTile : public tessera::Tile {
 public:
  explicit DialTile(ThermoDial *plugin) : plugin_(plugin) {}
  ~DialTile() override { plugin_->detach(this); }   // LVGL's objects go with the card; nothing else is ours

  void create(const tessera::TileContext &c) override {
    std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    w_ = c.width;
    h_ = c.height;
    tile_ = c.tile;
    decimal_ = decimal_of(plugin_);
    const int key_h = ui::mm(9), gap = ui::px(4);
    layout_ = layout(w_, h_, c.columns, c.rows, key_h, ui::mm(3) + ui::px(6), true, false);
    columns_ = c.columns;
    const int body_h = ui::line_height(Font::BODY), value_h = ui::line_height(Font::VALUE);
    const int info_h = std::max(body_h, ui::line_height(Font::ICON_SMALL));
    // Wide and one cell high: - and + at the sides, the number and what is under it between them.
    side_ = side_layout(w_, h_, c.columns, c.rows, ui::mm(12), ui::mm(14), gap);
    if (side_.on) {
      layout_.keys = false;
      layout_.key_h = 0;
    }
    // The name, the number and the keys must fit one over the other, or the keys stay out.
    if (layout_.keys && body_h + value_h + 3 * gap + key_h > h_) {
      layout_.keys = false;
      layout_.key_h = 0;
    }
    keys_on_ = layout_.keys || side_.on;
    // The number (and under it the temperature now and the state) stands in the middle of what the name and the keys
    // leave free; the keys are at the bottom, under the words (at the sides when the tile is one cell high).
    // At the sides the name stays only if it fits above the number and what is under it.
    const bool name_fits = !side_.on || h_ >= body_h + value_h + 2 * gap + info_h;
    const int top = name_fits ? body_h : 0, bottom = layout_.keys ? layout_.keys_y : h_;
    region_x_ = side_.on ? side_.mid_x : 0;
    region_w_ = side_.on ? side_.mid_w : w_;
    show_info_ = bottom - top >= value_h + gap + info_h;
    const int block = value_h + (show_info_ ? gap + info_h : 0);
    const int y0 = top + std::max(0, (bottom - top - block) / 2);
    info_y_ = y0 + value_h + gap;
    info_h_ = info_h;

    name_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    ui::set_text(name_, c.name);
    lv_obj_set_width(name_, region_w_);
    lv_obj_set_pos(name_, region_x_, 0);
    if (side_.on) lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    if (!name_fits) lv_obj_add_flag(name_, LV_OBJ_FLAG_HIDDEN);
    center_ = ui::label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(center_, region_w_);
    lv_obj_set_style_text_align(center_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(center_, region_x_, y0);
    if (show_info_) {
      now_ = ui::label(c.parent, Font::BODY, theme::MUTED);
      glyph_ = ui::label(c.parent, Font::ICON_SMALL, theme::INK);
    }
    if (keys_on_) make_keys(c);
    attached_ = plugin_->attach(this);
    apply();
  }

  void on_state(JsonObjectConst data) override {
    reading_.read(data);
    if (pending_active_ && reading_.reported == pending_) settle();   // Home Assistant brought the target we chose
    if (!pending_active_) shown_ = reading_.reported;
    apply();
  }

  void on_theme() override { apply(); }

  // A tap on a key does what the key says; a tap anywhere else opens the dial.
  void on_tap() override {
    if (reading_.mood == Mood::UNAVAILABLE) return;
    const tessera::Point at = tessera::tap_point();
    if (keys_on_)
      for (int i = 0; i < 3; ++i)
        if (key_shown(i) && at.x >= key_x_[i] && at.x < key_x_[i] + key_w_ && at.y >= key_y_ && at.y < key_y_ + key_h_)
          return press_key(i);
    tessera::open_card(plugin_->plugin_id(), "dial", entity_, tile_);
  }

  // Called by the plugin every 250 ms: the waits of this tile.
  void tick(uint32_t now) {
    if (debounce_on_ && expired(now, debounce_since_, DEBOUNCE_MS)) {
      debounce_on_ = false;
      send(shown_);
    }
    if (confirm_on_ && expired(now, confirm_since_, CONFIRM_MS)) {
      settle();                          // Home Assistant did not follow: back to what it says
      shown_ = reading_.reported;
      apply();
    }
  }

 private:
  void make_keys(const tessera::TileContext &c) {
    for (int i = 0; i < 3; ++i) {
      keys_[i] = ui::block(c.parent, theme::TRACK);
      key_labels_[i] = ui::label(keys_[i], Font::BODY_LARGE, theme::INK);
      lv_obj_center(key_labels_[i]);
    }
    ui::set_text(key_labels_[0], "-");
    ui::set_text(key_labels_[2], "+");
  }

  bool key_shown(int i) const { return !lv_obj_has_flag(keys_[i], LV_OBJ_FLAG_HIDDEN); }

  // The row of keys: three with a mode key, two without, equal widths.
  // The mode key needs a tile two cells wide; one cell wide has - and + only.
  void place_keys() {
    if (side_.on) {                                   // - at the left, + at the right, no mode key
      key_w_ = side_.key_w;
      key_h_ = side_.key_h;
      key_y_ = side_.key_y;
      key_x_[0] = side_.left_x;
      key_x_[2] = side_.right_x;
      lv_obj_add_flag(keys_[1], LV_OBJ_FLAG_HIDDEN);
      for (int i = 0; i < 3; i += 2) {
        lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(keys_[i], key_w_, key_h_);
        lv_obj_set_pos(keys_[i], key_x_[i], key_y_);
      }
      return;
    }
    key_h_ = layout_.key_h;
    key_y_ = layout_.keys_y;
    const int count = reading_.modes_n >= 2 && columns_ >= 2 ? 3 : 2;
    const int gap = ui::px(4);
    key_w_ = (w_ - gap * (count - 1)) / count;
    int slot = 0;
    for (int i = 0; i < 3; ++i) {
      if (i == 1 && count == 2) {
        lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      key_x_[i] = slot++ * (key_w_ + gap);
      lv_obj_set_size(keys_[i], key_w_, key_h_);
      lv_obj_set_pos(keys_[i], key_x_[i], key_y_);
    }
  }

  // Everything that shows, from the state as it is now: the value shown, colours, words.
  void apply() {
    const bool offline = reading_.mood == Mood::UNAVAILABLE;
    const bool has_value = shown_ != NO_VALUE && !offline;
    ui::set_text(center_, has_value ? format(shown_, decimal_) + "\xC2\xB0" : std::string("-"));
    // Heating: Home Assistant's amber; cooling: the screen's blue; idle, off or offline keep the usual colours.
    ui::set_tone(center_, offline ? tessera::Tone::NORMAL : mood_tone(reading_.mood), offline ? theme::MUTED : theme::INK);
    ui::set_color(name_, theme::MUTED);
    if (show_info_) place_info(offline);
    if (keys_on_) {
      place_keys();
      for (int i = 0; i < 3; ++i) ui::set_color(key_labels_[i], offline ? theme::MUTED : theme::INK);
      ui::set_text(key_labels_[1], mode_text(plugin_, reading_.state_word));
    }
  }

  // Under the number: "Now 19.8°" and the state's icon (zzz when idle) as one row, centred on the number.
  void place_info(bool offline) {
    const std::string now = !offline && reading_.current != NO_VALUE
                                ? tessera::fill(plugin_->text("now"), "n", format(reading_.current, decimal_) + "\xC2\xB0")
                                : std::string();
    const uint32_t code = offline ? 0 : glyph_code(glyph_of(reading_.mood));
    const std::string icon = code ? ui::icon(code) : "";
    ui::set_text(now_, now);
    ui::set_text(glyph_, icon);
    const tessera::Tone tone = offline ? tessera::Tone::NORMAL : mood_tone(reading_.mood);
    ui::set_tone(now_, tone, theme::MUTED);
    ui::set_tone(glyph_, tone, theme::INK);
    const int nw = now.empty() ? 0 : ui::text_width(now, Font::BODY);
    const int iw = icon.empty() ? 0 : ui::text_width(icon, Font::ICON_SMALL);
    const int gap = nw && iw ? ui::px(8) : 0;
    const int x = region_x_ + std::max(0, (region_w_ - (nw + gap + iw)) / 2);
    lv_obj_set_width(now_, std::max(1, nw + ui::px(2)));
    lv_obj_set_pos(now_, x, info_y_ + (info_h_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_width(glyph_, std::max(1, iw));
    lv_obj_set_pos(glyph_, x + nw + gap, info_y_ + (info_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
  }

  void change_mode() {
    std::vector<std::string> modes(reading_.modes, reading_.modes + reading_.modes_n);
    const std::string next = next_mode(modes, reading_.state_word);
    if (next.empty()) return;
    JsonDocument fields;
    fields["hvac_mode"] = next;
    tessera::action("climate.set_hvac_mode", entity_, fields);
  }

  // - or +: the shown value moves one step; one command follows DEBOUNCE_MS after the last tap (at once when this tile
  // has no tick to wait on).
  void bump(int direction) {
    const int base = shown_ != NO_VALUE ? shown_ : reading_.reported;
    if (base == NO_VALUE) return;                     // a thermostat without a target: nothing to move
    shown_ = from_step(to_step(base, reading_.range) + direction, reading_.range);
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
    if (value == NO_VALUE || value == reading_.reported) {   // already there: nothing to tell Home Assistant
      settle();
      apply();
      return;
    }
    pending_ = value;
    pending_active_ = true;
    JsonDocument fields;
    fields["temperature"] = value / 10.0;   // tenths of a degree, sent as a number
    tessera::action("climate.set_temperature", entity_, fields);
    confirm_on_ = attached_;
    confirm_since_ = millis();
  }

  void settle() {
    pending_active_ = false;
    confirm_on_ = false;
  }

  void press_key(int index) {
    if (index == 1) return change_mode();
    bump(index == 0 ? -1 : 1);
  }

  ThermoDial *plugin_;
  char entity_[96]{};
  Reading reading_;
  int w_ = 0, h_ = 0, tile_ = -1, key_w_ = 0, key_h_ = 0, key_y_ = 0;
  int region_x_ = 0, region_w_ = 0, info_y_ = 0, info_h_ = 0;
  int columns_ = 1;
  bool show_info_ = false, keys_on_ = false;
  int key_x_[3]{};
  Layout layout_;
  SideLayout side_;
  char decimal_ = '.';
  int shown_ = NO_VALUE, pending_ = NO_VALUE;
  bool pending_active_ = false, attached_ = false;
  bool debounce_on_ = false, confirm_on_ = false;
  uint32_t debounce_since_ = 0, confirm_since_ = 0;
  lv_obj_t *name_{}, *center_{}, *now_{}, *glyph_{};
  lv_obj_t *keys_[3]{}, *key_labels_[3]{};
};

// ---- The card: the dial, like Home Assistant's thermostat card. A finger drags the ring; the card keeps its drags ----
class DialCard : public tessera::Card {
 public:
  explicit DialCard(ThermoDial *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    std::snprintf(entity_, sizeof entity_, "%s", c.entity);
    decimal_ = decimal_of(plugin_);
    // The ring stays this far from the card's edge; the knob reaches a little beyond the ring.
    const int margin = ui::mm(3) + ui::px(6);
    d_ = std::max(1, std::min(c.width, c.height) - 2 * margin);
    x_ = (c.width - d_) / 2;
    y_ = (c.height - d_) / 2;
    // The ring's width: thick, but never more than a sixth of the dial.
    ring_ = std::min(std::max(ui::mm(5), d_ / 9), d_ / 6);
    arc_ = lv_arc_create(c.parent);
    lv_obj_remove_style_all(arc_);
    lv_obj_set_size(arc_, d_, d_);
    lv_obj_set_pos(arc_, x_, y_);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_arc_set_range(arc_, 0, 56);
    for (lv_part_t part : {LV_PART_MAIN, LV_PART_INDICATOR}) {
      lv_obj_set_style_arc_width(arc_, ring_, part);
      lv_obj_set_style_arc_rounded(arc_, true, part);
    }
    lv_obj_set_style_bg_opa(arc_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(arc_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, ui::px(2), LV_PART_KNOB);
    lv_obj_add_event_cb(arc_, &DialCard::arc_event, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(arc_, &DialCard::arc_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(arc_, &DialCard::arc_event, LV_EVENT_PRESS_LOST, this);
    make_middle(c.parent);
  }

  void on_state(JsonObjectConst data) override {
    reading_.read(data);
    if (pending_active_ && reading_.reported == pending_) settle();   // Home Assistant brought the target we chose
    if (!dragging_ && !pending_active_) shown_ = reading_.reported;
    if (!dragging_) apply();                                            // under a finger the dial does not move
  }

  void on_tick(uint32_t) override {
    if (confirm_on_ && expired(millis(), confirm_since_, CONFIRM_MS)) {
      settle();                          // Home Assistant did not follow: back to what it says
      shown_ = reading_.reported;
      if (!dragging_) apply();
    }
  }

  void on_theme() override { apply(); }

 private:
  // What stands in the middle of the ring, from the top: the target, the temperature now, the state with its icon.
  // The first of these that fits inside the ring (each line within the chord of the inner circle where it stands):
  // everything with the large digits, then smaller faces, then without the state, then without the temperature now.
  void make_middle(lv_obj_t *parent) {
    struct Pass { Font face, now_face; bool now, state; };
    static const Pass passes[] = {
        {Font::DIGITS, Font::BODY_LARGE, true, true}, {Font::VALUE, Font::BODY_LARGE, true, true},
        {Font::VALUE, Font::BODY, true, true},        {Font::VALUE, Font::BODY_LARGE, true, false},
        {Font::VALUE, Font::BODY, true, false},       {Font::VALUE, Font::BODY, false, false}};
    const int gap = ui::px(4);
    const int hs = std::max(ui::line_height(Font::BODY), ui::line_height(Font::ICON_SMALL));
    const int cy = y_ + d_ / 2;
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
    center_ = ui::label(parent, face_, theme::INK);
    centre(center_, y);
    y += hd + gap;
    if (show_now_) {
      now_ = ui::label(parent, now_face_, theme::MUTED);
      centre(now_, y);
      y += hn + gap;
    }
    if (show_state_) {
      row_y_ = y;
      row_h_ = hs;
      glyph_ = ui::label(parent, Font::ICON_SMALL, theme::INK);
      state_ = ui::label(parent, Font::BODY, theme::MUTED);
    }
  }

  // Whether a line of `width` pixels, from y `top` and `h` high, stays inside the ring's inner circle: its widest chord
  // is at the line's edge farthest from the centre.
  bool inside(int top, int h, int width) const {
    const int cy = y_ + d_ / 2;
    const int radius = d_ / 2 - ring_ - ui::px(3);
    const int far = std::max(std::abs(top - cy), std::abs(top + h - cy));
    if (far >= radius) return false;
    const int half = (int) std::sqrt((double) radius * radius - (double) far * far);
    return width <= 2 * half;
  }

  // A label across the ring's width, its text centred.
  void centre(lv_obj_t *label, int y) {
    lv_obj_set_width(label, d_);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x_, y);
  }

  // Everything that shows, from the state as it is now.
  void apply() {
    const bool offline = reading_.mood == Mood::UNAVAILABLE;
    const bool has_value = shown_ != NO_VALUE && !offline;
    ui::set_text(center_, has_value ? format(shown_, decimal_) + "\xC2\xB0" : std::string("-"));
    ui::set_color(center_, offline ? theme::MUTED : theme::INK);
    if (now_) {
      ui::set_text(now_, reading_.current != NO_VALUE && !offline
                             ? tessera::fill(plugin_->text("now"), "n", format(reading_.current, decimal_) + "\xC2\xB0")
                             : "");
      ui::set_color(now_, theme::MUTED);
    }
    if (state_) place_state();
    lv_arc_set_range(arc_, 0, steps(reading_.range));
    lv_arc_set_value(arc_, has_value ? to_step(shown_, reading_.range) : 0);
    const bool quiet = offline || reading_.mood == Mood::OFF;
    lv_obj_set_style_arc_color(arc_, ui::color(theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, quiet ? ui::color(theme::TRACK) : ui::tone_color(mood_tone(reading_.mood), theme::MUTED),
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

  // The state's icon and word as one row, centred in the ring.
  void place_state() {
    const std::string word = mood_word(plugin_, reading_.mood);
    const Glyph glyph = glyph_of(reading_.mood);
    const uint32_t code = glyph_code(glyph);
    const std::string icon = code ? ui::icon(code) : "";
    ui::set_text(glyph_, icon);
    ui::set_text(state_, word);
    ui::set_color(glyph_, theme::INK);
    ui::set_color(state_, theme::MUTED);
    const int gap = icon.empty() ? 0 : ui::px(6);
    const int iw = icon.empty() ? 0 : ui::text_width(icon, Font::ICON_SMALL);
    const int tw = ui::text_width(word, Font::BODY);
    const int x = x_ + std::max(0, (d_ - (iw + gap + tw)) / 2);
    lv_obj_set_width(glyph_, std::max(1, iw));
    lv_obj_set_pos(glyph_, x, row_y_ + (row_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
    lv_obj_set_width(state_, tw + ui::px(2));
    lv_obj_set_pos(state_, x + iw + gap, row_y_ + (row_h_ - ui::line_height(Font::BODY)) / 2);
  }

  void send(int value) {
    if (value == NO_VALUE || value == reading_.reported) {   // already there: nothing to tell Home Assistant
      settle();
      return;
    }
    pending_ = value;
    pending_active_ = true;
    JsonDocument fields;
    fields["temperature"] = value / 10.0;   // tenths of a degree, sent as a number
    tessera::action("climate.set_temperature", entity_, fields);
    confirm_on_ = true;
    confirm_since_ = millis();
  }

  void settle() {
    pending_active_ = false;
    confirm_on_ = false;
  }

  static void arc_event(lv_event_t *e) {
    auto *self = static_cast<DialCard *>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
      self->dragging_ = true;
      self->shown_ = from_step(lv_arc_get_value(self->arc_), self->reading_.range);
      ui::set_text(self->center_, format(self->shown_, self->decimal_) + "\xC2\xB0");
    } else if (self->dragging_) {             // RELEASED or PRESS_LOST: the finger left the ring
      self->dragging_ = false;
      self->send(self->shown_);
      self->apply();
    }
  }

  ThermoDial *plugin_;
  char entity_[96]{};
  Reading reading_;
  int d_ = 0, x_ = 0, y_ = 0, ring_ = 0, row_y_ = 0, row_h_ = 0;
  Font face_ = Font::VALUE, now_face_ = Font::BODY_LARGE;
  bool show_now_ = false, show_state_ = false;
  char decimal_ = '.';
  int shown_ = NO_VALUE, pending_ = NO_VALUE;
  bool pending_active_ = false, dragging_ = false, confirm_on_ = false;
  uint32_t confirm_since_ = 0;
  lv_obj_t *arc_{}, *center_{}, *now_{}, *state_{}, *glyph_{};
};

void ThermoDial::setup() {
  add_tile("dial", [this]() { return new DialTile(this); });
  add_card("dial", [this]() { return new DialCard(this); });
}

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
