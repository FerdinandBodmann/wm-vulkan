#include "server.h"
#include "anim.h"
#include <string.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

// Aufgerufen wenn ein XDG-Fenster gemappt wird (also sichtbar wird)
static void view_map(struct wl_listener *listener, void *data) {
  // Holt die view-struktur aus dem listener
  struct phone_view *view = wl_container_of(listener, view, map);
  struct wlr_xdg_surface *xdg = view->xdg_surface;

  // Erstellt einen neuen scene-tree für das XDG-Fenster im workspace
  view->scene_tree = wlr_scene_xdg_surface_create(
      view->workspace->scene_tree, xdg);
  // Falls die Szene nicht erstellt werden konnte, abbrechen
  if (!view->scene_tree) {
    wlr_log(WLR_ERROR, "%s", "Failed to create xdg scene node");
    return;
  }
  // Speichert die view alsdaten am scene-node damit wir sie später wiederfinden
  view->scene_tree->node.data = view;
  view->mapped = true;

  // Setzt den Fullscreen-Zustand (wenn Fullscreen aktiv war, jetzt anwenden)
  view_set_fullscreen(view, view->fullscreen);

  // Gibt dem Fenster den Fokus damit es direkt interagiert werden kann
  focus_view(view, xdg->surface);
}

// Aufgerufen wenn ein Fenster verschwindet (unmapped)
static void view_unmap(struct wl_listener *listener, void *data) {
  struct phone_view *view = wl_container_of(listener, view, unmap);

  // Szene-Knoten zerstören falls vorhanden
  if (view->scene_tree) {
    wlr_scene_node_destroy(&view->scene_tree->node);
    view->scene_tree = NULL;
  }
  view->mapped = false;
}

// Aufgerufen wenn die view komplett zerstört wird
static void view_destroy(struct wl_listener *listener, void *data) {
  struct phone_view *view = wl_container_of(listener, view, destroy);

  // Alle Listener aus den Listen entfernen damit nichts mehr aufrufen kann
  wl_list_remove(&view->link);
  wl_list_remove(&view->map.link);
  wl_list_remove(&view->unmap.link);
  wl_list_remove(&view->destroy.link);
  wl_list_remove(&view->commit.link);
  // Falls noch ein Scene-Node existiert, auch den weg
  if (view->scene_tree) {
    wlr_scene_node_destroy(&view->scene_tree->node);
  }
  // Freigeben des Speichers für die view
  free(view);
}

// Wird bei jedem Surface-Commit aufgerufen (neue Daten/Größenänderung etc.)
static void view_commit(struct wl_listener *listener, void *data) {
  struct phone_view *view = wl_container_of(listener, view, commit);
  struct wlr_xdg_surface *xdg = view->xdg_surface;

  // Beim ersten Commit: Initiale Größe setzen (Fenster passt an den Bildschirm an)
  if (view->initial_configure_pending && xdg->initialized) {
    view->initial_configure_pending = false;
    struct phone_server *server = view->server;
    struct phone_output *output;
    // Nehmen den ersten Output um die Bildschirmgröße zu erfahren
    wl_list_for_each(output, &server->outputs, link) {
      int ow = output->wlr_output->width;
      int oh = output->wlr_output->height;
      // Bar-Höhe abziehen falls eine Leiste da ist
      int bar_h = server->bar ? server->bar->height : 0;
      int sbar_h = server->statusbar ? server->statusbar->height : 0;
      // Fenstergröße auf gesamte Fläche minus Leisten setzen
      wlr_xdg_toplevel_set_size(xdg->toplevel, ow, oh - bar_h - sbar_h);
      break; // Nur erster Output zählt
    }
  }
}

// Wird aufgerufen wenn der Toplevel zerstört wird
static void view_toplevel_destroy(struct wl_listener *listener, void *data) {
  struct phone_view *view = wl_container_of(listener, view, toplevel_destroy);
  // Listener für Fullscreen und Toplevel-Destoy entfernen
  wl_list_remove(&view->request_fullscreen.link);
  wl_list_remove(&view->toplevel_destroy.link);
}

// Das Fenster fragt an ob es Fullscreen werden soll
static void view_request_fullscreen(struct wl_listener *listener, void *data) {
  struct phone_view *view = wl_container_of(listener, view, request_fullscreen);
  struct wlr_xdg_toplevel *toplevel = data;

  // Fullscreen-Zustand vom Toplevel übernehmen
  view_set_fullscreen(view, toplevel->requested.fullscreen);
}

// Setzt den Fullscreen-Zustand einer view
void view_set_fullscreen(struct phone_view *view, bool fullscreen) {
  view->fullscreen = fullscreen;
  struct phone_server *server = view->server;

  struct phone_output *output;
  // Durch alle Outputs iterieren (eigentlich breaken wir nach dem ersten)
  wl_list_for_each(output, &server->outputs, link) {
    int ow = output->wlr_output->width;
    int oh = output->wlr_output->height;
    int bar_h = server->bar ? server->bar->height : 0;
    int sbar_h = server->statusbar ? server->statusbar->height : 0;

    // View oben an der Statusleiste positionieren
    if (view->scene_tree) {
      wlr_scene_node_set_position(&view->scene_tree->node, 0, sbar_h);
    }

    // Bei Fullscreen: Fenstergröße auf gesamten verfügbaren Bereich setzen
    if (fullscreen && view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
      wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel,
                                ow, oh - bar_h - sbar_h);
    }
    break; // Nur erstes Output
  }
}

// Erstellt alle internen Workspaces
static void workspace_init_internal(struct phone_server *server) {
  // Für jeden Workspace: Struktur allocieren und initialisieren
  for (int i = 0; i < NUM_WORKSPACES; i++) {
    struct phone_workspace *ws = calloc(1, sizeof(struct phone_workspace));
    if (!ws) return; // Speicher fehlt, abbrechen
    ws->server = server;
    ws->id = i;
    // Scene-Tree für den Workspace erstellen
    ws->scene_tree = wlr_scene_tree_create(server->workspace_tree);
    // Leere View-Liste initialisieren
    wl_list_init(&ws->views);
    // Workspace in die globale Liste einfügen
    wl_list_insert(&server->workspaces, &ws->link);
    // Daten am Node speichern
    ws->scene_tree->node.data = ws;
    // Nur den ersten Workspace aktiv schalten, Rest ausblenden
    wlr_scene_node_set_enabled(&ws->scene_tree->node, i == 0);
  }
  // Startworkspace ist 0
  server->current_workspace = 0;
}

// Öffentliche Init-Funktion für Workspaces
void workspace_init(struct phone_server *server) {
  wl_list_init(&server->workspaces); // Workspace-Liste leer machen
  workspace_init_internal(server);
}

// Gibt den aktuellen Workspace zurück
struct phone_workspace *workspace_current(struct phone_server *server) {
  struct phone_workspace *ws;
  // Liste durchsuchen nach passender ID
  wl_list_for_each(ws, &server->workspaces, link) {
    if (ws->id == server->current_workspace) return ws;
  }
  // Fallback: Ersten Workspace der Liste nehmen
  return wl_container_of(server->workspaces.next, ws, link);
}

// Wechselt zu einem anderen Workspace

static int ws_hide_old(void *data){
  struct phone_workspace *ws = data;
  wlr_scene_node_set_enabled(&ws->scene_tree->node, false);
  wlr_scene_node_set_position(&ws->scene_tree->node, 0, 0);
  return 0;
}

void workspace_switch_to(struct phone_server *server, int id) {
  if (id < 0 || id >= NUM_WORKSPACES) return;
  if (id == server->current_workspace) return;
  int lw, lh; float sc; anim_get_logical_size(server, &lw, &lh, &sc);
  int dir = (id > server->current_workspace) ? 1 : -1;
  struct phone_workspace *old_ws = workspace_current(server);
  struct phone_workspace *new_ws = NULL;
  struct phone_workspace *it;
  wl_list_for_each(it, &server->workspaces, link) if(it->id==id) new_ws=it;
  if(!new_ws) return;
  // prepare new offscreen
  wlr_scene_node_set_position(&new_ws->scene_tree->node, dir * lw, 0);
  wlr_scene_node_set_enabled(&new_ws->scene_tree->node, true);
  // animate
  anim_cancel(old_ws->scene_tree);
  anim_cancel(new_ws->scene_tree);
  anim_slide_x(server, old_ws->scene_tree, 0, -dir * lw, 260, NULL, NULL);
  anim_slide_x(server, new_ws->scene_tree, dir * lw, 0, 260, NULL, NULL);
  // schedule hide of old after anim
  struct wl_event_source *tm = wl_event_loop_add_timer(server->event_loop, ws_hide_old, old_ws);
  if(tm) wl_event_source_timer_update(tm, 270);
  server->current_workspace = id;
  new_ws = workspace_current(server); // re-fetch after id change (same)
  // keep new enabled (already)


  // Workspace-Indikator in der Statusleiste aktualisieren
  if (server->statusbar) statusbar_update(server->statusbar);

  /* Frame erzwingen — wenn ein deaktivierter Baum aktiviert wird, passiert
   * kein Schaden weil die Bounding-Box leer war. Deshalb manuell triggern. */
  struct phone_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    wlr_output_schedule_frame(output->wlr_output);
  }

  // Erstes gemapptes Fenster im neuen Workspace fokussieren
  struct phone_view *view;
  wl_list_for_each(view, &new_ws->views, link) {
    if (view->mapped) {
      focus_view(view, view->xdg_surface->surface);
      return; // Ein Fokus reicht
    }
  }
}

// Erstellt eine neue View (Fenster) für ein XDG-Surface
void view_create(struct phone_server *server,
                 struct wlr_xdg_surface *xdg_surface) {
  // Aktuellen Workspace holen
  struct phone_workspace *ws = workspace_current(server);

  // View-Struktur allocieren
  struct phone_view *view = calloc(1, sizeof(struct phone_view));
  if (!view) return;
  view->server = server;
  view->workspace = ws;
  view->xdg_surface = xdg_surface;
  view->mapped = false;
  view->fullscreen = true; // Standardmäßig Fullscreen an
  view->initial_configure_pending = true; // Erster Configure steht aus

  // Listener für Map-Event anmelden
  view->map.notify = view_map;
  wl_signal_add(&xdg_surface->surface->events.map, &view->map);
  // Listener für Unmap-Event anmelden
  view->unmap.notify = view_unmap;
  wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
  // Listener für Destroy-Event anmelden
  view->destroy.notify = view_destroy;
  wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
  // Listener für Commit-Event anmelden
  view->commit.notify = view_commit;
  wl_signal_add(&xdg_surface->surface->events.commit, &view->commit);
  // Listener für Fullscreen-Anfrage vom Fenster
  view->request_fullscreen.notify = view_request_fullscreen;
  wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen,
                &view->request_fullscreen);
  // Listener für Toplevel-Destroy
  view->toplevel_destroy.notify = view_toplevel_destroy;
  wl_signal_add(&xdg_surface->toplevel->events.destroy,
                &view->toplevel_destroy);

  // View in die Workspace-View-Liste einfügen
  wl_list_insert(&ws->views, &view->link);
}
