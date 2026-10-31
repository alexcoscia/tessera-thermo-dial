#include "../components/thermo_dial/thermo_logic.h"
#include <cassert>
#include <cstdio>

using namespace esphome::thermo_dial;

int main() {
  // Intervallo: valori di HA, passo 0,5.
  Range r = make_range(7, 35, 0.5);
  assert(r.lo == 70 && r.hi == 350 && r.step == 5);
  assert(steps(r) == 56);
  assert(to_step(215, r) == 29);         // 21,5 è il passo 29
  assert(from_step(29, r) == 215);
  assert(to_step(0, r) == 0);            // sotto il minimo: bloccato
  assert(to_step(999, r) == 56);         // sopra il massimo: bloccato
  assert(to_step(212, r) == 28);         // 21,2 -> 21,0 (arrotonda al passo vicino)
  assert(from_step(-4, r) == 70 && from_step(900, r) == 350);

  // Passo 1 grado.
  Range whole = make_range(10, 30, 1);
  assert(steps(whole) == 20 && from_step(5, whole) == 150);

  // Dati sbagliati: passo assente, zero, negativo; massimo sotto il minimo.
  assert(make_range(7, 35, 0).step == 5);
  assert(make_range(7, 35, -1).step == 5);
  Range bad = make_range(30, 10, 0.5);
  assert(bad.lo == 70 && bad.hi == 350);
  assert(steps(bad) > 0);

  // Formato.
  assert(format(215, '.') == "21.5");
  assert(format(215, ',') == "21,5");
  assert(format(-30, '.') == "-3.0");
  assert(format(70, '.') == "7.0");

  // Umore.
  assert(mood_of("heat", "heating") == Mood::HEATING);
  assert(mood_of("cool", "cooling") == Mood::COOLING);
  assert(mood_of("heat", "idle") == Mood::IDLE);
  assert(mood_of("heat", "") == Mood::HEATING);      // senza hvac_action ci si fida della modalità
  assert(mood_of("cool", "") == Mood::COOLING);
  assert(mood_of("auto", "") == Mood::IDLE);
  assert(mood_of("off", "off") == Mood::OFF);
  assert(mood_of("unavailable", "") == Mood::UNAVAILABLE);
  assert(mood_of("unknown", "") == Mood::UNAVAILABLE);
  assert(mood_of("", "") == Mood::UNAVAILABLE);

  // L'icona dello stato al centro: fiammella, fiocco, interruttore, niente.
  assert(glyph_of(Mood::HEATING) == Glyph::FIRE);
  assert(glyph_of(Mood::COOLING) == Glyph::SNOWFLAKE);
  assert(glyph_of(Mood::OFF) == Glyph::POWER);
  assert(glyph_of(Mood::IDLE) == Glyph::SLEEP);
  assert(glyph_of(Mood::UNAVAILABLE) == Glyph::NONE);

  // Modalità successiva.
  std::vector<std::string> modes = {"off", "heat", "cool"};
  assert(next_mode(modes, "off") == "heat");
  assert(next_mode(modes, "cool") == "off");
  assert(next_mode(modes, "dry") == "off");           // sconosciuta: riparte dalla prima
  assert(next_mode({"heat"}, "heat").empty());        // una sola: nessun cambio
  assert(next_mode({}, "heat").empty());

  // Geometria con margine 10: 2x2 di 300x300 con tasti alti 60: margine, arco, margine, tasti.
  Layout l = layout(300, 300, 2, 2, 60, 10);
  assert(l.dial && l.keys);
  assert(l.d == 220 && l.x == 40 && l.y == 10);
  assert(l.keys_y == 240 && l.key_h == 60);
  assert(l.y >= 10 && l.x >= 10 && l.x + l.d <= 300 - 10);   // l'arco non tocca i bordi
  // Senza tasti (allow_keys = false) l'arco prende tutta l'altezza, sempre col margine.
  Layout bare = layout(300, 300, 2, 2, 60, 10, false);
  assert(bare.dial && !bare.keys && bare.key_h == 0 && bare.keys_y == 0);
  assert(bare.d == 280 && bare.x == 10 && bare.y == 10);
  // 1x1: niente arco, niente tasti.
  Layout small = layout(120, 120, 1, 1, 40, 10);
  assert(!small.dial && !small.keys);
  // 2x1: niente arco, tasti sì, in fondo.
  Layout wide = layout(240, 110, 2, 1, 40, 10);
  assert(!wide.dial && wide.keys && wide.keys_y == 70);
  // Il tile (senza arco): 1x2 e 1x3 hanno i tasti in fondo, 1x1 no.
  Layout tall = layout(240, 300, 1, 2, 40, 10, true, false);
  assert(!tall.dial && tall.keys && tall.keys_y == 260);
  assert(!layout(240, 150, 1, 1, 40, 10, true, false).keys);
  assert(!layout(240, 300, 2, 2, 40, 10, true, false).dial);
  // 2x2 troppo basso per i tasti: solo l'arco, sempre col margine.
  Layout flat = layout(300, 100, 2, 2, 60, 10);
  assert(flat.dial && !flat.keys && flat.d == 80 && flat.y == 10);

  // Il tile 2x1: tasti ai lati, centrati in altezza, il numero in mezzo.
  SideLayout side = side_layout(240, 100, 2, 1, 50, 60, 6);
  assert(side.on && side.key_w == 50 && side.key_h == 60 && side.key_y == 20);
  assert(side.left_x == 0 && side.right_x == 190 && side.mid_x == 56 && side.mid_w == 128);
  assert(side_layout(240, 40, 2, 1, 50, 60, 6).key_h == 40);       // più basso del tasto: il tasto si abbassa
  assert(!side_layout(240, 100, 1, 1, 50, 60, 6).on);             // 1x1: no
  assert(!side_layout(240, 200, 2, 2, 50, 60, 6).on);             // 2x2: c'è l'arco
  assert(!side_layout(140, 100, 2, 1, 50, 60, 6).on);             // troppo stretto: in mezzo non resta posto

  // Scadenza della conferma, anche a cavallo del giro del contatore.
  assert(!expired(1000, 500, 5000));
  assert(expired(5500, 500, 5000));
  assert(expired(10, 0xFFFFFFF0u, 5000) == false);    // 26 ms dopo il giro
  std::puts("test_logic: ok");
  return 0;
}
