# Kernel Architecture

The PebbleOS kernel provides the core runtime infrastructure including event processing, task management, fault handling, and memory layout.

## Event Loop

**Location**: `src/fw/kernel/event_loop.c`

The event loop is the heart of PebbleOS, running in the `KernelMain` task:

```c
void launcher_main_loop(void) {
  while (1) {
    task_watchdog_bit_set(PebbleTask_KernelMain);

    if (event_take_timeout(&e, 1000)) {
      prv_handle_event(&e);
      event_service_handle_event(&e);
      event_cleanup(&e);
    }
  }
}
```

### Event Types (44 total)

| Category | Events |
|----------|--------|
| Input | `BUTTON_DOWN/UP`, `TOUCH`, `ACCEL_SHAKE` |
| Rendering | `RENDER_REQUEST/READY/FINISHED` |
| System | `TICK`, `SET_TIME`, `BATTERY_STATE_CHANGE` |
| App Lifecycle | `APP_LAUNCH`, `WORKER_LAUNCH`, `DEINIT`, `KILL` |
| Bluetooth | `BLE_GATT_CLIENT`, `COMM_SESSION` |
| IPC | `CALLBACK`, `SUBSCRIPTION` |

### Event Queues

| Queue | Size | Purpose |
|-------|------|---------|
| `s_kernel_event_queue` | 32 | General system events |
| `s_from_app_event_queue` | 10 | Events from running app |
| `s_from_worker_event_queue` | 5 | Events from background worker |
| `s_from_kernel_event_queue` | 14 | Self-posted kernel events |

## Task Management

**Location**: `src/fw/kernel/pebble_tasks.c`

### Task Hierarchy

| Task | Priority | Purpose |
|------|----------|---------|
| `KernelMain` | tskIDLE + 3 | Event loop, privileged |
| `KernelBackground` | Lower | Background services |
| `App` | tskIDLE + 2 | User application |
| `Worker` | tskIDLE + 1 | Background worker |
| `BTHost/BTController/BTHCI` | Varies | Bluetooth stack |
| `NewTimers` | High | Timer callbacks |

### Task Creation

Tasks are created with `xTaskCreateRestricted()` and MPU regions:
- App RAM access control
- Worker RAM access control
- Stack guard region (overflow detection)

## Fault Handling

**Location**: `src/fw/kernel/fault_handling.c`

Three Cortex-M exception handlers:
- `MemoryManagement_IRQn` - MPU violations, alignment faults
- `BusFault_IRQn` - Invalid memory access
- `UsageFault_IRQn` - Invalid instructions

### Fault Recovery Strategy

**Kernel Fault**: Core dump + hard reset (unrecoverable)

**User App Fault**: Redirect to "landing zone" for graceful termination
```c
static void prv_return_to_landing_zone(...) {
  // Log crash info
  setup_log_app_crash_info(crash_info);

  // Redirect PC to cleanup function
  stacked_args[6] = (int)hardware_fault_landing_zone;

  // Elevate privilege for cleanup
  mcu_state_set_thread_privilege(true);
}
```

### Stack Overflow Detection

Stack guard regions at bottom of each task stack. Overflow triggers MPU fault:
1. Detect fault address in guard region
2. Adjust stack pointer above guard
3. Redirect to landing zone
4. Log crash and terminate app

## Watchdog

**Location**: `src/fw/drivers/task_watchdog.c`

1Hz interrupt timer checks task liveness:

```c
// Tasks must call this every second
task_watchdog_bit_set(PebbleTask task);

// On timeout: core dump + reset
RebootReasonCode_WatchdogTimeout
```

## Core Dumps

**Location**: `src/fw/kernel/core_dump.c`

Captures system state on crash:
- RAM contents (128-384 KB)
- Peripheral registers (NVIC, RCC, RTC)
- Thread stacks and register state
- Firmware build ID, serial number

Storage: 2-3 slots in flash with rotation (oldest overwritten)

## Boot Sequence

**Location**: `src/fw/main.c`

1. **Hardware Init**: GPIO, clock, UART, fault handlers
2. **Kernel Heap**: Initialize kernel memory allocator
3. **FreeRTOS Start**: Create KernelMain task, start scheduler
4. **Driver Init**: Board, battery, flash, sensors, display
5. **Service Init**: Clock, Bluetooth, analytics, apps
6. **Event Loop**: Begin processing events

```c
int main(void) {
  gpio_init_all();
  enable_fault_handlers();
  kernel_heap_init();

  // Create KernelMain task and start scheduler
  xTaskCreateRestricted(...);
  vTaskStartScheduler();
}
```

## Memory Layout

**Location**: `src/fw/kernel/memory_layout.c`

MPU regions configured at boot:

| Region | Purpose | Access |
|--------|---------|--------|
| Flash | Code, read-only data | Priv R, Unpriv R |
| Readonly BSS | Kernel constants | Priv R/W, Unpriv R |
| ISR Stack Guard | Overflow detection | No access |
| App RAM | App code/data/heap | Per-task |
| Worker RAM | Worker code/data/heap | Per-task |
| Task Stack Guard | Overflow detection | No access |

## Key APIs

```c
// Event posting
event_put(PebbleEvent *event);
event_put_isr(PebbleEvent *event);  // From ISR

// Task identification
pebble_task_get_current();
pebble_task_get_handle(PebbleTask task);

// Watchdog
task_watchdog_bit_set(PebbleTask task);
task_watchdog_mask_set(PebbleTask task);

// Fault handling
kernel_fault(RebootReasonCode reason, uint32_t lr);
trigger_oom_fault();
```
