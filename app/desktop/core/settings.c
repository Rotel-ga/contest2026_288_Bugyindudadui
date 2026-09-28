/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <crypto/sha2.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SETTINGS_PATH "/data/desktop/settings.bin"
#define SETTINGS_TEMP "/data/desktop/settings.tmp"
#define PIN_ROUNDS 8192

void desk_zero(void *ptr, size_t length)
{
  volatile uint8_t *p = ptr;
  while (length--) *p++ = 0;
}

static void pin_digest(const char *pin, const uint8_t *salt, uint8_t *out)
{
  SHA2_CTX ctx;
  uint32_t round;

  sha256init(&ctx);
  sha256update(&ctx, salt, 16);
  sha256update(&ctx, pin, DESK_PIN_DIGITS);
  sha256final(out, &ctx);
  for (round = 0; round < PIN_ROUNDS; round++)
    {
      sha256init(&ctx);
      sha256update(&ctx, out, 32);
      sha256update(&ctx, salt, 16);
      sha256update(&ctx, pin, DESK_PIN_DIGITS);
      sha256final(out, &ctx);
    }

  desk_zero(&ctx, sizeof(ctx));
}

int desk_pin_set(struct desk_settings *settings, const char *pin)
{
  int fd = open("/dev/random", O_RDONLY);
  ssize_t n;

  if (fd < 0) return -errno;
  n = read(fd, settings->salt, sizeof(settings->salt));
  close(fd);
  if (n != sizeof(settings->salt)) return -EIO;
  pin_digest(pin, settings->salt, settings->digest);
  settings->mode = DESK_PIN;
  return 0;
}

bool desk_pin_verify(const char *pin)
{
  uint8_t digest[32];
  uint8_t mismatch = 0;
  unsigned int i;

  pin_digest(pin, g_desk.settings.salt, digest);
  for (i = 0; i < sizeof(digest); i++)
    mismatch |= digest[i] ^ g_desk.settings.digest[i];
  desk_zero(digest, sizeof(digest));
  return mismatch == 0;
}

int desk_settings_save(const struct desk_settings *settings)
{
  uint8_t checksum[32];
  SHA2_CTX ctx;
  int ret = 0;
  int fd;

  if (mkdir("/data/desktop", 0700) < 0 && errno != EEXIST) return -errno;
  sha256init(&ctx);
  sha256update(&ctx, settings, sizeof(*settings));
  sha256final(checksum, &ctx);
  fd = open(SETTINGS_TEMP, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return -errno;
  if (write(fd, settings, sizeof(*settings)) != sizeof(*settings) ||
      write(fd, checksum, sizeof(checksum)) != sizeof(checksum) ||
      fsync(fd) < 0) ret = -EIO;
  if (close(fd) < 0) ret = -EIO;
  if (ret == 0 && rename(SETTINGS_TEMP, SETTINGS_PATH) < 0) ret = -errno;
  return ret;
}

int desk_settings_load(struct desk_settings *settings)
{
  uint8_t stored[32];
  uint8_t actual[32];
  SHA2_CTX ctx;
  int fd = open(SETTINGS_PATH, O_RDONLY);
  int ret = 0;

  if (fd < 0)
    {
      if (errno != ENOENT) return -errno;
      memset(settings, 0, sizeof(*settings));
      settings->version = 1;
      settings->mode = DESK_SWIPE;
      return desk_settings_save(settings);
    }

  if (read(fd, settings, sizeof(*settings)) != sizeof(*settings) ||
      read(fd, stored, sizeof(stored)) != sizeof(stored)) ret = -EIO;
  close(fd);
  if (ret < 0) return ret;
  sha256init(&ctx);
  sha256update(&ctx, settings, sizeof(*settings));
  sha256final(actual, &ctx);
  if (memcmp(actual, stored, 32) || settings->version != 1 ||
      settings->mode > DESK_PIN) return -EINVAL;
  return 0;
}
