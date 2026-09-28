#!/usr/bin/env python3
"""Exercise the actual USB frame functions against a bounded fake FIFO."""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'chip/espressif/esp_usbserial.c').read_text()

def function(name):
    m = re.search(r'^.*\b' + name + r'\([^;{}]*\)\n\{', source, re.M)
    start, end, depth = m.start(), m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#define CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT 1
#define MSEC2TICK(x) (x)
typedef int irqstate_t;
typedef int mutex_t;
typedef int sem_t;
#define USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY 1
#define USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT 2
#define OK 0
static int irq_enabled, irq_status;
static sem_t g_frame_ready;
static void usb_serial_jtag_ll_disable_intr_mask(int mask) {irq_enabled &= ~mask;}
static void usb_serial_jtag_ll_ena_intr_mask(int mask) {irq_enabled |= mask;}
static void usb_serial_jtag_ll_clr_intsts_mask(int mask) {irq_status &= ~mask;}
static int usb_serial_jtag_ll_get_intsts_mask(void) {return irq_status;}
static int nxsem_post(sem_t *s) {(*s)++;return 0;}
static int nxsem_trywait(sem_t *s) {if(*s){(*s)--;return 0;}return -EAGAIN;}
static void uart_xmitchars(void *dev) {}
static void uart_recvchars(void *dev) {}
static int esp_interrupt(int irq,void *ctx,void *arg);
struct uart_dev_s {int unused;};
static mutex_t g_frame_lock;
static bool g_frame_tx, g_tx_stalled;
static int critical, available, ticks, disconnected, sleeps;
static char wire[8192];
static size_t wire_len;
static int nxmutex_trylock(mutex_t *l) {if(*l)return -EBUSY;*l=1;return 0;}
static void nxmutex_unlock(mutex_t *l) {*l=0;}
static irqstate_t enter_critical_section(void) {return critical++;}
static void leave_critical_section(irqstate_t f) {critical=f;}
static clock_t clock_systime_ticks(void) {return ticks;}
static void usb_serial_jtag_ll_txfifo_flush(void) {}
static bool usb_serial_jtag_ll_txfifo_writable(void) {return available>0;}
static size_t usb_serial_jtag_ll_write_txfifo(const uint8_t *data,size_t n)
{
  assert(critical);
  if(n>(size_t)available)n=available;
  memcpy(wire+wire_len,data,n);wire_len+=n;available-=n;return n;
}
static void up_udelay(int us) {ticks++;}
static void esp_send(struct uart_dev_s *dev,int ch);
static int nxsem_tickwait_uninterruptible(sem_t *sem, int timeout)
{
  assert(!critical);ticks++;sleeps++;
  /* A desktop log racing the transfer must not enter the wire. */
  esp_send(NULL,'!');
  assert(irq_enabled & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  if(disconnected)return -ETIMEDOUT;
  available=7;irq_status=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
  esp_interrupt(0,NULL,NULL);
  assert(!(irq_enabled & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY));
  return nxsem_trywait(sem);
}
'''
body = '\n'.join(function(n) for n in ['esp_interrupt','esp_send','esp_usbserial_frame_begin','esp_usbserial_frame_write','esp_usbserial_frame_end'])
suffix = r'''
int main(void)
{
  char input[4096];
  for(size_t i=0;i<sizeof(input);i++)input[i]='A'+i%26;
  assert(esp_usbserial_frame_begin()==0);
  assert(esp_usbserial_frame_begin()==-EBUSY);
  assert(esp_usbserial_frame_write(input,sizeof(input))==0);
  assert(wire_len==sizeof(input) && !memcmp(wire,input,sizeof(input)));
  assert(sleeps>0 && !critical);
  esp_usbserial_frame_end();assert(!g_frame_tx && !g_frame_lock);
  disconnected=1;available=0;
  assert(esp_usbserial_frame_begin()==0);
  assert(esp_usbserial_frame_write(input,1)==-ETIMEDOUT);
  esp_usbserial_frame_end();assert(!g_frame_tx && !g_frame_lock && !critical);
  puts("PASS: partial FIFO writes, yielding backpressure, log isolation, timeout, unlock");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'test.c';exe=Path(tmp)/'test';p.write_text(prefix+body+suffix)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
