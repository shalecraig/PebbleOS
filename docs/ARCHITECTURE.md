# PebbleOS Architecture Overview

This document provides a comprehensive architectural overview of PebbleOS, a real-time operating system for smartwatches.

## Table of Contents

1. [High-Level Architecture](#high-level-architecture)
2. [Directory Structure](#directory-structure)
3. [Kernel and Core OS](#kernel-and-core-os)
4. [Hardware Abstraction Layer](#hardware-abstraction-layer)
5. [Graphics and Display](#graphics-and-display)
6. [Bluetooth and Communication](#bluetooth-and-communication)
7. [Application Framework](#application-framework)
8. [Services Layer](#services-layer)
9. [Build System and Platforms](#build-system-and-platforms)

---

## High-Level Architecture

PebbleOS is a layered embedded operating system built on FreeRTOS with the following key characteristics:

- **Real-time kernel** based on FreeRTOS with priority-based event dispatching
- **Hardware-enforced memory protection** via ARM Cortex-M MPU
- **Privilege isolation** between kernel and user applications
- **Multi-platform support** for STM32F2/F4/F7, NRF52840, and SF32LB52 MCUs
- **BLE communication** via the Nimble stack with Pebble Protocol over GATT

```
┌─────────────────────────────────────────────────────────────────┐
│                    Third-Party Applications                      │
│                  (Unprivileged, Sandboxed)                       │
├─────────────────────────────────────────────────────────────────┤
│                      Application Library                         │
│              (Graphics, UI, AppMessage, Services)                │
├─────────────────────────────────────────────────────────────────┤
│                      Syscall Interface                           │
│                   (Privilege Escalation)                         │
├─────────────────────────────────────────────────────────────────┤
│                      Services Layer                              │
│        (Clock, Alarms, Notifications, Health, Analytics)         │
├─────────────────────────────────────────────────────────────────┤
│                         Kernel                                   │
│     (Event Loop, Memory Management, Task Scheduling)             │
├─────────────────────────────────────────────────────────────────┤
│                    Driver Layer / HAL                            │
│      (Display, Sensors, Bluetooth, Power Management)             │
├─────────────────────────────────────────────────────────────────┤
│                       Hardware                                   │
│    (STM32F2/F4/F7, NRF52840, SF32LB52 + Peripherals)            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Directory Structure

| Directory | Purpose |
|-----------|---------|
| `src/fw/` | Main firmware source code |
| `src/fw/kernel/` | Core kernel (events, tasks, memory, fault handling) |
| `src/fw/drivers/` | Hardware drivers and HAL |
| `src/fw/services/` | System services (clock, alarms, health, etc.) |
| `src/fw/applib/` | Application library (graphics, UI, APIs) |
| `src/fw/apps/` | Built-in system applications |
| `src/fw/comm/` | Bluetooth and communication stack |
| `src/fw/syscall/` | System call interface |
| `src/fw/process_management/` | App/worker lifecycle management |
| `src/fw/board/` | Board definitions and configurations |
| `platform/` | Platform-specific code and bootloaders |
| `sdk/` | Developer SDK |
| `resources/` | System resources (fonts, images, etc.) |
| `third_party/` | External dependencies (FreeRTOS, Nimble, etc.) |

---

## Kernel and Core OS

### Event Loop Architecture

The kernel uses a priority-based event loop with four queues:

**Key Files:**
- `src/fw/kernel/event_loop.c` - Main event dispatcher
- `src/fw/kernel/events.c` - Event queue management
- `src/fw/kernel/events.h` - 65+ event type definitions

**Event Queue Priority (highest to lowest):**
1. `s_from_kernel_event_queue` - Kernel internal events (14 max)
2. `s_kernel_event_queue` - External to kernel (32 max)
3. `s_from_app_event_queue` - App task events (10 max)
4. `s_from_worker_event_queue` - Worker task events (5 max)

**Main Event Loop:**
```c
void launcher_main_loop(void) {
  while (1) {
    task_watchdog_bit_set(PebbleTask_KernelMain);
    PebbleEvent e;
    if (event_take_timeout(&e, 1000)) {
      prv_handle_event(&e);           // Kernel-level handling
      event_service_handle_event(&e); // App-level handling
      event_cleanup(&e);
    }
  }
}
```

### Task Management

PebbleOS defines several task types integrated with FreeRTOS:

| Task | Priority | Purpose |
|------|----------|---------|
| `PebbleTask_KernelMain` | +3 | Main kernel event loop |
| `PebbleTask_KernelBackground` | - | Background kernel tasks |
| `PebbleTask_App` | - | User application task |
| `PebbleTask_Worker` | - | Background worker task |
| `PebbleTask_BTHost` | - | Bluetooth host layer |
| `PebbleTask_BTController` | - | Bluetooth controller |
| `PebbleTask_NewTimers` | - | Timer management |

### Memory Management

**Three Independent Heaps:**
- `kernel_malloc/free()` - Kernel protected memory
- `app_malloc/free()` - App task heap (reset per app)
- `worker_malloc/free()` - Worker task heap

**MPU Regions:**
```c
enum MemoryRegionAssignments {
  MemoryRegion_Flash,
  MemoryRegion_ReadOnlyBss,
  MemoryRegion_ReadOnlyData,
  MemoryRegion_IsrStackGuard,
  MemoryRegion_AppRAM,
  MemoryRegion_WorkerRAM,
  MemoryRegion_TaskStackGuard,
  MemoryRegion_Task4
};
```

### Boot Sequence

1. **Early Boot** (`main.c`): GPIO init, vector table, fault handlers, kernel heap
2. **Create KernelMain Task**: FreeRTOS task at priority +3, start scheduler
3. **Kernel Initialization**: Services, drivers, Bluetooth, display
4. **Event Loop Entry**: `launcher_main_loop()` handles all events

### Fault Handling

- **Memory Management Fault**: Stack overflow detection, MPU violations
- **Bus Fault / Usage Fault**: Invalid memory access, illegal instructions
- **Panic System**: Displays "SAD WATCH" error code, logs to flash

---

## Hardware Abstraction Layer

### Supported MCU Families

| Family | MCU | Platforms |
|--------|-----|-----------|
| STM32F2 | Cortex-M3 | Tintin (original Pebble) |
| STM32F4 | Cortex-M4 | Snowy, Spalding, Silk |
| STM32F7 | Cortex-M7 | Robert, Cutts |
| NRF52840 | Cortex-M4 | Asterix |
| SF32LB52 | Cortex-star-MC1 | Obelix |

### Driver Organization

**Key Directories:**
- `src/fw/drivers/` - Platform-independent interfaces
- `src/fw/drivers/stm32f{2,4,7}/` - STM32 implementations
- `src/fw/drivers/nrf5/` - Nordic implementations
- `src/fw/drivers/sf32lb52/` - Sifli implementations

### HAL Abstraction Pattern

Each peripheral has:
1. **Generic interface** (e.g., `i2c_hal.h`) - Platform-agnostic API
2. **MCU-specific definitions** (e.g., `stm32f7/i2c_hal_definitions.h`)
3. **Implementation file** (e.g., `stm32f7/i2c_hal.c`)

**Example I2C Structure:**
```c
struct I2CBus {
  I2CBusState *const state;
  const struct I2CBusHal *const hal;  // MCU-specific
  StopModeInhibitor stop_mode_inhibitor;
  const char *name;
};
```

### Key Driver Categories

| Category | Files | Purpose |
|----------|-------|---------|
| Display | `display/sharp_ls013b7dh01/`, `display/ice40lp/` | LCD drivers |
| Sensors | `imu/bmi160/`, `imu/lis3dh/`, `hrm/gh3x2x/` | Motion, heart rate |
| Power | `pmic/npm1300.h`, `battery.h` | Power management |
| Communication | `i2c_hal.h`, `spi.h`, `uart.h` | Bus interfaces |
| Input | `touch/cst816/`, buttons | User input |

---

## Graphics and Display

### Graphics Context

**Key Files:**
- `src/fw/applib/graphics/graphics.h` - Drawing primitives API
- `src/fw/applib/graphics/gcontext.h` - Graphics context structure
- `src/fw/applib/graphics/framebuffer.h` - Framebuffer management

**GContext Structure:**
```c
struct GContext {
  GBitmap dest_bitmap;
  FrameBuffer* parent_framebuffer;
  GDrawState draw_state;        // Colors, clip box
  TextDrawState text_draw_state;
  FontCache font_cache;
  bool lock;
};
```

### Framebuffer Formats

| Format | Depth | Platforms |
|--------|-------|-----------|
| 1-bit | 1 bpp | Tintin, Silk, Asterix |
| 8-bit | 8 bpp | Snowy, Spalding, Robert, Obelix |

### Compositor

**Location:** `src/fw/services/common/compositor/`

The compositor manages:
- Window transitions and animations
- Modal window composition
- DMA-accelerated framebuffer copying
- Rounded corner masking (circular displays)

**Compositor States:**
- `CompositorState_App` - Render app only
- `CompositorState_Modal` - Render modal only
- `CompositorState_AppAndModal` - Composite layers
- `CompositorState_Transitioning` - Animation in progress

### Layer System

Hierarchical UI with depth-first rendering:
```c
typedef struct Layer {
  GRect bounds, frame;
  struct Layer *next_sibling, *parent, *first_child;
  struct Window *window;
  LayerUpdateProc update_proc;
  // ...
} Layer;
```

### Color System

```c
typedef union GColor8 {
  uint8_t argb;
  struct {
    uint8_t b:2, g:2, r:2, a:2;  // 2 bits per channel
  };
} GColor8;
```

---

## Bluetooth and Communication

### Protocol Stack

```
┌─────────────────────────────────────────────────────────────────┐
│ Application Layer (AppMessage API)                              │
│ src/fw/applib/app_message/                                      │
├─────────────────────────────────────────────────────────────────┤
│ Pebble Protocol (Endpoint-based messaging)                      │
│ src/fw/services/common/comm_session/                            │
├─────────────────────────────────────────────────────────────────┤
│ PPoGATT Transport (Pebble Protocol over GATT)                   │
│ src/fw/comm/ble/kernel_le_client/ppogatt/                       │
├─────────────────────────────────────────────────────────────────┤
│ GATT & GAP Layer                                                │
│ src/fw/comm/ble/                                                │
├─────────────────────────────────────────────────────────────────┤
│ Nimble BLE Stack                                                │
│ third_party/nimble/                                             │
└─────────────────────────────────────────────────────────────────┘
```

### Pebble Protocol

**Header Format:**
```c
typedef struct PACKED {
  uint16_t length;
  uint16_t endpoint_id;
} PebbleProtocolHeader;
```

**Key Endpoints:**
| ID | Purpose |
|----|---------|
| 0x0030 (48) | AppMessage |
| 0x0010 (16) | System Version |
| 0x0012 (18) | System Messages |
| 0x0033 (51) | BLE Control |

### PPoGATT Transport

Reliable packet-based transport over BLE GATT:

```c
typedef struct PACKED {
  PPoGATTPacketType type:3;  // Data, Ack, Reset
  uint8_t sn:5;              // Sequence number (mod 32)
  uint8_t payload[];
} PPoGATTPacket;
```

### AppMessage API

Dictionary-based messaging between watch and phone:

```c
// Register callbacks
app_message_register_inbox_received(handler);
app_message_register_outbox_sent(handler);

// Open communication
app_message_open(inbox_size, outbox_size);

// Send message
app_message_outbox_begin(&iter);
dict_write_cstring(iter, KEY, "value");
app_message_outbox_send();
```

---

## Application Framework

### App Lifecycle

**Key Files:**
- `src/fw/process_management/app_manager.c` - App lifecycle
- `src/fw/process_management/process_manager.c` - Process management
- `src/fw/process_management/worker_manager.c` - Background workers

**Launch Flow:**
1. `app_manager_launch_new_app()` - Initiate launch
2. `process_loader_load()` - Load binary, apply relocations
3. `prv_app_task_main()` - Enter unprivileged mode, call `main()`
4. App event loop runs until exit

### Process Storage Types

```c
typedef enum {
  ProcessStorageBuiltin = 0,   // System apps in firmware
  ProcessStorageFlash = 1,     // Third-party apps in SPI flash
  ProcessStorageResource = 2   // Apps in system resource pack
} ProcessStorage;
```

### Syscall Interface

**Key Files:**
- `src/fw/syscall/syscall.h` - 150+ syscall declarations
- `src/fw/syscall/syscall_internal.c` - Privilege escalation

**How Syscalls Work:**
1. Unprivileged app calls syscall wrapper
2. SVC instruction elevates privilege
3. Syscall executes with kernel access
4. `prv_drop_privilege()` returns to user mode

**Syscall Categories:**
- Time/Clock (8 syscalls)
- Event Management (4 syscalls)
- Resource Access (6 syscalls)
- App Communication (8 syscalls)
- Worker Management (3 syscalls)
- Health/Activity (5 syscalls)

### App Sandboxing

| Mechanism | Purpose |
|-----------|---------|
| ARM MPU | Memory region isolation |
| Privilege Modes | Kernel vs user separation |
| Syscall Validation | Buffer bounds checking |
| Timeout Protection | 3-second graceful close |
| Stack Guards | Overflow detection |

### Worker Processes

Background execution for apps:
- Single worker per app
- 11.6KB RAM, 1.4KB stack
- No graphics access
- Message passing via plugin service

---

## Services Layer

### Organization

**Directories:**
- `src/fw/services/common/` - Shared across variants
- `src/fw/services/normal/` - Production firmware
- `src/fw/services/prf/` - Recovery/manufacturing firmware

### Runlevel System

UNIX SysVinit-style control:
```c
RunLevel_BareMinimum (0)  // Minimal boot
RunLevel_LowPower (1)     // Power saving
RunLevel_Stationary (2)   // Watch at rest
RunLevel_FirmwareUpdate (3)
RunLevel_Normal (4)       // Full operation
```

### Key Services

| Service | Location | Purpose |
|---------|----------|---------|
| Clock | `common/clock.h` | Time management, formatting |
| Alarms | `normal/alarms/` | Alarm scheduling, smart alarms |
| Notifications | `normal/notifications/` | ANCS, timeline notifications |
| Activity | `normal/activity/` | Step count, heart rate, sleep |
| Analytics | `normal/analytics/` | Telemetry, event logging |
| Data Logging | `normal/data_logging/` | Buffered data upload |
| Compositor | `common/compositor/` | Display composition |

### Service Initialization Order

**`services_common_init()`:**
1. Firmware update, put/get bytes
2. Accelerometer manager, light sensor
3. Bluetooth controller, touch, HRM

**`services_normal_init()`:**
1. Persist service, app install manager
2. Blob DB, app cache
3. Alarms, timeline, notifications
4. Activity (deferred if time invalid)
5. Weather, voice, app glances

---

## Build System and Platforms

### Waf Build System

**Key Files:**
- `wscript` - Master build configuration
- `platform/wscript` - Platform predicates
- `platform/platform_capabilities.py` - Capability definitions

### Supported Platforms

| Platform | SDK Name | MCU | Display |
|----------|----------|-----|---------|
| Tintin | aplite | STM32F2 | 1-bit |
| Snowy | basalt | STM32F4 | 8-bit color |
| Spalding | chalk | STM32F4 | 8-bit color, round |
| Silk | diorite | STM32F4 | 1-bit |
| Robert/Cutts | emery | STM32F7 | 8-bit color |
| Asterix | flint | NRF52840 | 1-bit |
| Obelix | emery | SF32LB52 | 8-bit color |

### Build Variants

| Variant | Purpose |
|---------|---------|
| Normal | Production firmware |
| PRF | Recovery firmware (RECOVERY_FW) |
| Applib | SDK generation |
| Test | Unit tests |

### Build Commands

```bash
./waf configure --board=snowy_dvt  # Configure
./waf build                         # Build firmware
./waf build_prf                     # Build recovery
./waf flash                         # Flash to device
./waf qemu                          # Start emulator
```

### Resource System

Resources are packaged into `.pbpack` files:
- Merged from `resources/common/` and `resources/[normal|prf]/`
- Platform-specific overrides supported
- Types: pdc (vector), png, font, js, raw, vibe

---

## Key Architectural Patterns

1. **Event-Driven Design**: Priority-based event queues prevent starvation
2. **Layered Abstraction**: HAL isolates MCU-specific code
3. **Privilege Separation**: Apps run unprivileged with syscall interface
4. **Runlevel Control**: Services enable/disable based on power state
5. **Platform Abstraction**: Single codebase supports 7+ platforms
6. **Resource Isolation**: Each app has dedicated resource bank

## Dependencies

| Library | Purpose |
|---------|---------|
| FreeRTOS | Real-time kernel |
| Nimble | BLE stack |
| JerryScript | JavaScript engine (RockyJS) |
| nanopb | Protocol buffers |
| Memfault | Error reporting |

---

*Document generated from codebase analysis. Last updated: January 2026*
