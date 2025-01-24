set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_SRCS "softserial.c" "ss_vfs.c")

set(COMPONENT_REQUIRES console spi_flash)

register_component()
