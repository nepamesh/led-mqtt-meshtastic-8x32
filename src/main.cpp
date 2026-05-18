#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include "mbedtls/aes.h"
#include "config.h"
#include "font5x7.h"

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

// Serpentine XY mapping: even rows L→R, odd rows R→L
static uint16_t XY(int x, int y) {
    if (x < 0 || x >= MATRIX_W || y < 0 || y >= MATRIX_H) return NUM_LEDS;
    return (y & 1) ? (y * MATRIX_W + MATRIX_W - 1 - x)
                   : (y * MATRIX_W + x);
}

// ============================================================
// Scrolling text
// ============================================================
#define SCROLL_BUF_COLS 640
static uint8_t  s_buf[SCROLL_BUF_COLS]; // column bitmaps (bits 0-6 = rows 0-6)
static int      s_width   = 0;
static int      s_pos     = 0;
static uint32_t s_last_ms = 0;
static bool     s_active  = false;

static int render_text(const char *text, uint8_t *buf, int max_cols) {
    int col = 0;
    buf[col++] = 0; buf[col++] = 0; // leading blank
    for (const char *p = text; *p && col < max_cols - 6; p++) {
        uint8_t c = (uint8_t)*p;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = font5x7[c - 32];
        for (int i = 0; i < 5; i++) buf[col++] = g[i];
        buf[col++] = 0; // inter-character gap
    }
    // trailing blank so last char scrolls fully off
    for (int i = 0; i < MATRIX_W && col < max_cols; i++) buf[col++] = 0;
    return col;
}

static void start_scroll(const char *text) {
    s_width  = render_text(text, s_buf, SCROLL_BUF_COLS);
    s_pos    = 0;
    s_active = true;
    s_last_ms = millis();
}

static void update_display() {
    if (!s_active) return;
    if (millis() - s_last_ms < (uint32_t)SCROLL_MS) return;
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

    if (++s_pos >= s_width) {
        s_active = false;
        FastLED.clear(true);
    }
}

// ============================================================
// Node name cache  (short_name by node ID)
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
#define MSG_Q    4
#define MSG_LEN  128
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
        v |= (uint64_t)(b & 0x7F) << sh;
        sh += 7;
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
// Nonce layout (16 bytes):
//   [0..3]  packet_id  (uint32 LE)
//   [4..7]  0x00       (upper half of uint64 packet_id)
//   [8..11] from_node  (uint32 LE)
//   [12..15] 0x00
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
        size_t nc_off = 0;
        uint8_t sb[16] = {};
        ok = (mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce, sb, enc, out) == 0);
    }
    mbedtls_aes_free(&ctx);
    return ok;
}

// ============================================================
// Protobuf parsers for Meshtastic structures
// ============================================================

// ServiceEnvelope → locate the MeshPacket bytes (field 1)
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

// MeshPacket → from, id, encrypted bytes (field 8)
static bool parse_packet(const uint8_t *data, size_t len,
                          uint32_t &from, uint32_t &pkt_id,
                          const uint8_t *&enc, size_t &enc_len) {
    const uint8_t *p = data, *end = data + len;
    from = pkt_id = 0; enc = nullptr;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if      (field == 1 && wt == 5) pb_fixed32(p, end, from);
        else if (field == 6 && wt == 5) pb_fixed32(p, end, pkt_id);
        else if (field == 8 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            enc = p; enc_len = sz; p += sz;
        } else { pb_skip(p, end, wt); }
    }
    return enc != nullptr && enc_len > 0;
}

// Data → portnum (field 1), payload bytes (field 2)
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

// User proto → short_name (field 3)
static bool parse_short_name(const uint8_t *data, size_t len, char *out) {
    const uint8_t *p = data, *end = data + len;
    while (p < end) {
        uint64_t tag; if (!pb_varint(p, end, tag)) break;
        int field = tag >> 3, wt = tag & 7;
        if (field == 3 && wt == 2) {
            uint64_t sz; if (!pb_varint(p, end, sz)) return false;
            if (sz > (uint64_t)(end - p)) return false;
            size_t n = min((size_t)sz, (size_t)4);
            memcpy(out, p, n); out[n] = 0;
            return true;
        } else { pb_skip(p, end, wt); }
    }
    return false;
}

// ============================================================
// MQTT message handler
// ============================================================
static uint8_t s_plain[256];

static void on_message(char *topic, uint8_t *payload, unsigned int length) {
    if (length == 0) return;

    // 1. Unwrap ServiceEnvelope
    const uint8_t *pkt_data; size_t pkt_len;
    if (!parse_envelope(payload, length, pkt_data, pkt_len)) return;

    // 2. Parse MeshPacket
    uint32_t from_node, pkt_id;
    const uint8_t *enc; size_t enc_len;
    if (!parse_packet(pkt_data, pkt_len, from_node, pkt_id, enc, enc_len)) return;
    if (enc_len > sizeof(s_plain)) return;

    // 3. Decrypt
    if (!mesh_decrypt(enc, enc_len, s_plain, from_node, pkt_id)) return;

    // 4. Parse Data
    uint32_t portnum;
    const uint8_t *payload_bytes; size_t payload_len;
    if (!parse_data(s_plain, enc_len, portnum, payload_bytes, payload_len)) return;
    if (!payload_bytes || payload_len == 0) return;

    if (portnum == 1) {
        // TEXT_MESSAGE_APP
        const char *name = node_name(from_node);
        char prefix[20];
        if (name) snprintf(prefix, sizeof(prefix), "%s: ", name);
        else       snprintf(prefix, sizeof(prefix), "!%04X: ", (unsigned)(from_node & 0xFFFF));

        char msg[MSG_LEN];
        snprintf(msg, sizeof(msg), "%s", prefix);
        size_t plen = strnlen(prefix, sizeof(prefix));
        size_t room = sizeof(msg) - plen - 1;
        size_t tlen = (payload_len < room) ? payload_len : room;
        memcpy(msg + plen, payload_bytes, tlen);
        msg[plen + tlen] = 0;

        // Sanitise non-printable bytes
        for (char *c = msg; *c; c++)
            if ((uint8_t)*c < 32 || (uint8_t)*c > 126) *c = '?';

        enqueue(msg);
        Serial.printf("[MSG] %s\n", msg);

    } else if (portnum == 4) {
        // NODEINFO_APP – cache short_name for display
        char sname[5] = {};
        if (parse_short_name(payload_bytes, payload_len, sname) && sname[0]) {
            cache_node(from_node, sname);
            Serial.printf("[NODE] !%08X -> %s\n", (unsigned)from_node, sname);
        }
    }
}

// ============================================================
// WiFi / MQTT
// ============================================================
static WiFiClient  s_wifi;
static PubSubClient s_mqtt(s_wifi);

static void wifi_connect() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.printf("WiFi %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
}

static void mqtt_connect() {
    while (!s_mqtt.connected()) {
        Serial.print("MQTT ");
        // Use ESP32 MAC for a unique client ID
        char cid[24];
        snprintf(cid, sizeof(cid), "mesh-%llX", ESP.getEfuseMac());
        if (s_mqtt.connect(cid, MQTT_USER, MQTT_PASS_STR)) {
            Serial.println("OK");
            s_mqtt.subscribe(MQTT_TOPIC);
        } else {
            Serial.printf("err %d, retry 5s\n", s_mqtt.state());
            delay(5000);
        }
    }
}

// ============================================================
// Boot LED test — red, blue, green snake along the strip
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

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.clear(true);

    boot_test();

    wifi_connect();

    s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    s_mqtt.setCallback(on_message);
    s_mqtt.setBufferSize(512);

    mqtt_connect();
    start_scroll("NEPAMesh");
}

void loop() {
    wifi_connect();
    if (!s_mqtt.connected()) mqtt_connect();
    s_mqtt.loop();

    update_display();

    if (!s_active) {
        char msg[MSG_LEN];
        if (dequeue(msg)) start_scroll(msg);
    }
}
