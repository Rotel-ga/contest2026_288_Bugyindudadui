/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nshlib/nshlib.h>
#include <sched.h>
#include <stdio.h>

int desktop_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
  int pid;
  nsh_initialize();
  pid = task_create("desktop", 100, 32768, desktop_main, NULL);
  printf("DESKTOP boot task=%d\n", pid);
  return nsh_consolemain(argc, argv);
}
