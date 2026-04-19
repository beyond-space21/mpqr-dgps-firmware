
**PROJECT ARCHITECTURE GUIDELINES**

Follow these rules strictly for all code in this project.

### 1. Overall Architecture

- Use a **multi-task** FreeRTOS design.
- Divide responsibilities into separate tasks.
- Pin tasks to appropriate CPU cores:
  - Core 0: Gyro and real-time I/O tasks
  - Core 1: UI and network tasks

### 2. Shared Resources Protection

- One global **I2C mutex** (`i2c_mutex`) protects the entire I2C bus.
- All I2C operations (gyro, fuel gauge, touch screen) **must** take the I2C mutex before starting any transaction and release it immediately after.
- Never perform I2C operations from the display task or network task.

- One global **telemetry mutex** (`telemetry_mutex`) protects shared sensor data.
- All tasks read or write telemetry data under this mutex.

### 3. Telemetry Hub

- Use a single global `telemetry_t` struct as the central data store.
- Gyro task (and other producers) write to it.
- Display and network tasks only read (by making a local copy under mutex).

### 4. Required Tasks

Create and maintain the following tasks:

- **Gyro Task** (Core 0)  
  Handles gyro/IMU (I2C) and fuel gauge (I2C).

- **RTK Task** (Core 0)  
  Reads data from RTK module over UART.

- **NTRIP Task** (Core 1)  
  Fetches correction data through websocket and sends it to the RTK module over UART.

- **Display Task** (Core 1)  
  Drives the colour OLED display over QSPI and updates the UI.

- **Touch Task** (Core 1)  
  Reads touch screen input over I2C and processes touch events.

### 5. Project Structure (Boilerplate)

```
src/
├── main.c                    // app_main() - creates mutexes and starts all tasks
├── config.h                  // all pin definitions and configuration
├── telemetry.h               // telemetry_t struct definition
├── telemetry.c
├── i2c_bus.h                 // I2C mutex and safe access wrappers
├── i2c_bus.c
├── tasks/
│   ├── gyro_task.h / .c      // gyro + fuel gauge
│   ├── rtk_task.h / .c       // UART RTK reading
│   ├── ntrip_task.h / .c     // websocket → RTK corrections
│   ├── display_task.h / .c   // QSPI display updates
│   └── touch_task.h / .c     // I2C touch input
└── utils/
    └── logging.h             // optional unified logging
```

### 6. Initialization Order (app_main)

1. Create `i2c_mutex` and `telemetry_mutex`
2. Initialize I2C master bus
3. Initialize all peripherals
4. Create all tasks with correct core pinning
5. Delete or suspend main task

### 7. Strict Rules

- Every I2C access must go through the I2C mutex.
- Never call I2C functions directly from Display Task or NTRIP Task.
- Display Task must only read telemetry (never write to I2C).
- Keep tasks responsive — avoid long blocking operations in UI and network tasks.
- Use `pdMS_TO_TICKS()` for all delays.
- Define all pins and constants in `config.h`

When generating or modifying any file, always follow this architecture and structure.
