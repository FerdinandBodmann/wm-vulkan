#define _POSIX_C_SOURCE 200809L
#include "hw.h"
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>

// Datei komplett in einen String einlesen (Zeilenumbrüche entfernen)
static int hw_read_str(const char *path, char *buf, size_t len) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  size_t n = fread(buf, 1, len - 1, f);
  fclose(f);
  if (n == 0) return -1;
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';
  return 0;
}

// Ganzzahl aus einer Sysfs-Datei lesen
static int hw_read_int(const char *path, int *out) {
  char buf[32];
  if (hw_read_str(path, buf, sizeof(buf)) < 0) return -1;
  *out = atoi(buf);
  return 0;
}

// Ein Systemkommando ausführen (Fehler nur loggen, nicht abbrechen)
static void hw_run(const char *cmd) {
  if (system(cmd) != 0)
    wlr_log(WLR_ERROR, "hw: command failed: %s", cmd);
}

// Ausgabe eines Kommandos in einen Puffer lesen (letzte Zeilen bleiben erhalten)
static int hw_cmd_read(const char *cmd, char *buf, size_t len) {
  FILE *p = popen(cmd, "r");
  if (!p) return -1;
  size_t n = 0;
  while (n < len - 1 && fgets(buf + n, (int)(len - n), p))
    n += strlen(buf + n);
  pclose(p);
  buf[n] = '\0';
  return 0;
}

// WLAN: zweites Feld von "nmcli -t radio" (WIFI-HW:WIFI:WWAN-HW:WWAN)
int hw_wifi_state(void) {
  char out[256];
  if (hw_cmd_read("nmcli -t radio", out, sizeof(out)) < 0) return -1;
  char *field = strtok(out, ":");
  if (!field) return -1;
  field = strtok(NULL, ":");
  if (!field) return -1;
  if (strcmp(field, "enabled") == 0) return 1;
  if (strcmp(field, "disabled") == 0) return 0;
  return -1;
}

// Bluetooth: "Powered:"-Zeile von "bluetoothctl show"
int hw_bluetooth_state(void) {
  char out[512];
  if (hw_cmd_read("bluetoothctl show", out, sizeof(out)) < 0) return -1;
  char *line = strtok(out, "\n");
  while (line) {
    while (*line == ' ' || *line == '\t') line++;
    if (strncmp(line, "Powered:", 8) == 0) {
      const char *val = line + 8;
      while (*val == ' ' || *val == '\t') val++;
      if (strncmp(val, "yes", 3) == 0) return 1;
      if (strncmp(val, "no", 2) == 0) return 0;
      return -1;
    }
    line = strtok(NULL, "\n");
  }
  return -1;
}

// Flugmodus-Zustand: ermittelt aus WLAN- und Bluetooth-Status
int hw_airplane_state(void) {
  int w = hw_wifi_state();
  int b = hw_bluetooth_state();
  if (w < 0 && b < 0) return -1;
  int woff = w == 0;
  int boff = b == 0;
  if (w < 0) return boff;
  if (b < 0) return woff;
  return (woff && boff) ? 1 : 0;
}

// WLAN per nmcli ein-/ausschalten
void hw_wifi_set(bool on) {
  hw_run(on ? "nmcli radio wifi on" : "nmcli radio wifi off");
}

// Bluetooth per bluetoothctl ein-/ausschalten
void hw_bluetooth_set(bool on) {
  hw_run(on ? "bluetoothctl power on" : "bluetoothctl power off");
}

// Flugmodus ein-/ausschalten (WLAN + Bluetooth)
void hw_airplane_set(bool on) {
  hw_run(on ? "nmcli radio all off" : "nmcli radio all on");
  hw_run(on ? "bluetoothctl power off" : "bluetoothctl power on");
}

// Pfad zum Backlight + Maximalwert im Sysfs suchen
static int hw_backlight_paths(char *brightness, size_t blen, int *max_out) {
  glob_t g;
  if (glob("/sys/class/backlight/*/max_brightness", 0, NULL, &g) != 0) return -1;
  int found = -1;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    int max;
    if (hw_read_int(g.gl_pathv[i], &max) < 0 || max <= 0) continue;
    size_t n = strlen(g.gl_pathv[i]);
    snprintf(brightness, blen, "%.*sbrightness", (int)(n - 14), g.gl_pathv[i]);
    *max_out = max;
    found = 0;
    break;
  }
  globfree(&g);
  return found;
}

// Aktuelle Display-Helligkeit in Prozent abfragen
int hw_backlight_value(void) {
  char path[256];
  int max;
  if (hw_backlight_paths(path, sizeof(path), &max) < 0) return -1;
  int cur;
  if (hw_read_int(path, &cur) < 0 || max <= 0) return -1;
  if (cur >= max) return 100;
  return (cur * 100 + max / 2) / max;
}

// Display-Helligkeit in Prozent setzen (0-100)
void hw_backlight_set(int percent) {
  char path[256];
  int max;
  if (hw_backlight_paths(path, sizeof(path), &max) < 0) return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  int val = percent * max / 100;
  FILE *f = fopen(path, "w");
  if (!f) {
    wlr_log(WLR_ERROR, "hw: cannot write backlight %s", path);
    return;
  }
  fprintf(f, "%d", val);
  fclose(f);
}

// Taschenlampe: beschreibbare LED mit "flash"/"torch" im Namen.
static int hw_torch_brightness_path(char *out, size_t len) {
  glob_t g;
  if (glob("/sys/class/leds/*/brightness", 0, NULL, &g) != 0) return -1;
  int found = -1;
  const char *base = "/sys/class/leds/";
  for (size_t i = 0; i < g.gl_pathc; i++) {
    if (access(g.gl_pathv[i], W_OK) != 0) continue;
    const char *p = g.gl_pathv[i];
    if (strncmp(p, base, strlen(base)) != 0) continue;
    const char *name = p + strlen(base);
    const char *slash = strrchr(name, '/');
    size_t nlen = slash ? (size_t)(slash - name) : strlen(name);
    if (nlen == 0 || nlen >= 64) continue;
    char lname[64];
    memcpy(lname, name, nlen);
    lname[nlen] = '\0';
    for (size_t k = 0; k < nlen; k++)
      if (lname[k] >= 'A' && lname[k] <= 'Z') lname[k] += 32;
    if (strstr(lname, "flash") || strstr(lname, "torch")) {
      snprintf(out, len, "%s", p);
      found = 0;
      break;
    }
  }
  globfree(&g);
  return found;
}

// Ist die Taschenlampe gerade an?
int hw_torch_state(void) {
  char path[256];
  if (hw_torch_brightness_path(path, sizeof(path)) < 0) return -1;
  int v;
  if (hw_read_int(path, &v) < 0) return -1;
  return v > 0 ? 1 : 0;
}

// Taschenlampe ein-/ausschalten (auf Maximalwert bzw. 0)
void hw_torch_set(bool on) {
  char path[256];
  if (hw_torch_brightness_path(path, sizeof(path)) < 0) {
    wlr_log(WLR_INFO, "%s", "hw: no torch LED available");
    return;
  }
  char max_path[300];
  snprintf(max_path, sizeof(max_path), "%s", path);
  char *b = strstr(max_path, "/brightness");
  if (b) strcpy(b, "/max_brightness");
  int max = 1;
  hw_read_int(max_path, &max);
  FILE *f = fopen(path, "w");
  if (!f) {
    wlr_log(WLR_ERROR, "hw: cannot write torch %s", path);
    return;
  }
  fprintf(f, "%d", on ? max : 0);
  fclose(f);
}

// Aktuelle Lautstärke in Prozent abfragen (pactl)
int hw_volume_value(void) {
  FILE *p = popen("pactl get-sink-volume @DEFAULT_SINK@", "r");
  if (!p) return -1;
  char line[256];
  int pct = -1;
  while (fgets(line, sizeof(line), p)) {
    char *pc = strchr(line, '%');
    if (!pc) continue;
    char *q = pc - 1;
    while (q >= line && *q >= '0' && *q <= '9') q--;
    if (q + 1 < pc) {
      pct = atoi(q + 1);
      break;
    }
  }
  pclose(p);
  return pct;
}

// Lautstärke in Prozent setzen (0-100, pactl)
void hw_volume_set(int percent) {
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", percent);
  hw_run(cmd);
}
