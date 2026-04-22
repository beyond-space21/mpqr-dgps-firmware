## Project: ESP32 ↔ Mobile App Connectivity System (DJI-like Flow)

### Objective

Design a dual-channel communication system between an ESP32 device and a React Native mobile app, using:

* BLE (Bluetooth Low Energy) → discovery, pairing, provisioning
* Wi-Fi → high-bandwidth communication (streaming + file transfer + control)

The system must be robust, secure, and fast to reconnect.

---

# 🔗 CONNECTION FLOW (STRICT)

## Phase 1: BLE Advertising (Device Side)

* ESP32 advertises using BLE:

  * Device Name: "ESP32-DEVICE-XXXX"
  * Service UUID: custom (e.g., 0xFFF0)
  * Include:

    * device_id
    * firmware_version
    * capabilities bitmask

---

## Phase 2: BLE Scan (App Side)

* React Native app scans for BLE devices
* Filter by:

  * Service UUID
  * Device name prefix

---

## Phase 3: BLE Connection & Pairing

### BLE GATT Design:

Service: 0xFFF0

Characteristics:

* 0xFFF1 → Device Info (read)
* 0xFFF2 → Wi-Fi Credentials (write)
* 0xFFF3 → Control Commands (write)
* 0xFFF4 → Notifications (notify)

### Flow:

1. App connects to ESP32 via BLE
2. App reads device info
3. Secure pairing:

   * Use passkey OR numeric comparison
   * Store bonding keys on both sides

---

## Phase 4: Wi-Fi Provisioning via BLE

ESP32 sends:

* SSID (e.g., ESP32_CAM_XXXX)
* Password

App:

* Disconnects current Wi-Fi
* Connects to ESP32 SoftAP

---

## Phase 5: Wi-Fi Server Setup (ESP32)

ESP32 must run:

* HTTP server (port 80 or 8080)
* Optional WebSocket server
* Optional UDP stream (if streaming required)

---

## Phase 6: Communication Protocol

### Control API (HTTP REST)

Endpoints:

GET /device/info
POST /camera/start
POST /camera/stop
POST /settings

Response format:
JSON only

---

### Real-time Control (WebSocket)

* Bi-directional
* Used for:

  * live status updates
  * button commands
  * telemetry

---

### Streaming (Optional)

If camera/display:

* Use MJPEG over HTTP OR RTP/UDP
* Avoid heavy codecs (ESP32 limitation)

---

### File Transfer

* Endpoint:
  GET /files
  GET /files/{filename}

---

## Phase 7: Keepalive

* App sends ping every 3–5 seconds
* ESP32 resets timeout watchdog

---

## Phase 8: Reconnection Logic

* Store bonded BLE device
* On app launch:

  * Try BLE reconnect
  * If known → skip pairing
  * Auto-connect Wi-Fi

---

# 🔐 SECURITY

* BLE bonding required
* Wi-Fi must use WPA2
* Optional:

  * token-based auth for HTTP
  * session key exchange

---

# ⚙️ ESP-IDF IMPLEMENTATION REQUIREMENTS

### BLE:

* Use NimBLE (preferred over Bluedroid)
* Implement GATT server
* Handle bonding persistence

### Wi-Fi:

* SoftAP mode
* Configurable SSID/password

### Server:

* Use esp_http_server
* Optional: lwIP sockets for UDP

### Tasking:

* Separate FreeRTOS tasks:

  * BLE task
  * Wi-Fi task
  * server task

### Memory Optimization:

* Avoid large buffers
* Stream in chunks
* Use PSRAM if available

---

# 📱 REACT NATIVE IMPLEMENTATION REQUIREMENTS

### BLE:

* Use react-native-ble-plx
* Scan + connect + read/write characteristics

### Wi-Fi:

* Use native modules:

  * Android: WifiManager
  * iOS: NEHotspotConfiguration

### Networking:

* Axios / fetch for HTTP
* WebSocket API for real-time

### UI Flow:

1. Scan screen
2. Device selection
3. Pairing screen
4. Connecting screen
5. Control dashboard

---

# 🚀 PERFORMANCE TARGETS

* BLE discovery: < 2 sec
* Wi-Fi connect: < 5 sec
* Command latency: < 100 ms
* Reconnect time: < 3 sec

---

# ⚠️ CONSTRAINTS (IMPORTANT)

* ESP32 RAM is limited (~320KB usable)
* Avoid:

  * heavy encryption libraries
  * large JSON payloads
* Prefer:

  * compact binary where needed
  * chunked transfer

---

# 🧪 TEST CASES

* First-time pairing
* Reconnection after power cycle
* Wi-Fi drop recovery
* Multiple app reconnect attempts
* Wrong password handling

---

# 📦 DELIVERABLES

## ESP-IDF:

* BLE GATT server implementation
* Wi-Fi AP + HTTP server
* API handlers

## React Native:

* BLE manager
* Wi-Fi connector
* API client
* UI screens

---

# 🧩 EXTENSIONS (OPTIONAL)

* OTA updates over Wi-Fi
* Video streaming optimization
* Multi-device support
* Cloud relay mode

---
