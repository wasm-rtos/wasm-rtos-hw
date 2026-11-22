# wasm-rtos-hw

**The devices WebAssembly programs reach: pins, buses, displays, input
devices, analog inputs and outputs, PWM, serial ports, pulse trains, 1-Wire
and counters.** A board registers what it has through small ports; programs
open devices by the names of the board (`"led"`, `"i2c0"`, `"main"`) through
the ABI of devices (`wasm-rtos:kernel/gpio@1.8.0` and the others), one owner
at a time. It is the device layer of
[wasm-rtos](https://github.com/wasm-rtos/wasm-rtos), between the interpreter
([wasm-rtos-vm](https://github.com/wasm-rtos/wasm-rtos-vm)) and the
operating system.

```c
static const HwGpioPort gpio = { NULL, pin_configure, pin_write, pin_read };
hw_gpio_port_set(&gpio);
hw_gpio_add_pin("led", 2, HW_GPIO_CAN_OUTPUT, 0);
hw_i2c_port_set(&i2c);
hw_i2c_add_bus("i2c0", 0, 400000);
```

## What makes it different

- **A build carries only what its board has.** A driver joins when the
  board registers its first device; nothing else is linked.
- **Programs never see the chip.** They open `"i2c0"` and address 0x76; on
  an ESP32, a Black Pill or in the browser the same `.wasm` reaches the same
  sensor.
- **Any operating system, or none.** What the devices need from one is
  declared in `hw_host.h` and nothing else: the calling program, its memory,
  waiting, time, handles and saving the whole system. The wasm-rtos kernel
  implements it; a scheduler of your own can too, and `single/` is a simple
  host that runs programs one at a time with no scheduler at all.
- **Binding is the library's.** `hw_driver_resolve` answers a program's
  device imports for `wm_instantiate`; a host adds only its own calls.

## Building

Add the files to your build next to the interpreter:

```sh
cc -std=c11 -I<path to wasm-rtos-vm>/source -c *.c
```

With the wasm-rtos kernel nothing more is needed: it has this library as a
submodule in `hw/`.

`HW_HAS_SYSTEM=0` leaves saving the whole system (deep sleep) out of every
driver, for a host that has none.

Programs include [`include/wasm-rtos-hw.h`](include/wasm-rtos-hw.h): the
devices' imports, constants and helpers, nothing of an operating system.

### A mini OS: the interpreter, the devices and the simple host

[`examples/mini`](examples/mini) runs a program that blinks a pin, waits for
a button and prints through WASI, with no kernel:

```c
hw_single_init(now_ms, idle);
hw_gpio_port_set(&gpio);
hw_gpio_add_pin("led", 13, HW_GPIO_CAN_OUTPUT, 0);
wm_instantiate(&instance, module, runtime, resolve, NULL); /* wm_wasi_resolve, then hw_single_resolve */
hw_single_enter(1, runtime, wm_instance_memory(instance));
wm_set_fuel(runtime, 20);
for (r = wm_call(runtime, entry.as.func, NULL, &result); r == wm_fuel_exhausted; r = wm_resume(runtime, &result))
{
    hw_drivers_poll();
    wm_set_fuel(runtime, 20);
}
```

On an ATmega328P it takes 48 KB of flash and 1.4 KB of static RAM (39 KB
and 0.9 KB without WASI): more than an Arduino Uno's 32 KB, so on 8-bit
boards it is for an ATmega2560 or larger.

## Devices

| Interface | Joins with | Limits (default) |
|---|---|---|
| `gpio` | `hw_gpio_add_pin` | `HW_GPIO_MAX_PINS` / `HW_GPIO_MAX_NAMES` (32 / 64): pins and pin names |
| `display` | `hw_display_add` | `HW_DISPLAY_MAX` (2) |
| `input` | `hw_input_add` | `HW_INPUT_MAX` / `HW_INPUT_QUEUE` / `HW_INPUT_MAX_HELD` (8 / 32 / 4): devices, events queued per device (16 bytes each), keys tracked for long press and repeat |
| `i2c` | `hw_i2c_add_bus` | `HW_I2C_MAX_BUSES` / `HW_I2C_MAX_DEVICES` (2 / 16): buses, and addresses programs hold open at once |
| `spi` | `hw_spi_add_bus` | `HW_SPI_MAX_BUSES` / `HW_SPI_MAX_DEVICES` (2 / 8) |
| `onewire` | `hw_onewire_add_bus` | `HW_ONEWIRE_MAX_BUSES` / `HW_ONEWIRE_MAX_DEVICES` (2 / 8) |
| | | `HW_BUS_MAX_OPS` (16): ops in one bus transaction |
| `adc` | `hw_adc_add_channel` | `HW_ADC_MAX` (8) |
| `dac` | `hw_dac_add_channel` | `HW_DAC_MAX` (4) |
| `pwm` | `hw_pwm_port_set` | `HW_PWM_MAX` (8): outputs at once, on GPIO pins with `HW_GPIO_CAN_PWM` |
| `pulse` | `hw_pulse_port_set` | `HW_PULSE_MAX` (4): channels at once, on GPIO pins with `HW_GPIO_CAN_PULSE` |
| `uart` | `hw_uart_add` | `HW_UART_MAX` / `HW_UART_RX_BUFFER` (4 / 256): ports, and received bytes kept per port |
| `counter` | `hw_counter_add` | `HW_COUNTER_MAX` / `HW_COUNTER_POLL_MS` (4 / 10): counters, and how often the library reads watched counters whose hardware cannot watch |

The `*_MAX` limits are slots: each costs a pointer. A pin, display, input
device, bus, serial port or analog channel takes memory when the platform
registers it, an input device's event queue while a program owns it, and a
serial port's receive buffer from the first time a program opens it (it
stays, as interrupt handlers write into it). A board with a few devices
spends a few hundred bytes on them.

A driver of your own is a `HwDriver` added with `hw_driver_add`: see
[docs/porting.md](docs/porting.md#drivers-of-your-own), which also explains
each port.

## Files

| File | |
|---|---|
| `hw.h` | ports and registration, for a board |
| `hw_driver.h` | drivers: their calls, the registry, what every driver's calls share |
| `hw_host.h` | what the devices need from the operating system |
| `single/hw_host_single.h` | a simple host: one program at a time, no scheduler |
| `include/wasm-rtos-hw.h` | for programs: the devices' imports |
| `hw_isr.h` | flags and counters interrupt handlers set |
| `hw_internal.h` | what one device file calls in another |
| `docs/api-1.0.txt` | the public names at 1.0, checked by `tools/check-api.py` |

## License

Public domain, as the rest of wasm-rtos.
