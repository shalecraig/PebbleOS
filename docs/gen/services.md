# Services Layer Architecture

The services layer provides high-level OS functionality including Bluetooth connectivity, health tracking, notifications, storage, and analytics.

## Service Categories

```
src/fw/services/
├── common/              # Core services (clock, battery, timers)
├── normal/              # Full-operation services
│   ├── activity/       # Health & activity tracking
│   ├── blob_db/        # Structured data storage
│   ├── notifications/  # Notification management
│   ├── timeline/       # Timeline system
│   └── ...
├── bluetooth/           # Bluetooth services
├── analytics/           # Telemetry & crash reporting
└── comm_session/        # Communication protocols
```

## Runlevel System

Services are controlled via runlevels:

| Level | Name | Purpose |
|-------|------|---------|
| 0 | BareMinimum | Minimal functionality |
| 1 | LowPower | Reduced power operation |
| 2 | Stationary | Device is stationary |
| 3 | FirmwareUpdate | Firmware update mode |
| 4 | Normal | Full operation |

Services implement `service_set_enabled(bool)` called by the runlevel system.

## Core Services

### Clock Service

**Location**: `src/fw/services/common/clock.c`

```c
time_t clock_get_time(void);
void clock_get_timezone(int16_t *utc_offset_minutes, char *tz_name);
void clock_get_time_string(char *buffer, size_t len, bool is_24h);
```

Features:
- Wall clock with timezone support
- DST handling
- Friendly date/time strings ("Today", "5 minutes ago")

### Battery Service

**Location**: `src/fw/services/common/battery/`

```c
BatteryChargeState battery_get_charge_state(void);  // percent, charging, plugged
```

- Low power mode detection
- Critical battery lockout
- Runlevel integration

### Timer Services

| Service | Purpose |
|---------|---------|
| `new_timer` | High-priority interrupt-driven |
| `evented_timer` | Event-based, lower priority |
| `regular_timer` | Standard task timers |
| `cron` | Wall-clock scheduled jobs |

### Event Service

**Location**: `src/fw/services/common/event_service.h`

```c
void event_service_subscribe(PebbleEventType type, EventServiceCallback callback);
void event_service_unsubscribe(PebbleEventType type);
```

Global event dispatch for apps.

## Health & Activity Services

**Location**: `src/fw/services/normal/activity/`

### Activity Tracking

```c
uint32_t activity_get_metric(ActivityMetric metric, time_t start, time_t end);
```

Metrics:
- Steps, distance, calories (active/resting)
- Active seconds
- Sleep (total, restful, entry/exit times)
- Heart rate (BPM, quality, zones)

### Activity Types

- Sleep, RestfulSleep, Nap
- Walk, Run, Bike, Sport

### HRM Manager

**Location**: `src/fw/services/common/hrm/`

```c
void hrm_manager_enable_app_monitoring(bool enable);
HRMSessionRef hrm_manager_app_subscribe(void);
```

## Notification Services

**Location**: `src/fw/services/normal/notifications/`

### Notification Storage

```c
void notification_storage_store(const Uuid *id, const uint8_t *data, size_t len);
bool notification_storage_get(const Uuid *id, uint8_t **data, size_t *len);
void notification_storage_remove(const Uuid *id);
void notification_storage_iterate(NotificationIteratorCallback callback, void *context);
```

Flash-based persistence with timeline integration.

### Alerts & DND

```c
void alerts_set_enabled(bool enabled);
bool do_not_disturb_is_active(void);
```

### ANCS Integration

Apple Notification Center Service for iOS:
- Match ANCS UID to internal notification ID
- Handle notification actions

## Bluetooth Services

**Location**: `src/fw/services/common/bluetooth/`

### Bluetooth Control

```c
void bt_ctl_set_enabled(bool enabled);
bool bt_ctl_is_bluetooth_running(void);
void bt_ctl_reset_stack(void);
```

### Persistent Storage

```c
void bt_persistent_storage_store_bonding(const BleBonding *bonding);
BleBonding *bt_persistent_storage_get_bonding(BTBondingID id);
```

Stores pairing/bonding information (~65KB).

### BLE Services

| Service | UUID | Purpose |
|---------|------|---------|
| Heart Rate | 0x180D | HRM data |
| Battery | 0x180F | Battery level |
| Device Info | 0x180A | Device metadata |
| Pebble Pairing | 0xFED9 | Connection management |

## Storage Services

### Persist Service

**Location**: `src/fw/services/normal/persist.h`

```c
int persist_write_data(const uint32_t key, const void *data, size_t size);
int persist_read_data(const uint32_t key, void *buffer, size_t size);
bool persist_exists(const uint32_t key);
int persist_delete(const uint32_t key);
```

Per-app key-value store (max 6KB per app).

### Blob Database

**Location**: `src/fw/services/normal/blob_db/`

Specialized databases:
- `app_db` - App installation metadata
- `pin_db` - Timeline pins
- `reminder_db` - Reminders
- `weather_db` - Weather data
- `contacts_db` - Contact information
- `health_db` - Health/activity data

```c
BlobDBStatus blob_db_insert(BlobDB *db, const uint8_t *key, size_t key_len,
                            const uint8_t *value, size_t value_len);
BlobDBStatus blob_db_get(BlobDB *db, const uint8_t *key, size_t key_len,
                         uint8_t **value, size_t *value_len);
```

### Data Logging

**Location**: `src/fw/services/normal/data_logging/`

```c
DataLoggingSessionRef dls_create(uint32_t tag, DataLoggingItemType type,
                                  uint16_t item_size, bool buffered);
DataLoggingResult dls_log(DataLoggingSessionRef session, const void *data, uint32_t count);
void dls_finish(DataLoggingSessionRef session);
```

Buffered async transmission to phone.

### File System (PFS)

**Location**: `src/fw/services/common/filesystem/`

```c
int pfs_open(const char *path, uint32_t flags);
int pfs_read(int fd, void *buffer, size_t len);
int pfs_write(int fd, const void *data, size_t len);
int pfs_close(int fd);
```

Wear-leveling flash filesystem.

## Analytics

**Location**: `src/fw/services/common/analytics/`

### Metrics Collection

```c
void analytics_set(AnalyticsMetric metric, uint32_t value, AnalyticsClient client);
void analytics_inc(AnalyticsMetric metric, AnalyticsClient client);
void analytics_max(AnalyticsMetric metric, uint32_t value, AnalyticsClient client);
```

### Metric Types

- Scalars (set, increment, max)
- Arrays (histograms, distributions)
- Stopwatches (time integration)
- Events (discrete occurrences)

### Heartbeat System

Periodic metric dumps for:
- Device health
- App performance
- Crash reporting (via Memfault)

## Communication Session

**Location**: `src/fw/services/common/comm_session/`

### Session Management

```c
CommSession *comm_session_get_system_session(void);
void comm_session_send_data(CommSession *session, uint16_t endpoint,
                            const uint8_t *data, size_t len);
```

Abstracts transport layer (iAP, SPP, BLE, QEMU).

### Capabilities

Sessions advertise capabilities:
- RunState, ExtendedMusic, ExtendedNotifications
- ActivityInsights, VoiceAPI, InfiniteLogDumping

## Service Initialization

```c
// Early boot
void services_early_init(void);

// After drivers
void services_common_init(void);  // Core services
void services_normal_init(void);  // Full-operation services
```

Order matters - services declare dependencies.

## Communication Patterns

1. **Synchronous RPC**: Direct function calls
2. **Async Events**: Event service dispatch
3. **Queue-based**: App inbox/outbox
4. **Callback-based**: Timers, accelerometer
5. **Data Logging**: Buffered transmission
6. **Database**: Mutex-protected access
