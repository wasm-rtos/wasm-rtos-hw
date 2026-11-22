/*
 * A simple host for the devices (hw_host.h): programs run one at a time,
 * no scheduler, no deep sleep. A host of its own (a mini OS on an 8-bit
 * board, a test) takes the interpreter, the devices and this file:
 *
 *   hw_single_init(now_ms, idle);
 *   ... register ports and devices ...
 *   wm_instantiate(&instance, module, runtime, hw_single_resolve, NULL);
 *   hw_single_enter(1, runtime, wm_instance_memory(instance));
 *   result = wm_call(runtime, entry, NULL, NULL);    (or in slices of fuel)
 *   hw_single_leave(1);                               (the program ended: its devices close)
 *
 * A call that waits (a serial port with no room, a bus transfer, wait-any)
 * waits right there, polling the drivers and idling between, and returns
 * what the operating system would have delivered when the program woke.
 * Besides the devices' calls, programs get handle-close and wait-any for
 * the devices' handles (hw_single_resolve).
 */
#include "hw_host_single.h"

#include <stdlib.h>
#include <string.h>

#if HW_HAS_SYSTEM
#error "hw_host_single.c saves no system: build the devices with -DHW_HAS_SYSTEM=0"
#endif

#ifndef HW_SINGLE_HANDLES
#define HW_SINGLE_HANDLES 16U
#endif

typedef struct HwSingleHandle
{
    const HwDriver* driver;
    uint32_t id;
    uint32_t task;
} HwSingleHandle;

struct HwGuestKept
{
    HwGuestKept* next;
    HwGuestBuffer buffer;
};

static struct
{
    uint32_t (*now_ms)(void);
    void (*idle)(void);
    uint32_t task;
    WmRuntime* runtime;
    WmMemory* memory;
    /* The call in progress: where its result goes, and what its wait came to. */
    uint32_t* slot;
    uint8_t waited;
    HwStatus status;
    uint32_t value;
    /* A wait for a signal or a transfer: its key (0: a transfer) and whether it ended. */
    uint32_t key;
    uint8_t woken;
    uint32_t last_ms;
    uint64_t high_ms;
    HwSingleHandle handles[HW_SINGLE_HANDLES];
    WmFunc* close_func;
    WmFunc* wait_func;
} g_single;

void hw_single_init(uint32_t (*now_ms)(void), void (*idle)(void))
{
    g_single.now_ms = now_ms;
    g_single.idle = idle;
}

void hw_single_enter(uint32_t task, WmRuntime* runtime, WmMemory* memory)
{
    g_single.task = task;
    g_single.runtime = runtime;
    g_single.memory = memory;
}

void hw_single_leave(uint32_t task)
{
    hw_drivers_finish_task(task);
    for (uint32_t i = 0U; i < HW_SINGLE_HANDLES; ++i)
    {
        HwSingleHandle* h = &g_single.handles[i];
        if (h->driver != NULL && h->task == task)
        {
            if (h->driver->close != NULL)
            {
                h->driver->close(h->id);
            }
            h->driver = NULL;
        }
    }
}

/* ---- The calling program --------------------------------------------------- */

uint32_t hw_host_task_id(void) { return g_single.task; }

uint64_t hw_host_time_ms64(void)
{
    uint32_t now = g_single.now_ms != NULL ? g_single.now_ms() : 0U;
    if (now < g_single.last_ms)
    {
        g_single.high_ms += (uint64_t)1U << 32;
    }
    g_single.last_ms = now;
    return g_single.high_ms | now;
}

uint32_t hw_host_tick_ms(void) { return (uint32_t)hw_host_time_ms64(); }

/* ---- Its memory ------------------------------------------------------------ */

static HwStatus hw_single_fault(WmResult result)
{
    if (result != NULL && g_single.runtime != NULL)
    {
        (void)wm_trap(g_single.runtime, result);
    }
    return result == NULL ? HW_STATUS_OK : HW_STATUS_OUT_OF_BOUNDS;
}

HwStatus hw_host_guest_check(uint32_t address, uint32_t length)
{
    return hw_host_memory_in_bounds(g_single.memory, address, length) ? HW_STATUS_OK : HW_STATUS_OUT_OF_BOUNDS;
}

HwStatus hw_host_guest_read(uint32_t address, void* out, uint32_t length)
{
    if (!hw_host_memory_in_bounds(g_single.memory, address, length))
    {
        return HW_STATUS_OUT_OF_BOUNDS;
    }
    return hw_single_fault(wm_memory_read(g_single.memory, address, out, length));
}

HwStatus hw_host_guest_write(uint32_t address, const void* data, uint32_t length)
{
    if (!hw_host_memory_in_bounds(g_single.memory, address, length))
    {
        return HW_STATUS_OUT_OF_BOUNDS;
    }
    return hw_single_fault(wm_memory_write(g_single.memory, address, data, length));
}

/* In place when the memory is one block; else a copy, written back on close when the port writes it. */
HwStatus hw_host_buffer_open(HwGuestBuffer* buffer, uint32_t address, uint32_t length, uint8_t access)
{
    uint32_t size = 0U;
    uint8_t* base;
    memset(buffer, 0, sizeof(*buffer));
    buffer->address = address;
    buffer->length = length;
    buffer->task_id = g_single.task;
    buffer->access = access;
    if (!hw_host_memory_in_bounds(g_single.memory, address, length))
    {
        return HW_STATUS_OUT_OF_BOUNDS;
    }
    if (length == 0U)
    {
        return HW_STATUS_OK;
    }
    base = (uint8_t*)wm_memory_data(g_single.memory, &size);
    if (base != NULL && (uint64_t)address + length <= size)
    {
        buffer->data = base + address;
        return HW_STATUS_OK;
    }
    if ((buffer->data = (uint8_t*)malloc(length)) == NULL)
    {
        return HW_STATUS_OUT_OF_MEMORY;
    }
    buffer->copied = 1U;
    return hw_single_fault(wm_memory_read(g_single.memory, address, buffer->data, length));
}

HwStatus hw_host_buffer_close(HwGuestBuffer* buffer)
{
    HwStatus status = HW_STATUS_OK;
    if (buffer->copied)
    {
        if (buffer->access & HW_GUEST_OUT)
        {
            status = hw_single_fault(wm_memory_write(g_single.memory, buffer->address, buffer->data, buffer->length));
        }
        free(buffer->data);
    }
    buffer->data = NULL;
    return status;
}

void hw_host_buffer_drop(HwGuestBuffer* buffer)
{
    if (buffer->copied)
    {
        free(buffer->data);
    }
    buffer->data = NULL;
}

void hw_host_buffer_keep(HwGuestBuffer* buffer, HwGuestKept** list)
{
    HwGuestKept* kept = (HwGuestKept*)malloc(sizeof(HwGuestKept));
    if (kept == NULL)
    {
        hw_host_buffer_drop(buffer);
        return;
    }
    kept->buffer = *buffer;
    kept->next = *list;
    *list = kept;
    buffer->data = NULL;
    buffer->copied = 0U;
}

HwStatus hw_host_kept_close(HwGuestKept** list)
{
    HwStatus status = HW_STATUS_OK;
    while (*list != NULL)
    {
        HwGuestKept* kept = *list;
        HwStatus one = hw_host_buffer_close(&kept->buffer);
        if (status == HW_STATUS_OK)
        {
            status = one;
        }
        *list = kept->next;
        free(kept);
    }
    return status;
}

/* ---- Waiting: right there, polling the drivers -------------------------------- */

HwStatus hw_host_guest_begin(uint32_t* raw_return)
{
    g_single.slot = raw_return;
    g_single.waited = 0U;
    return HW_STATUS_OK;
}

WmResult hw_host_guest_end(void)
{
    if (g_single.waited && g_single.slot != NULL)
    {
        *(int32_t*)(void*)g_single.slot =
            g_single.status == HW_STATUS_OK ? (int32_t)(g_single.value & 0x7FFFFFFFU) : hw_abi_error(g_single.status);
    }
    g_single.slot = NULL;
    g_single.waited = 0U;
    return NULL;
}

/* Poll until woken or timeout_ms passed (HW_WAIT_FOREVER: no limit). */
static void hw_single_wait(uint32_t timeout_ms)
{
    uint64_t start = hw_host_time_ms64();
    g_single.woken = 0U;
    g_single.status = HW_STATUS_OK;
    g_single.value = 0U;
    for (;;)
    {
        hw_drivers_time();
        hw_drivers_poll();
        if (g_single.woken)
        {
            break;
        }
        if (timeout_ms != HW_WAIT_FOREVER && hw_host_time_ms64() - start >= timeout_ms)
        {
            g_single.status = HW_STATUS_TIMEOUT;
            break;
        }
        if (g_single.idle != NULL)
        {
            g_single.idle();
        }
    }
    g_single.waited = 1U;
}

HwStatus hw_host_signal_wait(uint32_t key, uint32_t timeout_ms)
{
    g_single.key = key;
    hw_single_wait(timeout_ms);
    g_single.key = 0U;
    return HW_STATUS_OK;
}

void hw_host_signal_wake(uint32_t key)
{
    if (key != 0U && key == g_single.key)
    {
        g_single.woken = 1U;
    }
}

void hw_host_wait_any_recheck(void) {}

HwStatus hw_host_transfer_wait_begin(void)
{
    g_single.key = 0U;
    hw_single_wait(HW_WAIT_FOREVER);
    return HW_STATUS_OK;
}

void hw_host_transfer_wait_end(uint32_t task_id, HwStatus status, uint32_t value)
{
    (void)task_id;
    g_single.woken = 1U;
    g_single.status = status;
    g_single.value = value;
}

/* ---- Handles of the devices ------------------------------------------------- */

int32_t hw_driver_handle_new(const HwDriver* driver, uint32_t id)
{
    for (uint32_t i = 0U; i < HW_SINGLE_HANDLES; ++i)
    {
        if (g_single.handles[i].driver == NULL)
        {
            g_single.handles[i].driver = driver;
            g_single.handles[i].id = id;
            g_single.handles[i].task = g_single.task;
            return (int32_t)i + 1;
        }
    }
    return -WR_ELIMIT;
}

int hw_driver_handle_get(const HwDriver* driver, int32_t handle, uint32_t* out_id)
{
    HwSingleHandle* h = handle >= 1 && handle <= (int32_t)HW_SINGLE_HANDLES ? &g_single.handles[handle - 1] : NULL;
    if (h == NULL || h->driver != driver || h->task != g_single.task)
    {
        return 0;
    }
    *out_id = h->id;
    return 1;
}

static HwSingleHandle* hw_single_handle(int32_t handle)
{
    HwSingleHandle* h = handle >= 1 && handle <= (int32_t)HW_SINGLE_HANDLES ? &g_single.handles[handle - 1] : NULL;
    return h != NULL && h->driver != NULL && h->task == g_single.task ? h : NULL;
}

/* handle-close: the device goes back to its driver. */
static HW_HOST_FUNCTION(hw_single_close)
{
    HW_HOST_RETURNS(int32_t);
    HW_HOST_ARG(int32_t, handle);
    HwSingleHandle* h = hw_single_handle(handle);
    HW_ABI_UNUSED();
    if (h == NULL)
    {
        HW_HOST_RETURN(-WR_EBADH);
    }
    if (h->driver->close != NULL)
    {
        h->driver->close(h->id);
    }
    h->driver = NULL;
    HW_HOST_RETURN(0);
}

/* wait-any: the index of the first device handle ready, polling the drivers between. */
static HW_HOST_FUNCTION(hw_single_wait_any)
{
    HW_HOST_RETURNS(int32_t);
    HW_HOST_ARG(uint32_t, handles_address);
    HW_HOST_ARG(uint32_t, count);
    HW_HOST_ARG(uint32_t, timeout_ms);
    int32_t handles[HW_SINGLE_HANDLES];
    uint64_t start = hw_host_time_ms64();
    HW_ABI_UNUSED();
    if (count == 0U || count > HW_SINGLE_HANDLES)
    {
        HW_HOST_RETURN(-WR_EINVAL);
    }
    if (hw_host_guest_read(handles_address, handles, count * (uint32_t)sizeof(int32_t)) != HW_STATUS_OK)
    {
        HW_HOST_RETURN(-WR_EFAULT);
    }
    for (;;)
    {
        hw_drivers_time();
        hw_drivers_poll();
        for (uint32_t i = 0U; i < count; ++i)
        {
            HwSingleHandle* h = hw_single_handle(handles[i]);
            if (h == NULL || h->driver->ready == NULL)
            {
                HW_HOST_RETURN(-WR_EBADH);
            }
            if (h->driver->ready(h->id))
            {
                HW_HOST_RETURN((int32_t)i);
            }
        }
        if (timeout_ms != HW_WAIT_FOREVER && hw_host_time_ms64() - start >= timeout_ms)
        {
            HW_HOST_RETURN(-WR_ETIMEOUT);
        }
        if (g_single.idle != NULL)
        {
            g_single.idle();
        }
    }
}

/* The devices' imports, and handle-close and wait-any, for wm_instantiate. */
WmResult hw_single_resolve(void* context, const char* module, uint32_t module_length, const char* name,
                           uint32_t name_length, uint8_t kind, WmExtern* out)
{
    static const uint8_t i32[] = {WM_I32, WM_I32, WM_I32};
    const char* interface = NULL;
    uint32_t interface_length = 0U, minor = 0U;
    out->kind = kind;
    if (kind == WM_EXTERN_FUNC && hw_abi_module(module, module_length, &interface, &interface_length, &minor))
    {
        if (interface_length == 6U && memcmp(interface, "handle", 6U) == 0 && name_length == 5U &&
            memcmp(name, "close", 5U) == 0)
        {
            if (g_single.close_func == NULL && !(g_single.close_func = wm_host_function_new(i32, 1, i32, 1, hw_single_close, NULL)))
            {
                return "exhaustion: out of memory";
            }
            out->as.func = g_single.close_func;
            return NULL;
        }
        if (interface_length == 4U && memcmp(interface, "wait", 4U) == 0 && name_length == 3U && memcmp(name, "any", 3U) == 0)
        {
            if (g_single.wait_func == NULL && !(g_single.wait_func = wm_host_function_new(i32, 3, i32, 1, hw_single_wait_any, NULL)))
            {
                return "exhaustion: out of memory";
            }
            out->as.func = g_single.wait_func;
            return NULL;
        }
    }
    return hw_driver_resolve(context, module, module_length, name, name_length, kind, out);
}

/* Everything the host and the devices hold, given back. */
void hw_single_shutdown(void)
{
    hw_single_leave(g_single.task);
    hw_drivers_shutdown();
    wm_host_function_free(g_single.close_func);
    wm_host_function_free(g_single.wait_func);
    memset(&g_single, 0, sizeof(g_single));
}
