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

// ============================================================
// Log ring buffer + UDP broadcast
// ============================================================
#define LOG_PORT     4210
#define LOG_LINES    48
#define LOG_LINE_LEN 120
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
#define SCROLL_BUF_COLS 640
static uint8_t  s_buf[SCROLL_BUF_COLS];
static int      s_width   = 0;
static int      s_pos     = 0;
static uint32_t s_last_ms = 0;
static bool     s_active  = false;

static int render_text(const char *text, uint8_t *buf, int max_cols) {
    int col = 0;
    buf[col++] = 0; buf[col++] = 0;
    for (const char *p = text; *p && col < max_cols - 6; p++) {
        uint8_t c = (uint8_t)*p;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = font5x7[c - 32];
        for (int i = 0; i < 5; i++) buf[col++] = g[i];
        buf[col++] = 0;
    }
    for (int i = 0; i < MATRIX_W && col < max_cols; i++) buf[col++] = 0;
    return col;
}

static void start_scroll(const char *text) {
    ulog("[DISP] scroll: %s\n", text);
    s_width   = render_text(text, s_buf, SCROLL_BUF_COLS);
    s_pos     = 0;
    s_active  = true;
    s_last_ms = millis();
}

static void update_display() {
    if (!s_active) return;
    if (millis() - s_last_ms < (uint32_t)s_scroll_ms) return;
    s_last_ms = millis();

    for (int x = 0; x < MATRIX_W; x++) {
        int src = s_pos + x;
        uint8_t col_bits = (src < s_width) ? s_buf[src] : 0;
        for (int y = 0; y < MATRIX_H; y++) {
            uint16_t idx = XY(x, y);
            if (idx < NUM_LEDS)
                leds[idx] = (col_bits & (1 << y)) ? CRGB(0, 200, 0) : CRGB::Black;
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
#define MSG_LEN 128
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
            if ((uint8_t)*c < 32 || (uint8_t)*c > 126) *c = '?';

        enqueue(msg);
        ulog("[MSG] enqueued: %s\n", msg);

    } else if (portnum == 4) {
        char sname[5] = {};
        if (parse_short_name(payload_bytes, payload_len, sname) && sname[0]) {
            cache_node(from_node, sname);
            ulog("[NODE] !%08X -> %s\n", (unsigned)from_node, sname);
        }
    }
}

// ============================================================
// WiFi / MQTT
// ============================================================
static WiFiClient   s_wifi;
static PubSubClient s_mqtt(s_wifi);
static DNSServer    s_dns;
static bool         s_ap_mode = false;

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
// Web server
// ============================================================
static WebServer s_web(80);

static void web_send_head(const char *title) {
    s_web.sendContent(
        "<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NEPAMesh Sign</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:520px;margin:16px auto;padding:0 12px}"
        "h1{font-size:1.3em;margin-bottom:4px}h2{font-size:1em;margin:0 0 6px}"
        ".s{background:#f5f5f5;padding:10px;margin:10px 0;border-radius:4px}"
        "label{display:block;margin-top:8px;font-size:.9em;color:#444}"
        "input,select{width:100%;padding:5px;box-sizing:border-box;margin-top:2px}"
        "button{margin-top:14px;padding:10px 24px;background:#1a73e8;color:#fff;"
        "border:none;border-radius:4px;cursor:pointer;font-size:1em}"
        "nav{margin-bottom:10px}nav a{margin-right:12px;color:#1a73e8}"
        "#lb{font-family:monospace;background:#111;color:#0f0;padding:8px;"
        "height:360px;overflow-y:auto;white-space:pre-wrap;font-size:.8em}"
        "</style></head><body>"
        "<h1>NEPAMesh Sign</h1>"
        "<nav><a href='/'>Settings</a><a href='/log'>Log</a><a href='/ota'>OTA</a></nav>"
    );
    (void)title;
}

static void sf(const char *fmt, ...) {
    char buf[256];
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
    s_web.sendContent("<div class='s'><h2>Send Message</h2>"
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

static char s_cur_msg[MSG_LEN] = {};
static int  s_cur_repeats      = 0;

static void handle_send_msg() {
    if (s_web.hasArg("msg") && s_web.arg("msg").length() > 0) {
        char msg[MSG_LEN];
        strncpy(msg, s_web.arg("msg").c_str(), MSG_LEN - 1);
        msg[MSG_LEN - 1] = 0;
        for (char *c = msg; *c; c++)
            if ((uint8_t)*c < 32 || (uint8_t)*c > 126) *c = '?';
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
        if (sscanf(s_mqtt_host, "%d.%d.%d.%d", &a, &b, &c, &d) == 4)
            s_mqtt.setServer(IPAddress(a, b, c, d), s_mqtt_port);
        else
            s_mqtt.setServer(s_mqtt_host, s_mqtt_port);

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
    }
    s_web.handleClient();

    update_display();

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
            }
        }
    }
}
