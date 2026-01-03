# Process Management

The process management subsystem handles app loading, execution, memory isolation, and lifecycle management.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  App Manager (launch, monitor, terminate)                   │
├─────────────────────────────────────────────────────────────┤
│  Process Manager (state machine, cleanup)                   │
├─────────────────────────────────────────────────────────────┤
│  Process Loader (binary loading, relocation)                │
├─────────────────────────────────────────────────────────────┤
│  Memory Segments (heap, stack, code)                        │
├─────────────────────────────────────────────────────────────┤
│  MPU (hardware memory protection)                           │
└─────────────────────────────────────────────────────────────┘
```

## App Types

### Storage Types

| Type | Location | Examples |
|------|----------|----------|
| `ProcessStorageBuiltin` | Firmware | Launcher, Settings |
| `ProcessStorageFlash` | SPI Flash | Third-party apps |
| `ProcessStorageResource` | System resources | Embedded apps |

### Process Types

- `ProcessTypeApp` - Foreground application
- `ProcessTypeWatchface` - Clock face
- `ProcessTypeWorker` - Background thread

## Memory Layout

### App Memory Region

```
APP_RAM
├── Stack Guard (256-1024 bytes)  ← MPU protected, triggers fault on overflow
├── Stack (2-8 KB)                ← Grows downward
├── App Code                      ← Loaded from flash
├── AppState (~2-4 KB)            ← UI state, services
└── Heap                          ← Dynamic allocation
```

### Memory Constraints by SDK

| SDK | Typical Size | Notes |
|-----|-------------|-------|
| 2.x | 60-80 KB | Legacy apps |
| 3.x | 100-120 KB | Enhanced graphics |
| 4.x | 100-120 KB | Current SDK |
| Rocky.js | Reduced | JS interpreter overhead |

## Memory Isolation

### Hardware Protection (MPU)

```c
// Third-party apps run unprivileged
mcu_state_set_thread_privilege(false);

// MPU regions per task:
// - App RAM: Read/Write for app task
// - Worker RAM: No access for app task
// - Kernel RAM: No access for unprivileged
```

### Syscall Boundary

Unprivileged apps must use syscall wrappers:
```c
// App code uses:
void *ptr = sys_malloc(size);

// NOT direct kernel calls:
// void *ptr = kernel_malloc(size);  // Would fault
```

## App Manager

**Location**: `src/fw/process_management/app_manager.c`

### Launch Flow

```c
app_manager_launch_new_app(AppLaunchConfig *config)
  → prv_app_switch()
    → prv_app_stop()           // Stop current app
    → prv_app_start()          // Start new app
      → memory_segment_split() // Allocate regions
      → process_loader_load()  // Load binary
      → pebble_task_create()   // Create FreeRTOS task
      → prv_app_task_main()    // Task entry point
```

### App Task Entry

```c
static void prv_app_task_main(void *params) {
  app_state_init();

  if (!is_privileged_app) {
    mcu_state_set_thread_privilege(false);  // Drop privilege
  }

  app_main();  // Call app's main()

  app_state_deinit();
}
```

### Event Queue

```c
// Max 32 pending events per app
static PebbleEvent s_to_app_event_queue[32];
```

Events delivered:
- Click events
- Timer callbacks
- Accelerometer data
- App messages
- Deinit/Kill events

## Process State Machine

### States

```c
typedef enum ProcessRunState {
  ProcessRunState_Running,           // App actively executing
  ProcessRunState_GracefullyClosing, // Deinit posted, waiting
  ProcessRunState_ForceClosing       // Force kill in progress
} ProcessRunState;
```

### Shutdown Flow

```
Running
    ↓ (user action or new app launch)
GracefullyClosing
    ↓ (PEBBLE_DEINIT_EVENT posted)
    ↓ (app calls sys_exit() or 3s timeout)
ForceClosing (if unresponsive)
    ↓ (check privilege level)
    ↓ (cleanup and kill)
Terminated
```

### Graceful vs Force Close

**Graceful**:
1. App receives `PEBBLE_DEINIT_EVENT`
2. App saves state, closes connections
3. App calls `sys_exit()`
4. Task suspended, cleanup performed

**Force** (after 3s timeout):
1. Check if in unprivileged code → safe to kill
2. If stuck in privileged code, wait for syscall exit
3. Kill task, perform cleanup

## Worker Management

**Location**: `src/fw/process_management/worker_manager.c`

### Worker Memory

```
WORKER_RAM
├── Stack Guard
├── Stack (1400 bytes)
├── Worker Code (11,648 bytes)
└── Heap
```

### Worker Lifecycle

```c
worker_manager_launch_new_worker_with_args(AppInstallId id, void *args)
  → Only one worker per app
  → Priority: tskIDLE + 1 (lower than app)
  → Crash tracking prevents relaunch loops
```

### Crash Prevention

```c
// Track crash count and timestamp
if (same worker crashes twice within timeout) {
  disable auto-relaunch;  // Prevent battery drain
}
```

## App Installation

### App Metadata

```c
struct PebbleProcessInfo {
  char header[8];              // "PBLAPP"
  Version sdk_version;         // SDK used to build
  Version process_version;     // App version
  uint16_t load_size;          // Binary size
  uint32_t crc;                // Code CRC
  char name[32];               // Display name
  Uuid uuid;                   // Unique identifier
  uint32_t flags;              // Visibility, type, features
};
```

### SDK Compatibility

```c
bool is_compatible = (
  entry->sdk_version.major == CURRENT_SDK_MAJOR &&
  entry->sdk_version.minor <= CURRENT_SDK_MINOR
);
```

### Visibility Modes

| Mode | Behavior |
|------|----------|
| `Shown` | Always visible in launcher |
| `Hidden` | Never visible |
| `ShownOnCommunication` | Visible if used in last 5 min |
| `QuickLaunch` | Only in quick launch menu |

## Process Context

```c
struct ProcessContext {
  const PebbleProcessMd *app_md;      // Metadata
  AppInstallId install_id;             // Installation ID
  void *task_handle;                   // FreeRTOS handle
  void *load_start, *load_end;         // Memory region
  void *to_process_event_queue;        // Event queue
  volatile bool safe_to_kill;          // Safe to terminate
  AppLaunchReason launch_reason;       // How launched
  ProcessRunState closing_state;       // Shutdown state
};
```

## Run Levels

Apps can specify minimum run level:

```c
typedef enum {
  ProcessAppRunLevelNormal,    // Default
  ProcessAppRunLevelSystem,    // Elevated
  ProcessAppRunLevelCritical   // Highest priority
} ProcessAppRunLevel;
```

Higher-priority apps can block lower-priority launches.

## Key APIs

### App Manager

```c
void app_manager_launch_new_app(AppLaunchConfig *config);
void app_manager_close_current_app(void);
AppInstallId app_manager_get_current_app_id(void);
```

### Process Manager

```c
bool process_manager_launch_process(const PebbleProcessMd *md);
void process_manager_make_process_safe_to_kill(void);
void process_manager_process_cleanup(void);
```

### Worker Manager

```c
void worker_manager_launch_new_worker(AppInstallId id);
void worker_manager_enable(void);
void worker_manager_disable(void);
```

### App Installation

```c
bool app_install_entry_exists(AppInstallId id);
void app_install_get_entry(AppInstallId id, AppInstallEntry *entry);
void app_install_register_callback(AppInstallCallback callback, void *context);
```

## Design Patterns

### Privilege Separation

- System apps: Full kernel access
- Third-party apps: Unprivileged, syscall-only
- Apps can only drop privilege, never gain

### Memory Budget

- Fixed heap per SDK version
- Stack overflow detected by MPU
- Heap corruption detected on free

### Resource Cleanup

- Services auto-cleanup on app exit
- Crash cleanup same as normal exit
- Phone notified of app status
