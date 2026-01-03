# Driver Layer Architecture

The driver layer provides hardware abstraction for all peripherals, supporting multiple MCU families (STM32F2/F4/F7, NRF52840, SF32LB52).

## Directory Structure

```
src/fw/drivers/
├── display/              # Display controllers
│   ├── sharp_ls013b7dh01/  # Sharp Memory LCD
│   └── ice40lp/            # FPGA display controller
├── imu/                  # Accelerometer/gyroscope
│   ├── bmi160/            # Bosch 6-axis
│   ├── bma255/            # Bosch 3-axis
│   └── lis3dh/            # ST 3-axis
├── hrm/                  # Heart rate monitors
│   ├── as7000/            # AMS PPG sensor
│   └── gh3x2x/            # Goodix sensor
├── touch/                # Touch sensors
│   └── cst816/            # Capacitive touch
├── flash/                # Flash memory
├── i2c.c, spi.c, dma.c   # Bus drivers
├── battery.c, pmic.c     # Power management
├── button.c              # Button input
├── backlight.c           # Display backlight
└── stubs/                # Test stubs
```

## Display Drivers

**API**: `src/fw/drivers/display/display.h`

```c
void display_init(void);
void display_update(NextRowCallback next_row, UpdateCompleteCallback complete);
void display_set_enabled(bool enabled);
void display_clear(void);
```

### Row-Based Rendering

Displays pull frame data via callback (memory efficient):
```c
typedef bool (*NextRowCallback)(uint8_t **row_buffer, GColor **palette);
```

### Supported Displays

| Display | Interface | Notes |
|---------|-----------|-------|
| Sharp LS013B7DH01 | SPI | Memory LCD, bistable |
| ICE40LP FPGA | SPI | Higher resolution, color |

## Input Drivers

### Buttons

**API**: `src/fw/drivers/button.h`

```c
bool button_is_pressed(ButtonId button);
```

Debouncing via `debounced_button.c`:
- 32KHz timer, 2ms sample rate
- 40ms stability requirement (20 samples)
- 5-second hold detection for factory reset

### Touch Sensors

**API**: `src/fw/drivers/touch/touch_sensor.h`

CST816 capacitive touch:
- I2C communication
- Interrupt-based event detection
- Firmware update support

## Sensor Drivers

### Accelerometer

**API**: `src/fw/drivers/accel.h`

```c
void accel_set_sampling_interval(uint64_t interval_us);
void accel_set_num_samples(uint8_t num_samples);
void accel_enable_shake_detection(bool enable);

// Callbacks
void accel_cb_new_sample(void *context, AccelDriverSample *samples, uint32_t count);
void accel_cb_shake_detected(void *context);
```

Supported accelerometers:
- **BMI160**: 6-axis SPI, FIFO with watermark interrupts
- **BMA255**: 3-axis SPI, multiple power modes
- **LIS3DH/LIS2DW12**: ST 3-axis variants

### Heart Rate Monitor

**API**: `src/fw/drivers/hrm.h`

AS7000 PPG sensor features:
- Multiple app modes (HRM, PRV, GSR)
- Presence detection
- Signal Quality Index (SQI)
- Accelerometer sync for motion compensation

### Ambient Light Sensor

**API**: `src/fw/drivers/ambient_light.h`

```c
uint32_t ambient_light_get_light_level(void);  // 0-4096
```

ADC-based with board-specific calibration.

## Power Management

### PMIC

**API**: `src/fw/drivers/pmic.h`

```c
void pmic_power_off(void);
void pmic_set_charger_state(bool enable);
bool pmic_is_usb_connected(void);
bool pmic_is_charging(void);
uint16_t pmic_get_vsys(void);  // mV
```

### Battery

**API**: `src/fw/drivers/battery.h`

```c
uint16_t battery_get_millivolts(void);
BatteryChargeStatus battery_get_charge_status(void);
void battery_set_charge_enable(bool enable);
```

Charge states: Unknown, Complete, Trickle, CC, CV

### Backlight

**API**: `src/fw/drivers/backlight.h`

```c
void backlight_set_brightness(uint16_t brightness);  // 0-100
void backlight_set_enabled(bool enabled);
```

PWM-based (256Hz, 1024-step resolution) or GPIO on/off.

## Communication Buses

### I2C

**API**: `src/fw/drivers/i2c.h`

```c
void i2c_use(I2CSlavePort port);
void i2c_release(I2CSlavePort port);

bool i2c_read_register(I2CSlavePort port, uint8_t reg, uint8_t *value);
bool i2c_write_register(I2CSlavePort port, uint8_t reg, uint8_t value);
bool i2c_read_register_block(I2CSlavePort port, uint8_t reg, uint8_t *data, size_t len);
```

Features:
- Multi-bus support
- Mutex protection for thread safety
- NACK retry mechanism (up to 1000 retries)
- Power rail management

### SPI

**API**: `src/fw/drivers/spi.h`

```c
void spi_slave_acquire(SPISlavePort port);
void spi_slave_release(SPISlavePort port);

uint8_t spi_slave_read_write(SPISlavePort port, uint8_t data);
void spi_slave_burst_read(SPISlavePort port, uint8_t *buffer, size_t len);
void spi_slave_burst_write(SPISlavePort port, const uint8_t *data, size_t len);
```

DMA support via `spi_dma.h` for high-throughput transfers.

### DMA

**API**: `src/fw/drivers/dma.h`

```c
typedef void (*DMACallback)(void *context);

void dma_request_start_direct(DMARequest *req, void *src, void *dst, size_t len,
                               DMACallback complete, void *context);
void dma_request_start_circular(DMARequest *req, void *buffer, size_t len,
                                 DMACallback half, DMACallback complete, void *context);
```

## Flash Storage

**API**: `src/fw/drivers/flash.h`

```c
void flash_use(void);
void flash_release(void);

void flash_read_bytes(uint8_t *buffer, uint32_t addr, size_t len);
void flash_write_bytes(const uint8_t *data, uint32_t addr, size_t len);
void flash_erase_subsector(uint32_t addr);
void flash_erase_sector(uint32_t addr);
```

Features:
- QSPI controller with memory-mapped access
- Async operations with completion callbacks
- Deep sleep for power savings
- CRC32 validation

## GPIO

**API**: `src/fw/drivers/gpio.h`

```c
void gpio_use(GPIO_TypeDef *port);
void gpio_release(GPIO_TypeDef *port);

void gpio_output_set(OutputConfig *config, bool value);
bool gpio_input_read(InputConfig *config);
```

## Driver Patterns

### Registration via Board Config

```c
// In board file
BOARD_CONFIG_BUTTON      // Button GPIO mapping
BOARD_CONFIG_BACKLIGHT   // Backlight control
BOARD_CONFIG.ambient_light_dark_threshold
```

### Callback Model

ISR sets flag, returns `should_context_switch`:
```c
void accel_isr(void) {
  bool should_switch = false;
  accel_offload_work_from_isr(&should_switch);
  portYIELD_FROM_ISR(should_switch);
}
```

### Power-Aware Pattern

```c
// Reference counting for power rails
led_enable(LEDEnabler_Backlight);
// ... use LED ...
led_disable(LEDEnabler_Backlight);
```
