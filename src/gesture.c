#include "server.h"
#include "lock.h"
#include "notify.h"
#include "power.h"
#include <math.h>
#include <wlr/types/wlr_xdg_shell.h>

// Mindest-Abstand in Pixel damit ein Swipe erkannt wird
#define SWIPE_THRESHOLD 30.0
// Untere 15% des Bildschirms gelten als unterer Rand für Launcher-Swipe
#define BOTTOM_EDGE_FRACTION 0.15
// Obere 12% des Bildschirms gelten als oberer Rand für Control-Center-Swipe
#define TOP_EDGE_FRACTION 0.12

// Neue Gesture initialisieren
void gesture_begin(struct phone_gesture *gesture, double x, double y,
                   uint32_t time, int32_t touch_id) {
  // Gesture ist jetzt aktiv
  gesture->active = true;
  // Startposition merken
  gesture->start_x = x;
  gesture->start_y = y;
  // Letzte Position auf Start setzen
  gesture->last_x = x;
  gesture->last_y = y;
  // Zeitstempel speichern
  gesture->start_time = time;
  // Welcher Finger war das
  gesture->touch_id = touch_id;
  // Noch keine Gesture erkannt
  gesture->type = GESTURE_NONE;
  // Schwellenwert noch nicht erreicht
  gesture->threshold_met = false;
}

// Bei jedem Touch-Move updaten
void gesture_update(struct phone_gesture *gesture, double x, double y,
                    uint32_t time) {
  // Abbrechen wenn Gesture inaktiv oder Schwellenwert schon erreicht
  if (!gesture->active || gesture->threshold_met) return;

  // Weg seit Start berechnen (absolut)
  double dx = fabs(x - gesture->start_x);
  double dy = fabs(y - gesture->start_y);

  // Letzte Position aktualisieren
  gesture->last_x = x;
  gesture->last_y = y;

  // Prüfen ob der Schwellenwert überschritten ist
  if (dx > SWIPE_THRESHOLD || dy > SWIPE_THRESHOLD) {
    gesture->threshold_met = true;
    // Je nachdem was mehr bewegt wurde: horizontal oder vertikal
    gesture->type = (dx > dy) ? GESTURE_SWIPE_HORIZONTAL : GESTURE_SWIPE_VERTICAL;
  }
}

// Swipe-Up in Recents-View: Karte an der Position schließen
static void recents_handle_swipe_up(struct phone_recents *recents,
                                     double start_x, double start_y) {
  // Durch alle Karten iterieren
  for (int i = 0; i < recents->num_cards; i++) {
    struct phone_recents_card *card = &recents->cards[i];
    // Prüfen ob der Tap innerhalb dieser Karte liegt
    if (start_x >= card->x && start_x < card->x + card->w &&
        start_y >= card->y && start_y < card->y + card->h) {
      // Karte schließen wenn sie ein gemapptes View hat
      if (card->view && card->view->mapped) {
        wlr_xdg_toplevel_send_close(card->view->xdg_surface->toplevel);
      }
      break;
    }
  }
}

// Gesture auswerten wenn Finger losgelassen wird
void gesture_end(struct phone_gesture *gesture, double x, double y,
                 uint32_t time, struct phone_server *server) {
  // Nichts zu tun wenn keine Gesture aktiv war
  if (!gesture->active) return;

  // Gesamtweg berechnen (Vektor)
  double dx = x - gesture->start_x;
  double dy = y - gesture->start_y;

  // Ist der Sperrbildschirm gerade aktiv? Nur hochwischen entsperrt.
  bool lock_visible = server->lock && server->lock->visible;
  if (lock_visible) {
    if (gesture->type == GESTURE_SWIPE_VERTICAL &&
        dy < 0 && fabs(dy) > SWIPE_THRESHOLD) {
      lock_hide(server->lock);
    }
    gesture->active = false;
    gesture->type = GESTURE_NONE;
    gesture->threshold_met = false;
    return;
  }

  // Ist das Power-Menü offen? Jeder Swipe schließt es.
  bool power_visible = server->power && server->power->visible;
  if (power_visible) {
    power_hide(server->power);
    gesture->active = false;
    gesture->type = GESTURE_NONE;
    gesture->threshold_met = false;
    return;
  }

  // Ist das Recents-Overlay gerade sichtbar?
  bool recents_visible = server->recents && server->recents->visible;
  // Ist das Control Center gerade sichtbar?
  bool cc_visible = server->control_center && server->control_center->visible;
  // Ist der Benachrichtigungs-Shade gerade sichtbar?
  bool notify_visible = server->notify && server->notify->visible;

  // Horizontaler Swipe: Workspace wechseln
  if (gesture->type == GESTURE_SWIPE_HORIZONTAL && fabs(dx) > SWIPE_THRESHOLD) {
    if (!recents_visible && !cc_visible && !notify_visible) {
      if (dx < 0) {
        // Nach links swipe = nächster Workspace (wrapping)
        workspace_switch_to(server,
            (server->current_workspace + 1) % NUM_WORKSPACES);
      } else {
        // Nach rechts swipe = vorheriger Workspace
        workspace_switch_to(server,
            (server->current_workspace - 1 + NUM_WORKSPACES) % NUM_WORKSPACES);
      }
    }
  } else if (gesture->type == GESTURE_SWIPE_VERTICAL) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &output_box);
    // Y-Positionen der Ränder berechnen
    double top_edge = output_box.height * TOP_EDGE_FRACTION;
    double bottom_edge = output_box.height * (1.0 - BOTTOM_EDGE_FRACTION);

    if (notify_visible) {
      // Benachrichtigungs-Shade ist offen: hoch wischen schließt ihn
      if (dy < 0 && fabs(dy) > SWIPE_THRESHOLD) {
        notify_hide(server->notify);
      }
    } else if (cc_visible) {
      // Control Center ist offen: hoch wischen schließt es
      if (dy < 0 && fabs(dy) > SWIPE_THRESHOLD) {
        control_center_hide(server->control_center);
      }
    } else if (recents_visible) {
      // Recents ist offen
      if (dy < 0 && fabs(dy) > SWIPE_THRESHOLD) {
        // Nach oben swipe = Karte an der Position schließen
        recents_handle_swipe_up(server->recents,
            gesture->start_x, gesture->start_y);
      } else if (dy > 0 && dy > SWIPE_THRESHOLD) {
        // Nach unten swipe = Recents schließen
        recents_hide(server->recents);
      }
    } else {
      // Kein Overlay offen: Ränder prüfen
      if (dy > 0 && dy > SWIPE_THRESHOLD && gesture->start_y < top_edge) {
        // Vom oberen Rand nach unten wischen = Control Center
        control_center_show(server->control_center);
      } else if (dy < 0 && fabs(dy) > SWIPE_THRESHOLD &&
                 gesture->start_y > bottom_edge) {
        // Von unten nach oben ab dem unteren Rand = Launcher zeigen
        launcher_show(server->launcher);
      } else if (dy > 0 && dy > SWIPE_THRESHOLD) {
        // Nach unten swipe = Launcher verstecken
        launcher_hide(server->launcher);
      }
    }
  }

  // Gesture zurücksetzen
  gesture->active = false;
  gesture->type = GESTURE_NONE;
  gesture->threshold_met = false;
}
