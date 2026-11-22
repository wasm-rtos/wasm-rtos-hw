/*
 * A program for the mini OS: it blinks "led" five times, waiting on
 * "button" between blinks (a press, or 10 ms), and returns how many
 * presses it saw. The devices' calls and one WASI call (fd_write, which
 * the interpreter answers): no operating system needed.
 */
#include "wasm-rtos-hw.h"

__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write")))
int32_t fd_write(int32_t fd, const void* iovs, int32_t count, uint32_t* written);

static void say(const char* text)
{
    struct { const char* data; uint32_t length; } iov = {text, wr_strlen(text)};
    uint32_t written;
    fd_write(1, &iov, 1, &written);
}

int app_main(void)
{
    int32_t led = wr_gpio_open_cstr("led", WR_GPIO_OUTPUT);
    int32_t button = wr_gpio_open_cstr("button", WR_GPIO_PULL_UP);
    int presses = 0;
    if (led < 0 || button < 0)
        return -1;
    say("blink: start\n");
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
