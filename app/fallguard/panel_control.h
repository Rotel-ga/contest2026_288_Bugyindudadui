/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FALLGUARD_PANEL_CONTROL_H
#define FALLGUARD_PANEL_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

enum panel_pc_state
{
  PANEL_PC_IDLE,
  PANEL_PC_RUNNING,
  PANEL_PC_OK,
  PANEL_PC_ERROR,
  PANEL_PC_FALL
};

struct panel_control_state
{
  uint32_t revision;
  uint32_t acknowledged;
  bool requested;
  bool connected;
  enum panel_pc_state pc_state;
};

void panel_control_request(bool enabled);
void panel_control_snapshot(struct panel_control_state *state);
int panel_control_command(int argc, char **argv);

#endif
