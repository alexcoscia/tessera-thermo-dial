#include "thermo_dial.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <string>

namespace esphome::thermo_dial {

static const char *const TAG = "thermo_dial";
using tessera::Font;
namespace ui = tessera::ui;

// Prototipo: un arco e il suo valore. Ogni evento dell'arco è una riga di log, tag "thermo_dial".
class DialTile : public tessera::Tile {
 public:
  void create(const tessera::TileContext &c) override {
    const int d = std::min(c.width, c.height);
    arc_ = lv_arc_create(c.parent);
    lv_obj_remove_style_all(arc_);
    lv_obj_set_size(arc_, d, d);
    lv_obj_set_pos(arc_, (c.width - d) / 2, (c.height - d) / 2);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_arc_set_range(arc_, 0, 56);
    lv_arc_set_value(arc_, 28);
    const int w = ui::mm(3);
    lv_obj_set_style_arc_width(arc_, w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, ui::color(theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, ui::color(theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc_, ui::color(theme::INK), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(arc_, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc_, ui::px(3), LV_PART_KNOB);
    label_ = ui::label(c.parent, Font::DIGITS, theme::INK);
    lv_obj_set_width(label_, c.width);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_, LV_ALIGN_CENTER, 0, 0);
    ui::set_text(label_, "21.0");
    lv_obj_add_event_cb(arc_, &DialTile::arc_event, LV_EVENT_ALL, this);
    ESP_LOGI(TAG, "spike create %dx%d", c.width, c.height);
  }
  void on_tap() override { ESP_LOGI(TAG, "spike on_tap at=%d,%d", tessera::tap_point().x, tessera::tap_point().y); }

 private:
  static void arc_event(lv_event_t *e) {
    auto *self = static_cast<DialTile *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
      const int v = lv_arc_get_value(self->arc_);
      ESP_LOGI(TAG, "spike value=%d", v);
      char buf[16];
      snprintf(buf, sizeof buf, "%d.%d", 7 + v / 2, (v % 2) * 5);
      ui::set_text(self->label_, buf);
    } else if (code == LV_EVENT_RELEASED) {
      ESP_LOGI(TAG, "spike released value=%d", lv_arc_get_value(self->arc_));
    }
  }
  lv_obj_t *arc_{}, *label_{};
};

void ThermoDial::setup() { add_tile("dial", []() { return new DialTile(); }); }

}  // namespace esphome::thermo_dial
