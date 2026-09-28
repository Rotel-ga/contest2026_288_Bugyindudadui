include $(APPDIR)/Make.defs
PROGNAME = desktop desktop_boot
PRIORITY = 100 100
STACKSIZE = 32768 8192
MAINSRC = desktop_main.c desktop_boot.c
CSRCS = core/settings.c ui/common.c ui/lockscreen.c ui/home.c ui/settings_page.c assets/desktop_font.c ../fallguard/fallguard.c
CFLAGS += -I$(CURDIR) -I$(CURDIR)/../fallguard
include $(APPDIR)/Application.mk
