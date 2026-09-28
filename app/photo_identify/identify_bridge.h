/* SPDX-License-Identifier: Apache-2.0 */
#ifndef IDENTIFY_BRIDGE_H
#define IDENTIFY_BRIDGE_H
#include <stdbool.h>
#include <stdint.h>
int identify_request(const uint16_t *pixels);
bool identify_pending(void);
void identify_cancel(void);
int identify_command(int argc, char **argv);
#endif
