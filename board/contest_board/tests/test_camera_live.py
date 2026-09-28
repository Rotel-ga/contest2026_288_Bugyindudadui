#!/usr/bin/env python3
"""Exercise actual CSI callbacks in their driver ordering (get before done)."""
from pathlib import Path
import re
import subprocess
import tempfile
r = Path(__file__).resolve().parents[3]
s = (r/'app/p4x_selftest/p4x_camera_csi.c').read_text()
def function(name):
    m = re.search(r'^static bool ' + name + r'\([^{}]+\)\n\{', s, re.M)
    end = m.end(); depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end]
prefix = '''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define SC2336_FRAME_SIZE 1843200
#define SC2336_SKIP_FRAMES 5
typedef int irqstate_t;
typedef void *esp_cam_ctlr_handle_t;
typedef struct {void *buffer;size_t buflen;} esp_cam_ctlr_trans_t;
enum live_slot_state { LIVE_FREE, LIVE_DMA, LIVE_READY, LIVE_READING };
static unsigned int g_live_completed;
static char slots[3];
static void *g_live_frames[]={slots,slots+1,slots+2};
static enum live_slot_state g_live_slots[3];
static int critical;
static irqstate_t enter_critical_section(void) {return critical++;}
static void leave_critical_section(irqstate_t f) {critical=f;}
static int index_of(void *p) {
  for(int i=0;i<3;i++)if(p==g_live_frames[i])return i;
  return -1;
}
'''
test = '''
int main(void) {
  esp_cam_ctlr_trans_t current={0},next={0};
  live_get_buffer(NULL,&current,NULL);
  int reader=-1;
  for(int frame=0;frame<10000;frame++) {
    next.buffer=NULL;
    live_get_buffer(NULL,&next,NULL);
    int next_slot=index_of(next.buffer), old=index_of(current.buffer);
    assert(next_slot>=0 && next_slot!=old && next_slot!=reader);
    assert(next.buflen==SC2336_FRAME_SIZE);
    live_frame_done(NULL,&current,NULL);
    if(reader>=0)assert(g_live_slots[reader]==LIVE_READING);
    current=next;
    /* Slow consumer holds a frame while DMA completes another 12. */
    if(frame%13==0) {
      if(reader>=0)g_live_slots[reader]=LIVE_FREE;
      reader=-1;
      for(int i=0;i<3;i++)if(g_live_slots[i]==LIVE_READY) {
        g_live_slots[i]=LIVE_READING;reader=i;break;
      }
    }
    int dma=0,ready=0;
    for(int i=0;i<3;i++) {
      dma += g_live_slots[i]==LIVE_DMA;
      ready += g_live_slots[i]==LIVE_READY;
    }
    assert(dma==1 && ready<=1 && !critical);
  }
  puts("PASS: 10000 continuous completions, slow reader protected, latest frame, bounded slots");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'test.c';exe=Path(tmp)/'test'
    p.write_text(prefix+function('live_get_buffer')+function('live_frame_done')+test)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
