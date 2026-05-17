#pragma once

// ---------- WiFi ----------
#define WIFI_SSID       "YOUR_SSID"
#define WIFI_PASS       "YOUR_PASSWORD"

// ---------- Meshtastic MQTT (public defaults) ----------
#define MQTT_HOST       "mqtt.meshtastic.org"
#define MQTT_PORT       1883
#define MQTT_USER       "meshdev"
#define MQTT_PASS_STR   "large4cats"
// Topic: msh/<region>/2/e/# — change region to match your node config
// Common regions: US, EU_433, EU_868, ANZ, KR, TW, RU, IN, JP, CN, NZ_865
#define MQTT_TOPIC      "msh/US/2/e/#"

// ---------- WS2812B 8x32 Panel ----------
#define LED_PIN         5       // GPIO pin connected to panel DIN
#define NUM_LEDS        256
#define MATRIX_W        32
#define MATRIX_H        8
#define BRIGHTNESS      40      // 0-255, keep low on USB power

// ---------- Scroll speed ----------
#define SCROLL_MS       35      // ms per pixel step (lower = faster)
