#define _POSIX_C_SOURCE 200809L
#include "statusbar.h"
#include "server.h"
#include "render.h"
#include "theme.h"
#include "anim.h"
#include <drm_fourcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

// Hilfsfunktion: Dateiinhalt als String lesen
static int read_file_str(const char *path, char *buf, size_t len) {
  // Datei im Lesemodus öffnen
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  // Inhalt in den Puffer lesen (max len-1 Bytes)
  size_t n = fread(buf, 1, len - 1, f);
  fclose(f);
  // Nichts gelesen? Dann Fehler
  if (n == 0) return -1;
  // Null-Terminator setzen
  buf[n] = '\0';
  // Zeilenumbrüche am Ende abschneiden
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return (int)n;
}

// Akkustand und Ladezustand aus dem sysfs auslesen
// Akkustand aus dem Sysfs lesen und als kurzen Text formatieren
static void read_battery(char *buf, size_t len) {
  char cap[16] = "?";
  char status[32] = "";

  // Mögliche Pfade für Batterie-Informationen
  const char *bases[] = {
    "/sys/class/power_supply/BAT0",
    "/sys/class/power_supply/BAT1",
    "/sys/class/power_supply/battery",
    NULL,
  };

  // Durch alle möglichen Batterie-Pfade iterieren
  for (int i = 0; bases[i]; i++) {
    char path[128];
    // Kapazität auslesen
    snprintf(path, sizeof(path), "%s/capacity", bases[i]);
    if (read_file_str(path, cap, sizeof(cap)) >= 0) {
      // Ladestatus zusätzlich auslesen
      snprintf(path, sizeof(path), "%s/status", bases[i]);
      read_file_str(path, status, sizeof(status));
      // Funktioniert, also Schleife beenden
      break;
    }
  }

  /* Also check for USB power supply (charging without battery) */
  // Falls keine Batterie gefunden, USB-Netzteil prüfen (z.B. Desktop ohne Akku)
  if (cap[0] == '?') {
    const char *usb_bases[] = {
      "/sys/class/power_supply/USB",
      "/sys/class/power_supply/AC",
      "/sys/class/power_supply/ADP0",
      NULL,
    };
    // Alle USB/AC-Pfade durchgehen
    for (int i = 0; usb_bases[i]; i++) {
      char path[128];
      snprintf(path, sizeof(path), "%s/online", usb_bases[i]);
      char online[8];
      // Prüfen ob Netzteil angeschlossen ist
      if (read_file_str(path, online, sizeof(online)) >= 0 && online[0] == '1') {
        snprintf(buf, len, "%s", "AC");
        return;
      }
    }
    // Weder Batterie noch Netzteil gefunden
    snprintf(buf, len, "?%%");
    return;
  }

  // Status formatieren: "Full" heißt volle Ladung
  if (status[0] && strcmp(status, "Full") == 0)
    snprintf(buf, len, "%s%%", cap);
  // "Charg" im Status heißt am Laden
  else if (status[0] && strstr(status, "Charg"))
    snprintf(buf, len, "%s%%", cap);
  // Sonst wird nicht geladen
  else
    snprintf(buf, len, "\u25A0 %s%%", cap);
}

// WLAN-Verbindungsstatus prüfen
// WLAN-Status lesen und als kurzen Text formatieren
static void read_wifi(char *buf, size_t len) {
  // Gängige WLAN-Schnittstellen durchprobieren
  const char *ifaces[] = { "wlan0", "wlp2s0", "wlp3s0", NULL };
  for (int i = 0; ifaces[i]; i++) {
    char path[128];
    char state[16];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifaces[i]);
    // Prüfen ob die Schnittstelle "up" ist
    if (read_file_str(path, state, sizeof(state)) >= 0 && strcmp(state, "up") == 0) {
      snprintf(buf, len, "WiFi");
      return;
    }
  }
  // Kein WLAN gefunden, Buffer leeren
  buf[0] = '\0';
}

// Akkustand in Prozent zurückgeben (oder -1 falls nicht ladebar),
// zusätzlich ob gerade geladen wird
static int battery_level(int *charging) {
  char cap[16] = "?";
  char status[32] = "";
  if (charging) *charging = 0;

  const char *bases[] = {
    "/sys/class/power_supply/BAT0",
    "/sys/class/power_supply/BAT1",
    "/sys/class/power_supply/battery",
    NULL,
  };

  for (int i = 0; bases[i]; i++) {
    char path[128];
    snprintf(path, sizeof(path), "%s/capacity", bases[i]);
    if (read_file_str(path, cap, sizeof(cap)) >= 0) {
      snprintf(path, sizeof(path), "%s/status", bases[i]);
      read_file_str(path, status, sizeof(status));
      break;
    }
  }

  if (cap[0] == '?') return -1;
  if (charging) *charging = status[0] && strstr(status, "Charg") ? 1 : 0;
  int pct = atoi(cap);
  return pct;
}

// Textinhalte — echtes Phone: Zeit mittig fett, links Carrier/dots, rechts Batterie-Pille
static void update_text(struct phone_statusbar *sbar) {
  struct phone_server *server = sbar->server;
  int sw = 0;
  int lsw,lsh; float lsc; anim_get_logical_size(server, &lsw, &lsh, &lsc);
  sw = lsw;
  if (sw < 1) sw = 800;

  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char time_str[16];
  strftime(time_str, sizeof(time_str), "%H:%M", tm);

  if (sbar->time_buf) { wlr_scene_node_destroy(&sbar->time_buf->node); sbar->time_buf = NULL; }
  float fg_r = server->theme ? server->theme->fg[0] : 1.0f;
  float fg_g = server->theme ? server->theme->fg[1] : 1.0f;
  float fg_b = server->theme ? server->theme->fg[2] : 1.0f;
  // Center clock — bold, slightly larger like iOS
  sbar->time_wlr_buf = phone_render_text(server->allocator,
      time_str, 96, sbar->height, 14, fg_r, fg_g, fg_b);
  if (sbar->time_wlr_buf) {
    sbar->time_buf = wlr_scene_buffer_create(sbar->bg_rect->node.parent, sbar->time_wlr_buf);
    wlr_scene_buffer_set_source_box(sbar->time_buf, &(struct wlr_fbox){0, 0, 96, sbar->height});
    wlr_scene_node_set_position(&sbar->time_buf->node, (sw - 96)/2, 0);
    wlr_buffer_drop(sbar->time_wlr_buf);
    sbar->time_wlr_buf = NULL;
  }

  // Workspace pill — active = wide capsule, inactive = dot (like iOS page dots)
  if (sbar->ws_buf) { wlr_scene_node_destroy(&sbar->ws_buf->node); sbar->ws_buf = NULL; }
  char ws_str[32] = "";
  // use em dash + dots with spacing that mimics pill: ● ● ○
  for (int w = 0; w < NUM_WORKSPACES; w++) {
    if (w > 0) strncat(ws_str, "  ", sizeof(ws_str)-strlen(ws_str)-1);
    // active is slightly larger filled circle, others dim
    strncat(ws_str, w == server->current_workspace ? "\u25CF" : "\u2022",
        sizeof(ws_str)-strlen(ws_str)-1);
  }
  struct wlr_buffer *ws_wlr = phone_render_text(server->allocator,
      ws_str, 120, sbar->height, 8, fg_r*0.9f, fg_g*0.9f, fg_b*0.9f);
  if (ws_wlr) {
    sbar->ws_buf = wlr_scene_buffer_create(sbar->bg_rect->node.parent, ws_wlr);
    wlr_scene_buffer_set_source_box(sbar->ws_buf, &(struct wlr_fbox){0, 0, 120, sbar->height});
    // place just left of centered time, or centered if no time? keep below time centered
    wlr_scene_node_set_position(&sbar->ws_buf->node, (sw - 120)/2, 18);
    wlr_buffer_drop(ws_wlr);
  }

  // Right side: battery + wifi + notif badge — cleaner, tinted
  char battery[32]; char wifi[16];
  int charging = 0; int level = battery_level(&charging);
  read_battery(battery, sizeof(battery));
  read_wifi(wifi, sizeof(wifi));
  struct phone_notify *notify = server->notify;
  if (level >= 0 && !charging && level <= 15) {
    if (!sbar->low_warned) {
      sbar->low_warned = true;
      notify_post(server, "System", "Akku fast leer", "Bitte rechtzeitig aufladen.");
    }
  } else if (level < 0 || charging) sbar->low_warned = false;
  int notify_n = notify_count(notify);
  char status_str[64] = "";
  if (notify_n > 0) snprintf(status_str, sizeof(status_str), "\u2022%d  ", notify_n);
  // battery as pill: "◼ 84%" but with charging bolt if needed
  if (battery[0] != '?' && battery[0]) {
    char b2[36];
    if (charging) snprintf(b2, sizeof(b2), "\u26A1 %s", battery);
    else snprintf(b2, sizeof(b2), "%s", battery);
    strncat(status_str, b2, sizeof(status_str)-strlen(status_str)-1);
  }
  if (wifi[0]) {
    if (status_str[0]) strncat(status_str, "  ", sizeof(status_str)-strlen(status_str)-1);
    strncat(status_str, "\u2248", sizeof(status_str)-strlen(status_str)-1); // wifi-ish
  }
  if (!status_str[0]) snprintf(status_str, sizeof(status_str), "\u25CF");
  if (sbar->status_buf) { wlr_scene_node_destroy(&sbar->status_buf->node); sbar->status_buf = NULL; }
  float sr = 0.92f, sg = 0.92f, sb = 0.94f;
  if (level >= 0 && !charging && level <= 15) { sr = 1.0f; sg = 0.38f; sb = 0.38f; }
  // dim slightly for inactive look, bright for active
  sbar->status_wlr_buf = phone_render_text(server->allocator,
      status_str, 170, sbar->height, 11, sr, sg, sb);
  if (sbar->status_wlr_buf) {
    sbar->status_buf = wlr_scene_buffer_create(sbar->bg_rect->node.parent, sbar->status_wlr_buf);
    wlr_scene_buffer_set_source_box(sbar->status_buf, &(struct wlr_fbox){0, 0, 170, sbar->height});
    wlr_scene_node_set_position(&sbar->status_buf->node, sw - 170 - 10, 0);
    wlr_buffer_drop(sbar->status_wlr_buf);
    sbar->status_wlr_buf = NULL;
  }
  // Left carrier / signal placeholder — subtle
  // we reuse status buf right side; left side could show "Phone WM" faint
}

// Timer-Callback für automatische Updates
static int timer_tick(void *data) {
  struct phone_statusbar *sbar = data;
  // Texte neu aktualisieren
  update_text(sbar);
  return 1; /* keep timer running */
}

// Statusleiste — iOS-like: translucent capsule bar with centered time
void statusbar_init(struct phone_server *server) {
  struct phone_statusbar *sbar = calloc(1, sizeof(struct phone_statusbar));
  if (!sbar) return;
  server->statusbar = sbar;
  sbar->server = server;
  sbar->height = 36; // slightly taller for pill + Dynamic Island feel

  float bg_r = 0.08f, bg_g = 0.08f, bg_b = 0.10f;
  if (server->theme) {
    bg_r = server->theme->bar_bg[0];
    bg_g = server->theme->bar_bg[1];
    bg_b = server->theme->bar_bg[2];
  }
  // Use translucent glass — real phone blurs wallpaper underneath
  sbar->bg_rect = wlr_scene_rect_create(
      server->layer_top_tree, 1, sbar->height,
      (float[4]){bg_r, bg_g, bg_b, 0.97f});
  wlr_scene_node_set_position(&sbar->bg_rect->node, 0, 0);
  update_text(sbar);
  struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
  sbar->timer = wl_event_loop_add_timer(loop, timer_tick, sbar);
  if (sbar->timer) wl_event_source_timer_update(sbar->timer, 30000);
  wlr_log(WLR_INFO, "%s", "Status bar initialized (pill)");
}

// Statusleiste bei Änderungen der Ausgabe neu positionieren
void statusbar_update(struct phone_statusbar *sbar) {
  if (!sbar) return;
  struct phone_output *output;
  // Neue Bildschirmbreite übernehmen
  wl_list_for_each(output, &sbar->server->outputs, link) {
    int sw = output->wlr_output->width;
    // Hintergrund oben positionieren und auf volle Breite setzen
    wlr_scene_node_set_position(&sbar->bg_rect->node, 0, 0);
    wlr_scene_rect_set_size(sbar->bg_rect, sw, sbar->height);
    // Erste Ausgabe reicht
    break;
  }
  // Texte mit neuer Breite aktualisieren
  update_text(sbar);
}

// Statusleiste aufräumen und freigeben
void statusbar_destroy(struct phone_statusbar *sbar) {
  if (!sbar) return;
  // Timer entfernen wenn vorhanden
  if (sbar->timer) {
    wl_event_source_remove(sbar->timer);
  }
  // Speicher freigeben
  free(sbar);
}
