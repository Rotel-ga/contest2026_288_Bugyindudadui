/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mtd/mtd.h>
#include <sys/mount.h>
#include <syslog.h>
#include <errno.h>
#include <stdint.h>
#include "espressif/esp_spiflash.h"
#include "espressif/esp_spiflash_mtd.h"

/* Reserve 512 KiB at the end of the verified 16 MiB device.
 * Desktop build checks the firmware ends before this partition.
 */
#define DESKTOP_DATA_OFFSET 0xf80000
#define DESKTOP_DATA_SIZE   0x080000

int board_desktop_storage_initialize(void)
{
  struct mtd_dev_s *mtd;
  uint8_t block[256];
  uint32_t offset;
  unsigned int i;
  bool erased = true;
  int ret;

  ret = esp_spiflash_initialize();
  if (ret < 0) return ret;
  mtd = esp_spiflash_alloc_mtdpart(DESKTOP_DATA_OFFSET, DESKTOP_DATA_SIZE);
  if (!mtd) return -ENODEV;
  ret = register_mtddriver("/dev/desktop-data", mtd, 0600, NULL);
  if (ret < 0) return ret;
  ret = nx_mount("/dev/desktop-data", "/data", "littlefs", 0, NULL);
  if (ret < 0)
    {
      /* Format only a fully erased dedicated partition. Never reformat
       * an existing or corrupt settings volume on a mount failure.
       */
      for (offset = 0; offset < DESKTOP_DATA_SIZE && erased; offset += sizeof(block))
        {
          ret = esp_spiflash_read(DESKTOP_DATA_OFFSET + offset, block, sizeof(block));
          if (ret < 0) return ret;
          for (i = 0; i < sizeof(block); i++)
            if (block[i] != 0xff) { erased = false; break; }
        }
      if (!erased)
        {
          syslog(LOG_ERR, "DESKTOP data not blank; refusing format\n");
          return -EIO;
        }
      ret = nx_mount("/dev/desktop-data", "/data", "littlefs", 0, "forceformat");
    }
  syslog(LOG_INFO, "DESKTOP storage mount=%d offset=0xf80000 size=0x80000\n", ret);
  return ret;
}
