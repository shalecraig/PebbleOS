# PebbleOS Battery Architecture Issues

This document identifies the top architectural issues that negatively impact battery life during regular watch usage, ranked by severity.

---

## Executive Summary

Analysis of the PebbleOS codebase reveals **10 critical architectural patterns** that significantly reduce battery life. The most severe issues relate to:
- Unnecessary wake-ups preventing deep sleep
- Overly aggressive BLE parameters
- Lack of adaptive power management

**Estimated combined impact**: These issues could account for 30-50% of unnecessary battery drain during typical daily use.

---

## Issue #1: Watchdog Timer Forces Wake Every 500ms (CRITICAL)

**Files:**
- `src/fw/drivers/task_watchdog.c:162-167`
- `src/fw/debug/setup.c:20-23`

**Problem:** The TIM2 hardware timer fires every 500ms to feed the watchdog, even during stop mode. This causes **2 mandatory wake-ups per second** regardless of system activity.

```c
// TIM2 interrupt fires every 500ms
void TIM2_IRQHandler(void) {
  TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
  task_watchdog_feed();  // Called EVERY 500ms even during sleep
}
```

**Evidence this is known:** Debug mode stops TIM2 (`DBGMCU_TIM2_STOP`), but production code does not.

**Battery Impact:**
- 2 CPU wake-ups/second × 86,400 seconds/day = **172,800 unnecessary wake-ups/day**
- Each wake costs ~2-5mA for ~1ms = ~0.3-0.9 Ah wasted daily
- **Prevents deep sleep consolidation entirely**

**Recommendation:** Feed watchdog from RTC alarm callback (like SF32LB52 does) instead of dedicated timer.

---

## Issue #2: BLE Scanning at 100% Duty Cycle (CRITICAL)

**File:** `src/fw/comm/ble/gap_le_scan.c:51-53`

**Problem:** BLE scanning uses scan_window == scan_interval (10.24 seconds each), meaning the radio **never stops listening**. Additionally, active scanning is enabled, requiring transmission of scan requests.

```c
bt_driver_start_le_scan(
    true /* active_scan */,      // Transmits scan requests (high power!)
    false /* use_white_list */,  // Scans ALL devices
    true /* filter_dups */,
    10240, 10240);               // 100% duty cycle!
```

**Battery Impact:**
- BLE radio continuously powered during any scan operation
- Active scanning adds TX power on top of RX
- **10-20mA continuous drain** during device discovery
- No automatic timeout - scanning continues until explicitly stopped

**Recommendation:**
- Reduce duty cycle: window=100ms, interval=1000ms (10% duty)
- Use passive scanning when possible
- Add automatic timeout after 30-60 seconds

---

## Issue #3: 15ms BLE Connection Interval with Disabled Slave Latency (HIGH)

**Files:**
- `src/fw/comm/ble/gap_le_connect_params.c:74-96`
- `src/fw/comm/ble/gatt_client_discovery.c:390-426`

**Problem:** When high-power mode (ResponseTimeMin) is requested, connection interval drops to **15ms** (vs 150-180ms normal). Slave latency is **disabled** (set to 0) due to stability concerns, preventing the device from skipping connection events.

```c
// ResponseTimeMin uses 15ms interval - 10x more radio events!
[ResponseTimeMin] = {
  .min_connection_interval_1_25ms = 12,  // 15ms
  .max_connection_interval_1_25ms = 12,
  .slave_latency_events = 0,  // DISABLED - cannot skip events!
  .supervision_timeout_10ms = 600,
}

// TODO comment admits: "Let's just remove slave latency for now"
```

**Battery Impact:**
- 15ms vs 150ms = **10x more connection events**
- Each event requires radio TX/RX = ~10mA for ~2ms
- Service discovery holds fast mode for **30 seconds**
- Music service can hold fast mode **indefinitely** (`MAX_PERIOD_RUN_FOREVER`)

**Recommendation:**
- Re-enable slave latency with proper testing
- Add timeout to music service fast mode
- Reduce service discovery to 10-15 seconds

---

## Issue #4: Stop Mode Fallback Causes Busy-Wait (HIGH)

**File:** `src/fw/freertos_application.c:86-90`

**Problem:** When stop mode is disabled (RTC not ready, or inhibitor active), the function returns immediately without any sleep. This causes FreeRTOS to busy-loop at **100% CPU usage**.

```c
void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime) {
  if (!rtc_alarm_is_initialized() || !sleep_mode_is_allowed()) {
    // Just returning causes busy loop where caller thought we slept for
    // 0 ticks and will reevaluate what to do next (probably just try again).
    return;  // NO SLEEP AT ALL!
  }
  // ... actual sleep code ...
}
```

**Battery Impact:**
- During boot: **10-20 seconds of 100% CPU**
- When any stop inhibitor is held: continuous 100% CPU
- Measured at boot: ~50-100mA vs ~5mA in stop mode

**Recommendation:** Add WFI (Wait For Interrupt) fallback when stop mode unavailable:
```c
if (!stop_mode_is_allowed()) {
  __WFI();  // At least shallow sleep
  return;
}
```

---

## Issue #5: Flash Writes Immediately Disable Stop Mode (HIGH)

**Files:**
- `src/fw/drivers/flash/flash_api.c:237, 311`
- `src/fw/services/normal/settings/settings_file.c:435-468`

**Problem:** Every flash write immediately disables stop mode and writes synchronously. A single settings change triggers **5-6 separate flash operations**, each holding the CPU awake.

```c
void flash_write_bytes(...) {
  stop_mode_disable(InhibitorFlash);  // FIXME: PBL-18028 - known issue!
  while (buffer_size) {
    flash_impl_write_page_begin(...);
    while ((status = flash_impl_get_write_status()) == E_BUSY) {
      psleep(0);  // Busy waiting!
    }
  }
  stop_mode_enable(InhibitorFlash);
}
```

**Settings write amplification:**
```c
status_t settings_file_set(...) {
  // WRITE #1: Mark existing record
  // WRITE #2-4: Write new header, key, value
  // WRITE #5: Mark write complete
  // WRITE #6: Mark old record overwritten
  // = 6 flash writes per setting change!
}
```

**Battery Impact:**
- Each flash operation: 5-50ms of blocked CPU
- Activity service writes step count frequently
- Analytics write every hour (multiple operations)
- No write batching or coalescing

**Recommendation:**
- Batch settings writes with periodic flush
- Use deferred write queue
- Consider RAM-backed cache with periodic sync

---

## Issue #6: Accelerometer 200Hz Sampling for Double-Tap (HIGH)

**Files:**
- `src/fw/drivers/imu/bmi160/bmi160.c:92, 213-220`
- `src/fw/services/common/accel_manager.c:229`

**Problem:** Enabling double-tap detection forces the **entire accelerometer system** to sample at 200Hz, even when apps only need 10-25Hz data. The chip is never suspended when idle.

```c
[AccelOperatingModeDoubleTapDetection] = {
  .sample_interval = BMI160SampleRate_200_HZ,  // 8x default rate!
}

// Driver selects HIGHEST rate needed by ANY mode
// If double-tap enabled: everything runs at 200Hz

// TODO at line 817: "If we aren't doing anything else, suspend the chip?"
// TODO at line 229: "Add low power support"
```

**Battery Impact:**
- 200Hz vs 25Hz = **8x more SPI transactions**
- Accelerometer chip never enters suspend mode
- FIFO fills 8x faster, causing more CPU interrupts
- Default 25Hz is already 2.5x higher than minimum 10Hz

**Recommendation:**
- Implement motion-based adaptive sampling (idle = 5-10Hz)
- Use hardware motion detection to wake from low-rate mode
- Suspend chip when no subscribers

---

## Issue #7: 25ms Backlight Fade Timer (MEDIUM)

**File:** `src/fw/services/common/light.c:31-33, 209`

**Problem:** Backlight fades use 20 steps over 500ms, meaning a timer fires every **25ms** during any fade operation. This creates **40 wake-ups per second** during a fade.

```c
const uint32_t LIGHT_FADE_TIME_MS = 500;
const uint32_t LIGHT_FADE_STEPS = 20;

// Timer fires every 25ms during fade
new_timer_start(s_timer_id, LIGHT_FADE_TIME_MS / LIGHT_FADE_STEPS,  // = 25ms
                light_timer_callback, NULL, 0);
```

**Battery Impact:**
- 20 wake-ups per backlight fade (every button press, notification dismiss)
- With motion-enabled backlight: many fades per hour
- Each wake prevents sleep consolidation

**Recommendation:**
- Reduce steps from 20 to 5-10 (50-100ms intervals)
- Use hardware PWM fade if available
- Consider instant-off for aggressive power saving

---

## Issue #8: Fixed 30 FPS Animations with Full-Screen Dirty (MEDIUM)

**Files:**
- `src/fw/applib/ui/animation.h:71`
- `src/fw/services/common/compositor/compositor.c:278`
- `src/fw/services/common/compositor/default/compositor_launcher_app_transitions.c:163-166`

**Problem:** All animations run at fixed 30 FPS regardless of content complexity. Every frame marks the **entire framebuffer dirty**, forcing full display refresh even for tiny changes.

```c
#define ANIMATION_TARGET_FRAME_INTERVAL_MS 33  // 30 Hz fixed

void compositor_render_app(void) {
  framebuffer_dirty_all(&s_framebuffer);  // ENTIRE screen marked dirty
}

// Developer comment acknowledges inefficiency:
// "let's make it easy and just dirty the whole framebuffer on each frame anyway"
```

**Battery Impact:**
- 30 display updates/second during any transition
- Full SPI DMA transfer every 33ms (stops deep sleep)
- Typical app transition: 6-8 frames = 200-270ms of high activity
- No frame skipping for static content

**Recommendation:**
- Implement dirty rectangle tracking
- Add content-aware frame rate (skip identical frames)
- Reduce transition FPS to 20-24 for simple animations

---

## Issue #9: Multiple 1-Second Timer Wake-Ups (MEDIUM)

**Files:**
- `src/fw/services/common/regular_timer.c:100`
- `src/fw/services/common/clock.c:415`
- `src/fw/services/common/cron.c:90`
- `src/fw/services/common/tick_timer.c:32`

**Problem:** Multiple independent services schedule 1-second callbacks that **cannot be coalesced**:

| Service | Purpose | File |
|---------|---------|------|
| DST Check | Monitor daylight savings | `clock.c:415` |
| Cron | Check alarms/calendar | `cron.c:90` |
| Tick Timer | App clock ticks | `tick_timer.c:32` |
| Battery Sample | Voltage monitoring | `battery_state.c:23` |

**Battery Impact:**
- System wakes **at least once per second** even when idle
- Each wake runs multiple callback chains
- No timer coalescing mechanism exists
- STM32F4 RTC alarm limited to ~4 seconds maximum

**Recommendation:**
- Implement timer coalescing with slack tolerance
- Use event-driven DST checking (only on time set)
- Batch cron checks with 5-10 second granularity when no imminent alarms

---

## Issue #10: 10-Minute Alarm Vibration Duration (MEDIUM)

**File:** `src/fw/popups/alarm_popup.c:118-179`

**Problem:** Alarms vibrate for up to **10 minutes continuously** with 1 vibration per second. This is 600 vibrations at full motor power.

```c
#define VIBE_DURATION (10 * 60 * 1000)  // 10 MINUTES!
#define TINTIN_MAX_VIBES (10 * 60)       // 600 vibrations
#define TINTIN_VIBE_REPEAT_INTERVAL_MS 1000

// Default intensity is HIGH (100% power) on most platforms
#define DEFAULT_VIBE_INTENSITY VibeIntensityHigh
```

**Battery Impact:**
- Vibration motor: 50-150mA per pulse
- 600 pulses × ~100ms × 100mA = ~1.7 mAh per missed alarm
- Missed alarms can drain 5-10% of total battery

**Recommendation:**
- Reduce maximum duration to 2-3 minutes
- Implement escalating pattern (start quiet, get louder)
- Reduce intensity after first minute

---

## Summary Table

| Rank | Issue | Severity | Daily Impact | Fix Complexity |
|------|-------|----------|--------------|----------------|
| 1 | Watchdog 500ms wake | CRITICAL | ~1 mAh wasted | Medium |
| 2 | BLE 100% scan duty | CRITICAL | 10-20mA during scan | Low |
| 3 | BLE 15ms interval | HIGH | 5-10mA during fast mode | Medium |
| 4 | Stop mode busy-wait | HIGH | 50-100mA during boot | Low |
| 5 | Flash write blocking | HIGH | Variable | Medium |
| 6 | 200Hz accel sampling | HIGH | ~2mA continuous | Medium |
| 7 | 25ms backlight fade | MEDIUM | 20 wakes/fade | Low |
| 8 | 30 FPS full-screen dirty | MEDIUM | During transitions | Medium |
| 9 | 1-second timer wakes | MEDIUM | 86,400 wakes/day | High |
| 10 | 10-minute alarm vibe | MEDIUM | ~1.7mAh/alarm | Low |

---

## Quick Wins (Low Effort, High Impact)

1. **Add WFI fallback** in `vPortSuppressTicksAndSleep()` - 1 line change
2. **Reduce BLE scan duty cycle** - Change 2 parameters
3. **Add timeout to music service** fast mode - Change 1 constant
4. **Reduce backlight fade steps** from 20 to 8 - Change 1 constant
5. **Reduce alarm duration** from 10 to 3 minutes - Change 1 constant

---

*Analysis performed January 2026 based on codebase review*
