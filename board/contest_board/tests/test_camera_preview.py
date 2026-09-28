#!/usr/bin/env python3
"""Test actual preview downsampling and mailbox ownership with host stubs."""
from pathlib import Path
import subprocess
import tempfile
r = Path(__file__).resolve().parents[3]
source = (r/'app/fallguard/camera_preview.c').read_text()
source = '\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
header = (r/'app/fallguard/camera_preview.h').read_text()
prefix = '''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef int mutex_t;
#define NXMUTEX_INITIALIZER 0
static void nxmutex_lock(mutex_t *m) {assert(!*m);*m=1;}
static void nxmutex_unlock(mutex_t *m) {assert(*m);*m=0;}
static int nxmutex_trylock(mutex_t *m) {if(*m)return -EBUSY;*m=1;return 0;}
'''
test = '''
int main(void) {
  uint16_t *src=malloc(1280*720*2), *dst=calloc(1,CAMERA_PREVIEW_BYTES);
  assert(src && dst);
  for(int y=0;y<720;y++)for(int x=0;x<1280;x++)
    src[y*1280+x]= y<360 ? (x<640 ? 0xf800:0x07e0) : (x<640 ? 0x001f:0xffff);
  assert(camera_preview_publish(src,1280,720,NULL)==-ENODEV);
  assert(camera_preview_open()==0);
  assert(camera_preview_take(dst,0)==0);
  assert(camera_preview_publish(src,1280,720,NULL)==0);
  memset(src,0,1280*720*2); /* Producer's original frame can now be freed. */
  assert(camera_preview_take(dst,0)==1);
  assert(dst[0]==0xf800 && dst[479]==0x07e0);
  assert(dst[269*480]==0x001f && dst[270*480-1]==0xffff);
  assert(camera_preview_publish(src,0,720,NULL)==-EINVAL);
  assert(camera_preview_take(dst,1)==1 && dst[0]==0xf800);
  for(int i=0;i<1280*720;i++)src[i]=0xffff;
  uint32_t gain[]={0,65536,0};
  assert(camera_preview_publish(src,1280,720,gain)==0);
  g_lock=1;assert(camera_preview_take(dst,1)==1);g_lock=0;
  assert(camera_preview_take(dst,1)==2 && dst[0]==0x07e0);
  assert(camera_preview_take(dst,2)==2);
  for(int i=0;i<100;i++)assert(camera_preview_publish(src,1280,720,NULL)==0);
  assert(camera_preview_take(dst,2)==102);
  camera_preview_close();assert(!g_pending);
  assert(camera_preview_open()==0 && camera_preview_take(dst,0)==0);
  camera_preview_close();free(src);free(dst);
  puts("PASS: orientation, RGB565 channels, AWB, source lifetime, repeated updates, cleanup");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'test.c';exe=Path(tmp)/'test';p.write_text(prefix+header+source+test)
    subprocess.run(['cc','-std=c11','-O1','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
