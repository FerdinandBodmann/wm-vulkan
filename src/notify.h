#ifndef PHONE_WM_NOTIFY_H
#define PHONE_WM_NOTIFY_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>

#define NOTIFY_MAX 8     // Maximale Anzahl gespeicherter Benachrichtigungen
#define NOTIFY_CARD_H 64 // Höhe einer Benachrichtigungs-Karte

struct phone_server;

// Eine einzelne Benachrichtigung
struct phone_notification {
  char *app;     // Name der App, die die Benachrichtigung erzeugt hat
  char *title;   // Titel
  char *body;    // Text/Beschreibung
  uint32_t time; // Unix-Zeitstempel (für die Anzeige)
};

// Benachrichtigungs-Shade (Panels, die von oben hereingeschoben wird)
struct phone_notify {
  struct phone_server *server;
  struct wlr_scene_tree *tree;      // Scene-Baum des Shade
  struct wlr_scene_rect *bg_rect;   // Vollflächiger Dim
  struct wlr_scene_buffer *header_buf; // "Benachrichtigungen" - Titel
  struct phone_notification list[NOTIFY_MAX]; // Gespeicherte Nachrichten
  int count;                        // Anzahl aktiver Nachrichten
  bool visible;                     // Ob der Shade gerade offen ist
};

// Benachrichtigungen initialisieren (Scene-Tree im Overlay anlegen)
void notify_init(struct phone_server *server);

// Neue Benachrichtigung posten (älteste wird verdrängt)
void notify_post(struct phone_server *server,
                 const char *app, const char *title, const char *body);

// Anzahl aktiver Benachrichtigungen (für Statusleisten-Indikator)
int notify_count(struct phone_notify *n);

// Shade öffnen / schließen
void notify_show(struct phone_notify *n);
void notify_hide(struct phone_notify *n);
void notify_toggle(struct phone_notify *n);

// Alle Benachrichtigungen löschen
void notify_clear_all(struct phone_notify *n);

// Tap im Shade verarbeiten (Tap außerhalb schließt)
void notify_handle_tap(struct phone_notify *n, double x, double y);

#endif