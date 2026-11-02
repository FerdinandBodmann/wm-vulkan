#ifndef PHONE_WM_THEME_H
#define PHONE_WM_THEME_H

#include <stdbool.h>

struct phone_server;

// Zentrales Theme des Compositors: alle UI-Farben an einem Ort.
// Wird aus ~/.config/phone-wm/settings.conf (bzw. /etc/phone-wm/settings.conf)
// geladen, mit sinnvollen Defaults als Fallback.
struct phone_theme {
  bool dark;                 // Dunkles Theme (true) oder helles Theme (false)
  float accent[4];           // Akzentfarbe (aktive Kacheln, Slider, Hervorhebungen)
  float fg[4];               // Vordergrund-/Textfarbe
  float bar_bg[4];           // Hintergrund der Navigationsleiste & Statusleiste
  float panel_bg[4];         // Hintergrund von Panels (Control Center, Launcher)
  float tile_inactive[4];    // Farbe inaktiver Quick-Settings-Kacheln
  float wallpaper_start[4];  // Startfarbe des Wallpaper-Verlaufs
  float wallpaper_end[4];    // Endfarbe des Wallpaper-Verlaufs
};

// Theme laden: Defaults setzen, dann settings.conf einlesen.
void theme_init(struct phone_server *server);
// Theme-Modus wechseln (Dunkel <-> Hell), setzt jeweilige Defaults.
void theme_set_dark(struct phone_server *server, bool dark);
// Neue Hex/Float-Farben aus der Config in einen 4er-Float-Array schreiben.
// Akzeptiert "r,g,b" (0..1) oder "#rrggbb".
bool theme_parse_color(const char *str, float out[4]);

#endif