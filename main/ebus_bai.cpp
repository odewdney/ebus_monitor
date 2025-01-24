
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"

enum class HcMode : uint8_t { Auto = 0, Off = 1, Heat = 2, Water = 3};
enum class HwcMode : uint8_t { Disabled=0, On=1, Off=2, Auto=3 };
enum class StateCode : uint8_t { 
    // heating
    NoHeatDemand=0, FanOn=1, PumpRunning=2, Ignition=3, Burner=4, PumpFanOverrun=5, FanOverrun=6, PumpOverrun=7, Shutdown=8,
    // hw
    HwRequired=10, HwFanOn=11, HwIgnition=13, HwBurner=14, HwPumpFanOverrun=15, HwFanOverrun=16, HwPumpOverrun=17,
    // hw cyl
    HwcRequired=20, HwcFanOn=21, HwcPumpRunning=22, HwcIgnition=23, HwcBurner=24, HwcPumpFanOverrun=25, HwcFanOverrun=26, HwcPumpOverrun=27, HwcShutdown=28,
    // Other
    NoCallForHeat=30, 
     };

class EbusDeviceBoiler : public EbusDeviceBase
{
    // inputs
    float tempDesired, hwcDesired;
    bool hcDisabled, hwcDisabled, stgDesired;
    HcMode hcMode;
    HwcMode hwcMode;
    uint8_t cirSpeed;

    // sensors
    float flowTemp, retTemp;
    float hwcTemp, hwcFlowTemp;
    float stgTemp;
    float outsideTemp;
    float pressure;

    // outputs
    bool fan, gas, pump;
    StateCode state;

public:
    EbusDeviceBoiler(uint8_t masterAddr, EbusBus *bus)
        : EbusDeviceBase(masterAddr, 0xb5, "BAI00", 0x0518, 0x7401, bus)
    {
        // inputs
        tempDesired = 0.5;
        hwcDesired = 1.5;
        stgDesired = 2.5;
        hcDisabled = true;
        hwcDisabled = true;
        cirSpeed = 0;

        hcMode = HcMode::Auto; //0=auto, 1=off, 2=heat, 3=water
        hwcMode = HwcMode::Auto;

        // sensors
        flowTemp = 71.5;
        retTemp = 55.5;
        hwcTemp = 61.5;
        hwcFlowTemp = 65.5;
        stgTemp = 63.5;
        pressure = 1.5;
        outsideTemp = 13.5;

        // outputs
        state = StateCode::NoHeatDemand;
        fan = gas = pump = false;

    }
    
    bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response)
    {
        auto cmd = msg.GetCmd();
        auto data = msg.GetPayload();
        switch (cmd) {
            case 0xb504: // read
                switch(data[0]) {
                    case 0: // datetime - only primary boiler
                    {
                    // 10 08 b504 01 00 / 0a 00 000000ffffffff 000e
                        auto resp = new EbusResponse();
                        resp->AddPayload(0); // dcfstate
                        resp->AddPayload(0); // s
                        resp->AddPayload(0); // m
                        resp->AddPayload(0); // h
                        resp->AddPayload(0xff); // d
                        resp->AddPayload(0xff); // m
                        resp->AddPayload(0xff); // d
                        resp->AddPayload(0xff); // y
                        resp->AddPayloadData2c(outsideTemp);
                        *response = resp;
                        return true;
                    }
                    case 0x10: // outside
                        break;
                }
                break;
            case 0xb505: // write
                ESP_LOGI(name, "Write DHW");
                break;
            case 0xb510: // Set Mode
                if (msg.GetPayloadLength() > 0) {
                    uint8_t id = data[0];
                    switch(id) {
                        case 0: // SetMode
                            if (msg.GetPayloadLength() == 9) {
                                //   0  1  2  3  4  5  6  7  8
                                //  00 00 00 76 ff ff 01 00 00
                                //  00 00 3c 76 ff ff 00 ff 00
                                //  00 00 ff ff ff ff 05 00 00
                                auto hcModeSet = data[1];
                                auto hcTempSet = msg.ReadPayloadData1c(2);
                                auto hwTempSet = msg.ReadPayloadData1c(3);
                                auto hwcTempFlowSet = data[4];
                                // 5 unknown ff
                                auto flags = data[6]; //
                                // 7 unknown 00/ff
                                auto remote = data[8];

                                ESP_LOGI(name, "SetMode hcmode:%d flow:%d hwc:%d flag:%02x", hcModeSet, (int)hcTempSet, (int)hwTempSet, flags);

                                if(!std::isnan(hcTempSet))
                                    tempDesired = hcTempSet;
                                if(!std::isnan(hwTempSet))
                                    hwcDesired = hwTempSet;

                                hcDisabled = flags & 1;
                                hwcDisabled = flags & 4;
                                hcMode = (HcMode) data[1];

                                auto resp = new EbusResponse();
                                resp->AddPayload((uint8_t)1); // ack?
                                *response = resp;
                                return true;
                            }
                    }
                }
                return false;
            case 0xb511: // Read Status
                if (msg.GetPayloadLength() == 1) {
                    uint8_t id = data[0];
                    switch (id) {
                        case 0:
                            { // 08 ee010800 1e000000
                                auto resp = new EbusResponse();
                                resp->AddPayloadData2c(flowTemp); // flow
                                resp->AddPayload(pressure*10); // pressure10
                                resp->AddPayload(0); // unknown
                                resp->AddPayload((uint8_t)state); // state
                                resp->AddPayload((fan?1:0)|(gas?6:0)|(pump?8:0)); // bits
                                resp->AddPayload(0); // errors
                                resp->AddPayload(0); // running 1=hcDemand 2=Blocked 64/128=hwcDemand?
                                *response = resp;
                                return true;
                            }
                            break;
                        case 1:
                            { // 1008b511010189 00 09 403e000a3c3e0000ff 01 00
                            // 09 3c38 0007 343a 0000ff
                                auto resp = new EbusResponse();
                                resp->AddPayloadData1c(flowTemp); // flow
                                resp->AddPayloadData1c(retTemp); // return
                                resp->AddPayloadData2b(outsideTemp); // frc out
                                resp->AddPayloadData1c(hwcTemp); // hwcTemp
                                resp->AddPayloadData1c(stgTemp); // storageTemp
                                resp->AddPayload(1); // pump
                                resp->AddPayload(0); // unknown
                                resp->AddPayload(Ebus::BYTE_REPLACEMENT); // unknown
                                *response = resp;
                                return true;
                            }
                            break;
                        case 2:
                            { // 1008b51101028a 00 05 033c864676 2f 00
                            // 05 03 3c 86 46 76
                                auto resp = new EbusResponse();
                                resp->AddPayload((uint8_t)hwcMode); // hwcmode
                                resp->AddPayload((uint8_t)hwcDesired); // t0
                                resp->AddPayloadData1c(22.5); // t1
                                resp->AddPayload((uint8_t)27.5); // t0
                                resp->AddPayloadData1c(stgDesired); // t1
                                *response = resp;
                                return true;
                            }
                            break;
                    }
                }
                return false;
            case 0xb512:
                { // 1008b512020064 ae00 00 00 00
                if (msg.GetPayloadLength() > 0) {
                    uint8_t id = data[0];
                    switch(id) {
                        case 0: // circ pump
                            if (msg.GetPayloadLength() == 2) {
                                ESP_LOGI(name, "Set circ %02x", data[1]);
                                cirSpeed = data[1];
                                auto resp = new EbusResponse();
                                resp->AddPayload((uint8_t)0); // ack?
                                *response = resp;
                                return true;
                            }
                            break;
                        case 4: // unknown
                            if (msg.GetPayloadLength() == 2) {
                                ESP_LOGI(name, "Set xxx %02x", data[1]);
                                auto resp = new EbusResponse();
                                resp->AddPayload(1); // ?
                                resp->AddPayload(1); // ?
                                *response = resp;
                                return true;
                            }
                            break;
                    }
                }
                }
                return false;
            case 0xb513:
                break;
            case 0xb516: // unknown - simple return
                auto p = *data;
                if ( p == 0x11) {
                    *response = new EbusResponse();
                    (*response)->AddPayload(0);
                    return true;
                }

        }
        return EbusDeviceBase::ProcessSlaveMessage(msg, response);
    }

};

EbusDevice *CreateBAI(EbusBus *bus)
{
    return new EbusDeviceBoiler(0x03, bus);
}
