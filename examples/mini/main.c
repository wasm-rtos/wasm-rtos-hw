/*
 * A mini OS: the interpreter (wasm-rtos-vm), the devices (wasm-rtos-hw) and
 * the simple host, no kernel. One program runs in slices of fuel; its pins
 * are a pretend board here (a real port drives GPIO registers instead), and
 * the button is pressed once while the program waits for it.
 *
 * Build: this file, the devices' sources, single/hw_host_single.c and the
 * interpreter's source/wm_*.c, with -DHW_HAS_SYSTEM=0 (see build.sh).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "single/hw_host_single.h"
#define BLINK_SECTION
#include "blink.wasm.h"

enum
{
    LED = 13,
    BUTTON = 2
};

static uint8_t g_levels[32];
static unsigned g_led_on, g_idles;

static HwStatus pin_configure(void* context, uint32_t pin, uint32_t flags)
{
    (void)context;
    if (pin == BUTTON)
        g_levels[pin] = 1; /* pulled up */
    (void)flags;
    return HW_STATUS_OK;
}

static HwStatus pin_write(void* context, uint32_t pin, uint8_t level)
{
    (void)context;
    if (pin == LED && level && !g_levels[pin])
        ++g_led_on;
    g_levels[pin] = level;
    return HW_STATUS_OK;
}

static HwStatus pin_read(void* context, uint32_t pin, uint8_t* out_level)
{
    (void)context;
    *out_level = g_levels[pin];
    return HW_STATUS_OK;
}

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* While a call waits: on the third idle, the button goes down (an interrupt handler would call hw_gpio_edge). */
static void idle(void)
{
    struct timespec t = {0, 100000};
    if (++g_idles == 3)
    {
        g_levels[BUTTON] = 0;
        hw_gpio_edge(BUTTON, HW_GPIO_EDGE_FALLING);
    }
    nanosleep(&t, NULL);
}

int main(void)
{
    static const HwGpioPort gpio = {NULL, pin_configure, pin_write, pin_read};
    WmModule* module = NULL;
    WmInstance* instance = NULL;
    WmRuntime* runtime;
    WmExtern entry;
    WmResult r;
    uint32_t result = 0;
    unsigned long slices = 0;

    hw_single_init(now_ms, idle);
    hw_gpio_port_set(&gpio);
    hw_gpio_add_pin("led", LED, HW_GPIO_CAN_OUTPUT, 0);
    hw_gpio_add_pin("button", BUTTON, HW_GPIO_CAN_INPUT | HW_GPIO_CAN_PULL_UP | HW_GPIO_CAN_EDGES, 0);

    runtime = wm_runtime_new(1024, 16);
    if ((r = wm_module_load(&module, blink_wasm, blink_wasm_len)) ||
        (r = wm_instantiate(&instance, module, runtime, hw_single_resolve, NULL)) ||
        (r = wm_instance_export(instance, "app_main", 8, &entry)))
    {
        printf("load: %s\n", r);
        return 1;
    }
    hw_single_enter(1, runtime, wm_instance_memory(instance));
    wm_set_fuel(runtime, 20);
    r = wm_call(runtime, entry.as.func, NULL, &result);
    while (r == wm_fuel_exhausted) /* the whole scheduler: one program, slices of fuel */
    {
        ++slices;
        hw_drivers_poll();
        wm_set_fuel(runtime, 20);
        r = wm_resume(runtime, &result);
    }
    hw_single_leave(1);
    printf("blink: %s, result %d (presses), led on %u times, %lu slices\n", r ? r : "ended", (int)result, g_led_on,
           slices);
    wm_instance_free(instance);
    wm_module_free(module);
    wm_runtime_free(runtime);
    hw_single_shutdown();
    return r != NULL || (int)result != 1 || g_led_on != 5;
}
