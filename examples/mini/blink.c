/*
 * A program for the mini OS: it blinks "led" five times, waiting on
 * "button" between blinks (a press, or 10 ms), and returns how many
 * presses it saw. Only the devices' calls: no operating system needed.
 */
#include "wasm-rtos-hw.h"

int app_main(void)
{
    int32_t led = wr_gpio_open_cstr("led", WR_GPIO_OUTPUT);
    int32_t button = wr_gpio_open_cstr("button", WR_GPIO_PULL_UP);
    int presses = 0;
    if (led < 0 || button < 0)
        return -1;
    wr_gpio_watch(button, WR_GPIO_FALLING);
    for (int i = 0; i < 5; ++i)
    {
        wr_gpio_set(led, 1);
        if (wr_wait_any(&button, 1, 10) == 0)
            presses += wr_gpio_events(button);
        wr_gpio_set(led, 0);
    }
    wr_close(button);
    wr_close(led);
    return presses;
}
