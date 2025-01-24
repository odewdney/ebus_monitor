#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"

bool EbusDeviceBase1::ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response) 
{
    auto cmd = msg.GetCmd();
    auto len = msg.GetPayloadLength();
    switch(cmd)
    {
        case 0x0704:
            if(len==0)
            {
                *response = new EbusResponse();
                WriteID(**response, manu, name, sw, hw);
                return true;
            }
            break;
        default:
            ESP_LOGW(name, "Unknown command %04x", cmd);
            if ((cmd&0xff00)==0xb500) {

            }
    }
    return false;
}


bool EbusDeviceBase::ProcessTimer(int cnt)
{
    if ( (cnt % 60) == 0 ) {
        ESP_LOGI(name,"Sending ID");
        auto cmd = new EbusMessage(masterAddress, BROADCAST_ADDR, 0x0704);
        WriteID(*cmd, manu, name, sw, hw);
        cmd->SetCRC();
        bus->QueueMessage(cmd);
    }

    return true;
}



void EbusDeviceBase::processMasterTimer(TimerHandle_t handle)
{

    auto dev = (EbusDeviceBase*)pvTimerGetTimerID(handle);

    //if (cmd_retry != 0) return;

    if (dev->ProcessTimer(dev->cnt))
        dev->cnt++;
}


void EbusDeviceBase::start()
{
    ESP_LOGI(name, "Starting");
    cnt = masterAddress % 60;
    bcastTimer = xTimerCreate(name, 1000/portTICK_PERIOD_MS, true, this, processMasterTimer);
    auto ret = xTimerStart( bcastTimer, 0);
    if ( ret == pdFAIL ) {
        ESP_LOGE(name, "Failed to start timer");
        ret = xTimerStart( bcastTimer, 10);
        if ( ret == pdFAIL )
            ESP_LOGE(name, "Failed to start timer again");
    }
}
