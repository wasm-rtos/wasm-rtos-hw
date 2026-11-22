/*
 * The registry of drivers and what every driver's calls share: opening by
 * name, properties, ABI errors, names from a program's memory.
 */

#include "hw_driver.h"

#include <stdlib.h>

/* The trap HW_HOST_CHECK_ADDR returns. */
const char hw_host_out_of_bounds[] = "trap: out of bounds memory access";

int32_t hw_abi_error(HwStatus status)
{
    switch (status)
    {
        case HW_STATUS_OK: return 0;
        case HW_STATUS_OUT_OF_MEMORY: return -WR_ENOMEM;
        case HW_STATUS_WASM_ERROR: return -WR_EBADPROGRAM;
        case HW_STATUS_TASK_DEAD: return -WR_EDEAD;
        case HW_STATUS_TASK_NOT_FOUND:
        case HW_STATUS_QUEUE_NOT_FOUND:
        case HW_STATUS_MUTEX_NOT_FOUND:
        case HW_STATUS_SEMAPHORE_NOT_FOUND:
        case HW_STATUS_EVENT_GROUP_NOT_FOUND:
        case HW_STATUS_TIMER_NOT_FOUND: return -WR_EBADH;
        case HW_STATUS_BUFFER_TOO_SMALL: return -WR_ETOOLONG;
        case HW_STATUS_UNSUPPORTED:
        case HW_STATUS_READ_ONLY: return -WR_EUNSUPPORTED;
        case HW_STATUS_OUT_OF_BOUNDS: return -WR_EFAULT;
        case HW_STATUS_QUEUE_FULL:
        case HW_STATUS_SEMAPHORE_FULL: return -WR_EFULL;
        case HW_STATUS_QUEUE_EMPTY: return -WR_EEMPTY;
        case HW_STATUS_TIMEOUT: return -WR_ETIMEOUT;
        case HW_STATUS_BUSY: return -WR_EBUSY;
        case HW_STATUS_NOT_OWNER: return -WR_ENOTOWNER;
        case HW_STATUS_ABORTED: return -WR_EABORTED;
        case HW_STATUS_LIMIT_REACHED: return -WR_ELIMIT;
        case HW_STATUS_FILE_NOT_FOUND:
        case HW_STATUS_END_OF_DIRECTORY: return -WR_ENOTFOUND;
        case HW_STATUS_NACK: return -WR_ENACK;
        case HW_STATUS_BUS_ERROR: return -WR_EBUS;
        case HW_STATUS_NOT_A_DIRECTORY: return -WR_ENOTDIR;
        case HW_STATUS_IS_A_DIRECTORY: return -WR_EISDIR;
        default: return -WR_EINVAL;
    }
}

int32_t hw_driver_property(HwStatus status, uint32_t value)
{
    return status == HW_STATUS_OK ? (int32_t)(value & 0x7FFFFFFFU) : hw_abi_error(status);
}

int32_t hw_driver_open(const HwDriver* driver, uint32_t name_address, uint32_t name_length,
                       HwStatus (*opener)(const char* name, uint32_t length, void* context, uint32_t* out_id),
                       void* context)
{
    const char* name = NULL;
    char name_storage[HW_ABI_NAME_MAX];
    uint32_t id = 0U;
    int32_t handle = hw_abi_read_name(name_address, name_length, name_storage, &name);
    HwStatus status = HW_STATUS_OK;

    if (handle < 0)
    {
        return handle;
    }
    if (name == NULL)
    {
        return -WR_EINVAL;
    }
    status = opener(name, name_length, context, &id);
    if (status != HW_STATUS_OK)
    {
        return status == HW_STATUS_BUSY ? -WR_EINUSE : hw_abi_error(status);
    }
    handle = hw_driver_handle_new(driver, id);
    if (handle < 0 && driver->close != NULL)
    {
        driver->close(id);
    }
    return handle;
}

static const HwDriver* g_drivers[HW_MAX_DRIVERS];
static uint32_t g_driver_count;

/* The calls hw_driver_resolve made into functions, kept until shutdown. */
static struct
{
    const HwDriverCall* call;
    WmFunc* func;
}* g_bound;
static uint32_t g_bound_count, g_bound_room;

HwStatus hw_driver_add(const HwDriver* driver)
{
    uint32_t index = 0U;

    if (driver == NULL || driver->interface == NULL)
    {
        return HW_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < g_driver_count; ++index)
    {
        if (g_drivers[index] == driver)
        {
            return HW_STATUS_OK;
        }
    }
    if (g_driver_count == HW_MAX_DRIVERS)
    {
        return HW_STATUS_LIMIT_REACHED;
    }
    g_drivers[g_driver_count++] = driver;
    return HW_STATUS_OK;
}

uint32_t hw_driver_count(void)
{
    return g_driver_count;
}

const HwDriver* hw_driver_at(uint32_t index)
{
    return index < g_driver_count ? g_drivers[index] : NULL;
}

const HwDriver* hw_driver_find(const char* interface)
{
    uint32_t index = 0U;

    for (index = 0U; index < g_driver_count; ++index)
    {
        if (strcmp(g_drivers[index]->interface, interface) == 0)
        {
            return g_drivers[index];
        }
    }
    return NULL;
}

uint32_t hw_driver_signal_key(const HwDriver* driver, uint32_t id)
{
    uint32_t index = 0U;

    /* Kernel objects use the low type numbers; drivers take theirs from 0x80 up, by place. */
    while (index < g_driver_count && g_drivers[index] != driver)
    {
        ++index;
    }
    return HW_SIGNAL_KEY(0x80U + index, id);
}

void hw_drivers_poll(void)
{
    uint32_t index = 0U;

    for (index = 0U; index < g_driver_count; ++index)
    {
        if (g_drivers[index]->poll != NULL)
        {
            g_drivers[index]->poll();
        }
    }
}

uint32_t hw_drivers_next_deadline_ms(void)
{
    uint32_t next = UINT32_MAX;
    uint32_t index = 0U;

    for (index = 0U; index < g_driver_count; ++index)
    {
        if (g_drivers[index]->next_deadline_ms != NULL)
        {
            uint32_t due = g_drivers[index]->next_deadline_ms();
            if (due < next)
            {
                next = due;
            }
        }
    }
    return next;
}

void hw_drivers_time(void)
{
    uint32_t index = 0U;

    for (index = 0U; index < g_driver_count; ++index)
    {
        if (g_drivers[index]->time != NULL)
        {
            g_drivers[index]->time();
        }
    }
}

void hw_drivers_finish_task(uint32_t task_id)
{
    uint32_t index = 0U;

    for (index = 0U; index < g_driver_count; ++index)
    {
        if (g_drivers[index]->finish_task != NULL)
        {
            g_drivers[index]->finish_task(task_id);
        }
    }
}

void hw_drivers_shutdown(void)
{
    for (uint32_t i = 0U; i < g_bound_count; ++i)
    {
        wm_host_function_free(g_bound[i].func);
    }
    free(g_bound);
    g_bound = NULL;
    g_bound_count = g_bound_room = 0U;
    while (g_driver_count > 0U)
    {
        const HwDriver* driver = g_drivers[--g_driver_count];
        if (driver->shutdown != NULL)
        {
            driver->shutdown();
        }
    }
}

int hw_abi_utf8_valid(const uint8_t* text, uint32_t length)
{
    uint32_t index = 0U;

    while (index < length)
    {
        uint8_t byte = text[index];
        uint32_t extra = byte < 0x80U ? 0U : (byte & 0xE0U) == 0xC0U ? 1U
                       : (byte & 0xF0U) == 0xE0U ? 2U : (byte & 0xF8U) == 0xF0U ? 3U : 4U;
        uint32_t next = 0U;

        if (extra == 4U || (extra == 1U && byte < 0xC2U) || length - index <= extra)
        {
            return 0U;
        }
        for (next = 1U; next <= extra; ++next)
        {
            if ((text[index + next] & 0xC0U) != 0x80U)
            {
                return 0U;
            }
        }
        index += extra + 1U;
    }
    return 1U;
}

/* An optional object name: (0, 0) means none. */
int32_t hw_abi_read_name(uint32_t address, uint32_t length, char* storage, const char** out_name)
{
    *out_name = NULL;
    if (length == 0U)
    {
        return 0;
    }
    if (length > HW_ABI_NAME_MAX)
    {
        return -WR_ETOOLONG;
    }
    if (hw_host_guest_read(address, storage, length) != HW_STATUS_OK)
    {
        return -WR_EFAULT;
    }
    if (!hw_abi_utf8_valid((const uint8_t*)storage, length))
    {
        return -WR_EINVAL;
    }
    *out_name = storage;
    return 0;
}

int hw_abi_module(const char* module, uint32_t length, const char** out_interface, uint32_t* out_interface_length,
                  uint32_t* out_minor)
{
    const uint32_t prefix_length = (uint32_t)(sizeof(HW_KERNEL_PREFIX) - 1U);
    uint32_t at = prefix_length;
    uint32_t minor = 0U;

    if (length <= prefix_length || memcmp(module, HW_KERNEL_PREFIX, prefix_length) != 0)
    {
        return 0;
    }
    while (at < length && module[at] != '@')
    {
        ++at;
    }
    *out_interface = module + prefix_length;
    *out_interface_length = at - prefix_length;
    if (*out_interface_length == 0U || length < at + 6U || memcmp(module + at, "@1.", 3U) != 0)
    {
        return 0;
    }
    at += 3U;
    if (module[at] < '0' || module[at] > '9' || (module[at] == '0' && module[at + 1U] != '.'))
    {
        return 0;
    }
    while (at < length && module[at] >= '0' && module[at] <= '9' && minor < 1000U)
    {
        minor = minor * 10U + (uint32_t)(module[at++] - '0');
    }
    *out_minor = minor;
    return length - at == 2U && memcmp(module + at, ".0", 2U) == 0;
}

static int hw_name_is(const char* text, uint32_t length, const char* name)
{
    return strlen(name) == length && memcmp(text, name, length) == 0;
}

const HwDriverCall* hw_driver_find_call(const char* interface, uint32_t interface_length, const char* name,
                                        uint32_t name_length, uint32_t minor, int* out_offered)
{
    *out_offered = 0;
    for (uint32_t index = 0U; index < g_driver_count; ++index)
    {
        const HwDriver* driver = g_drivers[index];
        if (!hw_name_is(interface, interface_length, driver->interface))
        {
            continue;
        }
        *out_offered = 1;
        for (uint32_t call = 0U; call < driver->call_count; ++call)
        {
            if (hw_name_is(name, name_length, driver->calls[call].name) && minor >= driver->calls[call].since)
            {
                return &driver->calls[call];
            }
        }
        return NULL;
    }
    return NULL;
}

static uint8_t hw_signature_type(char letter)
{
    switch (letter)
    {
        case 'i': case '*': return WM_I32;
        case 'I': return WM_I64;
        case 'f': return WM_F32;
        case 'F': return WM_F64;
        default: return 0U;
    }
}

int hw_signature(const char* signature, uint8_t params[16], uint32_t* out_param_count, uint8_t* out_result,
                 uint32_t* out_result_count)
{
    const char* at = signature;
    *out_param_count = 0U;
    *out_result_count = 0U;
    if (*at != 'v')
    {
        if ((*out_result = hw_signature_type(*at)) == 0U)
        {
            return -1;
        }
        *out_result_count = 1U;
    }
    if (*++at != '(')
    {
        return -1;
    }
    for (++at; *at != ')'; ++at)
    {
        if (*out_param_count == 16U || hw_signature_type(*at) == 0U)
        {
            return -1;
        }
        params[(*out_param_count)++] = hw_signature_type(*at);
    }
    return 0;
}

WmResult hw_driver_resolve(void* context, const char* module, uint32_t module_length, const char* name,
                           uint32_t name_length, uint8_t kind, WmExtern* out)
{
    const char* interface = NULL;
    uint32_t interface_length = 0U, minor = 0U, param_count = 0U, result_count = 0U;
    uint8_t params[16];
    uint8_t result = 0U;
    int offered = 0;
    const HwDriverCall* call;
    WmFunc* func;
    (void)context;
    out->kind = kind;
    if (kind != WM_EXTERN_FUNC || !hw_abi_module(module, module_length, &interface, &interface_length, &minor) ||
        !(call = hw_driver_find_call(interface, interface_length, name, name_length, minor, &offered)))
    {
        return NULL;
    }
    for (uint32_t i = 0U; i < g_bound_count; ++i)
    {
        if (g_bound[i].call == call)
        {
            out->as.func = g_bound[i].func;
            return NULL;
        }
    }
    if (hw_signature(call->signature, params, &param_count, &result, &result_count) != 0)
    {
        return "invalid driver call signature";
    }
    if (g_bound_count == g_bound_room)
    {
        uint32_t room = g_bound_room ? g_bound_room * 2U : 8U;
        void* grown = realloc(g_bound, room * sizeof(*g_bound));
        if (!grown)
        {
            return "exhaustion: out of memory";
        }
        g_bound = grown;
        g_bound_room = room;
    }
    if (!(func = wm_host_function_new(params, param_count, &result, result_count, call->function, NULL)))
    {
        return "exhaustion: out of memory";
    }
    g_bound[g_bound_count].call = call;
    g_bound[g_bound_count++].func = func;
    out->as.func = func;
    return NULL;
}

