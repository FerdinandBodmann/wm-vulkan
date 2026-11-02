#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include "anim.h"
#include <wlr/types/wlr_scene.h>

// Output zerstört: Listener abhängen und Speicher freigeben
void output_begin_destroy(struct wl_listener *listener, void *data) {
  struct phone_output *output = wl_container_of(listener, output, destroy);
  wl_list_remove(&output->link);
  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->commit.link);
  free(output);
}

// Output konfigurieren: aktivieren und bevorzugte Auflösung setzen
void output_configure(struct phone_output *output) {
  if (!output->wlr_output) return;
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);
  struct wlr_output_mode *mode = wlr_output_preferred_mode(output->wlr_output);
  if (mode) {
    wlr_output_state_set_mode(&state, mode);
  }
  wlr_output_commit_state(output->wlr_output, &state);
  wlr_output_state_finish(&state);
}

// Ein Frame ist fällig: Scene rendern und frame_done an Clients senden
static void output_frame(struct wl_listener *listener, void *data) {
  struct phone_output *output = wl_container_of(listener, output, frame);

  wlr_scene_output_commit(output->scene_output, NULL);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(output->scene_output, &now);
}

// Output-Zustandsanforderung vom Backend weiterleiten (z.B. Auflösungswechsel)
static void output_request_state(struct wl_listener *listener, void *data) {
  struct phone_output *output = wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;

  wlr_output_commit_state(output->wlr_output, event->state);
}

// Output-Commit: Hintergrund, Wallpaper, Nachtmodus und Balken anpassen
static void output_commit(struct wl_listener *listener, void *data) {
  struct phone_output *output = wl_container_of(listener, output, commit);
  struct phone_server *server = output->server;

  int ow = output->wlr_output->width;
  int oh = output->wlr_output->height;
  wlr_scene_rect_set_size(server->background_rect, ow, oh);
  if (server->night_rect) wlr_scene_rect_set_size(server->night_rect, ow, oh);
  wallpaper_update(server);
  bar_update(server->bar);
  statusbar_update(server->statusbar);
}

// Frame-, Request-State- und Commit-Listener am Output registrieren
void output_handle_frame(struct phone_output *output) {
  output->frame.notify = output_frame;
  wl_signal_add(&output->wlr_output->events.frame, &output->frame);

  output->request_state.notify = output_request_state;
  wl_signal_add(&output->wlr_output->events.request_state, &output->request_state);

  output->commit.notify = output_commit;
  wl_signal_add(&output->wlr_output->events.commit, &output->commit);
}
