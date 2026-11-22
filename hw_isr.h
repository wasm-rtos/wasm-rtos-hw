#ifndef WASM_RTOS_HW_ISR_H
#define WASM_RTOS_HW_ISR_H

/*
 * Flags and counters an interrupt handler sets and the library takes: one
 * place for what every device needs between a port's interrupt and the
 * next scheduling decision.
 *
 *   HwIsrFlag   set by a handler, taken (read and cleared) by the library
 *   HwIsrCount  added to by a handler; loaded, stored and taken by the library
 *
 * Built on C11 atomics where the compiler has them lock-free; on AVR, which
 * has no atomic 32-bit operations, each is a short section with interrupts
 * off; a port may give its own section (HW_ISR_ENTER and HW_ISR_EXIT, both
 * or neither). Without atomics and without a section (a host with no
 * interrupts) they are plain volatile values.
 */

#include <stdint.h>

#if defined(HW_ISR_ENTER) != defined(HW_ISR_EXIT)
#error "a port gives both HW_ISR_ENTER and HW_ISR_EXIT, or neither"
#endif

#if !defined(HW_ISR_ENTER) && defined(__AVR__)
#include <avr/io.h>
#include <avr/interrupt.h>
#define HW_ISR_ENTER() uint8_t hw_isr_sreg_ = SREG; cli()
#define HW_ISR_EXIT() SREG = hw_isr_sreg_
#endif

#if defined(HW_ISR_ENTER) || defined(__STDC_NO_ATOMICS__)

typedef volatile uint8_t HwIsrFlag;
typedef volatile uint32_t HwIsrCount;

#ifndef HW_ISR_ENTER
#define HW_ISR_ENTER() (void)0
#define HW_ISR_EXIT() (void)0
#endif

static inline void hw_isr_flag_set(HwIsrFlag* flag) { *flag = 1U; }
static inline uint8_t hw_isr_flag_take(HwIsrFlag* flag)
{
    uint8_t value;
    HW_ISR_ENTER();
    value = *flag;
    *flag = 0U;
    HW_ISR_EXIT();
    return value;
}

static inline uint32_t hw_isr_count_load(HwIsrCount* count)
{
    uint32_t value;
    HW_ISR_ENTER();
    value = *count;
    HW_ISR_EXIT();
    return value;
}
static inline void hw_isr_count_store(HwIsrCount* count, uint32_t value)
{
    HW_ISR_ENTER();
    *count = value;
    HW_ISR_EXIT();
}
static inline void hw_isr_count_add(HwIsrCount* count, uint32_t amount)
{
    HW_ISR_ENTER();
    *count += amount;
    HW_ISR_EXIT();
}
static inline uint32_t hw_isr_count_take(HwIsrCount* count)
{
    uint32_t value;
    HW_ISR_ENTER();
    value = *count;
    *count = 0U;
    HW_ISR_EXIT();
    return value;
}

#else

#include <stdatomic.h>

typedef atomic_uchar HwIsrFlag;
typedef atomic_uint_fast32_t HwIsrCount;

static inline void hw_isr_flag_set(HwIsrFlag* flag) { atomic_store_explicit(flag, 1U, memory_order_release); }
static inline uint8_t hw_isr_flag_take(HwIsrFlag* flag)
{
    return (uint8_t)atomic_exchange_explicit(flag, 0U, memory_order_acquire);
}

static inline uint32_t hw_isr_count_load(HwIsrCount* count)
{
    return (uint32_t)atomic_load_explicit(count, memory_order_acquire);
}
static inline void hw_isr_count_store(HwIsrCount* count, uint32_t value)
{
    atomic_store_explicit(count, value, memory_order_release);
}
static inline void hw_isr_count_add(HwIsrCount* count, uint32_t amount)
{
    (void)atomic_fetch_add_explicit(count, amount, memory_order_relaxed);
}
static inline uint32_t hw_isr_count_take(HwIsrCount* count)
{
    return (uint32_t)atomic_exchange_explicit(count, 0U, memory_order_acq_rel);
}

#endif

#endif
