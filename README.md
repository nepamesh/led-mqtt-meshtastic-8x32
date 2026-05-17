# led-mqtt-meshtastic-8x32

ESP32 firmware that subscribes to a Meshtastic MQTT broker and scrolls incoming text messages across an 8×32 WS2812B LED matrix. Each message is displayed as **NodeName: message text** in green scrolling text. Node short names are learned automatically from NodeInfo packets on the mesh.

---

## Features

- Decrypts Meshtastic packets using the default AES-128-CTR key (`AQ==` / LongFast channel)
- Parses Meshtastic protobuf wire format with full buffer-bounds validation — no external proto library required
- Caches node short names from NodeInfo packets; falls back to `!XXXX` node ID
- Serpentine XY mapping for standard 8×32 WS2812B panels
- Message queue holds up to 4 pending messages while one is scrolling
- Supports **ESP32** and **ESP32-C6** targets in a single `platformio.ini`

---

## Hardware

| Part | Notes |
|------|-------|
| ESP32 DevKit (38-pin) | Any standard variant |
| ESP32-C6-DevKitC-1 | Also supported, see below |
| WS2812B 8×32 LED matrix | e.g. SVFISHKK B0CY2R8FSL |
| 5 V supply, ≥ 3 A | Power the panel directly — not through the ESP32 |

### Wiring

```
ESP32                   8x32 Panel
──────                  ──────────
GPIO 5  ──────────────► DIN
GND     ──────────────► GND
                        5V  ◄──── 5V external supply (≥3A)

External supply GND ──► ESP32 GND  (shared ground)
```

> Add a 300–500 Ω resistor in series on the DIN line to reduce signal ringing.  
> Add a 100 µF capacitor across the panel's 5 V and GND pads to suppress power spikes.

---

## Configuration

Edit **`src/config.h`** before flashing:

```c
// WiFi — required
#define WIFI_SSID       "YourNetworkName"
#define WIFI_PASS       "YourPassword"

// MQTT broker — defaults point to the public Meshtastic server
#define MQTT_HOST       "mqtt.meshtastic.org"
#define MQTT_PORT       1883
#define MQTT_USER       "meshdev"
#define MQTT_PASS_STR   "large4cats"

// Topic region — change to match your node's region setting
// Common: US, EU_433, EU_868, ANZ, KR, TW, RU, IN, JP, CN, NZ_865
#define MQTT_TOPIC      "msh/US/2/e/#"

// Hardware
#define LED_PIN         5       // GPIO connected to panel DIN
#define BRIGHTNESS      40      // 0-255; keep ≤ 80 on USB power
#define SCROLL_MS       35      // ms per pixel step (lower = faster)
```

---

## Build & Flash

### Requirements

- [VS Code](https://code.visualstudio.com) with the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension

### Steps

1. Open the `led-mqtt-meshtastic-8x32/` folder in VS Code
2. Edit `src/config.h` with your WiFi credentials and MQTT region
3. Select the target environment from the PlatformIO toolbar:
   - `esp32dev` — standard ESP32 *(default)*
   - `esp32c6` — ESP32-C6-DevKitC-1
4. Click **Upload** (→ button in the bottom toolbar) or press `Ctrl+Alt+U`

### CLI

```bash
# ESP32
pio run -e esp32dev -t upload

# ESP32-C6
pio run -e esp32c6 -t upload
```

### Manual flash mode (if the port is not auto-detected)

1. Hold **BOOT** on the ESP32
2. Press and release **EN** (reset)
3. Release **BOOT**, then run the upload command

### Verify

Open the serial monitor at **115200 baud** (`Ctrl+Alt+S` in PlatformIO):

```
WiFi YourNetwork ... 192.168.x.x
MQTT OK
[NODE] !A1B2C3D4 -> W3XYZ
[MSG]  W3XYZ: Hello from the mesh
```

---

## ESP32-C6 Notes

The C6 uses a RISC-V core. Ensure your PlatformIO `espressif32` platform is **v6.4.0 or later**:

```bash
pio pkg update -g -p espressif32
```

GPIO5 is RMT-capable on the C6. If you use a different pin, verify it supports RMT output.

---

## Troubleshooting

**Display shows mirrored or reversed rows**  
The serpentine direction varies by panel manufacturer. In `src/main.cpp`, swap the condition in `XY()`:

```c
// If display looks wrong, change this line:
return (y & 1) ? (y * MATRIX_W + MATRIX_W - 1 - x) : (y * MATRIX_W + x);
// To:
return (y & 1) ? (y * MATRIX_W + x) : (y * MATRIX_W + MATRIX_W - 1 - x);
```

**Messages arrive but display as `?????`**  
Your channel uses a custom PSK. Replace the 16 bytes in `MESH_KEY[]` at the top of `src/main.cpp` with your channel's expanded AES key.

**Node shows as `!XXXX` instead of callsign**  
Short names are cached from NodeInfo packets. Ask the node operator to trigger one via the Meshtastic app (**Admin → Send NodeInfo**), or wait for their next automatic broadcast.

**MQTT disconnects frequently**  
The firmware generates a unique client ID from the ESP32 chip MAC address to avoid conflicts. If disconnects persist, check broker reachability and WiFi signal strength.

**Panel flickers or shows wrong colors**  
- Confirm GND is shared between the ESP32 and the panel's power supply
- Check resistor on DIN line (300–500 Ω recommended)
- Ensure power supply is rated ≥ 3 A

---

## File Reference

```
led-mqtt-meshtastic-8x32/
├── platformio.ini        Build config — esp32dev and esp32c6 environments
└── src/
    ├── config.h          WiFi, MQTT, pin, and display settings  ← edit this
    ├── font5x7.h         5×7 column-major ASCII glyph data (chars 32–126)
    └── main.cpp          All firmware logic
```

---

## Security

The protobuf parser validates every length-delimited field against the remaining buffer before advancing the read pointer. Malformed or malicious MQTT packets are dropped at the parser stage without causing out-of-bounds memory access. All display output is sanitised to printable ASCII before rendering.

---

## License

MIT
