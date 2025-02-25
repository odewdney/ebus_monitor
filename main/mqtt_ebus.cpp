#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <alloca.h>

#include "nvs.h"

#include "esp_log.h"

#include "mqtt_client.h"
#include "ebus.h"
#include "ebus_dev.h"

static const char *TAG = "MQTT_EBUS";


esp_err_t nvs_get_string(nvs_handle handle, const char*key, std::string &str)
{
    size_t len = 0;
    esp_err_t err;

    err = nvs_get_str(handle, key, nullptr, &len);
    if ( err != ESP_OK && err != ESP_ERR_NVS_INVALID_LENGTH ) {
        return err;
    }

    str.resize(len);
    return nvs_get_str(handle, key, &str[0], &len);
}


class MqttMonitor : public EbusMonitor
{

protected:
    esp_mqtt_client_handle_t client = nullptr;
    // needed to persist for lifetime of client
    std::string server_name;
    std::string username;
    std::string password;

    esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
    {
        esp_mqtt_client_handle_t client = event->client;
        int msg_id;
        // your_context_t *context = event->context;
        switch (event->event_id) {
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
                msg_id = esp_mqtt_client_subscribe(client, "/ebus/req", 0);
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
                /*
                msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
                ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

                msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

                msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

                msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
                ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
                */
                break;
            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
                break;

            case MQTT_EVENT_SUBSCRIBED:
                ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
                //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
                //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
                break;
            case MQTT_EVENT_UNSUBSCRIBED:
                ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
                break;
            case MQTT_EVENT_PUBLISHED:
    //            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
                break;
            case MQTT_EVENT_DATA:
                ESP_LOGI(TAG, "MQTT_EVENT_DATA");
                printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
                printf("DATA=%.*s\r\n", event->data_len, event->data);
                break;
            case MQTT_EVENT_ERROR:
                ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
                break;
            default:
                ESP_LOGI(TAG, "Other event id:%d", event->event_id);
                break;
        }
        return ESP_OK;
    }

    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
    {
        ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
        auto mon = (MqttMonitor *)handler_args;
        mon->mqtt_event_handler_cb((esp_mqtt_event_handle_t)event_data);
    }

public:
    MqttMonitor(EbusSender *sender)
    {}

    void start(void)
    {
        esp_mqtt_client_config_t mqtt_cfg = {};
        nvs_handle_t handle;
        esp_err_t err;

        err = nvs_open("mqtt", NVS_READONLY, &handle);
        if ( err != ESP_OK) {
            ESP_LOGI(TAG, "Not init MQTT %d", err);
            return;
        }

        err = nvs_get_string(handle, "url", server_name);
        if ( err != ESP_OK) {
            ESP_LOGI(TAG, "No Url MQTT %d", err);
            goto close_handle;
        }
        if(server_name.empty()) {
            ESP_LOGI(TAG, "No empty MQTT");
            goto close_handle;
        }

        err = nvs_get_string(handle, "username", username);
        err = nvs_get_string(handle, "password", password);
        
        mqtt_cfg.uri = server_name.c_str();
        mqtt_cfg.username = username.empty() ? nullptr : username.c_str();
        mqtt_cfg.password = password.empty() ? nullptr : password.c_str();

        client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event_handler, this);
        esp_mqtt_client_start(client);

    close_handle:
        nvs_close(handle);
    }

    void NotifyBroadcast(EbusMessage const &msg)
    {
        if ( !client ) return;

        auto len = msg.GetBufferLength();

        auto buffer = (char*)alloca(len*2+1);
        auto p = WriteHex(buffer, msg.GetBuffer(), len);
        *p = 0;
        len = len * 2;

        esp_mqtt_client_publish( client, "ebus/data", buffer, len, 1, 0);
    }

    static char *WriteHex(char *buffer, uint8_t const *data, size_t len)
    {
        for(auto n=0;n<len;n++) {
            auto nibble = (*data >> 4) & 0xf;
            *buffer++ = '0' + nibble + (nibble>9? 'a'-'9'-1 :0);
            nibble = (*data++) & 0xf;
            *buffer++ = '0' + nibble + (nibble>9? 'a'-'9'-1 :0);
        }
        return buffer;
    }


    void Notify(EbusMessage const &msg, EbusResponse const &response)
    {
        if ( !client ) return;

        auto msgLen = msg.GetBufferLength();
        auto respLen = response.GetBufferLength();
        size_t len =  msgLen + respLen;

        auto buffer = (char*)alloca(len*2+2);
        auto p = WriteHex(buffer, msg.GetBuffer(), msgLen);
        *p++ = ' ';
        p = WriteHex(p, response.GetBuffer(), respLen);
        *p = 0;
        len = len * 2+1;

        auto msgid = esp_mqtt_client_publish( client, "ebus/data", buffer, len, 1, 0);
        ESP_LOGI(TAG, "Sent %d msg %d", len, msgid);
    }

};

extern "C" {
    void mqtt_app_start(void);
}

EbusMonitor *initialise_mqtt(EbusSender *sender)
{
    auto mon = new MqttMonitor(sender);
    mon->start();
    return mon;
}