#include "server.h"
#include <wlr/util/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Programm-Einstiegspunkt: Renderer wählen und Server starten
int main(int argc, char *argv[]) {
  wlr_log_init(WLR_DEBUG, NULL); // Logging auf Debug-Level aktivieren

  const char *renderer = "vulkan";
  if (argc > 1) {
    if (strcmp(argv[1], "vulkan") == 0 || strcmp(argv[1], "gles2") == 0 ||
        strcmp(argv[1], "auto") == 0) {
      renderer = argv[1];
    } else {
      fprintf(stderr, "Usage: %s [vulkan|gles2|auto]\n", argv[0]);
      return 1;
    }
  }

  struct phone_server server;
  server.renderer_name = renderer;
  server_init(&server); // Server initialisieren (Outputs, Sessions, etc.)
  server_run(&server); // Event-Loop starten (blockiert bis Ende)
  server_finish(&server); // Aufräumen beim Beenden

  return 0;
}
