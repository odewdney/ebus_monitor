#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"

class EbusDeviceVr65 : public EbusDeviceBase1
{
    uint8_t state; // ch1, dhw, dhwoff/ch2
    uint8_t sel;
    bool cyl;
    bool ntc_en;
    float ntc;
public:

    EbusDeviceVr65(bool isVr66, uint8_t s)
        : EbusDeviceBase1(0x64, 0xb5, "V6500", 0x0107, 0x7602)
    {
        sel = s;
        state = 0;
        cyl = false;
        ntc_en = false;
        ntc = 0;
    }

    bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response)
    {
        auto cmd = msg.GetCmd();
        auto data = msg.GetPayload();
        uint8_t p = *data;
        switch (cmd) {
            case 0xb512:
                if (p == 2) {
                    if (msg.GetPayloadLength() == 2) {
                        // 0 off, 50= 100= 0xfe=
                        state = data[1];
                        ESP_LOGI(name, "Set Config %02x", state);
                        // response 6 bytes
                        *response = new EbusResponse();
                        (*response)->AddPayload(state);
                        (*response)->AddPayload(cyl);
                        if ( ntc_en)
                            (*response)->AddPayloadData2c(ntc);
                        else
                            (*response)->AddPayloadWord(0x8000);
                        (*response)->AddPayloadWord(0xffff);
                        return true;
                    }

                }
                break;
        }
        return EbusDeviceBase1::ProcessSlaveMessage(msg, response);
    }

    void print()
    {
        printf("VR65(%d) State: %02x c=%d ntc=%d\r\n", sel, state, cyl, (int)ntc);
    }

    void SetCyl(bool f)
    {
        cyl = f;
    }

    void SetNTC(float f)
    {
        ntc_en = true;
        ntc = f;
    }
};

EbusDeviceVr65 *vr65 = nullptr;

EbusDevice *CreateVR65Device(bool isVr66, uint8_t sw)
{
    vr65 = new EbusDeviceVr65(isVr66, sw);
    return vr65;
}

int vr65_print_func(int argc, char**argv)
{
    if (!vr65)
        return 1;
    vr65->print();
    return 0;
}

struct {
    struct arg_int *cyl;
    struct arg_dbl *ntc;
    struct arg_end *end;
} vr65_sensor_args;

int sensor_vr65_func(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &vr65_sensor_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, vr65_sensor_args.end, argv[0]);
        return 1;
    }

    if (!vr65)
        return 1;

    if (vr65_sensor_args.cyl->count)
        vr65->SetCyl(vr65_sensor_args.cyl->ival[0]);

    if (vr65_sensor_args.ntc->count)
        vr65->SetNTC(vr65_sensor_args.ntc->dval[0]);

    return 0;
}

void register_vr65_cmds()
{

    const esp_console_cmd_t print_vr65_cmd = {
        .command = "vr65_print",
        .help = "VR65 print",
        .hint = NULL,
        .func = &vr65_print_func,
        .argtable = NULL,
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&print_vr65_cmd));

    vr65_sensor_args.cyl = arg_int0("c",NULL, "<cyl>", "Cyl value");
    vr65_sensor_args.ntc = arg_dbl0(NULL, NULL, "<ntc>", "NTC Temp");
    vr65_sensor_args.end = arg_end(1);

    const esp_console_cmd_t sensor_vr65_cmd = {
        .command = "vr65_sensor",
        .help = "VR65 set sensor",
        .hint = NULL,
        .func = &sensor_vr65_func,
        .argtable = &vr65_sensor_args,
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&sensor_vr65_cmd));

}

