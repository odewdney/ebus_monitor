/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
//#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"
#include "cmd_wifi.h"

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
static const char *TAG = "wifi";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(__func__, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void initialise_wifi(bool flash_storage)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;
    if (initialized) {
        return;
    }
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ( flash_storage )
        cfg.nvs_enable = 1;
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(flash_storage ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM) );
    if ( !flash_storage )
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;

    bool en;
    if (esp_wifi_get_auto_connect(&en) == ESP_OK && en)
        esp_wifi_connect();

}

static bool wifi_join(const char* ssid, const char* pass, int timeout_ms)
{
    initialise_wifi(false);
    wifi_config_t wifi_config = { 0 };
    strncpy((char*) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char*) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
            1, 1, timeout_ms / portTICK_PERIOD_MS);
    return (bits & CONNECTED_BIT) != 0;
}

/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

static int connect(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'",
            join_args.ssid->sval[0]);

    bool connected = wifi_join(join_args.ssid->sval[0],
                           join_args.password->sval[0],
                           join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(__func__, "Connection timed out");
        return 1;
    }
    ESP_LOGI(__func__, "Connected");
    return 0;
}

static struct {
    struct arg_lit *nvs;
    struct arg_end *end;
} init_args;

int init(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &init_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, init_args.end, argv[0]);
        return 1;
    }

    initialise_wifi(init_args.nvs->count > 0);
    /*
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init %s", esp_err_to_name(err));
        return 1;
    }
    */
    return 0;
}

static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_int *channel;
    struct arg_lit *hidden;
    struct arg_end *end;
} scan_args;

int scan(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &scan_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return 1;
    }

esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_scan_config_t scan = {
        // ssid bssid
        .channel = scan_args.channel->count ? scan_args.channel->ival[0] : WIFI_ALL_CHANNEL_SCAN,
        .show_hidden = scan_args.hidden->count ? true : false,
    };

    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan %s", esp_err_to_name(err));
        return 1;
    }

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "num %s", esp_err_to_name(err));
        return 1;
    }

    ESP_LOGI(TAG,"Ap=%d", ap_num);

    wifi_ap_record_t *rec = alloca(sizeof(wifi_ap_record_t) * ap_num);

    err = esp_wifi_scan_get_ap_records(&ap_num, rec);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rec %s", esp_err_to_name(err));
        return 1;
    }

    for ( int n = 0; n < ap_num; n++) {
        ESP_LOGI(TAG, "ap:%d SSID=%s ch:%d rssi:%d", n, rec[n].ssid, rec[n].primary, rec[n].rssi);
    }

    return 0;
}


static int wifi_cmd_query(int argc, char** argv)
{
    wifi_config_t cfg;
    wifi_mode_t mode;

    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mode %s", esp_err_to_name(err));
        return 1;
    }

    if (WIFI_MODE_AP == mode) {
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        ESP_LOGI(TAG, "AP mode, %s %s ch:%d", cfg.ap.ssid, cfg.ap.password, cfg.ap.channel);
    } else if (WIFI_MODE_STA == mode) {
        int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);
        if (bits & CONNECTED_BIT) {
            esp_wifi_get_config(WIFI_IF_STA, &cfg);
            ESP_LOGI(TAG, "sta mode, connected %s chan:%d", cfg.sta.ssid, cfg.sta.channel);
        } else {
            ESP_LOGI(TAG, "sta mode, disconnected");
        }
    } else {
        ESP_LOGI(TAG, "NULL mode");
        return 0;
    }

    return 0;
}


static struct {
    struct arg_int *auto_conn;
    struct arg_end *end;
} auto_args;

int wifi_auto_connect_cmd(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &auto_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, auto_args.end, argv[0]);
        return 1;
    }
    bool en = true;
    if (auto_args.auto_conn->count)
        en = !!auto_args.auto_conn->ival[0];
    esp_wifi_set_auto_connect(en);
    printf("Auto connect set to %d\r\n",en);
    return 0;
}

void register_wifi()
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.timeout->ival[0] = 5000; // set default value
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",
        .help = "Join WiFi AP as a station",
        .hint = NULL,
        .func = &connect,
        .argtable = &join_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );

    init_args.nvs = arg_lit0("f","flash","flash storage");
    init_args.end = arg_end(1);

    const esp_console_cmd_t init_cmd = {
        .command = "wifi_init",
        .func = init,
        .argtable = &init_args
    };
    esp_console_cmd_register(&init_cmd);

    auto_args.auto_conn = arg_int0(NULL,NULL,"<auto>","auto");
    auto_args.end = arg_end(1);
    
    const esp_console_cmd_t auto_cmd = {
        .command = "wifi_auto",
        .help = "set wifi auto connect",
        .hint = NULL,
        .func = &wifi_auto_connect_cmd,
        .argtable = &auto_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&auto_cmd) );


    scan_args.timeout = arg_int0("t", "timeout", "<t>", "scan timeout");
    scan_args.ssid = arg_str0("s", "ssid", "<ssid>", "SSID");
    scan_args.channel = arg_int0("c","channel","<ch>", "Channel");
    scan_args.hidden = arg_lit0("h","hidden", "hidden");
    scan_args.end = arg_end(4);
    const esp_console_cmd_t scan_cmd = {
        .command = "wifi_scan",
        .func = scan,
        .argtable = &scan_args
    };
    esp_console_cmd_register(&scan_cmd);

    const esp_console_cmd_t query_cmd = {
        .command = "wifi_query",
        .help = "query WiFi info",
        .hint = NULL,
        .func = &wifi_cmd_query,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&query_cmd) );
}
