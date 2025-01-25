#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/apps/sntp.h"

#include "argtable3/argtable3.h"
#include "esp_console.h"

const char *TAG="sntp";

// needs to not go out of scope
static char server_name[64];

void initialize_sntp()
{
    size_t len = sizeof(server_name);
    esp_err_t err;
    nvs_handle_t handle;
    uint8_t mode = SNTP_OPMODE_POLL;

    err = nvs_open("sntp", NVS_READONLY, &handle);
    if ( err != ESP_OK) {
        ESP_LOGI(TAG, "Not init SNTP %d", err);
        return;
    }

    err = nvs_get_str(handle, "host", server_name, &len);
    if ( err != ESP_OK) {
        ESP_LOGI(TAG, "No host SNTP %d", err);
        goto close_handle;
    }
    if(strlen(server_name) == 0) {
        ESP_LOGI(TAG, "No empty SNTP");
        goto close_handle;
    }
    
#ifdef SNTP_GET_SERVERS_FROM_DHCP
    if (strcmp(server_name, "dhcp") == 0) {
        ESP_LOGI(TAG, "dhcp SNTP");
        sntp_servermode_dhcp(1);
    } else
#endif
    if (strcmp(server_name, "listen") == 0) {
        ESP_LOGI(TAG, "listen SNTP");
        mode = SNTP_OPMODE_LISTENONLY;
    } else {
        ESP_LOGI(TAG, "host SNTP %s", server_name);
        sntp_setservername(0, server_name);
    }

    sntp_setoperatingmode(mode);

    sntp_init();

close_handle:
    nvs_close(handle);
}

int sntp_status_func(int argc, char**argv)
{
    if (sntp_enabled()) {
        int opmode = sntp_getoperatingmode();
        uint32_t interval = sntp_get_sync_interval();
        int status = sntp_get_sync_status();
        int syncmode = sntp_get_sync_mode();
        const char *server = sntp_getservername(0);
        printf("SNTP opMode:%d server %s\r\n", opmode, server);
        printf("SNTP syncMode:%d status:%d interval:%d\r\n", syncmode, status, interval);
    } else {
        printf("Not enabled\r\n");
    }
    return 0;
}

int sntp_restart_func(int argc, char**argv)
{
    if (sntp_enabled()) {
        sntp_stop();
        sntp_init();
    } else {
        printf("Not enabled\r\n");
    }
    return 0;
}

void register_sntp_cmd()
{
    const esp_console_cmd_t sntp_restart_cmd = {
        .command = "sntp_restart",
        .help = "sntp restart",
        .hint = NULL,
        .func = sntp_restart_func,
        .argtable = NULL
    };
    esp_console_cmd_register(&sntp_restart_cmd);
    const esp_console_cmd_t sntp_status_cmd = {
        .command = "sntp_status",
        .help = "sntp status",
        .hint = NULL,
        .func = sntp_status_func,
        .argtable = NULL
    };
    esp_console_cmd_register(&sntp_status_cmd);

}