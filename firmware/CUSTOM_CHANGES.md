# Custom Modifications

Fork of [vedderb/bldc](https://github.com/vedderb/bldc) (`release_6_06`, GPL-3.0).
Goal: analog throttle → custom PID speed control, with target-speed telemetry to a host app.

## Modified

| File | Change |
|---|---|
| `comm/commands.c` | New comm command `COMM_GET_SRPM` — returns current target ERPM |
| `datatypes.h` | Adds `COMM_GET_SRPM = 247` to `COMM_PACKET_ID` |
| `conf_general.h` | `APP_CUSTOM_TO_USE` → `app_adc_advance.c` |
| `rules.mk` | Whitespace fix (tabs vs spaces), Windows build only |

## New (`applications/`)

| File | What it does |
|---|---|
| `app_terminal.c` | Minimal test — registers `print_hello` terminal command |
| `app_basic_adc.c` | Basic ADC throttle → RPM, built-in PID speed mode |
| `app_adc_advance.c` | **Active app.** Custom PID speed loop, exposes target RPM via `COMM_GET_SRPM` |
