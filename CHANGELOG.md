# Changelog

## Unreleased

- The devices of wasm-rtos as a library of their own (wasm-rtos decision
  14): moved out of the kernel with the prefix `hw_`, nothing they do
  changed. What they need from an operating system is `hw_host.h`.
- Each layer holds what is its own (wasm-rtos decision 15):
  - `hw_driver_resolve` binds the devices' imports for `wm_instantiate`;
    `hw_driver_find_call`, `hw_abi_module` and `hw_signature` for a host
    that makes its functions itself.
  - `include/wasm-rtos-hw.h`: the program's header for the devices.
  - `single/`: a simple host, programs one at a time with no scheduler;
    `examples/mini` runs the interpreter and the devices without a kernel,
    on a computer and (size only) on an ATmega328P, in CI.
  - `HW_HAS_SYSTEM=0` leaves saving the system out of every driver.
  - `hw_isr.h`: one layer for what interrupt handlers set, also on AVR
    (no 32-bit atomics there), instead of five copies.
  - The public API at 1.0 in `docs/api-1.0.txt`, checked in CI: names
    are only added.
