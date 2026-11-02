#ifndef PHONE_WM_HW_H
#define PHONE_WM_HW_H

#include <stdbool.h>

// Zustände der Hardware (1=an, 0=aus, -1=unbekannt)
int hw_wifi_state(void);       // WLAN-Zustand abfragen
int hw_bluetooth_state(void);  // Bluetooth-Zustand abfragen
int hw_airplane_state(void);   // Flugmodus-Zustand abfragen
int hw_torch_state(void);      // Taschenlampen-Zustand abfragen
int hw_backlight_value(void);  // Display-Helligkeit in Prozent (0-100)
int hw_volume_value(void);     // Lautstärke in Prozent (0-100)

// Hardware ein-/ausschalten
void hw_wifi_set(bool on);       // WLAN ein/aus
void hw_bluetooth_set(bool on);  // Bluetooth ein/aus
void hw_airplane_set(bool on);   // Flugmodus ein/aus
void hw_torch_set(bool on);      // Taschenlampe ein/aus
void hw_backlight_set(int percent); // Helligkeit setzen (0-100)
void hw_volume_set(int percent);    // Lautstärke setzen (0-100)

#endif
