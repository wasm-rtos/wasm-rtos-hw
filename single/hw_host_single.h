#ifndef WASM_RTOS_HW_HOST_SINGLE_H
#define WASM_RTOS_HW_HOST_SINGLE_H

/*
 * A simple host for the devices: programs run one at a time, no scheduler,
 * no deep sleep (build the devices with -DHW_HAS_SYSTEM=0). With it the
 * interpreter and the devices make a small system of their own, as on an
 * 8-bit board. See hw_host_single.c.
 */

#include "../hw_driver.h"

/* now_ms: a millisecond clock; idle: what to do while a call waits (NULL: spin). */
void hw_single_init(uint32_t (*now_ms)(void), void (*idle)(void));
/* The program whose calls come next: its id (not 0), runtime and memory. */
void hw_single_enter(uint32_t task, WmRuntime* runtime, WmMemory* memory);
/* The program ended: its devices close and its transfers end. */
void hw_single_leave(uint32_t task);
/* A resolver for wm_instantiate: the devices' imports, handle-close and wait-any. */
WmResult hw_single_resolve(void* context, const char* module, uint32_t module_length, const char* name,
                           uint32_t name_length, uint8_t kind, WmExtern* out);
/* Everything given back. */
void hw_single_shutdown(void);

#endif
