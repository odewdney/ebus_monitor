
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

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
enum class DisableFlags { None=0, CH=1, DHWtapping=2, DHWloading=4, };

class EbusDeviceBoiler : public EbusDeviceBase
{
    // inputs
    float tempDesired, hwcDesired;
    float stgDesired;
    HcMode hcMode;
    HwcMode hwcMode;
    DisableFlags disableFlags;

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
    EbusDeviceBoiler(uint8_t masterAddr, uint8_t idx, EbusBus *bus)
        // 0x0518, 0x7401
        : EbusDeviceBase(masterAddr, 0xb5, "BAI00", 0x0500+idx, 0x7401, bus)
    {
        // inputs
        tempDesired = 0.5;
        hwcDesired = 1.5;
        stgDesired = 2.5;
        disableFlags = DisableFlags::None;
        cirSpeed = 0;

        hcMode = HcMode::Auto;
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
                        resp->AddPayloadData2b(outsideTemp);
                        *response = resp;
                        return true;
                    }
                    case 0x10: // Status16 - outside
                        {
                            auto resp = new EbusResponse();
                            resp->AddPayloadWord(0xffff);
                            *response = resp;
                            return true;
                        };
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
                                // others: SetPointLoadingPump, CHDisableMonitor
                                auto hcModeSet = data[1];
                                auto hcTempSet = msg.ReadPayloadData1c(2);
                                auto hwTempSet = msg.ReadPayloadData1c(3);
                                auto hwcTempFlowSet = data[4];
                                // 5 unknown ff
                                auto flags = data[6]; //
                                // 7 unknown 00/ff
                                // bits 0-7: remote control CH pump/release backup heater/release cooling/not used/left stop position DHW o, bits sent in M14 
                                auto remote = data[8];

                                ESP_LOGI(name, "SetMode hcmode:%d flow:%d hwc:%d flag:%02x", hcModeSet, (int)hcTempSet, (int)hwTempSet, flags);

                                if(!std::isnan(hcTempSet))
                                    tempDesired = hcTempSet;
                                if(!std::isnan(hwTempSet))
                                    hwcDesired = hwTempSet;
                                hcMode = (HcMode) hcModeSet;
                                disableFlags = (DisableFlags) flags;

                                auto resp = new EbusResponse();
                                resp->AddPayload((uint8_t)1); // ack?
                                *response = resp;
                                return true;
                            }
                    }
                }
                break;
                //return false;
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
                break;
                //return false;
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
                break;
                //return false;
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

    void print()
    {
        printf("BAI id:%02x\r\n", masterAddress);
        printf("Mode hc:%d hwc:%d\r\n", (int)hcMode, (int)hwcMode);
        printf("Desired temp:%.2f hwc:%.2f stg:%.2f\r\n", tempDesired, hwcDesired, stgDesired);
        printf("Flags disable:%02x\r\n", (int)disableFlags);
    }

    int SetSensorsCmd(int argc, char**argv);
};

EbusDeviceBoiler *bai[8] = {nullptr};

EbusDevice *CreateBAI(EbusBus *bus, uint8_t index)
{
    auto b = new EbusDeviceBoiler(0x03, index, bus);
    bai[index] = b;
    return b;
}

static struct {
    struct arg_int *num;
    struct arg_end *end;
} bai_print_args;

int bai_print_func(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &bai_print_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, bai_print_args.end, argv[0]);
        return 1;
    }

    int index = 1;

    if (bai_print_args.num->count)
        index = bai_print_args.num->ival[0];

    if (index < 1 || index > sizeof(bai))
        return -1;

    index--;
    if (bai[index])
        return -1;

    bai[index]->print();
    return 0;
}

static struct {
    struct arg_int *num;
    struct arg_dbl *outside;
    struct arg_end *end;
} bai_set_args;

int bai_set_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &bai_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, bai_set_args.end, argv[0]);
        return 1;
    }

    int index = 1;

    if (bai_set_args.num->count)
        index = bai_set_args.num->ival[0];

    if (index < 1 || index > sizeof(bai))
        return -1;

    index--;
    if (bai[index])
        return -1;

    return bai[index]->SetSensorsCmd(argc, argv);    return 0;
}

int EbusDeviceBoiler::SetSensorsCmd(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &bai_set_args);

    if ( bai_set_args.outside->count) {
        outsideTemp = bai_set_args.outside->dval[0];
    }
    return 0;
}

void register_bai_cmds()
{
    bai_print_args.num = arg_int0("n",NULL, "<num>", "index");
    bai_print_args.end = arg_end(5);

    const esp_console_cmd_t print_bai_cmd = {
        .command = "bai_print",
        .help = "bai print",
        .hint = NULL,
        .func = &bai_print_func,
        .argtable = &bai_print_args,
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&print_bai_cmd));

    bai_set_args.num = bai_print_args.num;
    bai_set_args.outside = arg_dbl0("o","outside", "<flt>", "outside temp");
    bai_set_args.end = bai_print_args.end;

    const esp_console_cmd_t set_bai_cmd = {
        .command = "bai_set",
        .help = "bai set",
        .hint = NULL,
        .func = &bai_set_func,
        .argtable = &bai_set_args,
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&set_bai_cmd));
}
