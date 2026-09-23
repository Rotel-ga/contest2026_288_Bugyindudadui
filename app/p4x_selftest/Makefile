############################################################################
# ESP32-P4X board self-test - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = p4x_selftest
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 2048
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST)

MAINSRC = p4x_selftest_main.c

include $(APPDIR)/Application.mk
