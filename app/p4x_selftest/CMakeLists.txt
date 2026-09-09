# ESP32-P4X board self-test
if(CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST)
  nuttx_add_application(
    NAME p4x_selftest
    SRCS p4x_selftest_main.c
    STACKSIZE 2048)
endif()
