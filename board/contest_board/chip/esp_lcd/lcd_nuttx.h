/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#ifndef __containerof
#define __containerof(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static inline void lcd_nuttx_delay_ms(unsigned int ms)
{
    if (ms != 0) {
        nxsig_usleep((useconds_t)ms * 1000);
    }
}

/* Private compatibility names: only the vendored LCD sources include this. */
#define pdMS_TO_TICKS(ms) (ms)
#define vTaskDelay(ms) lcd_nuttx_delay_ms(ms)
#define portYIELD_FROM_ISR() do { } while (0) /* NuttX reschedules on IRQ exit. */

/* Optional accelerator backend is not part of this board port yet. */
#define LCD_NUTTX_DMA2D 0
