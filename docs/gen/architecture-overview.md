# PebbleOS Architecture Overview

PebbleOS is a sophisticated real-time operating system designed for smartwatches, built on FreeRTOS with extensive customization for power efficiency and memory-constrained hardware.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  System Apps (Launcher, Settings, Music, Notifications)    │
├─────────────────────────────────────────────────────────────┤
│  Third-Party Apps (Watchfaces, Apps, Workers)              │
├─────────────────────────────────────────────────────────────┤
│  App Library (Graphics, UI, Animations, App Services)      │
├─────────────────────────────────────────────────────────────┤
│  Services (Bluetooth, Health, Notifications, Storage)      │
├─────────────────────────────────────────────────────────────┤
│  Kernel (Events, Tasks, Memory, Fault Handling)            │
├─────────────────────────────────────────────────────────────┤
│  Drivers (Display, Sensors, Power, Communication Buses)    │
├─────────────────────────────────────────────────────────────┤
│  FreeRTOS + Hardware Abstraction Layer                     │
├─────────────────────────────────────────────────────────────┤
│  Hardware (STM32, NRF52, SF32LB52)                         │
└─────────────────────────────────────────────────────────────┘
```

## Core Subsystems

### Kernel (`src/fw/kernel/`)
The kernel provides the foundational runtime:
- **Event Loop**: Processes 44 event types (buttons, timers, Bluetooth, rendering)
- **Task Management**: FreeRTOS-based preemptive scheduling with 9 task types
- **Memory Layout**: MPU-based isolation between kernel, app, and worker heaps
- **Fault Handling**: Graceful app termination on crashes, core dumps for debugging
- **Watchdog**: 1Hz monitoring with automatic reset on task stalls

### Driver Layer (`src/fw/drivers/`)
Hardware abstraction with 24+ driver categories:
- **Display**: Sharp Memory LCD, FPGA controllers
- **Sensors**: IMU (BMI160, BMA255), HRM (AS7000, GH3X2X), ambient light
- **Power**: PMIC, battery monitoring, backlight PWM
- **Buses**: I2C, SPI, DMA with thread-safe access
- **Input**: Buttons with debouncing, capacitive touch (CST816)

### Graphics & UI (`src/fw/applib/`)
Efficient rendering for small displays:
- **Layer System**: Hierarchical compositing with dirty-rectangle optimization
- **Drawing Primitives**: Lines, shapes, paths, bitmaps with antialiasing
- **Text Rendering**: UTF-8 support, font caching, layout algorithms
- **Animations**: Declarative system with easing curves
- **Widgets**: TextLayer, BitmapLayer, MenuLayer, ScrollLayer, etc.

### Services (`src/fw/services/`)
High-level OS functionality:
- **Bluetooth**: NimBLE stack, PPoGATT reliable messaging, ANCS/AMS
- **Health**: Activity tracking, sleep detection, heart rate monitoring
- **Notifications**: Timeline-based storage, Do Not Disturb
- **Storage**: Blob databases, persistent key-value stores, data logging
- **Analytics**: Telemetry collection, crash reporting

### Process Management (`src/fw/process_management/`)
App lifecycle control:
- **App Manager**: Launches, monitors, and terminates apps
- **Memory Isolation**: MPU-enforced separation of app memory
- **Workers**: Background threads for periodic tasks
- **Installation**: App loading from flash, SDK compatibility checking

### Communication (`src/fw/comm/`, `src/bluetooth-fw/`)
Connectivity stack:
- **BLE/GAP**: Connection management, advertising, scanning
- **GATT**: Service discovery, characteristic operations
- **PPoGATT**: Pebble Protocol over GATT for reliable messaging
- **Session Layer**: Abstract transport for app messaging

## Platform Support

| Platform | MCU | Boards |
|----------|-----|--------|
| Tintin | STM32F2 | Original Pebble |
| Snowy | STM32F4/F7 | Pebble Time, Time Steel |
| Silk | NRF52840 | Pebble 2 |
| Robert | SF32LB52 | Newer platforms |
| Asterix/Obelix | SF32LB52 | Latest platforms |

## Build System

WAF-based build with custom plugins:
```bash
./waf configure --board=<board> [--qemu]
./waf build
./waf flash_fw
```

## Key Design Principles

1. **Power Efficiency**: Runlevel-based service control, deep sleep with RTC wakeup
2. **Memory Safety**: Hardware MPU isolation, stack guards, heap corruption detection
3. **Fault Tolerance**: Graceful app crashes, core dumps, watchdog recovery
4. **Backward Compatibility**: SDK version checking, legacy app support
5. **Real-Time**: Preemptive scheduling, bounded event queue sizes
