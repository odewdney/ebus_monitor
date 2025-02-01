
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"

class EbusDeviceBridge : public EbusDeviceBridgeBase
{
    bool ACKed;
    EbusResponseWriter prxResponse;
protected:
    void SendACK() {ACKed=true;}
    void SendNAK() {}
    void SendResponse(EbusResponse const &response)
    {
        auto m = response.GetPayloadLength();
        //ESP_LOGI(name, "got response len %d", m);
        prxResponse.Reset();
        auto p = response.GetPayload();
        prxResponse.Write(m);
        for( int n = 0; n < m; n++)
            prxResponse.Write(*p++);
    }
public:
    EbusDeviceBridge(uint8_t addr, EbusBus *b)
        : EbusDeviceBridgeBase(addr, 0xb5, "V32  ", 0x117, 0x9802 ,b)
    {}

    bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response)
    {
        auto cmd = msg.GetCmd();
        switch (cmd) {
            case 0xb517:
            {
                EbusMessageWriter prxMsg;
                prxMsg.Write(masterAddress);
                auto m = msg.GetPayloadLength();
                for(int n = 0; n < m; n++)
                    prxMsg.Write(msg.GetPayload()[n]);
                prxMsg.SetCRC();
                //printf("prox req:");
                prxMsg.print();
                ACKed = false;
                prxResponse.Reset();
                ProcessDeviceMessage(prxMsg);
                if (!prxResponse.IsEmpty()) {
                    auto prxMsg = new EbusMessage(masterAddress, msg.GetSource(), 0xb518);
                    auto m = prxResponse.GetPayloadLength();
        //ESP_LOGI(name, "sending response len %d", m);
                    for( int n = 0; n < m; n++)
                        prxMsg->AddPayload(prxResponse.GetPayload()[n]);
                    prxMsg->SetCRC();
                    //printf("queued response: ");
                    prxMsg->print();
                    bus->QueueMessage(prxMsg);
                }

                return ACKed;
            }
            case 0xb510: // write state
            case 0xb511: // read state
            case 0xb512: // read
            case 0xb513: // ?
            case 0xb516: // ?
            { 
                EbusMessageWriter prxMsg;
                prxMsg.Write(masterAddress);
                prxMsg.Write(0x08);
                prxMsg.Write(cmd>>8);
                prxMsg.Write(cmd & 0xff);
                auto m = msg.GetPayloadLength();
                prxMsg.Write(m);
                for(int n = 0; n < m; n++)
                    prxMsg.Write(msg.GetPayload()[n]);
                prxMsg.SetCRC();
                //printf("prox req:");
                //prxMsg.print();
                ACKed = false;
                prxResponse.Reset();
                ProcessDeviceMessage(prxMsg);
                if (ACKed && !prxResponse.IsEmpty()) {
                    auto res = new EbusResponse(prxResponse);
                    *response = res;
                }
                return ACKed;
            }
            break;
        }
        return EbusDeviceBase::ProcessSlaveMessage(msg, response);
    }

    void ProcessBroadcastMessage(EbusMessage const &msg)
    {
        EbusDevice::ProcessBroadcastMessage(msg);
    }


    void QueueMessage(EbusMessage const *msg)
    {
        printf("proxy queue:");
        msg->print();
        delete msg;
    }

    void start()
    {
        EbusDeviceBase::start();
        ESP_LOGI(name, "Starting bus");
        for(auto dev : devices) {
            ESP_LOGI(name,"starting %s", dev->GetName());
            dev->start();
        }
    }

};

// 1-based
static const uint8_t VR32_addr[] = {0,0x13,0x33};

EbusDeviceBridgeBase *CreateBridgeDevice(uint8_t index, EbusBus *bus)
{
    if (index < 2 || index > sizeof(VR32_addr))
        return nullptr;

    index--;

    return new EbusDeviceBridge(VR32_addr[index], bus);
}

