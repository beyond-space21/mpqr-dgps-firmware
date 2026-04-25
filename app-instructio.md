# Mobile App Connection Prompt (DGPS Device)

Use this as the implementation prompt/spec for the mobile app team (React Native).

## Goal

Implement a DJI-style connection flow:

1. Discover DGPS device over BLE.
2. Send connection request with a 4-digit code.
3. User confirms the same code on device screen.
4. Device exposes temporary Wi-Fi SoftAP.
5. App receives temporary SSID/password via BLE notification.
6. App joins device Wi-Fi and uses HTTP/WebSocket over Wi-Fi.

## BLE Contract

- Device name prefix: `DGPS_` (or project-specific configured name).
- Service UUID: `0xFFF0` (custom service).
- Characteristics:
  - `0xFFF1` Device Info (read)
  - `0xFFF3` Control Command (write)
  - `0xFFF4` Notification (notify)

### Control Command payload (JSON UTF-8)

Write JSON to `0xFFF3`:

```json
{ "cmd": "pair_request", "code": "1234", "app_nonce": "A1B2C3D4" }
```

Optional status/ack:

```json
{ "cmd": "wifi_connected" }
```

### Notification payload from device (JSON UTF-8)

Code shown and waiting for user confirm:

```json
{ "evt": "pair_pending_confirm", "code": "1234" }
```

SoftAP ready:

```json
{
  "evt": "softap_ready",
  "ssid": "DGPS_ABCD",
  "password": "P12345678ABCD",
  "ip": "192.168.4.1",
  "port": 80
}
```

Failure:

```json
{ "evt": "error", "reason": "pair_rejected_or_timeout" }
```

## App Flow (React Native)

1. **Scan BLE**
   - Use `react-native-ble-plx`.
   - Filter by service UUID + name prefix.
2. **Connect + Discover Services**
   - Connect, discover all services and chars.
   - Subscribe to `0xFFF4` notifications immediately.
3. **Start Pair Request**
   - Generate random 4-digit code in app.
   - Write `pair_request` to `0xFFF3`.
   - UI state: "Confirm this code on device screen: ####".
4. **Wait for user action on device**
   - Wait for `pair_pending_confirm` then `softap_ready`.
   - Timeout after 90s and offer Retry.
5. **Join Wi-Fi**
   - Android: use `WifiManager` / `react-native-wifi-reborn`.
   - iOS: use `NEHotspotConfiguration`.
   - Connect to received SSID/password.
6. **Open Wi-Fi API session**
   - Base URL: `http://192.168.4.1` (fallback to mDNS later).
   - Start keepalive (`GET /device/info` every 3-5s or websocket ping).
7. **Persist reconnect metadata**
   - Store `device_id`, last BLE MAC/identifier, last SSID.
   - On next app launch: attempt BLE reconnect first, then fast Wi-Fi reconnect.

## Reconnect Strategy

- If BLE reconnect succeeds:
  - Request current connection state.
  - If SoftAP already active, reuse last credentials if still valid.
  - Else trigger fresh pair request flow.
- If Wi-Fi drops:
  - Retry Wi-Fi join up to 3 times with exponential backoff.
  - If still failing, return to BLE-assisted reconnect screen.

## Reliability Rules

- Always include app-level timeout + retry logic.
- BLE writes: retry up to 3 times for transient failures.
- Treat notifications as source-of-truth state transitions.
- Make all transitions idempotent in app state machine.

## Security Rules

- Require user-confirmed 4-digit code match before Wi-Fi join attempt.
- Never log full Wi-Fi password in production logs.
- Store reconnect metadata in secure storage where possible.

## Recommended App State Machine

- `IDLE`
- `SCANNING`
- `BLE_CONNECTING`
- `PAIR_REQUEST_SENT`
- `WAITING_USER_CONFIRM`
- `WAITING_SOFTAP`
- `WIFI_CONNECTING`
- `WIFI_CONNECTED`
- `SESSION_READY`
- `FAILED`

## QA Test Matrix

- First-time pairing success.
- Wrong code and rejected confirm.
- User does not confirm within timeout.
- BLE disconnect during pairing.
- Wi-Fi credentials received but join fails.
- Device reboot after successful pairing.
- App restart and fast reconnect.
