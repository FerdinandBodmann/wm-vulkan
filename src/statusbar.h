#ifndef PHONE_WM_STATUSBAR_H
#define PHONE_WM_STATUSBAR_H

#include <wlr/types/wlr_scene.h>
#include <wayland-server-core.h>

#define STATUSBAR_HEIGHT 28 // Höhe der Statusleiste oben in Pixeln

struct phone_server;

// Statusleiste (oben): Zeit, Akku, WLAN-Anzeige
struct phone_statusbar {
  struct phone_server *server;
  struct wlr_scene_rect *bg_rect;      // Hintergrund-Rechteck der Leiste
  struct wlr_scene_buffer *time_buf;   // Scene-Buffer für die Uhrzeit
  struct wlr_buffer *time_wlr_buf;     // WLR-Buffer der Uhrzeit
  struct wlr_scene_buffer *status_buf; // Scene-Buffer für den Status
  struct wlr_buffer *status_wlr_buf;   // WLR-Buffer des Status
  struct wlr_scene_buffer *ws_buf;     // Workspace-Indikatorpunkte
  struct wl_event_source *timer;       // Timer für automatische Updates
  int height;                          // Höhe der Leiste in Pixeln
  bool low_warned;                     // Warnung bei schwachem Akku schon gezeigt
};

// Statusleiste initialisieren (Hintergrund, Texte, Timer)
void statusbar_init(struct phone_server *server);
// Statusleiste bei Änderungen der Ausgabe neu positionieren
void statusbar_update(struct phone_statusbar *sbar);
// Statusleiste aufräumen und freigeben
void statusbar_destroy(struct phone_statusbar *sbar);

#endif
