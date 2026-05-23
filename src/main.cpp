#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include "mbedtls/aes.h"
#include "config.h"
#include "font5x7.h"
#include "emoji.h"

// ============================================================
// Log ring buffer + UDP broadcast
// ============================================================
#define LOG_PORT     4210
#define LOG_LINES    48
#define LOG_LINE_LEN 256
static WiFiUDP s_log_udp;
static char    s_log_buf[LOG_LINES][LOG_LINE_LEN];
static int     s_log_tail  = 0;
static int     s_log_count = 0;

static void ulog(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    // ring buffer (strip trailing newline)
    char line[LOG_LINE_LEN];
    strncpy(line, buf, LOG_LINE_LEN - 1);
    line[LOG_LINE_LEN - 1] = 0;
    int ll = strlen(line);
    if (ll > 0 && line[ll - 1] == '\n') line[ll - 1] = 0;
    strncpy(s_log_buf[s_log_tail], line, LOG_LINE_LEN - 1);
    s_log_tail = (s_log_tail + 1) % LOG_LINES;
    if (s_log_count < LOG_LINES) s_log_count++;
    // UDP
    if (WiFi.status() == WL_CONNECTED) {
        s_log_udp.beginPacket(IPAddress(255, 255, 255, 255), LOG_PORT);
        s_log_udp.print(buf);
        s_log_udp.endPacket();
    }
}

// ============================================================
// Runtime settings  (NVS-backed, fall back to config.h defaults)
// ============================================================
static bool        s_ap_mode = false;
static Preferences s_prefs;
static char     s_wifi_ssid[33]  = WIFI_SSID;
static char     s_wifi_pass[65]  = WIFI_PASS;
static char     s_mqtt_host[40]  = MQTT_HOST;
static uint16_t s_mqtt_port      = MQTT_PORT;
static char     s_mqtt_user[32]  = MQTT_USER;
static char     s_mqtt_pass[64]  = MQTT_PASS_STR;
static char     s_mqtt_topic[64] = MQTT_TOPIC;
static bool     s_conn_right     = MATRIX_CONNECTOR_RIGHT;
static bool     s_flip_y         = MATRIX_FLIP_Y;
static uint8_t  s_brightness     = BRIGHTNESS;
static uint16_t s_scroll_ms      = SCROLL_MS;
static uint8_t  s_repeat_count   = 3;
static uint8_t  s_led_r          = 0;
static uint8_t  s_led_g          = 200;
static uint8_t  s_led_b          = 0;
static int8_t   s_utc_offset     = 0;
static char     s_hourly_msg[256] = {};
static uint8_t  s_msg_interval   = 60;   // minutes between custom message (0 = off)
static uint8_t  s_date_interval  = 10;   // minutes between date display  (0 = off)
static uint8_t  s_effect         = 0;    // 0=none 1=rainbow 2=cycle 3=gradient
static uint8_t  s_grad_r         = 255;  // gradient end color
static uint8_t  s_grad_g         = 0;
static uint8_t  s_grad_b         = 0;
static uint8_t  s_idle_mode      = 0;    // 0=clock 1=fire 2=rain 3=scroll 4=twinkle 5=off

static void load_settings() {
    s_prefs.begin("nepamesh", true);
    s_prefs.getString("w_ssid",  s_wifi_ssid,  sizeof(s_wifi_ssid));
    s_prefs.getString("w_pass",  s_wifi_pass,  sizeof(s_wifi_pass));
    s_prefs.getString("m_host",  s_mqtt_host,  sizeof(s_mqtt_host));
    s_mqtt_port  = s_prefs.getUShort("m_port",  s_mqtt_port);
    s_prefs.getString("m_user",  s_mqtt_user,  sizeof(s_mqtt_user));
    s_prefs.getString("m_pass",  s_mqtt_pass,  sizeof(s_mqtt_pass));
    s_prefs.getString("m_topic", s_mqtt_topic, sizeof(s_mqtt_topic));
    s_conn_right = s_prefs.getBool("d_cr",   s_conn_right);
    s_flip_y     = s_prefs.getBool("d_fy",   s_flip_y);
    s_brightness   = s_prefs.getUChar("d_br",  s_brightness);
    s_scroll_ms    = s_prefs.getUShort("d_sm", s_scroll_ms);
    s_repeat_count = s_prefs.getUChar("d_rep", s_repeat_count);
    s_led_r        = s_prefs.getUChar("d_lr",  s_led_r);
    s_led_g        = s_prefs.getUChar("d_lg",  s_led_g);
    s_led_b        = s_prefs.getUChar("d_lb",  s_led_b);
    s_utc_offset     = (int8_t)s_prefs.getChar("d_tz",   s_utc_offset);
    s_prefs.getString("hourly_msg", s_hourly_msg, sizeof(s_hourly_msg));
    s_msg_interval   = s_prefs.getUChar("m_iv",  s_msg_interval);
    s_date_interval  = s_prefs.getUChar("d_iv",  s_date_interval);
    s_effect         = s_prefs.getUChar("d_fx",  s_effect);
    s_grad_r         = s_prefs.getUChar("d_gr",  s_grad_r);
    s_grad_g         = s_prefs.getUChar("d_gg",  s_grad_g);
    s_grad_b         = s_prefs.getUChar("d_gb",  s_grad_b);
    s_idle_mode      = s_prefs.getUChar("d_im",  s_idle_mode);
    s_prefs.end();
}

static void save_settings() {
    s_prefs.begin("nepamesh", false);
    s_prefs.putString("w_ssid",  s_wifi_ssid);
    s_prefs.putString("w_pass",  s_wifi_pass);
    s_prefs.putString("m_host",  s_mqtt_host);
    s_prefs.putUShort("m_port",  s_mqtt_port);
    s_prefs.putString("m_user",  s_mqtt_user);
    s_prefs.putString("m_pass",  s_mqtt_pass);
    s_prefs.putString("m_topic", s_mqtt_topic);
    s_prefs.putBool("d_cr",      s_conn_right);
    s_prefs.putBool("d_fy",      s_flip_y);
    s_prefs.putUChar("d_br",     s_brightness);
    s_prefs.putUShort("d_sm",    s_scroll_ms);
    s_prefs.putUChar("d_rep",    s_repeat_count);
    s_prefs.putUChar("d_lr",     s_led_r);
    s_prefs.putUChar("d_lg",     s_led_g);
    s_prefs.putUChar("d_lb",     s_led_b);
    s_prefs.putChar("d_tz",         s_utc_offset);
    s_prefs.putString("hourly_msg", s_hourly_msg);
    s_prefs.putUChar("m_iv",        s_msg_interval);
    s_prefs.putUChar("d_iv",        s_date_interval);
    s_prefs.putUChar("d_fx",        s_effect);
    s_prefs.putUChar("d_gr",        s_grad_r);
    s_prefs.putUChar("d_gg",        s_grad_g);
    s_prefs.putUChar("d_gb",        s_grad_b);
    s_prefs.putUChar("d_im",        s_idle_mode);
    s_prefs.end();
}

// ============================================================
// Meshtastic default AES-128 key (PSK "AQ==" / LongFast channel)
// ============================================================
static const uint8_t MESH_KEY[16] = {
    0xd4,0xf1,0xbb,0x3a, 0x20,0x29,0x07,0x59,
    0xf0,0xbd,0xff,0xab, 0xcf,0x4e,0x69,0x01
};

// ============================================================
// LED matrix
// ============================================================
static CRGB leds[NUM_LEDS];

// Column-major serpentine — orientation from runtime settings.
static uint16_t XY(int x, int y) {
    if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return NUM_LEDS;
    int col = s_conn_right ? (MATRIX_W - 1 - x) : x;
    return (col & 1)
        ? (s_flip_y ? col * MATRIX_H + MATRIX_H - 1 - y : col * MATRIX_H + y)
        : (s_flip_y ? col * MATRIX_H + y : col * MATRIX_H + MATRIX_H - 1 - y);
}

// ============================================================
// Scrolling text
// ============================================================
#define SCROLL_BUF_COLS 1600
static CRGB s_buf[SCROLL_BUF_COLS][MATRIX_H];
static int      s_width      = 0;
static int      s_pos        = 0;
static uint32_t s_last_ms    = 0;
static bool     s_active     = false;
static char     s_cur_msg[256] = {};  // 256 == MSG_LEN (defined later)
static int      s_cur_repeats  = 0;

// Decode one UTF-8 code point; advances *p past the bytes consumed.
static uint32_t utf8_next(const char *&p) {
    uint8_t c = (uint8_t)*p;
    if (!c) return 0;
    p++;
    if (c < 0x80) return c;
    uint32_t cp; int extra;
    if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else return 0xFFFD;
    while (extra--) {
        if ((*p & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
    }
    return cp;
}

static int render_text(const char *text, CRGB buf[][MATRIX_H], int max_cols) {
    auto blank_col = [&](int c) {
        for (int r = 0; r < MATRIX_H; r++) buf[c][r] = CRGB::Black;
    };
    int col = 0;
    blank_col(col++); blank_col(col++);     // 2 leading blank cols

    const char *p = text;
    while (*p && col < max_cols - 7) {
        uint32_t cp = utf8_next(p);
        if (!cp) break;
        // skip variation selectors, ZWJ, combining chars
        if (cp == 0xFE0F || cp == 0x200D || cp == 0x20E3) continue;

        const EmojiGlyph *eg = find_emoji(cp);
        if (eg) {
            for (int ec = 0; ec < EMOJI_W && col < max_cols - 2; ec++, col++) {
                for (int r = 0; r < MATRIX_H; r++)
                    buf[col][r] = (r < EMOJI_H) ? eg->px[r][ec] : CRGB::Black;
            }
            blank_col(col++);               // 1 space after emoji
        } else if (cp >= 32 && cp <= 126) {
            const uint8_t *g = font5x7[cp - 32];
            for (int fc = 0; fc < 5; fc++, col++) {
                uint8_t bits = g[fc];
                for (int r = 0; r < MATRIX_H; r++)
                    buf[col][r] = (bits & (1 << r)) ? CRGB(s_led_r, s_led_g, s_led_b) : CRGB::Black;
            }
            blank_col(col++);               // 1 space after char
        }
        // unknown code points silently skipped
    }
    for (int i = 0; i < MATRIX_W && col < max_cols; i++) blank_col(col++);
    return col;
}

static void start_scroll(const char *text) {
    ulog("[DISP] scroll: %s\n", text);
    s_width   = render_text(text, s_buf, SCROLL_BUF_COLS);
    s_pos     = -MATRIX_W;
    s_active  = true;
    s_last_ms = millis();
}

static CRGB apply_effect(CRGB base, int src) {
    if (!base) return CRGB::Black;          // off pixel — always stays off
    if (s_effect == 0) return base;         // none
    uint8_t hue;
    if (s_effect == 1) {                    // rainbow: hue by position in buffer
        hue = (uint8_t)((uint32_t)src * 255 / max(s_width, 1));
        return CHSV(hue, 240, 255);
    }
    if (s_effect == 2) {                    // cycle: hue animates over time
        hue = (uint8_t)(src * 4 + millis() / 15);
        return CHSV(hue, 240, 255);
    }
    if (s_effect == 3) {                    // gradient: text color → end color
        uint8_t t = (uint8_t)((uint32_t)src * 255 / max(s_width, 1));
        return CRGB(
            s_led_r + (int8_t)(((int)s_grad_r - s_led_r) * t / 255),
            s_led_g + (int8_t)(((int)s_grad_g - s_led_g) * t / 255),
            s_led_b + (int8_t)(((int)s_grad_b - s_led_b) * t / 255)
        );
    }
    return base;
}

// ============================================================
// Idle animations
// ============================================================

// --- Fire ---
static uint8_t  s_fire[MATRIX_H][MATRIX_W];
static uint32_t s_fire_ms = 0;

static CRGB heat_color(uint8_t h) {
    if (h < 85)  return CRGB(h * 3, 0, 0);
    if (h < 170) return CRGB(255, (h - 85) * 3, 0);
    return CRGB(255, 255, (h - 170) * 3);
}

static void update_fire() {
    if (millis() - s_fire_ms < 50) return;
    s_fire_ms = millis();
    for (int y = 0; y < MATRIX_H - 1; y++) {
        for (int x = 0; x < MATRIX_W; x++) {
            uint16_t v = s_fire[y+1][x]
                       + s_fire[y+1][(x+1) % MATRIX_W]
                       + s_fire[y+1][(x-1+MATRIX_W) % MATRIX_W];
            v += (y + 2 < MATRIX_H) ? s_fire[y+2][x] : s_fire[y+1][x];
            v /= 4;
            uint8_t cool = random8(0, 30);
            s_fire[y][x] = v > cool ? (uint8_t)(v - cool) : 0;
        }
    }
    for (int x = 0; x < MATRIX_W; x++)
        s_fire[MATRIX_H-1][x] = random8(180, 255);
    for (int x = 0; x < MATRIX_W; x++)
        for (int y = 0; y < MATRIX_H; y++) {
            uint16_t i = XY(x, y);
            if (i < NUM_LEDS) leds[i] = heat_color(s_fire[y][x]);
        }
    FastLED.show();
}

// --- Matrix Rain ---
struct RainDrop { int8_t y; uint8_t skip; uint8_t frame; uint8_t len; };
static RainDrop s_rain[MATRIX_W];
static uint32_t s_rain_ms  = 0;
static bool     s_rain_init = false;

static void update_rain() {
    if (!s_rain_init) {
        for (int x = 0; x < MATRIX_W; x++)
            s_rain[x] = { (int8_t)-(int8_t)random8(0, MATRIX_H * 2),
                          random8(1, 4), 0, random8(2, 6) };
        s_rain_init = true;
    }
    if (millis() - s_rain_ms < 80) return;
    s_rain_ms = millis();
    FastLED.clear();
    for (int x = 0; x < MATRIX_W; x++) {
        RainDrop &d = s_rain[x];
        if (++d.frame >= d.skip) {
            d.frame = 0;
            if (++d.y > MATRIX_H + d.len) {
                d.y    = -(int8_t)random8(1, MATRIX_H * 2);
                d.skip = random8(1, 4);
                d.len  = random8(2, 6);
            }
        }
        for (int t = 0; t <= d.len; t++) {
            int py = d.y - t;
            if (py < 0 || py >= MATRIX_H) continue;
            uint16_t i = XY(x, py);
            if (i >= NUM_LEDS) continue;
            if (t == 0) {
                leds[i] = CRGB(180, 255, 180);
            } else {
                uint8_t v = (uint8_t)(255 - (uint16_t)t * 220 / d.len);
                leds[i] = CRGB(0, v, 0);
            }
        }
    }
    FastLED.show();
}

// --- Twinkle ---
static uint8_t  s_twinkle[MATRIX_W][MATRIX_H];
static uint32_t s_twinkle_ms = 0;

static void update_twinkle() {
    if (millis() - s_twinkle_ms < 50) return;
    s_twinkle_ms = millis();
    for (int x = 0; x < MATRIX_W; x++) {
        for (int y = 0; y < MATRIX_H; y++) {
            if (s_twinkle[x][y] == 0 && random8(100) < 3)
                s_twinkle[x][y] = 255;
            uint16_t i = XY(x, y);
            if (i >= NUM_LEDS) continue;
            uint8_t v = s_twinkle[x][y];
            leds[i] = v ? CRGB(CHSV((uint8_t)(x * 8 + y * 32 + millis() / 50), 200, v))
                        : CRGB::Black;
            if (v > 15) s_twinkle[x][y] = v - 15;
            else        s_twinkle[x][y] = 0;
        }
    }
    FastLED.show();
}

static void update_idle_animation() {
    switch (s_idle_mode) {
        case 1: update_fire();    break;
        case 2: update_rain();    break;
        case 4: update_twinkle(); break;
        default: break;
    }
}

static void update_display() {
    if (!s_active) return;
    if (millis() - s_last_ms < (uint32_t)s_scroll_ms) return;
    s_last_ms = millis();

    for (int x = 0; x < MATRIX_W; x++) {
        int src = s_pos + x;
        for (int y = 0; y < MATRIX_H; y++) {
            uint16_t idx = XY(x, y);
            if (idx < NUM_LEDS) {
                CRGB px = (src >= 0 && src < s_width) ? s_buf[src][y] : CRGB::Black;
                leds[idx] = apply_effect(px, src);
            }
        }
    }
    FastLED.show();
    if (++s_pos >= s_width) { s_active = false; FastLED.clear(true); }
}

// ============================================================
// Node name cache
// ============================================================
struct NodeEntry { uint32_t id; char name[5]; };
static NodeEntry s_nodes[32];
static int       s_node_count = 0;

static const char *node_name(uint32_t id) {
    for (int i = 0; i < s_node_count; i++)
        if (s_nodes[i].id == id) return s_nodes[i].name;
    return nullptr;
}

static void cache_node(uint32_t id, const char *name) {
    for (int i = 0; i < s_node_count; i++) {
        if (s_nodes[i].id == id) { strncpy(s_nodes[i].name, name, 4); return; }
    }
    if (s_node_count < 32) {
        s_nodes[s_node_count].id = id;
        strncpy(s_nodes[s_node_count].name, name, 4);
        s_nodes[s_node_count].name[4] = 0;
        s_node_count++;
    }
}

// ============================================================
// Message queue
// ============================================================
#define MSG_Q   4
#define MSG_LEN 256
static char s_q[MSG_Q][MSG_LEN];
static int  s_qhead = 0, s_qtail = 0;

static void enqueue(const char *s) {
    strncpy(s_q[s_qtail], s, MSG_LEN - 1);
    s_q[s_qtail][MSG_LEN - 1] = 0;
    s_qtail = (s_qtail + 1) % MSG_Q;
}

static bool dequeue(char *out) {
    if (s_qhead == s_qtail) return false;
    strcpy(out, s_q[s_qhead]);
    s_qhead = (s_qhead + 1) % MSG_Q;
    return true;
}

// ============================================================
// Clock display + hourly message
// ============================================================
static void show_static(const char *text) {
    // Render into global s_buf (safe — only called when !s_active)
    int w = render_text(text, s_buf, SCROLL_BUF_COLS);
    int char_w = w - 2 - MATRIX_W;
    if (char_w < 0) char_w = 0;
    int pad = (MATRIX_W - char_w) / 2;
    FastLED.clear();
    for (int x = 0; x < MATRIX_W; x++) {
        int src = 2 + (x - pad);            // 2 skips leading blank cols
        for (int y = 0; y < MATRIX_H; y++) {
            uint16_t idx = XY(x, y);
            if (idx < NUM_LEDS)
                leds[idx] = (src >= 0 && src < w) ? s_buf[src][y] : CRGB::Black;
        }
    }
    FastLED.show();
}

static uint8_t  s_last_clock_min = 255;
static uint32_t s_last_clock_ms  = 0;

static void update_clock() {
    if (s_ap_mode) return;
    if (millis() - s_last_clock_ms < 1000) return;
    s_last_clock_ms = millis();

    time_t now; time(&now);
    if (now < 1000000000UL) {
        static uint32_t s_ntp_log_ms = 0;
        if (millis() - s_ntp_log_ms > 15000) {
            s_ntp_log_ms = millis();
            ulog("[CLK] waiting for NTP\n");
        }
        return;
    }
    // Apply UTC offset manually; system clock stores raw UTC
    time_t local_now = now + (long)s_utc_offset * 3600;
    struct tm t; gmtime_r(&local_now, &t);

    // Custom message on configurable interval
    static uint8_t s_last_msg_min = 255;
    if (s_msg_interval > 0 && s_hourly_msg[0] &&
        t.tm_min % s_msg_interval == 0 && (uint8_t)t.tm_min != s_last_msg_min) {
        s_last_msg_min = t.tm_min;
        enqueue(s_hourly_msg);
    }

    // Clock display when idle and minute changed — only in clock idle mode
    if (!s_active && (uint8_t)t.tm_min != s_last_clock_min) {
        s_last_clock_min = t.tm_min;
        if (s_idle_mode == 0) {
            int h = t.tm_hour % 12;
            if (h == 0) h = 12;
            char ts[8];
            snprintf(ts, sizeof(ts), "%d:%02d", h, t.tm_min);
            ulog("[CLK] show %s\n", ts);
            show_static(ts);
        }
    }

    // Date scroll on configurable interval when fully idle;
    // skip if custom message fires at the same minute
    static uint8_t s_last_date_min = 255;
    bool msg_fires_now = s_msg_interval > 0 && s_hourly_msg[0] &&
                         t.tm_min % s_msg_interval == 0;
    if (!msg_fires_now && s_date_interval > 0 &&
        !s_active && s_cur_repeats == 0 && s_qhead == s_qtail &&
        t.tm_min % s_date_interval == 0 && (uint8_t)t.tm_min != s_last_date_min) {
        s_last_date_min = t.tm_min;
        static const char *months[] = {
            "January","February","March","April","May","June",
            "July","August","September","October","November","December"
        };
        char ds[32];
        snprintf(ds, sizeof(ds), "%s %d, %d",
                 months[t.tm_mon], t.tm_mday, 1900 + t.tm_year);
        ulog("[CLK] date: %s\n", ds);
        start_scroll(ds);
    }
}

// ============================================================
// Minimal protobuf wire-format helpers
// ============================================================
static bool pb_varint(const uint8_t *&p, const uint8_t *end, uint64_t &v) {
    v = 0; int sh = 0;
    while (p < end) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7F) << sh; sh += 7;
        if (!(b & 0x80)) return true;
        if (sh >= 64) return false;
    }
    return false;
}

static bool pb_fixed32(const uint8_t *&p, const uint8_t *end, uint32_t &v) {
    if (p + 4 > end) return false;
    memcpy(&v, p, 4); p += 4; return true;
}

static bool pb_skip(const uint8_t *&p, const uint8_t *end, int wt) {
    uint64_t v;
    switch (wt) {
        case 0: return pb_varint(p, end, v);
        case 1: if (p+8>end) return false; p+=8; return true;
        case 2: if (!pb_varint(p,end,v)) return false;
                if (p+(size_t)v>end) return false; p+=v; return true;
        case 5: if (p+4>end) return false; p+=4; return true;
        default: return false;
    }
}

// ============================================================
// Meshtastic AES-128-CTR decrypt
// ============================================================
static bool mesh_decrypt(const uint8_t *enc, size_t len, uint8_t *out,
                          uint32_t from_node, uint32_t pkt_id) {
    uint8_t nonce[16] = {};
    memcpy(nonce,   &pkt_id,    4);
    memcpy(nonce+8, &from_node, 4);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    bool ok = false;
    if (mbedtls_aes_setkey_enc(&ctx, MESH_KEY, 128) == 0) {
        size_t nc_off = 0; uint8_t sb[16] = {};
        ok = (mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce, sb, enc, out) == 0);
    }
    mbedtls_aes_free(&ctx);
    return ok;
}

// ============================================================
// Protobuf parsers
// ============================================================

static bool parse_envelope(const uint8_t *data, size_t len,
                            const uint8_t *&pkt_out, size_t &pkt_len) {
    const uint8_t *p = data, *end = data + len;
    pkt_out = nullptr;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if (field == 1 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            pkt_out = p; pkt_len = sz; p += sz;
        } else { pb_skip(p, end, wt); }
    }
    return pkt_out != nullptr;
}

static bool parse_packet(const uint8_t *data, size_t len,
                          uint32_t &from, uint32_t &pkt_id,
                          const uint8_t *&enc,      size_t &enc_len,
                          const uint8_t *&dec_data, size_t &dec_len) {
    const uint8_t *p = data, *end = data + len;
    from = pkt_id = 0; enc = dec_data = nullptr; enc_len = dec_len = 0;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if      (field == 1 && wt == 5) pb_fixed32(p, end, from);
        else if (field == 1 && wt == 0) { uint64_t v; pb_varint(p, end, v); from = v; }
        else if (field == 6 && wt == 5) pb_fixed32(p, end, pkt_id);
        else if (field == 6 && wt == 0) { uint64_t v; pb_varint(p, end, v); pkt_id = v; }
        else if (field == 4 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            dec_data = p; dec_len = sz; p += sz;
        } else if (field == 8 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            enc = p; enc_len = sz; p += sz;
        } else { pb_skip(p, end, wt); }
    }
    return (enc && enc_len > 0) || (dec_data && dec_len > 0);
}

static bool parse_data(const uint8_t *data, size_t len,
                        uint32_t &portnum,
                        const uint8_t *&payload, size_t &payload_len) {
    const uint8_t *p = data, *end = data + len;
    portnum = 0; payload = nullptr;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if (field == 1 && wt == 0) {
            uint64_t v; if (!pb_varint(p, end, v)) return false; portnum = v;
        } else if (field == 2 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            payload = p; payload_len = sz; p += sz;
        } else { pb_skip(p, end, wt); }
    }
    return true;
}

static bool parse_short_name(const uint8_t *data, size_t len, char *out) {
    const uint8_t *p = data, *end = data + len;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if (field == 3 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            size_t n = min((size_t)sz, (size_t)4);
            memcpy(out, p, n); out[n] = 0; return true;
        } else { pb_skip(p, end, wt); }
    }
    return false;
}

// ============================================================
// MQTT message handler
// ============================================================
static uint8_t s_plain[256];

static void on_message(char *topic, uint8_t *payload, unsigned int length) {
    ulog("[MQTT] topic=%s len=%u\n", topic, length);
    if (length == 0) return;

    const uint8_t *pkt_data; size_t pkt_len;
    if (!parse_envelope(payload, length, pkt_data, pkt_len)) {
        ulog("[MQTT] envelope parse failed\n"); return;
    }

    uint32_t from_node, pkt_id;
    const uint8_t *enc; size_t enc_len;
    const uint8_t *dec_data; size_t dec_len;
    if (!parse_packet(pkt_data, pkt_len, from_node, pkt_id, enc, enc_len, dec_data, dec_len)) {
        ulog("[MQTT] packet parse failed\n"); return;
    }

    const uint8_t *data_bytes; size_t data_len;
    if (dec_data) {
        ulog("[MQTT] from=!%08X decoded_len=%u\n", (unsigned)from_node, (unsigned)dec_len);
        data_bytes = dec_data; data_len = dec_len;
    } else {
        ulog("[MQTT] from=!%08X enc_len=%u\n", (unsigned)from_node, (unsigned)enc_len);
        if (enc_len > sizeof(s_plain)) { ulog("[MQTT] too large\n"); return; }
        if (!mesh_decrypt(enc, enc_len, s_plain, from_node, pkt_id)) {
            ulog("[MQTT] decrypt failed\n"); return;
        }
        data_bytes = s_plain; data_len = enc_len;
    }

    uint32_t portnum;
    const uint8_t *payload_bytes; size_t payload_len;
    if (!parse_data(data_bytes, data_len, portnum, payload_bytes, payload_len)) {
        ulog("[MQTT] data parse failed\n"); return;
    }
    ulog("[MQTT] portnum=%u payload_len=%u\n",
         (unsigned)portnum, payload_bytes ? (unsigned)payload_len : 0);
    if (!payload_bytes || payload_len == 0) return;

    if (portnum == 1) {
        const char *name = node_name(from_node);
        char prefix[20];
        if (name) snprintf(prefix, sizeof(prefix), "%s: ", name);
        else       snprintf(prefix, sizeof(prefix), "!%04X: ", (unsigned)(from_node & 0xFFFF));

        char msg[MSG_LEN];
        snprintf(msg, sizeof(msg), "%s", prefix);
        size_t plen = strnlen(prefix, sizeof(prefix));
        size_t tlen = (payload_len < sizeof(msg) - plen - 1)
                      ? payload_len : sizeof(msg) - plen - 1;
        memcpy(msg + plen, payload_bytes, tlen);
        msg[plen + tlen] = 0;
        for (char *c = msg; *c; c++)
            if ((uint8_t)*c < 32 && *c != '\t') *c = ' '; // strip ctrl chars; UTF-8 high bytes pass through

        enqueue(msg);
        ulog("[MSG] enqueued: %s\n", msg);

    } else if (portnum == 4) {
        char sname[5] = {};
        if (parse_short_name(payload_bytes, payload_len, sname) && sname[0]) {
            cache_node(from_node, sname);
            ulog("[NODE] !%08X -> %s\n", (unsigned)from_node, sname);
        }
    } else {
        // telemetry, position, routing, etc. — log only, never display
        ulog("[SKIP] portnum=%u\n", (unsigned)portnum);
    }
}

// ============================================================
// WiFi / MQTT
// ============================================================
static WiFiClient   s_wifi;
static PubSubClient s_mqtt(s_wifi);
static DNSServer    s_dns;

// DNS resolution direct to 8.8.8.8 — bypasses whatever the ESP32's resolver does
static bool simple_dns_resolve(const char *hostname, IPAddress &result) {
    WiFiUDP udp;
    if (!udp.begin(5354)) return false;

    uint8_t pkt[64] = {};
    int pos = 0;
    pkt[pos++] = 0xAB; pkt[pos++] = 0xCD;  // transaction id
    pkt[pos++] = 0x01; pkt[pos++] = 0x00;  // standard query, RD=1
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  // QDCOUNT=1
    pos += 6;                               // ANCOUNT/NSCOUNT/ARCOUNT=0
    // encode hostname as length-prefixed labels
    char tmp[64]; strncpy(tmp, hostname, sizeof(tmp)-1); tmp[63] = 0;
    char *p = tmp;
    while (*p) {
        char *dot = strchr(p, '.');
        uint8_t len = dot ? (dot - p) : (uint8_t)strlen(p);
        pkt[pos++] = len; memcpy(pkt + pos, p, len); pos += len;
        if (!dot) break;
        p = dot + 1;
    }
    pkt[pos++] = 0;                         // name terminator
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  // QTYPE=A
    pkt[pos++] = 0x00; pkt[pos++] = 0x01;  // QCLASS=IN

    udp.beginPacket(IPAddress(8, 8, 8, 8), 53);
    udp.write(pkt, pos);
    udp.endPacket();

    uint32_t t0 = millis();
    while (millis() - t0 < 4000) {
        int len = udp.parsePacket();
        if (len >= 12) {
            uint8_t resp[512]; int rlen = udp.read(resp, sizeof(resp));
            if (resp[0] != 0xAB || resp[1] != 0xCD) { delay(5); continue; }
            uint16_t ancount = ((uint16_t)resp[6] << 8) | resp[7];
            if (ancount == 0) { udp.stop(); return false; }
            // skip header + question
            int idx = 12;
            while (idx < rlen) {
                if ((resp[idx] & 0xC0) == 0xC0) { idx += 2; break; }
                if (resp[idx] == 0) { idx++; break; }
                idx += 1 + resp[idx];
            }
            idx += 4; // QTYPE+QCLASS
            // parse answers
            for (uint16_t i = 0; i < ancount && idx + 10 <= rlen; i++) {
                if ((resp[idx] & 0xC0) == 0xC0) idx += 2;
                else { while (idx < rlen && resp[idx]) idx += 1 + resp[idx]; idx++; }
                uint16_t rtype = ((uint16_t)resp[idx]<<8)|resp[idx+1]; idx += 2;
                uint16_t rcls  = ((uint16_t)resp[idx]<<8)|resp[idx+1]; idx += 2;
                idx += 4; // TTL
                uint16_t rdlen = ((uint16_t)resp[idx]<<8)|resp[idx+1]; idx += 2;
                if (rtype == 1 && rcls == 1 && rdlen == 4 && idx + 4 <= rlen) {
                    result = IPAddress(resp[idx], resp[idx+1], resp[idx+2], resp[idx+3]);
                    udp.stop(); return true;
                }
                idx += rdlen;
            }
            udp.stop(); return false;
        }
        delay(10);
    }
    udp.stop(); return false;
}

static void wifi_start() {
    ulog("WiFi %s ", s_wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_wifi_ssid, s_wifi_pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        ulog("%s\n", WiFi.localIP().toString().c_str());
        return;
    }
    ulog("failed\n");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("sign");
    delay(100);
    s_dns.start(53, "*", WiFi.softAPIP());
    s_ap_mode = true;
    ulog("AP: sign / %s\n", WiFi.softAPIP().toString().c_str());
}

static void wifi_check() {
    if (s_ap_mode) return;
    if (WiFi.status() == WL_DISCONNECTED) {
        ulog("WiFi lost, reconnecting\n");
        WiFi.begin(s_wifi_ssid, s_wifi_pass);
    }
}

static uint32_t s_mqtt_retry_ms    = 0;
static bool     s_mqtt_was_connected = false;

static void mqtt_check() {
    if (s_ap_mode) return;
    bool connected = s_mqtt.connected();
    if (!connected && s_mqtt_was_connected)
        ulog("MQTT disconnected (state=%d) at %lums\n", s_mqtt.state(), millis());
    s_mqtt_was_connected = connected;

    if (connected) return;
    if (millis() - s_mqtt_retry_ms < 5000) return;
    s_mqtt_retry_ms = millis();
    char cid[24];
    snprintf(cid, sizeof(cid), "mesh-%llX", ESP.getEfuseMac());
    ulog("MQTT connecting...\n");
    if (s_mqtt.connect(cid, s_mqtt_user, s_mqtt_pass)) {
        ulog("MQTT OK\n");
        bool sub = s_mqtt.subscribe(s_mqtt_topic);
        ulog("SUB %s -> %s\n", s_mqtt_topic, sub ? "OK" : "FAIL");
    } else {
        ulog("MQTT err %d\n", s_mqtt.state());
    }
}

// ============================================================
// Custom NTP (bypasses broken IDF5 SNTP client)
// ============================================================
static WiFiUDP  s_ntp_udp;
static bool     s_ntp_pending  = false;
static uint32_t s_ntp_sent_ms  = 0;

static void ntp_poll() {
    if (s_ap_mode) return;
    time_t now; time(&now);
    if (now >= 1000000000UL) return;  // already synced

    if (!s_ntp_pending || millis() - s_ntp_sent_ms > 30000) {
        s_ntp_udp.stop();
        s_ntp_udp.begin(2390);
        uint8_t pkt[48] = {};
        pkt[0] = 0x1b;  // LI=0, VN=3, Mode=3 (client)
        s_ntp_udp.beginPacket(IPAddress(216, 239, 35, 0), 123);
        s_ntp_udp.write(pkt, 48);
        s_ntp_udp.endPacket();
        s_ntp_pending  = true;
        s_ntp_sent_ms  = millis();
        ulog("[CLK] NTP request sent\n");
    }

    if (s_ntp_udp.parsePacket() >= 48) {
        uint8_t buf[48];
        s_ntp_udp.read(buf, 48);
        uint32_t secs = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                        ((uint32_t)buf[42] << 8)  |  buf[43];
        secs -= 2208988800UL;  // NTP epoch 1900 → Unix epoch 1970
        struct timeval tv = { .tv_sec = (time_t)secs, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        s_ntp_udp.stop();
        s_ntp_pending = false;
        ulog("[CLK] NTP synced (UTC %lu)\n", (unsigned long)secs);
    }
}

// ============================================================
// Web server
// ============================================================
static WebServer s_web(80);

static void web_send_head(const char *title) {
    s_web.sendContent(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NEPAMesh Sign</title>"
        "<style>"
        "body{font-family:'Courier New',monospace;max-width:540px;margin:0 auto;"
        "padding:16px 12px;background:#0a0a0a;color:#33ff33}"
    );
    s_web.sendContent(
        ".logo{text-align:center;margin-bottom:12px}"
        ".logo img{height:72px;width:auto}"
        "h1{font-size:1.3em;color:#44ff44;margin:0 0 2px;text-align:center;"
        "letter-spacing:3px;text-transform:uppercase;"
        "text-shadow:0 0 8px #33ff33}"
        ".sub{text-align:center;font-size:.75em;color:#44dd44;letter-spacing:2px;"
        "margin-bottom:12px}"
    );
    s_web.sendContent(
        "h2{font-size:.85em;color:#44ff44;margin:0 0 8px;letter-spacing:2px;"
        "text-transform:uppercase;border-bottom:1px solid #1a3a1a;padding-bottom:4px}"
        ".s{background:#111;padding:12px;margin:10px 0;border-radius:4px;"
        "border:1px solid #1a3a1a}"
        ".send{border-color:#224422}"
        "label{display:block;margin-top:8px;font-size:.8em;color:#66ff66}"
    );
    s_web.sendContent(
        "input,select{width:100%;padding:6px;box-sizing:border-box;margin-top:3px;"
        "background:#0a0a0a;color:#33ff33;border:1px solid #224422;border-radius:3px;"
        "font-family:'Courier New',monospace;font-size:.9em}"
        "input:focus,select:focus{outline:none;border-color:#33ff33}"
        "input[type='color']{height:32px;padding:2px}"
    );
    s_web.sendContent(
        "button{margin-top:14px;padding:10px 24px;background:#1a3a1a;color:#33ff33;"
        "border:1px solid #33ff33;border-radius:4px;cursor:pointer;font-size:.9em;"
        "font-family:'Courier New',monospace;letter-spacing:2px;text-transform:uppercase}"
        "button:hover{background:#224422;text-shadow:0 0 6px #33ff33}"
        "nav{text-align:center;margin-bottom:14px;border-bottom:1px solid #1a3a1a;"
        "padding-bottom:10px}"
        "nav a{margin:0 10px;color:#44dd44;text-decoration:none;font-size:.8em;"
        "letter-spacing:2px;text-transform:uppercase}"
        "nav a:hover{color:#66ff66}"
    );
    s_web.sendContent(
        "#lb{font-family:'Courier New',monospace;background:#000;color:#33ff33;"
        "padding:10px;height:360px;overflow-y:auto;white-space:pre-wrap;font-size:.8em;"
        "border:1px solid #1a3a1a;border-radius:4px}"
        "</style></head><body>"
        "<div class='logo'>"
        "<img src='https://nepamesh.com/content/images/2026/04/smallernepamesh-3.png'"
        " alt='NEPAMesh' onerror='this.style.display=\"none\"'>"
        "</div>"
        "<h1>NEPAMesh Sign</h1>"
        "<div class='sub'>LED Matrix Display</div>"
        "<nav><a href='/'>Settings</a><a href='/log'>Log</a><a href='/ota'>OTA</a></nav>"
    );
    (void)title;
}

static void sf(const char *fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s_web.sendContent(buf);
}

static void handle_root() {
    s_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_web.send(200, "text/html", "");
    web_send_head("Settings");

    // Direct message send
    s_web.sendContent("<div class='s send'><h2>&#9654; Send Message</h2>"
        "<form method='POST' action='/sendmsg'>"
        "<label>Message<input name='msg' placeholder='Text to display on screen'></label>"
        "<button type='submit'>Send to Screen</button>"
        "</form></div>");

    s_web.sendContent("<form method='POST' action='/save'>");

    // WiFi
    s_web.sendContent("<div class='s'><h2>WiFi</h2>");
    sf("<label>SSID<input name='ws' value='%s'></label>", s_wifi_ssid);
    sf("<label>Password<input name='wp' type='password' value='%s'></label>", s_wifi_pass);
    s_web.sendContent("</div>");

    // MQTT
    s_web.sendContent("<div class='s'><h2>MQTT</h2>");
    sf("<label>Host (IP or hostname)<input name='mh' value='%s'></label>", s_mqtt_host);
    sf("<label>Port<input name='mp' type='number' value='%u'></label>", s_mqtt_port);
    sf("<label>User<input name='mu' value='%s'></label>", s_mqtt_user);
    sf("<label>Password<input name='mpw' type='password' value='%s'></label>", s_mqtt_pass);
    sf("<label>Subscription topic<input name='mt' value='%s'></label>", s_mqtt_topic);
    s_web.sendContent("</div>");

    // Display
    s_web.sendContent("<div class='s'><h2>Display</h2>");
    sf("<label>Connector side<select name='cr'>"
       "<option value='1'%s>Right</option><option value='0'%s>Left</option>"
       "</select></label>",
       s_conn_right ? " selected" : "", !s_conn_right ? " selected" : "");
    sf("<label>Flip Y<select name='fy'>"
       "<option value='0'%s>Normal</option><option value='1'%s>Flipped</option>"
       "</select></label>",
       !s_flip_y ? " selected" : "", s_flip_y ? " selected" : "");
    sf("<label>Brightness (1-255)<input name='br' type='number' min='1' max='255' value='%u'></label>", s_brightness);
    sf("<label>Scroll speed ms<input name='sm' type='number' min='10' max='500' value='%u'></label>", s_scroll_ms);
    sf("<label>Message repeat count<input name='rc' type='number' min='1' max='10' value='%u'></label>", s_repeat_count);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", s_led_r, s_led_g, s_led_b);
    sf("<label>Text color<input name='col' type='color' value='%s'></label>", hex);
    sf("<label>Text effect<select name='fx'>"
       "<option value='0'%s>None</option>"
       "<option value='1'%s>Rainbow</option>"
       "<option value='2'%s>Cycle</option>"
       "<option value='3'%s>Gradient (text color → end color)</option>"
       "</select></label>",
       s_effect==0?" selected":"", s_effect==1?" selected":"",
       s_effect==2?" selected":"", s_effect==3?" selected":"");
    char hex2[8];
    snprintf(hex2, sizeof(hex2), "#%02x%02x%02x", s_grad_r, s_grad_g, s_grad_b);
    sf("<label>Gradient end color<input name='gcol' type='color' value='%s'></label>", hex2);
    sf("<label>Idle display<select name='im'>"
       "<option value='0'%s>Clock</option>"
       "<option value='1'%s>Fire</option>"
       "<option value='2'%s>Matrix Rain</option>"
       "<option value='3'%s>Scroll message (loop)</option>"
       "<option value='4'%s>Twinkle</option>"
       "<option value='5'%s>Off</option>"
       "</select></label>",
       s_idle_mode==0?" selected":"", s_idle_mode==1?" selected":"",
       s_idle_mode==2?" selected":"", s_idle_mode==3?" selected":"",
       s_idle_mode==4?" selected":"", s_idle_mode==5?" selected":"");
    s_web.sendContent("</div>");

    // Clock
    s_web.sendContent("<div class='s'><h2>Clock</h2>");
    sf("<label>UTC offset (hours, e.g. -4 for EDT, -5 for EST)"
       "<input name='tz' type='number' min='-12' max='14' value='%d'></label>", s_utc_offset);
    sf("<label>Custom message (empty to disable)"
       "<input name='hm' value='%s'></label>", s_hourly_msg);
    sf("<label>Custom message interval (minutes, 0 to disable)"
       "<input name='miv' type='number' min='0' max='60' value='%u'></label>", s_msg_interval);
    sf("<label>Date display interval (minutes, 0 to disable)"
       "<input name='div' type='number' min='0' max='60' value='%u'></label>", s_date_interval);
    s_web.sendContent("</div>");

    s_web.sendContent("<button type='submit'>Save &amp; Restart</button></form></body></html>");
}

static void handle_save() {
    if (s_web.hasArg("ws"))  strncpy(s_wifi_ssid,  s_web.arg("ws").c_str(),  sizeof(s_wifi_ssid)  - 1);
    if (s_web.hasArg("wp"))  strncpy(s_wifi_pass,  s_web.arg("wp").c_str(),  sizeof(s_wifi_pass)  - 1);
    if (s_web.hasArg("mh"))  strncpy(s_mqtt_host,  s_web.arg("mh").c_str(),  sizeof(s_mqtt_host)  - 1);
    if (s_web.hasArg("mp"))  s_mqtt_port  = s_web.arg("mp").toInt();
    if (s_web.hasArg("mu"))  strncpy(s_mqtt_user,  s_web.arg("mu").c_str(),  sizeof(s_mqtt_user)  - 1);
    if (s_web.hasArg("mpw")) strncpy(s_mqtt_pass,  s_web.arg("mpw").c_str(), sizeof(s_mqtt_pass)  - 1);
    if (s_web.hasArg("mt"))  strncpy(s_mqtt_topic, s_web.arg("mt").c_str(),  sizeof(s_mqtt_topic) - 1);
    if (s_web.hasArg("cr"))  s_conn_right = s_web.arg("cr").toInt();
    if (s_web.hasArg("fy"))  s_flip_y     = s_web.arg("fy").toInt();
    if (s_web.hasArg("br"))  s_brightness = s_web.arg("br").toInt();
    if (s_web.hasArg("sm"))  s_scroll_ms    = s_web.arg("sm").toInt();
    if (s_web.hasArg("rc"))  s_repeat_count = s_web.arg("rc").toInt();
    if (s_web.hasArg("col")) {
        String h = s_web.arg("col");
        if (h.length() == 7 && h[0] == '#') {
            s_led_r = strtol(h.substring(1, 3).c_str(), nullptr, 16);
            s_led_g = strtol(h.substring(3, 5).c_str(), nullptr, 16);
            s_led_b = strtol(h.substring(5, 7).c_str(), nullptr, 16);
        }
    }
    if (s_web.hasArg("fx"))  s_effect = s_web.arg("fx").toInt();
    if (s_web.hasArg("gcol")) {
        String h = s_web.arg("gcol");
        if (h.length() == 7 && h[0] == '#') {
            s_grad_r = strtol(h.substring(1,3).c_str(), nullptr, 16);
            s_grad_g = strtol(h.substring(3,5).c_str(), nullptr, 16);
            s_grad_b = strtol(h.substring(5,7).c_str(), nullptr, 16);
        }
    }
    if (s_web.hasArg("im"))  s_idle_mode     = s_web.arg("im").toInt();
    if (s_web.hasArg("tz"))  s_utc_offset    = (int8_t)s_web.arg("tz").toInt();
    if (s_web.hasArg("hm"))  strncpy(s_hourly_msg, s_web.arg("hm").c_str(), sizeof(s_hourly_msg) - 1);
    if (s_web.hasArg("miv")) s_msg_interval  = s_web.arg("miv").toInt();
    if (s_web.hasArg("div")) s_date_interval = s_web.arg("div").toInt();
    save_settings();
    s_web.send(200, "text/html",
        "<html><body><h2>Saved. Restarting...</h2>"
        "<script>setTimeout(()=>location.href='/',4000)</script></body></html>");
    delay(500);
    ESP.restart();
}

static void handle_log_page() {
    s_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_web.send(200, "text/html", "");
    web_send_head("Log");
    s_web.sendContent(
        "<div id='lb'></div>"
        "<script>"
        "function r(){"
        "fetch('/log/data').then(x=>x.json()).then(d=>{"
        "var e=document.getElementById('lb');"
        "e.textContent=d.join('\\n');"
        "e.scrollTop=e.scrollHeight;"
        "})}"
        "r();setInterval(r,1000);"
        "</script>"
        "</body></html>");
}

static void handle_log_data() {
    // Return a JSON array of the last LOG_LINES log lines
    String j = "[";
    int count = s_log_count < LOG_LINES ? s_log_count : LOG_LINES;
    int start = (s_log_tail - count + LOG_LINES) % LOG_LINES;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % LOG_LINES;
        if (i > 0) j += ',';
        j += '"';
        // escape special chars
        for (const char *c = s_log_buf[idx]; *c; c++) {
            if (*c == '"') j += "\\\"";
            else if (*c == '\\') j += "\\\\";
            else j += *c;
        }
        j += '"';
    }
    j += ']';
    s_web.send(200, "application/json", j);
}

static void handle_ota_page() {
    s_web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_web.send(200, "text/html", "");
    web_send_head("OTA");
    s_web.sendContent(
        "<div class='s'>"
        "<h2>Firmware Upload</h2>"
        "<form method='POST' action='/ota' enctype='multipart/form-data'>"
        "<input type='file' name='f' accept='.bin' style='margin-bottom:10px'><br>"
        "<button type='submit'>Upload &amp; Restart</button>"
        "</form></div></body></html>");
}

static void handle_ota_upload() {
    HTTPUpload &up = s_web.upload();
    if (up.status == UPLOAD_FILE_START) {
        ulog("[OTA] web upload start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            ulog("[OTA] begin failed\n");
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize)
            ulog("[OTA] write error\n");
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) ulog("[OTA] done %u bytes\n", up.totalSize);
        else                  ulog("[OTA] end failed\n");
    }
}

static void handle_send_msg() {
    if (s_web.hasArg("msg") && s_web.arg("msg").length() > 0) {
        char msg[MSG_LEN];
        strncpy(msg, s_web.arg("msg").c_str(), MSG_LEN - 1);
        msg[MSG_LEN - 1] = 0;
        for (char *c = msg; *c; c++)
            if ((uint8_t)*c < 32 && *c != '\t') *c = ' '; // strip ctrl chars; UTF-8 high bytes pass through
        // jump the queue — start scrolling immediately
        s_active = false;
        s_cur_repeats = 0;
        strncpy(s_cur_msg, msg, MSG_LEN - 1);
        enqueue(msg);
        ulog("[MSG] web direct: %s\n", msg);
    }
    s_web.sendHeader("Location", "/");
    s_web.send(302, "text/plain", "");
}

static void setup_webserver() {
    s_web.on("/",          HTTP_GET,  handle_root);
    s_web.on("/save",      HTTP_POST, handle_save);
    s_web.on("/sendmsg",   HTTP_POST, handle_send_msg);
    s_web.on("/log",       HTTP_GET,  handle_log_page);
    s_web.on("/log/data",  HTTP_GET,  handle_log_data);
    s_web.on("/ota",       HTTP_GET,  handle_ota_page);
    s_web.on("/ota", HTTP_POST,
        []() {
            s_web.send(200, "text/html",
                Update.hasError()
                ? "<html><body><h2>Upload FAILED</h2><a href='/ota'>Try again</a></body></html>"
                : "<html><body><h2>Upload OK. Restarting...</h2></body></html>");
            delay(500);
            ESP.restart();
        },
        handle_ota_upload);
    // Captive portal — redirect all unrecognised URLs to settings page
    s_web.onNotFound([]() {
        s_web.sendHeader("Location", "http://192.168.4.1/");
        s_web.send(302, "text/plain", "");
    });
    s_web.begin();
    if (s_ap_mode)
        ulog("AP Web: http://%s\n", WiFi.softAPIP().toString().c_str());
    else
        ulog("Web: http://%s\n", WiFi.localIP().toString().c_str());
}

// ============================================================
// Boot LED test
// ============================================================
static void boot_test() {
    const CRGB colors[3] = { CRGB::Red, CRGB::Blue, CRGB::Green };
    const int TAIL = 10;
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < NUM_LEDS + TAIL; i++) {
            FastLED.clear();
            for (int t = 0; t < TAIL; t++) {
                int idx = i - t;
                if (idx >= 0 && idx < NUM_LEDS) {
                    leds[idx] = colors[c];
                    leds[idx].nscale8(255 - (t * 255 / TAIL));
                }
            }
            FastLED.show();
            delay(8);
        }
    }
    FastLED.clear(true);
}

// ============================================================
// setup / loop
// ============================================================
void setup() {
    Serial.begin(115200);

    load_settings();

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(s_brightness);
    FastLED.clear(true);

    boot_test();

    wifi_start();
    s_log_udp.begin(4211);

    if (!s_ap_mode) {
        ArduinoOTA.setHostname("nepamesh-sign");
        ArduinoOTA.setPassword("nepamesh");
        ArduinoOTA.begin();
    }

    setup_webserver();

    if (!s_ap_mode) {
        s_wifi.setTimeout(5000);

        int a, b, c, d;
        if (sscanf(s_mqtt_host, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
            s_mqtt.setServer(IPAddress(a, b, c, d), s_mqtt_port);
        } else {
            IPAddress resolved;
            if (simple_dns_resolve(s_mqtt_host, resolved)) {
                ulog("[DNS] %s -> %s\n", s_mqtt_host, resolved.toString().c_str());
                s_mqtt.setServer(resolved, s_mqtt_port);
            } else {
                ulog("[DNS] FAILED to resolve %s, trying anyway\n", s_mqtt_host);
                s_mqtt.setServer(s_mqtt_host, s_mqtt_port);
            }
        }

        s_mqtt.setKeepAlive(10);
        s_mqtt.setCallback(on_message);
        s_mqtt.setBufferSize(1024);

        mqtt_check();
    }

    start_scroll(s_ap_mode ? "Connect to: sign" : "NEPAMesh");
}

void loop() {
    if (s_ap_mode) {
        s_dns.processNextRequest();
    } else {
        wifi_check();
        ArduinoOTA.handle();
        mqtt_check();
        s_mqtt.loop();
        ntp_poll();
    }
    s_web.handleClient();

    static bool s_prev_active = false;
    update_display();
    if (s_prev_active && !s_active) s_last_clock_min = 255;  // any scroll just finished — refresh clock
    s_prev_active = s_active;
    update_clock();

    if (!s_active) {
        if (s_cur_repeats > 0 && s_cur_repeats < s_repeat_count) {
            // scroll current message again
            s_cur_repeats++;
            start_scroll(s_cur_msg);
        } else {
            char msg[MSG_LEN];
            if (dequeue(msg)) {
                strncpy(s_cur_msg, msg, MSG_LEN - 1);
                s_cur_repeats = 1;
                start_scroll(s_cur_msg);
            } else {
                s_cur_repeats = 0;
                if (s_idle_mode == 3 && s_hourly_msg[0])
                    start_scroll(s_hourly_msg);
                else
                    update_idle_animation();
            }
        }
    }
}
