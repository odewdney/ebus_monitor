set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_SRCS "cmd_system.c" "cmd_wifi.c" "cmd_nvs.c")

set(COMPONENT_REQUIRES console spi_flash)

register_component()
