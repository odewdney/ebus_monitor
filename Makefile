#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := ebusbridge

#CONFIG_NVS_ENCRYPTION=

#COMPONENTS=freertos esp32 newlib esptool_py nvs_flash spi_flash log tcpip_adapter lwip main xtensa-debug-module driver bt
COMPONENTS=esptool_py main freertos nvs_flash spi_flash partition_table
COMPONENTS+=bootloader bootloader_support app_update
COMPONENTS+=tcpip_adapter lwip mbedtls wpa_supplicant
COMPONENTS+=log vfs newlib heap pthread
COMPONENTS+=esp8266 esp_common esp_ringbuf esp_event
COMPONENTS+=console cmd_system softserial

include $(IDF_PATH)/make/project.mk

