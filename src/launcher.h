#ifndef PHONE_WM_LAUNCHER_H
#define PHONE_WM_LAUNCHER_H

#include <wlr/types/wlr_scene.h>

#define MAX_APPS 16
#define LAUNCHER_COLS 4
#define LAUNCHER_ICON_WIDTH 100
#define LAUNCHER_ICON_HEIGHT 100
#define LAUNCHER_ICON_GAP 16
#define LAUNCHER_PADDING 16
#define LAUNCHER_TOP_OFFSET 40

#define HOME_COLS 4
#define HOME_ICON_SIZE 72
#define HOME_ICON_GAP 12
#define HOME_BOTTOM_OFFSET 50

struct phone_server;

struct phone_app {
  char *name;
  char *command;
  char *icon_path;
  bool home;
  float color[4];
};

struct phone_app_icon {
  struct phone_app *app;
  struct wlr_scene_rect *bg_rect;
  struct wlr_scene_buffer *icon_buf;
  struct wlr_buffer *icon_wlr_buf;
  struct wlr_scene_buffer *label_buf;
  struct wlr_buffer *label_wlr_buf;
  int x, y, w, h;
  int base_x, base_y; // Originalposition (für das Wiederherstellen nach Suche)
  int label_x, label_y, label_w, label_h;
};

struct phone_launcher {
  struct phone_server *server;
  struct wlr_scene_rect *bg_rect;
  struct wlr_scene_tree *tree;
  bool visible;

  // App-Suche: Textfeld oben in der Schublade
  char filter[64];                    // Aktueller Suchtext
  int search_x, search_y, search_w, search_h; // Geometrie des Textfelds
  struct wlr_scene_rect *search_bg;   // Textfeld-Hintergrund
  struct wlr_scene_buffer *search_buf;// Gerenderter Suchtext

  struct phone_app apps[MAX_APPS];
  int num_apps;
  struct phone_app_icon icons[MAX_APPS];
};

struct phone_homescreen {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct phone_app_icon icons[MAX_APPS];
  int num_icons;
};

// Launcher initialisieren: Apps laden, Icons erzeugen, Scene-Tree anlegen
void launcher_init(struct phone_server *server);
// Launcher anzeigen (andere Overlays werden geschlossen)
void launcher_show(struct phone_launcher *launcher);
// Launcher ausblenden
void launcher_hide(struct phone_launcher *launcher);
// Launcher ein-/ausblenden (Umschalter)
void launcher_toggle(struct phone_launcher *launcher);
// Tap im Launcher: App-Icon finden und starten
void launcher_handle_tap(struct phone_launcher *launcher, double x, double y);
// Tastendruck für die App-Suche verarbeiten (Buchstaben, Backspace, Enter, Esc)
void launcher_search_key(struct phone_launcher *launcher, uint32_t keycode);
// Suchtext anwenden: passende Apps anzeigen, Rest ausblenden
void launcher_search_apply(struct phone_launcher *launcher);
// launcher.conf aus den aktuellen Apps schreiben (homescreen-Markierungen speichern)
void launcher_write_config(struct phone_launcher *launcher);

// Home-Screen initialisieren (Icons im Bottom-Layer anlegen)
void homescreen_init(struct phone_server *server);
// Home-Screen-Icons neu aufbauen (Apps mit home=true anzeigen)
void homescreen_update(struct phone_homescreen *hs);
// Tap auf dem Home-Screen: App-Icon finden und starten
void homescreen_handle_tap(struct phone_homescreen *hs, double x, double y);

// Icon-Hilfsfunktionen (auch von recents.c benutzt)
char *find_icon(const char *name);  // Icon-Pfad zur App-ID suchen
struct wlr_buffer *load_icon_png(const char *path, int size);  // PNG-Icon laden und als Buffer erzeugen
struct wlr_buffer *create_color_icon(float r, float g, float b, int size);  // Icon-Buffer aus einer Farbe erzeugen

#endif
