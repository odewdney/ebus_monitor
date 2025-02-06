#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "ebus.h"

#include "ebus_dev.h"
#include "ebus_device.h"

#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_console.h"


class EbusDeviceVr70 : public EbusDeviceBase1
{
protected:
    bool relay[5];
    enum SensorMode { Sensor_VR10 = 0, Sensor_VR11 = 1, Sensor_Contact = 2, Sensor_PWM = 3};

    struct {
        enum SensorMode mode;
        float value;
    } sensors[6];

    uint8_t s7Out;

    struct {
        bool enabled;
        bool active;
        float desired;
        uint8_t pos;
    } mixers[2];

public:

    EbusDeviceVr70(uint8_t slaveAddr)
        : EbusDeviceBase1(slaveAddr, 0xb5, "VR_70", 0x0200, 0x1903)
    {
        int n;
        for(n=0;n<5;n++) relay[n] = false;
        for(n=0;n<6;n++) { sensors[n].mode = Sensor_VR10; sensors[n].value = (n+1)*10; }
        for(n=0; n<2; n++) { mixers[n].enabled = false; mixers[n].pos = 30 + n*10; }
        s7Out = 0;
    }

    bool ProcessSlaveMessage(EbusMessage const &msg, EbusResponse **response)
    {
        auto cmd = msg.GetCmd();
        auto data = msg.GetPayload();
        uint8_t p = *data;
        switch (cmd) {
            case 0xb503://  10 52 b503 (12) Data: 07 00 ff ff ff ff ff ff ff ff ff ff 
                ESP_LOGI(name, "probe");
                *response = new EbusResponse();
                (*response)->AddPayload(1);
                return true;
                //break;
            case 0xb523:
                switch(p) {
                    case 0: //  10 52 b523 (9) Data: 00 00 01 00 02 02 00 00 00 (43)
                        ESP_LOGI(name,"Set config");
                        if (msg.GetPayloadLength() == 9) {
                            mixers[0].enabled = !!data[1];
                            mixers[1].enabled = !!data[2];
                            for(int n = 0; n < 6; n++) {
                                sensors[n].mode = (enum SensorMode)data[3+n];
                            }
                            *response = new EbusResponse();
                            (*response)->AddPayload(1);
                            return true;
                        }
                        break;
                    case 1: // SetActorState
                        ESP_LOGI(name,"Set Actor");
                        if (msg.GetPayloadLength() == 8) {
                            for (int n = 0; n<6; n++) {
                                auto c = data[1+n];
                                if (c != 0xff) {
                                    // normally 20d, but test-actuator 0xfe
                                    relay[n] = !!c;
                                }
                            }
                            // 1 byte for s7/PWM?
                            if (data[7] != 0xff) {
                                s7Out = data[7];
                            }
                            *response = new EbusResponse();
                            (*response)->AddPayload(1);
                            return true;
                        }
                        break;
                    case 2: // desiered temp
                        ESP_LOGI(name,"set mix");
                        if (msg.GetPayloadLength() == 4) {
                            uint8_t index = data[1];
                            if (index < 2) {
                                mixers[index].active = !!data[2];
                                mixers[index].desired = msg.ReadPayloadData1c(3);
                                *response = new EbusResponse();
                                (*response)->AddPayload(1);
                                (*response)->AddPayload(mixers[index].pos);
                                return true;
                            }
                        }
                        break;
                    case 3: // read sensor 
                        *response = new EbusResponse();
                        for(auto n = 0; n < 6; n++)
                            (*response)->AddPayloadData2c( sensors[n].value);
                        (*response)->AddPayload(0); // s7 in?
                        (*response)->AddPayload(0);
                        (*response)->AddPayload(0);
                        return true;
                }
                break;
            case 0xb516: // unknown - simple return
                // 10 ?
                if ( p == 0x11) { // read 8 bytes
                    *response = new EbusResponse();
                    (*response)->AddPayload(0);
                    return true;
                }
                break;
        }
        return EbusDeviceBase1::ProcessSlaveMessage(msg, response);
    }

    void print()
    {
        printf("Name: %s id:%2x\r\n", name, slaveAddress);
        int n;
        printf("Relay:");
        for(n=0;n<5;n++)
            printf(" R%d:%d", n, relay[n]);
        printf("\r\nMixer:");
        for(n=0;n<2;n++){
            printf(" M%d: e:%d", n, mixers[n].enabled);
            if (mixers[n].enabled)
                printf(" a:%d d:%d.%d p:%d", mixers[n].active, (int)(mixers[n].desired), ((int)mixers[n].desired*10)%10, mixers[n].pos);
        }
        printf("\r\nSensors:");
        for(n=0;n<6;n++)
            printf(" S%d(%d):%d.%d", n, sensors[n].mode, (int)(sensors[n].value), ((int)sensors[n].value*10)%10);
        printf("\r\n");
    }

    void SetSensor(int index, float val)
    {
        sensors[index].value = val;
    }

};

const static uint8_t slaveAddressess[] = {0x52};
EbusDeviceVr70 *vr70[sizeof(slaveAddressess)] = {0};

EbusDevice *CreateVR70Device(uint8_t sw)
{
    if (sw >= sizeof(slaveAddressess))
        return nullptr;

    vr70[sw] = new EbusDeviceVr70(slaveAddressess[sw]);
    return vr70[sw];
}

static struct {
    struct arg_int *num;
    struct arg_end *end;
} vr70_print_args;


int print_vr70_func(int argc, char**argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &vr70_print_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, vr70_print_args.end, argv[0]);
        return 1;
    }

    int index = 0;

    if (vr70_print_args.num->count)
        index = vr70_print_args.num->ival[0];

    if ( index < 0 || index >= sizeof(slaveAddressess))
        return 1;

    if (vr70[index] == nullptr) {
        printf("No device\r\n");
        return 1;
    }

    vr70[index]->print();
    return 0;
}


static struct {
    struct arg_int *num;
    struct arg_int *sensor;
    struct arg_dbl *value;
    struct arg_end *end;
} vr70_sensor_args;

int sensor_vr70_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &vr70_sensor_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, vr70_sensor_args.end, argv[0]);
        return 1;
    }

    int index = 0;

    if (vr70_sensor_args.num->count)
        index = vr70_print_args.num->ival[0];

    if ( index < 0 || index >= sizeof(slaveAddressess))
        return 1;

    if (vr70[index] == nullptr) {
        printf("No device\r\n");
        return 1;
    }

    auto sensor = vr70_sensor_args.sensor->ival[0];
    if (sensor<0 || sensor>6)
        return 1;
    vr70[index]->SetSensor(sensor, vr70_sensor_args.value->dval[0]);
    return 0;
}

void register_vr70_cmds()
{
    vr70_print_args.num = arg_int0("n",NULL, "<num>", "index");
    vr70_print_args.end = arg_end(1);

    const esp_console_cmd_t print_vr70_cmd = {
        .command = "vr70_print",
        .help = "VR71 print",
        .hint = NULL,
        .func = &print_vr70_func,
        .argtable = &vr70_print_args,
    };


    ESP_ERROR_CHECK( esp_console_cmd_register(&print_vr70_cmd));

    vr70_sensor_args.num = vr70_print_args.num;
    vr70_sensor_args.sensor = arg_int1(NULL,NULL,"<index>", "sensor");
    vr70_sensor_args.value = arg_dbl1(NULL, NULL, "<value>", "value");
    vr70_sensor_args.end = vr70_print_args.end;

    const esp_console_cmd_t sensor_vr70_cmd = {
        .command = "vr70_sensor",
        .help = "VR71 set sensor",
        .hint = NULL,
        .func = &sensor_vr70_func,
        .argtable = &vr70_sensor_args,
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&sensor_vr70_cmd));

}

