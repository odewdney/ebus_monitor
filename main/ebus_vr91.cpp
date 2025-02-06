#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

class EbusDeviceVr91 : public EbusDeviceBase
{
    float temp, humid, desiredTemp;
    uint8_t index;
    enum class Mode { Off=0, Auto=1, Day=2, Setback=3 };

    Mode mode;


    void SendReading(uint8_t reg, float val)
    {
        if ( index == 0xff ) return;
        auto msg = new EbusMessage(masterAddress, 0x15, 0xb524);
        //06010a010f00
        msg->AddPayload(0x06); // data
        msg->AddPayload(0x01); // 0-read 1-wr
        msg->AddPayload(0x0a); // 09=int, 0a=remote
        msg->AddPayload(index+1); // index

        msg->AddPayloadWord(reg); // 7=humid f=temp

        msg->AddPayloadEXP(val);
        msg->SetCRC();
        bus->QueueMessage(msg);

    }
public:
    EbusDeviceVr91(uint8_t masterAddr, EbusBus *bus)
    // SW=0415;HW=4803"
        : EbusDeviceBase(masterAddr, 0xb5, "VR_91", 0x0200, 0x1903, bus)
    {
        index = 0xff;
        temp = 16.5f;
        humid = 49.5f;
        desiredTemp = 0.0f;
        mode = Mode::Off;
    }


    bool ProcessResponse(EbusMessage const &msg, EbusResponse const &response)
    {
        auto cmd = msg.GetCmd();
        auto data = msg.GetPayload();
        auto p = *data;
        switch (cmd) {
            case 0xb524:
            {
                switch(p)
                {
                    case 8: // query
                        // resp - 08 000001010c012e30
                        if (response.GetPayloadLength() == 8){
                            index = response.GetPayload()[0];
                            mode = (Mode)response.GetPayload()[2];
                            desiredTemp = response.ReadPayloadData1c(6);
                            return true;
                        }
                }
            }
        }
        return EbusDeviceBase1::ProcessResponse(msg, response);
    }


    bool ProcessTimer(int cnt)
    {
        switch (cnt % 60) {
            case 1:
            {
                auto msg = new EbusMessage(masterAddress, 0x15, 0xb524);
                // resp - 08000001010c012e30
                msg->AddPayload(0x08); // query
                msg->SetCRC();
                bus->QueueMessage(msg);
                break;
            }
            case 10:
                SendReading(0x0f, temp);
                break;
            case 15:
                SendReading(0x07, humid);
                break;
        }
        return EbusDeviceBase::ProcessTimer(cnt);

    }

    void print()
    {
        printf("VR91: id:%02x\r\n", masterAddress);
        printf(" index:%d mode:%d desired:%.1f\r\n", index, (int)mode, desiredTemp);
        printf(" Temp:%.1f humid:%.1f\r\n", temp, humid);
    }

    /*
    r:Req: 10 35 b516 (1) Data: 11 (fc)
W (175163) VR_91: Unknown command b516

    
    */
};

//vr81 75 35 xx f5 1c 3c 7c fc

// 1-based
const static uint8_t slaveAddressess[] = 
    { 0x35, 0x75, 0xf5, 0x1c, 
      0x3c, 0x7c, 0xfc, 0x06 };
EbusDeviceVr91 *vr91[sizeof(slaveAddressess)] = {0};

// index is switch index - 1-based
EbusDevice *CreateVR91Device(uint8_t index, EbusBus *bus)
{
    if ( index == 0 || index > sizeof(slaveAddressess))
        return nullptr;
    index--;

    vr91[index] = new EbusDeviceVr91(slaveAddressess[index]-5, bus);
    return vr91[index];
}
