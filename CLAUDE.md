# PebbleOS

PebbleOS is a real-time operating system for smartwatches, built on FreeRTOS with extensive customization for power efficiency and memory-constrained ARM Cortex-M hardware.

## Quick Reference

### Build Commands

```bash
# Configure (pick a board)
./waf configure --board=snowy_bb2

# Build firmware
./waf build

# Flash to device
./waf flash_fw

# Build resources
./waf image_resources
```

### QEMU Simulator

```bash
# Configure for QEMU (STM32 boards only)
./waf configure --board=snowy_bb2 --qemu

# Build and run
./waf build qemu_image_micro qemu_image_spi
./waf qemu

# Debug with GDB
./waf qemu_gdb
```

### Supported Boards

- **STM32F2**: tintin (original Pebble)
- **STM32F4/F7**: snowy, spalding (Pebble Time, Time Steel)
- **NRF52840**: silk (Pebble 2)
- **SF32LB52**: robert, asterix, obelix (newer platforms)

## Directory Structure

```
src/
├── fw/                    # Main firmware
│   ├── kernel/           # Event loop, tasks, fault handling, memory
│   ├── drivers/          # Hardware drivers (display, sensors, power, buses)
│   ├── applib/           # App library (graphics, UI, animations)
│   ├── services/         # System services (Bluetooth, health, notifications)
│   ├── process_management/ # App loading, isolation, workers
│   ├── apps/             # System apps (launcher, settings, music)
│   └── comm/             # Communication protocols (BLE, PPoGATT)
├── bluetooth-fw/         # NimBLE BLE stack integration
├── libutil/              # Utilities (heap, data structures)
├── libc/                 # Custom C library
└── include/              # Shared headers

platform/                 # Board-specific code (tintin, snowy, silk, robert, etc.)
sdk/                      # App developer SDK
tools/                    # Build and analysis tools
third_party/              # FreeRTOS, NimBLE, JerryScript, etc.
docs/                     # Documentation (Sphinx-based)
```

## Architecture Overview

### Kernel (`src/fw/kernel/`)
- **Event-driven**: 44 event types processed in main loop
- **FreeRTOS-based**: Preemptive priority scheduling
- **Fault tolerant**: Hardware faults redirect apps to "landing zone" for graceful termination
- **Watchdog**: 1Hz task liveness monitoring with core dump on timeout

### Memory Model
- **Three isolated heaps**: Kernel, App, Worker (MPU-enforced)
- **Hardware protection**: ARM MPU isolates unprivileged apps from kernel
- **Stack guards**: Detect overflow via MPU fault

### Process Management (`src/fw/process_management/`)
- **Privileged**: System apps run with full kernel access
- **Unprivileged**: Third-party apps isolated via MPU
- **Workers**: Background threads for periodic tasks
- **Graceful shutdown**: 3-second timeout before force kill

### Graphics (`src/fw/applib/graphics/`)
- **Layer hierarchy**: Dirty-rectangle optimization
- **1-bit and 8-bit**: Platform-dependent color depth
- **Animations**: Configurable easing curves

### Bluetooth (`src/fw/comm/ble/`, `src/bluetooth-fw/`)
- **NimBLE stack**: BLE 4.x support
- **PPoGATT**: Reliable messaging over GATT
- **ANCS/AMS**: iOS notification and media integration

## Key Patterns

### Event Handling
Events flow through `event_loop.c`. Post events with:
```c
PebbleEvent event = { .type = PEBBLE_BUTTON_DOWN_EVENT, ... };
event_put(&event);
```

### Memory Allocation
```c
kernel_malloc(size);  // Kernel heap (privileged only)
app_malloc(size);     // App heap
task_malloc(size);    // Current task's heap (auto-selects)
```

### Driver Registration
Drivers use board-specific configuration via `BOARD_CONFIG_*` macros in board files.

### Service Runlevels
Services implement `service_set_enabled(bool)` and are controlled by runlevel system:
- `BareMinimum(0)` → `LowPower(1)` → `Stationary(2)` → `FirmwareUpdate(3)` → `Normal(4)`

## Testing

```bash
./waf test              # Run unit tests
./waf qemu              # Run in emulator
```

## Generated Documentation

See `docs/gen/` for detailed architecture documentation:
- `architecture-overview.md` - System overview
- `kernel.md` - Kernel internals
- `drivers.md` - Driver layer
- `graphics-ui.md` - Graphics and UI system
- `services.md` - System services
- `process-management.md` - App lifecycle
- `bluetooth.md` - Bluetooth stack
- `memory-management.md` - Memory architecture
