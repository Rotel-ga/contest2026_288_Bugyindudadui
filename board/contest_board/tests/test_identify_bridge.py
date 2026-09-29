#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'app/photo_identify/identify_bridge.c').read_text()
s='\n'.join(l for l in s.splitlines() if not l.lstrip().startswith(('#include','#  include')))
prefix='''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
#include <assert.h>
#include <time.h>
typedef int mutex_t;
#define NXMUTEX_INITIALIZER 0
#define CAMERA_PREVIEW_BYTES 259200
#define MSEC2TICK(x) (x)
static clock_t now;
static clock_t clock_systime_ticks(void){return now;}
static int nxmutex_trylock(mutex_t *m){if(*m)return -EBUSY;*m=1;return 0;}
static void nxmutex_lock(mutex_t *m){assert(!*m);*m=1;}
static void nxmutex_unlock(mutex_t *m){assert(*m);*m=0;}
static char message[1024];
static int photo_identify_set_message(const char *p){strcpy(message,p);return 0;}
'''
test='''
int main(void){
 uint16_t *pixels=calloc(1,CAMERA_PREVIEW_BYTES);
 assert(identify_request(pixels)==0);pixels[0]=123;assert(g_snapshot[0]==0);
 assert(identify_request(pixels)==-EBUSY);
 char *b[]={"pictl","b","1","2","195"};
 char *c[]={"pictl","c","1","0","6162"};
 char *e[]={"pictl","e","1"};
 assert(identify_command(5,b)==0);
 c[3]="1";assert(identify_command(5,c)==1);c[3]="0";
 assert(identify_command(5,c)==0);assert(identify_command(3,e)==0);
 assert(strcmp(message,"ab")==0 && !identify_pending());
 assert(identify_request(pixels)==0);assert(identify_command(5,b)==1);
 identify_cancel();assert(!identify_pending() && !g_snapshot);
 assert(identify_request(pixels)==0);now=180001;assert(!identify_pending());
 assert(identify_command(3,e)==1);
 identify_cancel();free(pixels);
 unsigned char bad[]={0xc0,0x80};assert(!utf8_valid(bad,2));
 puts("PASS: snapshot ownership, stale request, offsets, commit, cancel, timeout, invalid UTF8");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.c';exe=Path(tmp)/'test';p.write_text(prefix+s+test)
 subprocess.run(['cc','-std=c11','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
