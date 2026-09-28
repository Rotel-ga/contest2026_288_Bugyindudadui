#!/usr/bin/env python3
"""Run the actual panel mailbox with host mutex/clock stubs."""
from pathlib import Path
import subprocess
import tempfile
r = Path(__file__).resolve().parents[3]
source = (r/'app/fallguard/panel_control.c').read_text()
source = '\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
header = (r/'app/fallguard/panel_control.h').read_text()
prefix = '''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
typedef int mutex_t;
#define NXMUTEX_INITIALIZER 0
#define MSEC2TICK(x) (x)
static clock_t now;
static clock_t clock_systime_ticks(void) {return now;}
static void nxmutex_lock(mutex_t *m) {assert(!*m);*m=1;}
static void nxmutex_unlock(mutex_t *m) {assert(*m);*m=0;}
'''
test = '''
int main(void) {
  struct panel_control_state s;
  char *query[]={"fgctl","query"};
  char *ack[]={"fgctl","ack","1","running"};
  panel_control_request(true);
  panel_control_snapshot(&s);assert(s.revision==1 && s.requested && !s.connected);
  assert(panel_control_command(2,query)==0);
  assert(panel_control_command(4,ack)==0);
  panel_control_request(false);
  assert(panel_control_command(4,ack)==0);
  panel_control_snapshot(&s);
  assert(s.revision==2 && !s.requested && s.acknowledged==1);
  ack[2]="2";ack[3]="idle";assert(panel_control_command(4,ack)==0);
  panel_control_snapshot(&s);assert(s.acknowledged==2 && s.connected);
  now=180001;panel_control_snapshot(&s);assert(!s.connected);
  puts("PASS: persistent request, stale ack rejected, stop ack, contact expiry");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)/'test.c';exe=Path(tmp)/'test';p.write_text(prefix+header+source+test)
    subprocess.run(['cc','-std=c11',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
