#include "server.h"
#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

// Gibt den passenden Scene-Tree für eine Layer-Ebene zurück
static struct wlr_scene_tree *layer_get_parent(struct phone_server *server,
                                                enum zwlr_layer_shell_v1_layer l) {
  // Je nach Layer-Typ den richtigen Baum auswählen
  switch (l) {
  case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
    return server->layer_background_tree; // Hintergrund (Desktop-Wallpaper etc.)
  case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
    return server->layer_bottom_tree; // Unterste Ebene
  case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
    return server->layer_top_tree; // Obere Ebene
  case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
    return server->layer_overlay_tree; // Über alles drüber (z.B. Notifications)
  }
  return NULL; // Unbekannte Layer
}

// Aufgerufen wenn eine Layer-Surface gemappt wird
static void layer_surface_map(struct wl_listener *listener, void *data) {
  struct phone_layer_surface *ls = wl_container_of(listener, ls, map);
  struct phone_server *server = ls->server;
  struct wlr_layer_surface_v1 *layer = ls->wlr_layer_surface;

  ls->mapped = true;

  // Richtigen Parent-Tree für die Layer-Ebene holen
  struct wlr_scene_tree *parent = layer_get_parent(server, layer->pending.layer);
  if (!parent) return; // Ungültige Layer-Ebene, nichts tun

  // Scene-Layer-Surface im passenden Tree erstellen
  ls->scene_layer =
      wlr_scene_layer_surface_v1_create(parent, layer);
}

// Aufgerufen wenn eine Layer-Surface verschwindet
static void layer_surface_unmap(struct wl_listener *listener, void *data) {
  struct phone_layer_surface *ls = wl_container_of(listener, ls, unmap);

  // Falls eine Scene existiert, zerstören
  if (ls->scene_layer) {
    wlr_scene_node_destroy(&ls->scene_layer->tree->node);
    ls->scene_layer = NULL;
  }
  ls->mapped = false;
}

// Aufgerufen wenn eine Layer-Surface komplett zerstört wird
static void layer_surface_destroy(struct wl_listener *listener, void *data) {
  struct phone_layer_surface *ls = wl_container_of(listener, ls, destroy);

  // Alle Listener entfernen damit nichts mehr aufruft
  wl_list_remove(&ls->link);
  wl_list_remove(&ls->map.link);
  wl_list_remove(&ls->unmap.link);
  wl_list_remove(&ls->destroy.link);
  wl_list_remove(&ls->commit.link);

  // Falls Scene-Node noch da war, auch den weg
  if (ls->scene_layer) {
    wlr_scene_node_destroy(&ls->scene_layer->tree->node);
  }
  // Speicher freigeben
  free(ls);
}

// Wird bei jedem Surface-Commit aufgerufen (Layout neu berechnen)
static void layer_surface_commit(struct wl_listener *listener, void *data) {
  struct phone_layer_surface *ls = wl_container_of(listener, ls, commit);
  struct phone_server *server = ls->server;

  // Abbrechen wenn die Layer-Surface ungültig ist
  if (!ls->wlr_layer_surface) return;

  // Output der Surface holen
  struct wlr_output *output = ls->wlr_layer_surface->output;
  if (output) {
    // Alle Layer-Surfaces auf diesem Output neu anordnen
    layer_surface_arrange(server, output);
  }
}

// Erstellt eine neue Layer-Surface
void layer_surface_create(struct phone_server *server,
                          struct wlr_layer_surface_v1 *layer_surface) {
  // Speicher für die Struktur allokieren
  struct phone_layer_surface *ls = calloc(1, sizeof(struct phone_layer_surface));
  if (!ls) return;
  ls->server = server;
  ls->wlr_layer_surface = layer_surface;
  ls->mapped = false;

  // Map-Listener anmelden
  ls->map.notify = layer_surface_map;
  wl_signal_add(&layer_surface->surface->events.map, &ls->map);
  // Unmap-Listener anmelden
  ls->unmap.notify = layer_surface_unmap;
  wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);
  // Destroy-Listener anmelden
  ls->destroy.notify = layer_surface_destroy;
  wl_signal_add(&layer_surface->events.destroy, &ls->destroy);
  // Commit-Listener anmelden
  ls->commit.notify = layer_surface_commit;
  wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);

  // Layer-Surface in die globale Liste einfügen
  wl_list_insert(&server->layer_surfaces, &ls->link);
}

// Ordnet alle Layer-Surfaces auf einem Output neu an
void layer_surface_arrange(struct phone_server *server,
                           struct wlr_output *output) {
  int ow, oh;
  if (output) {
    // Output-Breite und -Höhe direkt nehmen
    ow = output->width;
    oh = output->height;
  } else {
    // Kein Output angegeben → gesamtes Layout als Referenz nehmen
    struct wlr_box full_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &full_box);
    ow = full_box.width;
    oh = full_box.height;
  }

  /* Erste Runde: Exclusive-Zonen von oben/unten berechnen
   * (z.B. Statusleiste blockiert Platz für andere Layers) */
  struct phone_layer_surface *ls;
  int top_exclusive = 0;
  int bottom_exclusive = 0;

  wl_list_for_each(ls, &server->layer_surfaces, link) {
    // Nur gemappte Surfaces auf dem richtigen Output beachten
    if (!ls->mapped || !ls->wlr_layer_surface) continue;
    if (ls->wlr_layer_surface->output != output) continue;

    struct wlr_layer_surface_v1_state *state = &ls->wlr_layer_surface->current;
    uint32_t anchor = state->anchor;

    // Nur Surfaces mit positivem Exclusive-Zone berücksichtigen
    if (state->exclusive_zone <= 0) continue;

    // Oben verankerter Layer: Höhe + Margin als Exclusive berechnen
    if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) &&
        !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
      int h = state->desired_height > 0 ? state->desired_height : 0;
      int exclusive = h + state->margin.top;
      if (exclusive > top_exclusive) top_exclusive = exclusive;
    }
    // Unten verankerter Layer: Höhe + Margin als Exclusive berechnen
    if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) &&
        !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
      int h = state->desired_height > 0 ? state->desired_height : 0;
      int exclusive = h + state->margin.bottom;
      if (exclusive > bottom_exclusive) bottom_exclusive = exclusive;
    }
  }

  /* Verfügbare Höhe nach Abzug der Exclusive-Zonen */
  int area_h = oh - top_exclusive - bottom_exclusive;
  if (area_h < 0) area_h = 0; // Sicherheitscheck

  /* Zweite Runde: Jede Surface mit dem verfügbaren Bereich konfigurieren */
  wl_list_for_each(ls, &server->layer_surfaces, link) {
    // Nur gemappte Surfaces auf diesem Output bearbeiten
    if (!ls->mapped || !ls->wlr_layer_surface) continue;
    if (ls->wlr_layer_surface->output != output) continue;

    struct wlr_layer_surface_v1_state *state = &ls->wlr_layer_surface->current;
    uint32_t w = state->desired_width;
    uint32_t h = state->desired_height;

    /* Bei links/rechts verankerten Surfaces: Volle Breite nutzen */
    if (state->anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
      w = ow;
    }
    /* Bei oben/unten verankerten Surfaces: Volle verfügbare Höhe nutzen */
    if (state->anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                         ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
      h = area_h;
    }

    // Layer-Surface mit berechneten Maßen konfigurieren
    wlr_layer_surface_v1_configure(ls->wlr_layer_surface, w, h);
  }
}
