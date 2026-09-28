include $(APPDIR)/Make.defs
PROGNAME = desktop desktop_boot fgctl pictl
PRIORITY = 100 100 100 100
STACKSIZE = 32768 8192 4096 4096
MAINSRC = desktop_main.c desktop_boot.c fgctl_main.c pictl_main.c
CSRCS = core/settings.c ui/common.c ui/lockscreen.c ui/home.c ui/settings_page.c assets/desktop_font.c ../fallguard/fallguard.c ../fallguard/panel_control.c ../fallguard/camera_preview.c ../photo_identify/photo_identify.c ../photo_identify/identify_bridge.c
CFLAGS += -I$(CURDIR) -I$(CURDIR)/../fallguard
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/chip/espressif
include $(APPDIR)/Application.mk
