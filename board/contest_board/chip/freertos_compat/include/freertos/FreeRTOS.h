/****************************************************************************
 * Minimal FreeRTOS compatibility for ESP32-P4 camera HAL on NuttX
 ****************************************************************************/

#ifndef __ESP32P4_FREERTOS_COMPAT_FREERTOS_H
#define __ESP32P4_FREERTOS_COMPAT_FREERTOS_H

#include <stdint.h>
#include <stdbool.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#ifndef CONFIG_SCHED_TICK_HZ
#  define CONFIG_SCHED_TICK_HZ 100
#endif

#ifndef portTICK_PERIOD_MS
#  define portTICK_PERIOD_MS (1000 / CONFIG_SCHED_TICK_HZ)
#endif
#define pdMS_TO_TICKS(ms)  ((TickType_t)(((uint64_t)(ms) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS))
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

#ifndef ESP_FREERTOS_COMPAT_PORTMUX_DEFINED
#  define ESP_FREERTOS_COMPAT_PORTMUX_DEFINED 1
typedef struct portmux_compat_s
{
  irqstate_t flags;
} portMUX_TYPE;
#endif

#ifndef portMUX_INITIALIZER_UNLOCKED
#  define portMUX_INITIALIZER_UNLOCKED {}
#endif
#ifndef portENTER_CRITICAL
#  define portENTER_CRITICAL(mux) \
    do { (void)(mux); } while (0)
#endif
#ifndef portEXIT_CRITICAL
#  define portEXIT_CRITICAL(mux) \
    do { (void)(mux); } while (0)
#endif
#ifndef portENTER_CRITICAL_ISR
#  define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#endif
#ifndef portEXIT_CRITICAL_ISR
#  define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
#endif
void portMUX_INITIALIZE(void *mux);
void portYIELD_FROM_ISR(void);

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
