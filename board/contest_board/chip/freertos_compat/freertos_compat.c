/****************************************************************************
 * Minimal FreeRTOS queue compatibility for ESP32-P4 CSI on NuttX
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

#include "freertos/FreeRTOS.h"

struct esp_freertos_queue_s
{
  mutex_t lock;
  sem_t items;
  sem_t slots;
  uint8_t *storage;
  size_t length;
  size_t item_size;
  size_t read_index;
  size_t write_index;
};

QueueHandle_t xQueueCreateWithCaps(UBaseType_t length, UBaseType_t item_size,
                                   uint32_t caps)
{
  struct esp_freertos_queue_s *queue;

  (void)caps;

  if (length == 0 || item_size == 0)
    {
      return NULL;
    }

  queue = kmm_zalloc(sizeof(*queue));
  if (queue == NULL)
    {
      return NULL;
    }

  queue->storage = kmm_malloc((size_t)length * item_size);
  if (queue->storage == NULL)
    {
      kmm_free(queue);
      return NULL;
    }

  nxmutex_init(&queue->lock);
  nxsem_init(&queue->items, 0, 0);
  nxsem_init(&queue->slots, 0, length);
  queue->length = length;
  queue->item_size = item_size;
  return queue;
}

static BaseType_t queue_send_internal(QueueHandle_t queue, const void *item,
                                      TickType_t ticks_to_wait)
{
  int ret;

  if (queue == NULL || item == NULL)
    {
      return pdFALSE;
    }

  if (ticks_to_wait == 0)
    {
      ret = nxsem_trywait(&queue->slots);
    }
  else if (ticks_to_wait == portMAX_DELAY)
    {
      ret = nxsem_wait_uninterruptible(&queue->slots);
    }
  else
    {
      ret = nxsem_tickwait(&queue->slots, ticks_to_wait);
    }

  if (ret < 0)
    {
      return pdFALSE;
    }

  nxmutex_lock(&queue->lock);
  memcpy(queue->storage + queue->write_index * queue->item_size,
         item, queue->item_size);
  queue->write_index = (queue->write_index + 1) % queue->length;
  nxmutex_unlock(&queue->lock);
  nxsem_post(&queue->items);
  return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item,
                      TickType_t ticks_to_wait)
{
  return queue_send_internal(queue, item, ticks_to_wait);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item,
                         TickType_t ticks_to_wait)
{
  int ret;

  if (queue == NULL || item == NULL)
    {
      return pdFALSE;
    }

  if (ticks_to_wait == 0)
    {
      ret = nxsem_trywait(&queue->items);
    }
  else if (ticks_to_wait == portMAX_DELAY)
    {
      ret = nxsem_wait_uninterruptible(&queue->items);
    }
  else
    {
      ret = nxsem_tickwait(&queue->items, ticks_to_wait);
    }

  if (ret < 0)
    {
      return pdFALSE;
    }

  nxmutex_lock(&queue->lock);
  memcpy(item, queue->storage + queue->read_index * queue->item_size,
         queue->item_size);
  queue->read_index = (queue->read_index + 1) % queue->length;
  nxmutex_unlock(&queue->lock);
  nxsem_post(&queue->slots);
  return pdTRUE;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t queue, void *item,
                                BaseType_t *higher_priority_task_woken)
{
  if (higher_priority_task_woken != NULL)
    {
      *higher_priority_task_woken = pdFALSE;
    }

  return xQueueReceive(queue, item, 0);
}

void vQueueDelete(QueueHandle_t queue)
{
  if (queue == NULL)
    {
      return;
    }

  nxsem_destroy(&queue->items);
  nxsem_destroy(&queue->slots);
  nxmutex_destroy(&queue->lock);
  kmm_free(queue->storage);
  kmm_free(queue);
}

void vQueueDeleteWithCaps(QueueHandle_t queue)
{
  vQueueDelete(queue);
}

/* Some ESP-IDF DMA sources use these as functions rather than macros. */
void portMUX_INITIALIZE(void *mux)
{
  (void)mux;
}

void portYIELD_FROM_ISR(void)
{
}
