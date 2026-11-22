/* The mini OS's size on an AVR (CI builds it for an ATmega2560: it does not fit an Uno's 32 KB): main.c's system with a port of its pins left to the board. */
#include "single/hw_host_single.h"
#include "wm_wasi.h"
#include <avr/pgmspace.h>
#define BLINK_SECTION PROGMEM
#include "blink.wasm.h"

static volatile uint32_t g_ms;
static HwStatus pin_configure(void* c, uint32_t pin, uint32_t flags) { (void)c, (void)pin, (void)flags; return HW_STATUS_OK; }
static HwStatus pin_write(void* c, uint32_t pin, uint8_t level) { (void)c, (void)pin, (void)level; return HW_STATUS_OK; }
static HwStatus pin_read(void* c, uint32_t pin, uint8_t* out) { (void)c, (void)pin; *out = 1; return HW_STATUS_OK; }
static uint32_t now_ms(void) { return g_ms; }

/* WASI output dropped (a port sends it to a serial line). */
static const WmWasiHost g_wasi = {0};
static WmResult resolve(void* c, const char* m, uint32_t ml, const char* n, uint32_t nl, uint8_t k, WmExtern* out)
{
    WmResult r = wm_wasi_resolve(&g_wasi, m, ml, n, nl, &out->as.func);
    if (r || out->as.func)
    {
        out->kind = WM_EXTERN_FUNC;
        return r;
    }
    return hw_single_resolve(c, m, ml, n, nl, k, out);
}

int main(void)
{
    static const HwGpioPort gpio = {0, pin_configure, pin_write, pin_read};
    WmModule* module;
    WmInstance* instance;
    WmRuntime* runtime = wm_runtime_new(256, 8);
    WmExtern entry;
    uint32_t result = 0;
    WmResult r;
    hw_single_init(now_ms, 0);
    hw_gpio_port_set(&gpio);
    hw_gpio_add_pin("led", 13, HW_GPIO_CAN_OUTPUT, 0);
    hw_gpio_add_pin("button", 2, HW_GPIO_CAN_INPUT | HW_GPIO_CAN_PULL_UP | HW_GPIO_CAN_EDGES, 0);
    if (wm_module_load(&module, blink_wasm, blink_wasm_len) || wm_instantiate(&instance, module, runtime, resolve, 0) ||
        wm_instance_export(instance, "app_main", 8, &entry))
        return 1;
    hw_single_enter(1, runtime, wm_instance_memory(instance));
    wm_set_fuel(runtime, 100);
    for (r = wm_call(runtime, entry.as.func, 0, &result); r == wm_fuel_exhausted; r = wm_resume(runtime, &result))
    {
        hw_drivers_poll();
        wm_set_fuel(runtime, 100);
    }
    return (int)result;
}
