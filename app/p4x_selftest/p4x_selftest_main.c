/****************************************************************************
 * app/p4x_selftest/p4x_selftest_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/ioexpander/gpio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SELFTEST_APPLICATION       "p4x_selftest"
#define SELFTEST_BOARD             "ESP32-P4X-Function-EV-Board"
#define SELFTEST_SCHEMA_VERSION    1
#define SELFTEST_RESULT_COUNT      5
#define SELFTEST_DETAIL_LENGTH     128

#define SELFTEST_GPIO_DEVICE       "/dev/gpio0"
#define SELFTEST_I2C_DEVICE        "/dev/i2c1"
#define SELFTEST_I2C_ADDRESS       0x18
#define SELFTEST_I2C_FREQUENCY     I2C_SPEED_STANDARD

#define SELFTEST_TIMER_DELAY_US    500000
#define SELFTEST_TIMER_MIN_MS      450
#define SELFTEST_TIMER_MAX_MS      1000

#define SELFTEST_EXIT_TEST_FAILURE 1
#define SELFTEST_EXIT_USAGE        2

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum selftest_status_e
{
  SELFTEST_STATUS_PASS = 0,
  SELFTEST_STATUS_FAIL,
  SELFTEST_STATUS_SKIP
};

struct selftest_result_s
{
  FAR const char *name;
  enum selftest_status_e status;
  int error_code;
  long duration_ms;
  char detail[SELFTEST_DETAIL_LENGTH];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int selftest_errno(void)
{
  return errno > 0 ? errno : EIO;
}

static void selftest_result_init(FAR struct selftest_result_s *result,
                                 FAR const char *name)
{
  memset(result, 0, sizeof(*result));
  result->name = name;
  result->status = SELFTEST_STATUS_FAIL;
}

static FAR const char *selftest_status_name(enum selftest_status_e status)
{
  switch (status)
    {
      case SELFTEST_STATUS_PASS:
        return "PASS";

      case SELFTEST_STATUS_FAIL:
        return "FAIL";

      case SELFTEST_STATUS_SKIP:
        return "SKIP";
    }

  return "FAIL";
}

static long selftest_elapsed_ms(FAR const struct timespec *start,
                                FAR const struct timespec *end)
{
  time_t seconds;
  long nanoseconds;

  seconds = end->tv_sec - start->tv_sec;
  nanoseconds = end->tv_nsec - start->tv_nsec;
  if (nanoseconds < 0)
    {
      seconds--;
      nanoseconds += 1000000000L;
    }

  if (seconds < 0)
    {
      return -1;
    }

  return (long)seconds * 1000L + nanoseconds / 1000000L;
}

static void selftest_run_system(FAR struct selftest_result_s *result)
{
  struct utsname info;
  int error_code;

  selftest_result_init(result, "system");

  if (uname(&info) < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "uname failed (errno=%d)", error_code);
      return;
    }

  result->status = SELFTEST_STATUS_PASS;
  snprintf(result->detail, sizeof(result->detail), "%s %s %s",
           info.sysname, info.release, info.machine);
}

static void selftest_run_timer(FAR struct selftest_result_s *result)
{
  struct timespec start;
  struct timespec end;
  int error_code;
  long elapsed_ms;

  selftest_result_init(result, "timer");

  if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "start clock_gettime failed (errno=%d)", error_code);
      return;
    }

  if (usleep(SELFTEST_TIMER_DELAY_US) < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "usleep failed (errno=%d)", error_code);
      return;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "end clock_gettime failed (errno=%d)", error_code);
      return;
    }

  elapsed_ms = selftest_elapsed_ms(&start, &end);
  result->duration_ms = elapsed_ms;
  if (elapsed_ms < SELFTEST_TIMER_MIN_MS ||
      elapsed_ms > SELFTEST_TIMER_MAX_MS)
    {
      result->error_code = ERANGE;
      snprintf(result->detail, sizeof(result->detail),
               "500 ms delay measured %ld ms (expected %d..%d ms)",
               elapsed_ms, SELFTEST_TIMER_MIN_MS,
               SELFTEST_TIMER_MAX_MS);
      return;
    }

  result->status = SELFTEST_STATUS_PASS;
  snprintf(result->detail, sizeof(result->detail),
           "500 ms delay measured %ld ms", elapsed_ms);
}

static int selftest_gpio_set_verify(int fd, bool expected)
{
  bool actual;
  int ret;

  ret = ioctl(fd, GPIOC_WRITE, (unsigned long)expected);
  if (ret < 0)
    {
      return selftest_errno();
    }

  actual = !expected;
  ret = ioctl(fd, GPIOC_READ,
              (unsigned long)((uintptr_t)&actual));
  if (ret < 0)
    {
      return selftest_errno();
    }

  if (actual != expected)
    {
      return EIO;
    }

  return 0;
}

static void selftest_run_gpio(FAR struct selftest_result_s *result)
{
  FAR const char *stage;
  int cleanup_error;
  int primary_error;
  int close_error;
  int ret;
  int fd;

  selftest_result_init(result, "gpio_sw");

  fd = open(SELFTEST_GPIO_DEVICE, O_RDWR);
  if (fd < 0)
    {
      primary_error = selftest_errno();
      result->error_code = primary_error;
      snprintf(result->detail, sizeof(result->detail),
               "open %s failed (errno=%d)", SELFTEST_GPIO_DEVICE,
               primary_error);
      return;
    }

  stage = "drive/read low";
  primary_error = selftest_gpio_set_verify(fd, false);
  if (primary_error == 0)
    {
      stage = "drive/read high";
      primary_error = selftest_gpio_set_verify(fd, true);
    }

  if (primary_error == 0)
    {
      stage = "drive/read final low";
      primary_error = selftest_gpio_set_verify(fd, false);
    }

  cleanup_error = 0;
  ret = ioctl(fd, GPIOC_WRITE, (unsigned long)false);
  if (ret < 0)
    {
      cleanup_error = selftest_errno();
    }

  close_error = 0;
  ret = close(fd);
  if (ret < 0)
    {
      close_error = selftest_errno();
    }

  if (primary_error != 0)
    {
      result->error_code = primary_error;
      snprintf(result->detail, sizeof(result->detail),
               "%s failed (errno=%d); restore_low=%s", stage,
               primary_error, cleanup_error == 0 ? "ok" : "failed");
      return;
    }

  if (cleanup_error != 0)
    {
      result->error_code = cleanup_error;
      snprintf(result->detail, sizeof(result->detail),
               "final restore low failed (errno=%d)", cleanup_error);
      return;
    }

  if (close_error != 0)
    {
      result->error_code = close_error;
      snprintf(result->detail, sizeof(result->detail),
               "close %s failed (errno=%d)", SELFTEST_GPIO_DEVICE,
               close_error);
      return;
    }

  result->status = SELFTEST_STATUS_PASS;
  snprintf(result->detail, sizeof(result->detail),
           "%s low-high-low readback; restored low",
           SELFTEST_GPIO_DEVICE);
}

static void selftest_run_gpio_physical(
  FAR struct selftest_result_s *result)
{
  selftest_result_init(result, "gpio_physical");
  result->status = SELFTEST_STATUS_SKIP;
  snprintf(result->detail, sizeof(result->detail),
           "no external test fixture");
}

static void selftest_run_i2c(FAR struct selftest_result_s *result)
{
  struct i2c_transfer_s transfer;
  struct i2c_msg_s message;
  uint8_t value;
  int close_error;
  int error_code;
  int ret;
  int fd;

  selftest_result_init(result, "i2c_es8311");

  fd = open(SELFTEST_I2C_DEVICE, O_RDONLY);
  if (fd < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "open %s failed (errno=%d)", SELFTEST_I2C_DEVICE,
               error_code);
      return;
    }

  value = 0;
  message.frequency = SELFTEST_I2C_FREQUENCY;
  message.addr = SELFTEST_I2C_ADDRESS;
  message.flags = I2C_M_READ;
  message.buffer = &value;
  message.length = 1;

  transfer.msgv = &message;
  transfer.msgc = 1;

  ret = ioctl(fd, I2CIOC_TRANSFER,
              (unsigned long)((uintptr_t)&transfer));
  if (ret < 0)
    {
      error_code = selftest_errno();
      result->error_code = error_code;
      snprintf(result->detail, sizeof(result->detail),
               "%s 100 kHz address 0x%02x read failed (errno=%d)",
               SELFTEST_I2C_DEVICE, SELFTEST_I2C_ADDRESS, error_code);
    }
  else
    {
      result->status = SELFTEST_STATUS_PASS;
      snprintf(result->detail, sizeof(result->detail),
               "%s 100 kHz address 0x%02x acknowledged (data=0x%02x)",
               SELFTEST_I2C_DEVICE, SELFTEST_I2C_ADDRESS, value);
    }

  close_error = 0;
  if (close(fd) < 0)
    {
      close_error = selftest_errno();
    }

  if (close_error != 0 && result->status == SELFTEST_STATUS_PASS)
    {
      result->status = SELFTEST_STATUS_FAIL;
      result->error_code = close_error;
      snprintf(result->detail, sizeof(result->detail),
               "close %s failed (errno=%d)", SELFTEST_I2C_DEVICE,
               close_error);
    }
}

static void selftest_json_string(FAR const char *value)
{
  unsigned char character;

  putchar('"');
  while (*value != '\0')
    {
      character = (unsigned char)*value++;
      switch (character)
        {
          case '"':
            fputs("\\\"", stdout);
            break;

          case '\\':
            fputs("\\\\", stdout);
            break;

          case '\b':
            fputs("\\b", stdout);
            break;

          case '\f':
            fputs("\\f", stdout);
            break;

          case '\n':
            fputs("\\n", stdout);
            break;

          case '\r':
            fputs("\\r", stdout);
            break;

          case '\t':
            fputs("\\t", stdout);
            break;

          default:
            if (character < 0x20)
              {
                printf("\\u%04x", (unsigned int)character);
              }
            else
              {
                putchar(character);
              }
            break;
        }
    }

  putchar('"');
}

static void selftest_count(FAR const struct selftest_result_s *results,
                           FAR int *pass_count, FAR int *fail_count,
                           FAR int *skip_count)
{
  int i;

  *pass_count = 0;
  *fail_count = 0;
  *skip_count = 0;

  for (i = 0; i < SELFTEST_RESULT_COUNT; i++)
    {
      switch (results[i].status)
        {
          case SELFTEST_STATUS_PASS:
            (*pass_count)++;
            break;

          case SELFTEST_STATUS_FAIL:
            (*fail_count)++;
            break;

          case SELFTEST_STATUS_SKIP:
            (*skip_count)++;
            break;
        }
    }
}

static void selftest_print_human(
  FAR const struct selftest_result_s *results)
{
  int skip_count;
  int pass_count;
  int fail_count;
  int i;

  selftest_count(results, &pass_count, &fail_count, &skip_count);

  printf("ESP32-P4X board self-test\n");
  for (i = 0; i < SELFTEST_RESULT_COUNT; i++)
    {
      printf("[%-4s] %-14s %s", selftest_status_name(results[i].status),
             results[i].name, results[i].detail);
      if (results[i].error_code != 0)
        {
          printf("; error=%d", results[i].error_code);
        }

      putchar('\n');
    }

  printf("Summary: PASS=%d FAIL=%d SKIP=%d RESULT=%s\n",
         pass_count, fail_count, skip_count,
         fail_count == 0 ? "PASS" : "FAIL");
}

static void selftest_print_json(
  FAR const struct selftest_result_s *results)
{
  int skip_count;
  int pass_count;
  int fail_count;
  int i;

  selftest_count(results, &pass_count, &fail_count, &skip_count);

  printf("{\"schema_version\":%d,\"application\":",
         SELFTEST_SCHEMA_VERSION);
  selftest_json_string(SELFTEST_APPLICATION);
  fputs(",\"board\":", stdout);
  selftest_json_string(SELFTEST_BOARD);
  fputs(",\"tests\":[", stdout);

  for (i = 0; i < SELFTEST_RESULT_COUNT; i++)
    {
      if (i > 0)
        {
          putchar(',');
        }

      fputs("{\"name\":", stdout);
      selftest_json_string(results[i].name);
      fputs(",\"status\":", stdout);
      selftest_json_string(selftest_status_name(results[i].status));
      fputs(",\"detail\":", stdout);
      selftest_json_string(results[i].detail);
      printf(",\"error_code\":%d,\"duration_ms\":%ld}",
             results[i].error_code, results[i].duration_ms);
    }

  printf("],\"summary\":{\"pass\":%d,\"fail\":%d,\"skip\":%d},",
         pass_count, fail_count, skip_count);
  fputs("\"result\":", stdout);
  selftest_json_string(fail_count == 0 ? "PASS" : "FAIL");
  fputs("}\n", stdout);
}

static void selftest_show_usage(FAR FILE *stream,
                                FAR const char *program)
{
  fprintf(stream, "Usage: %s [--json | --help]\n", program);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct selftest_result_s results[SELFTEST_RESULT_COUNT];
  bool json_mode;
  int skip_count;
  int pass_count;
  int fail_count;

  json_mode = false;
  if (argc == 2)
    {
      if (strcmp(argv[1], "--json") == 0)
        {
          json_mode = true;
        }
      else if (strcmp(argv[1], "--help") == 0 ||
               strcmp(argv[1], "-h") == 0)
        {
          selftest_show_usage(stdout, argv[0]);
          return EXIT_SUCCESS;
        }
      else
        {
          selftest_show_usage(stderr, argv[0]);
          return SELFTEST_EXIT_USAGE;
        }
    }
  else if (argc != 1)
    {
      selftest_show_usage(stderr, argv[0]);
      return SELFTEST_EXIT_USAGE;
    }

  selftest_run_system(&results[0]);
  selftest_run_timer(&results[1]);
  selftest_run_gpio(&results[2]);
  selftest_run_gpio_physical(&results[3]);
  selftest_run_i2c(&results[4]);

  if (json_mode)
    {
      selftest_print_json(results);
    }
  else
    {
      selftest_print_human(results);
    }

  selftest_count(results, &pass_count, &fail_count, &skip_count);
  return fail_count == 0 ? EXIT_SUCCESS : SELFTEST_EXIT_TEST_FAILURE;
}
