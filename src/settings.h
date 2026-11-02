#ifndef PHONE_WM_SETTINGS_H
#define PHONE_WM_SETTINGS_H

#include <wlr/types/wlr_scene.h>

#define SETTINGS_MAX_ITEMS 48
#define SETTINGS_MAX_NODES 96

struct phone_server;

// Einstellungen-Seite: Theme, Akzent, Wallpaper, Auto-Sperre, Nicht stören, Apps.
// Änderungen werden live angewendet und in settings.conf bzw. launcher.conf
// gespeichert. Die Seite läuft direkt im Compositor (wie das Control Center).

// IDs der antippbaren Elemente für die Tap-Verarbeitung
enum settings_item_id {
  SETTINGS_THEME,       // Theme umschalten (Dunkel <-> Hell)
  SETTINGS_ACCENT0,     // Akzent-Farbfelder (4 Stück)
  SETTINGS_ACCENT1,
  SETTINGS_ACCENT2,
  SETTINGS_ACCENT3,
  SETTINGS_WP0,         // Wallpaper-Voreinstellungen (3 Stück)
  SETTINGS_WP1,
  SETTINGS_WP2,
  SETTINGS_LOCK,        // Auto-Sperre umschalten
  SETTINGS_DND,         // Nicht stören umschalten
  SETTINGS_CLOSE,       // Seite schließen
  SETTINGS_APP_BASE,    // + Index = App-Reihe (homescreen-Anzeige umschalten)
};

struct settings_item {
  int id;
  int x, y, w, h;
};

struct phone_settings {
  struct phone_server *server;
  struct wlr_scene_tree *tree;
  struct wlr_scene_rect *bg_rect;      // Vollflächiger Dim
  struct wlr_scene_buffer *panel_buf;  // Panel-Hintergrund (abgerundet)
  struct wlr_scene_buffer *title_buf;  // Titelzeile
  bool visible;
  int panel_h;
  int screen_w, screen_h;

  struct settings_item items[SETTINGS_MAX_ITEMS];
  int items_n;

  // Alle während des Aufbaus erzeugten Nodes (Labels, Werte, Zeilen-Hintergründe)
  struct wlr_scene_buffer *nodes[SETTINGS_MAX_NODES];
  int nodes_n;
};

// Einstellungen-Seite initialisieren (Scene-Tree im Overlay)
void settings_init(struct phone_server *server);
// Seite öffnen bzw. neu aufbauen
void settings_show(struct phone_settings *s);
// Seite schließen und alle Scene-Nodes entfernen
void settings_hide(struct phone_settings *s);
// Seite ein-/ausblenden (Umschalter)
void settings_toggle(struct phone_settings *s);
// Tap auf der Seite verarbeiten (Elemente schalten)
void settings_handle_tap(struct phone_settings *s, double x, double y);
// settings.conf aus dem aktuellen Zustand schreiben
void settings_write(struct phone_settings *s);

#endif