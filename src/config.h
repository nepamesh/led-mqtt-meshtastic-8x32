#pragma once

// ---------- WiFi ----------
#define WIFI_SSID       "YourNetworkName"
#define WIFI_PASS       "YourPassword"

// ---------- NEPAMesh MQTT ----------
#define MQTT_HOST       "107.172.196.126"   // mqtt.nepamesh.com
#define MQTT_PORT       1883
#define MQTT_USER       "sign"
#define MQTT_PASS_STR   "yourpassword"
#define MQTT_TOPIC      "msh/US/2/e/LongFast/#"

// ---------- WS2812B 8x32 Panel ----------
#define LED_PIN         5       // GPIO pin to DIN
#define NUM_LEDS        256
#define MATRIX_W        32
#define MATRIX_H        8
#define BRIGHTNESS      40      // 0-255, keep low for 5V USB power

// ---------- Display orientation ----------
// These match the SVFISHKK 8x32 panel wired column-major with connector on the right.
// Flip one or both if text appears mirrored, upside-down, or scrolls the wrong direction.
#define MATRIX_CONNECTOR_RIGHT  1   // 1 = connector on right side, 0 = connector on left
#define MATRIX_FLIP_Y           0   // 1 = flip vertically (text upside-down)

// ---------- Scroll speed ----------
#define SCROLL_MS       35      // ms per pixel step
