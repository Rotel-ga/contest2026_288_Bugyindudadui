#!/usr/bin/env python3
"""Compile the actual IRQ ownership/dispatch functions against a fake allocator.

No hardware register behavior is simulated.  This regression verifies handle
identity, dispatch context, failure rollback and freeing one shared client.
"""
from pathlib import Path
import subprocess
import tempfile
import re

CHIP = Path(__file__).resolve().parents[1] / 'chip'
source = (CHIP / 'espressif/esp_irq.c').read_text()


def function(name):
    match = re.search(r'^.*\b' + name + r'\([^;{}]*\)\n\{', source, re.M)
    start = match.start()
    brace = match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define IRAM_ATTR
#define ESP_OK 0
#define OK 0
#define SOC_CPU_INTR_NUM 32
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_INTR_FLAG_INTRDISABLED 8
#define ESP_INTR_FLAG_SHARED 16
#define NR_IRQS 64
#define ESP_SOURCE2IRQ(s) ((s) + 16)
#define IRQ_UNMAPPED NULL
#define RISCV_IRQ_BIT 0x80000000u
#define VECTORS_MCAUSE_REASON_MASK 255
#define VECTORS_MCAUSE_INTBIT_MASK RISCV_IRQ_BIT
#define RV_EXTERNAL_INT_OFFSET 16
#define ESP_NCPUINTS 32
#define INTR_TYPE_EDGE 1
#define DEBUGASSERT assert
#define irqwarn(...) ((void)0)
typedef unsigned irqstate_t;
typedef uint32_t uintreg_t;
typedef void (*intr_handler_t)(void *);
struct handle {int cpu, vector, source; bool enabled; intr_handler_t cb; void *arg;};
typedef struct handle *intr_handle_t;
struct esp_native_intr_s {struct esp_native_intr_s *next; intr_handle_t handle; int irq, cpu;};
static struct esp_native_intr_s *g_native_intrs;
static intr_handle_t g_handle_map[1][NR_IRQS];
static int g_dispatch_cpuint[1];
static int critical, fail_alloc, fail_enable, current_cpu, live, ack_count;
static bool in_irq;
static int interrupt_type = INTR_TYPE_EDGE;
static intr_handle_t handles[16];
static int this_cpu(void) {return current_cpu;}
static irqstate_t enter_critical_section(void) {return critical++;}
static void leave_critical_section(irqstate_t s) {critical=s;}
#define kmm_zalloc(n) calloc(1,n)
#define kmm_free(p) free(p)
static int esp_intr_get_cpu(intr_handle_t h) {return h->cpu;}
static int esp_intr_get_intno(intr_handle_t h) {return h->vector;}
static int esp_intr_alloc_intrstatus(int s,int f,uint32_t r,uint32_t m,
                                    intr_handler_t cb,void *a,intr_handle_t *out)
{
  assert(critical && (f & ESP_INTR_FLAG_INTRDISABLED));
  if(fail_alloc) return 99;
  intr_handle_t h=calloc(1,sizeof(*h));
  *h=(struct handle){current_cpu,5,s,false,cb,a};
  for(int i=0;i<16;i++) if(!handles[i]) {handles[i]=h;break;}
  *out=h;live++;return 0;
}
static int esp_intr_enable(intr_handle_t h)
{
  if(fail_enable)return 98;
  assert(g_native_intrs);h->enabled=true;return 0;
}
static int esp_intr_free(intr_handle_t h)
{
  assert(critical);
  for(int i=0;i<16;i++)if(handles[i]==h)handles[i]=NULL;
  live--;free(h);return 0;
}
static int esprv_int_get_type(int c) {return interrupt_type;}
static void esp_cpu_intr_edge_ack(int c) {ack_count++;}
static void shared_handler(void *arg)
{
  for(int i=0;i<16;i++)if(handles[i] && handles[i]->enabled)
    handles[i]->cb(handles[i]->arg);
}
struct intr_adapter_from_nuttx {int irq;void *context;void *arg;};
static void isr_adapter_func(void *a) {abort();}
static intr_handler_t esp_cpu_intr_get_handler(int c) {assert(c==5);return shared_handler;}
static void *esp_cpu_intr_get_handler_arg(int c) {return NULL;}
static uintreg_t *riscv_doirq(int irq,uintreg_t *regs);
'''
names = ['esp_alloc_native_irq', 'esp_free_native_irq', 'esp_get_handle',
         'esp_cpuint_to_irq', 'esp_isr_demultiplexing', 'riscv_dispatch_irq']
# The demultiplexer is defined before any calls in the generated translation unit.
body = '\n'.join(function(n) for n in names)
suffix = r'''
static uintreg_t *riscv_doirq(int irq,uintreg_t *regs)
{
  in_irq=true;esp_isr_demultiplexing(irq,regs,NULL);in_irq=false;return regs;
}
static void callback(void *arg) {assert(in_irq);(*(int *)arg)++;}
static void fire(void) {uintreg_t regs[32]={0};riscv_dispatch_irq(RISCV_IRQ_BIT|21,regs);}
int main(void)
{
  intr_handle_t lcd=NULL,cam=NULL,tmp=NULL;
  int l=0,c=0;
  assert(esp_alloc_native_irq(3,ESP_INTR_FLAG_SHARED,0,1,callback,&l,&lcd)==0);
  assert(esp_alloc_native_irq(3,ESP_INTR_FLAG_SHARED,0,2,callback,&c,&cam)==0);
  assert(lcd!=cam && live==2);
  assert(esp_cpuint_to_irq(5,0)==19);
  fire();assert(l==1 && c==1 && ack_count==1);
  assert(esp_free_native_irq(cam)==0);
  assert(esp_get_handle(0,19)==lcd);
  fire();assert(l==2 && c==1);
  for(int i=0;i<100;i++) {
    assert(esp_alloc_native_irq(3,ESP_INTR_FLAG_SHARED,0,2,callback,&c,&cam)==0);
    assert(cam!=lcd);fire();assert(esp_free_native_irq(cam)==0);
  }
  interrupt_type=0;
  int before=ack_count;
  fire();assert(ack_count==before);
  assert(g_dispatch_cpuint[0]==0);
  fail_alloc=1;
  assert(esp_alloc_native_irq(3,0,0,0,callback,&c,&tmp)==99 && tmp==NULL);
  fail_alloc=0;fail_enable=1;
  assert(esp_alloc_native_irq(3,0,0,0,callback,&c,&tmp)==98 && tmp==NULL);
  fail_enable=0;assert(live==1 && critical==0);
  assert(esp_alloc_native_irq(4,ESP_INTR_FLAG_INTRDISABLED,0,0,callback,&c,&tmp)==0);
  assert(!tmp->enabled);assert(esp_free_native_irq(tmp)==0);
  current_cpu=1;assert(esp_free_native_irq(lcd)==ESP_ERR_INVALID_STATE);
  current_cpu=0;assert(esp_free_native_irq(lcd)==0);
  assert(!g_native_intrs && !live && !critical);
  assert(esp_cpuint_to_irq(5,0)==-1);
  assert(esp_free_native_irq(NULL)==ESP_ERR_INVALID_ARG);
  puts("PASS: shared handle identity, ISR context, 100 reopen cycles, rollback and release");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(prefix + body + suffix)
    subprocess.run(['cc', '-std=c11', '-g', '-fsanitize=address,undefined',
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
