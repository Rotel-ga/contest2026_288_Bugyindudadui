############################################################################
# ESP32-P4X board self-test - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = p4x_selftest
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 4096
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST)

MAINSRC = p4x_selftest_main.c
CSRCS += p4x_camera_capture.c
CSRCS += p4x_camera_csi.c

CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/freertos_compat/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/upper_hal_cam/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/upper_hal_cam/interface
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/upper_hal_cam/csi/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/upper_hal_isp/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_common/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_system/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_heap/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/heap/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hal_cam/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hal_cam/esp32p4/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hw_support/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_hw_support/ldo/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/hal/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/soc/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/soc/esp32p4/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/esp_mm/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/components/upper_hal_dma/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/esp32p4/include
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/esp-hal-3rdparty/nuttx/include

include $(APPDIR)/Application.mk
