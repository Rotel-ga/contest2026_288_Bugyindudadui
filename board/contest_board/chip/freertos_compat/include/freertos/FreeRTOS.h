/****************************************************************************
 * Minimal FreeRTOS compatibility for ESP32-P4 camera HAL on NuttX
 ****************************************************************************/

#ifndef __ESP32P4_FREERTOS_COMPAT_FREERTOS_H
#define __ESP32P4_FREERTOS_COMPAT_FREERTOS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#define portTICK_PERIOD_MS (CONFIG_USEC_PER_TICK / 1000)
#define pdMS_TO_TICKS(ms) ((TickType_t)MSEC2TICK(ms))
#ifndef portMAX_DELAY
#  define portMAX_DELAY 0xfffffffful
#endif
#ifndef pdTRUE
#  define pdTRUE 1
#endif
#ifndef pdFALSE
#  define pdFALSE 0
#endif
#ifndef pdPASS
#  define pdPASS 0
#endif
#ifndef errQUEUE_FULL
#  define errQUEUE_FULL 0
#endif
#ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef IRAM_ATTR
#  define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#  define DRAM_ATTR
#endif

#ifndef OK
#  define OK 0
#endif

typedef uint32_t TickType_t;
#ifndef ESP_FREERTOS_COMPAT_TYPES_DEFINED
#  define ESP_FREERTOS_COMPAT_TYPES_DEFINED 1
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;
#endif

typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;

/* Recursive IRQ-safe critical sections, shared by task and ISR callers.
 * Use the platform implementation rather than silently ignoring the lock.
 */

typedef struct
{
  rspinlock_t lock;
  irqstate_t flags;
} portMUX_TYPE;

#ifndef portMUX_INITIALIZER_UNLOCKED
#  define portMUX_INITIALIZER_UNLOCKED {RSPINLOCK_INITIALIZER, 0}
#endif

static inline void portMUX_INITIALIZE(portMUX_TYPE *mux)
{
  rspin_lock_init(&mux->lock);
  mux->flags = 0;
}

static inline void port_enter_critical(portMUX_TYPE *mux)
{
  irqstate_t flags = rspin_lock_irqsave(&mux->lock);
  if (mux->lock.count == 1)
    {
      mux->flags = flags;
    }
}

static inline void port_exit_critical(portMUX_TYPE *mux)
{
  rspin_unlock_irqrestore(&mux->lock, mux->flags);
}

#define portENTER_CRITICAL(mux) port_enter_critical(mux)
#define portEXIT_CRITICAL(mux) port_exit_critical(mux)
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
#define portYIELD_FROM_ISR() do { } while (0)

struct esp_freertos_queue_s;
typedef struct esp_freertos_queue_s *QueueHandle_t;

QueueHandle_t xQueueCreateWithCaps(UBaseType_t length, UBaseType_t item_size,
                                   uint32_t caps);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item,
                      TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item,
                         TickType_t ticks_to_wait);
BaseType_t xQueueReceiveFromISR(QueueHandle_t queue, void *item,
                                BaseType_t *higher_priority_task_woken);
void vQueueDelete(QueueHandle_t queue);
void vQueueDeleteWithCaps(QueueHandle_t queue);

#endif
