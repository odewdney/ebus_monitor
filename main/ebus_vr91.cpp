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
public:
    EbusDeviceVr91(uint8_t masterAddr, EbusBus *bus)
    // SW=0415;HW=4803"
        : EbusDeviceBase(masterAddr, 0xb5, "VR_91", 0x0200, 0x1903, bus)
    {

    }
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

    vr91[index] = new EbusDeviceVr91(slaveAddressess[index], bus);
    return vr91[index];
}
