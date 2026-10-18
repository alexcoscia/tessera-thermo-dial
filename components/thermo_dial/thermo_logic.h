#pragma once
// Logica pura del quadrante: nessun ESPHome, nessun LVGL, così gira anche su questo computer (tests/test_logic.cpp).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace esphome::thermo_dial {

// Decimi di grado: l'arco lavora su interi e HA parla in gradi con un decimale.
struct Range {
  int lo = 70, hi = 350, step = 5;
};

inline int tenths(double degrees) { return (int) std::lround(degrees * 10.0); }

// Dall'entità (min_temp, max_temp, target_temp_step). Un passo che manca o non è positivo vale 0,5; un massimo che non
// supera il minimo rimette 7 e 35.
inline Range make_range(double lo, double hi, double step) {
  Range r;
  if (hi > lo) {
    r.lo = tenths(lo);
    r.hi = tenths(hi);
  }
  const int s = tenths(step);
  r.step = s > 0 ? s : 5;
  if (r.hi - r.lo < r.step) r.hi = r.lo + r.step;
  return r;
}

inline int steps(const Range &r) { return (r.hi - r.lo) / r.step; }

// Dal valore in decimi al passo più vicino, dentro l'intervallo.
inline int to_step(int t, const Range &r) {
  const int clamped = std::min(std::max(t, r.lo), r.hi);
  return std::min((clamped - r.lo + r.step / 2) / r.step, steps(r));
}

inline int from_step(int n, const Range &r) { return r.lo + std::min(std::max(n, 0), steps(r)) * r.step; }

// "21.5": il separatore è quello della lingua dello schermo (solo per il testo); HA riceve sempre il punto.
inline std::string format(int t, char separator) {
  char buf[24];
  const int a = t < 0 ? -t : t;
  std::snprintf(buf, sizeof buf, "%s%d%c%d", t < 0 ? "-" : "", a / 10, separator, a % 10);
  return buf;
}

enum class Mood { HEATING, COOLING, IDLE, OFF, UNAVAILABLE };

// Da `state` (la modalità di HA: off, heat, cool, auto…) e dall'attributo hvac_action. Senza hvac_action ci si fida
// della modalità, perché non tutti i termostati lo danno.
inline Mood mood_of(const std::string &state, const std::string &action) {
  if (state.empty() || state == "unavailable" || state == "unknown") return Mood::UNAVAILABLE;
  if (state == "off") return Mood::OFF;
  if (action == "heating") return Mood::HEATING;
  if (action == "cooling") return Mood::COOLING;
  if (action.empty()) {
    if (state == "heat") return Mood::HEATING;
    if (state == "cool") return Mood::COOLING;
  }
  return Mood::IDLE;
}

// L'icona accanto alla parola dello stato, al centro dell'arco: fiammella, fiocco, interruttore o niente.
enum class Glyph { FIRE, SNOWFLAKE, POWER, NONE };

inline Glyph glyph_of(Mood mood) {
  switch (mood) {
    case Mood::HEATING: return Glyph::FIRE;
    case Mood::COOLING: return Glyph::SNOWFLAKE;
    case Mood::OFF: return Glyph::POWER;
    default: return Glyph::NONE;
  }
}

// La modalità dopo `current`; "" quando ce n'è una sola o nessuna. Una modalità che la lista non conosce riparte dalla prima.
inline std::string next_mode(const std::vector<std::string> &modes, const std::string &current) {
  if (modes.size() < 2) return "";
  const auto it = std::find(modes.begin(), modes.end(), current);
  const size_t i = it == modes.end() ? 0 : ((size_t)(it - modes.begin()) + 1) % modes.size();
  return modes[i];
}

// Dove stanno l'arco e i tasti in un tile di w×h pixel. L'arco da 2×2 in su, sempre a `margin` pixel dai bordi; i tasti
// dove c'è posto e `allow_keys`: sotto l'arco, che resta più alto dei tasti.
struct Layout {
  bool dial = false, keys = false;
  int d = 0, x = 0, y = 0;     // il quadrato dell'arco
  int keys_y = 0, key_h = 0;   // la riga dei tasti
};

inline Layout layout(int w, int h, int columns, int rows, int key_h, int margin, bool allow_keys = true) {
  Layout l;
  l.dial = columns >= 2 && rows >= 2;
  if (l.dial) {
    l.keys = allow_keys && h >= 3 * key_h + 2 * margin;
    l.key_h = l.keys ? key_h : 0;
    l.d = std::max(1, std::min(w - 2 * margin, h - l.key_h - 2 * margin));
    l.x = (w - l.d) / 2;
    l.y = margin;
    l.keys_y = l.keys ? l.y + l.d + margin : 0;
  } else {
    l.keys = columns >= 2 && h >= 2 * key_h;
    l.key_h = l.keys ? key_h : 0;
    l.keys_y = h - l.key_h;
  }
  return l;
}

// Il tempo passato dal momento `since`, anche a cavallo del giro dei millisecondi.
inline bool expired(uint32_t now, uint32_t since, uint32_t limit) { return (uint32_t)(now - since) >= limit; }

}  // namespace esphome::thermo_dial
