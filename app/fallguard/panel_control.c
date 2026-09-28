/* SPDX-License-Identifier: Apache-2.0 */
#include "panel_control.h"
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* State survives page changes and image transfer.  Only the LVGL task renders
 * widgets; the NSH command updates this small, mutex-protected mailbox.
 */

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static struct panel_control_state g_state;
static clock_t g_last_contact;

void panel_control_request(bool enabled)
{
  nxmutex_lock(&g_lock);
  if (g_state.requested != enabled)
    {
      g_state.requested = enabled;
      g_state.revision++;
    }
  nxmutex_unlock(&g_lock);
}

void panel_control_snapshot(struct panel_control_state *state)
{
  nxmutex_lock(&g_lock);
  *state = g_state;
  /* A capture can take 120 seconds plus bounded recovery. */
  state->connected = state->connected &&
    clock_systime_ticks() - g_last_contact < MSEC2TICK(180000);
  nxmutex_unlock(&g_lock);
}

int panel_control_command(int argc, char **argv)
{
  struct panel_control_state state;
  static const char * const names[] = {"idle", "running", "ok", "error", "fall"};
  unsigned long revision = 0;
  int mode = -1;
  char *end;

  if (argc == 4 && strcmp(argv[1], "ack") == 0)
    {
      errno = 0;
      revision = strtoul(argv[2], &end, 10);
      if (errno || *argv[2] == '\0' || *end || revision > UINT32_MAX ||
          argv[2][0] == '-')
        {
          return 1;
        }
      for (int i = 0; i < 5; i++)
        {
          if (strcmp(argv[3], names[i]) == 0)
            {
              mode = i;
            }
        }
    }
  else if (argc != 2 || strcmp(argv[1], "query") != 0)
    {
      printf("Usage: fgctl query | fgctl ack <revision> idle|running|ok|error|fall\n");
      return 1;
    }

  if (argc == 4 && mode < 0)
    {
      return 1;
    }

  nxmutex_lock(&g_lock);
  g_state.connected = true;
  g_last_contact = clock_systime_ticks();
  /* An old acknowledgement cannot overwrite a newer button action. */
  if (mode >= 0 && revision == g_state.revision)
    {
      g_state.acknowledged = revision;
      g_state.pc_state = mode;
    }
  state = g_state;
  nxmutex_unlock(&g_lock);

  printf("\nFGSTATE rev=%lu requested=%d ack=%lu pc=%s\nFGEND\n",
         (unsigned long)state.revision, state.requested,
         (unsigned long)state.acknowledged, names[state.pc_state]);
  fflush(stdout);
  return 0;
}
