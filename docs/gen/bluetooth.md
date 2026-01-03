# Bluetooth & Communications

The Bluetooth subsystem provides BLE connectivity using the NimBLE stack, with PPoGATT for reliable app messaging and integration with iOS services (ANCS/AMS).

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Applications & Kernel                                      │
│  (ble_client, ble_service, app messaging)                  │
├─────────────────────────────────────────────────────────────┤
│  High-Level Services                                        │
│  (PPoGATT, ANCS, AMS, Pebble Pairing Service)              │
├─────────────────────────────────────────────────────────────┤
│  Communication Session Layer (/src/fw/comm/)               │
├─────────────────────────────────────────────────────────────┤
│  Bluetooth Firmware (/src/bluetooth-fw/nimble/)            │
├─────────────────────────────────────────────────────────────┤
│  NimBLE Stack (HCI, L2CAP, ATT, GATT, GAP)                 │
└─────────────────────────────────────────────────────────────┘
```

## NimBLE Integration

**Location**: `src/bluetooth-fw/nimble/`

### Initialization

```c
void bt_driver_init(void) {
  bt_lock_init();
  nimble_port_init();
  nimble_store_init();

  // Create FreeRTOS tasks
  xTaskCreate(nimble_host_task, "NimbleHost", ...);
  xTaskCreate(nimble_ll_task, "NimbleLL", ...);  // If controller enabled
}

void bt_driver_start(BTDriverConfig *config) {
  nimble_start_host_task();
  // Register services: DIS, BAS, GAP, GATT
  // Initialize Pebble Pairing Service
}
```

### Task Structure

| Task | Priority | Purpose |
|------|----------|---------|
| NimbleHost | MAX - 2 | HCI, GATT/GAP state machines |
| NimbleLL | MAX - 1 | Link layer, PHY (if controller enabled) |

## GAP LE Operations

**Location**: `src/fw/comm/ble/`

### Connection Management

```c
// Track connection state
GAPLEConnection *gap_le_connection_by_addr(const BTDeviceAddress *addr);

// Connection intents (abstract requests to connect)
void gap_le_connect_create_intent(const BTDeviceInternal *device, ...);
void gap_le_connect_cancel_intent(GAPLEConnectIntent *intent);
```

### Advertising

```c
void gap_le_advert_start(GAPLEAdvertJob *job);
void gap_le_advert_stop(GAPLEAdvertJob *job);
```

Job-based advertising with configurable intervals and durations.

### Scanning

```c
void gap_le_scan_start(GAPLEScanConfig *config);
void gap_le_scan_stop(void);
```

Buffered results with duplicate filtering.

## GATT Client

**Location**: `src/fw/comm/ble/gatt_client_*.c`

### Service Discovery

```c
void gatt_client_discovery_discover_all(const BTDeviceInternal *device);
```

Discovers services, characteristics, and descriptors with retry logic.

### Operations

```c
void gatt_client_op_read(GATTClientOp *op, ...);
void gatt_client_op_write(GATTClientOp *op, ...);
void gatt_client_op_write_no_response(GATTClientOp *op, ...);
```

### Subscriptions

```c
void gatt_client_subscribe(GATTClientSubscription *sub, ...);
```

Handles notifications and indications from remote devices.

## PPoGATT Protocol

**Location**: `src/fw/comm/ble/kernel_le_client/ppogatt/`

Pebble Protocol over GATT - reliable messaging over unreliable GATT.

### Packet Format

```c
typedef struct PACKED {
  PPoGATTPacketType type:3;  // Data(0), Ack(1), Reset(2), Complete(3)
  uint8_t sn:5;              // Sequence number (0-31)
  uint8_t payload[];         // Variable length
} PPoGATTPacket;
```

### State Machine

```
StateDisconnected
  ↓
StateConnectedClosed (Reset exchange)
  ↓
StateConnectedOpen (Data exchange)
  ↓
StateDisconnected
```

### Flow Control

- **Window size**: Up to ~188 unacked packets (V1)
- **Timeout**: 5-6 seconds between retries
- **Max timeouts**: 2 consecutive before reset

### Data Flow

```
Phone writes PPoGATT characteristic
  → NimBLE receives notification
  → ppogatt.c validates sequence, buffers payload
  → Sends Ack packet
  → Opens CommSession to app
```

## Pebble Services

### Pebble Pairing Service (0xFED9)

| Characteristic | Purpose |
|---------------|---------|
| Connection Status | Notify: encryption, bonding, gateway info |
| Trigger Pairing | Write: initiate pairing |
| Connection Parameters | Read/Write: interval, latency, timeout |

### Supported Connection Parameters

Three response time states:
- `ResponseTimeMin` - Lowest latency
- `ResponseTimeMid` - Balanced
- `ResponseTimeMax` - Power saving

## iOS Integration

### ANCS (Apple Notification Center Service)

**Location**: `src/fw/comm/ble/kernel_le_client/ancs/`

```c
// Characteristics
Notification Source (notify) - New notifications
Data Source (notify)         - Notification details
Control Point (write)        - Perform actions
```

### AMS (Apple Media Service)

**Location**: `src/fw/comm/ble/kernel_le_client/ams/`

```c
// Characteristics
Remote Command (write)  - Control playback
Entity Update (notify)  - Media metadata
Entity Attribute (read) - Full attribute values
```

## Connection Lifecycle

```c
typedef struct GAPLEConnection {
  BTDeviceInternal device;           // Address
  bool is_encrypted;
  bool is_gateway;                   // Is phone?
  BTBondingID bonding_id;
  uint16_t gatt_mtu;
  GATTServiceNode *gatt_remote_services;
  BleConnectionParams conn_params;
} GAPLEConnection;
```

### Lifecycle

1. **GAP Connection**: `gap_le_connection_add()`
2. **GATT Connection**: MTU negotiation, service discovery
3. **App Operations**: Read/write/subscribe
4. **Disconnection**: `gap_le_connection_remove()`

## Bonding & Pairing

### Flow

```c
bt_driver_cb_pairing_confirm_handle_request()
  → Shows pairing dialog
  → User confirms/denies
  → bt_driver_pairing_confirm(ctx, is_confirmed)
  → bt_driver_cb_handle_create_bonding()
    → Store in persistent storage
    → Associate with connection
```

### Bonding Storage

```c
typedef struct BleBonding {
  SMPairingInfo pairing_info;
  bool is_gateway;
  bool should_pin_address;
  BTDeviceAddress pinned_address;
} BleBonding;
```

## App BLE Client API

**Location**: `src/fw/applib/bluetooth/`

### Service Discovery

```c
void ble_client_discover_services_and_characteristics(BTDevice device);
```

### Characteristic Operations

```c
BLECharacteristic ble_service_get_characteristic(BLEService service, size_t index);

void ble_characteristic_read(BLECharacteristic characteristic);
void ble_characteristic_write(BLECharacteristic characteristic,
                               const uint8_t *value, size_t length);
void ble_characteristic_subscribe(BLECharacteristic characteristic,
                                   BLESubscriptionType type);
```

### Handlers

```c
typedef struct {
  BLEClientServiceChangeHandler services;
  BLEClientReadHandler read;
  BLEClientWriteHandler write;
  BLEClientSubscribeHandler subscribe;
} BLEClientHandlers;
```

## Communication Session

**Location**: `src/fw/services/common/comm_session/`

### Session Types

- **System Session**: Firmware-level messages
- **App Session**: Application-specific messages

### Capabilities

Sessions advertise capabilities:
- RunState, ExtendedMusic, ExtendedNotifications
- ActivityInsights, VoiceAPI

## Thread Safety

**Critical Rule**: `bt_lock()` must be held for:
- Accessing GAPLEConnection structures
- GATT operations
- Bonding information

```c
bt_lock();
{
  GAPLEConnection *conn = gap_le_connection_by_addr(&addr);
  // Safe to access
}
bt_unlock();
```

## Key Constraints

| Parameter | Value |
|-----------|-------|
| MTU minimum | 23 bytes |
| PPoGATT window (V0) | 4 packets |
| PPoGATT timeout | 5-6 seconds |
| Max bondings | Dozens (storage limited) |
| Advertising interval | Min 20ms |
